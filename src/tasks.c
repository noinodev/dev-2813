#include <stdio.h>
#include "server.h"
#include "tasks.h"
#include "auth.h"
#include "../lib/tiny-json.h"
#include <openssl/md5.h>
//#include "../lib/picohttpparser.h"

const char* sqlexec[] = {
    "SELECT apikey FROM usr_auth WHERE uname = $1 AND passhash = $2;",
    "SELECT apikey FROM usr_auth WHERE apikey = $1;",
    "INSERT INTO usr_auth (uname,passhash,email,apikey)VALUES ($1,$2,$3,$4);",
    "SELECT image_md5 FROM geo_metadata WHERE image_md5 = $1;"
    //"INSERT INTO local ($2)VALUES $3;",
};

TaskQueue task_queue = {
    .tasks = {{0}},
    .head = 0, .tail = 0, .count = 0,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER
};

// Add task to the queue
void enqueue_task(int size, int client_fd, char* buffer, int index) {
    pthread_mutex_lock(&task_queue.mutex);
    while (task_queue.count == QUEUE_SIZE) {
        pthread_cond_wait(&task_queue.cond, &task_queue.mutex);
    }
    task_queue.tasks[task_queue.tail].size = size;
    task_queue.tasks[task_queue.tail].socket = client_fd;
    task_queue.tasks[task_queue.tail].index = index;
    memcpy(task_queue.tasks[task_queue.tail].buffer, buffer, sizeof(task_queue.tasks[task_queue.tail].buffer) - 1);
    task_queue.tail = (task_queue.tail + 1) % QUEUE_SIZE;
    task_queue.count++;
    pthread_cond_signal(&task_queue.cond);
    pthread_mutex_unlock(&task_queue.mutex);
}

// Get task from the queue
void dequeue_task(Task* task_to) {
    Task task_from;
    pthread_mutex_lock(&task_queue.mutex);
    while (task_queue.count == 0) {
        pthread_cond_wait(&task_queue.cond, &task_queue.mutex);
    }
    task_from = task_queue.tasks[task_queue.head];
    task_queue.head = (task_queue.head + 1) % QUEUE_SIZE;
    task_queue.count--;

    memcpy(task_to, &task_from, sizeof(Task));
    pthread_cond_signal(&task_queue.cond);
    pthread_mutex_unlock(&task_queue.mutex);
    //return task;
}

void http(int socket, int code, const char *status, const char *message) {
    char response[512];
    snprintf(response, sizeof(response),
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: text/html\r\n"
             "Content-Length: %zu\r\n"
             "\r\n"
             "<html><body><h1>%d %s</h1><p>%s</p></body></html>\r\n",
             code, status, strlen(message) + strlen(status)+28, // Calculate content length
             code, status, message);

    send(socket, response, strlen(response), 0);
}

void httpjson(int socket, const char *json) {
    char response[512];
    snprintf(response, sizeof(response),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "\r\n"
             "%s",
             strlen(json),json);

    send(socket, response, strlen(response), 0);
}

int task_auth_create(Task* task, const json_t* json, PGconn* conn){
    printf("creating account\n");
    int ret = 0;

    const json_t *usernf, *passnf, *emailnf;
    const char *user, *pass, *email;

    usernf = json_getProperty(json, "username");
    passnf = json_getProperty(json, "password");
    emailnf = json_getProperty(json, "email");

    user = usernf ? json_getValue(usernf) : NULL;
    pass = passnf ? json_getValue(passnf) : NULL;
    email = emailnf ? json_getValue(emailnf) : NULL;

    if(user == NULL || pass == NULL || email == NULL) return 0;

    srand(time(NULL));
    char authkey[65];
    for(int j = 0; j < 64; j++) authkey[j] = 'A'+(rand()%26);
    authkey[64] = '\0';

    const char *sqlparams[4] = { user, pass, email, authkey};
    PGresult *pgres = PQexecParams(conn,sqlexec[SQL_ADDUSER],4,NULL,sqlparams,NULL,NULL,0);
    if (PQresultStatus(pgres) != PGRES_COMMAND_OK) {
        fprintf(stderr, "query no: %s\n", PQerrorMessage(conn));
        http(task->socket,409,"Conflict","Username or email already exists");
        PQclear(pgres);
    }else{
        session_add_key(authkey);
        char json[128];
        snprintf(json,128,"{\"api_key\": \"%s\"}",authkey);
        httpjson(task->socket,json);
        ret = 1;
    }
    PQclear(pgres);
    return ret;
}

int task_auth_get(Task* task, const json_t* json, PGconn* conn){
    //printf("retrieving auth key\n");
    int ret = 0;

    const json_t *usernf, *passnf;
    const char *user, *pass;
    usernf = json_getProperty(json, "username");
    passnf = json_getProperty(json, "password");
    user = usernf ? json_getValue(usernf) : NULL;
    pass = passnf ? json_getValue(passnf) : NULL;

    if(user == NULL || pass == NULL) return 0;
    if (conn == NULL || PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "Connection is not valid\n");
        return 0;
    }

    const char *sqlparams[2] = { user, pass };
    PGresult *pgres = PQexecParams(conn,sqlexec[SQL_GETAUTHKEY],2,NULL,sqlparams,NULL,NULL,0);

    if (PQresultStatus(pgres) == PGRES_TUPLES_OK) {
        if (PQntuples(pgres) == 1) {
            char *authkey = PQgetvalue(pgres, 0, 0);
            session_add_key(authkey);
            char json[128];
            snprintf(json,128,"{\"api_key\": \"%s\"}",authkey);
            httpjson(task->socket,json);
            ret = 1;
        } else {
            printf("User not found or incorrect password.\n");
            //http
        }
    } else {
        fprintf(stderr, "Query failed: %s\n", PQerrorMessage(conn));
    }


    // Clean up
    PQclear(pgres);
    return ret;
}

int task_img_upload(Task* task, const char *buffer, const int buffer_size, const int total_size, PGconn* conn){
    //printf("IMAGE UP!\n");
    char filename[32] = "res/";
    srand(time(NULL));
    for(int j = 4; j < 16; j++) filename[j] = 'A'+(rand()%26);
    filename[16] = '\0';
    strncat(filename,".png",sizeof(filename) - strlen(filename) - 1);
    //printf("fname: %s",filename);

    //fd_block[task->index] = 1;

    FILE *file = fopen(filename, "wb");
    if (file == NULL) {
        perror("Error opening file for writing");
        return 0;
    }
    // write first part of image
    size_t written = 0;
    written += fwrite(buffer, 1, buffer_size, file);

    for (int i = 0; i < buffer_size; i++) {
        //printf("%02X ", buffer[i] & 0xFF);
    }

    // continue to get rest of image
    char nbuff[IMG_CHUNK_SIZE];
    memset(nbuff, 0, sizeof(nbuff));
    ssize_t bytes_read;
    while ((bytes_read = recv(task->socket, nbuff, sizeof(nbuff), 0)) > 0) {
        // Write the received chunk to the file
        if(written < total_size) written += fwrite(nbuff, 1, (written+bytes_read) > total_size ? total_size-written : bytes_read, file);
    }

    printf("written/total: %i/%i\n",written,total_size);
    fclose(file);


    // CHECK MD5 SUM AGAINST DB

    unsigned char c[MD5_DIGEST_LENGTH];
    int i;
    FILE *inFile = fopen (filename, "rb");
    MD5_CTX mdContext;
    int bytes;
    unsigned char data[1024];

    if (inFile == NULL) {
        printf ("%s can't be opened.\n", filename);
        return 0;
    }

    MD5_Init (&mdContext);
    while ((bytes = fread (data, 1, 1024, inFile)) != 0)
        MD5_Update (&mdContext, data, bytes);
    MD5_Final (c,&mdContext);
    //for(i = 0; i < MD5_DIGEST_LENGTH; i++) snprintf(hash[i], sizeof(hash),"%02x", c[i]);//snprintf(hash,sizeof(hash),"%02x", c[i]);
    //printf ("\n");
    fclose (inFile);

    //printf("the fuck??????\n");
    char buf[MD5_DIGEST_LENGTH * 2 + 1];
    //compute_md5("hello world", digest);
    for (int i = 0, j = 0; i < 16; i++, j+=2)
        sprintf(buf+j, "%02x", c[i]);
    buf[MD5_DIGEST_LENGTH * 2] = 0;
    printf ("md5: %s\n", buf);

    const char *sqlparams[1] = { buf };
    PGresult *pgres = PQexecParams(conn,sqlexec[SQL_CHECKMD5],1,NULL,sqlparams,NULL,NULL,0);
    if (PQresultStatus(pgres) == PGRES_TUPLES_OK) {
        if (PQntuples(pgres) == 0) {
            printf("image definitely not in table\n");
            //remove(filename);
        }else{
            printf("image is in table\n");

            char outfile[64] = {0};
            snprintf(outfile,64,"res/%s.png",buf);

            FILE *source, *target;
            int i;
            source = fopen(filename, "rb"); 

            //if( source == NULL ) { printf("Press any key to exit...\n");}

            fseek(source, 0, SEEK_END);
            int length = ftell(source);

            fseek(source, 0, SEEK_SET);
            target = fopen(outfile, "wb"); 

            if( target == NULL ) { fclose(source); }

            for(i = 0; i < length; i++){
                fputc(fgetc(source), target);
            }

            printf("File copied successfully.\n"); 
            fclose(source); 
            fclose(target);
            remove(filename);
        }
    }else{
        printf("bad request\n");
    }
    PQclear(pgres);

    return 1;
}

void sql_get_json_array(char* buffer, const json_t* array){
    if (array && JSON_ARRAY == json_getType(array)) {
        json_t const* index;
        for( index = json_getChild( array ); index != 0; index = json_getSibling( index ) ) {
            if ( JSON_TEXT == json_getType( index ) ) {
                char const* value = json_getValue( index );
                char part[32];

                if ( value ){
                    //snprintf(buffer+strlen(buffer),sizeof(buffer)-strlen(buffer),"('%s'),",value);
                    snprintf(part,sizeof(part),"\"%s\",",value);
                    if(strlen(buffer) < sizeof(buffer)-32) strncat(buffer,part,sizeof(part));
                }
            }
        }
        buffer[strlen(buffer)-1] = '\0';
        //printf("argsJSON: %s\n",buffer);
    } else {
        printf("Array 'data' not found or not an array.\n");
    }
}

int task_db_upload(Task* task, const json_t* json, PGconn* conn){
    //printf("uploading to db\n");
    //printf("%s\n",task->buffer);

    const json_t *md5nf, *timenf, *latnf, *lonnf, *altnf, *geonf, *arrfloranf, *arrfaunanf, *arrcallsnf;
    const char *md5, *time, *lat, *lon, *alt;
    int geo;
    md5nf = json_getProperty(json, "img_md5");
    timenf = json_getProperty(json, "timestamp");
    latnf = json_getProperty(json, "latitude");
    lonnf = json_getProperty(json, "longitude");
    altnf = json_getProperty(json, "altitude");
    geonf = json_getProperty(json, "metadata");
    arrfloranf = json_getProperty(json, "flora");
    arrfaunanf = json_getProperty(json, "fauna");
    arrcallsnf = json_getProperty(json, "calls");
    md5 = md5nf ? json_getValue(md5nf) : NULL;
    time = timenf ? json_getValue(timenf) : NULL;
    lat = latnf ? json_getValue(latnf) : NULL;
    lon = lonnf ? json_getValue(lonnf) : NULL;
    alt = altnf ? json_getValue(altnf) : NULL;
    geo = geonf ? json_getInteger(geonf) : 0;

    char arg[3][1024] = {{0}};
    sql_get_json_array(arg[0],arrfloranf);
    sql_get_json_array(arg[1],arrfaunanf);
    sql_get_json_array(arg[2],arrcallsnf);
    printf("%s\n",arg[0]);
    printf("%s\n",arg[1]);
    printf("%s\n",arg[2]);



    char sql[4096];
    char* endptr;
    snprintf(sql,4096,"INSERT INTO geo_metadata (image_md5,timestamp,latitude,longitude,altitude,sample_geo,sample_flora,sample_fauna,sample_calls)VALUES('%s','%s',%lf,%lf,%lf,%i,'[%s]','[%s]','[%s]');",
            md5,time,strtod(lat,&endptr),strtod(lon,&endptr),strtod(alt,&endptr),geo,arg[0],arg[1],arg[2]);
    PGresult *pgres = PQexec(conn,sql);
    //char* idstr = PQgetvalue(pgres, 0, 0);
    //int id = strtol(idstr,&endptr,10);
    //printf("FIRST Q: %s\n",sql);

    if (PQresultStatus(pgres) != PGRES_COMMAND_OK) printf("Query failed: %s\n", PQerrorMessage(conn));
    PQclear(pgres);
   return 1;
}
