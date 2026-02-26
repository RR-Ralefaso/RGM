/**
 * SENDER.CPP - SCREEN CAPTURE AND STREAMING
 *
 * This module handles screen capture and streaming to a receiver.
 * It automatically detects screen resolution and streams at high quality.
 */

#include <iostream>
#include <cstring>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <iomanip>
#include <fstream>
#include "discover.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mstcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <SDL2/SDL.h> // For splash screen
#endif

// Constants - easily modifiable for future upgrades
#define BYTES_PER_PIXEL 3                              // RGB format (can be changed to RGBA=4 for alpha channel)
#define CONNECTION_TIMEOUT_MS 5000                     // Connection timeout in milliseconds
#define STATS_INTERVAL_SEC 5                           // Statistics display interval
#define MAX_FRAME_SKIP 3                               // Maximum frames to skip when overloaded
static const int SOCKET_BUFFER_SIZE = 4 * 1024 * 1024; // 4MB socket buffer for high FPS

// Global variables for screen dimensions
int SCREEN_WIDTH = 1920;  // Default, will be updated
int SCREEN_HEIGHT = 1080; // Default, will be updated
int TARGET_FPS = 60;      // Target FPS (can be made adjustable in future)

// Global flag for running state (atomic for thread safety)
std::atomic<bool> g_running{true};

/**
 * Display RGM splash screen using SDL2
 * Shows the RGM.png image when the software starts
 */
void showSplashScreen()
{
    std::cout << "🎬 Initializing RGM Screen Share..." << std::endl;

    // Initialize SDL with video support only
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        std::cerr << "⚠️  Could not initialize SDL for splash screen: " << SDL_GetError() << std::endl;
        return; // Non-critical, continue without splash
    }

    // Create a splash window (always on top, no border)
    SDL_Window *splashWindow = SDL_CreateWindow(
        "RGM Screen Share",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        400, 300, // Splash screen size
        SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP);

    if (!splashWindow)
    {
        std::cerr << "⚠️  Could not create splash window: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return;
    }

    // Create renderer for the splash window
    SDL_Renderer *splashRenderer = SDL_CreateRenderer(splashWindow, -1,
                                                      SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    if (!splashRenderer)
    {
        std::cerr << "⚠️  Could not create splash renderer: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(splashWindow);
        SDL_Quit();
        return;
    }

    // Try to load RGM.png from various possible locations
    SDL_Surface *image = nullptr;
    const char *possiblePaths[] = {
        "assets/icons/RGM.png",
        "../assets/icons/RGM.png",
        "./assets/icons/RGM.png",
        "/usr/share/rgm/icons/RGM.png",
        nullptr};

    for (int i = 0; possiblePaths[i] != nullptr && !image; i++)
    {
        std::ifstream file(possiblePaths[i]);
        if (file.good())
        {
            file.close();
            image = SDL_LoadBMP(possiblePaths[i]);
            if (image)
            {
                std::cout << "✅ Loaded RGM logo from: " << possiblePaths[i] << std::endl;
            }
        }
    }

    // If no image found, create a colored rectangle as fallback
    if (!image)
    {
        std::cout << "ℹ️  RGM.png not found, using default splash" << std::endl;
        image = SDL_CreateRGBSurface(0, 380, 280, 32, 0, 0, 0, 0);
        if (image)
        {
            SDL_FillRect(image, NULL, SDL_MapRGB(image->format, 70, 130, 180)); // Steel blue
        }
    }

    if (image)
    {
        SDL_Texture *texture = SDL_CreateTextureFromSurface(splashRenderer, image);
        if (texture)
        {
            // Clear renderer and copy texture
            SDL_RenderClear(splashRenderer);
            SDL_RenderCopy(splashRenderer, texture, NULL, NULL);
            SDL_RenderPresent(splashRenderer);

            // Display for 2 seconds
            SDL_Delay(2000);

            SDL_DestroyTexture(texture);
        }
        SDL_FreeSurface(image);
    }

    // Cleanup SDL resources
    SDL_DestroyRenderer(splashRenderer);
    SDL_DestroyWindow(splashWindow);
    SDL_Quit();

    std::cout << "✅ Splash screen completed" << std::endl;
}

/**
 * Get screen dimensions - platform specific
 */
void getScreenDimensions(int &width, int &height)
{
#ifdef _WIN32
    // Windows: Use GetSystemMetrics for primary monitor
    width = GetSystemMetrics(SM_CXSCREEN);
    height = GetSystemMetrics(SM_CYSCREEN);
    std::cout << "🖥️  Detected Windows display: " << width << "x" << height << std::endl;
#else
    // Linux: Use X11 to get display dimensions
    Display *display = XOpenDisplay(NULL);
    if (display)
    {
        int screen_num = DefaultScreen(display);
        width = DisplayWidth(display, screen_num);
        height = DisplayHeight(display, screen_num);
        XCloseDisplay(display);
        std::cout << "🖥️  Detected X11 display: " << width << "x" << height << std::endl;
    }
    else
    {
        // Fallback if X11 fails
        width = 1920;
        height = 1080;
        std::cerr << "⚠️  Could not detect screen dimensions, using 1920x1080" << std::endl;
    }
#endif
}

/**
 * Network socket class with improved error handling
 * Encapsulates all socket operations for easy maintenance
 */
class NetworkSocket
{
private:
#ifdef _WIN32
    SOCKET sock; // Windows socket handle
#else
    int sock; // POSIX socket descriptor
#endif

public:
    // Constructor - initializes socket to invalid state
    NetworkSocket() : sock(-1) {}

    // Destructor - ensures socket is closed
    ~NetworkSocket()
    {
        close();
    }

    // Check if socket is valid
    bool isValid() const
    {
#ifdef _WIN32
        return sock != INVALID_SOCKET;
#else
        return sock >= 0;
#endif
    }

    // Close socket if open
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

    // Create a new TCP socket
    bool create()
    {
        close(); // Close any existing socket

#ifdef _WIN32
        sock = socket(AF_INET, SOCK_STREAM, 0);
        return sock != INVALID_SOCKET;
#else
        sock = socket(AF_INET, SOCK_STREAM, 0);
        return sock >= 0;
#endif
    }

    /**
     * Connect to a remote host with timeout
     */
    bool connect(const std::string &ip, int port, int timeout_ms = CONNECTION_TIMEOUT_MS)
    {
        if (!create())
        {
            std::cerr << "❌ Failed to create socket" << std::endl;
            return false;
        }

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);

        // Convert IP string to binary
        if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0)
        {
            std::cerr << "❌ Invalid IP address: " << ip << std::endl;
            return false;
        }

        // Set socket to non-blocking for timeout support
#ifdef _WIN32
        u_long mode = 1;
        ioctlsocket(sock, FIONBIO, &mode);
#else
        int flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif

        // Initiate non-blocking connection
        int result = ::connect(sock, (struct sockaddr *)&addr, sizeof(addr));

        bool connected = false;

        // Check if connection is in progress
#ifdef _WIN32
        if (result == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK)
#else
        if (result < 0 && errno == EINPROGRESS)
#endif
        {
            fd_set fdset;
            FD_ZERO(&fdset);
            FD_SET(sock, &fdset);

            struct timeval tv;
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;

            // Wait for connection to complete
            if (select(sock + 1, NULL, &fdset, NULL, &tv) == 1)
            {
                int so_error;
                socklen_t len = sizeof(so_error);
                getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&so_error, &len);

                if (so_error == 0)
                    connected = true;
            }
        }

        // Restore blocking mode
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

        // Enable TCP_NODELAY to disable Nagle's algorithm (reduces latency)
        int nodelay = 1;
        if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *)&nodelay, sizeof(nodelay)) < 0)
        {
            std::cerr << "⚠️  Warning: Could not set TCP_NODELAY" << std::endl;
        }

        // Increase socket buffer size for high FPS streaming
        int sock_buf_size = SOCKET_BUFFER_SIZE;
        setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char *)&sock_buf_size, sizeof(sock_buf_size));

        std::cout << "✅ Connected to " << ip << ":" << port << std::endl;
        return true;
    }

    /**
     * Send all data reliably
     * Handles partial sends and continues until all data is sent
     */
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
 * Windows screen capture with high quality
 */
std::vector<uint8_t> captureScreen()
{
    // Allocate pixel buffer
    std::vector<uint8_t> pixels(SCREEN_WIDTH * SCREEN_HEIGHT * BYTES_PER_PIXEL, 0);

    // Get device context for the entire screen
    HDC screen_dc = GetDC(NULL);
    if (!screen_dc)
    {
        std::cerr << "❌ Failed to get screen DC" << std::endl;
        return pixels;
    }

    // Create compatible DC and bitmap
    HDC mem_dc = CreateCompatibleDC(screen_dc);
    HBITMAP bitmap = CreateCompatibleBitmap(screen_dc, SCREEN_WIDTH, SCREEN_HEIGHT);

    if (!bitmap)
    {
        std::cerr << "❌ Failed to create bitmap" << std::endl;
        ReleaseDC(NULL, screen_dc);
        DeleteDC(mem_dc);
        return pixels;
    }

    // Select bitmap into memory DC
    SelectObject(mem_dc, bitmap);

    // Capture screen with CAPTUREBLT to include layered windows
    BitBlt(mem_dc, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, screen_dc, 0, 0, SRCCOPY | CAPTUREBLT);

    // Prepare bitmap info structure
    BITMAPINFOHEADER bi = {0};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = SCREEN_WIDTH;
    bi.biHeight = -SCREEN_HEIGHT; // Negative for top-down (no flipping needed)
    bi.biPlanes = 1;
    bi.biBitCount = 24; // 24-bit RGB
    bi.biCompression = BI_RGB;

    // Get the bitmap bits
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
 * Linux X11 screen capture with high quality
 */
std::vector<uint8_t> captureScreen()
{
    // Allocate pixel buffer
    std::vector<uint8_t> pixels(SCREEN_WIDTH * SCREEN_HEIGHT * BYTES_PER_PIXEL, 0);

    // Open X display
    Display *display = XOpenDisplay(NULL);
    if (!display)
    {
        std::cerr << "❌ Failed to open X display" << std::endl;
        return pixels;
    }

    int screen_num = DefaultScreen(display);
    Window root = RootWindow(display, screen_num);

    // Capture the screen
    XImage *image = XGetImage(display, root, 0, 0,
                              SCREEN_WIDTH, SCREEN_HEIGHT,
                              AllPlanes, ZPixmap);

    if (!image)
    {
        std::cerr << "❌ Failed to capture screen" << std::endl;
        XCloseDisplay(display);
        return pixels;
    }

    // Convert XImage to RGB format
    for (int y = 0; y < SCREEN_HEIGHT; y++)
    {
        for (int x = 0; x < SCREEN_WIDTH; x++)
        {
            unsigned long pixel = XGetPixel(image, x, y);
            size_t index = (y * SCREEN_WIDTH + x) * BYTES_PER_PIXEL;

            // Convert from X11 format (depends on endianness)
#ifdef WORDS_BIGENDIAN
            pixels[index + 0] = (pixel >> 16) & 0xFF; // Red
            pixels[index + 1] = (pixel >> 8) & 0xFF;  // Green
            pixels[index + 2] = pixel & 0xFF;         // Blue
#else
            pixels[index + 0] = pixel & 0xFF;         // Blue
            pixels[index + 1] = (pixel >> 8) & 0xFF;  // Green
            pixels[index + 2] = (pixel >> 16) & 0xFF; // Red
#endif
        }
    }

    // Cleanup
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
              << "/" << TARGET_FPS
              << " | Bandwidth: " << std::setprecision(2) << mbps << " MB/s"
              << " | Resolution: " << SCREEN_WIDTH << "x" << SCREEN_HEIGHT << std::endl;
}

/**
 * Main function
 * Handles the overall flow:
 * 1. Show splash screen
 * 2. Detect screen resolution
 * 3. Discover receivers
 * 4. Connect to selected receiver
 * 5. Stream screen captures
 */
int main()
{
    // Show RGM splash screen
    showSplashScreen();

    // Detect screen dimensions
    getScreenDimensions(SCREEN_WIDTH, SCREEN_HEIGHT);

    // Display program information
    std::cout << "========================================" << std::endl;
    std::cout << "🎥 RGM SCREEN SHARE SENDER v2.0" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Detected Resolution: " << SCREEN_WIDTH << "x" << SCREEN_HEIGHT << std::endl;
    std::cout << "Target FPS: " << TARGET_FPS << std::endl;
    std::cout << "========================================" << std::endl;

    // Initialize network sockets
    if (!initSockets())
    {
        std::cerr << "❌ Failed to initialize sockets" << std::endl;
        return 1;
    }

    // Discover available receivers
    std::cout << "🔍 Discovering receivers..." << std::endl;
    auto receivers = discoverReceivers(5);

    if (receivers.empty())
    {
        std::cerr << "❌ No receivers found!" << std::endl;
        std::cerr << "   Make sure receiver is running on the same network." << std::endl;
        std::cerr << "   Check firewall settings (UDP 1900, TCP 8081)." << std::endl;
        cleanupSockets();
        return 1;
    }

    // Display found receivers
    std::cout << listDevices(receivers);

    // Let user select receiver
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

    /**
     * Send handshake information to receiver
     */
    struct
    {
        uint32_t width;
        uint32_t height;
        uint32_t fps;
    } handshake = {
        htonl(SCREEN_WIDTH),
        htonl(SCREEN_HEIGHT),
        htonl(TARGET_FPS)};

    if (!connection.sendAll(&handshake, sizeof(handshake)))
    {
        std::cerr << "❌ Failed to send screen dimensions to receiver" << std::endl;
        cleanupSockets();
        return 1;
    }

    std::cout << "🎬 Starting stream..." << std::endl;
    std::cout << "   Press Ctrl+C to stop" << std::endl;

    // Initialize timing variables
    auto last_time = std::chrono::steady_clock::now();
    auto stats_time = last_time;
    int frames_sent = 0;
    size_t total_bytes = 0;
    bool streaming = true;

    // Calculate frame duration in microseconds
    const auto frame_duration = std::chrono::microseconds(1000000 / TARGET_FPS);

    /**
     * Main streaming loop
     */
    while (streaming && g_running)
    {
        auto frame_start = std::chrono::steady_clock::now();

        // Capture current screen
        auto frame = captureScreen();

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

        // Update statistics
        frames_sent++;
        total_bytes += 4 + frame_size;

        // Display periodic statistics
        auto now = std::chrono::steady_clock::now();
        auto stats_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                                 now - stats_time)
                                 .count();

        if (stats_elapsed >= STATS_INTERVAL_SEC)
        {
            showStats(frames_sent, stats_elapsed, total_bytes);
            stats_time = now;
        }

        /**
         * Adaptive frame timing
         * Skip frames if we're falling behind to maintain real-time
         */
        auto frame_end = std::chrono::steady_clock::now();
        auto frame_time = frame_end - frame_start;

        static int frames_behind = 0;
        if (frame_time > frame_duration)
        {
            frames_behind++;
            if (frames_behind > MAX_FRAME_SKIP)
            {
                frames_behind = 0;
                continue; // Skip this frame's wait time
            }
        }
        else
        {
            frames_behind = 0;
            std::this_thread::sleep_for(frame_duration - frame_time);
        }
    }

    // Display final statistics
    auto end_time = std::chrono::steady_clock::now();
    auto total_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                             end_time - last_time)
                             .count();

    std::cout << "========================================" << std::endl;
    std::cout << "📊 STREAMING STATISTICS" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Resolution:      " << SCREEN_WIDTH << "x" << SCREEN_HEIGHT << std::endl;
    std::cout << "Frames sent:     " << frames_sent << std::endl;
    std::cout << "Duration:        " << total_seconds << " seconds" << std::endl;
    if (total_seconds > 0)
    {
        std::cout << "Average FPS:     " << (frames_sent / total_seconds) << std::endl;
        float total_mb = total_bytes / (1024.0 * 1024.0);
        std::cout << "Total data:      " << std::fixed << std::setprecision(2) << total_mb << " MB" << std::endl;
        std::cout << "Avg bandwidth:   " << std::fixed << std::setprecision(2) << (total_mb / total_seconds) << " MB/s" << std::endl;
    }
    std::cout << "========================================" << std::endl;

    // Cleanup
    cleanupSockets();
    return 0;
}