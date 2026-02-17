#include <iostream> // For cin, cout, cerr - console I/O
using namespace std;
#include <cstring> // For strlen(), memset() - string/memory operations
#include <string>   // For std::string operations
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
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        cerr << "WSAStartup failed - cannot initialize networking" << endl;
        exit(1);
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

// ✅ FIXED: Cross-platform SINGLE KEY input WITH LOCAL ECHO
char getch()
{
#ifdef _WIN32
    char c = _getch(); // Get key without Enter
    cout << c;    // ✅ ECHO LOCALLY - user sees what they type!
    cout.flush(); // Force immediate display
    if (c == 13)
        c = '\n'; // Convert Windows Enter (13) to Unix (10)
    return c;
#else
    // Linux: Raw mode for instant key capture + echo
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt); // Save current settings
    newt = oldt;
    newt.c_lflag &= ~(ICANON); // Disable line buffering (KEEP ECHO)
    newt.c_lflag |= ECHO;      // ✅ ENSURE ECHO IS ENABLED
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    char c = getchar();                      // Read single character
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt); // Restore settings
    return c;
#endif
}

int main()
{
    initSockets(); // Initialize networking

    // Create client socket
    int clientsocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientsocket < 0)
    {
        cerr << "Socket creation failed" << endl;
        cleanupSockets();
        return -1;
    }

    // Server address setup
    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP);

    // Connect to receiver
    if (connect(clientsocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        cerr << "Connection failed - start receiver first!" << endl;
#ifdef _WIN32
        closesocket(clientsocket);
#else
        close(clientsocket);
#endif
        cleanupSockets();
        return -1;
    }

    cout << "🎉 LIVE TYPING ACTIVE!" << endl;
    cout << "💡 You WILL see what you type locally + receiver sees instantly!" << endl;
    cout << "💡 Press 'q' then Enter to quit" << endl
         << endl;

    // 🔥 LIVE TYPING LOOP WITH LOCAL ECHO
    while (true)
    {
        char c = getch(); // ⚡ Get key + ECHO LOCALLY AUTOMATICALLY

        // Handle special keys
        if (c == '\n')
        { // Enter pressed
            cout << endl;
            continue;
        }

        // Simple quit (q + Enter)
        if (c == 'q')
        {
            char next = getch();
            if (next == '\n')
            {
                send(clientsocket, ":q\n", 3, 0);
                cout << endl
                     << "Quit signal sent!" << endl;
                break;
            }
            else
            {
                send(clientsocket, &c, 1, 0); // Send 'q' character
                cout << next;                 // Echo next char locally
                continue;
            }
        }

        // 🚀 Send to receiver INSTANTLY (user already sees it locally)
        send(clientsocket, &c, 1, 0);
    }

    // Cleanup
#ifdef _WIN32
    closesocket(clientsocket);
#else
    close(clientsocket);
#endif
    cleanupSockets();
    cout << "Disconnected cleanly." << endl;
    return 0;
}
