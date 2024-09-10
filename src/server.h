#ifndef SERVER_H
#define SERVER_H

#include <stdio.h>
#include <pthread.h>
#include <libpq-fe.h>

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

#endif

#define PORT 8888
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 500
#define NUM_WORKERS 4
#define QUEUE_SIZE 100

typedef enum {
    TASK_TYPE_SQL,
    TASK_TYPE_RECV_IMAGE,
    TASK_TYPE_SEND_IMAGE
} TaskType;

typedef struct {
    TaskType type;
    int client_fd;
    char query[BUFFER_SIZE];
} Task;

// Thread-safe task queue
typedef struct {
    Task tasks[QUEUE_SIZE];
    int head;
    int tail;
    int count;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} TaskQueue;

extern TaskQueue task_queue;
extern int nfds;
extern struct pollfd fds[];

void enqueue_task(int client_fd, const char* query);
Task dequeue_task();
void* worker_thread(void* arg);

#endif