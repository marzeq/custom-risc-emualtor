#include "runtime.asm"

#define NAME_BUF_SIZE 32

main:
  loadi r1, msg
  call puts

  subi sp, sp, NAME_BUF_SIZE

  mov r1, sp
  loadi r2, NAME_BUF_SIZE
  call getline

  loadi r1, response_1
  call puts

  mov r1, sp
  call puts

  loadi r1, response_2
  call puts

  addi sp, sp, NAME_BUF_SIZE
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
