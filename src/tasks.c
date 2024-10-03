#include <stdio.h>
#include "server.h"
#include "tasks.h"
#include "auth.h"
#include "../lib/tiny-json.h"
#include <openssl/md5.h>

// SQL preset queries
const char* sqlexec[] = {
    "SELECT apikey FROM usr_auth WHERE uname = $1 AND passhash = $2;",
    "SELECT apikey FROM usr_auth WHERE apikey = $1;",
    "INSERT INTO usr_auth (uname,passhash,email,apikey)VALUES ($1,$2,$3,$4);",
    "SELECT image_md5 FROM geo_metadata WHERE image_md5 = $1;"
};

// Circular buffer definition, global scope
TaskQueue task_queue = {
    .tasks = {{0}},
    .head = 0, .tail = 0, .count = 0,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER
};

// Push task to circular queue with mutual exclusion
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

// pop task from circular queue with mutual exclusion
void dequeue_task(Task* task_to) {
    Task task_from;
    pthread_mutex_lock(&task_queue.mutex);
    while (task_queue.count == 0) {
        pthread_cond_wait(&task_queue.cond, &task_queue.mutex);
    }
    task_from = task_queue.tasks[task_queue.head];
    task_queue.head = (task_queue.head + 1) % QUEUE_SIZE;
    task_queue.count--;

    memcpy(task_to, &task_from, sizeof(Task)); // copy task buffer (http request)
    pthread_cond_signal(&task_queue.cond);
    pthread_mutex_unlock(&task_queue.mutex);
    //return task;
}

// Generic HTTP status response function
void http(int socket, int code, const char *status, const char *message) {
    char response[512];
    snprintf(response, sizeof(response),
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: text/html\r\n"
             "Content-Length: %zu\r\n"
             "\r\n"
             "<html><body><h1>%d %s</h1><p>%s</p></body></html>\r\n",
             code, status, strlen(message) + strlen(status)+28,
             code, status, message);

    send(socket, response, strlen(response), 0);
    //printf("%i %s: %s\n",code,status,message);
}

// Generic JSON response function
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

// Push new user account to database
int task_auth_create(Task* task, const json_t* json, PGconn* conn){
    //printf("creating account\n");
    int ret = 0;

    const json_t *usernf, *passnf, *emailnf;
    const char *user, *pass, *email;

    usernf = json_getProperty(json, "username");
    passnf = json_getProperty(json, "password"); // should really be encrypted
    emailnf = json_getProperty(json, "email");

    user = usernf ? json_getValue(usernf) : NULL;
    pass = passnf ? json_getValue(passnf) : NULL;
    email = emailnf ? json_getValue(emailnf) : NULL;

    if(user == NULL || pass == NULL || email == NULL) return 0;

    // Generate API key (not good use openssl goodness gracious)
    srand(time(NULL));
    char authkey[65];
    for(int j = 0; j < 64; j++) authkey[j] = 'A'+(rand()%26);
    authkey[64] = '\0';

    // SQL query for insert
    const char *sqlparams[4] = { user, pass, email, authkey};
    PGresult *pgres = PQexecParams(conn,sqlexec[SQL_ADDUSER],4,NULL,sqlparams,NULL,NULL,0);
    if (PQresultStatus(pgres) != PGRES_COMMAND_OK) {
        //////fprintf(stderr, "query no: %s\n", PQerrorMessage(conn));
        http(task->socket,409,"Conflict","Username or email already exists");
        //PQclear(pgres);
    }else{
        session_add_key(authkey);
        char json[128];
        snprintf(json,128,"{\"api_key\": \"%s\"}",authkey);
        httpjson(task->socket,json);
        //printf("create:! %s\n",json);
        ret = 2;
    }
    PQclear(pgres);
    count_create++;
    return ret;
}

// Get API key for user/password combo
int task_auth_get(Task* task, const json_t* json, PGconn* conn){
    int ret = 0;

    const json_t *usernf, *passnf;
    const char *user, *pass;
    usernf = json_getProperty(json, "username");
    passnf = json_getProperty(json, "password");  // this should really be encrypted but this is a prototype, can come back to this.
    user = usernf ? json_getValue(usernf) : NULL;
    pass = passnf ? json_getValue(passnf) : NULL;

    // basic validity check
    if(user == NULL || pass == NULL) return 0;
    if (conn == NULL || PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "Connection is not valid\n");
        return 0;
    }

    // SQL query
    const char *sqlparams[2] = { user, pass };
    PGresult *pgres = PQexecParams(conn,sqlexec[SQL_GETAUTHKEY],2,NULL,sqlparams,NULL,NULL,0);
    if (PQresultStatus(pgres) == PGRES_TUPLES_OK) {
        if (PQntuples(pgres) == 1) {
            // User is in database and password is correct
            char *authkey = PQgetvalue(pgres, 0, 0);
            session_add_key(authkey);
            char json[128];
            snprintf(json,128,"{\"api_key\": \"%s\"}",authkey);
            // return api token to client
            httpjson(task->socket,json);
            //printf("login:! %s\n",json);
            ret = 2;
        }else goto fail;
    } else {
        /////fprintf(stderr, "Query failed: %s\n", PQerrorMessage(conn));
        goto fail;
    }

    fail:
    http(task->socket,401,"Unauthorized","Incorrect username or password");

    PQclear(pgres);
    count_login++;
    return ret;
}

// Receive an image from client
int task_img_upload(Task* task, const char *buffer, const int buffer_size, const int total_size, PGconn* conn){
    // Create temporary filename
    char filename[32] = "res/";
    srand(time(NULL));
    for(int j = 4; j < 16; j++) filename[j] = 'A'+(rand()%26);
    filename[16] = '\0';
    strncat(filename,".png",sizeof(filename) - strlen(filename) - 1);

    // start MD5 hash
    unsigned char c[MD5_DIGEST_LENGTH];
    int i;
    MD5_CTX mdContext;
    unsigned char data[IMG_CHUNK_SIZE];
    MD5_Init (&mdContext);

    // open temporary file
    FILE *file = fopen(filename, "wb");
    if (file == NULL) {
        perror("Error opening file for writing");
        return 0;
    }

    // write first part of image from initial task buffer
    size_t written = 0;
    written += fwrite(buffer, 1, buffer_size, file);
    MD5_Update (&mdContext, buffer, written);

    // continue to get rest of image on socket
    char nbuff[IMG_CHUNK_SIZE];
    memset(nbuff, 0, sizeof(nbuff));
    ssize_t bytes_read,bytes_written = 0;
    while ((bytes_read = recv(task->socket, nbuff, sizeof(nbuff), 0)) > 0) {
        if(written < total_size) bytes_written = fwrite(nbuff, 1, (written+bytes_read) > total_size ? total_size-written : bytes_read, file);
        written += bytes_written;
        MD5_Update (&mdContext, nbuff, bytes_written);
    }

    MD5_Final (c,&mdContext);
    fclose(file);

    // convert hex MD5 digest to char array
    char buf[MD5_DIGEST_LENGTH * 2 + 1];
    for (int i = 0, j = 0; i < 16; i++, j+=2)
        sprintf(buf+j, "%02x", c[i]);
    buf[MD5_DIGEST_LENGTH * 2] = 0;
    printf ("md5: %s\n", buf);

    // make SQL query for MD5 hash to see if this hash should be on the server or not
    const char *sqlparams[1] = { buf };
    PGresult *pgres = PQexecParams(conn,sqlexec[SQL_CHECKMD5],1,NULL,sqlparams,NULL,NULL,0);
    if (PQresultStatus(pgres) == PGRES_TUPLES_OK) {
        if (PQntuples(pgres) == 0) {
            // delete the temporary file
            printf("image definitely not in table\n");
            remove(filename);
        }else{
            printf("image is in table\n");


            // create new file with name = MD5 hash
            char outfile[64] = {0};
            snprintf(outfile,64,"res/%s.png",buf);

            // copy the contents of the file
            FILE *source, *target;
            int i;
            source = fopen(filename, "rb"); 

            fseek(source, 0, SEEK_END);
            int length = ftell(source);
            fseek(source, 0, SEEK_SET);
            target = fopen(outfile, "wb"); 

            if( target == NULL ) fclose(source);
            for(i = 0; i < length; i++) fputc(fgetc(source), target);

            printf("File copied successfully.\n"); 
            fclose(source); 
            fclose(target);
            remove(filename);
        }
    }else{
        // sql error
        printf("bad request\n");
    }
    PQclear(pgres);

    return 1;
}

// parse a json array string and return it as a ummm json array string kind of silly
void sql_get_json_array(char* buffer, const json_t* array){
    if (array && JSON_ARRAY == json_getType(array)) {
        json_t const* index;
        for( index = json_getChild( array ); index != 0; index = json_getSibling( index ) ) {
            if ( JSON_TEXT == json_getType( index ) ) {
                char const* value = json_getValue( index );
                char part[32];

                if ( value ){
                    snprintf(part,sizeof(part),"\"%s\",",value);
                    if(strlen(buffer) < sizeof(buffer)-32) strncat(buffer,part,sizeof(part));
                }
            }
        }
        buffer[strlen(buffer)-1] = '\0';
    } else {
        printf("Array 'data' not found or not an array.\n");
    }
}

// submit to sample database
int task_db_upload(Task* task, const json_t* json, PGconn* conn){

    // frankly quit disgusting but this is how tinyjson works and hashmaps are ridiculous in C
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
    ////printf("%s\n",arg[0]);
    ////printf("%s\n",arg[1]);
    ////printf("%s\n",arg[2]);


    // i think this is the only seriously unsafe sql query that i need to paramaterize but i just didnt get around to it yet, its not that i dont know how to make it safe i just cant be bothered
    char sql[4096];
    char* endptr;
    snprintf(sql,4096,"INSERT INTO geo_metadata (image_md5,timestamp,latitude,longitude,altitude,sample_geo,sample_flora,sample_fauna,sample_calls)VALUES('%s','%s',%lf,%lf,%lf,%i,'[%s]','[%s]','[%s]');",
            md5,time,strtod(lat,&endptr),strtod(lon,&endptr),strtod(alt,&endptr),geo,arg[0],arg[1],arg[2]);
    PGresult *pgres = PQexec(conn,sql);

    //if (PQresultStatus(pgres) != PGRES_COMMAND_OK) printf("Query failed: %s\n", PQerrorMessage(conn));
    if (PQresultStatus(pgres) == PGRES_COMMAND_OK) count_db++;
    PQclear(pgres);
    return 1;
}
