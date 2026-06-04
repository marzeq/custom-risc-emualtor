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

#define MiB(x) ((x) * 1024 * 1024)

#define FLAG_ZERO    (1u << 0)
#define FLAG_LESS    (1u << 1)
#define FLAG_GREATER (1u << 2)

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

static bool read_u64_memory(const u8* memory, usz mem_size, u64 address, u64* value) {
  if (address == EMU_IO_ADDRESS) {
    int input = getchar();
    if (input == EOF) {
      *value = 0;
    } else {
      *value = (u64)(u8)input;
    }
    return true;
  }
  if (address > (u64)mem_size - sizeof(u64)) {
    return false;
  }
  if (address >= EMU_IO_ADDRESS && address < EMU_IO_ADDRESS + sizeof(u64)) {
    return false;
  }
  *value = read_u64_le(&memory[address]);
  return true;
}

static bool write_u64_memory(u8* memory, usz mem_size, u64 address, u64 value) {
  if (address == EMU_IO_ADDRESS) {
    fputc((int)(value & 0xFFu), stdout);
    fflush(stdout);
    return true;
  }
  if (address > (u64)mem_size - sizeof(u64)) {
    return false;
  }
  if (address >= EMU_IO_ADDRESS && address < EMU_IO_ADDRESS + sizeof(u64)) {
    return false;
  }
  write_u64_le(&memory[address], value);
  return true;
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

static bool jump_target_is_valid(u64 target, u64 memory_size) {
  return target < memory_size && target % INSN_SIZE == 0;
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

int main(int argc, char** argv) {
  usz mem_size = MiB(512);
  const usz rom_size = MiB(16);
  const usz io_size = MiB(16);
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
    mem_size = MiB(strtoull(argv[2], NULL, 10));
  }
  const usz register_count = EMU_GENERAL_REGISTER_COUNT + EMU_RESERVED_REGISTER_COUNT;

  if (rom_size + io_size > mem_size) {
    fprintf(stderr, "Error: Memory size must be at least %zu bytes to accommodate ROM and IO\n", rom_size + io_size);
    return 1;
  }

  if (rom_size % INSN_SIZE != 0) {
    fprintf(stderr, "Error: ROM size must be a multiple of instruction size\n");
    return 1;
  }

  u8* memory = NULL;
  u64* registers = NULL;
  FILE* binary_file = NULL;
  int exit_code = 1;

  memory = malloc(mem_size);
  if (!memory) {
    fprintf(stderr, "Error: Could not allocate memory\n");
    goto done;
  }
  memset(memory, 0, mem_size);

  registers = malloc(register_count * sizeof(u64));
  if (!registers) {
    fprintf(stderr, "Error: Could not allocate registers\n");
    goto done;
  }
  memset(registers, 0, register_count * sizeof(u64));

  const usz pc_idx = emu_reserved_register_index(EMU_REG_SLOT_PC);
  const usz sp_idx = emu_reserved_register_index(EMU_REG_SLOT_SP);
  const usz flags_idx = emu_reserved_register_index(EMU_REG_SLOT_FLAGS);
  const usz ram_start_idx = emu_reserved_register_index(EMU_REG_SLOT_RAM_START);
  const usz ram_end_idx = emu_reserved_register_index(EMU_REG_SLOT_RAM_END);

  const u64 rom_end = rom_size;
  const u64 io_start = rom_end;
  const u64 io_end = io_start + io_size;
  const u64 ram_start = io_end;
  const u64 ram_end = mem_size;

  registers[ram_start_idx] = ram_start;
  registers[ram_end_idx] = ram_end;
  registers[pc_idx] = 0;
  registers[sp_idx] = 0;
  registers[flags_idx] = 0;

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
  if (binary_size > rom_size) {
    fprintf(stderr, "Error: Binary size (%zu bytes) exceeds ROM size (%zu bytes)\n", binary_size, rom_size);
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

  size_t read_size = fread(memory, 1, binary_size, binary_file);
  if (read_size != binary_size) {
    fprintf(stderr, "Error: Could not read entire binary file (read %zu bytes, expected %zu bytes)\n", read_size, binary_size);
    goto done;
  }

  terminal_raw_enable();

  for (;;) {
    u64 pc = registers[pc_idx];
    if (pc > rom_end - INSN_SIZE) {
      fprintf(stderr, "Runtime error: program counter out of ROM bounds (%llu)\n", (unsigned long long)pc);
      goto done;
    }

    instruction insn = decode_instruction(&memory[pc]);
    opcode op = (opcode)insn.opcode;
    u64 next_pc = pc + INSN_SIZE;
    bool pc_written = false;

    switch (op) {
      case OP_HALT:
        exit_code = 0;
        goto done;

      case OP_LOADI:
        if (insn.a >= register_count) {
          fprintf(stderr, "Runtime error: invalid destination register %u\n", insn.a);
          goto done;
        }
        registers[insn.a] = (u64)(i32)insn.imm;
        pc_written = (insn.a == pc_idx);
        break;

      case OP_MOV:
        if (insn.a >= register_count || insn.b >= register_count) {
          fprintf(stderr, "Runtime error: invalid register operand\n");
          goto done;
        }
        registers[insn.a] = registers[insn.b];
        pc_written = (insn.a == pc_idx);
        break;

      case OP_ADD:
        if (insn.a >= register_count || insn.b >= register_count || insn.c >= register_count) {
          fprintf(stderr, "Runtime error: invalid register operand\n");
          goto done;
        }
        registers[insn.a] = registers[insn.b] + registers[insn.c];
        pc_written = (insn.a == pc_idx);
        break;

      case OP_SUB:
        if (insn.a >= register_count || insn.b >= register_count || insn.c >= register_count) {
          fprintf(stderr, "Runtime error: invalid register operand\n");
          goto done;
        }
        registers[insn.a] = registers[insn.b] - registers[insn.c];
        pc_written = (insn.a == pc_idx);
        break;

      case OP_MUL:
        if (insn.a >= register_count || insn.b >= register_count || insn.c >= register_count) {
          fprintf(stderr, "Runtime error: invalid register operand\n");
          goto done;
        }
        registers[insn.a] = registers[insn.b] * registers[insn.c];
        pc_written = (insn.a == pc_idx);
        break;
      
      case OP_DIV:
        if (insn.a >= register_count || insn.b >= register_count || insn.c >= register_count) {
          fprintf(stderr, "Runtime error: invalid register operand\n");
          goto done;
        }
        if (registers[insn.c] == 0) {
          fprintf(stderr, "Runtime error: division by zero\n");
          goto done;
        }
        registers[insn.a] = registers[insn.b] / registers[insn.c];
        pc_written = (insn.a == pc_idx);
        break;

      case OP_ADDI:
        if (insn.a >= register_count || insn.b >= register_count) {
          fprintf(stderr, "Runtime error: invalid register operand\n");
          goto done;
        }
        registers[insn.a] = registers[insn.b] + (u64)(i32)insn.imm;
        pc_written = (insn.a == pc_idx);
        break;

      case OP_SUBI:
        if (insn.a >= register_count || insn.b >= register_count) {
          fprintf(stderr, "Runtime error: invalid register operand\n");
          goto done;
        }
        registers[insn.a] = registers[insn.b] - (u64)(i32)insn.imm;
        pc_written = (insn.a == pc_idx);
        break;

      case OP_MULI:
        if (insn.a >= register_count || insn.b >= register_count) {
          fprintf(stderr, "Runtime error: invalid register operand\n");
          goto done;
        }
        registers[insn.a] = registers[insn.b] * (u64)(i32)insn.imm;
        pc_written = (insn.a == pc_idx);
        break;

      case OP_DIVI:
        if (insn.a >= register_count || insn.b >= register_count) {
          fprintf(stderr, "Runtime error: invalid register operand\n");
          goto done;
        }
        if ((i32)insn.imm == 0) {
          fprintf(stderr, "Runtime error: division by zero\n");
          goto done;
        }
        registers[insn.a] = registers[insn.b] / (u64)(i32)insn.imm;
        pc_written = (insn.a == pc_idx);
        break;

      case OP_MOD:
        if (insn.a >= register_count || insn.b >= register_count || insn.c >= register_count) {
          fprintf(stderr, "Runtime error: invalid register operand\n");
          goto done;
        }
        if (registers[insn.c] == 0) {
          fprintf(stderr, "Runtime error: division by zero\n");
          goto done;
        }
        registers[insn.a] = registers[insn.b] % registers[insn.c];
        pc_written = (insn.a == pc_idx);
        break;

      case OP_MODI:
        if (insn.a >= register_count || insn.b >= register_count) {
          fprintf(stderr, "Runtime error: invalid register operand\n");
          goto done;
        }
        if ((i32)insn.imm == 0) {
          fprintf(stderr, "Runtime error: division by zero\n");
          goto done;
        }
        registers[insn.a] = registers[insn.b] % (u64)(i32)insn.imm;
        pc_written = (insn.a == pc_idx);
        break;

      case OP_AND:
        if (insn.a >= register_count || insn.b >= register_count || insn.c >= register_count) {
          fprintf(stderr, "Runtime error: invalid register operand\n");
          goto done;
        }
        registers[insn.a] = registers[insn.b] & registers[insn.c];
        pc_written = (insn.a == pc_idx);
        break;

      case OP_OR:
        if (insn.a >= register_count || insn.b >= register_count || insn.c >= register_count) {
          fprintf(stderr, "Runtime error: invalid register operand\n");
          goto done;
        }
        registers[insn.a] = registers[insn.b] | registers[insn.c];
        pc_written = (insn.a == pc_idx);
        break;

      case OP_XOR:
        if (insn.a >= register_count || insn.b >= register_count || insn.c >= register_count) {
          fprintf(stderr, "Runtime error: invalid register operand\n");
          goto done;
        }
        registers[insn.a] = registers[insn.b] ^ registers[insn.c];
        pc_written = (insn.a == pc_idx);
        break;

      case OP_NOT:
        if (insn.a >= register_count || insn.b >= register_count) {
          fprintf(stderr, "Runtime error: invalid register operand\n");
          goto done;
        }
        registers[insn.a] = ~registers[insn.b];
        pc_written = (insn.a == pc_idx);
        break;

      case OP_ANDI:
        if (insn.a >= register_count || insn.b >= register_count) {
          fprintf(stderr, "Runtime error: invalid register operand\n");
          goto done;
        }
        registers[insn.a] = registers[insn.b] & (u64)(i32)insn.imm;
        pc_written = (insn.a == pc_idx);
        break;

      case OP_ORI:
        if (insn.a >= register_count || insn.b >= register_count) {
          fprintf(stderr, "Runtime error: invalid register operand\n");
          goto done;
        }
        registers[insn.a] = registers[insn.b] | (u64)(i32)insn.imm;
        pc_written = (insn.a == pc_idx);
        break;

      case OP_XORI:
        if (insn.a >= register_count || insn.b >= register_count) {
          fprintf(stderr, "Runtime error: invalid register operand\n");
          goto done;
        }
        registers[insn.a] = registers[insn.b] ^ (u64)(i32)insn.imm;
        pc_written = (insn.a == pc_idx);
        break;

      case OP_LEA:
        if (insn.a >= register_count || insn.b >= register_count) {
          fprintf(stderr, "Runtime error: invalid register operand\n");
          goto done;
        }
        registers[insn.a] = registers[insn.b] + (i64)(i32)insn.imm;
        pc_written = (insn.a == pc_idx);
        break;

      case OP_SHL:
        if (insn.a >= register_count || insn.b >= register_count || insn.c >= register_count) {
          fprintf(stderr, "Runtime error: invalid register operand\n");
          goto done;
        }
        registers[insn.a] = registers[insn.b] << (registers[insn.c] & 63u);
        pc_written = (insn.a == pc_idx);
        break;

      case OP_SHR:
        if (insn.a >= register_count || insn.b >= register_count || insn.c >= register_count) {
          fprintf(stderr, "Runtime error: invalid register operand\n");
          goto done;
        }
        registers[insn.a] = registers[insn.b] >> (registers[insn.c] & 63u);
        pc_written = (insn.a == pc_idx);
        break;

      case OP_SHLI:
        if (insn.a >= register_count || insn.b >= register_count) {
          fprintf(stderr, "Runtime error: invalid register operand\n");
          goto done;
        }
        registers[insn.a] = registers[insn.b] << ((u64)(i32)insn.imm & 63u);
        pc_written = (insn.a == pc_idx);
        break;

      case OP_SHRI:
        if (insn.a >= register_count || insn.b >= register_count) {
          fprintf(stderr, "Runtime error: invalid register operand\n");
          goto done;
        }
        registers[insn.a] = registers[insn.b] >> ((u64)(i32)insn.imm & 63u);
        pc_written = (insn.a == pc_idx);
        break;

      case OP_LOAD: {
        if (insn.a >= register_count || insn.b >= register_count) {
          fprintf(stderr, "Runtime error: invalid register operand\n");
          goto done;
        }

        u64 address = registers[insn.b] + (i64)(i32)insn.imm;
        if (address != EMU_IO_ADDRESS && ((address >= io_start && address < io_end) || address > (u64)mem_size - sizeof(u64))) {
          fprintf(stderr, "Runtime error: invalid load address %llu\n", (unsigned long long)address);
          goto done;
        }
        if (!read_u64_memory(memory, mem_size, address, &registers[insn.a])) {
          fprintf(stderr, "Runtime error: load address out of bounds (%llu)\n", (unsigned long long)address);
          goto done;
        }
        pc_written = (insn.a == pc_idx);
        break;
      }

      case OP_STORE: {
        if (insn.a >= register_count || insn.b >= register_count) {
          fprintf(stderr, "Runtime error: invalid register operand\n");
          goto done;
        }

        u64 address = registers[insn.b] + (i64)(i32)insn.imm;
        if (address != EMU_IO_ADDRESS && (address < ram_start || address >= mem_size)) {
          fprintf(stderr, "Runtime error: invalid store address %llu\n", (unsigned long long)address);
          goto done;
        }
        if (!write_u64_memory(memory, mem_size, address, registers[insn.a])) {
          fprintf(stderr, "Runtime error: store address out of bounds (%llu)\n", (unsigned long long)address);
          goto done;
        }
        break;
      }

      case OP_JMP:
        if (!jump_target_is_valid((u64)insn.imm, mem_size)) {
          fprintf(stderr, "Runtime error: invalid jump target %u\n", insn.imm);
          goto done;
        }
        registers[pc_idx] = (u64)insn.imm;
        continue;

      case OP_JMPR:
        if (insn.a >= register_count) {
          fprintf(stderr, "Runtime error: invalid jump target register\n");
          goto done;
        }
        if (!jump_target_is_valid(registers[insn.a], mem_size)) {
          fprintf(stderr, "Runtime error: invalid jump target %llu\n", (unsigned long long)registers[insn.a]);
          goto done;
        }
        registers[pc_idx] = registers[insn.a];
        continue;

      case OP_CMP:
        if (insn.a >= register_count || insn.b >= register_count) {
          fprintf(stderr, "Runtime error: invalid register operand\n");
          goto done;
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
          fprintf(stderr, "Runtime error: invalid register operand\n");
          goto done;
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
          if (!jump_target_is_valid((u64)insn.imm, mem_size)) {
            fprintf(stderr, "Runtime error: invalid jump target %u\n", insn.imm);
            goto done;
          }
          registers[pc_idx] = (u64)insn.imm;
          continue;
        }
        break;

      case OP_PUSH:
        if (insn.a >= register_count) {
          fprintf(stderr, "Runtime error: invalid register operand\n");
          goto done;
        }
        if (registers[sp_idx] < ram_start + sizeof(u64) || registers[sp_idx] > ram_end) {
          fprintf(stderr, "Runtime error: stack overflow\n");
          goto done;
        }
        registers[sp_idx] -= sizeof(u64);
        if (!write_u64_memory(memory, mem_size, registers[sp_idx], registers[insn.a])) {
          fprintf(stderr, "Runtime error: stack write failed\n");
          goto done;
        }
        break;

      case OP_POP:
        if (insn.a >= register_count) {
          fprintf(stderr, "Runtime error: invalid register operand\n");
          goto done;
        }
        if (registers[sp_idx] < ram_start || registers[sp_idx] >= ram_end) {
          fprintf(stderr, "Runtime error: stack underflow\n");
          goto done;
        }
        if (!read_u64_memory(memory, mem_size, registers[sp_idx], &registers[insn.a])) {
          fprintf(stderr, "Runtime error: stack read failed\n");
          goto done;
        }
        registers[sp_idx] += sizeof(u64);
        break;

      case OP_CALL:
        if (!jump_target_is_valid((u64)insn.imm, mem_size)) {
            fprintf(stderr, "Runtime error: invalid call target %u\n", insn.imm);
            goto done;
        }

        if (registers[sp_idx] < ram_start + sizeof(u64) ||
            registers[sp_idx] > ram_end) {
            fprintf(stderr, "Runtime error: stack overflow\n");
            goto done;
        }

        registers[sp_idx] -= sizeof(u64);

        if (!write_u64_memory(memory, mem_size,
                              registers[sp_idx],
                              next_pc)) {
            fprintf(stderr, "Runtime error: stack write failed\n");
            goto done;
        }

        registers[pc_idx] = (u64)insn.imm;
        continue;
      
      case OP_CALLR:
        if (insn.a >= register_count) {
            fprintf(stderr, "Runtime error: invalid target register\n");
            goto done;
        }

        if (!jump_target_is_valid(registers[insn.a], mem_size)) {
            fprintf(stderr,
                    "Runtime error: invalid call target %llu\n",
                    (unsigned long long)registers[insn.a]);
            goto done;
        }

        if (registers[sp_idx] < ram_start + sizeof(u64) ||
            registers[sp_idx] > ram_end) {
            fprintf(stderr, "Runtime error: stack overflow\n");
            goto done;
        }

        registers[sp_idx] -= sizeof(u64);

        if (!write_u64_memory(memory,
                              mem_size,
                              registers[sp_idx],
                              next_pc)) {
            fprintf(stderr, "Runtime error: stack write failed\n");
            goto done;
        }

        registers[pc_idx] = registers[insn.a];
        continue;

      case OP_RET: {
        u64 return_address;

        if (registers[sp_idx] < ram_start ||
            registers[sp_idx] >= ram_end) {
            fprintf(stderr, "Runtime error: stack underflow\n");
            goto done;
        }

        if (!read_u64_memory(memory, mem_size,
                            registers[sp_idx],
                            &return_address)) {
            fprintf(stderr, "Runtime error: stack read failed\n");
            goto done;
        }

        registers[sp_idx] += sizeof(u64);

        if (!jump_target_is_valid(return_address, mem_size)) {
            fprintf(stderr,
                    "Runtime error: invalid return address %llu\n",
                    (unsigned long long)return_address);
            goto done;
        }

        registers[pc_idx] = return_address;
        continue;
      }
    }

    if (!pc_written) {
      registers[pc_idx] = next_pc;
    }
  }

done:
  if (binary_file) {
    fclose(binary_file);
  }
  if (memory) {
    free(memory);
  }
  if (registers) {
    free(registers);
  }
  return exit_code;
}
