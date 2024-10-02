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

char* rword(){
    return WORDS[rand()%32];
}

void create(char* str){
    //printf("create");
    char json[512];
    snprintf(json, 512, "{\"username\":\"%s\",\"password\":\"%s!\",\"email\":\"%s@gmail.com\"}", rword(), rword(), rword());
    snprintf(str,1024,
            "POST /auth/create HTTP/1.1\r\n"
            "Content-Type: application/json\n"
            "Content-Length: %i\n\n%s\r\n",
            strlen(json),json);
}

void login(char* str){
    //printf("login");
    char json[512];
    snprintf(json,512,"{\"username\":\"%s\",\"password\":\"%s!\"}",rword(),rword());
    snprintf(str,1024,
            "POST /auth/login HTTP/1.1\n"
            "Content-Type: application/json\n"
            "Content-Length: %i\n\n%s\r\n",
            strlen(json),json
    );
    //printf("json: %s\nlen: %i\n",json,strlen(json));
}

void db(char* str){
    //printf("db");
    char json[512];
    snprintf(json,512,
        "{\"img_md5\":\"%s%s%s\","
        "\"timestamp\":\"2000-12-09\","
        "\"latitude\":4743.546754,"
        "\"longitude\":729.55436546,"
        "\"altitude\":4935.3,"
        "\"metadata\":35983465,"
        "\"flora\":[\"%s\",\"%s\",\"%s\"],"
        "\"fauna\":[\"%s\",\"%s\",\"%s\"],"
        "\"calls\":[\"%s\",\"%s\",\"%s\"]}",
        rword(),rword(),rword(),
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
    //int uri = rand()%sizeof(URI);
    srand(clock());
    char message[1024];
    // uri, type, body
    int i = rand()%9;
    if(i < 3) create(message);
    else if(i < 6) login(message);
    else db(message);
    //baka(message);
    //login(message);
    //printf("%s\n",message);

    int j = send(socket, message, strlen(message), 0);
    if(j <= 0){
        perror("send: ");
        return 0;
    }else return 1;
    //if(j != strlen(message)) printf("problem: %i/%i\n",j,strlen(message));
    //printf("\n%s\n, ",message);
}

typedef struct ccpc {
    int cc,pc;
    struct sockaddr_in server;
} ccpc;

int fails = 0;

void* worker_thread(void* arg){
    ccpc *a = (ccpc*)arg;
    // create TCP socket
    int* clientsock = malloc(a->cc*sizeof(int));
    for(int i = 0; i < a->cc; i++){
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
    }

    printf("Attempting to connect... ");

    // attempt connection
    for(int i = 0; i < a->cc; i++){
        int res = connect(clientsock[i], (struct sockaddr *) &a->server, sizeof(a->server));
        if (res < 0) {
            perror("ERROR connection failed");
            return 0;
        }else printf("%i,",i);
    }
    printf("all sockets open!\n");

    // integration test, fire off packets as fast as possible
    int time = clock();
    for(int j = 0; j < a->pc; j++){
        //printf("%i,",j);
        for(int i = 0; i < a->cc; i++){
            //printf("%i,",j,i);
            if(http(clientsock[i]) == 0){
                printf("failed at %i,%i\n",j,i);
                fails++;
                break;
            }
        }
        //printf("%i,",j);
    }
    printf("time to send: %lf\n",((double)(clock()-time))/CLOCKS_PER_SEC);

    // cleanup
    for(int i = 0; i < a->cc; i++) CLOSE_SOCKET(clientsock[i]);
    free(clientsock);
    return NULL;
}

int main(int argc, char** argv) {
    int cc = atoi(argv[2]), pc = atoi(argv[3]), tc = atoi(argv[1]);
    struct sockaddr_in server;

    #ifdef _WIN32
    init_winsock(); // self explanatory i think
    #endif

    /*struct hostent *host;
    host = gethostbyname("127.0.0.1");

    char* ipaddr;
    if (host == NULL) {
        printf("ERROR cannot resolve hostname\n");
        return 0;
    }
    
        
    ipaddr = inet_ntoa(*(struct in_addr*)host->h_addr_list[0]);*/
    char* ipaddr = "127.0.0.1";

    // define target -> TCP server at ipaddr:port
    server.sin_addr.s_addr = inet_addr(ipaddr);
    server.sin_family = AF_INET;
    server.sin_port = htons(8888);

    ccpc a = {cc,pc,server};
    pthread_t* workers = (pthread_t*)malloc(tc*sizeof(pthread_t));
    for (int i = 0; i < tc; ++i) pthread_create(&workers[i], NULL, worker_thread, (void*)&a);
    

    for (int i = 0; i < tc; ++i) pthread_join(workers[i],NULL);
    printf("fails: %i\n",fails);
    free(workers);
    #ifdef _WIN32
    cleanup_winsock();
    #endif
    return 0;
}
