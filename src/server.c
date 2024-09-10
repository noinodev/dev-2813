#include "server.h"

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
TaskQueue task_queue = {
    .tasks = {{0}},
    .head = 0,
    .tail = 0,
    .count = 0,
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER
};

// Add task to the queue
void enqueue_task(int client_fd, const char* query) {
    pthread_mutex_lock(&task_queue.mutex);
    while (task_queue.count == QUEUE_SIZE) {
        pthread_cond_wait(&task_queue.cond, &task_queue.mutex);
    }
    task_queue.tasks[task_queue.tail].client_fd = client_fd;
    strncpy(task_queue.tasks[task_queue.tail].query, query, sizeof(task_queue.tasks[task_queue.tail].query) - 1);
    task_queue.tail = (task_queue.tail + 1) % QUEUE_SIZE;
    task_queue.count++;
    pthread_cond_signal(&task_queue.cond);
    pthread_mutex_unlock(&task_queue.mutex);
}

// Get task from the queue
Task dequeue_task() {
    Task task;
    pthread_mutex_lock(&task_queue.mutex);
    while (task_queue.count == 0) {
        pthread_cond_wait(&task_queue.cond, &task_queue.mutex);
    }
    task = task_queue.tasks[task_queue.head];
    task_queue.head = (task_queue.head + 1) % QUEUE_SIZE;
    task_queue.count--;
    pthread_cond_signal(&task_queue.cond);
    pthread_mutex_unlock(&task_queue.mutex);
    return task;
}

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

    // Set the first slot to monitor the server socket
    fds[0].fd = server_fd;
    fds[0].events = POLLIN;  // Monitor for incoming connections

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

            // Add new client to the poll array
            fds[nfds].fd = client_fd;
            fds[nfds].events = POLLIN;  // Monitor for incoming data
            nfds++;

            printf("New connection accepted, fd: %d\n", client_fd);
        }

        // Check the other clients for incoming data
        for (int i = 1; i < nfds; i++) {
            if (fds[i].revents & POLLIN) {
                // Receive data from the client
                int bytes_received = recv(fds[i].fd, buffer, BUFFER_SIZE - 1, 0);
                if (bytes_received > 0) {
                    buffer[bytes_received] = '\0';
                    //printf("Received: %s\n", buffer);

                    /*const char* http_response = "HTTP/1.1 200 OK\r\n"
                                    "Content-Length: 13\r\n"
                                    "Connection: close\r\n"
                                    "\r\n"
                                    "Hello, morgus!";*/
                    //int res = send(fds[i].fd, http_response, strlen(http_response), 0);
                    //printf("Sent http request: '%i'\n",res);
                    //const char* sql_query = "INSERT INTO geo_metadata (image_md5, timestamp, latitude, longitude, altitude, sample_geo, sample_fauna, sample_flora)\
                                            VALUES ('your_md5_hash', '2024-09-10 10:15:30', 40.712776, -74.005974, 15.5,null,null,null);";
                    const char *sql_query = 
                    enqueue_task(fds[i].fd,sql_query);
                    //CLOSE_SOCKET(fds[i].fd);
                    //fds[i] = fds[nfds - 1];  // Move last entry to current slot
                    //nfds--;   
                } else if (bytes_received == 0) {
                    // Client disconnected
                    printf("Client disconnected, fd: %d\n", fds[i].fd);
                    CLOSE_SOCKET(fds[i].fd);
                    fds[i].fd = -1;
                }
            }else if (fds[i].revents & (POLLERR | POLLHUP)) {
                // Handle errors or hangups
                printf("Socket %d encountered an error\n", fds[i].fd);
                CLOSE_SOCKET(fds[i].fd);
                fds[i].fd = -1;
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
