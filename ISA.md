# ISA Reference

This project uses a custom RISC-style instruction set shared by the emulator and assembler.

## Instruction Format

Each instruction is encoded as either:

```c
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
```

based on the number of register operands and whether there is an immediate operand.

## Registers

General-purpose registers are numbered `r0` through `r15`.

These reserved registers are included in addition to the general-purpose registers:

* `ip`: instruction pointer
* `sp`: stack pointer
* `flags`: comparison flags
* `machine_info`: pointer to the machine information structure (read-only)
* `ivt`: programmer written interrupt vector table pointer

The assembler accepts these reserved names directly.

## Flags

The `cmp` instruction sets the `flags` register using these bits:

* `zero`: operands are equal
* `less`: first operand is less than second operand
* `greater`: first operand is greater than second operand

Only one of the three bits is set for a comparison result.

## Machine Information

At startup the `machine_info` register contains the address of a machine information structure located in ROM.

```c
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
```

Fields:

* `version`: version number of the machine information structure format
* `self_size`: size of the machine information structure in bytes
* `ram_start`: first byte of writable RAM
* `ram_size`: size of writable RAM in bytes
* `firmware_rom_start`: first byte of firmware ROM
* `firmware_rom_size`: size of firmware ROM in bytes
* `device_count`: number of devices in the device list
* `device_size`: size of each device descriptor in bytes
* `device_list`: address of the first device descriptor

## Device Information

Devices are described by device descriptors.

```c
typedef struct {
  u64 type;
  u64 start;
  u64 size;
  u8 name[16];
} device_info;
```

Fields:

* `type`: implementation-defined device type identifier
* `start`: first byte of the device's MMIO region
* `size`: size of the MMIO region in bytes
* `name`: null-terminated ASCII string describing the device

## Opcodes

### Data Movement

* `loadi dst, imm`: load a 32-bit immediate into the low bits of `dst` and zero-extend to 64 bits
* `loadil dst, imm`: load a 32-bit immediate into the low bits of `dst` and retain the high bits of `dst`
* `loadih dst, imm`: load a 32-bit immediate into the high bits of `dst` and retain the low bits of `dst`
* `mov dst, src`: copy a register
* `load dst, base, imm`: load a 64-bit value from memory at `base + imm`
* `store src, base, imm`: store a 64-bit value to memory at `base + imm`
* `lea dst, base, imm`: compute `base + imm` and store the result in `dst`
* `load8 dst, base, imm`: load a byte from memory at `base + imm` and zero-extend it to 64 bits
* `store8 src, base, imm`: store the least significant byte of `src` to memory at `base + imm`
* `load16 dst, base, imm`: load a 16-bit value from memory at `base + imm` and zero-extend it to 64 bits
* `store16 src, base, imm`: load the least significant 16 bits of `src` to memory at `base + imm`
* `load32 dst, base, imm`: load a 32-bit value from memory at `base + imm` and zero-extend it to 64 bits
* `store32 src, base, imm`: load the least significant 32 bits of `src` to memory at `base + imm`

### Arithmetic

* `add dst, lhs, rhs`: integer addition
* `sub dst, lhs, rhs`: integer subtraction
* `mul dst, lhs, rhs`: integer multiplication
* `div dst, lhs, rhs`: integer division
* `mod dst, lhs, rhs`: integer remainder

### Arithmetic with Immediate

* `addi dst, src, imm`: integer addition with immediate
* `subi dst, src, imm`: integer subtraction with immediate
* `muli dst, src, imm`: integer multiplication with immediate
* `divi dst, src, imm`: integer division with immediate
* `modi dst, src, imm`: integer remainder with immediate

### Bitwise

* `and dst, lhs, rhs`: bitwise and
* `or dst, lhs, rhs`: bitwise or
* `xor dst, lhs, rhs`: bitwise xor
* `not dst, src`: bitwise not

### Bitwise with Immediate

* `andi dst, src, imm`: bitwise and with immediate
* `ori dst, src, imm`: bitwise or with immediate
* `xori dst, src, imm`: bitwise xor with immediate

### Shifts

* `shl dst, src, shift_reg`: shift left by value in `shift_reg` (`shift_reg & 63`)
* `shr dst, src, shift_reg`: shift right by value in `shift_reg` (`shift_reg & 63`)

### Shifts with Immediate

* `shli dst, src, imm`: shift left by `imm & 63`
* `shri dst, src, imm`: shift right by `imm & 63`

### Comparison

* `cmp lhs, rhs`: compare two registers and update `flags`
* `cmpi lhs, imm`: compare a register against an immediate and update `flags`

### Control Flow

* `jmp target`: unconditional jump to an immediate address
* `jmpr reg`: unconditional jump to the address stored in a register
* `je target`: jump if equal
* `jne target`: jump if not equal
* `jl target`: jump if less than
* `jle target`: jump if less than or equal
* `jg target`: jump if greater than
* `jge target`: jump if greater than or equal

### Function Calls

* `call target`: push the return address onto the stack and jump to `target`
* `callr reg`: push the return address onto the stack and jump to the address stored in `reg`
* `ret`: pop a return address from the stack and jump to it

### Interrupts

* `int imm`: trigger a software interrupt with the given immediate number
* `iret`: return from an interrupt

### Stack

* `push reg`: decrement `sp` by 8 and write the register value to memory
* `pop reg`: read 8 bytes from memory at `sp` and increment `sp` by 8

### Miscellaneous

* `halt`: stop execution
* `nop`: no operation

## Memory Model

Apart from the first instruction fetch being `0x00...`, there are no fixed memory mappings. Programs can use the `machine_info` structure to discover where RAM and ROM are located.

## Stack Initialization

There is no hardware-managed stack region. Programs must initialize `sp` before using `push`, `pop`, `call`, or `ret`.

A common initialization sequence is:

```asm
load r0, machine_info, 16   // ram_start
load r1, machine_info, 24   // ram_size

add sp, r0, r1
```

which places the stack at the end of available RAM and allows it to grow downward.

## Function Calls

Function calls use the program stack.

`call` and `callr`:

1. Push the return address (`ip + 8`) onto the stack.
2. Transfer control to the target.

`ret`:

1. Pop a return address from the stack.
2. Transfer control to that address.

Example:

```asm
start:
  load r0, machine_info, 16
  load r1, machine_info, 24

  add sp, r0, r1

  call hello
  halt

hello:
  ret
```

Function pointers can be invoked with `callr`:

```asm
loadi r0, hello
callr r0
```

## Assembler Syntax

The assembler is line-oriented and supports labels.

Examples:

```asm
start:
  loadi r0, 123
  loadi r1, 456
  add r2, r0, r1
  halt
```

Labels are resolved to byte offsets in ROM.

Comments can start with `//`. `;` is *NOT* a comment character, it is a line separator for multiple instructions on the same line.

Immediate values may be:

* numeric literals
* labels
* character literals using the syntax `'.'`

Examples:

```asm
loadi r0, 123
loadi r1, message
loadi r2, 'A'
loadi r3, '\n'
```

Scoped labels prefixed with `.` relative to the previous non-scoped label are supported:

```asm
func:
  .loop: // resolved to func.loop
    // ... loop body
    jmp .loop

func2:
  .loop: // resolved to func2.loop - does not conflict with previous .loop
    // ... loop body
    jmp .loop
```

### Directives

The assembler supports these directives:

* `.entry (label)`: first instruction should be `jump (label)` to set the entry point. Local labels are not allowed in the entry point. May only be used once.
* `.byte (byte)`: emit a single byte with the given value
* `.quad (quad)`: emit an 8-byte little-endian value with the given value
* `.ascii (string)`: emit the bytes of the given string without a null terminator

### Preprocessor

The assembly is first ran through the standard C preprocessor, so it supports `#define`, `#include`, and so on.

## Suggested ABI

The ISA does not mandate an ABI, but the reference runtime uses:

* `r0`: return value
* `r1`–`r5`: arguments and caller-saved registers
* `r6`–`r13`: callee-saved registers
* `r14-r15`: reserved

## Runtime

We provide a reference runtime which sets up the stack, ivt, and provides basic I/O functions for use by assembly programs before calling the `main` function.

See `runtime.asm` for details.

## Notes

* The assembler emits raw binary instruction streams; it does not add headers or metadata.
* The assembler accepts `-` as the output path to write the binary to stdout.
