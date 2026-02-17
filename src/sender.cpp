#include <iostream> // Console output and status messages
#include <cstring>  // Memory operations (memset for socket addresses)
#include <chrono>   // Precise FPS timing control (10 FPS streaming)
#include <thread>   // Thread sleep for frame rate limiting
#include <vector>   // Dynamic pixel buffer storage
#include <cstdint>  // Fixed-width types (uint32_t for network bytes)

#ifdef _WIN32                      // === WINDOWS PLATFORM ===
#include <winsock2.h>              // Windows socket API (WSAStartup, send, recv)
#include <windows.h>               // GDI screen capture functions (BitBlt, GetDC)
#pragma comment(lib, "ws2_32.lib") // Auto-link Windows socket library
#define PLATFORM_WINDOWS
#elif defined(__APPLE__) // === APPLE PLATFORMS (macOS/iOS) ===
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE // iOS detection
#define PLATFORM_IOS
#import <UIKit/UIKit.h> // iOS window snapshot capture
#else                   // macOS detection
#define PLATFORM_MACOS
#include <ApplicationServices/ApplicationServices.h> // CoreGraphics
#endif
#elif defined(__ANDROID__) // === ANDROID ===
#define PLATFORM_ANDROID
#include <android/native_window.h> // Native window placeholder
#elif defined(__linux__)           // === LINUX ===
#define PLATFORM_LINUX
#include <X11/Xlib.h>  // X11 screen capture
#include <X11/Xutil.h> // X11 utility functions
#else
#error "Unsupported platform! Use Windows, macOS, Linux, iOS, or Android."
#endif

// === STREAMING CONFIGURATION ===
#define SERVER_IP "127.0.0.1" // Receiver IP (localhost for testing)
#define SCREEN_PORT 8081      // TCP port for video stream
#define SCREEN_WIDTH 1280     // 720p resolution (fast streaming)
#define SCREEN_HEIGHT 720
#define TARGET_FPS 10     // 10 FPS = smooth but low bandwidth
#define BYTES_PER_PIXEL 3 // RGB format (no alpha channel)

// ========================================
// CROSS-PLATFORM NETWORKING WRAPPER CLASS
// ========================================
class NetworkSocket
{
private:
#ifdef PLATFORM_WINDOWS
    SOCKET socket_fd; // Windows socket handle
#else
    int socket_fd; // Unix/Linux file descriptor
#endif

public:
    NetworkSocket() : socket_fd(-1) {}
    ~NetworkSocket() { disconnect(); }

    // Connect to receiver with full error handling
    bool connect(const char *ip, uint16_t port)
    {
        // Windows: Initialize networking stack
#ifdef PLATFORM_WINDOWS
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        {
            std::cerr << "❌ Windows networking initialization failed!" << std::endl;
            return false;
        }
#endif

        // Create TCP socket
        socket_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_fd < 0)
        {
            std::cerr << "❌ Failed to create socket!" << std::endl;
            return false;
        }

        // Setup receiver address structure
        sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        server_addr.sin_addr.s_addr = inet_addr(ip);

        // Attempt TCP connection
        if (::connect(socket_fd, (sockaddr *)&server_addr, sizeof(server_addr)) < 0)
        {
            std::cerr << "❌ Connection failed to " << ip << ":" << port
                      << " (run receiver first!)" << std::endl;
            return false;
        }

        std::cout << "✅ Connected to receiver at " << ip << ":" << port << std::endl;
        return true;
    }

    // Reliable TCP send (guaranteed delivery)
    bool sendData(const void *data, size_t size)
    {
        const uint8_t *bytes = static_cast<const uint8_t *>(data);
        size_t total_sent = 0;

        // Send all bytes (TCP may split into multiple packets)
        while (total_sent < size)
        {
            int sent = ::send(socket_fd, (const char *)bytes + total_sent,
                              size - total_sent, 0);
            if (sent <= 0)
            {
                std::cerr << "❌ Send failed!" << std::endl;
                return false;
            }
            total_sent += sent;
        }
        return true;
    }

    void disconnect()
    {
        if (socket_fd >= 0)
        {
#ifdef PLATFORM_WINDOWS
            closesocket(socket_fd);
            WSACleanup();
#else
            ::close(socket_fd);
#endif
            socket_fd = -1;
        }
    }
};

// ========================================
// PLATFORM-SPECIFIC SCREEN CAPTURE
// ========================================

// 🔥 WINDOWS: Fastest method using GDI BitBlt
#ifdef PLATFORM_WINDOWS
std::vector<uint8_t> captureScreen()
{
    std::vector<uint8_t> pixels(SCREEN_WIDTH * SCREEN_HEIGHT * BYTES_PER_PIXEL);

    // Get entire desktop device context
    HDC desktop_dc = GetDC(NULL);
    HDC memory_dc = CreateCompatibleDC(desktop_dc);
    HBITMAP bitmap = CreateCompatibleBitmap(desktop_dc, SCREEN_WIDTH, SCREEN_HEIGHT);

    // Copy screen pixels to memory bitmap
    SelectObject(memory_dc, bitmap);
    BitBlt(memory_dc, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT,
           desktop_dc, 0, 0, SRCCOPY);

    // Extract raw RGB pixels from bitmap
    BITMAPINFOHEADER bmi = {0};
    bmi.biSize = sizeof(BITMAPINFOHEADER);
    bmi.biWidth = SCREEN_WIDTH;
    bmi.biHeight = -SCREEN_HEIGHT; // Negative = top-down (no flip)
    bmi.biPlanes = 1;
    bmi.biBitCount = 24; // RGB format
    bmi.biCompression = BI_RGB;

    GetDIBits(memory_dc, bitmap, 0, SCREEN_HEIGHT, pixels.data(),
              (BITMAPINFO *)&bmi, DIB_RGB_COLORS);

    // Cleanup GDI objects
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(NULL, desktop_dc);

    std::cout << "📸 Windows screen captured (" << pixels.size() << " bytes)" << std::endl;
    return pixels;
}
#endif

// 🔥 macOS: CoreGraphics (hardware accelerated)
#ifdef PLATFORM_MACOS
std::vector<uint8_t> captureScreen()
{
    std::vector<uint8_t> pixels(SCREEN_WIDTH * SCREEN_HEIGHT * BYTES_PER_PIXEL);

    // Capture primary display
    CGImageRef screen_image = CGDisplayCreateImage(CGMainDisplayID());
    CGColorSpaceRef rgb_space = CGColorSpaceCreateDeviceRGB();

    // Create pixel buffer context
    CGContextRef pixel_context = CGBitmapContextCreate(
        pixels.data(), SCREEN_WIDTH, SCREEN_HEIGHT, 8, SCREEN_WIDTH * 3,
        rgb_space, kCGImageAlphaNoneSkipFirst);

    // Render screen to pixel buffer
    CGRect screen_rect = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
    CGContextDrawImage(pixel_context, screen_rect, screen_image);

    // Cleanup CoreGraphics resources
    CGContextRelease(pixel_context);
    CGColorSpaceRelease(rgb_space);
    CGImageRelease(screen_image);

    return pixels;
}
#endif

// 🔥 Linux X11: Root window capture
#ifdef PLATFORM_LINUX
std::vector<uint8_t> captureScreen()
{
    std::vector<uint8_t> pixels(SCREEN_WIDTH * SCREEN_HEIGHT * BYTES_PER_PIXEL);

    Display *x_display = XOpenDisplay(NULL);
    Window root_window = DefaultRootWindow(x_display);

    // Capture screen region (fast ZPixmap format)
    XImage *x_image = XGetImage(x_display, root_window, 0, 0,
                                SCREEN_WIDTH, SCREEN_HEIGHT,
                                AllPlanes, ZPixmap);

    // Convert X11 pixels to RGB24
    for (int y = 0; y < SCREEN_HEIGHT; y++)
    {
        for (int x = 0; x < SCREEN_WIDTH; x++)
        {
            unsigned long pixel = XGetPixel(x_image, x, y);
            size_t idx = (y * SCREEN_WIDTH + x) * 3;
            pixels[idx + 0] = (pixel >> 16) & 0xFF; // Red channel
            pixels[idx + 1] = (pixel >> 8) & 0xFF;  // Green channel
            pixels[idx + 2] = pixel & 0xFF;         // Blue channel
        }
    }

    XDestroyImage(x_image);
    XCloseDisplay(x_display);
    return pixels;
}
#endif

// Placeholder implementations (require native integration)
#ifdef PLATFORM_IOS
std::vector<uint8_t> captureScreen()
{
    std::vector<uint8_t> pixels(SCREEN_WIDTH * SCREEN_HEIGHT * BYTES_PER_PIXEL, 0x80);
    return pixels; // Gray placeholder
}
#endif

#ifdef PLATFORM_ANDROID
std::vector<uint8_t> captureScreen()
{
    std::vector<uint8_t> pixels(SCREEN_WIDTH * SCREEN_HEIGHT * BYTES_PER_PIXEL, 0);
    return pixels; // Black placeholder
}
#endif

// ========================================
// MAIN PROGRAM - Works identically on all platforms
// ========================================
int main()
{
    std::cout << "🎥 CROSS-PLATFORM SCREEN SHARE SENDER" << std::endl;
    std::cout << "Supported: Windows/macOS/Linux/iOS/Android" << std::endl;
    std::cout << "Resolution: " << SCREEN_WIDTH << "x" << SCREEN_HEIGHT
              << " @ " << TARGET_FPS << " FPS" << std::endl
              << std::endl;

    // 1. Connect to receiver
    NetworkSocket network_socket;
    if (!network_socket.connect(SERVER_IP, SCREEN_PORT))
    {
        std::cerr << "💡 Run receiver.cpp first!" << std::endl;
        return 1;
    }

    // 2. Main streaming loop
    auto last_frame_time = std::chrono::steady_clock::now();
    int frame_counter = 0;

    while (true)
    {
        // Capture current screen frame
        auto frame_pixels = captureScreen();
        uint32_t frame_size = frame_pixels.size();

        // Send frame size first (4-byte header, network byte order)
        uint32_t size_network = htonl(frame_size);
        if (!network_socket.sendData(&size_network, 4))
        {
            std::cout << "❌ Stream interrupted!" << std::endl;
            break;
        }

        // Send raw RGB pixel data
        if (!network_socket.sendData(frame_pixels.data(), frame_size))
        {
            break;
        }

        frame_counter++;

        // Enforce target FPS (prevents overheating/100% CPU)
        auto current_time = std::chrono::steady_clock::now();
        auto frame_duration = std::chrono::milliseconds(1000 / TARGET_FPS);
        if (current_time - last_frame_time < frame_duration)
        {
            std::this_thread::sleep_until(last_frame_time + frame_duration);
        }
        last_frame_time = current_time;

        // Progress indicator
        if (frame_counter % 50 == 0)
        {
            std::cout << "📊 Sent " << frame_counter << " frames ("
                      << (frame_counter * 1000LL / (current_time - last_frame_time).count())
                      << " FPS)" << std::endl;
        }
    }

    std::cout << "🔌 Screen sharing ended" << std::endl;
    return 0;
}
