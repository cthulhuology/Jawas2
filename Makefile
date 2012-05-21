all : jawasd

jawasd : jawasd.c
	gcc -ggdb --std=c99  -o jawasd jawasd.c

.PHONY: clean
clean:
	rm jawasd

.PHONY: debug
debug:
	gdb ./jawasd

