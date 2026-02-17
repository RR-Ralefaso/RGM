#include <iostream>   // Console output for status messages and user input
#include <cstring>    // memset() for socket address structure initialization
#include <chrono>     // std::chrono for precise 10 FPS frame timing control
#include <thread>     // std::this_thread::sleep_until() for smooth frame pacing
#include <vector>     // Dynamic pixel buffer storage (RGB24 format)
#include "discover.h" // SSDP auto-discovery module (zero-config device finding)

/**
 * ============================================================================
 * PLATFORM-SPECIFIC NETWORKING AND SCREEN CAPTURE HEADERS
 * ============================================================================
 * Windows: Winsock2 + GDI (fastest screen capture method)
 * Linux: POSIX sockets + X11 (root window capture)
 */
#ifdef _WIN32
#include <winsock2.h>              // Windows socket API (WSAStartup, connect, send)
#include <windows.h>               // GDI screen capture (GetDC, BitBlt, CreateCompatibleBitmap)
#pragma comment(lib, "ws2_32.lib") // Automatically link Windows socket library
#else
#include <sys/socket.h> // POSIX socket functions (socket, connect, send)
#include <netinet/in.h> // sockaddr_in address structure
#include <arpa/inet.h>  // inet_pton() for IP address conversion
#include <unistd.h>     // close() function for socket cleanup
#include <X11/Xlib.h>   // X11 display server connection
#include <X11/Xutil.h>  // X11 utilities (DefaultRootWindow, XGetImage)
#endif

// ============================================================================
// STREAMING CONFIGURATION (Optimized for performance)
// ============================================================================
#define SCREEN_WIDTH 1280 // 720p resolution (fast capture + low bandwidth)
#define SCREEN_HEIGHT 720
#define TARGET_FPS 10                         // 10 FPS = smooth motion + ~3MB/s bandwidth
#define BYTES_PER_PIXEL 3                     // RGB24 format (no alpha channel needed)
#define FRAME_INTERVAL_MS (1000 / TARGET_FPS) // 100ms between frames

/**
 * ============================================================================
 * CROSS-PLATFORM TCP SOCKET CLASS
 * ============================================================================
 * Abstracts Windows SOCKET vs Unix int socket differences
 * Provides reliable TCP send with automatic partial-send handling
 */
class NetworkSocket
{
private:
#ifdef _WIN32
    SOCKET socket_fd; // Windows socket handle type
#else
    int socket_fd; // Unix/Linux file descriptor
#endif

public:
    NetworkSocket() : socket_fd(-1) {} // Invalid socket initially

    /**
     * Destructor automatically closes socket (RAII)
     */
    ~NetworkSocket()
    {
        closeSocket();
    }

    /**
     * Connect to discovered receiver via TCP
     * @param ip Receiver IP from SSDP discovery (ex: "192.168.1.100")
     * @param port TCP streaming port (usually 8081)
     * @return true if connection successful
     */
    bool connect(const std::string &ip, int port)
    {
        // Windows: Initialize Winsock networking stack (one-time call)
#ifdef _WIN32
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
        {
            std::cerr << "❌ Windows networking initialization failed!" << std::endl;
            return false;
        }
#endif

        // Create TCP stream socket (reliable, ordered delivery)
        socket_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_fd < 0)
        {
            std::cerr << "❌ Failed to create TCP socket!" << std::endl;
            return false;
        }

        // Setup receiver network address
        sockaddr_in server_address;
        memset(&server_address, 0, sizeof(server_address));       // Zero structure
        server_address.sin_family = AF_INET;                      // IPv4 protocol family
        server_address.sin_port = htons(port);                    // Convert to network byte order
        inet_pton(AF_INET, ip.c_str(), &server_address.sin_addr); // IP string → binary

        // Establish TCP connection (3-way handshake)
        if (::connect(socket_fd, (sockaddr *)&server_address, sizeof(server_address)) < 0)
        {
            std::cerr << "❌ TCP connection failed to " << ip << ":" << port << std::endl;
            return false;
        }

        std::cout << "✅ Connected to receiver: " << ip << ":" << port << std::endl;
        return true;
    }

    /**
     * Reliable TCP send - handles partial sends automatically
     * TCP may split large frames into multiple packets
     * @param data Pointer to frame data (size prefix + pixels)
     * @param size Total bytes to send
     * @return true if all bytes transmitted successfully
     */
    bool send(const void *data, size_t size)
    {
        const uint8_t *bytes = static_cast<const uint8_t *>(data);
        size_t total_sent = 0;

        // Continue sending until all bytes transmitted
        while (total_sent < size)
        {
            ssize_t sent = ::send(socket_fd, (const char *)(bytes + total_sent),
                                  size - total_sent, 0);
            if (sent <= 0)
            {
                std::cerr << "❌ Send failed (connection lost?)" << std::endl;
                return false;
            }
            total_sent += sent;
        }
        return true;
    }

    /**
     * Platform-safe socket cleanup
     */
    void closeSocket()
    {
        if (socket_fd >= 0)
        {
#ifdef _WIN32
            closesocket(socket_fd); // Windows socket cleanup
            WSACleanup();           // Windows networking cleanup
#else
            ::close(socket_fd); // Unix file descriptor cleanup
#endif
            socket_fd = -1;
        }
    }
};

/**
 * ============================================================================
 * PLATFORM-SPECIFIC SCREEN CAPTURE FUNCTIONS
 * ============================================================================
 * Returns RGB24 pixel buffer (1280x720x3 bytes = ~2.7MB per frame)
 */

/**
 * Windows: GDI BitBlt - Fastest screen capture method (<10ms/frame)
 */
#ifdef _WIN32
std::vector<uint8_t> captureScreen()
{
    std::vector<uint8_t> pixel_buffer(SCREEN_WIDTH * SCREEN_HEIGHT * BYTES_PER_PIXEL);

    // Get primary monitor device context (entire desktop)
    HDC screen_dc = GetDC(NULL);                   // Capture source
    HDC memory_dc = CreateCompatibleDC(screen_dc); // Offscreen buffer
    HBITMAP bitmap_handle = CreateCompatibleBitmap(screen_dc, SCREEN_WIDTH, SCREEN_HEIGHT);

    // Copy screen pixels to memory bitmap (hardware accelerated)
    SelectObject(memory_dc, bitmap_handle);
    BitBlt(memory_dc, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, // Destination
           screen_dc, 0, 0, SRCCOPY);                    // Source (top-left)

    // Extract raw RGB pixels from bitmap
    BITMAPINFOHEADER bitmap_info = {0};
    bitmap_info.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.biWidth = SCREEN_WIDTH;
    bitmap_info.biHeight = -SCREEN_HEIGHT; // Negative = top-down bitmap
    bitmap_info.biPlanes = 1;
    bitmap_info.biBitCount = 24; // RGB24 format
    bitmap_info.biCompression = BI_RGB;

    GetDIBits(memory_dc, bitmap_handle, 0, SCREEN_HEIGHT,
              pixel_buffer.data(), (BITMAPINFO *)&bitmap_info, DIB_RGB_COLORS);

    // Cleanup GDI resources (prevent memory leaks)
    DeleteObject(bitmap_handle);
    DeleteDC(memory_dc);
    ReleaseDC(NULL, screen_dc);

    return pixel_buffer;
}
#endif

/**
 * Linux: X11 Root Window Capture - Captures primary display
 */
#ifndef _WIN32
std::vector<uint8_t> captureScreen()
{
    std::vector<uint8_t> pixel_buffer(SCREEN_WIDTH * SCREEN_HEIGHT * BYTES_PER_PIXEL);

    // Connect to X11 display server (default :0 display)
    Display *x11_display = XOpenDisplay(NULL);
    if (!x11_display)
    {
        std::cerr << "❌ X11 display connection failed!" << std::endl;
        return pixel_buffer; // Return empty/black frame
    }

    Window root_window = DefaultRootWindow(x11_display); // Primary screen

    // Capture screen region (ZPixmap = fastest format)
    XImage *x11_image = XGetImage(x11_display, root_window, 0, 0,
                                  SCREEN_WIDTH, SCREEN_HEIGHT,
                                  AllPlanes, ZPixmap);

    // Convert X11 pixels to RGB24 (handles 24/32-bit depth automatically)
    for (int y = 0; y < SCREEN_HEIGHT; ++y)
    {
        for (int x = 0; x < SCREEN_WIDTH; ++x)
        {
            unsigned long x11_pixel = XGetPixel(x11_image, x, y);
            size_t pixel_index = (y * SCREEN_WIDTH + x) * BYTES_PER_PIXEL;

            pixel_buffer[pixel_index + 0] = (x11_pixel >> 16) & 0xFF; // Red channel
            pixel_buffer[pixel_index + 1] = (x11_pixel >> 8) & 0xFF;  // Green channel
            pixel_buffer[pixel_index + 2] = (x11_pixel >> 0) & 0xFF;  // Blue channel
        }
    }

    // Cleanup X11 resources
    XDestroyImage(x11_image);
    XCloseDisplay(x11_display);

    return pixel_buffer;
}
#endif

/**
 * ============================================================================
 * MAIN PROGRAM - Zero-Config Screen Sharing Workflow
 * ============================================================================
 * 1. SSDP auto-discovery finds receivers (no IP entry needed)
 * 2. User selects target device from list
 * 3. Establishes TCP connection
 * 4. Streams screen continuously at 10 FPS
 */
int main()
{
    std::cout << "🎥 AUTO-DISCOVERY SCREEN SHARING SENDER" << std::endl;
    std::cout << "Resolution: " << SCREEN_WIDTH << "x" << SCREEN_HEIGHT
              << " @ " << TARGET_FPS << " FPS" << std::endl;
    std::cout << "========================================" << std::endl
              << std::endl;

    // === STEP 1: AUTOMATIC DEVICE DISCOVERY ===
    std::vector<DiscoveredDevice> available_receivers = discoverReceivers();

    if (available_receivers.empty())
    {
        std::cerr << "❌ No screen receivers found on network!" << std::endl;
        std::cerr << "💡 Start 'receiver' on target device first" << std::endl;
        return 1;
    }

    // === STEP 2: DISPLAY DISCOVERED DEVICES ===
    std::cout << listDevices(available_receivers);

    // === STEP 3: USER SELECTS TARGET ===
    size_t selected_device_index;
    std::cout << "Enter device number (0-" << (available_receivers.size() - 1) << "): ";
    std::cin >> selected_device_index;

    if (selected_device_index >= available_receivers.size())
    {
        std::cerr << "❌ Invalid selection!" << std::endl;
        return 1;
    }

    // === STEP 4: CONNECT TO SELECTED RECEIVER ===
    NetworkSocket tcp_connection;
    const DiscoveredDevice &target_receiver = available_receivers[selected_device_index];

    if (!tcp_connection.connect(target_receiver.ip_address, target_receiver.tcp_port))
    {
        std::cerr << "❌ Failed to connect to " << target_receiver.toString() << std::endl;
        return 1;
    }

    std::cout << "\n🎬 STREAMING ACTIVE to " << target_receiver.toString()
              << " (Ctrl+C to stop)" << std::endl;

    // === STEP 5: MAIN STREAMING LOOP (10 FPS) ===
    auto previous_frame_time = std::chrono::steady_clock::now();
    int frame_counter = 0;

    while (true)
    {
        // Capture current screen frame (~5-10ms)
        std::vector<uint8_t> screen_frame = captureScreen();
        uint32_t frame_size_bytes = static_cast<uint32_t>(screen_frame.size());

        // Send frame size prefix (4 bytes, network byte order)
        uint32_t network_frame_size = htonl(frame_size_bytes);
        if (!tcp_connection.send(&network_frame_size, sizeof(network_frame_size)))
        {
            std::cout << "\n❌ Connection lost - stream ended" << std::endl;
            break;
        }

        // Send raw RGB pixel data (~2.7MB per frame)
        if (!tcp_connection.send(screen_frame.data(), frame_size_bytes))
        {
            break;
        }

        frame_counter++;

        // === PRECISE FPS LIMITING ===
        // Sleep until exactly 100ms has passed (10 FPS)
        auto current_time = std::chrono::steady_clock::now();
        auto target_frame_time = previous_frame_time + std::chrono::milliseconds(FRAME_INTERVAL_MS);

        if (current_time < target_frame_time)
        {
            std::this_thread::sleep_until(target_frame_time);
        }
        previous_frame_time = std::chrono::steady_clock::now();

        // Live status update (every 5 seconds)
        if (frame_counter % 50 == 0)
        {
            std::cout << "📊 Sent " << frame_counter << " frames ("
                      << TARGET_FPS << " FPS)" << std::endl;
        }
    }

    std::cout << "\n🔌 Stream ended (" << frame_counter << " frames transmitted)" << std::endl;
    return 0;
}
