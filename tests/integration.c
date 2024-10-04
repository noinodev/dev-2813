#include <stdio.h>
#include <time.h>
#include <pthread.h>

#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#include <windows.h>
#include <mstcpip.h>
#pragma comment(lib, "ws2_32.lib")

void init_winsock();
void cleanup_winsock();

#define CLOSE_SOCKET(s) closesocket(s)
#define SOCKET_ERROR_CODE SOCKET_ERROR
#define INVALID_SOCKET_CODE INVALID_SOCKET
#define POLL WSAPoll
typedef int socklen_t;

#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <poll.h>

void init_winsock();
void cleanup_winsock();

#define CLOSE_SOCKET(s) close(s)
#define SOCKET_ERROR_CODE -1
#define INVALID_SOCKET_CODE -1
#define POLL poll

#endif

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

char* WORDS[] = {
    "apple",
    "banana",
    "orange",
    "grape",
    "mango",
    "cherry",
    "peach",
    "pear",
    "plum",
    "kiwi",
    "strawberry",
    "blueberry",
    "raspberry",
    "lemon",
    "lime",
    "watermelon",
    "pineapple",
    "papaya",
    "coconut",
    "apricot",
    "fig",
    "pomegranate",
    "blackberry",
    "dragonfruit",
    "lychee",
    "guava",
    "passionfruit",
    "cranberry",
    "tangerine",
    "nectarine",
    "avocado"
};

const char* URI[] = {"/auth/create", "/auth/login", "/api/sample", "/api/file"};
const char* TYPE[] = {"application/json","application/octet-stream"};

_Atomic volatile int fails = 0;
_Atomic volatile int succs = 0;

char* rword(){
    return WORDS[rand()%32];
}

void create(char* str){
    char json[512];
    snprintf(json, 512, "{\"username\":\"%s\",\"password\":\"%s!\",\"email\":\"%s@gmail.com\"}", rword(), rword(), rword());
    snprintf(str,1024,
            "POST /auth/create HTTP/1.1\r\n"
            "Content-Type: application/json\n"
            "Content-Length: %i\n\n%s\r\n",
            strlen(json),json);
}

void login(char* str){
    char json[512];
    snprintf(json,512,"{\"username\":\"%s\",\"password\":\"%s!\"}",rword(),rword());
    snprintf(str,1024,
            "POST /auth/login HTTP/1.1\n"
            "Content-Type: application/json\n"
            "Content-Length: %i\n\n%s\r\n",
            strlen(json),json
    );
}

void db(char* str){
    char json[512];
    char md5[32] = {'A'};
    for(int i = 0; i < 32; i++) md5[i] = 'A'+rand()%26;
    md5[31] = 0;
    snprintf(json,512,
        "{\"img_md5\":\"%s\","
        "\"timestamp\":\"2000-12-09\","
        "\"latitude\":4743.546754,"
        "\"longitude\":729.55436546,"
        "\"altitude\":4935.3,"
        "\"metadata\":35983465,"
        "\"flora\":[\"%s\",\"%s\",\"%s\"],"
        "\"fauna\":[\"%s\",\"%s\",\"%s\"],"
        "\"calls\":[\"%s\",\"%s\",\"%s\"]}",
        md5,
        rword(),rword(),rword(),
        rword(),rword(),rword(),
        rword(),rword(),rword()
    );
    snprintf(
        str,1024,
        "POST /api/sample HTTP/1.1\n"
        "Authorization: Bearer UCDHAIGXTFRXVCCIAGOMBJXOQLWTUORBTCTIRKURPPNRVQIACQINDSXTSGGCFXJR\n"
        "Content-Type: application/json\n"
        "Content-Length: %i\n\n%s\n",
        strlen(json),json
    );
}

void baka(char* str){
    snprintf(
        str,1024,
        "POST /idiot HTTP/1.1\n"
        "Authorization: Bearer UCDHAIGXTFRXVCCIAGOMBJXOQLWTUORBTCTIRKURPPNRVQIACQINDSXTSGGCFXJR\n"
        "Content-Type: application/idiot\n"
        "Content-Length: 0\n\n"
    );
}

int http(int socket) {
    char message[1024];
    // uri, type, body
    int i = rand()%9;
    if(i < 3) create(message);
    else if(i < 6) login(message);
    else db(message);

    int j = send(socket, message, strlen(message), 0);
    if(j <= 0){
        //printf("%i, %d ",j,WSAGetLastError());
        return 0;
    }else return 1;
}

typedef struct ccpc {
    int cc,pc,id;
    struct sockaddr_in server;
} ccpc;

int* clientsock;

void* worker_thread(void* arg){
    ccpc *a = (ccpc*)arg;
    printf("worker %d ok, ",a->id);
    int offset = a->cc*(a->id);
    //thread++;

    // integration test, fire off packets as fast as possible
    int time = clock();
    for(int j = 0; j < a->pc; j++){
        for(int i = 0; i < a->cc; i++){
            if(clock()-time > 10*CLOCKS_PER_SEC) goto end;
            if(clientsock[i+offset] == -1 || http(clientsock[i+offset]) == 0){
                fails += a->cc-i;
                break;
            }else succs++;
        }
    }

    // cleanup
    end:
    printf("time to send: %lf\n",((double)(clock()-time))/CLOCKS_PER_SEC);
    return NULL;
}

int main(int argc, char** argv) {
    srand(time(NULL));
    int cc = atoi(argv[2]), pc = atoi(argv[3]), tc = atoi(argv[1]);
    struct sockaddr_in server;

    #ifdef _WIN32
    init_winsock(); // self explanatory i think
    #endif
    char* ipaddr = "127.0.0.1";

    // define target -> TCP server at ipaddr:port
    server.sin_addr.s_addr = inet_addr(ipaddr);
    server.sin_family = AF_INET;
    server.sin_port = htons(8888);

    clientsock = malloc(cc*tc*sizeof(int));
    for(int i = 0; i < cc*tc; i++){
        clientsock[i] = socket(AF_INET, SOCK_STREAM, 0);
        //int flag = 1;
        //setsockopt(clientsock[i], IPPROTO_TCP, TCP_NODELAY, (void *)&flag, sizeof(int));
        if (clientsock[i]  == INVALID_SOCKET_CODE) {
            perror("socket");
            #ifdef _WIN32
            cleanup_winsock();
            #endif
            exit(EXIT_FAILURE);
        }

        int res = connect(clientsock[i], (struct sockaddr *) &server, sizeof(server));
        if (res < 0) {
            perror("ERROR connection failed");
            CLOSE_SOCKET(clientsock[i]);
            clientsock[i] = -1;
            //return 0;
        }else printf("%i,",i);
    }

    printf("\nall sockets connected!\n");

    //ccpc a = {cc,pc,server};
    ccpc* args = calloc(tc,sizeof(ccpc));
    pthread_t* workers = (pthread_t*)malloc(tc*sizeof(pthread_t));
    for (int i = 0; i < tc; i++){
        args[i].cc = cc;
        args[i].pc = pc;
        args[i].id = i;
        args[i].server = server;
        pthread_create(&workers[i], NULL, worker_thread, (void*)&args[i]);
    }
    

    for (int i = 0; i < tc; i++) pthread_join(workers[i],NULL);
    printf("packets: %i\n",succs);
    printf("fails: %i\n",fails);
    free(workers);
    free(args);
    for(int i = 0; i < cc*tc; i++){
        CLOSE_SOCKET(clientsock[i]);
    }
    free(clientsock);
    #ifdef _WIN32
    cleanup_winsock();
    #endif
    return 0;
}
