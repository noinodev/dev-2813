//tasks !

#ifndef TASKS_H
#define TASKS_H
#include <stdio.h>
#include "server.h"
#include "../lib/tiny-json.h"

// Task URI indices
typedef enum {
    TASK_AUTH_CREATE,
    TASK_AUTH_LOGIN,
    TASK_DB_SUBMIT,
    TASK_IMG_SUBMIT,
    TASK_COUNT
} TaskType;

//temp unused
typedef struct {

} HTTPConstrict;

// Task struct for queue
struct Task;
typedef struct Task{
    int socket,size,index;
    char buffer[BUFFER_SIZE];
} Task;

// Circular queue
typedef struct {
    Task tasks[QUEUE_SIZE];
    int head;
    int tail;
    int count;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} TaskQueue;

extern TaskQueue task_queue;

// SQL statement preset indices
enum SQLExecStatements{
    SQL_GETAUTHKEY,
    SQL_CHECKAUTHKEY,
    SQL_ADDUSER,
    SQL_CHECKMD5
};

extern const char* sqlexec[];

// task scheduling
void enqueue_task(int size, int socket, char* buffer, int index);
void dequeue_task();

// http response helper functions
void http(int socket, int code, const char *status, const char *message);
void httpjson(int socket, const char *json);

// task functions
int task_auth_create(Task* task,const json_t* json,PGconn* conn);
int task_auth_get(Task* task,const json_t* json,PGconn* conn);
int task_db_upload(Task* task,const json_t* json,PGconn* conn);
int task_img_upload(Task* task, const char *buffer, const int buffer_size, const int total_size, PGconn* conn);


#endif