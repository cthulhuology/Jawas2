// html.c
//
// © 2012 David J. Goehrig
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern void register_handler(char*,void*);

void init(void* self) {
	register_handler("/html/",self);
}

void get(int sock) {
	char* response = "HTTP/1.1 200 OK\r\n"
	"Content-Type: text/html\r\n"
	"Content-Length: 11\r\n"
	"Connection: keep-alive\r\n"
	"\r\n"
	"Hello World";
	write(sock,response,strlen(response));
}

void post(int sock) {

}

void put(int sock) {

}

void delete(int sock) {

}
