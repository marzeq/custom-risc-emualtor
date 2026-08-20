#include <assert.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "isa.h"
#include "mmio_plugin.h"

#define KiB(x) ((x) * 1024)
#define MiB(x) ((x) * 1024 * 1024)

#define FLAG_ZERO       (1ull << 0)
#define FLAG_LESS       (1ull << 1)
#define FLAG_GREATER    (1ull << 2)

#define FLAG_CARRY      (1u << 3)
#define FLAG_OVERFLOW   (1ull << 4)

#define FLAG_INT_ENABLE (1ull << 16)

static inline void set_arithmetic_flags(
  u64 *flags,
  bool carry,
  bool overflow
) {
  const u64 mask =
    FLAG_CARRY |
    FLAG_OVERFLOW;

  u64 new_flags = *flags & ~mask;

  if (carry)    new_flags |= FLAG_CARRY;
  if (overflow) new_flags |= FLAG_OVERFLOW;

  *flags = new_flags;
}

typedef struct {
  u64 version;
  u64 self_size;

  u64 ram_start;
  u64 ram_size;

  u64 firmware_rom_start;
  u64 firmware_rom_size;

  u64 device_count;
  u64 device_size;
  u64 device_list;
} machine_info;

typedef struct {
  u64 type;
  u64 start;
  u64 size;
  u8 name[16];
} device_info;

typedef struct {
  void* library;
  const mmio_plugin_descriptor* plugin;
  device_info info;
} loaded_device;

static loaded_device* mmio_devices;

static u64 read_u64_le(const u8* bytes) {
  return (u64)bytes[0]
    | ((u64)bytes[1] << 8)
    | ((u64)bytes[2] << 16)
    | ((u64)bytes[3] << 24)
    | ((u64)bytes[4] << 32)
    | ((u64)bytes[5] << 40)
    | ((u64)bytes[6] << 48)
    | ((u64)bytes[7] << 56);
}

static void write_u64_le(u8* bytes, u64 value) {
  bytes[0] = (u8)(value & 0xFFu);
  bytes[1] = (u8)((value >> 8) & 0xFFu);
  bytes[2] = (u8)((value >> 16) & 0xFFu);
  bytes[3] = (u8)((value >> 24) & 0xFFu);
  bytes[4] = (u8)((value >> 32) & 0xFFu);
  bytes[5] = (u8)((value >> 40) & 0xFFu);
  bytes[6] = (u8)((value >> 48) & 0xFFu);
  bytes[7] = (u8)((value >> 56) & 0xFFu);
}

static u32 read_u32_le(const u8* bytes) {
  return (u32)bytes[0]
    | ((u32)bytes[1] << 8)
    | ((u32)bytes[2] << 16)
    | ((u32)bytes[3] << 24);
}

static void write_u32_le(u8* bytes, u32 value) {
  bytes[0] = (u8)(value & 0xFFu);
  bytes[1] = (u8)((value >> 8) & 0xFFu);
  bytes[2] = (u8)((value >> 16) & 0xFFu);
  bytes[3] = (u8)((value >> 24) & 0xFFu);
}

static u16 read_u16_le(const u8* bytes) {
  return (u16)bytes[0]
    | ((u16)bytes[1] << 8);
}

static void write_u16_le(u8* bytes, u16 value) {
  bytes[0] = (u8)(value & 0xFFu);
  bytes[1] = (u8)((value >> 8) & 0xFFu);
}

static u8 read_u8(const u8* p) {
  return *p;
}

static void write_u8(u8* p, u8 value) {
  *p = value;
}

typedef enum {
  FIRMWARE_ROM = 0,
  MACHINE_INFO_ROM = 1,
  DEVICE_INFO_ROM = 2,
  RAM = 3,
  MMIO = 4,
} memory_region;

static memory_region get_memory_region_type(
  u64 address,
  u64 firmware_rom_size,
  u64 machine_info_rom_size,
  u64 device_info_rom_size,
  u64 ram_size,
  u64 mmio_size
) {
  if (address < firmware_rom_size) {
    return FIRMWARE_ROM;
  } else if (address < firmware_rom_size + machine_info_rom_size) {
    return MACHINE_INFO_ROM;
  } else if (address < firmware_rom_size + machine_info_rom_size + device_info_rom_size) {
    return DEVICE_INFO_ROM;
  } else if (address < firmware_rom_size + machine_info_rom_size + device_info_rom_size + ram_size) {
    return RAM;
  } else if (address < firmware_rom_size + machine_info_rom_size + device_info_rom_size + ram_size + mmio_size) {
    return MMIO;
  } else {
    return -1; // Invalid memory region
  }
}

static loaded_device* find_device(loaded_device* devices, u64 device_count, u64 address) {
  for (u64 i = 0; i < device_count; i++) {
    loaded_device* d = &devices[i];

    if (address >= d->info.start && address < d->info.start + d->info.size) {
      return d;
    }
  }

  return NULL;
}

static bool mmio_read(
  const device_info* devices,
  u64 device_count,
  u64 address,
  u64* out_value
) {
  (void)devices;
  loaded_device* device = find_device(mmio_devices, device_count, address);

  if (!device) {
    return false;
  }

  return device->plugin->read(address - device->info.start, out_value);
}

static bool mmio_write(
  const device_info* devices,
  u64 device_count,
  u64 address,
  u64 value
) {
  (void)devices;
  loaded_device* device = find_device(mmio_devices, device_count, address);

  if (!device) {
    return false;
  }

  return device->plugin->write(address - device->info.start, value);
}

#define DEFINE_MEMORY_READ(bits, type, read_fn)               \
static bool read_u##bits##_memory(                            \
  u64 address,                                                \
  u64 firmware_rom_size,                                      \
  u64 machine_info_rom_size,                                  \
  u64 device_info_rom_size,                                   \
  u64 ram_size,                                               \
  u64 mmio_size,                                              \
  u8* firmware_rom,                                           \
  machine_info* machine_info,                                 \
  device_info* devices,                                       \
  u8* ram,                                                    \
  type* out_value                                             \
) {                                                           \
  memory_region region = get_memory_region_type(              \
    address,                                                  \
    firmware_rom_size,                                        \
    machine_info_rom_size,                                    \
    device_info_rom_size,                                     \
    ram_size,                                                 \
    mmio_size                                                 \
  );                                                          \
                                                              \
  switch (region) {                                           \
    case FIRMWARE_ROM:                                        \
      *out_value = read_fn(&firmware_rom[address]);           \
      return true;                                            \
                                                              \
    case MACHINE_INFO_ROM:                                    \
      *out_value = read_fn(                                   \
        (u8*)machine_info + (address - firmware_rom_size)     \
      );                                                      \
      return true;                                            \
                                                              \
    case DEVICE_INFO_ROM:                                     \
      *out_value = read_fn(                                   \
        (u8*)devices +                                        \
        (address - firmware_rom_size - machine_info_rom_size) \
      );                                                      \
      return true;                                            \
                                                              \
    case RAM:                                                 \
      *out_value = read_fn(                                   \
        &ram[address - machine_info->ram_start]               \
      );                                                      \
      return true;                                            \
                                                              \
    case MMIO: {                                              \
      u64 value;                                              \
                                                              \
      if (!mmio_read(                                         \
        devices,                                              \
        machine_info->device_count,                           \
        address,                                              \
        &value                                                \
      )) {                                                    \
        return false;                                         \
      }                                                       \
                                                              \
      *out_value = (type)value;                               \
      return true;                                            \
    }                                                         \
  }                                                           \
                                                              \
  return false;                                               \
}

#define DEFINE_MEMORY_WRITE(bits, type, write_fn) \
static bool write_u##bits##_memory(               \
  u64 address,                                    \
  u64 firmware_rom_size,                          \
  u64 machine_info_rom_size,                      \
  u64 device_info_rom_size,                       \
  u64 ram_size,                                   \
  u64 mmio_size,                                  \
  u8* firmware_rom,                               \
  machine_info* machine_info,                     \
  device_info* devices,                           \
  u8* ram,                                        \
  type value                                      \
) {                                               \
  (void)firmware_rom;                             \
                                                  \
  memory_region region = get_memory_region_type(  \
    address,                                      \
    firmware_rom_size,                            \
    machine_info_rom_size,                        \
    device_info_rom_size,                         \
    ram_size,                                     \
    mmio_size                                     \
  );                                              \
                                                  \
  switch (region) {                               \
    case RAM:                                     \
      write_fn(                                   \
        &ram[address - machine_info->ram_start],  \
        value                                     \
      );                                          \
      return true;                                \
                                                  \
    case MMIO:                                    \
      return mmio_write(                          \
        devices,                                  \
        machine_info->device_count,               \
        address,                                  \
        value                                     \
      );                                          \
                                                  \
    case FIRMWARE_ROM:                            \
    case MACHINE_INFO_ROM:                        \
    case DEVICE_INFO_ROM:                         \
      return false;                               \
  }                                               \
                                                  \
  return false;                                   \
}

DEFINE_MEMORY_READ(64, u64, read_u64_le)
DEFINE_MEMORY_READ(32, u32, read_u32_le)
DEFINE_MEMORY_READ(16, u16, read_u16_le)
DEFINE_MEMORY_READ(8, u8, read_u8)

DEFINE_MEMORY_WRITE(64, u64, write_u64_le)
DEFINE_MEMORY_WRITE(32, u32, write_u32_le)
DEFINE_MEMORY_WRITE(16, u16, write_u16_le)
DEFINE_MEMORY_WRITE(8, u8, write_u8)

static inline opcode read_opcode(const u8* bytes) {
  return (opcode)bytes[0];
}

static inline instruction_type decode_instruction_type(const u8* bytes) {
  opcode op = (opcode)bytes[0];
  return opcode_instruction_type(op);
}

static bool jump_condition_is_met(u64 flags, opcode op) {
  if (op == OP_JE) {
    return (flags & FLAG_ZERO) != 0;
  }
  if (op == OP_JNE) {
    return (flags & FLAG_ZERO) == 0;
  }
  if (op == OP_JL) {
    return (flags & FLAG_LESS) != 0;
  }
  if (op == OP_JLE) {
    return (flags & (FLAG_LESS | FLAG_ZERO)) != 0;
  }
  if (op == OP_JG) {
    return (flags & FLAG_GREATER) != 0;
  }
  if (op == OP_JGE) {
    return (flags & (FLAG_GREATER | FLAG_ZERO)) != 0;
  }
  if (op == OP_JO) {
    return (flags & FLAG_OVERFLOW) != 0;
  }
  if (op == OP_JNO) {
    return (flags & FLAG_OVERFLOW) == 0;
  }
  if (op == OP_JC) {
    return (flags & FLAG_CARRY) != 0;
  }
  if (op == OP_JNC) {
    return (flags & FLAG_CARRY) == 0;
  }
  return false;
}

#define USAGE(prog) "Usage: %s [--device plugin.so]... binary [mem_mib]\n", (prog)

static void print_help(const char* program) {
  printf(USAGE(program));
  printf("  -d, --device: load an MMIO plugin (repeatable)\n");
  printf("  binary: path to the program image to load into ROM\n");
  printf("  mem_mib: total memory size in MiB (default: 512)\n");
}

static bool load_mmio_device(
  const char* path,
  u64 start,
  u64 available_size,
  loaded_device* out_device
) {
  void* library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!library) {
    fprintf(stderr, "Error: Could not load MMIO plugin '%s': %s\n", path, dlerror());
    return false;
  }

  dlerror();
  void* symbol = dlsym(library, MMIO_PLUGIN_ENTRYPOINT);
  const char* symbol_error = dlerror();
  if (symbol_error) {
    fprintf(stderr, "Error: MMIO plugin '%s' has no %s entrypoint: %s\n",
      path, MMIO_PLUGIN_ENTRYPOINT, symbol_error);
    dlclose(library);
    return false;
  }

  mmio_plugin_entrypoint_fn entrypoint;
  static_assert(sizeof(entrypoint) == sizeof(symbol));
  memcpy(&entrypoint, &symbol, sizeof(entrypoint));
  const mmio_plugin_descriptor* plugin = entrypoint(MMIO_PLUGIN_ABI_VERSION);
  if (!plugin) {
    fprintf(stderr, "Error: MMIO plugin '%s' does not support ABI version %u\n",
      path, MMIO_PLUGIN_ABI_VERSION);
    dlclose(library);
    return false;
  }

  if (!plugin->name || plugin->name[0] == '\0' || plugin->size == 0 ||
      !plugin->read || !plugin->write) {
    fprintf(stderr, "Error: MMIO plugin '%s' returned an invalid descriptor\n", path);
    dlclose(library);
    return false;
  }
  if (plugin->size > available_size) {
    fprintf(stderr,
      "Error: MMIO plugin '%s' needs %llu bytes, but only %llu MMIO bytes remain\n",
      path,
      (unsigned long long)plugin->size,
      (unsigned long long)available_size);
    dlclose(library);
    return false;
  }

  *out_device = (loaded_device){
    .library = library,
    .plugin = plugin,
    .info = {
      .type = plugin->type,
      .start = start,
      .size = plugin->size,
    },
  };
  snprintf((char*)out_device->info.name, sizeof(out_device->info.name), "%s", plugin->name);
  return true;
}

static void dump_register(const u64* registers, size_t index) {
  if (index < GENERAL_REGISTER_COUNT) {
    fprintf(stderr, "r%zu = 0x%016llx (%llu)\n",
      index,
      (unsigned long long)registers[index],
      (unsigned long long)registers[index]
    );
    return;
  }

  char* reg_name = NULL;

  if (index == reserved_register_index(REG_SLOT_IP)) {
    reg_name = "pc";
  } else if (index == reserved_register_index(REG_SLOT_SP)) {
    reg_name = "sp";
  } else if (index == reserved_register_index(REG_SLOT_FLAGS)) {
    reg_name = "flags";
  } else if (index == reserved_register_index(REG_SLOT_MACHINE_INFO)) {
    reg_name = "machine_info";
  } else if (index == reserved_register_index(REG_SLOT_IVT)) {
    reg_name = "ivt";
  } else {
    fprintf(stderr, "r%zu = 0x%016llx (%llu)\n",
      index,
      (unsigned long long)registers[index],
      (unsigned long long)registers[index]
    );
  }

  if (reg_name) {
    fprintf(stderr, "%s = 0x%016llx (%llu)\n",
      reg_name,
      (unsigned long long)registers[index],
      (unsigned long long)registers[index]
    );
  }
}

static void dump_registers(const u64* registers) {
  fprintf(stderr, "==== REGISTER DUMP ====\n");

  for (size_t i = 0; i < GENERAL_REGISTER_COUNT + RESERVED_REGISTER_COUNT; i++) {
    dump_register(registers, i);
  }
}

#define RUNTIME_ERROR(error)                       \
  do {                                             \
    fprintf(stderr, "Runtime error: " error "\n"); \
    dump_registers(registers);                     \
    goto done;                                     \
  } while (0)

int main(int argc, char** argv) {
  const usz firmware_rom_size = MiB(16);
  const usz machine_info_rom_size = KiB(4);
  const usz device_info_rom_size = KiB(32);
  usz ram_size = MiB(512);
  const usz mmio_size = MiB(32);
  const char* binary_path = NULL;
  const char* mem_mib_arg = NULL;
  const char** plugin_paths = NULL;
  usz plugin_count = 0;
  device_info* devices = NULL;
  u8* ram = NULL;
  u8* firmware_rom = NULL;
  u64* registers = NULL;
  FILE* binary_file = NULL;
  int exit_code = 1;

  plugin_paths = calloc((usz)argc, sizeof(*plugin_paths));
  if (!plugin_paths) {
    fprintf(stderr, "Error: Could not allocate plugin path list\n");
    goto done;
  }
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      print_help(argv[0]);
      exit_code = 0;
      goto done;
    } else if (strcmp(argv[i], "--device") == 0 || strcmp(argv[i], "-d") == 0) {
      if (++i >= argc) {
        fprintf(stderr, "Error: %s requires a plugin path\n", argv[i - 1]);
        goto done;
      }
      plugin_paths[plugin_count++] = argv[i];
    } else if (argv[i][0] == '-') {
      fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
      goto done;
    } else if (!binary_path) {
      binary_path = argv[i];
    } else if (!mem_mib_arg) {
      mem_mib_arg = argv[i];
    } else {
      fprintf(stderr, "Error: Unexpected argument '%s'\n", argv[i]);
      goto done;
    }
  }

  if (!binary_path) {
    fprintf(stderr, "Error: No binary specified\n");
    fprintf(stderr, USAGE(argv[0]));
    goto done;
  }

  if (mem_mib_arg) {
    ram_size = MiB(strtoull(mem_mib_arg, NULL, 10));
  }
  const usz register_count = GENERAL_REGISTER_COUNT + RESERVED_REGISTER_COUNT;

  machine_info machine_info = {
    .version = 0,
    .self_size = sizeof(machine_info),
    .ram_start = firmware_rom_size + machine_info_rom_size + device_info_rom_size,
    .ram_size = ram_size,
    .firmware_rom_start = 0,
    .firmware_rom_size = firmware_rom_size,
    .device_size = sizeof(device_info),
    .device_list = firmware_rom_size + machine_info_rom_size,
  };

  const usz mmio_start = machine_info.ram_start + machine_info.ram_size;
  if (plugin_count > device_info_rom_size / sizeof(*devices)) {
    fprintf(stderr, "Error: Too many MMIO plugins for the device info ROM\n");
    goto done;
  }
  devices = calloc(1, device_info_rom_size);
  if (!devices) {
    fprintf(stderr, "Error: Could not allocate device info ROM\n");
    goto done;
  }
  if (plugin_count != 0) {
    mmio_devices = calloc(plugin_count, sizeof(*mmio_devices));
    if (!mmio_devices) {
      fprintf(stderr, "Error: Could not allocate MMIO device list\n");
      goto done;
    }
  }

  u64 next_mmio_address = mmio_start;
  const u64 mmio_end = mmio_start + mmio_size;
  for (usz i = 0; i < plugin_count; i++) {
    if (!load_mmio_device(
      plugin_paths[i],
      next_mmio_address,
      mmio_end - next_mmio_address,
      &mmio_devices[i]
    )) {
      goto done;
    }
    devices[i] = mmio_devices[i].info;

    u64 device_end = next_mmio_address + devices[i].size;
    next_mmio_address = (device_end + 7u) & ~7ull;
    if (next_mmio_address < device_end) {
      fprintf(stderr, "Error: MMIO address overflow while loading '%s'\n", plugin_paths[i]);
      goto done;
    }
  }
  machine_info.device_count = plugin_count;

  firmware_rom = malloc(firmware_rom_size);
  if (!firmware_rom) {
    fprintf(stderr, "Error: Could not allocate firmware ROM\n");
    goto done;
  }
  memset(firmware_rom, 0, firmware_rom_size);

  ram = malloc(ram_size);
  if (!ram) {
    fprintf(stderr, "Error: Could not allocate RAM\n");
    goto done;
  }
  memset(ram, 0, ram_size);

  registers = malloc(register_count * sizeof(u64));
  if (!registers) {
    fprintf(stderr, "Error: Could not allocate registers\n");
    goto done;
  }
  memset(registers, 0, register_count * sizeof(u64));

  const usz ip_idx = reserved_register_index(REG_SLOT_IP);
  const usz sp_idx = reserved_register_index(REG_SLOT_SP);
  const usz flags_idx = reserved_register_index(REG_SLOT_FLAGS);
  const usz machine_info_idx = reserved_register_index(REG_SLOT_MACHINE_INFO);
  const usz ivt_idx = reserved_register_index(REG_SLOT_IVT);

  registers[machine_info_idx] = firmware_rom_size;
  registers[ip_idx] = 0;
  registers[sp_idx] = 0;
  registers[flags_idx] = 0;

  // enable interrupts by default
  registers[flags_idx] |= FLAG_INT_ENABLE;

  const u64 ram_start = machine_info.ram_start;
  const u64 ram_end   = machine_info.ram_start + machine_info.ram_size;

  binary_file = fopen(binary_path, "rb");
  if (!binary_file) {
    fprintf(stderr, "Error: Could not open binary file '%s'\n", binary_path);
    goto done;
  }

  if (fseek(binary_file, 0, SEEK_END) != 0) {
    fprintf(stderr, "Error: Could not seek binary file '%s'\n", binary_path);
    goto done;
  }

  long binary_size_long = ftell(binary_file);
  if (binary_size_long < 0) {
    fprintf(stderr, "Error: Could not determine binary size for '%s'\n", binary_path);
    goto done;
  }

  usz binary_size = (usz)binary_size_long;
  if (binary_size > firmware_rom_size) {
    fprintf(stderr, "Error: Binary size (%zu bytes) exceeds ROM size (%zu bytes)\n", binary_size, firmware_rom_size);
    goto done;
  }

  if (fseek(binary_file, 0, SEEK_SET) != 0) {
    fprintf(stderr, "Error: Could not rewind binary file '%s'\n", binary_path);
    goto done;
  }

  size_t read_size = fread(firmware_rom, 1, binary_size, binary_file);
  if (read_size != binary_size) {
    fprintf(stderr, "Error: Could not read entire binary file (read %zu bytes, expected %zu bytes)\n", read_size, binary_size);
    goto done;
  }
  fclose(binary_file);
  binary_file = NULL;

  while (true) {
    u64 ip = registers[ip_idx];

    u8 insn_buf[MAX_INSN_SIZE];

    for (usz i = 0; i < MAX_INSN_SIZE; i++) {
      if (!read_u8_memory(
        ip + i,
        firmware_rom_size,
        machine_info_rom_size,
        device_info_rom_size,
        ram_size,
        mmio_size,
        firmware_rom,
        &machine_info,
        devices,
        ram,
        &insn_buf[i]
      )) {
        RUNTIME_ERROR("invalid instruction fetch");
      }
    }

    instruction_type insn_type = decode_instruction_type(insn_buf);
    opcode op = read_opcode(insn_buf);
    u64 next_ip = ip + size_for_instruction_type(insn_type);
    bool ip_written = false;

#define get_insn(type) \
  instruction_##type insn = *(instruction_##type*)(insn_buf)

    switch (op) {
      case OP_HALT:
        exit_code = 0;
        goto done;

      case OP_LOADI: {
        get_insn(1reg_imm);
        if (insn.a >= register_count) {
          RUNTIME_ERROR("invalid destination register");
        }
        registers[insn.a] = insn.imm;
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_MOV: {
        get_insn(2reg);
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        registers[insn.a] = registers[insn.b];
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_ADD: {
        get_insn(3reg);
        if (insn.a >= register_count || insn.b >= register_count || insn.c >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        u64 lhs = registers[insn.b];
        u64 rhs = registers[insn.c];
        u64 result = lhs + rhs;

        bool carry = result < lhs;

        bool overflow =
          (((lhs ^ result) & (rhs ^ result)) >> 63) != 0;

        registers[insn.a] = result;

        set_arithmetic_flags(
          &registers[flags_idx],
          carry,
          overflow
        );
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_SUB: {
        get_insn(3reg);
        if (insn.a >= register_count || insn.b >= register_count || insn.c >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        u64 lhs = registers[insn.b];
        u64 rhs = registers[insn.c];
        u64 result = lhs - rhs;

        bool carry = lhs < rhs;

        bool overflow =
          (((lhs ^ rhs) & (lhs ^ result)) >> 63) != 0;

        registers[insn.a] = result;

        set_arithmetic_flags(
          &registers[flags_idx],
          carry,
          overflow
        );
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_MUL: {
        get_insn(3reg);
        if (insn.a >= register_count || insn.b >= register_count || insn.c >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        u64 lhs = registers[insn.b];
        u64 rhs = registers[insn.c];

        u128 wide = (u128)lhs * (u128)rhs;

        u64 result = (u64)wide;

        bool overflow = (wide >> 64) != 0;

        registers[insn.a] = result;

        set_arithmetic_flags(
          &registers[flags_idx],
          overflow,
          overflow
        );
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_MULH: {
        get_insn(3reg);
        if (insn.a >= register_count || insn.b >= register_count || insn.c >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        u64 lhs = registers[insn.b];
        u64 rhs = registers[insn.c];

        u128 wide = (u128)lhs * (u128)rhs;

        u64 result = (u64)(wide >> 64);

        registers[insn.a] = result;

        set_arithmetic_flags(
          &registers[flags_idx],
          false,
          false
        );
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_MULHS: {
        get_insn(3reg);
        if (insn.a >= register_count || insn.b >= register_count || insn.c >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        i64 lhs = (i64)registers[insn.b];
        i64 rhs = (i64)registers[insn.c];

        i128 wide = (i128)lhs * (i128)rhs;

        i64 result = (i64)(wide >> 64);

        registers[insn.a] = (u64)result;

        set_arithmetic_flags(
          &registers[flags_idx],
          false,
          false
        );
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_DIV: {
        get_insn(3reg);
        if (insn.a >= register_count || insn.b >= register_count || insn.c >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        if (registers[insn.c] == 0) {
          RUNTIME_ERROR("division by zero");
        }
        u64 result = registers[insn.b] / registers[insn.c];

        registers[insn.a] = result;

        set_arithmetic_flags(
          &registers[flags_idx],
          false,
          false
        );
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_DIVS: {
        get_insn(3reg);
        if (insn.a >= register_count || insn.b >= register_count || insn.c >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        if ((i64)registers[insn.c] == 0) {
          RUNTIME_ERROR("division by zero");
        }
        i64 result = (i64)registers[insn.b] / (i64)registers[insn.c];

        registers[insn.a] = (u64)result;

        set_arithmetic_flags(
          &registers[flags_idx],
          false,
          false
        );
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_MOD: {
        get_insn(3reg);
        if (insn.a >= register_count || insn.b >= register_count || insn.c >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        if (registers[insn.c] == 0) {
          RUNTIME_ERROR("division by zero");
        }
        u64 result = registers[insn.b] % registers[insn.c];

        registers[insn.a] = result;

        set_arithmetic_flags(
          &registers[flags_idx],
          false,
          false
        );
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_MODS: {
        get_insn(3reg);
        if (insn.a >= register_count || insn.b >= register_count || insn.c >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        if ((i64)registers[insn.c] == 0) {
          RUNTIME_ERROR("division by zero");
        }
        i64 result = (i64)registers[insn.b] % (i64)registers[insn.c];

        registers[insn.a] = (u64)result;

        set_arithmetic_flags(
          &registers[flags_idx],
          false,
          false
        );
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_ADDI: {
        get_insn(2reg_imm);
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        u64 lhs = registers[insn.b];
        u64 rhs = insn.imm;
        u64 result = lhs + rhs;

        bool carry = result < lhs;

        bool overflow =
          (((lhs ^ result) & (rhs ^ result)) >> 63) != 0;

        registers[insn.a] = result;

        set_arithmetic_flags(
          &registers[flags_idx],
          carry,
          overflow
        );
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_SUBI: {
        get_insn(2reg_imm);
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        u64 lhs = registers[insn.b];
        u64 rhs = insn.imm;
        u64 result = lhs - rhs;

        bool carry = lhs < rhs;

        bool overflow =
          (((lhs ^ rhs) & (lhs ^ result)) >> 63) != 0;

        registers[insn.a] = result;

        set_arithmetic_flags(
          &registers[flags_idx],
          carry,
          overflow
        );
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_MULI: {
        get_insn(2reg_imm);
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        u64 lhs = registers[insn.b];
        u64 rhs = insn.imm;

        u128 wide = (u128)lhs * (u128)rhs;

        u64 result = (u64)wide;

        bool overflow = (wide >> 64) != 0;

        registers[insn.a] = result;

        set_arithmetic_flags(
          &registers[flags_idx],
          overflow,
          overflow
        );
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_MULHI: {
        get_insn(2reg_imm);
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        u64 lhs = registers[insn.b];
        u64 rhs = insn.imm;

        u128 wide = (u128)lhs * (u128)rhs;

        u64 result = (u64)(wide >> 64);

        registers[insn.a] = result;

        set_arithmetic_flags(
          &registers[flags_idx],
          false,
          false
        );
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_MULHSI: {
        get_insn(2reg_imm);
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        i64 lhs = (i64)registers[insn.b];
        i64 rhs = (i64)insn.imm;

        i128 wide = (i128)lhs * (i128)rhs;

        i64 result = (i64)(wide >> 64);

        registers[insn.a] = (u64)result;

        set_arithmetic_flags(
          &registers[flags_idx],
          false,
          false
        );
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_DIVI: {
        get_insn(2reg_imm);
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        if ((i64)insn.imm == 0) {
          RUNTIME_ERROR("division by zero");
        }
        u64 result = registers[insn.b] / insn.imm;

        registers[insn.a] = result;

        set_arithmetic_flags(
          &registers[flags_idx],
          false,
          false
        );
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_DIVSI: {
        get_insn(2reg_imm);
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        if ((i64)insn.imm == 0) {
          RUNTIME_ERROR("division by zero");
        }
        i64 result = (i64)registers[insn.b] / (i64)insn.imm;

        registers[insn.a] = (u64)result;

        set_arithmetic_flags(
          &registers[flags_idx],
          false,
          false
        );
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_MODI: {
        get_insn(2reg_imm);
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        if ((i64)insn.imm == 0) {
          RUNTIME_ERROR("division by zero");
        }
        u64 result = registers[insn.b] % insn.imm;

        registers[insn.a] = result;

        set_arithmetic_flags(
          &registers[flags_idx],
          false,
          false
        );
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_MODSI: {
        get_insn(2reg_imm);
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        if ((i64)insn.imm == 0) {
          RUNTIME_ERROR("division by zero");
        }
        i64 result = (i64)registers[insn.b] % (i64)insn.imm;

        registers[insn.a] = (u64)result;

        set_arithmetic_flags(
          &registers[flags_idx],
          false,
          false
        );
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_AND: {
        get_insn(3reg);
        if (insn.a >= register_count || insn.b >= register_count || insn.c >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        registers[insn.a] = registers[insn.b] & registers[insn.c];
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_OR: {
        get_insn(3reg);
        if (insn.a >= register_count || insn.b >= register_count || insn.c >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        registers[insn.a] = registers[insn.b] | registers[insn.c];
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_XOR: {
        get_insn(3reg);
        if (insn.a >= register_count || insn.b >= register_count || insn.c >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        registers[insn.a] = registers[insn.b] ^ registers[insn.c];
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_NOT: {
        get_insn(2reg);
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        registers[insn.a] = ~registers[insn.b];
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_ANDI: {
        get_insn(2reg_imm);
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        registers[insn.a] = registers[insn.b] & insn.imm;
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_ORI: {
        get_insn(2reg_imm);
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        registers[insn.a] = registers[insn.b] | insn.imm;
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_XORI: {
        get_insn(2reg_imm);
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        registers[insn.a] = registers[insn.b] ^ insn.imm;
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_LEA: {
        get_insn(2reg_imm);
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        registers[insn.a] = registers[insn.b] + (i64)insn.imm;
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_SHL: {
        get_insn(3reg);
        if (insn.a >= register_count || insn.b >= register_count || insn.c >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        u64 value = registers[insn.b];
        u32 shift = registers[insn.c] & 63u;

        u64 result = value << shift;

        bool carry =
          shift != 0 &&
          ((value >> (64 - shift)) & 1);
        bool overflow =
          shift != 0 &&
          ((value >> 63) != (result >> 63));

        registers[insn.a] = result;

        set_arithmetic_flags(
          &registers[flags_idx],
          carry,
          overflow
        );
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_SHR: {
        get_insn(3reg);
        if (insn.a >= register_count || insn.b >= register_count || insn.c >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        u64 value = registers[insn.b];
        u32 shift = registers[insn.c] & 63u;

        u64 result = value >> shift;

        bool carry =
          shift != 0 &&
          ((value >> (shift - 1)) & 1);
        bool overflow = false;

        registers[insn.a] = result;

        set_arithmetic_flags(
          &registers[flags_idx],
          carry,
          overflow
        );
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_SAR: {
        get_insn(3reg);

        if (insn.a >= register_count || insn.b >= register_count || insn.c >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }

        u64 value = registers[insn.b];
        u32 shift = (u32)(registers[insn.c] & 63u);

        u64 result = (u64)((i64)value >> shift);

        bool carry =
          shift != 0 &&
          ((value >> (shift - 1)) & 1u);
        bool overflow = false;

        registers[insn.a] = result;

        set_arithmetic_flags(
          &registers[flags_idx],
          carry,
          overflow
        );
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_SHLI: {
        get_insn(2reg_imm);
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        u64 value = registers[insn.b];
        u32 shift = insn.imm & 63u;

        u64 result = value << shift;

        bool carry =
          shift != 0 &&
          ((value >> (64 - shift)) & 1);
        bool overflow =
          shift != 0 &&
          ((value >> 63) != (result >> 63));

        registers[insn.a] = result;

        set_arithmetic_flags(
          &registers[flags_idx],
          carry,
          overflow
        );
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_SHRI: {
        get_insn(2reg_imm);
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        u64 value = registers[insn.b];
        u32 shift = insn.imm & 63u;

        u64 result = value >> shift;

        bool carry =
          shift != 0 &&
          ((value >> (shift - 1)) & 1);
        bool overflow = false;

        registers[insn.a] = result;

        set_arithmetic_flags(
          &registers[flags_idx],
          carry,
          overflow
        );
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_SARI: {
        get_insn(2reg_imm);
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        u64 value = registers[insn.b];
        u32 shift = (u32)(insn.imm & 63u);

        u64 result = (u64)((i64)value >> shift);

        bool carry =
          shift != 0 &&
          ((value >> (shift - 1)) & 1u);
        bool overflow = false;

        registers[insn.a] = result;

        set_arithmetic_flags(
          &registers[flags_idx],
          carry,
          overflow
        );
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_LOAD: {
        get_insn(2reg_imm);
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }

        u64 address = registers[insn.b] + insn.imm;
        if (!read_u64_memory(
          address,
          firmware_rom_size,
          machine_info_rom_size,
          device_info_rom_size,
          ram_size,
          mmio_size,
          firmware_rom,
          &machine_info,
          devices,
          ram,
          &registers[insn.a]
        )) {
          RUNTIME_ERROR("illegal load address");
        }
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_STORE: {
        get_insn(2reg_imm);
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }

        u64 address = registers[insn.b] + insn.imm;
        if (!write_u64_memory(
          address,
          firmware_rom_size,
          machine_info_rom_size,
          device_info_rom_size,
          ram_size,
          mmio_size,
          firmware_rom,
          &machine_info,
          devices,
          ram,
          registers[insn.a]
        )) {
          RUNTIME_ERROR("illegal store address");
        }
        break;
      }

      case OP_LOAD32: {
        get_insn(2reg_imm);
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }

        u32 value;
        u64 address = registers[insn.b] + insn.imm;

        if (!read_u32_memory(
          address,
          firmware_rom_size,
          machine_info_rom_size,
          device_info_rom_size,
          ram_size,
          mmio_size,
          firmware_rom,
          &machine_info,
          devices,
          ram,
          &value
        )) {
          RUNTIME_ERROR("illegal load address");
        }

        registers[insn.a] = value;
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_STORE32: {
        get_insn(2reg_imm);
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }

        u64 address = registers[insn.b] + insn.imm;

        if (!write_u32_memory(
          address,
          firmware_rom_size,
          machine_info_rom_size,
          device_info_rom_size,
          ram_size,
          mmio_size,
          firmware_rom,
          &machine_info,
          devices,
          ram,
          (u32)registers[insn.a]
        )) {
          RUNTIME_ERROR("illegal store address");
        }

        break;
      }

      case OP_LOAD16: {
        get_insn(2reg_imm);
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }

        u16 value;
        u64 address = registers[insn.b] + insn.imm;

        if (!read_u16_memory(
          address,
          firmware_rom_size,
          machine_info_rom_size,
          device_info_rom_size,
          ram_size,
          mmio_size,
          firmware_rom,
          &machine_info,
          devices,
          ram,
          &value
        )) {
          RUNTIME_ERROR("illegal load address");
        }

        registers[insn.a] = value;
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_STORE16: {
        get_insn(2reg_imm);
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }

        u64 address = registers[insn.b] + insn.imm;

        if (!write_u16_memory(
          address,
          firmware_rom_size,
          machine_info_rom_size,
          device_info_rom_size,
          ram_size,
          mmio_size,
          firmware_rom,
          &machine_info,
          devices,
          ram,
          (u16)registers[insn.a]
        )) {
          RUNTIME_ERROR("illegal store address");
        }

        break;
      }

      case OP_LOAD8: {
        get_insn(2reg_imm);
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }

        u8 value;
        u64 address = registers[insn.b] + insn.imm;

        if (!read_u8_memory(
          address,
          firmware_rom_size,
          machine_info_rom_size,
          device_info_rom_size,
          ram_size,
          mmio_size,
          firmware_rom,
          &machine_info,
          devices,
          ram,
          &value
        )) {
          RUNTIME_ERROR("illegal load address");
        }

        registers[insn.a] = value;
        ip_written = (insn.a == ip_idx);
        break;
      }

      case OP_STORE8: {
        get_insn(2reg_imm);
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }

        u64 address = registers[insn.b] + insn.imm;

        if (!write_u8_memory(
          address,
          firmware_rom_size,
          machine_info_rom_size,
          device_info_rom_size,
          ram_size,
          mmio_size,
          firmware_rom,
          &machine_info,
          devices,
          ram,
          (u8)registers[insn.a]
        )) {
          RUNTIME_ERROR("illegal store address");
        }

        break;
      }

      case OP_JMP: {
        get_insn(0reg_imm);
        registers[ip_idx] = insn.imm;
        continue;
      }

      case OP_JMPR: {
        get_insn(1reg);
        if (insn.a >= register_count) {
          RUNTIME_ERROR("invalid jump target register");
        }
        registers[ip_idx] = registers[insn.a];
        continue;
      }

      case OP_CMP: {
        get_insn(2reg);

        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }

        u64 left = registers[insn.a];
        u64 right = registers[insn.b];

        const u64 condition_mask =
          FLAG_ZERO |
          FLAG_LESS |
          FLAG_GREATER;

        u64 new_flags = registers[flags_idx] & ~condition_mask;

        if (left == right) {
          new_flags |= FLAG_ZERO;
        } else if (left < right) {
          new_flags |= FLAG_LESS;
        } else {
          new_flags |= FLAG_GREATER;
        }

        registers[flags_idx] = new_flags;
        break;
      }

      case OP_CMPI: {
        get_insn(1reg_imm);

        if (insn.a >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }

        u64 left = registers[insn.a];
        u64 right = insn.imm;

        const u64 condition_mask =
          FLAG_ZERO |
          FLAG_LESS |
          FLAG_GREATER;

        u64 new_flags = registers[flags_idx] & ~condition_mask;

        if (left == right) {
          new_flags |= FLAG_ZERO;
        } else if (left < right) {
          new_flags |= FLAG_LESS;
        } else {
          new_flags |= FLAG_GREATER;
        }

        registers[flags_idx] = new_flags;
        break;
      }

      case OP_CMPS: {
        get_insn(2reg);

        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }

        i64 left = (i64)registers[insn.a];
        i64 right = (i64)registers[insn.b];

        const u64 condition_mask =
          FLAG_ZERO |
          FLAG_LESS |
          FLAG_GREATER;

        u64 new_flags = registers[flags_idx] & ~condition_mask;

        if (left == right) {
          new_flags |= FLAG_ZERO;
        } else if (left < right) {
          new_flags |= FLAG_LESS;
        } else {
          new_flags |= FLAG_GREATER;
        }

        registers[flags_idx] = new_flags;
        break;
      }

      case OP_CMPSI: {
        get_insn(1reg_imm);

        if (insn.a >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }

        i64 left = (i64)registers[insn.a];
        i64 right = (i64)insn.imm;

        const u64 condition_mask =
          FLAG_ZERO |
          FLAG_LESS |
          FLAG_GREATER;

        u64 new_flags = registers[flags_idx] & ~condition_mask;

        if (left == right) {
          new_flags |= FLAG_ZERO;
        } else if (left < right) {
          new_flags |= FLAG_LESS;
        } else {
          new_flags |= FLAG_GREATER;
        }

        registers[flags_idx] = new_flags;
        break;
      }

      case OP_JE:
      case OP_JNE:
      case OP_JL:
      case OP_JLE:
      case OP_JG:
      case OP_JGE:
      case OP_JO:
      case OP_JNO:
      case OP_JC:
      case OP_JNC: {
        get_insn(0reg_imm);
        if (jump_condition_is_met(registers[flags_idx], op)) {
          registers[ip_idx] = (u64)insn.imm;
          continue;
        }
        break;
      }

      case OP_PUSH: {
        get_insn(1reg);
        if (insn.a >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        if (registers[sp_idx] < ram_start || registers[sp_idx] > ram_end) {
          RUNTIME_ERROR("stack overflow");
        }
        registers[sp_idx] -= sizeof(u64);
        if (!write_u64_memory(
          registers[sp_idx],
          firmware_rom_size,
          machine_info_rom_size,
          device_info_rom_size,
          ram_size,
          mmio_size,
          firmware_rom,
          &machine_info,
          devices,
          ram,
          registers[insn.a]
        )) {
          RUNTIME_ERROR("stack write failed");
        }
        break;
      }

      case OP_POP: {
        get_insn(1reg);
        if (insn.a >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        if (registers[sp_idx] < ram_start || registers[sp_idx] >= ram_end) {
          RUNTIME_ERROR("stack underflow");
        }
        if (!read_u64_memory(
            registers[sp_idx],
            firmware_rom_size,
            machine_info_rom_size,
            device_info_rom_size,
            ram_size,
            mmio_size,
            firmware_rom,
            &machine_info,
            devices,
            ram,
            &registers[insn.a]
          )) {
          RUNTIME_ERROR("stack read failed");
        }
        registers[sp_idx] += sizeof(u64);
        break;
      }

      case OP_CALL: {
        get_insn(0reg_imm);
        if (registers[sp_idx] < ram_start + sizeof(u64) ||
            registers[sp_idx] > ram_end) {
          RUNTIME_ERROR("stack overflow");
        }

        registers[sp_idx] -= sizeof(u64);

        if (!write_u64_memory(
          registers[sp_idx],
          firmware_rom_size,
          machine_info_rom_size,
          device_info_rom_size,
          ram_size,
          mmio_size,
          firmware_rom,
          &machine_info,
          devices,
          ram,
          next_ip
        )) {
          RUNTIME_ERROR("stack write failed");
        }

        registers[ip_idx] = insn.imm;
        continue;
      }
      
      case OP_CALLR: {
        get_insn(1reg);
        if (insn.a >= register_count) {
          RUNTIME_ERROR("invalid target register");
        }

        if (registers[sp_idx] < ram_start + sizeof(u64) ||
            registers[sp_idx] > ram_end) {
          RUNTIME_ERROR("stack overflow");
        }

        registers[sp_idx] -= sizeof(u64);

        if (!write_u64_memory(
          registers[sp_idx],
          firmware_rom_size,
          machine_info_rom_size,
          device_info_rom_size,
          ram_size,
          mmio_size,
          firmware_rom,
          &machine_info,
          devices,
          ram,
          next_ip
        )) {
          RUNTIME_ERROR("stack write failed");
        }

        registers[ip_idx] = registers[insn.a];
        continue;
      }

      case OP_RET: {
        u64 return_address;

        if (registers[sp_idx] < ram_start || registers[sp_idx] >= ram_end) {
          RUNTIME_ERROR("stack underflow");
        }

        if (!read_u64_memory(
          registers[sp_idx],
          firmware_rom_size,
          machine_info_rom_size,
          device_info_rom_size,
          ram_size,
          mmio_size,
          firmware_rom,
          &machine_info,
          devices,
          ram,
          &return_address
        )) {
          RUNTIME_ERROR("stack read failed");
        }

        registers[sp_idx] += sizeof(u64);

        registers[ip_idx] = return_address;
        continue;
      }

      case OP_INT: {
        get_insn(0reg_imm);

        if (insn.imm >= 256) {
          RUNTIME_ERROR("invalid interrupt number");
        }

        if (insn.imm == DUMP_REGS_INT_CODE) {
          dump_registers(registers);
          break;
        }

        if ((registers[flags_idx] & FLAG_INT_ENABLE) == 0) {
          RUNTIME_ERROR("attempted to trigger interrupt while interrupts are disabled");
        }

        if (registers[sp_idx] < ram_start + 2 * sizeof(u64) ||
          registers[sp_idx] > ram_end) {
          RUNTIME_ERROR("stack overflow");
        }

        // save flags
        registers[sp_idx] -= sizeof(u64);
        if (!write_u64_memory(
          registers[sp_idx],
          firmware_rom_size,
          machine_info_rom_size,
          device_info_rom_size,
          ram_size,
          mmio_size,
          firmware_rom,
          &machine_info,
          devices,
          ram,
          registers[flags_idx]
        )) {
          RUNTIME_ERROR("stack write failed");
        }

        // save return address
        registers[sp_idx] -= sizeof(u64);
        if (!write_u64_memory(
          registers[sp_idx],
          firmware_rom_size,
          machine_info_rom_size,
          device_info_rom_size,
          ram_size,
          mmio_size,
          firmware_rom,
          &machine_info,
          devices,
          ram,
          next_ip
        )) {
          RUNTIME_ERROR("stack write failed");
        }

        // load interrupt handler address from ivt register
        u64 ivt_address = registers[ivt_idx] + (insn.imm * sizeof(u64));
        u64 handler_address;
        if (!read_u64_memory(
          ivt_address,
          firmware_rom_size,
          machine_info_rom_size,
          device_info_rom_size,
          ram_size,
          mmio_size,
          firmware_rom,
          &machine_info,
          devices,
          ram,
          &handler_address
        )) {
          RUNTIME_ERROR("invalid interrupt vector table address");
        }

        // disable interrupts
        registers[flags_idx] &= ~FLAG_INT_ENABLE;

        // jump to handler
        registers[ip_idx] = handler_address;
        continue;
      }
      
      case OP_IRET: {
        u64 return_address;
        u64 flags;

        if (registers[sp_idx] < ram_start + 2 * sizeof(u64) ||
          registers[sp_idx] > ram_end) {
          RUNTIME_ERROR("stack underflow");
        }

        // restore return address
        if (!read_u64_memory(
          registers[sp_idx],
          firmware_rom_size,
          machine_info_rom_size,
          device_info_rom_size,
          ram_size,
          mmio_size,
          firmware_rom,
          &machine_info,
          devices,
          ram,
          &return_address
        )) {
          RUNTIME_ERROR("stack read failed");
        }
        registers[sp_idx] += sizeof(u64);

        // restore flags
        if (!read_u64_memory(
          registers[sp_idx],
          firmware_rom_size,
          machine_info_rom_size,
          device_info_rom_size,
          ram_size,
          mmio_size,
          firmware_rom,
          &machine_info,
          devices,
          ram,
          &flags
        )) {
          RUNTIME_ERROR("stack read failed");
        }
        registers[sp_idx] += sizeof(u64);
        registers[flags_idx] = flags;

        // jump to return address
        registers[ip_idx] = return_address;
        continue;
      }
      
      case OP_NOP:
        break;

      default:
        RUNTIME_ERROR("invalid opcode");
        break;
    }

    if (!ip_written) {
      registers[ip_idx] = next_ip;
    }
  }

done:
#ifdef DEBUG
  if (registers) {
    dump_registers(registers);
  }
#endif
  if (binary_file) {
    fclose(binary_file);
  }
  if (firmware_rom) {
    free(firmware_rom);
  }
  if (ram) {
    free(ram);
  }
  if (registers) {
    free(registers);
  }
  if (mmio_devices) {
    for (usz i = plugin_count; i > 0; i--) {
      if (mmio_devices[i - 1].library) {
        dlclose(mmio_devices[i - 1].library);
      }
    }
    free(mmio_devices);
    mmio_devices = NULL;
  }
  free(devices);
  free(plugin_paths);
  return exit_code;
}
