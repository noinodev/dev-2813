#include "server.h"
#include "tasks.h"
#include "auth.h"
#include <time.h>

session activekeys[MAX_CLIENTS] = {0};
int keycount = 0;

void session_add_key(const char* key){
    for(int i = 0; i < keycount; i++){
        if(strncmp(activekeys[i].authkey,key,64) == 0){
            activekeys[i].lastupdated = time(NULL);
            return;
        }
    }
    strncpy(activekeys[keycount].authkey,key,64);
    activekeys[keycount].lastupdated = time(NULL);
    keycount++;
}

void session_remove_key(const char* key){
    for(int i = 0; i < keycount; i++){
        if(strncmp(activekeys[i].authkey,key,64) == 0){
            activekeys[i].lastupdated = activekeys[keycount-1].lastupdated;
            strncpy(activekeys[i].authkey,activekeys[keycount-1].authkey,64);
            keycount--;
            return;
        }
    }
}

int session_find_key(const char* key){
    for(int i = 0; i < keycount; i++){
        if(strncmp(activekeys[i].authkey,key,64) == 0){
            return 1;
        }
    }
    return 0;
}

int session_auth(PGconn* conn, const char* key){
    if(session_find_key(key) == 1) return 1;

    //printf("key not in session, checking db... ");
    if (conn == NULL || PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "Connection is not valid\n");
        return 0;
    }//else printf("db connection ok... ");
    int ret = 0;

    //const char* testkey = "UCDHAIGXTFRXVCCIAGOMBJXOQLWTUORBTCTIRKURPPNRVQIACQINDSXTSGGCFXJR";
    //printf("\n\nVERY FUCKING IMPORTANT:\n%s\n%s\ncmp: %i\n\n",key,testkey,strncmp(key,testkey,64));
    const char *sqlparams[1] = { key };
    PGresult *pgres = PQexecParams(conn,sqlexec[SQL_CHECKAUTHKEY],1,NULL,sqlparams,NULL,NULL,0);
    //printf("Connection status: %s\n", PQerrorMessage(conn));
    //int rows = PQntuples(pgres);
    //printf("Number of rows: %d\n", rows);

    if (PQresultStatus(pgres) != PGRES_TUPLES_OK) {
        fprintf(stderr, "Query failed: %s\n", PQerrorMessage(conn));
        
    } else {
        if (PQntuples(pgres) > 0) {
            //printf("key found in db... ");
            session_add_key(key);
            ret = 1;
        } else {
            printf("key not in db. ");
        }
    }

    PQclear(pgres);
    return ret;
}