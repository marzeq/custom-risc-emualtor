  jmp _setup

_setup:
  mov r14, ram_start

  mov r1, ram_end
  mov r2, ram_start

  sub r3, r1, r2      ; total RAM
  divi r3, r3, 4      ; stack_size = total / 4

  sub sp, r1, r3      ; stack begins here

  call main
  halt

;; LIBRARY FUNCTIONS

putch:
  loadi r2, io
  store r1, r2, 0
  ret

getch:
  loadi r2, io
  load r0, r2, 0
  ret

putn:
  push r6
  push r7

  cmpi r1, 0
  jne putn_nonzero

  loadi r1, '0'
  call putch

  pop r7
  pop r6
  ret

putn_nonzero:
  loadi r6, 0      ; digit count

putn_extract:
  modi r4, r1, 10
  push r4

  addi r6, r6, 1

  divi r1, r1, 10

  cmpi r1, 0
  jne putn_extract

putn_print:
  cmpi r6, 0
  je putn_done

  pop r4

  addi r1, r4, '0'
  call putch

  subi r6, r6, 1

  jmp putn_print

putn_done:
  pop r7
  pop r6
  ret

getn:
  push r1
  push r2
  push r3
  push r4

  loadi r0, 0

getn_loop:
  loadi r2, io
  load r3, r2, 0

  cmpi r3, '\n'
  je getn_done

  cmpi r3, '0'
  jl getn_invalid

  cmpi r3, '9'
  jg getn_invalid

  mov r1, r3
  call putch

  subi r3, r3, '0'

  muli r0, r0, 10
  add r0, r0, r3

  jmp getn_loop

getn_invalid:
  loadi r0, 0

getn_done:
  loadi r1, '\n'
  call putch

  pop r4
  pop r3
  pop r2
  pop r1
  ret
