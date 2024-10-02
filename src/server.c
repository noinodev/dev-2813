#include "server.h"
#include "tasks.h"

#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

// windows/unix compatability directives for sockets

#ifdef _WIN32
// initialize with windows compatability
void init_winsock() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "Failed to initialize Winsock.\n");
        exit(EXIT_FAILURE);
    }
}

void cleanup_winsock() {
    WSACleanup();
}

#else
// POSIX doesnt need setup for sockets
void init_winsock() {}
void cleanup_winsock() {}
#endif

void set_nonblocking(int fd) {
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(fd, FIONBIO, &mode);
#else
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

// socket file descriptor arrays
int nfds = 1;
struct pollfd fds[MAX_CLIENTS];
char fd_block[MAX_CLIENTS];
int tasks = 0;
int streamtasks = 0;

int main() {
    // init server
    int server_fd, client_fd, max_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE];

    // init thread pool
    pthread_t workers[NUM_WORKERS];
    for (int i = 0; i < NUM_WORKERS; ++i) pthread_create(&workers[i], NULL, worker_thread, NULL);

    #ifdef _WIN32
    init_winsock(); // self explanatory i think
    #endif

    // create a TCP server socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET_CODE) {
        perror("socket");
        #ifdef _WIN32
        cleanup_winsock();
        #endif
        exit(EXIT_FAILURE);
    }

    // set server to non blocking
    set_nonblocking(server_fd);

    // get address and port and bind server to socket
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    if (bind(server_fd, (struct sockaddr *)&address, addrlen) == SOCKET_ERROR_CODE) {
        perror("bind");
        CLOSE_SOCKET(server_fd);
        #ifdef _WIN32
        cleanup_winsock();
        #endif
        exit(EXIT_FAILURE);
    }

    // start listener
    if (listen(server_fd, 5) == SOCKET_ERROR_CODE) {
        perror("listen");
        CLOSE_SOCKET(server_fd);
        #ifdef _WIN32
        cleanup_winsock();
        #endif
        exit(EXIT_FAILURE);
    }
    memset(fds, 0, sizeof(fds));
    memset(fd_block, 0, sizeof(fd_block));

    fds[0].fd = server_fd;
    fds[0].events = POLLIN;

    // start task scheduling loop
    int time = clock();
    while (1) {
        if(clock()-time > CLOCKS_PER_SEC){
            time = clock();
            if(tasks > 0) printf("tasks this second: %i\n",tasks);
            if(streamtasks > 0) printf("streamtasks : %i\n",streamtasks);
            tasks = 0;
            streamtasks = 0;
        }
        int poll_count = POLL(fds, nfds, -1);
        if (poll_count == -1) {
            perror("poll");
            CLOSE_SOCKET(server_fd);
            exit(EXIT_FAILURE);
        }

        if (fds[0].revents & POLLIN) {
            client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
            if (client_fd < 0) {
                perror("accept");
                continue;
            }//else printf("n");

            fds[nfds].fd = client_fd;
            fds[nfds].events = POLLIN;
            nfds++;
        }

        for (int i = 1; i < nfds; i++) {
            if ((fds[i].revents & POLLIN) && fd_block[i] == 0) {
                int bytes_received = recv(fds[i].fd, buffer, BUFFER_SIZE - 1, 0);
                if (bytes_received > 0) {
                    //printf("br:: %i\n",bytes_received);
                    buffer[bytes_received] = '\0';

                    enqueue_task(bytes_received,fds[i].fd,buffer,i);
                    fd_block[i] = 1;
                } else if (bytes_received <= 0) {
                    // client disconnect or error
                    CLOSE_SOCKET(fds[i].fd);
                    fds[i].fd = -1;
                }
            }
        }

        // socket buffer cleaner or something
        for (int i = 0; i < nfds; i++) {
            if (fds[i].fd == -1) {
                fds[i] = fds[--nfds];
                fds[nfds].fd = -1;
                /*for (int j = i; j < nfds - 1; j++) {
                    fds[j] = fds[j + 1];
                }
                nfds--;*/
            }
        }
    }

    // end threads
    for (int i = 0; i < NUM_WORKERS; ++i) pthread_cancel(workers[i]);

    // cleanup
    CLOSE_SOCKET(server_fd);
    #ifdef _WIN32
    cleanup_winsock();
    #endif
    return 0;
}
