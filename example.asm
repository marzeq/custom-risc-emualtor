#include "runtime.asm"

main:
  call getn
  mov r1, r0
  call factorial
  mov r1, r0
  call putn
  loadi r1, '\n'
  call putch
  ret

fib: ; (r1) -> r0
  cmpi r1, 1
  jle fib_base

  push r1

  subi r1, r1, 1
  call fib

  mov r2, r0 ; save fib(n - 1)

  pop r1
  push r2 ; save fib(n - 1)

  subi r1, r1, 2
  call fib

  pop r2 ; restore fib(n - 1)

  add r0, r0, r2
  ret
fib_base:
  mov r0, r1
  ret

factorial: ; (r1) -> r0
  loadi r0, 1
  cmpi r1, 1
  jle fact_done
  fact_loop:
    mul r0, r0, r1
    subi r1, r1, 1
    cmpi r1, 1
    jg fact_loop
fact_done:
  ret

