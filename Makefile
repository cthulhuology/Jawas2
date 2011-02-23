all : jawasd

jawasd : jawasd.c
	gcc --std=c99  -o jawasd jawasd.c
