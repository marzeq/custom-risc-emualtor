#ifndef EMU_ISA_H
#define EMU_ISA_H

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

typedef struct {
  u8 opcode;
  u8 a;
  u8 b;
  u8 c;
  u32 imm;
} instruction;

#define INSN_SIZE 8

#define _STRINGIFY(x) #x
#define STRINGIFY(x) _STRINGIFY(x)
static_assert(sizeof(instruction) == INSN_SIZE, "instruction struct must be exactly " STRINGIFY(INSN_SIZE) " bytes");

#define EMU_GENERAL_REGISTER_COUNT 16

typedef enum {
  EMU_REG_SLOT_PC = 0,
  EMU_REG_SLOT_SP = 1,
  EMU_REG_SLOT_FLAGS = 2,
  EMU_REG_SLOT_MACHINE_INFO = 3,
  EMU_RESERVED_REGISTER_COUNT = 4,
} emu_reserved_register_slot;

typedef enum {
  /* 0x00-0x0f: data movement */
  OP_HALT   = 0x00,
  OP_LOADI  = 0x01,
  OP_MOV    = 0x02,
  OP_LOAD   = 0x03,
  OP_STORE  = 0x04,
  OP_LEA    = 0x05,
  OP_LOADB  = 0x06,
  OP_STOREB = 0x07,
  OP_LOADIL = 0x08,
  OP_LOADIH = 0x09,

  /* 0x10-0x1f: arithmetic */
  OP_ADD   = 0x10,
  OP_SUB   = 0x11,
  OP_MUL   = 0x12,
  OP_DIV   = 0x13,
  OP_MOD   = 0x14,

  OP_ADDI  = 0x15,
  OP_SUBI  = 0x16,
  OP_MULI  = 0x17,
  OP_DIVI  = 0x18,
  OP_MODI  = 0x19,

  /* 0x20-0x2f: bitwise */
  OP_AND   = 0x20,
  OP_OR    = 0x21,
  OP_XOR   = 0x22,
  OP_NOT   = 0x23,

  OP_ANDI  = 0x24,
  OP_ORI   = 0x25,
  OP_XORI  = 0x26,

  OP_SHL   = 0x27,
  OP_SHR   = 0x28,

  OP_SHLI  = 0x29,
  OP_SHRI  = 0x2a,

  /* 0x30-0x3f: compare/branch */
  OP_CMP   = 0x30,
  OP_CMPI  = 0x31,

  OP_JMP   = 0x32,
  OP_JMPR  = 0x33,

  OP_JE    = 0x34,
  OP_JNE   = 0x35,
  OP_JL    = 0x36,
  OP_JLE   = 0x37,
  OP_JG    = 0x38,
  OP_JGE   = 0x39,

  /* 0x40-0x4f: calls */
  OP_CALL  = 0x40,
  OP_CALLR = 0x41,
  OP_RET   = 0x42,

  /* 0x50-0x5f: stack */
  OP_PUSH  = 0x50,
  OP_POP   = 0x51,

  /* 0xf0 - 0xff: reserved for special purposes */
  OP_NOP = 0xf0,
  OP_DUMP_REG = 0xf3, // trigger a dump of specified register in emulator
  OP_DUMP_REGS = 0xf4, // trigger a dump of all registers in emulator
} opcode;

static inline size_t emu_reserved_register_index(emu_reserved_register_slot slot) {
  return EMU_GENERAL_REGISTER_COUNT + (size_t)slot;
}

#endif
