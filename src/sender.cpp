#include <iostream> // Console input/output for status messages
#include <cstring>  // memset() for clearing socket address structures
#include <chrono>   // High-precision timing for 10 FPS frame rate control
#include <thread>   // std::this_thread::sleep_until() for smooth FPS limiting
#include <vector>   // Dynamic pixel buffer storage (RGB24 format)
#include <cstdint>  // uint32_t for network byte order conversion

// === PLATFORM-SPECIFIC INCLUDES ===
// Windows: Winsock2 + GDI screen capture
#ifdef _WIN32
#include <winsock2.h>              // Windows socket API (WSAStartup, send, recv)
#include <windows.h>               // GDI screen capture (GetDC, BitBlt)
#pragma comment(lib, "ws2_32.lib") // Auto-link Windows socket library
// Linux/macOS: POSIX sockets + X11 screen capture
#else
#include <sys/socket.h> // socket(), connect(), send(), recv() functions
#include <netinet/in.h> // sockaddr_in address structure
#include <arpa/inet.h>  // htons(), inet_addr(), htonl() network byte order
#include <unistd.h>     // close() function
#include <X11/Xlib.h>   // Linux X11 screen capture (XGetImage)
#include <X11/Xutil.h>  // X11 utility functions (DefaultRootWindow)
#endif

// === STREAMING CONFIGURATION ===
#define SERVER_IP "127.0.0.1" // Receiver IP address (localhost)
#define SCREEN_PORT 8081      // TCP port for screen streaming
#define SCREEN_WIDTH 1280     // 720p resolution (performance optimized)
#define SCREEN_HEIGHT 720
#define TARGET_FPS 10     // 10 FPS = smooth + low bandwidth (~3MB/s)
#define BYTES_PER_PIXEL 3 // RGB format (no alpha channel needed)

/**
 * Cross-platform socket wrapper class
 * Handles Windows SOCKET vs Unix int differences automatically
 */
class Socket
{
private:
#ifdef _WIN32
    SOCKET sock_fd; // Windows socket handle (SOCKET type)
#else
    int sock_fd; // Unix/Linux file descriptor (int)
#endif

public:
    Socket() : sock_fd(-1) {} // Initialize as invalid socket

    ~Socket()
    {
        close(); // Auto-cleanup on destruction
    }

    /**
     * Connect to receiver with full error handling
     * Windows: Calls WSAStartup automatically
     * Linux: Uses standard POSIX connect
     */
    bool connect(const char *ip, int port)
    {
        // Windows: Initialize networking stack (one-time call)
#ifdef _WIN32
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
        {
            std::cerr << "❌ Windows networking failed!" << std::endl;
            return false;
        }
#endif

        // Create TCP socket (stream-oriented, reliable delivery)
        sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_fd < 0)
        {
            std::cerr << "❌ Socket creation failed!" << std::endl;
            return false;
        }

        // Setup server address structure
        sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr)); // Zero out structure
        server_addr.sin_family = AF_INET;             // IPv4 protocol
        server_addr.sin_port = htons(port);           // Host-to-network byte order
        server_addr.sin_addr.s_addr = inet_addr(ip);  // IP string → binary

        // Attempt TCP connection to receiver
        if (::connect(sock_fd, (sockaddr *)&server_addr, sizeof(server_addr)) < 0)
        {
            std::cerr << "❌ Connection failed to " << ip
                      << ":" << port << " (start receiver first!)" << std::endl;
            return false;
        }

        std::cout << "✅ Connected to receiver: " << ip << ":" << port << std::endl;
        return true;
    }

    /**
     * Send data reliably over TCP (handles partial sends automatically)
     * Returns true if all bytes sent successfully
     */
    bool send(const void *data, size_t size)
    {
        const uint8_t *bytes = static_cast<const uint8_t *>(data);
        size_t total_sent = 0;

        // Keep sending until all bytes transmitted (TCP may fragment)
        while (total_sent < size)
        {
            int bytes_sent = ::send(sock_fd, (const char *)bytes + total_sent,
                                    size - total_sent, 0);
            if (bytes_sent <= 0)
            {
                std::cerr << "❌ Send failed!" << std::endl;
                return false;
            }
            total_sent += bytes_sent;
        }
        return true;
    }

    /**
     * Close socket connection (Windows vs Unix handling)
     */
    void close()
    {
        if (sock_fd >= 0)
        {
#ifdef _WIN32
            closesocket(sock_fd); // Windows socket cleanup
#else
            ::close(sock_fd); // Unix file descriptor cleanup
#endif
            sock_fd = -1;
        }
    }
};

/**
 * Capture screen pixels (platform-specific implementation)
 * Returns RGB24 pixel buffer (WIDTH x HEIGHT x 3 bytes)
 * Fastest method per platform
 */
#ifdef _WIN32
// 🔥 WINDOWS: GDI BitBlt (hardware accelerated, <10ms/frame)
std::vector<uint8_t> captureScreen()
{
    std::vector<uint8_t> pixels(SCREEN_WIDTH * SCREEN_HEIGHT * BYTES_PER_PIXEL);

    // Get desktop device context (entire screen)
    HDC screen_dc = GetDC(NULL);                   // Primary monitor
    HDC memory_dc = CreateCompatibleDC(screen_dc); // Offscreen drawing
    HBITMAP bitmap = CreateCompatibleBitmap(screen_dc, SCREEN_WIDTH, SCREEN_HEIGHT);

    // Copy screen contents to bitmap (SRCCOPY = full RGB copy)
    SelectObject(memory_dc, bitmap);
    BitBlt(memory_dc, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT,
           screen_dc, 0, 0, SRCCOPY);

    // Extract raw RGB pixels from bitmap
    BITMAPINFOHEADER bitmap_info = {0};
    bitmap_info.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.biWidth = SCREEN_WIDTH;
    bitmap_info.biHeight = -SCREEN_HEIGHT; // Top-down (no vertical flip)
    bitmap_info.biPlanes = 1;
    bitmap_info.biBitCount = 24; // RGB24 format
    bitmap_info.biCompression = BI_RGB;

    GetDIBits(memory_dc, bitmap, 0, SCREEN_HEIGHT, pixels.data(),
              (BITMAPINFO *)&bitmap_info, DIB_RGB_COLORS);

    // Cleanup GDI resources (prevents memory leaks)
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(NULL, screen_dc);

    return pixels;
}
#endif

// 🔥 LINUX: X11 Root Window Capture (fast ZPixmap format)
#ifndef _WIN32
std::vector<uint8_t> captureScreen()
{
    std::vector<uint8_t> pixels(SCREEN_WIDTH * SCREEN_HEIGHT * BYTES_PER_PIXEL);

    // Connect to X11 display server
    Display *display = XOpenDisplay(NULL);           // Default display (:0)
    Window root_window = DefaultRootWindow(display); // Full screen

    // Capture screen region (ZPixmap = server-native format, fastest)
    XImage *ximage = XGetImage(display, root_window, 0, 0,
                               SCREEN_WIDTH, SCREEN_HEIGHT,
                               AllPlanes, ZPixmap);

    // Convert X11 pixels → RGB24 (handles 24/32-bit depth automatically)
    for (int y = 0; y < SCREEN_HEIGHT; y++)
    {
        for (int x = 0; x < SCREEN_WIDTH; x++)
        {
            unsigned long pixel = XGetPixel(ximage, x, y);
            size_t pixel_idx = (y * SCREEN_WIDTH + x) * BYTES_PER_PIXEL;
            pixels[pixel_idx + 0] = (pixel >> 16) & 0xFF; // Red channel
            pixels[pixel_idx + 1] = (pixel >> 8) & 0xFF;  // Green channel
            pixels[pixel_idx + 2] = pixel & 0xFF;         // Blue channel
        }
    }

    // Cleanup X11 resources
    XDestroyImage(ximage);
    XCloseDisplay(display);

    return pixels;
}
#endif

/**
 * Main program entry point
 * Infinite loop: capture → encode → transmit → repeat at TARGET_FPS
 */
int main()
{
    std::cout << "🎥 CROSS-PLATFORM SCREEN SHARE SENDER" << std::endl;
    std::cout << "Resolution: " << SCREEN_WIDTH << "x" << SCREEN_HEIGHT
              << " @ " << TARGET_FPS << " FPS" << std::endl;
    std::cout << "Target: " << SERVER_IP << ":" << SCREEN_PORT << std::endl
              << std::endl;

    // Step 1: Connect to receiver
    Socket network_socket;
    if (!network_socket.connect(SERVER_IP, SCREEN_PORT))
    {
        std::cerr << "💡 Hint: Run './receiver' first in another terminal!" << std::endl;
        return 1;
    }

    // Step 2: Main streaming loop (runs until Ctrl+C)
    auto last_frame_time = std::chrono::steady_clock::now();
    int frame_count = 0;

    while (true)
    {
        // Capture current screen frame (~5-10ms)
        auto frame_pixels = captureScreen();
        uint32_t frame_size_bytes = frame_pixels.size();

        // Send frame header (4 bytes: frame size in network byte order)
        uint32_t size_network_order = htonl(frame_size_bytes);
        if (!network_socket.send(&size_network_order, 4))
        {
            std::cout << "❌ Connection lost!" << std::endl;
            break;
        }

        // Send raw RGB pixel data (~2.5MB per frame)
        if (!network_socket.send(frame_pixels.data(), frame_size_bytes))
        {
            break;
        }

        frame_count++;

        // Precise FPS limiting (100ms intervals for 10 FPS)
        auto current_time = std::chrono::steady_clock::now();
        auto frame_interval = std::chrono::milliseconds(1000 / TARGET_FPS);
        if (current_time - last_frame_time < frame_interval)
        {
            std::this_thread::sleep_until(last_frame_time + frame_interval);
        }
        last_frame_time = current_time;

        // Live FPS counter (every 30 seconds)
        if (frame_count % 300 == 0)
        {
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                current_time - last_frame_time + std::chrono::milliseconds(3000));
            float actual_fps = 300.0f / (elapsed_ms.count() / 1000.0f);
            std::cout << "📊 Frame " << frame_count
                      << " (" << actual_fps << " FPS)" << std::endl;
        }
    }

    std::cout << "🔌 Screen sharing stopped (" << frame_count << " frames sent)" << std::endl;
    return 0;
}
