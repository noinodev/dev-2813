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
struct pollfd* fds;
char* fd_block;
int* fd_timeout;
_Atomic int tasks = 0, streamtasks = 0, count_db = 0, count_create = 0, count_login = 0;
_Atomic long long int time_hr = 0, time_hr_res = 0, time_lr = 0;

int main(int argc, char** argv) {
    // init server
    fds = (struct pollfd*)malloc(MAX_CLIENTS*sizeof(struct pollfd));
    fd_block = (char*)calloc(MAX_CLIENTS,1);
    fd_timeout = (int*)calloc(MAX_CLIENTS,sizeof(int));
    byte num_workers = 1;
    if(argv[1] != NULL) num_workers = atoi(argv[1]);
    int server_fd, client_fd, max_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE];

    // init thread pool
    pthread_t workers[num_workers];
    for (int i = 0; i < num_workers; ++i) pthread_create(&workers[i], NULL, worker_thread, NULL);

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
    setbuf(stdout, NULL);

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

    fds[0].fd = server_fd;
    fds[0].events = POLLIN;

    // start task scheduling loop
    int time = clock();
    while (1) {
        if(clock()-time > CLOCKS_PER_SEC){
            time = clock();
            if(tasks > 0){
                //printf("\033[2J"); // clear
                //printf("\033[H"); // move top left
                printf("\033[4A");
                printf("PROCESS:\n");
                printf("tasks: %i [db: %i, login: %i, create: %i]                 \n",tasks+streamtasks,count_db,count_login,count_create);
                printf("avg. task time: %Lfms [%lli ns]                  \n",((long double)time_lr)/tasks,(time_hr*100)/tasks);//(time_lr)/num_workers,(time_hr*100)/num_workers,(time_hr*100)/tasks);
                printf("server load: %Lf%                   \n",(((long double)time_hr*100.)/num_workers)/(long double)time_hr_res)*100;
            }
            count_db = 0;
            count_login = 0;
            count_create = 0;
            tasks = 0;
            streamtasks = 0;
            time_hr = 0;
            time_lr = 0;
        }
        int poll_count = POLL(fds, nfds, -1);
        //WSAPoll
        if(poll_count == -1) {
            perror("poll");
            //printf("NFDS: %i\n",nfds);
            CLOSE_SOCKET(server_fd);
            #ifdef _WIN32
            cleanup_winsock();
            #endif
            exit(EXIT_FAILURE);
        }

        if(fds[0].revents & POLLIN && nfds < MAX_CLIENTS) {
            client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
            if (client_fd < 0) {
                //perror("accept");
            }else{
                fds[nfds].fd = client_fd;
                fds[nfds].events = POLLIN;
                fd_block[nfds] = 0;
                fd_timeout[nfds] = clock();
                nfds++;
            }
        }

        for (int i = 1; i < nfds; i++) {
            if (nfds > 0 && (fds[i].revents & POLLIN) && fd_block[i] == 0) {
                int bytes_received = recv(fds[i].fd, buffer, BUFFER_SIZE - 1, 0);
                if (bytes_received > 0) {
                    //printf("br:: %i\n",bytes_received);
                    buffer[bytes_received] = '\0';
                    fd_timeout[i] = clock();

                    enqueue_task(bytes_received,fds[i].fd,buffer,i);
                    fd_block[i] = 1;
                } else if (bytes_received == 0) {
                    // client disconnect or error
                    CLOSE_SOCKET(fds[i].fd);
                    fds[i].fd = -1;
                }
            }
        }

        // socket buffer cleaner or something
        for (int i = 1; i < nfds; i++) {
            if(clock()-fd_timeout[i] > 10*CLOCKS_PER_SEC){
                CLOSE_SOCKET(fds[i].fd);
                fds[i].fd = -1;
                //printf("timeout %i\n",i);
            }
            if (fds[i].fd == -1) {
                fds[i] = fds[nfds-1];
                fds[nfds-1].fd = -1;
                nfds--;
                if(nfds == 1){
                    //printf("all connections timed out\n");
                }
                //printf("nfds: %i\n",nfds);
                /*for (int j = i; j < nfds - 1; j++) {
                    fds[j] = fds[j + 1];
                }
                nfds--;*/
            }
        }
    }

    // end threads
    for(int i = 0; i < num_workers; i++) pthread_cancel(workers[i]);
    for(int i = 0; i < MAX_CLIENTS; i++){
        if(fds[i].fd != -1) CLOSE_SOCKET(fds[i].fd);
    }

    free(fds);
    free(fd_block);
    free(fd_timeout);

    // cleanup
    CLOSE_SOCKET(server_fd);
    #ifdef _WIN32
    cleanup_winsock();
    #endif
    return 0;
}
