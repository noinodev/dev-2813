#ifndef AUTH_H
#define AUTH_H

#include <time.h>
#include "server.h"

typedef struct {
    char authkey[64];
    time_t lastupdated;
}session;

extern session activekeys[MAX_CLIENTS];
extern int keycount;

void session_add_key(const char* key);
void session_remove_key(const char* key);
int session_find_key(const char* key);
int session_auth(PGconn* conn, const char* key);

#endif