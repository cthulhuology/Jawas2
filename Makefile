all : jawasd

jawasd : jawasd.c
	gcc -ggdb --std=c99  -o jawasd jawasd.c

.PHONY: clean
clean:
	rm -rf jawasd jawasd.dSYM

.PHONY: debug
debug:
	gdb ./jawasd

.PHONY: run
run:
	./jawasd -p 8888
