cc := clang
cflags := -Wall -Wextra -Werror -std=c23 -Wswitch-enum

all: emulator asm plugins/stdio.so plugins/simple_vdisk.so

emulator: emulator.c isa.h mmio_plugin.h
	$(cc) $(cflags) -o emulator emulator.c -ldl

debug-emulator: emulator.c isa.h mmio_plugin.h
	$(cc) $(cflags) -o debug-emulator emulator.c -DDEBUG -ldl

asm: assembler.c isa.h
	$(cc) $(cflags) -o asm assembler.c

plugins/stdio.so: plugins/stdio.c mmio_plugin.h
	$(cc) $(cflags) -fPIC -shared -o plugins/stdio.so plugins/stdio.c

plugins/simple_vdisk.so: plugins/simple_vdisk.c mmio_plugin.h
	$(cc) $(cflags) -fPIC -shared -o plugins/simple_vdisk.so plugins/simple_vdisk.c

.PHONY: clean
clean:
	rm -f emulator debug-emulator asm plugins/stdio.so plugins/simple_vdisk.so

.PHONY: dump-example
dump-example: asm
	./asm example.asm /tmp/example.bin && xxd -g1 /tmp/example.bin && rm -f /tmp/example.bin

.PHONY: run-example
run-example: emulator asm plugins/stdio.so plugins/simple_vdisk.so
	./asm example.asm /tmp/example.bin && ./emulator --device ./plugins/stdio.so --device ./plugins/simple_vdisk.so /tmp/example.bin && rm -f /tmp/example.bin
