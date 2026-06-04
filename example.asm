#include "runtime.asm"

main:
  ; device_count
  load r0, machine_info, 40

  ; device_list
  load r1, machine_info, 48

find_device:
  cmpi r0, 0
  je not_found

  ; device.type
  load r2, r1, 0

  cmpi r2, 1              ; DEVICE_STDIO
  je found_stdio

  addi r1, r1, 40         ; sizeof(device_info)
  subi r0, r0, 1
  jmp find_device

found_stdio:
  ; device.start
  load r3, r1, 8

  ; device.name
  lea r5, r1, 24

print_name:
  loadb r6, r5, 0

  cmpi r6, 0
  je print_newline

  storeb r6, r3, 0

  addi r5, r5, 1
  jmp print_name

print_newline:
  loadi r6, '\n'
  storeb r6, r3, 0

  loadi r6, 'H'
  storeb r6, r3, 0

  loadi r6, '\n'
  storeb r6, r3, 0

  ret

not_found:
  ret
