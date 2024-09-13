//tasks !

#ifndef TASKS_H
#define TASKS_H
#include <stdio.h>
#include "server.h"
//#include <libpq-fe.h>
#include "../lib/tiny-json.h"

typedef enum {
    TASK_AUTH_CREATE,
    TASK_AUTH_LOGIN,
    TASK_DB_SUBMIT,
    TASK_IMG_SUBMIT,
    TASK_COUNT
} TaskType;

struct Task;
typedef struct Task{
    int socket,size,index;
    char buffer[BUFFER_SIZE];
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

enum SQLExecStatements{
    SQL_GETAUTHKEY,
    SQL_CHECKAUTHKEY,
    SQL_ADDUSER,
    SQL_CHECKMD5
};

extern const char* sqlexec[];

void enqueue_task(int size, int socket, char* buffer, int index);
void dequeue_task();

void http(int socket, int code, const char *status, const char *message);
void httpjson(int socket, const char *json);

int task_parsehttp(Task*);
int task_dbsubmit(Task*);
int task_dbfetch(Task*);
int task_sendimg(Task*);
int handle_recvimg(Task*);

int task_auth_create(Task* task,const json_t* json,PGconn* conn);
int task_auth_get(Task* task,const json_t* json,PGconn* conn);
int task_db_upload(Task* task,const json_t* json,PGconn* conn);
int task_img_upload(Task* task, const char *buffer, const int buffer_size, const int total_size, PGconn* conn);


#endif