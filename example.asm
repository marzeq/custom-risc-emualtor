#include "runtime.asm"

#define NAME_BUF_SIZE 32

main:
  subi sp, sp, NAME_BUF_SIZE

  // If a disk is attached and large enough, load the previously stored name.
  call vdisk_size
  cmpi r0, NAME_BUF_SIZE
  jl .prompt

  loadi r1, 0
  call vdisk_seek
  cmpi r0, 0
  je .prompt

  call vdisk_read
  store r0, sp, 0
  call vdisk_read
  store r0, sp, 8
  call vdisk_read
  store r0, sp, 16
  call vdisk_read
  store r0, sp, 24

  load8 r0, sp, 0
  cmpi r0, 0
  je .prompt

  loadi r1, previous_msg
  call puts
  mov r1, sp
  call puts
  loadi r1, newline
  call puts

.prompt:
  loadi r1, msg
  call puts

  mov r1, sp
  loadi r2, NAME_BUF_SIZE
  call getline

  loadi r1, response_1
  call puts

  mov r1, sp
  call puts

  loadi r1, response_2
  call puts

  // Persist the complete fixed-size name buffer when a disk is available.
  call vdisk_size
  cmpi r0, NAME_BUF_SIZE
  jl .done

  loadi r1, 0
  call vdisk_seek
  cmpi r0, 0
  je .done

  load r1, sp, 0
  call vdisk_write
  load r1, sp, 8
  call vdisk_write
  load r1, sp, 16
  call vdisk_write
  load r1, sp, 24
  call vdisk_write

.done:
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

previous_msg:
  .ascii "Previous name: "
  .byte 0

newline:
  .ascii "\n"
  .byte 0
