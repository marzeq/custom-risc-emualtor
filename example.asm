#include "runtime.asm"

main:
  loadi r0, 0xffffffff
  dump_reg r0
  ret
