#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "..\src\server.h"
#include "..\src\tasks.h"

int test_task(){
    char* buffer = "testbuffer";
    int a=-1,b=128,c=1;
    enqueue_task(a,b,buffer,c);
    Task task;
    dequeue_task(&task);
    assert(task.socket == a);
    assert(task.size == b);
    assert(task.index == c);
    assert(strncmp(task.buffer,buffer,strlen(buffer)) == 0);
    return 1;
}

int main(int argc, char** argv){
    test_task();
}