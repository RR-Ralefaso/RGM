#include <iostream> // For cin, cout, cerr
#include <cstring>  // For strlen(), memset()
#include <string>   // For std::string and getline()
using namespace std;

#ifdef _WIN32 // Windows-specific socket headers
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib") // Link Windows Socket library
#else                              // Linux/macOS socket headers
#include <sys/socket.h>            // Socket functions (socket(), connect())
#include <arpa/inet.h>             // IP address functions (inet_addr(), htons())
#include <unistd.h>                // close() function
#endif

#define SERVER_IP "127.0.0.1" // Localhost - receiver runs on same machine
#define PORT 8080             // TCP port to connect to

// Initialize Winsock (Windows only - Linux doesn't need this)
void initSockets()
{
#ifdef _WIN32
    WSADATA wsaData; // Stores Winsock version info
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    { // Init networking version 2.2
        cerr << "WSAStartup failed" << endl;
        exit(1); // Fatal error - can't network without this
    }
#endif
}

// Cleanup Winsock (Windows only)
void cleanupsockets()
{
#ifdef _WIN32
    WSACleanup(); // Shutdown Winsock properly
#endif
}

int main()
{
    initSockets(); // Setup networking layer

    // Create TCP IPv4 socket for client
    int clientsocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientsocket < 0)
    { // Error creating socket
        cerr << "socket creation failed" << endl;
        cleanupsockets();
        return -1;
    }

    // Setup server address structure
    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));        // Zero out structure
    serverAddr.sin_family = AF_INET;                   // IPv4 protocol
    serverAddr.sin_port = htons(PORT);                 // Host-to-network byte order (big-endian)
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP); // Convert IP string to binary

    // Connect to receiver server
    if (connect(clientsocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        cerr << "connection failed - is receiver running?" << endl;
#ifdef _WIN32
        closesocket(clientsocket);
#else
        close(clientsocket);
#endif
        cleanupsockets();
        return -1;
    }
    cout << "Connected! Type messages (':q' to quit):" << endl;

    // Main interactive loop - stays connected until :q
    string input;
    while (getline(cin, input))
    { // Read full lines from stdin
        if (input == ":q")
            break; // Quit command

        input += '\0'; // Null-terminate for safe string handling
        int bytesSent = send(clientsocket, input.c_str(), input.length(), 0);
        if (bytesSent > 0)
        {
            // Show echo without null terminator
            cout << "Sent (" << bytesSent << " bytes): "
                 << input.substr(0, input.length() - 1) << endl;
        }
        else
        {
            cerr << "Send failed - connection lost?" << endl;
            break;
        }
    }

    // Cleanup connection
#ifdef _WIN32
    closesocket(clientsocket);
#else
    close(clientsocket);
#endif
    cleanupsockets();
    cout << "Disconnected." << endl;
    return 0;
}
