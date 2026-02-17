#include <iostream> // Console input/output
#include <cstring>  // Memory operations (memset)
#include <chrono>   // Timing for FPS control
#include <thread>   // Sleep functions
#ifdef _WIN32       // Windows headers
#include <winsock2.h>
#include <windows.h> // Screen capture (BitBlt)
#pragma comment(lib, "ws2_32.lib")
#else                   // Linux headers
#include <sys/socket.h> // Socket operations
#include <arpa/inet.h>  // IP address conversion
#include <unistd.h>     // close()
#include <X11/Xlib.h>   // X11 screen capture
#include <X11/Xutil.h>
#endif

#define SERVER_IP "127.0.0.1" // Receiver IP (localhost)
#define PORT 8081             // TCP port
#define WIDTH 1280            // Screen width (reduced for speed)
#define HEIGHT 720            // Screen height (reduced for speed)

// Initialize Windows networking (Linux doesn't need this)
void initSockets()
{
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        std::cerr << "❌ Windows networking failed!" << std::endl;
        exit(1);
    }
#endif
}

// 🔥 Windows: Capture entire screen using BitBlt (fast!)
#ifdef _WIN32
unsigned char *captureScreen(int &size)
{
    HDC screenDC = GetDC(NULL);               // Get screen device context
    HDC memDC = CreateCompatibleDC(screenDC); // Memory DC for bitmap
    HBITMAP bitmap = CreateCompatibleBitmap(screenDC, WIDTH, HEIGHT);
    SelectObject(memDC, bitmap); // Select bitmap into DC

    // Copy screen to bitmap (SRCCOPY = full color copy)
    BitBlt(memDC, 0, 0, WIDTH, HEIGHT, screenDC, 0, 0, SRCCOPY);

    // Setup bitmap info for pixel extraction
    BITMAPINFOHEADER info = {0};
    info.biSize = sizeof(BITMAPINFOHEADER);
    info.biWidth = WIDTH;
    info.biHeight = -HEIGHT; // Negative = top-down
    info.biPlanes = 1;
    info.biBitCount = 24; // 24-bit RGB
    info.biCompression = BI_RGB;

    size = WIDTH * HEIGHT * 3; // RGB = 3 bytes per pixel
    unsigned char *pixels = new unsigned char[size];

    // Extract raw RGB pixels from bitmap
    GetDIBits(memDC, bitmap, 0, HEIGHT, pixels, (BITMAPINFO *)&info, DIB_RGB_COLORS);

    // Cleanup GDI resources
    DeleteObject(bitmap);
    DeleteDC(memDC);
    ReleaseDC(NULL, screenDC);

    return pixels;
}
#endif

// 🔥 Linux: Capture screen using X11 (fast!)
#ifndef _WIN32
unsigned char *captureScreen(int &size)
{
    Display *display = XOpenDisplay(NULL);    // Connect to X server
    Window root = DefaultRootWindow(display); // Root window = full screen

    // Capture screen image (ZPixmap = fast format)
    XImage *image = XGetImage(display, root, 0, 0, WIDTH, HEIGHT,
                              AllPlanes, ZPixmap);

    size = WIDTH * HEIGHT * 3; // RGB output
    unsigned char *pixels = new unsigned char[size];

    // Convert X11 pixels to RGB (handles different depth formats)
    for (int y = 0; y < HEIGHT; y++)
    {
        for (int x = 0; x < WIDTH; x++)
        {
            unsigned long pixel = XGetPixel(image, x, y);
            int idx = (y * WIDTH + x) * 3;
            pixels[idx + 0] = (pixel >> 16) & 0xFF; // Red
            pixels[idx + 1] = (pixel >> 8) & 0xFF;  // Green
            pixels[idx + 2] = pixel & 0xFF;         // Blue
        }
    }

    XDestroyImage(image);
    XCloseDisplay(display);
    return pixels;
}
#endif

int main()
{
    initSockets();

    // === CREATE SOCKET & CONNECT ===
    std::cout << "🎥 Starting screen capture..." << std::endl;
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    server.sin_addr.s_addr = inet_addr(SERVER_IP);

    if (connect(socket_fd, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        std::cerr << "❌ Cannot connect to receiver! Run screenshare_receiver first!" << std::endl;
        return 1;
    }
    std::cout << "✅ Connected to receiver!" << std::endl;

    // === 10 FPS SCREEN STREAMING LOOP ===
    auto lastFrame = std::chrono::steady_clock::now();
    int frameCount = 0;

    while (true)
    {
        // 1. CAPTURE SCREEN
        int frameSize;
        unsigned char *frame = captureScreen(frameSize);

        // 2. SEND FRAME SIZE FIRST (4 bytes - network byte order)
        uint32_t size_net = htonl(frameSize);
        send(socket_fd, &size_net, 4, 0);

        // 3. SEND RAW PIXEL DATA
        send(socket_fd, frame, frameSize, 0);
        delete[] frame;

        frameCount++;

        // 4. 10 FPS LIMIT (100ms per frame)
        auto now = std::chrono::steady_clock::now();
        if (now - lastFrame < std::chrono::milliseconds(100))
        {
            std::this_thread::sleep_until(lastFrame + std::chrono::milliseconds(100));
        }
        lastFrame = now;

        // FPS counter
        if (frameCount % 100 == 0)
        {
            std::cout << "📊 Sent " << frameCount << " frames" << std::endl;
        }
    }

#ifdef _WIN32
    closesocket(socket_fd);
#else
    close(socket_fd);
#endif
    return 0;
}
