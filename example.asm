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

msg:
  .ascii "What is your name? "
  .byte 0

response_1:
  .ascii "Hello, "
  .byte 0

response_2:
  .ascii "!\n"
  .byte 0
