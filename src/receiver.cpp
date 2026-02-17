#include <iostream> // For cin, cout, cerr
#include <cstring>  // For strlen(), memset()
#include <string>   // For std::string
using namespace std;

#ifdef _WIN32 // Windows-specific headers
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#else                   // POSIX/Linux headers
#include <sys/socket.h> // socket(), bind(), listen(), accept()
#include <netinet/in.h> // sockaddr_in structure
#include <arpa/inet.h>  // inet_ntoa(), htons()
#include <unistd.h>     // close()
#endif

#define PORT 8080 // Port to listen on

// Initialize networking (Windows only)
void initSockets()
{
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        cerr << "WSAStartup Failed" << endl;
        exit(1);
    }
#endif
}

// Cleanup networking (Windows only)
void cleanupSockets()
{
#ifdef _WIN32
    WSACleanup();
#endif
}

int main()
{
    initSockets();

    // Create server socket
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0)
    {
        cerr << "socket creation failed" << endl;
        cleanupSockets();
        return -1;
    }

    // Bind to all interfaces on port 8080
    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;         // IPv4
    serverAddr.sin_port = htons(PORT);       // Network byte order
    serverAddr.sin_addr.s_addr = INADDR_ANY; // 0.0.0.0 = all interfaces

    if (bind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        cerr << "Bind failed - port " << PORT << " may be in use" << endl;
#ifdef _WIN32
        closesocket(serverSocket);
#else
        close(serverSocket);
#endif
        cleanupSockets();
        return -1;
    }
    cout << "Server bound to port " << PORT << endl;

    // Listen for connections (queue 1 client)
    if (listen(serverSocket, 1) < 0)
    {
        cerr << "Listen failed" << endl;
#ifdef _WIN32
        closesocket(serverSocket);
#else
        close(serverSocket);
#endif
        cleanupSockets();
        return -1;
    }
    cout << "Server listening on port " << PORT << "..." << endl;

    // Accept single client connection
    sockaddr_in clientAddr;
    socklen_t clientLen = sizeof(clientAddr);
    int clientSocket = accept(serverSocket, (struct sockaddr *)&clientAddr, &clientLen);
    if (clientSocket < 0)
    {
        cerr << "Accept failed" << endl;
#ifdef _WIN32
        closesocket(serverSocket);
#else
        close(serverSocket);
#endif
        cleanupSockets();
        return -1;
    }

    // Show client connection info
    cout << "Client connected from "
         << inet_ntoa(clientAddr.sin_addr) << ":" // IP address
         << ntohs(clientAddr.sin_port) << endl;   // Port
    cout << "Receiving messages..." << endl;

    // Receive loop - stays open until sender quits
    char buffer[1024]; // 1KB receive buffer
    int recvSize;
    while ((recvSize = recv(clientSocket, buffer, sizeof(buffer) - 1, 0)) > 0)
    {
        buffer[recvSize] = '\0'; // Null-terminate received data

        string message(buffer); // Convert to string for easy handling

        // Check for quit signal
        if (message.find(":q") == 0)
        {
            cout << "Sender quit command received. Closing..." << endl;
            break;
        }

        cout << "Received (" << recvSize << " bytes): " << message << endl;
    }

    // Handle disconnect reasons
    if (recvSize == 0)
    {
        cout << "Client disconnected normally" << endl;
    }
    else
    {
        cerr << "Connection error occurred" << endl;
    }

    // Cleanup sockets
#ifdef _WIN32
    closesocket(clientSocket);
    closesocket(serverSocket);
#else
    close(clientSocket);
    close(serverSocket);
#endif
    cleanupSockets();
    return 0;
}
