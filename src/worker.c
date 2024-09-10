#include "server.h"

void* worker_thread(void* arg) {
    printf("starting worker thread\n");
    PGconn* conn = PQconnectdb("user=postgres dbname=postgres password=dev2813 hostaddr=127.0.0.1 port=5432");
    if (PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "Connection to database failed: %s\n", PQerrorMessage(conn));
        PQfinish(conn);
        return NULL;
    }

    while (1) {
        Task task = dequeue_task();
        
        PGresult* res = PQexec(conn, task.query);
        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            fprintf(stderr, "query no: %s\n", PQerrorMessage(conn));
            PQclear(res);
            continue;
        }else printf("query yes\n");

        int nrows = PQntuples(res);
        int nfields = PQnfields(res);

        // Build the HTTP response
        char response[BUFFER_SIZE];
        int offset = 0;

        // Write HTTP headers
        offset += snprintf(response + offset, sizeof(response) - offset,
                           "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\n\r\n",
                           nrows * nfields * 50); // Approximate content length

        // Write query results
        for (int i = 0; i < nrows; i++) {
            for (int j = 0; j < nfields; j++) {
                const char* value = PQgetvalue(res, i, j);
                offset += snprintf(response + offset, sizeof(response) - offset, "%s\t", value);
            }
            offset += snprintf(response + offset, sizeof(response) - offset, "\r\n");
        }

        // Send the response to the client
        if (send(task.client_fd, response, offset, 0) < 0) {
            perror("send");
        }

        PQclear(res);
        CLOSE_SOCKET(task.client_fd);
    }

    PQfinish(conn);
    return NULL;
}