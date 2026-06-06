#include "runtime.asm"

main:
  loadi r1, msg
  call puts
  ret

msg:
  .ascii "Hello, world!\n"
  .byte 0
