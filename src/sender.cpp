/**
 * ============================================================================
 * SENDER.CPP - SCREEN SHARING SENDER WITH SSDP AUTO-DISCOVERY
 * ============================================================================
 * This program captures the screen and streams it to a discovered receiver.
 * Features:
 * - Automatic device discovery via SSDP (no manual IP entry)
 * - Cross-platform screen capture (Windows GDI / Linux X11)
 * - Precise 10 FPS frame timing
 * - Reliable TCP streaming with length-prefixed frames
 *
 * Workflow:
 * 1. Discover receivers on network using SSDP
 * 2. Present list to user for selection
 * 3. Connect to selected receiver via TCP
 * 4. Continuously capture and stream screen frames
 */

#include <iostream>   // Console output for status and user input
#include <cstring>    // memset() for socket structures
#include <chrono>     // High-precision timing for FPS control
#include <thread>     // sleep_until for frame pacing
#include <vector>     // Dynamic pixel buffer storage
#include "discover.h" // SSDP auto-discovery module

/**
 * ============================================================================
 * PLATFORM-SPECIFIC HEADERS
 * ============================================================================
 * Windows: Winsock2 + GDI for fastest screen capture
 * Linux: POSIX sockets + X11 for root window capture
 */
#ifdef _WIN32
#include <winsock2.h>              // Windows socket API
#include <windows.h>               // GDI screen capture functions
#pragma comment(lib, "ws2_32.lib") // Auto-link Winsock
#else
#include <sys/socket.h> // POSIX socket functions
#include <netinet/in.h> // sockaddr_in structure
#include <arpa/inet.h>  // inet_pton for IP conversion
#include <unistd.h>     // close() for socket cleanup
#include <X11/Xlib.h>   // X11 display server connection
#include <X11/Xutil.h>  // X11 utilities (root window, XGetImage)
#endif

// ============================================================================
// STREAMING CONFIGURATION
// ============================================================================
#define SCREEN_WIDTH 1280                     // Capture width (720p for good balance)
#define SCREEN_HEIGHT 720                     // Capture height
#define TARGET_FPS 10                         // 10 FPS = smooth motion, ~3MB/s bandwidth
#define BYTES_PER_PIXEL 3                     // RGB24 format (no alpha)
#define FRAME_INTERVAL_MS (1000 / TARGET_FPS) // 100ms between frames

/**
 * ============================================================================
 * CROSS-PLATFORM TCP SOCKET CLASS
 * ============================================================================
 * RAII wrapper for TCP sockets that handles Windows/Unix differences.
 * Provides reliable sending with automatic partial-send handling.
 */
class NetworkSocket
{
private:
#ifdef _WIN32
    SOCKET socket_fd; // Windows uses SOCKET type (actually a pointer)
#else
    int socket_fd; // Unix uses integer file descriptors
#endif

public:
    /**
     * Constructor - Initializes with invalid socket
     */
    NetworkSocket() : socket_fd(-1) {}

    /**
     * Destructor - Automatically closes socket (RAII principle)
     */
    ~NetworkSocket()
    {
        closeSocket();
    }

    /**
     * Connect to discovered receiver
     * @param ip Receiver IP address (e.g., "192.168.1.100")
     * @param port TCP port for streaming (typically 8081)
     * @return true if connection successful
     */
    bool connect(const std::string &ip, int port)
    {
        /**
         * Windows: Initialize Winsock (required once per process)
         * This loads the Windows networking stack
         */
#ifdef _WIN32
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
        {
            std::cerr << "❌ Windows networking initialization failed!" << std::endl;
            return false;
        }
#endif

        /**
         * Create TCP socket
         * SOCK_STREAM = TCP (reliable, ordered delivery)
         * 0 = default protocol (TCP for this family)
         */
        socket_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (socket_fd < 0)
        {
            std::cerr << "❌ Failed to create TCP socket!" << std::endl;
            return false;
        }

        /**
         * Set up receiver address structure
         */
        sockaddr_in server_address;
        memset(&server_address, 0, sizeof(server_address));       // Zero out structure
        server_address.sin_family = AF_INET;                      // IPv4
        server_address.sin_port = htons(port);                    // Port in network byte order
        inet_pton(AF_INET, ip.c_str(), &server_address.sin_addr); // Convert IP string to binary

        /**
         * Establish TCP connection (3-way handshake)
         * This blocks until connection is established or fails
         */
        if (::connect(socket_fd, (sockaddr *)&server_address, sizeof(server_address)) < 0)
        {
            std::cerr << "❌ TCP connection failed to " << ip << ":" << port << std::endl;
            return false;
        }

        std::cout << "✅ Connected to receiver: " << ip << ":" << port << std::endl;
        return true;
    }

    /**
     * Reliable TCP send - handles partial sends
     * TCP may split large messages into multiple packets
     * @param data Pointer to data to send
     * @param size Total bytes to send
     * @return true if all bytes transmitted successfully
     */
    bool send(const void *data, size_t size)
    {
        const uint8_t *bytes = static_cast<const uint8_t *>(data); // Byte pointer for arithmetic
        size_t total_sent = 0;                                     // Track bytes sent

        /**
         * Continue until all bytes are transmitted
         * Handles case where send() doesn't send everything at once
         */
        while (total_sent < size)
        {
            ssize_t sent = ::send(socket_fd, (const char *)(bytes + total_sent),
                                  size - total_sent, 0);
            if (sent <= 0)
            {
                std::cerr << "❌ Send failed (connection lost?)" << std::endl;
                return false; // Connection broken
            }
            total_sent += sent; // Update progress
        }
        return true; // All bytes sent
    }

    /**
     * Platform-safe socket cleanup
     * Closes socket and cleans up Windows networking
     */
    void closeSocket()
    {
        if (socket_fd >= 0) // Valid socket?
        {
#ifdef _WIN32
            closesocket(socket_fd); // Windows-specific close
            WSACleanup();           // Unload Winsock
#else
            ::close(socket_fd); // Unix close
#endif
            socket_fd = -1; // Mark as invalid
        }
    }
};

/**
 * ============================================================================
 * WINDOWS SCREEN CAPTURE (GDI)
 * ============================================================================
 * Uses Windows GDI BitBlt for fastest screen capture.
 * This is hardware-accelerated on most systems.
 */
#ifdef _WIN32
std::vector<uint8_t> captureScreen()
{
    // Allocate pixel buffer (1280x720x3 = ~2.76MB)
    std::vector<uint8_t> pixel_buffer(SCREEN_WIDTH * SCREEN_HEIGHT * BYTES_PER_PIXEL);

    // Get device context for entire screen
    HDC screen_dc = GetDC(NULL);                   // NULL = entire desktop
    HDC memory_dc = CreateCompatibleDC(screen_dc); // Offscreen buffer
    HBITMAP bitmap_handle = CreateCompatibleBitmap(screen_dc, SCREEN_WIDTH, SCREEN_HEIGHT);

    /**
     * Copy screen to memory bitmap
     * BitBlt is hardware-accelerated and very fast
     */
    SelectObject(memory_dc, bitmap_handle);              // Select bitmap into DC
    BitBlt(memory_dc, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, // Destination
           screen_dc, 0, 0, SRCCOPY);                    // Source (top-left)

    /**
     * Configure bitmap info structure for extraction
     */
    BITMAPINFOHEADER bitmap_info = {0};
    bitmap_info.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.biWidth = SCREEN_WIDTH;
    bitmap_info.biHeight = -SCREEN_HEIGHT; // Negative = top-down bitmap
    bitmap_info.biPlanes = 1;
    bitmap_info.biBitCount = 24;        // 24 bits = RGB24
    bitmap_info.biCompression = BI_RGB; // No compression

    /**
     * Extract raw RGB pixels from bitmap
     * GetDIBits copies the bitmap data to our buffer
     */
    GetDIBits(memory_dc, bitmap_handle, 0, SCREEN_HEIGHT,
              pixel_buffer.data(), (BITMAPINFO *)&bitmap_info, DIB_RGB_COLORS);

    /**
     * Clean up GDI resources
     * Critical to prevent memory leaks
     */
    DeleteObject(bitmap_handle);
    DeleteDC(memory_dc);
    ReleaseDC(NULL, screen_dc);

    return pixel_buffer; // Return captured frame
}
#endif

/**
 * ============================================================================
 * LINUX SCREEN CAPTURE (X11)
 * ============================================================================
 * Captures root window using X11 API.
 * Slower than GDI but works on all Unix-like systems.
 */
#ifndef _WIN32
std::vector<uint8_t> captureScreen()
{
    // Allocate pixel buffer
    std::vector<uint8_t> pixel_buffer(SCREEN_WIDTH * SCREEN_HEIGHT * BYTES_PER_PIXEL);

    /**
     * Connect to X11 display server
     * :0 is usually the primary display
     */
    Display *x11_display = XOpenDisplay(NULL);
    if (!x11_display)
    {
        std::cerr << "❌ X11 display connection failed!" << std::endl;
        return pixel_buffer; // Return empty/black frame
    }

    Window root_window = DefaultRootWindow(x11_display); // Get root window

    /**
     * Capture screen region
     * ZPixmap = format with pixel values (fastest)
     */
    XImage *x11_image = XGetImage(x11_display, root_window, 0, 0,
                                  SCREEN_WIDTH, SCREEN_HEIGHT,
                                  AllPlanes, ZPixmap);

    /**
     * Convert X11 image to RGB24 format
     * X11 may use 24-bit or 32-bit pixels
     */
    for (int y = 0; y < SCREEN_HEIGHT; ++y)
    {
        for (int x = 0; x < SCREEN_WIDTH; ++x)
        {
            unsigned long x11_pixel = XGetPixel(x11_image, x, y); // Get pixel value
            size_t pixel_index = (y * SCREEN_WIDTH + x) * BYTES_PER_PIXEL;

            // X11 typically stores as 0xRRGGBB (big endian)
            pixel_buffer[pixel_index + 0] = (x11_pixel >> 16) & 0xFF; // Red
            pixel_buffer[pixel_index + 1] = (x11_pixel >> 8) & 0xFF;  // Green
            pixel_buffer[pixel_index + 2] = (x11_pixel >> 0) & 0xFF;  // Blue
        }
    }

    // Clean up X11 resources
    XDestroyImage(x11_image);
    XCloseDisplay(x11_display);

    return pixel_buffer;
}
#endif

/**
 * ============================================================================
 * MAIN PROGRAM - Zero-Config Screen Sharing
 * ============================================================================
 * Steps:
 * 1. SSDP discovery finds receivers automatically
 * 2. User selects target from list
 * 3. TCP connection established
 * 4. Screen streaming at 10 FPS
 */
int main()
{
    std::cout << "🎥 AUTO-DISCOVERY SCREEN SHARING SENDER" << std::endl;
    std::cout << "Resolution: " << SCREEN_WIDTH << "x" << SCREEN_HEIGHT
              << " @ " << TARGET_FPS << " FPS" << std::endl;
    std::cout << "========================================" << std::endl
              << std::endl;

    /**
     * STEP 1: DISCOVER RECEIVERS
     * ==========================
     * SSDP multicast discovery scans network
     */
    std::vector<DiscoveredDevice> available_receivers = discoverReceivers();

    if (available_receivers.empty())
    {
        std::cerr << "❌ No screen receivers found on network!" << std::endl;
        std::cerr << "💡 Start 'receiver' on target device first" << std::endl;
        return 1; // Exit with error
    }

    /**
     * STEP 2: DISPLAY DISCOVERED DEVICES
     * ==================================
     * Show numbered list for user selection
     */
    std::cout << listDevices(available_receivers);

    /**
     * STEP 3: USER SELECTION
     * ======================
     * Let user choose target receiver
     */
    size_t selected_device_index;
    std::cout << "Enter device number (0-" << (available_receivers.size() - 1) << "): ";
    std::cin >> selected_device_index;

    // Validate selection
    if (selected_device_index >= available_receivers.size())
    {
        std::cerr << "❌ Invalid selection!" << std::endl;
        return 1;
    }

    /**
     * STEP 4: CONNECT TO SELECTED RECEIVER
     * ====================================
     * Establish TCP connection for streaming
     */
    NetworkSocket tcp_connection;
    const DiscoveredDevice &target_receiver = available_receivers[selected_device_index];

    if (!tcp_connection.connect(target_receiver.ip_address, target_receiver.tcp_port))
    {
        std::cerr << "❌ Failed to connect to " << target_receiver.toString() << std::endl;
        return 1;
    }

    std::cout << "\n🎬 STREAMING ACTIVE to " << target_receiver.toString()
              << " (Ctrl+C to stop)" << std::endl;

    /**
     * STEP 5: MAIN STREAMING LOOP
     * ===========================
     * Capture and send frames at precise 10 FPS
     */
    auto previous_frame_time = std::chrono::steady_clock::now();
    int frame_counter = 0;

    while (true) // Loop until connection lost
    {
        /**
         * Capture current screen frame
         * Returns RGB24 pixel data
         */
        std::vector<uint8_t> screen_frame = captureScreen();
        uint32_t frame_size_bytes = static_cast<uint32_t>(screen_frame.size());

        /**
         * Send frame size prefix (4 bytes)
         * Network byte order ensures compatibility
         */
        uint32_t network_frame_size = htonl(frame_size_bytes); // Convert to network order
        if (!tcp_connection.send(&network_frame_size, sizeof(network_frame_size)))
        {
            std::cout << "\n❌ Connection lost - stream ended" << std::endl;
            break;
        }

        /**
         * Send actual pixel data
         * ~2.76MB per frame at 720p
         */
        if (!tcp_connection.send(screen_frame.data(), frame_size_bytes))
        {
            break; // Send failed
        }

        frame_counter++;

        /**
         * Precise FPS limiting
         * Ensures we don't send faster than TARGET_FPS
         */
        auto current_time = std::chrono::steady_clock::now();
        auto target_frame_time = previous_frame_time + std::chrono::milliseconds(FRAME_INTERVAL_MS);

        if (current_time < target_frame_time)
        {
            std::this_thread::sleep_until(target_frame_time); // Sleep until next frame time
        }
        previous_frame_time = std::chrono::steady_clock::now();

        /**
         * Status update every 50 frames (5 seconds at 10 FPS)
         */
        if (frame_counter % 50 == 0)
        {
            std::cout << "📊 Sent " << frame_counter << " frames ("
                      << TARGET_FPS << " FPS)" << std::endl;
        }
    }

    std::cout << "\n🔌 Stream ended (" << frame_counter << " frames transmitted)" << std::endl;
    return 0;
}