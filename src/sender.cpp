/**
 * SENDER.CPP - SIMPLIFIED & FIXED
 */
#include <iostream>
#include <cstring>
#include <chrono>
#include <thread>
#include <vector>
#include "discover.h"

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720
#define TARGET_FPS 10
#define BYTES_PER_PIXEL 3

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
        if (sock >= 0)
        {
#ifdef _WIN32
            closesocket(sock);
            WSACleanup();
#else
            close(sock);
#endif
        }
    }

    bool connect(const std::string &ip, int port)
    {
#ifdef _WIN32
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
        sock = socket(AF_INET, SOCK_STREAM, 0);

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

        return ::connect(sock, (sockaddr *)&addr, sizeof(addr)) >= 0;
    }

    bool send(const void *data, size_t size)
    {
        size_t total = 0;
        while (total < size)
        {
            ssize_t sent = ::send(sock, (const char *)data + total, size - total, 0);
            if (sent <= 0)
                return false;
            total += sent;
        }
        return true;
    }
};

#ifdef _WIN32
std::vector<uint8_t> captureScreen()
{
    std::vector<uint8_t> pixels(SCREEN_WIDTH * SCREEN_HEIGHT * BYTES_PER_PIXEL);
    HDC screen = GetDC(NULL);
    HDC memdc = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, SCREEN_WIDTH, SCREEN_HEIGHT);

    SelectObject(memdc, bmp);
    BitBlt(memdc, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, screen, 0, 0, SRCCOPY);

    BITMAPINFOHEADER bi = {0};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = SCREEN_WIDTH;
    bi.biHeight = -SCREEN_HEIGHT;
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;

    GetDIBits(memdc, bmp, 0, SCREEN_HEIGHT, pixels.data(), (BITMAPINFO *)&bi, DIB_RGB_COLORS);

    DeleteObject(bmp);
    DeleteDC(memdc);
    ReleaseDC(NULL, screen);
    return pixels;
}
#else
std::vector<uint8_t> captureScreen()
{
    std::vector<uint8_t> pixels(SCREEN_WIDTH * SCREEN_HEIGHT * BYTES_PER_PIXEL);
    Display *display = XOpenDisplay(NULL);
    if (!display)
        return pixels;

    Window root = DefaultRootWindow(display);
    XImage *img = XGetImage(display, root, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, AllPlanes, ZPixmap);

    for (int y = 0; y < SCREEN_HEIGHT; y++)
    {
        for (int x = 0; x < SCREEN_WIDTH; x++)
        {
            unsigned long pixel = XGetPixel(img, x, y);
            size_t idx = (y * SCREEN_WIDTH + x) * BYTES_PER_PIXEL;
            pixels[idx + 0] = (pixel >> 16) & 0xFF;
            pixels[idx + 1] = (pixel >> 8) & 0xFF;
            pixels[idx + 2] = pixel & 0xFF;
        }
    }

    XDestroyImage(img);
    XCloseDisplay(display);
    return pixels;
}
#endif

int main()
{
    std::cout << "🎥 SCREEN SENDER 720p@" << TARGET_FPS << "FPS" << std::endl;

    auto receivers = discoverReceivers();
    if (receivers.empty())
    {
        std::cerr << "❌ No receivers found! Run receiver first." << std::endl;
        return 1;
    }

    std::cout << listDevices(receivers);
    size_t choice;
    std::cout << "Select (0-" << receivers.size() - 1 << "): ";
    std::cin >> choice;

    if (choice >= receivers.size())
        return 1;

    NetworkSocket conn;
    if (!conn.connect(receivers[choice].ip_address, receivers[choice].tcp_port))
    {
        std::cerr << "❌ Connection failed!" << std::endl;
        return 1;
    }

    std::cout << "🎬 Streaming to " << receivers[choice].toString() << std::endl;

    auto last_time = std::chrono::steady_clock::now();
    int frames = 0;

    while (true)
    {
        auto frame = captureScreen();
        uint32_t size = frame.size();
        uint32_t net_size = htonl(size);

        if (!conn.send(&net_size, 4) || !conn.send(frame.data(), size))
            break;

        frames++;
        auto target = last_time + std::chrono::milliseconds(100);
        if (std::chrono::steady_clock::now() < target)
        {
            std::this_thread::sleep_until(target);
        }
        last_time = std::chrono::steady_clock::now();

        if (frames % 50 == 0)
            std::cout << "📊 " << frames << " frames" << std::endl;
    }

    std::cout << "🔌 Stream ended" << std::endl;
    return 0;
}
