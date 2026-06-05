# ISA Reference

This project uses a fixed-width 8-byte little-endian instruction format shared by the emulator and assembler.

## Instruction Format

Each instruction is encoded as:

* byte 0: opcode
* byte 1: register `a`
* byte 2: register `b`
* byte 3: register `c`
* bytes 4-7: 32-bit immediate value, little-endian

All instruction addresses are byte offsets from the start of ROM. Jumps must target instruction boundaries, so valid targets are multiples of 8.

Technically, nothing stops you from emitting valid instructions in RAM and jumping to them.

## Registers

General-purpose registers are numbered `r0` through `r15`.

Four reserved registers are appended after the general-purpose set:

* `pc`: program counter
* `sp`: stack pointer
* `flags`: comparison flags
* `machine_info`: pointer to the machine information structure

The assembler accepts these reserved names directly. It also accepts numeric general registers such as `r0`, `r1`, and so on.

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

The machine information structure contains both the number of devices and the address of the device descriptor list.

Programs can enumerate devices by reading the device list.

## Opcodes

### Data Movement

* `loadi dst, imm`: load a 32-bit immediate into the low bits of `dst` and zero-extend to 64 bits
* `loadil dst, imm`: load a 32-bit immediate into the low bits of `dst` and retain the high bits of `dst`
* `loadih dst, imm`: load a 32-bit immediate into the high bits of `dst` and retain the low bits of `dst`
* `mov dst, src`: copy a register
* `load dst, base, imm`: load a 64-bit value from memory at `base + imm`
* `store src, base, imm`: store a 64-bit value to memory at `base + imm`
* `lea dst, base, imm`: compute `base + imm` and store the result in `dst`
* `loadb dst, base, imm`: load a byte from memory at `base + imm` and zero-extend it to 64 bits
* `storeb src, base, imm`: store the least significant byte of `src` to memory at `base + imm`

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

### Stack

* `push reg`: decrement `sp` by 8 and write the register value to memory
* `pop reg`: read 8 bytes from memory at `sp` and increment `sp` by 8

### Miscellaneous

* `halt`: stop execution
* `nop`: no operation
* `dump_reg reg`: dump a register to the console in the emulator
* `dump_regs`: dump all registers to the console in the emulator

## Memory Model

The architecture exposes a single byte-addressed address space containing ROM, MMIO devices, and RAM.

The address space layout is:
```text
Firmware ROM
Machine Information ROM
Device Information ROM
RAM
MMIO Regions
```

Programs must discover RAM and devices through the machine information structure rather than assuming fixed addresses.

All addresses are byte offsets within the address space.

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

1. Push the return address (`pc + 8`) onto the stack.
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

## Suggested ABI

The ISA does not mandate an ABI, but the reference runtime uses:

* `r0`: return value
* `r1`–`r5`: arguments and caller-saved registers
* `r6`–`r13`: callee-saved registers
* `r14-r15`: reserved

## Notes

* The assembler emits raw binary instruction streams; it does not add headers or metadata.
* The assembler accepts `-` as the output path to write the binary to stdout.
* All jump, call, and return targets must resolve to valid instruction boundaries.
