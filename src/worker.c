#include "server.h"
#include "tasks.h"
#include "auth.h"
//#include <assert.h>
#include "../lib/picohttpparser.h"
#include "../lib/tiny-json.h"
#include <stdlib.h>
//#include "../lib/cJSON.h"

const char* URI[TASK_COUNT] = {"/auth/create", "/auth/login", "/api/sample", "/api/file"};


void* worker_thread(void* arg) {
    printf("connection to postgres... ");
    PGconn* conn = PQconnectdb("user=postgres dbname=postgres password=dev2813 hostaddr=127.0.0.1 port=5432");
    if (PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "Connection to database failed: %s\n", PQerrorMessage(conn));
        PQfinish(conn);
        return NULL;
    }
    printf("ok!\n");

    /*char buf[4096], *method, *path;
    int pret, minor_version;
    struct phr_header headers[100];
    size_t buflen = 0, prevbuflen = 0, method_len, path_len, num_headers;
    ssize_t rret;*/

    const char *method;
    size_t method_len;
    const char *path;
    size_t path_len;
    int minor_version;
    struct phr_header headers[32];
    size_t num_headers;
    int i, ret;

    while (1) {

        Task task;
        dequeue_task(&task);
        //printf("new task: %i bytes\n%s",task.size,task.buffer);

        num_headers = sizeof(headers) / sizeof(headers[0]);
        ret = phr_parse_request(task.buffer, task.size, &method, &method_len, &path, &path_len, &minor_version, headers, &num_headers,0);


        char* body = task.buffer+ret;
        int header_auth = -1, header_contenttype = -1, header_contentlen = -1;

        for (int i = 0; i != num_headers; ++i) {
            if(strncmp(headers[i].name,"Authorization: ",headers[i].name_len) == 0) header_auth = i;
            else if(strncmp(headers[i].name,"Content-Type: ",headers[i].name_len) == 0) header_contenttype = i;
            else if(strncmp(headers[i].name,"Content-Length: ",headers[i].name_len) == 0) header_contentlen = i;
        }

        if(strncmp(method,"GET",method_len) == 0){
            printf("why the fuck is this get?\n");
            if(header_auth != -1){
                const char* authkey = headers[header_auth].value+strlen("Bearer ");
                printf("GET request with authkey [%i]: %s\n",strlen(authkey),authkey);
                session_auth(conn,authkey);
                printf("Packet has been validated!\n");
                http(task.socket,200,"OK",".");
            }else{
                http(task.socket,401,"Unauthorized","No authkey");
            }
        }else if(strncmp(method,"POST",method_len) == 0){
            //printf("POST... ");

            char length[64];
            char* errptr;
            snprintf(length,64,"%.*s", (int)headers[header_contentlen].value_len, headers[header_contentlen].value);
            //printf("length: %s\n",length);
            int totallength = strtol(length,&errptr,10);
            //printf("int length: %i\n",totallength);
            int bodylength = task.size - (body - task.buffer);

            //printf("URI: %.*s\n",(int)path_len,path);

            json_t pool[64];
            const json_t *parent = NULL;

            unsigned char tasks[TASK_COUNT];
            tasks[TASK_AUTH_CREATE] = strncmp(path, URI[TASK_AUTH_CREATE], path_len);
            tasks[TASK_AUTH_LOGIN] = strncmp(path, URI[TASK_AUTH_LOGIN], path_len);
            tasks[TASK_DB_SUBMIT] = strncmp(path, URI[TASK_DB_SUBMIT], path_len);
            tasks[TASK_IMG_SUBMIT] = strncmp(path, URI[TASK_IMG_SUBMIT], path_len);
            //printf("ac[%i], lg[%i], db[%i], ig[%i]\n",tasks[TASK_AUTH_CREATE] ,tasks[TASK_AUTH_LOGIN] ,tasks[TASK_DB_SUBMIT] ,tasks[TASK_IMG_SUBMIT] );

            if(tasks[TASK_IMG_SUBMIT] != 0){
                if(header_contenttype == -1 || header_contentlen == -1){
                    http(task.socket,400,"Bad Request","No body for parse");
                    continue;
                }//else printf("header content ok... ");
                if(strncmp(headers[header_contenttype].value,"application/json",headers[header_contenttype].value_len) != 0){
                    http(task.socket,400,"Bad Request","Content-type should be application/json");
                    continue;
                }//else printf("content type ok... ");
                parent = json_create(body,pool,64);
                if(parent == NULL){
                    http(task.socket,400,"Bad Request","Malformed JSON body");
                    for(int i = 0; i < bodylength; i++) printf("%c",body[i]);
                    continue;
                }
            }else{
                if(strncmp(headers[header_contenttype].value,"application/octet-stream",headers[header_contenttype].value_len) != 0){
                    http(task.socket,400,"Bad Request","Content-type should be octet-stream");
                    continue;
                }
            }

            //int meta = strncmp(path, "/samples/upload", path_len), img = strncmp(path, "/samples/image", path_len);

            int task_ret = 0;
            if ( tasks[TASK_AUTH_CREATE] == 0) task_ret = task_auth_create(&task,parent,conn);
            else if ( tasks[TASK_AUTH_LOGIN]  == 0) task_ret = task_auth_get(&task,parent,conn);
            else if ( tasks[TASK_DB_SUBMIT] == 0 || tasks[TASK_IMG_SUBMIT] == 0 ) {
                if(header_auth == -1){
                    http(task.socket,401,"Unauthorized","No API key in header");
                    continue;
                }//else printf("header auth ok... ");
                char authkey[65];
                strncpy(authkey,headers[header_auth].value+strlen("Bearer "),64);
                if(session_auth(conn,authkey) != 1){
                    http(task.socket,401,"Unauthorized","Invalid API key");
                    continue;
                }// printf("AUTH ok... ");
                // Handle sample upload
                if(tasks[TASK_DB_SUBMIT] == 0) task_ret = task_db_upload(&task,parent,conn);
                else if(tasks[TASK_IMG_SUBMIT] == 0) task_ret = task_img_upload(&task,body,bodylength < totallength ? bodylength : totallength,totallength, conn);
                ///printf("Handling sample upload\n");
            } else {
                http(task.socket,404,"Not Found","URI not found");
                continue;
            }

            if(task_ret == 1){
                //printf("it worked\n");
                http(task.socket,200,"OK",".");
            }else{
                //printf("didnt work.\n");
                http(task.socket,400,"Bad Request","Malformed request, undefined error");
            }
        }
        fd_block[task.index] = 0;
    }

    PQfinish(conn);
    return NULL;
}