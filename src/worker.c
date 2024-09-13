#include "server.h"
#include "tasks.h"
#include "auth.h"
#include "../lib/picohttpparser.h"
#include "../lib/tiny-json.h"
#include <stdlib.h>

// URI string literal consts
const char* URI[TASK_COUNT] = {"/auth/create", "/auth/login", "/api/sample", "/api/file"};

// worker thread function
void* worker_thread(void* arg) {
    // connect to database
    printf("connection to postgres... ");
    PGconn* conn = PQconnectdb("user=postgres dbname=postgres password=dev2813 hostaddr=127.0.0.1 port=5432");
    if (PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "Connection to database failed: %s\n", PQerrorMessage(conn));
        PQfinish(conn);
        return NULL;
    }
    printf("ok!\n");

    // define http headers
    const char *method;
    size_t method_len;
    const char *path;
    size_t path_len;
    int minor_version;
    struct phr_header headers[32];
    size_t num_headers;
    int i, ret;

    while (1) {
        // pop task from task queue, this is where i would implement work stealing
        Task task;
        dequeue_task(&task);

        // parse http headers with picohttpparser
        num_headers = sizeof(headers) / sizeof(headers[0]);
        ret = phr_parse_request(task.buffer, task.size, &method, &method_len, &path, &path_len, &minor_version, headers, &num_headers,0);

        // get pointer to body of http request
        char* body = task.buffer+ret;

        // get important headers
        int header_auth = -1, header_contenttype = -1, header_contentlen = -1;
        for (int i = 0; i != num_headers; ++i) {
            if(strncmp(headers[i].name,"Authorization: ",headers[i].name_len) == 0) header_auth = i;
            else if(strncmp(headers[i].name,"Content-Type: ",headers[i].name_len) == 0) header_contenttype = i;
            else if(strncmp(headers[i].name,"Content-Length: ",headers[i].name_len) == 0) header_contentlen = i;
        }

        if(strncmp(method,"GET",method_len) == 0){
            // parse GET request
            if(header_auth != -1){
                const char* authkey = headers[header_auth].value+strlen("Bearer ");
                printf("GET request with authkey [%i]: %s\n",strlen(authkey),authkey);
                session_auth(conn,authkey);
                http(task.socket,200,"OK",".");
                // do nothing just OK
            }else{
                http(task.socket,401,"Unauthorized","No authkey");
            }
        }else if(strncmp(method,"POST",method_len) == 0){
            // parse POST request

            char length[64];
            char* errptr;
            snprintf(length,64,"%.*s", (int)headers[header_contentlen].value_len, headers[header_contentlen].value);
            int totallength = strtol(length,&errptr,10); // get content-size header to find size of the entire request
            int bodylength = task.size - (body - task.buffer); // get the size of the body up to the end of the task buffer

            // initialize json memory pool
            json_t pool[64];
            const json_t *parent = NULL;

            // get task OKs by URI 
            unsigned char tasks[TASK_COUNT];
            tasks[TASK_AUTH_CREATE] = strncmp(path, URI[TASK_AUTH_CREATE], path_len);
            tasks[TASK_AUTH_LOGIN] = strncmp(path, URI[TASK_AUTH_LOGIN], path_len);
            tasks[TASK_DB_SUBMIT] = strncmp(path, URI[TASK_DB_SUBMIT], path_len);
            tasks[TASK_IMG_SUBMIT] = strncmp(path, URI[TASK_IMG_SUBMIT], path_len);

            // all above URIs require a body. I want to use some kind of flags struct to make this whole validation process a bit nicer
            if(header_contenttype == -1 || header_contentlen == -1){
                http(task.socket,400,"Bad Request","No body for parse");
                continue;
            }

            // all URIs except img_submit are JSON so it needs to be verified
            if(tasks[TASK_IMG_SUBMIT] != 0){
                // check header is JSON
                if(strncmp(headers[header_contenttype].value,"application/json",headers[header_contenttype].value_len) != 0){
                    http(task.socket,400,"Bad Request","Content-type should be application/json");
                    continue;
                }
                // check body actually parses as JSON
                parent = json_create(body,pool,64);
                if(parent == NULL){
                    http(task.socket,400,"Bad Request","Malformed JSON body");
                    for(int i = 0; i < bodylength; i++) printf("%c",body[i]);
                    continue;
                }
            }else{
                // images are of binary type, so this needs to be verified
                if(strncmp(headers[header_contenttype].value,"application/octet-stream",headers[header_contenttype].value_len) != 0){
                    http(task.socket,400,"Bad Request","Content-type should be octet-stream");
                    continue;
                }
            }

            int task_ret = 0;
            if ( tasks[TASK_AUTH_CREATE] == 0) task_ret = task_auth_create(&task,parent,conn); // create an account
            else if ( tasks[TASK_AUTH_LOGIN]  == 0) task_ret = task_auth_get(&task,parent,conn); // retrieve an api token from an account
            else if ( tasks[TASK_DB_SUBMIT] == 0 || tasks[TASK_IMG_SUBMIT] == 0 ) {
                //db submit and image submit require authorization

                // check auth header
                if(header_auth == -1){
                    http(task.socket,401,"Unauthorized","No API key in header");
                    continue;
                }

                // check auth key
                char authkey[65];
                strncpy(authkey,headers[header_auth].value+strlen("Bearer "),64);
                if(session_auth(conn,authkey) != 1){
                    http(task.socket,401,"Unauthorized","Invalid API key");
                    continue;
                }

                // process task
                if(tasks[TASK_DB_SUBMIT] == 0) task_ret = task_db_upload(&task,parent,conn); // upload sample
                else if(tasks[TASK_IMG_SUBMIT] == 0) task_ret = task_img_upload(&task,body,bodylength < totallength ? bodylength : totallength,totallength, conn); // upload image
                ///printf("Handling sample upload\n");
            } else {
                http(task.socket,404,"Not Found","URI not found");
                continue;
            }

            // tell the client the outcome of their task
            if(task_ret == 1) http(task.socket,200,"OK",".");
            else http(task.socket,400,"Bad Request","Malformed request, undefined error");
            
        }
        // reset socket block for this thread
        fd_block[task.index] = 0;
    }

    // close db connection
    PQfinish(conn);
    return NULL;
}