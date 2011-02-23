// jawasd.c
//
// © 2009,2011 David J. Goehrig
// All Rights Reserved
//
// Version: 0.0.1
//

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/event.h>
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
short port = 80;		// port to listen on

int tcpSocket() {
	int fd = socket(AF_INET,SOCK_STREAM,0);
	if (0>fd) return 0;
}

int bindSocket(int fd, unsigned long addr, short port) {
	struct sockaddr_in saddr = {sizeof(struct sockaddr_in),AF_INET,port,{addr},{0,0,0,0,0,0}};
	return bind(fd,(struct sockaddr*)&saddr,sizeof(saddr));
}

int reuseSocket(int fd) {
	int one = 1;
	setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
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
	fprintf(stderr,"[%d] Jawasd done\n",getpid());
	done = 1;
}

void handleSignals() {
	signal(SIGHUP,signalHandler);	
	signal(SIGINT,signalHandler);	
	signal(SIGQUIT,signalHandler);	
	signal(SIGCHLD,SIG_IGN);
}

int monitor() {
	int fd = tcpSocket();
	if (!fd) return 0;
	if (reuseSocket(fd)) goto error;
	if (bindSocket(fd, address, htons(port))) goto error;
	if (listen(fd,backlog)) goto error;
	return fd;
error:
	close(fd);
	return 0;
}

int setup() {
	handleSignals();
	sfd = monitor();
	if (!sfd) {
		fprintf(stderr,"Failed to monitor port %d\n",port);
		exit(1);
	}
	nonblock(sfd);
	kq = kqueue();
}

void spawnClient(int sock) {
	if (! sock) return;
	if (!(child = fork())) {
		fprintf(stderr,"[%d] is child\n",getpid());
		char buffer[4096];
		memset(&buffer,0,4096);
		int bytes = read(sock,&buffer,4096);
		fprintf(stderr,"[%s]\n",buffer);
		close(sock);
		fprintf(stderr,"[%d] is closed\n",sock);
		exit(0);
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
