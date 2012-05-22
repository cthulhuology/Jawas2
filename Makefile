

MODULES = $(patsubst src/%.c, modules/%.dylib, $(wildcard src/*.c))


all : jawasd $(MODULES)

jawasd : jawasd.c
	gcc -gstabs -ggdb --std=c99 -o jawasd jawasd.c

.PHONY: clean
clean:
	rm -rf jawasd jawasd.dSYM modules/*

.PHONY: debug
debug:
	gdb ./jawasd

.PHONY: run
run:
	./jawasd -p 8888

modules/%.dylib : src/%.c
	gcc -undefined dynamic_lookup -dynamiclib -gstabs -ggdb --std=c99 -o $@ $<
