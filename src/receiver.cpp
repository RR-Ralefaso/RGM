#include <iostream> // Console I/O
#include <cstring>  // Memory/string operations
#include <string>   // String handling
using namespace std;

#ifdef _WIN32 // Windows headers
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#else                   // Linux/Mac headers
#include <sys/socket.h> // Core socket functions
#include <netinet/in.h> // sockaddr_in structure
#include <arpa/inet.h>  // IP conversion functions
#include <unistd.h>     // close() function
#endif

#define PORT 8080 // Port to listen on

// Initialize networking (Windows only)
void initSockets()
{
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        cerr << "WSAStartup failed" << endl;
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
    initSockets(); // Setup networking

    // === CREATE SERVER SOCKET ===
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0); // TCP server socket
    if (serverSocket < 0)
    {
        cerr << "Server socket creation failed" << endl;
        cleanupSockets();
        return -1;
    }

    // === BIND TO PORT 8080 ===
    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;         // IPv4
    serverAddr.sin_port = htons(PORT);       // Network byte order
    serverAddr.sin_addr.s_addr = INADDR_ANY; // Bind to ALL interfaces (0.0.0.0)

    if (bind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        cerr << "Bind failed - port " << PORT << " already in use?" << endl;
#ifdef _WIN32
        closesocket(serverSocket);
#else
        close(serverSocket);
#endif
        cleanupSockets();
        return -1;
    }
    cout << "✅ Server bound to port " << PORT << endl;

    // === START LISTENING ===
    if (listen(serverSocket, 1) < 0)
    { // Queue 1 connection
        cerr << "Listen failed" << endl;
#ifdef _WIN32
        closesocket(serverSocket);
#else
        close(serverSocket);
#endif
        cleanupSockets();
        return -1;
    }
    cout << "👂 Listening on port " << PORT << "..." << endl;

    // === ACCEPT CLIENT ===
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

    // Show connection details
    cout << "🔗 Client connected: "
         << inet_ntoa(clientAddr.sin_addr) << ":"
         << ntohs(clientAddr.sin_port) << endl;
    cout << "🚀 LIVE RECEIVER ACTIVE - typing appears INSTANTLY!" << endl;
    cout << "==================================================" << endl
         << endl;

    // === LIVE CHARACTER-BY-CHARACTER DISPLAY ===
    char ch; // Single character buffer
    while (true)
    {
        int bytes = recv(clientSocket, &ch, 1, 0); // Read EXACTLY 1 byte

        if (bytes <= 0)
        { // Connection closed or error
            cout << endl
                 << "❌ Client disconnected" << endl;
            break;
        }

        // 🔥 PRINT IMMEDIATELY - NO BUFFERING!
        cout << ch;
        cout.flush(); // FORCE instant screen update
    }

    // === CLEANUP ===
    cout << endl
         << "Shutting down server..." << endl;
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
