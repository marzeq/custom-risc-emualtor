#include <assert.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <termios.h>
#include <unistd.h>

static struct termios old_termios;

static void terminal_raw_enable(void) {
  struct termios t;

  tcgetattr(STDIN_FILENO, &old_termios);
  t = old_termios;

  t.c_lflag &= ~(ICANON | ECHO);

  tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

#include "isa.h"

#define KiB(x) ((x) * 1024)
#define MiB(x) ((x) * 1024 * 1024)

#define FLAG_ZERO    (1u << 0)
#define FLAG_LESS    (1u << 1)
#define FLAG_GREATER (1u << 2)

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

enum {
  DEVICE_STDIO = 1,
};

static u32 read_u32_le(const u8* bytes) {
  return (u32)bytes[0] | ((u32)bytes[1] << 8) | ((u32)bytes[2] << 16) | ((u32)bytes[3] << 24);
}

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

static const device_info* find_device(
  const device_info* devices,
  u64 device_count,
  u64 address
) {
  for (u64 i = 0; i < device_count; i++) {
    const device_info* d = &devices[i];

    if (address >= d->start &&
        address < d->start + d->size) {
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
  const device_info* device =
      find_device(devices, device_count, address);

  if (!device) {
    return false;
  }

  switch (device->type) {
    case DEVICE_STDIO: {
      int ch = getchar();
      if (ch == EOF) {
        *out_value = (u64)0;
      } else {
        *out_value = (u64)(unsigned char)ch;
      }
      return true;
    }

    default:
      return false;
  }
}

static bool mmio_write(
  const device_info* devices,
  u64 device_count,
  u64 address,
  u64 value
) {
  const device_info* device =
      find_device(devices, device_count, address);

  if (!device) {
    return false;
  }

  switch (device->type) {
    case DEVICE_STDIO: {
      int ch = (int)(value & 0xFFu);
      if (putchar(ch) == EOF) {
        return false;
      }
      return true;
    }

    default:
      return false;
  }
}

static bool read_u64_memory(
  u64 address,
  u64 firmware_rom_size,
  u64 machine_info_rom_size,
  u64 device_info_rom_size,
  u64 ram_size,
  u64 mmio_size,
  u8* firmware_rom,
  machine_info* machine_info,
  device_info* devices,
  u8* ram,
  u64* out_value
) {
  memory_region region = get_memory_region_type(
    address,
    firmware_rom_size,
    machine_info_rom_size,
    device_info_rom_size,
    ram_size,
    mmio_size
  );

  switch (region) {
    case FIRMWARE_ROM:
      *out_value = read_u64_le(&firmware_rom[address]);
      return true;

    case MACHINE_INFO_ROM:
      if (address + sizeof(*machine_info) > firmware_rom_size + machine_info_rom_size) {
        *out_value = 0;
      } else {
        *out_value = read_u64_le((u8*)machine_info + (address - firmware_rom_size));
      }
      return true;

    case DEVICE_INFO_ROM: {
      u64 device_info_offset = address - firmware_rom_size - machine_info_rom_size;
      if (device_info_offset + sizeof(device_info) > device_info_rom_size) {
        *out_value = 0;
      } else {
        *out_value = read_u64_le((u8*)devices + device_info_offset);
      }
      return true;
    }

    case RAM:
      *out_value = read_u64_le(&ram[address - machine_info->ram_start]);
      return true;

    case MMIO:
      return mmio_read(
        devices,
        machine_info->device_count,
        address,
        out_value
      );

    default:
      return false;
  }
}

static bool write_u64_memory(
  u64 address,
  u64 firmware_rom_size,
  u64 machine_info_rom_size,
  u64 device_info_rom_size,
  u64 ram_size,
  u64 mmio_size,
  u8* firmware_rom,
  machine_info* machine_info,
  device_info* devices,
  u8* ram,
  u64 value
) {
  (void)firmware_rom;
  (void)devices;
  memory_region region = get_memory_region_type(
    address,
    firmware_rom_size,
    machine_info_rom_size,
    device_info_rom_size,
    ram_size,
    mmio_size
  );

  switch (region) {
    case RAM:
      write_u64_le(&ram[address - machine_info->ram_start], value);
      return true;

    case MMIO:
      return mmio_write(
        devices,
        machine_info->device_count,
        address,
        value
      );

    case FIRMWARE_ROM:
    case MACHINE_INFO_ROM:
    case DEVICE_INFO_ROM:
      return false;
  }
}

static bool read_u8_memory(
  u64 address,
  u64 firmware_rom_size,
  u64 machine_info_rom_size,
  u64 device_info_rom_size,
  u64 ram_size,
  u64 mmio_size,
  u8* firmware_rom,
  machine_info* machine_info,
  device_info* devices,
  u8* ram,
  u8* out_value
) {
  memory_region region = get_memory_region_type(
    address,
    firmware_rom_size,
    machine_info_rom_size,
    device_info_rom_size,
    ram_size,
    mmio_size
  );

  switch (region) {
    case FIRMWARE_ROM:
      *out_value = read_u8(&firmware_rom[address]);
      return true;

    case MACHINE_INFO_ROM:
      if (address >= firmware_rom_size + machine_info_rom_size) {
        *out_value = 0;
      } else {
        *out_value =
          read_u8((u8*)machine_info + (address - firmware_rom_size));
      }
      return true;

    case DEVICE_INFO_ROM: {
      u64 offset =
        address - firmware_rom_size - machine_info_rom_size;

      if (offset >= device_info_rom_size) {
        *out_value = 0;
      } else {
        *out_value = read_u8((u8*)devices + offset);
      }

      return true;
    }

    case RAM:
      *out_value =
        read_u8(&ram[address - machine_info->ram_start]);
      return true;

    case MMIO: {
      u64 value;

      if (!mmio_read(
            devices,
            machine_info->device_count,
            address,
            &value
          )) {
        return false;
      }

      *out_value = (u8)value;
      return true;
    }

    default:
      return false;
  }
}

static bool write_u8_memory(
  u64 address,
  u64 firmware_rom_size,
  u64 machine_info_rom_size,
  u64 device_info_rom_size,
  u64 ram_size,
  u64 mmio_size,
  u8* firmware_rom,
  machine_info* machine_info,
  device_info* devices,
  u8* ram,
  u8 value
) {
  (void)firmware_rom;

  memory_region region = get_memory_region_type(
    address,
    firmware_rom_size,
    machine_info_rom_size,
    device_info_rom_size,
    ram_size,
    mmio_size
  );

  switch (region) {
    case RAM:
      write_u8(
        &ram[address - machine_info->ram_start],
        value
      );
      return true;

    case MMIO:
      return mmio_write(
        devices,
        machine_info->device_count,
        address,
        value
      );

    case FIRMWARE_ROM:
    case MACHINE_INFO_ROM:
    case DEVICE_INFO_ROM:
      return false;

    default:
      return false;
  }
}

static instruction decode_instruction(const u8* bytes) {
  instruction insn;
  insn.opcode = bytes[0];
  insn.a = bytes[1];
  insn.b = bytes[2];
  insn.c = bytes[3];
  insn.imm = read_u32_le(&bytes[4]);
  return insn;
}

#define get_memory_size(firmware_rom_size, machine_info_rom_size, ram_size, mmio_size) \
  ((firmware_rom_size) + (machine_info_rom_size) + (ram_size) + (mmio_size))

static bool jump_target_is_valid(u64 target, u64 memory_size) {
  return target < memory_size && (target % INSN_SIZE) == 0;
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
  return false;
}

#define USAGE(prog) "Usage: %s binary [mem_mib]\n", (prog)

static void print_help(const char* program) {
  printf(USAGE(program));
  printf("  binary: path to the program image to load into ROM\n");
  printf("  mem_mib: total memory size in MiB (default: 512)\n");
}

static void dump_register(const u64* registers, size_t index) {
  if (index < EMU_GENERAL_REGISTER_COUNT) {
    fprintf(stderr,
            "r%zu = 0x%016llx (%llu)\n",
            index,
            (unsigned long long)registers[index],
            (unsigned long long)registers[index]);
    return;
  }

  if (index == emu_reserved_register_index(EMU_REG_SLOT_PC)) {
    fprintf(stderr, "pc = 0x%016llx (%llu)\n",
            (unsigned long long)registers[index],
            (unsigned long long)registers[index]);
  } else if (index == emu_reserved_register_index(EMU_REG_SLOT_SP)) {
    fprintf(stderr, "sp = 0x%016llx (%llu)\n",
            (unsigned long long)registers[index],
            (unsigned long long)registers[index]);
  } else if (index == emu_reserved_register_index(EMU_REG_SLOT_FLAGS)) {
    fprintf(stderr, "flags = 0x%016llx (%llu)\n",
            (unsigned long long)registers[index],
            (unsigned long long)registers[index]);
  } else if (index == emu_reserved_register_index(EMU_REG_SLOT_MACHINE_INFO)) {
    fprintf(stderr, "machine_info = 0x%016llx (%llu)\n",
            (unsigned long long)registers[index],
            (unsigned long long)registers[index]);
  } else {
    fprintf(stderr, "r%zu = 0x%016llx (%llu)\n",
            index,
            (unsigned long long)registers[index],
            (unsigned long long)registers[index]);
  }
}

static void dump_registers(const u64* registers, instruction* insn) {
  fprintf(stderr, "==== REGISTER DUMP ====\n");

  for (usz i = 0; i < EMU_GENERAL_REGISTER_COUNT; i++) {
    fprintf(stderr,
            "r%-2zu = 0x%016llx (%llu)\n",
            i,
            (unsigned long long)registers[i],
            (unsigned long long)registers[i]);
  }
  
  fprintf(stderr, "----------------------------\n");

  fprintf(stderr, "pc           = 0x%016llx\n",
          (unsigned long long)registers[emu_reserved_register_index(EMU_REG_SLOT_PC)]);
  fprintf(stderr, "sp           = 0x%016llx\n",
          (unsigned long long)registers[emu_reserved_register_index(EMU_REG_SLOT_SP)]);
  fprintf(stderr, "flags        = 0x%016llx\n",
          (unsigned long long)registers[emu_reserved_register_index(EMU_REG_SLOT_FLAGS)]);
  fprintf(stderr, "machine_info = 0x%016llx\n",
          (unsigned long long)registers[emu_reserved_register_index(EMU_REG_SLOT_MACHINE_INFO)]);

  if (insn) {
    fprintf(stderr, "=== INSTRUCTION DUMP ===\n");

    fprintf(stderr,
          "op=0x%02x a=%u b=%u c=%u imm=%u\n",
          insn->opcode,
          insn->a,
          insn->b,
          insn->c,
          insn->imm);
  }
}

#define RUNTIME_ERROR(error)                       \
  do {                                             \
    fprintf(stderr, "Runtime error: " error "\n"); \
    dump_registers(registers, &insn);              \
    goto done;                                     \
  } while (0)

int main(int argc, char** argv) {
  const usz firmware_rom_size = MiB(16);
  const usz machine_info_rom_size = KiB(4);
  const usz device_info_rom_size = KiB(32);
  usz ram_size = MiB(512);
  const usz mmio_size = MiB(32);
  static_assert(firmware_rom_size % INSN_SIZE == 0, "firmware ROM size must be a multiple of instruction size");
  static_assert(machine_info_rom_size % 8 == 0, "machine info ROM size must be a multiple of 8 bytes");
  static_assert(mmio_size % 8 == 0, "MMIO size must be a multiple of 8 bytes");
  const char* binary_path = NULL;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      print_help(argv[0]);
      return 0;
    }
  }

  if (argc < 2) {
    fprintf(stderr, "Error: No binary specified\n");
    fprintf(stderr, USAGE(argv[0]));
    return 1;
  }

  binary_path = argv[1];
  if (argc > 2) {
    ram_size = MiB(strtoull(argv[2], NULL, 10));
  }
  const usz register_count = EMU_GENERAL_REGISTER_COUNT + EMU_RESERVED_REGISTER_COUNT;

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

  device_info devices[] = {
    {
      .type = DEVICE_STDIO,
      .start = mmio_start,
      .size = 16,
      .name = "stdio device",
    },
  };

  machine_info.device_count = sizeof(devices) / sizeof(devices[0]);

  u8* ram = NULL;
  u8* firmware_rom = NULL;
  u64* registers = NULL;
  FILE* binary_file = NULL;
  int exit_code = 1;

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

  const usz pc_idx = emu_reserved_register_index(EMU_REG_SLOT_PC);
  const usz sp_idx = emu_reserved_register_index(EMU_REG_SLOT_SP);
  const usz flags_idx = emu_reserved_register_index(EMU_REG_SLOT_FLAGS);
  const usz machine_info_idx = emu_reserved_register_index(EMU_REG_SLOT_MACHINE_INFO);

  registers[machine_info_idx] = firmware_rom_size;
  registers[pc_idx] = 0;
  registers[sp_idx] = 0;
  registers[flags_idx] = 0;

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

  if (binary_size % INSN_SIZE != 0) {
    fprintf(stderr, "Error: Binary size (%zu bytes) is not a multiple of instruction size (%d bytes)\n", binary_size, INSN_SIZE);
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

  terminal_raw_enable();

  for (;;) {
    u64 pc = registers[pc_idx];

    instruction insn = decode_instruction(&firmware_rom[pc]);
    opcode op = (opcode)insn.opcode;
    u64 next_pc = pc + INSN_SIZE;
    bool pc_written = false;

    switch (op) {
      case OP_HALT:
        exit_code = 0;
        goto done;

      case OP_LOADI:
        if (insn.a >= register_count) {
          RUNTIME_ERROR("invalid destination register");
        }
        registers[insn.a] = (u64)(i32)insn.imm;
        pc_written = (insn.a == pc_idx);
        break;

      case OP_MOV:
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        registers[insn.a] = registers[insn.b];
        pc_written = (insn.a == pc_idx);
        break;

      case OP_ADD:
        if (insn.a >= register_count || insn.b >= register_count || insn.c >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        registers[insn.a] = registers[insn.b] + registers[insn.c];
        pc_written = (insn.a == pc_idx);
        break;

      case OP_SUB:
        if (insn.a >= register_count || insn.b >= register_count || insn.c >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        registers[insn.a] = registers[insn.b] - registers[insn.c];
        pc_written = (insn.a == pc_idx);
        break;

      case OP_MUL:
        if (insn.a >= register_count || insn.b >= register_count || insn.c >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        registers[insn.a] = registers[insn.b] * registers[insn.c];
        pc_written = (insn.a == pc_idx);
        break;
      
      case OP_DIV:
        if (insn.a >= register_count || insn.b >= register_count || insn.c >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        if (registers[insn.c] == 0) {
          RUNTIME_ERROR("division by zero");
        }
        registers[insn.a] = registers[insn.b] / registers[insn.c];
        pc_written = (insn.a == pc_idx);
        break;

      case OP_ADDI:
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        registers[insn.a] = registers[insn.b] + (u64)(i32)insn.imm;
        pc_written = (insn.a == pc_idx);
        break;

      case OP_SUBI:
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        registers[insn.a] = registers[insn.b] - (u64)(i32)insn.imm;
        pc_written = (insn.a == pc_idx);
        break;

      case OP_MULI:
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        registers[insn.a] = registers[insn.b] * (u64)(i32)insn.imm;
        pc_written = (insn.a == pc_idx);
        break;

      case OP_DIVI:
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        if ((i32)insn.imm == 0) {
          RUNTIME_ERROR("division by zero");
        }
        registers[insn.a] = registers[insn.b] / (u64)(i32)insn.imm;
        pc_written = (insn.a == pc_idx);
        break;

      case OP_MOD:
        if (insn.a >= register_count || insn.b >= register_count || insn.c >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        if (registers[insn.c] == 0) {
          RUNTIME_ERROR("division by zero");
        }
        if ((i32)insn.imm == 0) {
          RUNTIME_ERROR("division by zero");
        }
        registers[insn.a] = registers[insn.b] % (u64)(i32)insn.imm;
        pc_written = (insn.a == pc_idx);
        break;

      case OP_MODI:
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        if ((i32)insn.imm == 0) {
          RUNTIME_ERROR("division by zero");
        }
        registers[insn.a] = registers[insn.b] % (u64)(i32)insn.imm;
        pc_written = (insn.a == pc_idx);
        break;

      case OP_AND:
        if (insn.a >= register_count || insn.b >= register_count || insn.c >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        registers[insn.a] = registers[insn.b] & registers[insn.c];
        pc_written = (insn.a == pc_idx);
        break;

      case OP_OR:
        if (insn.a >= register_count || insn.b >= register_count || insn.c >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        registers[insn.a] = registers[insn.b] | registers[insn.c];
        pc_written = (insn.a == pc_idx);
        break;

      case OP_XOR:
        if (insn.a >= register_count || insn.b >= register_count || insn.c >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        registers[insn.a] = registers[insn.b] ^ registers[insn.c];
        pc_written = (insn.a == pc_idx);
        break;

      case OP_NOT:
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        registers[insn.a] = ~registers[insn.b];
        pc_written = (insn.a == pc_idx);
        break;

      case OP_ANDI:
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        registers[insn.a] = registers[insn.b] & (u64)(i32)insn.imm;
        pc_written = (insn.a == pc_idx);
        break;

      case OP_ORI:
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        registers[insn.a] = registers[insn.b] | (u64)(i32)insn.imm;
        pc_written = (insn.a == pc_idx);
        break;

      case OP_XORI:
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        registers[insn.a] = registers[insn.b] ^ (u64)(i32)insn.imm;
        pc_written = (insn.a == pc_idx);
        break;

      case OP_LEA:
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        registers[insn.a] = registers[insn.b] + (i64)(i32)insn.imm;
        pc_written = (insn.a == pc_idx);
        break;

      case OP_SHL:
        if (insn.a >= register_count || insn.b >= register_count || insn.c >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        registers[insn.a] = registers[insn.b] << (registers[insn.c] & 63u);
        pc_written = (insn.a == pc_idx);
        break;

      case OP_SHR:
        if (insn.a >= register_count || insn.b >= register_count || insn.c >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        registers[insn.a] = registers[insn.b] >> (registers[insn.c] & 63u);
        pc_written = (insn.a == pc_idx);
        break;

      case OP_SHLI:
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        registers[insn.a] = registers[insn.b] << ((u64)(i32)insn.imm & 63u);
        pc_written = (insn.a == pc_idx);
        break;

      case OP_SHRI:
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        registers[insn.a] = registers[insn.b] >> ((u64)(i32)insn.imm & 63u);
        pc_written = (insn.a == pc_idx);
        break;

      case OP_LOAD: {
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }

        u64 address = registers[insn.b] + (i64)(i32)insn.imm;
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
        pc_written = (insn.a == pc_idx);
        break;
      }

      case OP_STORE: {
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }

        u64 address = registers[insn.b] + (i64)(i32)insn.imm;
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

      case OP_LOADB: {
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }

        u8 value;
        u64 address = registers[insn.b] + (i64)(i32)insn.imm;

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
        pc_written = (insn.a == pc_idx);
        break;
      }

      case OP_STOREB: {
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }

        u64 address = registers[insn.b] + (i64)(i32)insn.imm;

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

      case OP_JMP:
        if (!jump_target_is_valid((u64)insn.imm, ram_size)) {
          RUNTIME_ERROR("invalid jump target");
        }
        registers[pc_idx] = (u64)insn.imm;
        continue;

      case OP_JMPR:
        if (insn.a >= register_count) {
          RUNTIME_ERROR("invalid jump target register");
        }
        if (!jump_target_is_valid(registers[insn.a], ram_size)) {
          RUNTIME_ERROR("invalid jump target");
        }
        registers[pc_idx] = registers[insn.a];
        continue;

      case OP_CMP:
        if (insn.a >= register_count || insn.b >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        {
          u64 left = registers[insn.a];
          u64 right = registers[insn.b];
          u64 flags = 0;
          if (left == right) {
            flags |= FLAG_ZERO;
          } else if (left < right) {
            flags |= FLAG_LESS;
          } else {
            flags |= FLAG_GREATER;
          }
          registers[flags_idx] = flags;
        }
        break;

      case OP_CMPI:
        if (insn.a >= register_count) {
          RUNTIME_ERROR("invalid register operand");
        }
        {
          u64 left = registers[insn.a];
          u64 right = (u64)(i32)insn.imm;
          u64 flags = 0;
          if (left == right) {
            flags |= FLAG_ZERO;
          } else if (left < right) {
            flags |= FLAG_LESS;
          } else {
            flags |= FLAG_GREATER;
          }
          registers[flags_idx] = flags;
        }
        break;

      case OP_JE:
      case OP_JNE:
      case OP_JL:
      case OP_JLE:
      case OP_JG:
      case OP_JGE:
        if (jump_condition_is_met(registers[flags_idx], op)) {
          if (!jump_target_is_valid((u64)insn.imm, ram_size)) {
            RUNTIME_ERROR("invalid jump target");
          }
          registers[pc_idx] = (u64)insn.imm;
          continue;
        }
        break;

      case OP_PUSH:
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

      case OP_POP:
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

      case OP_CALL:
        if (!jump_target_is_valid((u64)insn.imm, ram_size)) {
          RUNTIME_ERROR("invalid call target");
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
              next_pc
            )) {
          RUNTIME_ERROR("stack write failed");
        }

        registers[pc_idx] = (u64)insn.imm;
        continue;
      
      case OP_CALLR:
        if (insn.a >= register_count) {
          RUNTIME_ERROR("invalid target register");
        }

        if (!jump_target_is_valid(registers[insn.a], ram_size)) {
          RUNTIME_ERROR("invalid call target");
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
              next_pc
            )) {
          RUNTIME_ERROR("stack write failed");
        }

        registers[pc_idx] = registers[insn.a];
        continue;

      case OP_RET: {
        u64 return_address;

        if (registers[sp_idx] < ram_start ||
            registers[sp_idx] >= ram_end) {
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

        if (!jump_target_is_valid(return_address, ram_size)) {
          RUNTIME_ERROR("invalid return address");
        }

        registers[pc_idx] = return_address;
        continue;
      }
    
    case OP_NOP:
      break;

    case OP_DUMP_REG:
      if (insn.a >= register_count) {
        RUNTIME_ERROR("invalid register operand");
      }
      dump_register(registers, insn.a);
      break;

    case OP_DUMP_REGS:
      dump_registers(registers, NULL);
      break;

    default:
      RUNTIME_ERROR("invalid opcode");
      break;
    }

    if (!pc_written) {
      registers[pc_idx] = next_pc;
    }
  }

done:
#ifdef DEBUG
  dump_registers(registers, NULL);
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
  return exit_code;
}
