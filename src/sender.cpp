#include <iostream>
using namespace std;

#include <cstring>
#ifdef _WIN32 //for windows

#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#else // For Linux and macOS
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#define SERVER_IP "127.0.0.1" // Server IP address
#define PORT 8080             // Same port as the server


//socket initialisation
void initSockets(){
#ifdef _WIN32
    WSADATA wsaData; 
    if(WSAStartup(MAKEWORD(2,2),&wsaData) != 0 ){
        cerr << "WSAStartup failed";
        exit(1);
    }
#endif
}

//socket cleanup
void cleanupsockets(){
#ifdef _WIN32
    WSACleanup();
#endif
}


int main(){
    initSockets();

    //create socket
    int clientsocket;
#ifdef _WIN32
    clientsocket = socket(AF_INET, SOCK_STREAM, 0); //windows
#else
    clientsocket = socket(AF_INET, SOCK_STREAM, 0);
#endif
    if(clientsocket<0){
        cerr << "socket creation failed";
        cleanupsockets();
        return -1;
    }

    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP); //ip address



    //connect to the server
    if (connect(clientsocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        cerr << "connetion failed";
        close(clientsocket);
        cleanupsockets();
        return -1;
    }

    const char *message = "greetings from sender";
    send(clientsocket, message, strlen(message), 0);
    cout << "message sent : " << message ;

    close(clientsocket);
    cleanupsockets();
    return 0; 
}