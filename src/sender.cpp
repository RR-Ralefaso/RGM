/**
 * SENDER.CPP - IMPROVED VERSION with TCP_NODELAY fix
 */
#include <iostream>
#include <cstring>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <iomanip>
#include "discover.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mstcpip.h> // For TCP_NODELAY on Windows
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h> // IMPORTANT: For TCP_NODELAY on Linux/Unix
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720
#define TARGET_FPS 10
#define BYTES_PER_PIXEL 3
#define CONNECTION_TIMEOUT_MS 5000

// Global flag for running state
std::atomic<bool> g_running{true};

/**
 * Initialize sockets (Windows only)
 */
bool initSockets()
{
#ifdef _WIN32
    WSADATA wsa;
    int result = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (result != 0)
    {
        std::cerr << "❌ WSAStartup failed: " << result << std::endl;
        return false;
    }
#endif
    return true;
}

/**
 * Cleanup sockets
 */
void cleanupSockets()
{
#ifdef _WIN32
    WSACleanup();
#endif
}

/**
 * Network socket class with improved error handling
 */
class NetworkSocket
{
private:
#ifdef _WIN32
    SOCKET sock;
#else
    int sock;
#endif

public:
    NetworkSocket() : sock(-1) {}

    ~NetworkSocket()
    {
        close();
    }

    bool isValid() const
    {
#ifdef _WIN32
        return sock != INVALID_SOCKET;
#else
        return sock >= 0;
#endif
    }

    void close()
    {
        if (isValid())
        {
#ifdef _WIN32
            closesocket(sock);
            sock = INVALID_SOCKET;
#else
            ::close(sock);
            sock = -1;
#endif
        }
    }

    bool create()
    {
        close();

#ifdef _WIN32
        sock = socket(AF_INET, SOCK_STREAM, 0);
        return sock != INVALID_SOCKET;
#else
        sock = socket(AF_INET, SOCK_STREAM, 0);
        return sock >= 0;
#endif
    }

    bool connect(const std::string &ip, int port, int timeout_ms = CONNECTION_TIMEOUT_MS)
    {
        if (!create())
        {
            std::cerr << "❌ Failed to create socket" << std::endl;
            return false;
        }

        // Prepare address
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);

        if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0)
        {
            std::cerr << "❌ Invalid IP address: " << ip << std::endl;
            return false;
        }

        // Set non-blocking for timeout
#ifdef _WIN32
        u_long mode = 1;
        ioctlsocket(sock, FIONBIO, &mode);
#else
        int flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif

        // Start connection
        int result = ::connect(sock, (struct sockaddr *)&addr, sizeof(addr));

        bool connected = false;

#ifdef _WIN32
        if (result == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK)
#else
        if (result < 0 && errno == EINPROGRESS)
#endif
        {
            // Wait for connection with timeout
            fd_set fdset;
            FD_ZERO(&fdset);
            FD_SET(sock, &fdset);

            struct timeval tv;
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;

            if (select(sock + 1, NULL, &fdset, NULL, &tv) == 1)
            {
                int so_error;
                socklen_t len = sizeof(so_error);
                getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&so_error, &len);

                if (so_error == 0)
                    connected = true;
            }
        }

        // Set back to blocking mode
#ifdef _WIN32
        mode = 0;
        ioctlsocket(sock, FIONBIO, &mode);
#else
        fcntl(sock, F_SETFL, flags);
#endif

        if (!connected)
        {
            std::cerr << "❌ Connection timeout to " << ip << ":" << port << std::endl;
            close();
            return false;
        }

        // Set TCP_NODELAY to disable Nagle's algorithm (reduces latency)
        int nodelay = 1;
        if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *)&nodelay, sizeof(nodelay)) < 0)
        {
            std::cerr << "⚠️  Warning: Could not set TCP_NODELAY" << std::endl;
            // Non-fatal error, continue anyway
        }

        std::cout << "✅ Connected to " << ip << ":" << port << std::endl;
        return true;
    }

    bool sendAll(const void *data, size_t size)
    {
        const char *buffer = (const char *)data;
        size_t total_sent = 0;

        while (total_sent < size && g_running)
        {
#ifdef _WIN32
            int sent = ::send(sock, buffer + total_sent, size - total_sent, 0);
            if (sent == SOCKET_ERROR)
            {
                std::cerr << "❌ Send error: " << WSAGetLastError() << std::endl;
                return false;
            }
#else
            int sent = ::send(sock, buffer + total_sent, size - total_sent, 0);
            if (sent < 0)
            {
                std::cerr << "❌ Send error: " << strerror(errno) << std::endl;
                return false;
            }
#endif
            if (sent == 0)
            {
                std::cerr << "❌ Connection closed by receiver" << std::endl;
                return false;
            }

            total_sent += sent;
        }

        return total_sent == size;
    }
};

#ifdef _WIN32
/**
 * Windows screen capture
 */
std::vector<uint8_t> captureScreen()
{
    std::vector<uint8_t> pixels(SCREEN_WIDTH * SCREEN_HEIGHT * BYTES_PER_PIXEL, 0);

    HDC screen_dc = GetDC(NULL);
    if (!screen_dc)
    {
        std::cerr << "❌ Failed to get screen DC" << std::endl;
        return pixels;
    }

    HDC mem_dc = CreateCompatibleDC(screen_dc);
    HBITMAP bitmap = CreateCompatibleBitmap(screen_dc, SCREEN_WIDTH, SCREEN_HEIGHT);

    if (!bitmap)
    {
        std::cerr << "❌ Failed to create bitmap" << std::endl;
        ReleaseDC(NULL, screen_dc);
        DeleteDC(mem_dc);
        return pixels;
    }

    SelectObject(mem_dc, bitmap);

    // Capture screen
    BitBlt(mem_dc, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, screen_dc, 0, 0, SRCCOPY);

    // Get bitmap bits
    BITMAPINFOHEADER bi = {0};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = SCREEN_WIDTH;
    bi.biHeight = -SCREEN_HEIGHT; // Negative to flip vertically
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;

    int result = GetDIBits(mem_dc, bitmap, 0, SCREEN_HEIGHT,
                           pixels.data(), (BITMAPINFO *)&bi, DIB_RGB_COLORS);

    if (!result)
    {
        std::cerr << "❌ Failed to get bitmap bits" << std::endl;
    }

    // Cleanup
    DeleteObject(bitmap);
    DeleteDC(mem_dc);
    ReleaseDC(NULL, screen_dc);

    return pixels;
}
#else
/**
 * Linux X11 screen capture
 */
std::vector<uint8_t> captureScreen()
{
    std::vector<uint8_t> pixels(SCREEN_WIDTH * SCREEN_HEIGHT * BYTES_PER_PIXEL, 0);

    // Open display
    Display *display = XOpenDisplay(NULL);
    if (!display)
    {
        std::cerr << "❌ Failed to open X display. Make sure you're running in a GUI environment." << std::endl;
        return pixels;
    }

    // Get default screen
    int screen_num = DefaultScreen(display);
    Window root = RootWindow(display, screen_num);

    // Get screen dimensions (use actual screen size)
    int actual_width = DisplayWidth(display, screen_num);
    int actual_height = DisplayHeight(display, screen_num);

    // If actual screen is smaller than our target, adjust
    int capture_width = std::min(SCREEN_WIDTH, actual_width);
    int capture_height = std::min(SCREEN_HEIGHT, actual_height);

    // Get screen image
    XImage *image = XGetImage(display, root, 0, 0,
                              capture_width, capture_height,
                              AllPlanes, ZPixmap);

    if (!image)
    {
        std::cerr << "❌ Failed to capture screen" << std::endl;
        XCloseDisplay(display);
        return pixels;
    }

    // Convert to RGB24
    for (int y = 0; y < capture_height; y++)
    {
        for (int x = 0; x < capture_width; x++)
        {
            unsigned long pixel = XGetPixel(image, x, y);
            size_t index = (y * SCREEN_WIDTH + x) * BYTES_PER_PIXEL;

            // Extract RGB components (X11 uses 0xRRGGBB format typically)
            pixels[index + 0] = (pixel >> 16) & 0xFF; // Red
            pixels[index + 1] = (pixel >> 8) & 0xFF;  // Green
            pixels[index + 2] = pixel & 0xFF;         // Blue
        }
    }

    XDestroyImage(image);
    XCloseDisplay(display);

    return pixels;
}
#endif

/**
 * Calculate and display streaming statistics
 */
void showStats(int frames_sent, int elapsed_seconds, size_t bytes_sent)
{
    if (elapsed_seconds == 0)
        elapsed_seconds = 1;

    float fps = frames_sent / (float)elapsed_seconds;
    float mbps = (bytes_sent / (1024.0f * 1024.0f)) / elapsed_seconds;

    std::cout << "📊 Frames: " << frames_sent
              << " | FPS: " << std::fixed << std::setprecision(1) << fps
              << " | Bandwidth: " << std::setprecision(2) << mbps << " MB/s" << std::endl;
}

int main()
{
    std::cout << "========================================" << std::endl;
    std::cout << "🎥 SCREEN SHARE SENDER v2.0" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Resolution: " << SCREEN_WIDTH << "x" << SCREEN_HEIGHT << std::endl;
    std::cout << "Target FPS: " << TARGET_FPS << std::endl;
    std::cout << "========================================" << std::endl;

    // Initialize sockets
    if (!initSockets())
    {
        std::cerr << "❌ Failed to initialize sockets" << std::endl;
        return 1;
    }

    // Discover receivers
    std::cout << "🔍 Discovering receivers..." << std::endl;
    auto receivers = discoverReceivers(5); // 5 second timeout

    if (receivers.empty())
    {
        std::cerr << "❌ No receivers found!" << std::endl;
        std::cerr << "   Make sure receiver is running on the same network." << std::endl;
        std::cerr << "   Check firewall settings (UDP 1900, TCP 8081)." << std::endl;
        cleanupSockets();
        return 1;
    }

    // Display discovered receivers
    std::cout << listDevices(receivers);

    // Let user select a receiver
    size_t choice;
    std::cout << "Select receiver (0-" << receivers.size() - 1 << "): ";
    std::cin >> choice;

    if (choice >= receivers.size())
    {
        std::cerr << "❌ Invalid selection" << std::endl;
        cleanupSockets();
        return 1;
    }

    const auto &selected = receivers[choice];
    std::cout << "🎯 Selected: " << selected.toString() << std::endl;

    // Connect to receiver
    std::cout << "🔌 Connecting to receiver..." << std::endl;

    NetworkSocket connection;
    if (!connection.connect(selected.ip_address, selected.tcp_port))
    {
        std::cerr << "❌ Failed to connect to receiver" << std::endl;
        std::cerr << "   Check if receiver is running and firewall allows TCP port 8081." << std::endl;
        cleanupSockets();
        return 1;
    }

    std::cout << "🎬 Starting stream..." << std::endl;
    std::cout << "   Press Ctrl+C to stop" << std::endl;

    // Streaming loop
    auto last_time = std::chrono::steady_clock::now();
    auto stats_time = last_time;
    int frames_sent = 0;
    size_t total_bytes = 0;
    bool streaming = true;

    // Frame timing for constant FPS
    const auto frame_duration = std::chrono::milliseconds(1000 / TARGET_FPS);

    while (streaming && g_running)
    {
        auto frame_start = std::chrono::steady_clock::now();

        // Capture screen
        auto frame = captureScreen();

        // Check if capture was successful (non-zero pixels)
        bool capture_valid = false;
        for (size_t i = 0; i < frame.size() && !capture_valid; i += 1000)
        {
            if (frame[i] != 0)
                capture_valid = true;
        }

        if (!capture_valid)
        {
            std::cerr << "⚠️  Screen capture may be invalid (all zeros)" << std::endl;
        }

        // Send frame size (network byte order)
        uint32_t frame_size = frame.size();
        uint32_t net_frame_size = htonl(frame_size);

        if (!connection.sendAll(&net_frame_size, 4))
        {
            std::cerr << "❌ Failed to send frame size" << std::endl;
            break;
        }

        // Send frame data
        if (!connection.sendAll(frame.data(), frame.size()))
        {
            std::cerr << "❌ Failed to send frame data" << std::endl;
            break;
        }

        frames_sent++;
        total_bytes += 4 + frame_size;

        // Show stats every 5 seconds
        auto now = std::chrono::steady_clock::now();
        auto stats_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                                 now - stats_time)
                                 .count();

        if (stats_elapsed >= 5)
        {
            showStats(frames_sent, stats_elapsed, total_bytes);
            stats_time = now;
        }

        // Maintain constant FPS
        auto frame_end = std::chrono::steady_clock::now();
        auto frame_time = frame_end - frame_start;

        if (frame_time < frame_duration)
        {
            std::this_thread::sleep_for(frame_duration - frame_time);
        }
    }

    // Final stats
    auto end_time = std::chrono::steady_clock::now();
    auto total_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                             end_time - last_time)
                             .count();

    std::cout << "========================================" << std::endl;
    std::cout << "📊 STREAMING STATISTICS" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Frames sent:     " << frames_sent << std::endl;
    std::cout << "Duration:        " << total_seconds << " seconds" << std::endl;
    if (total_seconds > 0)
    {
        std::cout << "Average FPS:     " << (frames_sent / total_seconds) << std::endl;
        std::cout << "Total data:      " << (total_bytes / (1024.0 * 1024.0)) << " MB" << std::endl;
    }
    std::cout << "========================================" << std::endl;

    cleanupSockets();
    return 0;
}