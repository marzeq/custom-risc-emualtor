#ifndef ISA_H
#define ISA_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

typedef uint64_t u64;
typedef int64_t i64;
typedef uint32_t u32;
typedef int32_t i32;
typedef uint16_t u16;
typedef uint8_t u8;
typedef size_t usz;

typedef struct [[gnu::packed]] {
  u8 opcode;
} instruction_0reg;

typedef struct [[gnu::packed]] {
  u8 opcode;
  u8 a;
} instruction_1reg;

typedef struct [[gnu::packed]] {
  u8 opcode;
  u8 a;
  u8 b;
} instruction_2reg;

typedef struct [[gnu::packed]] {
  u8 opcode;
  u8 a;
  u8 b;
  u8 c;
} instruction_3reg;

typedef struct [[gnu::packed]] {
  u8 opcode;
  u64 imm;
} instruction_0reg_imm;

typedef struct [[gnu::packed]] {
  u8 opcode;
  u8 a;
  u64 imm;
} instruction_1reg_imm;

typedef struct [[gnu::packed]] {
  u8 opcode;
  u8 a;
  u8 b;
  u64 imm;
} instruction_2reg_imm;

typedef enum {
  INSN_TYPE_0REG,
  INSN_TYPE_1REG,
  INSN_TYPE_2REG,
  INSN_TYPE_3REG,
  INSN_TYPE_0REG_IMM,
  INSN_TYPE_1REG_IMM,
  INSN_TYPE_2REG_IMM,
} instruction_type;

static inline usz size_for_instruction_type(instruction_type type) {
  switch (type) {
    case INSN_TYPE_0REG: return sizeof(instruction_0reg);
    case INSN_TYPE_1REG: return sizeof(instruction_1reg);
    case INSN_TYPE_2REG: return sizeof(instruction_2reg);
    case INSN_TYPE_3REG: return sizeof(instruction_3reg);
    case INSN_TYPE_0REG_IMM: return sizeof(instruction_0reg_imm);
    case INSN_TYPE_1REG_IMM: return sizeof(instruction_1reg_imm);
    case INSN_TYPE_2REG_IMM: return sizeof(instruction_2reg_imm);
    default: assert(false && "invalid instruction type"); return 0;
  }
}

#define GENERAL_REGISTER_COUNT 16

typedef enum {
  REG_SLOT_IP = 0,
  REG_SLOT_SP = 1,
  REG_SLOT_FLAGS = 2,
  REG_SLOT_MACHINE_INFO = 3,
  REG_SLOT_IVT = 4,
  RESERVED_REGISTER_COUNT = 5,
} reserved_register_slot;

typedef enum {
  // 0x00-0x0f: data movement
  OP_HALT      = 0x00,
  OP_LOADI     = 0x01,
  OP_MOV       = 0x02,
  OP_LOAD      = 0x03,
  OP_STORE     = 0x04,
  OP_LEA       = 0x05,
  OP_LOADB     = 0x06,
  OP_STOREB    = 0x07,

  // 0x10-0x1f: arithmetic
  OP_ADD       = 0x10,
  OP_SUB       = 0x11,
  OP_MUL       = 0x12,
  OP_DIV       = 0x13,
  OP_MOD       = 0x14,

  OP_ADDI      = 0x15,
  OP_SUBI      = 0x16,
  OP_MULI      = 0x17,
  OP_DIVI      = 0x18,
  OP_MODI      = 0x19,

  // 0x20-0x2f: bitwise
  OP_AND       = 0x20,
  OP_OR        = 0x21,
  OP_XOR       = 0x22,
  OP_NOT       = 0x23,

  OP_ANDI      = 0x24,
  OP_ORI       = 0x25,
  OP_XORI      = 0x26,

  OP_SHL       = 0x27,
  OP_SHR       = 0x28,

  OP_SHLI      = 0x29,
  OP_SHRI      = 0x2a,

  // 0x30-0x3f: compare/branch
  OP_CMP       = 0x30,
  OP_CMPI      = 0x31,

  OP_JMP       = 0x32,
  OP_JMPR      = 0x33,

  OP_JE        = 0x34,
  OP_JNE       = 0x35,
  OP_JL        = 0x36,
  OP_JLE       = 0x37,
  OP_JG        = 0x38,
  OP_JGE       = 0x39,

  // 0x40-0x4f: calls
  OP_CALL      = 0x40,
  OP_CALLR     = 0x41,
  OP_RET       = 0x42,

  OP_INT       = 0x4a, // trigger interrupt
  OP_IRET      = 0x4b, // return from interrupt

  // 0x50-0x5f: stack
  OP_PUSH      = 0x50,
  OP_POP       = 0x51,

  // 0xf0 - 0xff: reserved for special purposes
  OP_NOP       = 0xf0,
  OP_DUMP_REG  = 0xf3, // trigger a dump of specified register in emulator
  OP_DUMP_REGS = 0xf4, // trigger a dump of all registers in emulator
} opcode;

static inline instruction_type opcode_instruction_type(opcode op) {
  switch (op) {
    case OP_HALT:
    case OP_RET:
    case OP_IRET:
    case OP_NOP:
    case OP_DUMP_REGS:
      return INSN_TYPE_0REG;

    case OP_JMPR:
    case OP_CALLR:
    case OP_PUSH:
    case OP_POP:
    case OP_DUMP_REG:
      return INSN_TYPE_1REG;

    case OP_MOV:
    case OP_NOT:
    case OP_CMP:
      return INSN_TYPE_2REG;

    case OP_ADD:
    case OP_SUB:
    case OP_MUL:
    case OP_DIV:
    case OP_MOD:
    case OP_AND:
    case OP_OR:
    case OP_XOR:
    case OP_SHL:
    case OP_SHR:
      return INSN_TYPE_3REG;

    case OP_JMP:
    case OP_JE:
    case OP_JNE:
    case OP_JL:
    case OP_JLE:
    case OP_JG:
    case OP_JGE:
    case OP_CALL:
    case OP_INT:
      return INSN_TYPE_0REG_IMM;

    case OP_LOADI:
    case OP_CMPI:
      return INSN_TYPE_1REG_IMM;

    case OP_LOAD:
    case OP_STORE:
    case OP_LEA:
    case OP_LOADB:
    case OP_STOREB:
    case OP_ADDI:
    case OP_SUBI:
    case OP_MULI:
    case OP_DIVI:
    case OP_MODI:
    case OP_ANDI:
    case OP_ORI:
    case OP_XORI:
    case OP_SHLI:
    case OP_SHRI:
      return INSN_TYPE_2REG_IMM;
  }

  assert(false && "invalid opcode");
}

static inline size_t reserved_register_index(reserved_register_slot slot) {
  return GENERAL_REGISTER_COUNT + (size_t)slot;
}

#endif
