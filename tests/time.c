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
    int i = rand()%12;
    if(i < 3) create(message);
    else if(i < 6) login(message);
    else if(i <= 9) db(message);
    else baka(message);


    int j = send(socket, message, strlen(message), 0);
    //printf("SEND %i BYTES:\n%s\n",j,message);
    if(j <= 0){
        //printf("%i, %d ",j,WSAGetLastError());
        return 0;
    }else return 1;
}

int main(int argc, char** argv) {
    srand(time(NULL));
    int pc = atoi(argv[1]);
    struct sockaddr_in server;

    #ifdef _WIN32
    init_winsock(); // self explanatory i think
    #endif
    char* ipaddr = "127.0.0.1";

    // define target -> TCP server at ipaddr:port
    server.sin_addr.s_addr = inet_addr(ipaddr);
    server.sin_family = AF_INET;
    server.sin_port = htons(8888);

    
    int clientsock = socket(AF_INET, SOCK_STREAM, 0);
        //int flag = 1;
        //setsockopt(clientsock[i], IPPROTO_TCP, TCP_NODELAY, (void *)&flag, sizeof(int));
    if (clientsock  == INVALID_SOCKET_CODE) {
        perror("socket");
        #ifdef _WIN32
        cleanup_winsock();
        #endif
        exit(EXIT_FAILURE);
    }
    printf("Attempting to connect... ");

    int res = connect(clientsock, (struct sockaddr *)&server, sizeof(server));
    if (res < 0) {
        perror("ERROR connection failed");
        return 0;
    }
    printf("ok!\n");

    // integration test, fire off packets as fast as possible
    long double avgtime = 0;
    char buff[1024] = {0};
    for(int j = 0; j < pc; j++){
        int t = clock();
        printf("[%i] PING\n",j);
        http(clientsock);
        int bytes = recv(clientsock,buff,1024-1,0);
        int time = clock()-t;
        if(bytes <= 0) perror("recv");
        else printf("RECV %i BYTES:\n",bytes);
        buff[bytes] = 0;
        avgtime += (long double)time/pc;
        //printf("%s\n",buff);
        printf("PONG [%ims]\n-------------\n",time);
        Sleep(100);
    }

    printf("Average response time: %Lf",avgtime);
    
    #ifdef _WIN32
    cleanup_winsock();
    #endif
    return 0;
}
