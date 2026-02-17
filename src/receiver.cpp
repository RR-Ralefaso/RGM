#include <iostream>  // Standard I/O for console output
#include <cstring>   // For memset(), strlen()
using namespace std; // Use standard namespace

#ifdef _WIN32 // Windows-specific socket headers
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib") // Link Windows Socket library
#else                              // Linux/macOS socket headers
#include <sys/socket.h>            // Socket API functions
#include <netinet/in.h>            // Socket address structures
#include <arpa/inet.h>             // IP address conversion
#include <unistd.h>                // For close() function
#endif

#define PORT 8080 // TCP port for incoming connections

// Initialize Winsock on Windows (Linux/macOS don't need this)
void initSockets()
{
#ifdef _WIN32
    WSADATA wsaData; // Data structure for Winsock version info
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        cerr << "WSAStartup Failed" << endl;
        exit(1); // Exit on failure (missing from original)
    }
#endif
}

// Cleanup Winsock on Windows only
void cleanupSockets()
{
#ifdef _WIN32
    WSACleanup(); // Shutdown Winsock
#endif
}

int main()
{
    initSockets(); // Setup networking

    // Create TCP IPv4 socket for server
    int serverSocket;
#ifdef _WIN32
    serverSocket = socket(AF_INET, SOCK_STREAM, 0); // Windows socket creation
#else
    serverSocket = socket(AF_INET, SOCK_STREAM, 0); // Linux/macOS socket creation
#endif

    if (serverSocket < 0)
    { // Check socket creation
        cerr << "socket creation failed" << endl;
        cleanupSockets();
        return -1;
    }

    // Server address structure - listen on all interfaces
    sockaddr_in serverAddr;                     // IPv4 address structure
    memset(&serverAddr, 0, sizeof(serverAddr)); // Zero out structure
    serverAddr.sin_family = AF_INET;            // IPv4 protocol family
    serverAddr.sin_port = htons(PORT);          // Convert port to network byte order
    serverAddr.sin_addr.s_addr = INADDR_ANY;    // Bind to all available interfaces (0.0.0.0)

    // Bind socket to address/port
    if (bind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        cerr << "Bind failed - port " << PORT << " may be in use" << endl;
        close(serverSocket); // Close socket on failure
        cleanupSockets();
        return -1;
    }
    cout << "Server bound to port " << PORT << endl;

    // Start listening for incoming connections (queue up to 1 client)
    if (listen(serverSocket, 1) < 0)
    { // SOMAXCONN or 1 for single client
        cerr << "Listen failed" << endl;
        close(serverSocket);
        cleanupSockets();
        return -1;
    }
    cout << "Server listening on port " << PORT << "..." << endl;

    // Accept incoming client connection
    sockaddr_in clientAddr;                   // Client address info
    socklen_t clientLen = sizeof(clientAddr); // Size of client address structure
    int clientSocket = accept(serverSocket, (struct sockaddr *)&clientAddr, &clientLen);
    if (clientSocket < 0)
    {
        cerr << "Accept failed" << endl;
        close(serverSocket);
        cleanupSockets();
        return -1;
    }

    cout << "Client connected from "
         << inet_ntoa(clientAddr.sin_addr) << ":"
         << ntohs(clientAddr.sin_port) << endl;

    // Receive data loop - persistent connection
    char buffer[1024]; // Receive buffer
    int recvSize;      // Bytes received
    while ((recvSize = recv(clientSocket, buffer, sizeof(buffer) - 1, 0)) > 0)
    {
        buffer[recvSize] = '\0'; // Null-terminate received string
        cout << "Received (" << recvSize << " bytes): " << buffer << endl;

        // TODO: For screen sharing - decode/decompress/render frame here
        // Example: JPEG decode -> QImage -> display on Qt widget
    }

    // Connection handling after loop exit
    if (recvSize == 0)
    {
        cout << "Connection closed by client" << endl;
    }
    else
    {
        cerr << "Receive failed" << endl;
    }

    // Cleanup sockets
    close(clientSocket); // Close client connection
    close(serverSocket); // Close server socket
    cleanupSockets();    // Final networking cleanup
    return 0;            // Success
}
