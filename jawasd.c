// jawasd.c
//
// © 2009,2011,2012 David J. Goehrig
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

#define IP(X)	(htonl(X)>>24&0xff),(htonl(X)>>16&0xff),(htonl(X)>>8&0xff),(htonl(X)&0xff)

int kq = 0; 			// the Kqueue
int done = 0;			// signal flag
int sfd = 0;			// server fd
int detach = 0;			// detach from foreground flag
int child = 0;			// child spawned
int status = 0;			// response status
int backlog = 1024;		// pending connection backlog
int address = INADDR_ANY;	// address to bind to
short port = 8888;		// port to listen on
int level = 1;			// log level, default to warnings
int peer = 0;			// peer's ethernet address
int peer_port = 0;		// peer's port
int timeout = 60;
int linger = 60;

void die(int status, char* message, ... ) {
	va_list args;
	va_start(args,message);
	vsyslog(LOG_ERR, message, args);
	exit(status);
}

void warn(char* message,...) {
	if (level > 1) return;
	va_list args;
	va_start(args,message);
	vsyslog(LOG_WARNING, message, args);
}

void debug(char* message,...) {
	if (level > 0) return;
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

void closeSocket(int sock) {
	debug("Closing connection from %d.%d.%d.%d:%d",IP(peer),peer_port);
	close(sock);
	exit(0);
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
	struct itimerval interval = {{0,0},{ seconds, 0}};
	return setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&interval,sizeof(struct itimerval))
                && setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&interval,sizeof(struct itimerval));
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
	peer = saddr.sin_addr.s_addr;
        peer_port = saddr.sin_port;
	debug("Accepting connection from %d.%d.%d.%d:%d",IP(peer),peer_port);
	nonblock(sock);
	timeoutSocket(sock,linger);
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
	if (reuseSocket(sfd)) die(2,"Failed to reuse socket %d",sfd);
	if (bindSocket(sfd, address, htons(port))) die(3,"Failed to bind to %d.%d.%d.%d:%d",IP(address),port);
	if (listen(sfd,backlog)) die(4,"Failed to listen on port %d",port);
	if (nonblock(sfd)<0) die(5,"Failed to set nonblocking on socket %d", sfd);
	debug("Listening on %d.%d.%d.%d:%d",IP(address),port);
}

int setup() {
	logging();
	handleSignals();
	monitor();
	kq = kqueue();
}

int readRequest(int sock) {
	int bytes = 0;
	char buffer[4096];
	if (done) exit(0);
	memset(&buffer,0,4096);
	bytes = recv(sock,&buffer,4096,0);
	if (bytes < 0 && errno != EAGAIN) closeSocket(sock);		// Socket closed or in an errorneous condition
	if (bytes > 0) timeout = linger;				// Reset the timeout on new requests
	return bytes < 0 ? 0 : bytes; 
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

void work(int sock) {
	if (readRequest(sock) > 0) writeResponse(sock);
	snooze(1,0);
	if (--timeout <= 0) done = 1;
	debug("Socket %d has %ds before disconnect",sock,timeout);
	if (done) closeSocket(sock);
}

void closeParent(int sock) {
	close(sock);			// NB: we're closing the parent's copy not the childs!
}

void spawn(int sock) {
	if (!sock) return;			
	if (fork()) closeParent(sock);
	else while (!done) work(sock);
}

void processIncoming() {
	struct timespec ts = { 1, 0 };
	struct kevent cl = { sfd, EVFILT_READ, EV_ADD|EV_ONESHOT, 0, 0, 0 };
	struct kevent el = { 0, 0, 0, 0, 0, 0 };
	if (kevent(kq,&cl,1,&el,1,&ts)>0) spawn(acceptSocket(sfd));
}

void run() {
	setup();
	while (!done) processIncoming();
}

void usage(char* command) {
	fprintf(stderr,
		"Usage: %s [-d] [-v] [-q] [-p port] [-a address] [-b backlog] [-l linger]\n"
		"\n"
		"	-h -?		this message\n"
		"	-d		run in the background\n"
		"	-v		verbose logging\n"
		"	-q		quite mode\n"
		"	-p port		port to listen on\n"
		"	-a address	address to listen on\n"
		"	-b backlog	socket backlog for incoming connections\n"
		"	-l linger	time in seconds to wait before dropping a connection\n"
		"\n",
		command);
	exit(0);
}

void processArgs(int argc, char** argv) {
	for (int i = 1; i < argc; ++i) {
		if (!strcmp(argv[i],"-d")) detach = 1;						// detach from console
		if (!strcmp(argv[i],"-p") && i < argc-1) port = (short)atoi(argv[i+1]);		// set port
		if (!strcmp(argv[i],"-a") && i < argc-1) address = inet_addr(argv[i+1]);	// sort eth address
		if (!strcmp(argv[i],"-b") && i < argc-1) backlog = atoi(argv[i+1]);		// set connection backlog
		if (!strcmp(argv[i],"-l") && i < argc-1) linger = atoi(argv[i+1]);		// time to let a socket linger between requests 
		if (!strcmp(argv[i],"-v") && i < argc) level = 0;				// verbose mode
		if (!strcmp(argv[i],"-q") && i < argc) level = 2;				// quite mode
		if (!strcmp(argv[i],"-h") || !strcmp(argv[i],"-?")) usage(argv[0]);		// show help
	}
}

int main(int argc, char** argv) {
	processArgs(argc,argv);
	if (detach) child = fork();
	if (!child) run();		// We are the child if we don't have a child so run
	return 0;
}
