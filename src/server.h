#ifndef SERVER_H
#define SERVER_H

#include <stdio.h>
#include <pthread.h>
#include <libpq-fe.h>
#include <time.h>

#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#include <windows.h>
#include <mstcpip.h>
#pragma comment(lib, "ws2_32.lib")

void init_winsock();
void cleanup_winsock();

#define CLOSE_SOCKET(s) closesocket(s)
#define SOCKET_ERROR_CODE SOCKET_ERROR
#define INVALID_SOCKET_CODE INVALID_SOCKET
#define POLL WSAPoll
#define CLOCK(a,b) QueryPerformanceFrequency(b);\
                   QueryPerformanceCounter(a)
typedef int socklen_t;

#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <poll.h>

void init_winsock();
void cleanup_winsock();

#define CLOSE_SOCKET(s) close(s)
#define SOCKET_ERROR_CODE -1
#define INVALID_SOCKET_CODE -1
#define POLL poll
#define CLOCK(a,b) clock_gettime(a)

#endif

#define ADDRLEN 4
#define PORT 8888
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 10000
#define QUEUE_SIZE 10000
#define IMG_CHUNK_SIZE 8192

extern int nfds; // number of active sockets
extern struct pollfd* fds; // array of active sockets
extern char* fd_block; // array of blocked sockets ( binary packets exceeding buffer size )
extern _Atomic int tasks,streamtasks,count_db,count_login,count_create;
extern _Atomic long long int time_hr,time_hr_res,time_lr;

void* worker_thread(void* arg); // worker thread function

#endif