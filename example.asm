#include "runtime.asm"

main:
  loadi r1, msg
  call puts

  subi sp, sp, 32

  mov r1, sp
  loadi r2, 32
  call getline

  loadi r1, response_1
  call puts

  mov r1, sp
  call puts

  loadi r1, response_2
  call puts

  addi sp, sp, 32
  ret


getline: // (r1 = buffer, r2 = sizeof buffer) -> void
  push r5
  push r6
  push r7

  mov r5, r1          // buffer start
  mov r6, r1          // current write pointer
  mov r7, r2          // remaining buffer size

  // Cannot store anything
  cmpi r7, 0
  je .discard_line

  // Reserve space for terminating NUL
  subi r7, r7, 1

.read_loop:
  call getch          // r0 = char

  // Backspace?
  cmpi r0, 0x7f
  je .handle_backspace

  cmpi r0, 0x08
  je .handle_backspace

  cmpi r0, '\n'
  je .finish

  // Buffer full?
  cmpi r7, 0
  je .discard_rest

  storeb r0, r6, 0

  // Echo character
  mov r1, r0
  call putch

  addi r6, r6, 1
  subi r7, r7, 1

  jmp .read_loop

.handle_backspace:
  // Already at start of buffer?
  cmp r6, r5
  je .read_loop

  subi r6, r6, 1
  addi r7, r7, 1

  call backspace

  jmp .read_loop

.discard_rest:
  // Echo but don't store
  mov r1, r0
  call putch

.discard_loop:
  call getch

  cmpi r0, '\n'
  je .finish

  mov r1, r0
  call putch

  jmp .discard_loop

.finish:
  // Echo newline
  mov r1, r0
  call putch

  loadi r0, 0
  storeb r0, r6, 0

  pop r7
  pop r6
  pop r5
  ret

.discard_line:
  call getch
  cmpi r0, '\n'
  jne .discard_line

  pop r7
  pop r6
  pop r5
  ret
msg:
  .ascii "What is your name? "
  .byte 0

response_1:
  .ascii "Hello, "
  .byte 0

response_2:
  .ascii "!\n"
  .byte 0
