// jawasd.c
//
// © 2009,2011 David J. Goehrig
// All Rights Reserved
//
// Version: 0.0.2
//

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>
#include <sys/time.h>
#include <sys/event.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <arpa/inet.h>

int kq = 0; 			// the Kqueue
int done = 0;			// signal flag
int sfd = 0;			// server fd
int detach = 0;			// detach from foreground flag
int child = 0;			// child spawned
int status = 0;			// response status
int backlog = 1024;		// pending connection backlog
int address = INADDR_ANY;	// address to bind to
short port = 8888;		// port to listen on
int level = 0;			// log level

void die(int status, char* message, ... ) {
	va_list args;
	va_start(args,message);
	vsyslog(LOG_ERR, message, args);
	exit(status);
}

void warn(char* message,...) {
	va_list args;
	va_start(args,message);
	vsyslog(LOG_WARNING, message, args);
}

void debug(char* message,...) {
	va_list args;
	va_start(args,message);
	vsyslog(LOG_DEBUG, message, args);
}

void snooze(size_t s, size_t ns) {
	struct timespec ts = { s, ns };
	nanosleep(&ts,NULL);
}

int tcpSocket() {
	int fd = socket(AF_INET,SOCK_STREAM,0);
	return  0 > fd ? 0 : fd;
}

int bindSocket(int fd, unsigned long addr, short port) {
	struct sockaddr_in saddr = {sizeof(struct sockaddr_in),AF_INET,port,{addr},{0,0,0,0,0,0}};
	return bind(fd,(struct sockaddr*)&saddr,sizeof(saddr));
}

int reuseSocket(int fd) {
	int one = 1;
	return setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
}

int timeoutSocket(int fd, int seconds) {
	struct itimerval timeout = {{0,0},{ seconds, 0}};
	return setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(struct itimerval))
                && setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(struct itimerval));
}

int nonblock(int fd) {
        int flags = fcntl(fd,F_GETFL,0);
        return fcntl(fd,F_SETFL,flags | O_NONBLOCK);
}

int acceptSocket(int fd) {
        struct sockaddr_in saddr;
        socklen_t len = sizeof(struct sockaddr_in);
        int sock = accept(fd,(struct sockaddr*)&saddr,&len);
	if (sock < 0) return 0;
	nonblock(sock);
	return sock;
}

void signalHandler() {
	if (!done) killpg(0,SIGHUP);
	done = 1;
}

void logging() {
	openlog("jawasd", LOG_NDELAY|LOG_PID, LOG_LOCAL7);
}

void handleSignals() {
	signal(SIGHUP,signalHandler);	
	signal(SIGINT,signalHandler);	
	signal(SIGQUIT,signalHandler);	
	signal(SIGCHLD,SIG_IGN);
}

void monitor() {
	sfd = tcpSocket();
	if (!sfd) die(1,"Could not allocate socket");
	if (reuseSocket(sfd)) die(2,"Failed to reuse socket");
	if (bindSocket(sfd, address, htons(port))) die(3,"Failed to bind to address and port");
	if (listen(sfd,backlog)) die(4,"Failed to listen on port");
	if (nonblock(sfd)<0) die(5,"Failed to set nonblocking on port");
}

int setup() {
	logging();
	handleSignals();
	monitor();
	kq = kqueue();
}

int readRequest(int sock) {
	int bytes = 0;
	do {
		snooze(1,0);
		char buffer[4096];
		memset(&buffer,0,4096);
		bytes = recv(sock,&buffer,4096,0);
		syslog(LOG_DEBUG,"[%s] %d (%d) %d\n",buffer,bytes,errno,done);
	} while (!done && bytes < 0 && errno == EAGAIN);
	return bytes; 
}

void writeResponse(int sock) {
	char* response = "HTTP/1.1 200 OK\r\n"
	"Content-Type: text/html\r\n"
	"Content-Length: 11\r\n"
	"Connection: keep-alive\r\n"
	"\r\n"
	"Hello World";
	write(sock,response,strlen(response));
}

void spawnClient(int sock) {
	if (! sock) return;
	if (!(child = fork())) {
		while(!done) {
			if (readRequest(sock) < 0) {
				fprintf(stderr,"[%d] %d is closed\n",getpid(),sock);
				close(sock);
				exit(0);
			}
			writeResponse(sock);
		}
	}
	close(sock); // Close parent's copy
	fprintf(stderr,"[%d] spawned child %d\n",getpid(),child);
}

void processIncoming() {
	struct timespec ts = { 1, 0 };
	struct kevent cl = { sfd, EVFILT_READ, EV_ADD|EV_ONESHOT, 0, 0, 0 };
	struct kevent el = { 0, 0, 0, 0, 0, 0 };
	if (kevent(kq,&cl,1,&el,1,&ts)>0) spawnClient(acceptSocket(sfd));
}

void run() {
	while (!done) processIncoming();
}

void processArgs(int argc, char** argv) {
	for (int i = 1; i < argc; ++i) {
		if (!strcmp(argv[i],"-d")) detach = 1;
		if (!strcmp(argv[i],"-p") && i < argc-1) port = (short)atoi(argv[i+1]);
		if (!strcmp(argv[i],"-a") && i < argc-1) address = inet_addr(argv[i+1]);
		if (!strcmp(argv[i],"-b") && i < argc-1) backlog = atoi(argv[i+1]);
		if (!strcmp(argv[i],"-h") || !strcmp(argv[i],"-?")) {
			fprintf(stderr,"Usage: %s [-d] [-p port] [-a address] [-b backlog]\n",argv[0]);
			exit(0);
		}
	}
}

int main(int argc, char** argv) {
	processArgs(argc,argv);
	if (detach) child = fork();
	if(!child) {
		setup();
		run();
	}
	return 0;
}
