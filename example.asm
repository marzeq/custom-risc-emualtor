#include "runtime.asm"

main:
  loadi r0, 0
  dump_reg r0
  addi r0, r0, 1
  dump_reg r0
  muli r0, r0, 2
  dump_reg r0
  muli r0, r0, 2
  dump_reg sp
  ret
