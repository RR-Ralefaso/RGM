#include <iostream>  // Standard I/O for console output
#include <cstring>   // For memset() and strlen()
using namespace std; // Use standard namespace

#ifdef _WIN32 // Windows-specific socket headers
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib") // Link Windows Socket library
#else                              // Linux/macOS socket headers
#include <sys/socket.h>            // Socket API functions
#include <arpa/inet.h>             // IP address conversion functions
#include <unistd.h>                // For close() function
#endif

#define SERVER_IP "127.0.0.1" // Localhost (same machine) - change to target IP
#define PORT 8080             // TCP port for communication

// Initialize Winsock on Windows (Linux/macOS don't need this)
void initSockets()
{
#ifdef _WIN32
    WSADATA wsaData; // Data structure for Winsock version info
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        cerr << "WSAStartup failed"; // Initialize networking failed
        exit(1);                     // Exit program on failure
    }
#endif
}

// Cleanup Winsock on Windows only
void cleanupsockets()
{
#ifdef _WIN32
    WSACleanup(); // Shutdown Winsock
#endif
}

int main()
{
    initSockets(); // Setup networking

    // Create TCP IPv4 socket
    int clientsocket;
#ifdef _WIN32
    clientsocket = socket(AF_INET, SOCK_STREAM, 0); // Windows socket creation
#else
    clientsocket = socket(AF_INET, SOCK_STREAM, 0); // Linux/macOS socket creation
#endif

    if (clientsocket < 0)
    { // Check if socket created successfully
        cerr << "socket creation failed" << endl;
        cleanupsockets(); // Cleanup before exit
        return -1;        // Exit with error
    }

    // Server address structure setup
    sockaddr_in serverAddr;                            // IPv4 address structure
    memset(&serverAddr, 0, sizeof(serverAddr));        // Zero out structure
    serverAddr.sin_family = AF_INET;                   // Use IPv4 protocol
    serverAddr.sin_port = htons(PORT);                 // Convert port to network byte order
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP); // Convert IP string to binary

    // Attempt connection to server
    if (connect(clientsocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        cerr << "connection failed" << endl; // Connection refused/timeout
        close(clientsocket);                 // Close socket
        cleanupsockets();                    // Cleanup networking
        return -1;                           // Exit with error
    }
    cout << "Connected to server successfully!" << endl;

    // Send test message
    const char *message = "greetings from sender";
    int bytesSent = send(clientsocket, message, strlen(message), 0); // Send data
    if (bytesSent > 0)
    {
        cout << "Message sent (" << bytesSent << " bytes): " << message << endl;
    }
    else
    {
        cout << "Send failed!" << endl;
    }

    // TODO: For screen sharing - replace this section with capture/compress/send loop
    // while(true) {
    //     // 1. Capture screen frame
    //     // 2. Compress (JPEG/H.264)
    //     // 3. Send binary data
    //     // sleep(33); // ~30 FPS
    // }

    close(clientsocket); // Close connection
#ifdef _WIN32
    closesocket(clientsocket); // Windows socket close (already handled by close macro)
#endif
    cleanupsockets(); // Final cleanup
    return 0;         // Success
}
