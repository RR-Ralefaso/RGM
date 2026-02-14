#include <iostream>
using namespace std;
#include <cstring>

#ifdef _WIN32 //windows 32 bit
#include <winsock2.h>
#pragma comment(lib,"ws2_32.lib")
#else //macos ,linux
#include <sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<unistd.h>
#endif

#define PORT 8080 //listening port fot incomming connections


//socket initialisation
void initSockets(){
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2,2) , &wsaData) != 0 ){
        cerr << "WSAStartup Failed";
    }
#endif
}

//socket cleanup
void cleanupSockets(){
#ifdef _WIN32
    WSACleanup();
#endif
}



int main(){
    initSockets();

    //create socket
    int serverSocket;
#ifdef _WIN32
    serverSocket = socket(AF_INET, SOCK_STREAM, 0); // windows
#else
    serverSocket = socket(AF_INET, SOCK_STREAM, 0); // linux and macos
#endif
    if(serverSocket<0){
        cerr << "socket creation failed";
        cleanupSockets();
        return -1;
    }

    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;


    //bind socket to the address
    if (bind(serverSocket,(struct sockaddr*)&serverAddr,sizeof(serverAddr))<0){
        cerr << "Bind failed";
        close(serverSocket);
        cleanupSockets();
        return -1;
    }

    //listening for incoming connections
    if(listen(serverSocket,1)<0){
        cerr << "Listen failed";
        close(serverSocket);
        cleanupSockets();
        return -1;
    }

    cout << "waiting for connection...";

    //accepting client connection
    sockaddr_in clientAddr;
    socklen_t clientlen = sizeof(clientAddr);
    int clientSocket = accept(serverSocket, (struct sockaddr *)&clientAddr, &clientlen);
    if (clientSocket < 0)
    {
        cerr << "Accept failed";
        close(serverSocket);
        cleanupSockets();
        return -1;
    }



    //recieve data  from client
    char buffer[1024];
    int recvSize;
    while((recvSize = recv(clientSocket , buffer , sizeof(buffer),0))>0){
        buffer[recvSize] = '\0'; //null terminae the recieved data
        cout << "Recived" << buffer;
    }


    if (recvSize == 0 ){
        cout << "connnection closed by client";   
    }else{
        cerr << "recv failed";
    }

    close(clientSocket);
    close(serverSocket);
    cleanupSockets();
    return 0;
}
