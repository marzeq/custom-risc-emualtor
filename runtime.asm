  jmp _setup

_setup:
  ; r1 = ram_start
  load r1, machine_info, 8

  ; r2 = ram_size
  load r2, machine_info, 16

  ; heap starts at beginning of RAM
  mov r14, r1

  ; r3 = ram_end
  add r3, r1, r2

  ; r4 = stack_size = ram_size / 4
  divi r4, r2, 4

  ; sp = ram_end - stack_size
  sub sp, r3, r4

  call main
  halt

;; LIBRARY FUNCTIONS
