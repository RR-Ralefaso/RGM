#include <iostream> // For cin, cout, cerr - console I/O
using namespace std;
#include <cstring> // For strlen(), memset() - string/memory operations
#include <string>   // For std::string operations
#include <thread>   // For threading support (cross-platform compatibility)
#include <chrono>   // For timing functions (not used but included for future)
#include <fcntl.h>  // For file control (non-blocking I/O setup)

#ifdef _WIN32                      // === WINDOWS-SPECIFIC HEADERS ===
#include <winsock2.h>              // Windows socket API
#include <conio.h>                 // For _getch() - single key input without Enter
#pragma comment(lib, "ws2_32.lib") // Link Windows socket library
#else                              // === LINUX/MACOS HEADERS ===
#include <sys/socket.h>            // Socket functions: socket(), connect(), send(), recv()
#include <arpa/inet.h>             // Network functions: inet_addr(), htons()
#include <unistd.h>                // Unix functions: close()
#include <termios.h>               // Terminal I/O settings for raw input mode
#endif

#define SERVER_IP "127.0.0.1" // Localhost - receiver on same machine
#define PORT 8080             // Standard TCP port for our connection

// Initialize networking layer (Windows ONLY - Linux has it built-in)
void initSockets()
{
#ifdef _WIN32
    WSADATA wsaData;               // Stores Winsock DLL version info
    if (WSAStartup(MAKEWORD(2, 2), // Request Winsock version 2.2
                   &wsaData) != 0)
    { // Initialize networking stack
        cerr << "WSAStartup failed - cannot initialize networking" << endl;
        exit(1); // Fatal error - no networking possible
    }
#endif
}

// Cleanup networking layer (Windows ONLY)
void cleanupSockets()
{
#ifdef _WIN32
    WSACleanup(); // Properly shutdown Winsock DLL
#endif
}

// CRITICAL: Cross-platform function to read SINGLE KEY without Enter
// Windows: Uses _getch() from conio.h
// Linux: Temporarily disables canonical mode (line buffering)
char getch()
{
#ifdef _WIN32
    char c = _getch(); // Windows: Direct console input, no Enter needed
    if (c == 13)
        c = '\n';      // Convert Windows Enter (13) to Unix newline (10)
    std::cout << c;    // Echo locally so user sees what they typed
    std::cout.flush(); // Force immediate display
    return c;
#else
    // Linux/Mac: Switch terminal to RAW mode temporarily
    struct termios oldt, newt;               // Terminal attribute structures
    tcgetattr(STDIN_FILENO, &oldt);          // Save current terminal settings
    newt = oldt;                             // Copy current settings
    newt.c_lflag &= ~(ICANON | ECHO);        // Disable line buffering + echo
    tcsetattr(STDIN_FILENO, TCSANOW, &newt); // Apply raw mode IMMEDIATELY

    char c = getchar();                      // Read single character directly
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt); // Restore normal terminal mode
    return c;
#endif
}

int main()
{
    initSockets(); // Initialize networking (Windows only)

    // === CREATE CLIENT SOCKET ===
    int clientsocket = socket(AF_INET, SOCK_STREAM, 0); // TCP IPv4 socket
    if (clientsocket < 0)
    {
        cerr << "Socket creation failed" << endl;
        cleanupSockets();
        return -1;
    }

    // === SETUP SERVER ADDRESS ===
    sockaddr_in serverAddr;                            // IPv4 address structure
    memset(&serverAddr, 0, sizeof(serverAddr));        // Zero out structure
    serverAddr.sin_family = AF_INET;                   // IPv4 protocol family
    serverAddr.sin_port = htons(PORT);                 // Convert port to network byte order
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP); // IP string → binary

    // === CONNECT TO RECEIVER ===
    if (connect(clientsocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        cerr << "Connection failed - make sure receiver is running first!" << endl;
#ifdef _WIN32
        closesocket(clientsocket);
#else
        close(clientsocket);
#endif
        cleanupSockets();
        return -1;
    }

    cout << "🎉 LIVE TYPING ACTIVE! Type ANY key - appears instantly on receiver!" << endl;
    cout << "💡 Type 'q' then Enter to quit" << endl
         << endl;

    // === INFINITE LIVE TYPING LOOP ===
    while (true)
    {
        char c = getch(); // ⚡ Get character IMMEDIATELY (NO ENTER needed)

        // Handle Enter key
        if (c == '\n')
        {
            cout << endl; // New line locally
            continue;
        }

        // Quit condition (simple 'q' detection)
        if (c == 'q' && (std::cin.peek() == '\n' || std::cin.peek() == EOF))
        {
            send(clientsocket, ":q\n", 3, 0); // Send quit signal
            cout << endl
                 << "Sent quit signal..." << endl;
            break;
        }

        // CRITICAL: Send SINGLE CHARACTER IMMEDIATELY to receiver
        send(clientsocket, &c, 1, 0); // 1 byte transmission - instant!
    }

    // === CLEANUP ===
#ifdef _WIN32
    closesocket(clientsocket); // Windows socket close
#else
    close(clientsocket); // Unix socket close
#endif
    cleanupSockets();
    cout << "Disconnected cleanly." << endl;
    return 0;
}
