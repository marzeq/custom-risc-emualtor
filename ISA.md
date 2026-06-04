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

Technically through, nothing stops you from emitting valid instructions in RAM and jumping to them.

## Registers

General-purpose registers are numbered `r0` through `r15`.

Five reserved registers are appended after the general-purpose set:

* `pc`: program counter
* `sp`: stack pointer
* `flags`: comparison flags
* `ram_start`: first byte of writable RAM
* `ram_end`: one-past-the-end of RAM

The assembler accepts these reserved names directly. It also accepts numeric general registers such as `r0`, `r1`, and so on.

## Flags

The `cmp` instruction sets the `flags` register using these bits:

* `zero`: operands are equal
* `less`: first operand is less than second operand
* `greater`: first operand is greater than second operand

Only one of the three bits is set for a comparison result.

## Opcodes

### Data Movement

- `loadi dst, imm`: load a 32-bit immediate into a register
- `mov dst, src`: copy a register
- `load dst, base, imm`: load a 64-bit value from memory at `base + imm`
- `store src, base, imm`: store a 64-bit value to memory at `base + imm`
- `lea dst, base, imm`: compute `base + imm` and store the result in `dst`

### Arithmetic

- `add dst, lhs, rhs`: integer addition
- `sub dst, lhs, rhs`: integer subtraction
- `mul dst, lhs, rhs`: integer multiplication
- `div dst, lhs, rhs`: integer division
- `mod dst, lhs, rhs`: integer remainder

### Arithmetic with Immediate

- `addi dst, src, imm`: integer addition with immediate
- `subi dst, src, imm`: integer subtraction with immediate
- `muli dst, src, imm`: integer multiplication with immediate
- `divi dst, src, imm`: integer division with immediate
- `modi dst, src, imm`: integer remainder with immediate

### Bitwise

- `and dst, lhs, rhs`: bitwise and
- `or dst, lhs, rhs`: bitwise or
- `xor dst, lhs, rhs`: bitwise xor
- `not dst, src`: bitwise not

### Bitwise with Immediate

- `andi dst, src, imm`: bitwise and with immediate
- `ori dst, src, imm`: bitwise or with immediate
- `xori dst, src, imm`: bitwise xor with immediate

### Shifts

- `shl dst, src, shift_reg`: shift left by value in `shift_reg` (`shift_reg & 63`)
- `shr dst, src, shift_reg`: shift right by value in `shift_reg` (`shift_reg & 63`)

### Shifts with immediate

- `shli dst, src, imm`: shift left by `imm & 63`
- `shri dst, src, imm`: shift right by `imm & 63`

### Comparison

- `cmp lhs, rhs`: compare two registers and update `flags`
- `cmpi lhs, imm`: compare a register against an immediate and update `flags`

### Control flow

* `jmp target`: unconditional jump to an immediate ROM address
* `jmpr reg`: unconditional jump to the ROM address stored in a register
* `je target`: jump if equal
* `jne target`: jump if not equal
* `jl target`: jump if less than
* `jle target`: jump if less than or equal
* `jg target`: jump if greater than
* `jge target`: jump if greater than or equal

### Function calls

* `call target`: push the return address onto the stack and jump to `target`
* `callr reg`: push the return address onto the stack and jump to the address stored in `reg`
* `ret`: pop a return address from the stack and jump to it

### Stack

* `push reg`: decrement `sp` by 8 and write the register value to RAM
* `pop reg`: read 8 bytes from RAM at `sp` and increment `sp` by 8

### Miscellaneous

* `halt`: stop execution

## Memory-Mapped IO

The first byte of the IO section is a character device at address `io`.

* Writing to `io` sends the low byte to stdout using `putchar`
* Reading from `io` returns a byte from stdin using `getchar`
* Reads past EOF return `0`

## Memory Model

Memory is laid out as:

* ROM at the start of memory
* IO immediately after ROM
* free RAM after IO

There is no hardware-managed stack region. The program must initialize `sp` itself before using `push`, `pop`, `call`, or `ret`.

A common initialization sequence is:

```asm
mov sp, ram_end
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
  mov sp, ram_end

  call hello
  halt

hello:
  loadi r0, io
  loadi r1, 'H'
  store r1, r0, 0
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
  mov sp, ram_end

  loadi r0, io
  loadi r1, 'A'
  store r1, r0, 0

  halt
```

Labels are resolved to byte offsets in ROM. Comments can start with `;` or `#`.

Immediate values may be:

* numeric literals
* labels
* the special symbol `io`
* character literals using the syntax `'.'`

Examples:

```asm
loadi r0, 123
loadi r1, io
loadi r2, message
loadi r3, 'A'
loadi r4, '\n'
```

## Suggested ABI

The ISA does not mandate an ABI, but the reference runtime uses:

- `r0`: return value
- `r1`–`r5`: arguments and caller-saved registers
- `r6`–`r13`: callee-saved registers
- `r14`: heap pointer
- `r15`: reserved

## Notes

* The assembler emits raw binary instruction streams; it does not add headers or metadata.
* The assembler accepts `-` as the output path to write the binary to stdout.
* All jump, call, and return targets must resolve to valid instruction boundaries.
