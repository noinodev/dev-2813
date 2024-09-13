#include "server.h"
#include "tasks.h"
#include "auth.h"
#include <time.h>

session activekeys[MAX_CLIENTS] = {0};
int keycount = 0;

// push key to end of array, but make sure its not already there
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

// pop key from array, move end key to index
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

// check if key is in session cache
int session_find_key(const char* key){
    for(int i = 0; i < keycount; i++){
        if(strncmp(activekeys[i].authkey,key,64) == 0){
            return 1;
        }
    }
    return 0;
}

// check session cache and database for auth token
int session_auth(PGconn* conn, const char* key){
    // check session cache
    if(session_find_key(key) == 1) return 1;

    // make sure db connection is still live
    if (conn == NULL || PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "Connection is not valid\n");
        return 0;
    }
    int ret = 0;

    // sql query
    const char *sqlparams[1] = { key };
    PGresult *pgres = PQexecParams(conn,sqlexec[SQL_CHECKAUTHKEY],1,NULL,sqlparams,NULL,NULL,0);

    if (PQresultStatus(pgres) != PGRES_TUPLES_OK) {
        fprintf(stderr, "Query failed: %s\n", PQerrorMessage(conn));
        
    } else {
        if (PQntuples(pgres) > 0) {
            // key was found, add it to session
            session_add_key(key);
            ret = 1;
        }
    }

    PQclear(pgres);
    return ret;
}