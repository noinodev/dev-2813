#include "server.h"
#include "tasks.h"

#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#ifdef _WIN32
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

int nfds = 1;
struct pollfd fds[MAX_CLIENTS];
char fd_block[MAX_CLIENTS];

int main() {
    int server_fd, client_fd, max_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE];

    pthread_t workers[NUM_WORKERS];
    for (int i = 0; i < NUM_WORKERS; ++i) pthread_create(&workers[i], NULL, worker_thread, NULL);

    #ifdef _WIN32
    init_winsock();
    #endif

    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET_CODE) {
        perror("socket");
        #ifdef _WIN32
        cleanup_winsock();
        #endif
        exit(EXIT_FAILURE);
    }

    // Set socket to non-blocking mode
    set_nonblocking(server_fd);

    // Bind socket to address
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

    // Listen for incoming connections
    if (listen(server_fd, 5) == SOCKET_ERROR_CODE) {
        perror("listen");
        CLOSE_SOCKET(server_fd);
        #ifdef _WIN32
        cleanup_winsock();
        #endif
        exit(EXIT_FAILURE);
    }

    // Prepare the array for poll/WSAPoll
    memset(fds, 0, sizeof(fds));
    memset(fd_block, 0, sizeof(fd_block));

    // Set the first slot to monitor the server socket
    fds[0].fd = server_fd;
    fds[0].events = POLLIN;  // Monitor for incoming connections

    //unsigned char client_ip[ADDRLEN];
    while (1) {
        // Use WSAPoll on Windows and poll on Unix
        int poll_count = POLL(fds, nfds, -1);
        if (poll_count == -1) {
            perror("poll");
            CLOSE_SOCKET(server_fd);
            exit(EXIT_FAILURE);
        }

        // Check if the server socket is ready to accept new connections
        if (fds[0].revents & POLLIN) {
            // Accept new connection
            client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
            if (client_fd < 0) {
                perror("accept");
                continue;
            }
            //memcpy(buffer, &(address.sin_addr), sizeof(address.sin_addr));

            // Add new client to the poll array
            fds[nfds].fd = client_fd;
            fds[nfds].events = POLLIN;  // Monitor for incoming data
            nfds++;

            printf("New connection accepted, fd: %d\n", client_fd);
        }

        // Check the other clients for incoming data
        for (int i = 1; i < nfds; i++) {
            if ((fds[i].revents & POLLIN) && fd_block[i] == 0) {
                // Receive data from the client
                //char* buffer_http = buffer+ADDRLEN;
                int bytes_received = recv(fds[i].fd, buffer, BUFFER_SIZE - 1, 0);
                if (bytes_received > 0) {
                    buffer[bytes_received] = '\0';

                    enqueue_task(bytes_received,fds[i].fd,buffer,i);
                    fd_block[i] = 1;
                } else if (bytes_received == 0) {
                    // Client disconnected
                    printf("Client disconnected, fd: %d\n", fds[i].fd);
                    CLOSE_SOCKET(fds[i].fd);
                    fds[i].fd = -1;
                }
            }
        }

        for (int i = 0; i < nfds; i++) {
            if (fds[i].fd == -1) {
                for (int j = i; j < nfds - 1; j++) {
                    fds[j] = fds[j + 1];  // Shift remaining fds down
                }
                nfds--;  // Reduce the total number of file descriptors
            }
        }
    }

    for (int i = 0; i < NUM_WORKERS; ++i) pthread_cancel(workers[i]);

    CLOSE_SOCKET(server_fd);
    #ifdef _WIN32
    cleanup_winsock();
    #endif
    return 0;
}
