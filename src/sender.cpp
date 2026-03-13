/**
 * SENDER.CPP - SCREEN CAPTURE, STREAMING, AND VIRTUAL DISPLAY EXTENSION
 *
 * Cross-platform: Windows (GDI), Linux (X11), macOS (CoreGraphics).
 *
 * Screen Extender Mode
 * --------------------
 * Instead of mirroring, the receiver is treated as a second monitor placed
 * to the RIGHT of the sender's display. The receiver opens a borderless
 * fullscreen window at its own (0,0), presenting as a natural extension.
 *
 * Interactive Features:
 * - Press 'p' during streaming to open Port Inspector
 * - Press 's' during streaming to open Storage Manager
 * - Press 'g' during streaming to view GPU/CPU statistics
 * - Press 'm' during streaming to show main menu
 * - Press 't' to toggle GPU offload
 * - Press 'q' to quit streaming
 */

#include <iostream>
#include <cstring>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <iomanip>
#include <fstream>
#include <cstdint>
#include <algorithm>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include "discover.h"
#include "gpu_accelerate.h"
#include "ports.h"
#include "storage.h"

/* ── Platform headers ───────────────────────────────────────────────────── */
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mstcpip.h>
#pragma comment(lib, "ws2_32.lib")
#elif defined(__APPLE__)
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <CoreGraphics/CoreGraphics.h>
#else /* Linux */
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

#include <SDL2/SDL.h>
/* SDL2_image: required for rcorp.jpeg splash (install libsdl2-image-dev) */
#if __has_include(<SDL2/SDL_image.h>)
#include <SDL2/SDL_image.h>
#define HAVE_SDL_IMAGE 1
#else
#define HAVE_SDL_IMAGE 0
#warning "SDL2_image not found – splash will use fallback rectangle."
#warning "Fix: sudo apt install libsdl2-image-dev  (or: make install-sdl2-image)"
#endif /* PNG + JPEG support */

/* ── Display mode ───────────────────────────────────────────────────────── */
#define MODE_MIRROR 0u
#define MODE_EXTEND_RIGHT 1u
#define MODE_EXTEND_BELOW 2u

/* ── Constants ──────────────────────────────────────────────────────────── */
#define BYTES_PER_PIXEL 3
#define CONNECTION_TIMEOUT_MS 5000
#define STATS_INTERVAL_SEC 5
#define MAX_FRAME_SKIP 3
#define MAX_FRAME_BYTES (7680u * 4320u * 3u)

static const int SOCK_BUF = 4 * 1024 * 1024;

/* ── ANSI colours (Windows CMD does not support VT100 by default) ────────── */
#ifdef _WIN32
#define COL_RESET ""
#define COL_RED ""
#define COL_GREEN ""
#define COL_YELLOW ""
#define COL_CYAN ""
#define COL_MAGENTA ""
#define COL_BOLD ""
#else
#define COL_RESET "\033[0m"
#define COL_RED "\033[31m"
#define COL_GREEN "\033[32m"
#define COL_YELLOW "\033[33m"
#define COL_CYAN "\033[36m"
#define COL_MAGENTA "\033[35m"
#define COL_BOLD "\033[1m"
#endif

/* ── Globals ─────────────────────────────────────────────────────────────── */
static int SCREEN_WIDTH = 1920;
static int SCREEN_HEIGHT = 1080;
static int TARGET_FPS = 60;
static uint32_t DISPLAY_MODE = MODE_EXTEND_RIGHT; /* default: extend */

/* Receiver display dimensions (filled after handshake response) */
static int RECV_WIDTH = 1920;
static int RECV_HEIGHT = 1080;

static std::atomic<bool> g_running{true};
static std::atomic<bool> g_menu_active{false};

/* GPU offload */
static gpu_sock_t g_gpu_sock = GPU_INVALID_SOCK;
static bool g_gpu_active = false;

/* Port inspection */
static ports_sock_t g_ports_sock = PORTS_INVALID_SOCK;

/* Storage access */
static storage_sock_t g_storage_sock = STORAGE_INVALID_SOCK;

/* ══════════════════════════════════════════════════════════════════════════
 * SIGNAL HANDLER
 * ══════════════════════════════════════════════════════════════════════════ */
static void signalHandler(int)
{
    g_running = false;
}

/* ══════════════════════════════════════════════════════════════════════════
 * SPLASH SCREEN  –  rcorp.jpeg preferred, RGM.png fallback
 * ══════════════════════════════════════════════════════════════════════════ */
static void showSplashScreen()
{
    std::cout << COL_CYAN << COL_BOLD
              << "========================================\n"
                 "        RGM SENDER v2.0.2               \n"
                 "========================================\n"
              << COL_RESET;

    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        std::cerr << COL_YELLOW << "Splash: SDL_Init: "
                  << SDL_GetError() << COL_RESET << "\n";
        return;
    }

    /* Enable PNG + JPEG loading via SDL_image */
#if HAVE_SDL_IMAGE
    int img_flags = IMG_INIT_PNG | IMG_INIT_JPG;
    if ((IMG_Init(img_flags) & img_flags) != img_flags)
        std::cerr << COL_YELLOW << "Splash: SDL_image partial init\n"
                  << COL_RESET;
#endif

    SDL_Window *win = SDL_CreateWindow(
        "RGM", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        480, 300, SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP);
    if (!win)
    {
#if HAVE_SDL_IMAGE
        IMG_Quit();
#endif
        SDL_Quit();
        return;
    }

    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren)
    {
        SDL_DestroyWindow(win);
#if HAVE_SDL_IMAGE
        IMG_Quit();
#endif
        SDL_Quit();
        return;
    }

    /* Dark background */
    SDL_SetRenderDrawColor(ren, 12, 12, 20, 255);
    SDL_RenderClear(ren);

    /*
     * Search order: rcorp.jpeg FIRST (brand logo), then RGM.png fallback.
     * Paths cover: running from project root, from build/, and installed.
     */
    const char *paths[] = {
        "../assets/icons/rcorp.jpeg", /* running from build/     */
        "assets/icons/rcorp.jpeg",    /* running from project root */
        "../assets/icons/RGM.png",
        "assets/icons/RGM.png",
#ifndef _WIN32
        "/usr/share/rgm/icons/rcorp.jpeg",
        "/usr/share/rgm/icons/RGM.png",
#endif
        nullptr};

    SDL_Surface *img = nullptr;
#if HAVE_SDL_IMAGE
    for (int i = 0; paths[i] && !img; i++)
    {
        img = IMG_Load(paths[i]); /* handles JPEG and PNG natively */
        if (img)
            std::cout << COL_GREEN << "Splash: " << paths[i]
                      << COL_RESET << "\n";
    }
#endif

    if (!img)
    {
        /* Fallback: rcorp-styled blue rectangle */
        SDL_SetRenderDrawColor(ren, 20, 60, 140, 255);
        SDL_Rect fill = {30, 30, 420, 240};
        SDL_RenderFillRect(ren, &fill);
        SDL_SetRenderDrawColor(ren, 0, 180, 255, 255);
        SDL_Rect border = {28, 28, 424, 244};
        SDL_RenderDrawRect(ren, &border);
    }
    else
    {
        SDL_Texture *tex = SDL_CreateTextureFromSurface(ren, img);
        SDL_FreeSurface(img);
        if (tex)
        {
            int tw, th;
            SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
            /* Scale to fit 460×280 keeping aspect ratio */
            float sc = std::min(460.0f / (float)tw, 280.0f / (float)th);
            SDL_Rect dst;
            dst.w = (int)(tw * sc);
            dst.h = (int)(th * sc);
            dst.x = (480 - dst.w) / 2;
            dst.y = (300 - dst.h) / 2;
            SDL_RenderCopy(ren, tex, nullptr, &dst);
            SDL_DestroyTexture(tex);
        }
    }

    SDL_RenderPresent(ren);
    SDL_Delay(2200);

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
#if HAVE_SDL_IMAGE
    IMG_Quit();
#endif
    SDL_Quit();
}

/* ══════════════════════════════════════════════════════════════════════════
 * SCREEN DIMENSIONS
 * ══════════════════════════════════════════════════════════════════════════ */
static void getScreenDimensions(int &w, int &h)
{
#ifdef _WIN32
    w = GetSystemMetrics(SM_CXSCREEN);
    h = GetSystemMetrics(SM_CYSCREEN);
    std::cout << "Display: " << w << "x" << h << " (GDI)\n";

#elif defined(__APPLE__)
    CGDisplayModeRef mode = CGDisplayCopyDisplayMode(kCGDirectMainDisplay);
    if (mode)
    {
        w = (int)CGDisplayModeGetWidth(mode);
        h = (int)CGDisplayModeGetHeight(mode);
        CGDisplayModeRelease(mode);
        std::cout << "Display: " << w << "x" << h << " (CoreGraphics)\n";
    }
    else
    {
        w = 1920;
        h = 1080;
        std::cerr << COL_YELLOW
                  << "CoreGraphics query failed; using 1920x1080\n"
                  << COL_RESET;
    }

#else /* Linux / X11 */
    Display *dpy = XOpenDisplay(nullptr);
    if (dpy)
    {
        int sn = DefaultScreen(dpy);
        w = DisplayWidth(dpy, sn);
        h = DisplayHeight(dpy, sn);
        XCloseDisplay(dpy);
        std::cout << "Display: " << w << "x" << h << " (X11)\n";
    }
    else
    {
        w = 1920;
        h = 1080;
        std::cerr << COL_YELLOW
                  << "X11 open failed; defaulting to 1920x1080\n"
                  << COL_RESET;
    }
#endif
}

/* ══════════════════════════════════════════════════════════════════════════
 * NETWORK SOCKET
 * ══════════════════════════════════════════════════════════════════════════ */
class NetworkSocket
{
#ifdef _WIN32
    SOCKET _s = INVALID_SOCKET;
    bool valid() const { return _s != INVALID_SOCKET; }
    void raw_close()
    {
        closesocket(_s);
        _s = INVALID_SOCKET;
    }
#else
    int _s = -1;
    bool valid() const { return _s >= 0; }
    void raw_close()
    {
        ::close(_s);
        _s = -1;
    }
#endif

public:
    ~NetworkSocket() { close(); }

    void close()
    {
        if (valid())
            raw_close();
    }

    bool create()
    {
        close();
        _s = socket(AF_INET, SOCK_STREAM, 0);
#ifdef _WIN32
        return _s != INVALID_SOCKET;
#else
        return _s >= 0;
#endif
    }

    bool connect(const std::string &ip, int port,
                 int timeout_ms = CONNECTION_TIMEOUT_MS)
    {
        if (!create())
            return false;

        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons((uint16_t)port);
        if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1)
        {
            std::cerr << COL_RED << "Invalid IP: " << ip << COL_RESET << "\n";
            return false;
        }

        /* Non-blocking connect with timeout */
#ifdef _WIN32
        u_long nb = 1;
        ioctlsocket(_s, FIONBIO, &nb);
#else
        int fl = fcntl(_s, F_GETFL, 0);
        fcntl(_s, F_SETFL, fl | O_NONBLOCK);
#endif
        ::connect(_s, (struct sockaddr *)&addr, sizeof(addr));

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(_s, &fds);
        struct timeval tv = {timeout_ms / 1000,
                             (timeout_ms % 1000) * 1000};
        bool ok = false;
        if (select((int)_s + 1, nullptr, &fds, nullptr, &tv) == 1)
        {
            int err = 0;
            socklen_t len = sizeof(err);
            getsockopt(_s, SOL_SOCKET, SO_ERROR, (char *)&err, &len);
            ok = (err == 0);
        }

#ifdef _WIN32
        {
            u_long bl = 0;
            ioctlsocket(_s, FIONBIO, &bl);
        }
#else
        fcntl(_s, F_SETFL, fl);
#endif
        if (!ok)
        {
            std::cerr << COL_RED << "Timeout: " << ip << ":"
                      << port << COL_RESET << "\n";
            close();
            return false;
        }

        int nd = 1, sb = SOCK_BUF;
        setsockopt(_s, IPPROTO_TCP, TCP_NODELAY, (char *)&nd, sizeof(nd));
        setsockopt(_s, SOL_SOCKET, SO_SNDBUF, (char *)&sb, sizeof(sb));
        std::cout << COL_GREEN << "Connected to "
                  << ip << ":" << port << COL_RESET << "\n";
        return true;
    }

    bool sendAll(const void *data, size_t size)
    {
        const char *p = (const char *)data;
        size_t sent = 0;
        while (sent < size && g_running)
        {
            int n = (int)send(_s, p + sent, (int)(size - sent), 0);
#ifdef _WIN32
            if (n == SOCKET_ERROR)
            {
                std::cerr << COL_RED << "Send error: "
                          << WSAGetLastError() << COL_RESET << "\n";
                return false;
            }
#else
            if (n < 0)
            {
                std::cerr << COL_RED << "Send error: "
                          << strerror(errno) << COL_RESET << "\n";
                return false;
            }
#endif
            if (n == 0)
                return false;
            sent += (size_t)n;
        }
        return sent == size;
    }

    bool recvAll(void *data, size_t size)
    {
        char *p = (char *)data;
        size_t got = 0;
        while (got < size)
        {
            int n = (int)recv(_s, p + got, (int)(size - got), 0);
            if (n <= 0)
                return false;
            got += (size_t)n;
        }
        return true;
    }
};

/* ══════════════════════════════════════════════════════════════════════════
 * SCREEN CAPTURE  –  platform-specific, always returns RGB24
 * ══════════════════════════════════════════════════════════════════════════ */
#ifdef _WIN32
static std::vector<uint8_t> captureScreen()
{
    std::vector<uint8_t> px(SCREEN_WIDTH * SCREEN_HEIGHT * BYTES_PER_PIXEL, 0);
    HDC sdc = GetDC(nullptr);
    if (!sdc)
        return px;
    HDC mdc = CreateCompatibleDC(sdc);
    HBITMAP bmp = CreateCompatibleBitmap(sdc, SCREEN_WIDTH, SCREEN_HEIGHT);
    if (!bmp)
    {
        DeleteDC(mdc);
        ReleaseDC(nullptr, sdc);
        return px;
    }
    SelectObject(mdc, bmp);
    BitBlt(mdc, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, sdc, 0, 0,
           SRCCOPY | CAPTUREBLT);
    BITMAPINFOHEADER bi = {};
    bi.biSize = sizeof(bi);
    bi.biWidth = SCREEN_WIDTH;
    bi.biHeight = -SCREEN_HEIGHT; /* top-down, no flip needed */
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;
    GetDIBits(mdc, bmp, 0, SCREEN_HEIGHT, px.data(),
              (BITMAPINFO *)&bi, DIB_RGB_COLORS);
    DeleteObject(bmp);
    DeleteDC(mdc);
    ReleaseDC(nullptr, sdc);
    /* GDI returns BGR; swap to RGB for network transmission */
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++)
        std::swap(px[i * 3], px[i * 3 + 2]);
    return px;
}

#elif defined(__APPLE__)
static std::vector<uint8_t> captureScreen()
{
    std::vector<uint8_t> px(SCREEN_WIDTH * SCREEN_HEIGHT * BYTES_PER_PIXEL, 0);
    CGImageRef img = CGDisplayCreateImage(kCGDirectMainDisplay);
    if (!img)
        return px;
    CGDataProviderRef dp = CGImageGetDataProvider(img);
    CFDataRef raw = CGDataProviderCopyData(dp);
    if (!raw)
    {
        CGImageRelease(img);
        return px;
    }
    const uint8_t *src = CFDataGetBytePtr(raw);
    size_t bpr = CGImageGetBytesPerRow(img);
    size_t bpp = CGImageGetBitsPerPixel(img) / 8; /* usually 4: BGRA */
    for (int y = 0; y < SCREEN_HEIGHT; y++)
    {
        for (int x = 0; x < SCREEN_WIDTH; x++)
        {
            size_t si = y * bpr + x * bpp;
            size_t di = ((size_t)y * SCREEN_WIDTH + x) * 3;
            px[di + 0] = src[si + 2]; /* R */
            px[di + 1] = src[si + 1]; /* G */
            px[di + 2] = src[si + 0]; /* B */
        }
    }
    CFRelease(raw);
    CGImageRelease(img);
    return px;
}

#else /* Linux / X11 */
static std::vector<uint8_t> captureScreen()
{
    std::vector<uint8_t> px(SCREEN_WIDTH * SCREEN_HEIGHT * BYTES_PER_PIXEL, 0);
    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy)
        return px;
    Window root = RootWindow(dpy, DefaultScreen(dpy));
    XImage *xi = XGetImage(dpy, root, 0, 0,
                           SCREEN_WIDTH, SCREEN_HEIGHT, AllPlanes, ZPixmap);
    if (!xi)
    {
        XCloseDisplay(dpy);
        return px;
    }
    for (int y = 0; y < SCREEN_HEIGHT; y++)
    {
        for (int x = 0; x < SCREEN_WIDTH; x++)
        {
            unsigned long p = XGetPixel(xi, x, y);
            size_t i = ((size_t)y * SCREEN_WIDTH + x) * 3;
            px[i + 0] = (uint8_t)((p >> 16) & 0xFF); /* R */
            px[i + 1] = (uint8_t)((p >> 8) & 0xFF);  /* G */
            px[i + 2] = (uint8_t)(p & 0xFF);         /* B */
        }
    }
    XDestroyImage(xi);
    XCloseDisplay(dpy);
    return px;
}
#endif

/* ══════════════════════════════════════════════════════════════════════════
 * DISPLAY MODE SELECTION
 * ══════════════════════════════════════════════════════════════════════════ */
static uint32_t selectDisplayMode()
{
    std::cout << "\n"
              << COL_CYAN << COL_BOLD
              << "  Select display mode:\n"
              << COL_RESET
              << "  " << COL_GREEN << "1" << COL_RESET
              << "  Extend Right  (receiver = right monitor)\n"
              << "  " << COL_YELLOW << "2" << COL_RESET
              << "  Extend Below  (receiver = bottom monitor)\n"
              << "  " << COL_CYAN << "3" << COL_RESET
              << "  Mirror        (duplicate screen)\n"
              << COL_BOLD << "  Choice [1]: " << COL_RESET;

    std::string line;
    /* consume leftover newline from previous cin >> */
    if (std::cin.peek() == '\n')
        std::cin.ignore();
    std::getline(std::cin, line);

    if (line == "2")
        return MODE_EXTEND_BELOW;
    if (line == "3")
        return MODE_MIRROR;
    return MODE_EXTEND_RIGHT; /* default */
}

/* ══════════════════════════════════════════════════════════════════════════
 * STATS
 * ══════════════════════════════════════════════════════════════════════════ */
static void showStats(int frames, long elapsed, size_t bytes)
{
    if (elapsed < 1)
        elapsed = 1;
    float fps = (float)frames / (float)elapsed;
    float mbps = (float)(bytes / (1024.0 * 1024.0)) / (float)elapsed;
    const char *modeStr =
        DISPLAY_MODE == MODE_EXTEND_RIGHT ? "extend-right" : DISPLAY_MODE == MODE_EXTEND_BELOW ? "extend-below"
                                                                                               : "mirror";

    std::cout << COL_CYAN
              << "Frames: " << frames
              << "  FPS: " << std::fixed << std::setprecision(1) << fps
              << "/" << TARGET_FPS
              << "  BW: " << std::setprecision(2) << mbps << " MB/s"
              << "  Src: " << SCREEN_WIDTH << "x" << SCREEN_HEIGHT
              << "  Dst: " << RECV_WIDTH << "x" << RECV_HEIGHT
              << "  Mode: " << modeStr
              << (g_gpu_active ? "  GPU:remote" : "  GPU:local")
              << COL_RESET << "\n";
}

/* ══════════════════════════════════════════════════════════════════════════
 * INTERACTIVE MENU FUNCTIONS
 * ══════════════════════════════════════════════════════════════════════════ */

/**
 * GPU interactive statistics display
 */
static void gpu_interactive_stats()
{
    std::cout << COL_CYAN << COL_BOLD
              << "\n╔════════════════════════════════════════╗\n"
              << "║        GPU OFFLOAD STATISTICS         ║\n"
              << "╠════════════════════════════════════════╣\n"
              << COL_RESET;

    if (GPU_SOCK_VALID(g_gpu_sock))
    {
        gpu_print_stats(g_gpu_sock);
    }
    else
    {
        std::cout << COL_YELLOW << "  GPU service not connected\n"
                  << COL_RESET;
    }

    std::cout << COL_YELLOW << "\nPress Enter to continue..." << COL_RESET;
    std::cin.get();
}

/**
 * Print port entries in formatted table
 */
static void print_port_entries(const PortEntry *entries, uint32_t count)
{
    if (count == 0)
    {
        std::cout << COL_YELLOW << "  (no entries)\n"
                  << COL_RESET;
        return;
    }
    std::cout << COL_CYAN << COL_BOLD
              << std::left
              << std::setw(5) << "Proto"
              << std::setw(25) << "Local Address"
              << std::setw(25) << "Remote Address"
              << std::setw(15) << "State"
              << std::setw(8) << "PID"
              << "Process\n"
              << std::string(90, '-') << "\n"
              << COL_RESET;

    for (uint32_t i = 0; i < count; i++)
    {
        std::string local = ports_ip_to_string(entries[i].local_ip, entries[i].ip_ver) +
                            ":" + std::to_string(entries[i].local_port);
        std::string remote = (entries[i].remote_port != 0) ? ports_ip_to_string(entries[i].remote_ip, entries[i].ip_ver) +
                                                                 ":" + std::to_string(entries[i].remote_port)
                                                           : "*:*";

        std::cout << std::left
                  << std::setw(5) << (entries[i].proto == 0 ? "TCP" : "UDP")
                  << std::setw(25) << local
                  << std::setw(25) << remote
                  << std::setw(15) << ports_state_name(entries[i].state)
                  << std::setw(8) << entries[i].pid
                  << entries[i].process
                  << "\n";
    }
    std::cout << COL_GREEN << "  Total: " << count << " entries\n"
              << COL_RESET;
}

/**
 * Port inspector interactive menu
 */
static void ports_interactive_menu()
{
    if (!PORTS_SOCK_VALID(g_ports_sock))
    {
        std::cout << COL_RED << "Port service not connected.\n"
                  << COL_RESET;
        return;
    }

    while (true)
    {
        std::cout << COL_CYAN << COL_BOLD
                  << "\n╔════════════════════════════════════════════════╗\n"
                  << "║           RECEIVER PORT INSPECTOR             ║\n"
                  << "╠════════════════════════════════════════════════╣\n"
                  << "║  1. List all TCP ports                        ║\n"
                  << "║  2. List all UDP ports                        ║\n"
                  << "║  3. List ALL ports                            ║\n"
                  << "║  4. Query specific port                       ║\n"
                  << "║  5. Kill process on port                      ║\n"
                  << "║  6. Show listening ports only                 ║\n"
                  << "║  7. Show established connections              ║\n"
                  << "║  8. Refresh statistics                        ║\n"
                  << "║  0. Back to stream                            ║\n"
                  << "╚════════════════════════════════════════════════╝\n"
                  << COL_RESET
                  << COL_BOLD << "Choice: " << COL_RESET;

        std::string line;
        std::getline(std::cin, line);
        if (line.empty())
            continue;

        int choice = -1;
        try
        {
            choice = std::stoi(line);
        }
        catch (...)
        {
        }

        if (choice == 0)
            break;

        PortEntry *entries = nullptr;
        uint32_t count = 0;
        int result = 0;

        switch (choice)
        {
        case 1:
            std::cout << COL_CYAN << "\n── TCP Ports ──\n"
                      << COL_RESET;
            result = ports_remote_list_tcp(g_ports_sock, &entries, &count);
            if (result >= 0)
                print_port_entries(entries, count);
            else
                std::cout << COL_RED << "Failed to list TCP ports\n"
                          << COL_RESET;
            ports_free_entries(entries);
            break;

        case 2:
            std::cout << COL_CYAN << "\n── UDP Ports ──\n"
                      << COL_RESET;
            result = ports_remote_list_udp(g_ports_sock, &entries, &count);
            if (result >= 0)
                print_port_entries(entries, count);
            else
                std::cout << COL_RED << "Failed to list UDP ports\n"
                          << COL_RESET;
            ports_free_entries(entries);
            break;

        case 3:
            std::cout << COL_CYAN << "\n── All Ports ──\n"
                      << COL_RESET;
            result = ports_remote_list_all(g_ports_sock, &entries, &count);
            if (result >= 0)
                print_port_entries(entries, count);
            else
                std::cout << COL_RED << "Failed to list ports\n"
                          << COL_RESET;
            ports_free_entries(entries);
            break;

        case 4:
        {
            std::cout << COL_BOLD << "Enter port number: " << COL_RESET;
            std::string p;
            std::getline(std::cin, p);
            int pn = -1;
            try
            {
                pn = std::stoi(p);
            }
            catch (...)
            {
            }
            if (pn < 1 || pn > 65535)
            {
                std::cout << COL_RED << "Invalid port\n"
                          << COL_RESET;
                break;
            }
            int r = ports_remote_get_port(g_ports_sock, (uint16_t)pn, &entries, &count);
            if (r < 0)
            {
                std::cout << COL_RED << "Query failed\n"
                          << COL_RESET;
            }
            else
            {
                std::cout << COL_CYAN << "\n── Port " << pn << " ──\n"
                          << COL_RESET;
                print_port_entries(entries, count);
                ports_free_entries(entries);
            }
            break;
        }

        case 5:
        {
            std::cout << COL_BOLD << "Enter port number to kill: " << COL_RESET;
            std::string p;
            std::getline(std::cin, p);
            int pn = -1;
            try
            {
                pn = std::stoi(p);
            }
            catch (...)
            {
            }
            if (pn < 1 || pn > 65535)
            {
                std::cout << COL_RED << "Invalid port\n"
                          << COL_RESET;
                break;
            }
            std::cout << COL_YELLOW
                      << "⚠  Kill process on port " << pn
                      << " on the RECEIVER? [y/N]: " << COL_RESET;
            std::string confirm;
            std::getline(std::cin, confirm);
            if (confirm == "y" || confirm == "Y")
            {
                int r = ports_remote_kill_port(g_ports_sock, (uint16_t)pn);
                if (r == 0)
                    std::cout << COL_GREEN << "Kill signal sent\n"
                              << COL_RESET;
                else
                    std::cout << COL_RED << "Kill failed or no process found\n"
                              << COL_RESET;
            }
            else
            {
                std::cout << "Cancelled\n";
            }
            break;
        }

        case 6:
        {
            std::cout << COL_CYAN << "\n── Listening Ports ──\n"
                      << COL_RESET;
            result = ports_remote_list_all(g_ports_sock, &entries, &count);
            if (result >= 0)
            {
                std::vector<PortEntry> filtered;
                for (uint32_t i = 0; i < count; i++)
                {
                    if (entries[i].state == PSTATE_LISTEN)
                        filtered.push_back(entries[i]);
                }
                print_port_entries(filtered.data(), (uint32_t)filtered.size());
            }
            ports_free_entries(entries);
            break;
        }

        case 7:
        {
            std::cout << COL_CYAN << "\n── Established Connections ──\n"
                      << COL_RESET;
            result = ports_remote_list_all(g_ports_sock, &entries, &count);
            if (result >= 0)
            {
                std::vector<PortEntry> filtered;
                for (uint32_t i = 0; i < count; i++)
                {
                    if (entries[i].state == PSTATE_ESTABLISHED)
                        filtered.push_back(entries[i]);
                }
                print_port_entries(filtered.data(), (uint32_t)filtered.size());
            }
            ports_free_entries(entries);
            break;
        }

        case 8:
            std::cout << COL_GREEN << "Statistics refreshed\n"
                      << COL_RESET;
            break;

        default:
            std::cout << COL_RED << "Invalid choice\n"
                      << COL_RESET;
        }

        if (choice != 8)
        {
            std::cout << COL_YELLOW << "\nPress Enter to continue..." << COL_RESET;
            std::cin.get();
        }
    }
}

/**
 * Storage manager interactive menu
 */
static void storage_interactive_menu()
{
    if (!STORAGE_SOCK_VALID(g_storage_sock))
    {
        std::cout << COL_RED << "Storage service not connected.\n"
                  << COL_RESET;
        return;
    }

    std::string current_path =
#ifdef _WIN32
        "C:\\";
#else
        "/home/" + std::string(getenv("USER") ? getenv("USER") : "user");
#endif

    while (true)
    {
        std::cout << COL_CYAN << COL_BOLD
                  << "\n╔════════════════════════════════════════════════╗\n"
                  << "║           RECEIVER STORAGE MANAGER            ║\n"
                  << "╠════════════════════════════════════════════════╣\n"
                  << "║  Current path: " << std::left << std::setw(33)
                  << current_path << "║\n"
                  << "╠════════════════════════════════════════════════╣\n"
                  << "║  1. List current directory                    ║\n"
                  << "║  2. Change directory                          ║\n"
                  << "║  3. Read file                                 ║\n"
                  << "║  4. Upload file (write)                       ║\n"
                  << "║  5. Delete file                               ║\n"
                  << "║  6. Create directory                          ║\n"
                  << "║  7. Show drives/mount points                  ║\n"
                  << "║  8. File information                          ║\n"
                  << "║  0. Back to stream                            ║\n"
                  << "╚════════════════════════════════════════════════╝\n"
                  << COL_RESET
                  << COL_BOLD << "Choice: " << COL_RESET;

        std::string line;
        std::getline(std::cin, line);
        if (line.empty())
            continue;

        int choice = -1;
        try
        {
            choice = std::stoi(line);
        }
        catch (...)
        {
        }

        if (choice == 0)
            break;

        switch (choice)
        {
        case 1:
        {
            std::vector<StorageEntry> entries;
            int result = storage_remote_list_dir(g_storage_sock, current_path.c_str(), entries);
            if (result >= 0)
            {
                storage_print_directory(entries);
            }
            else
            {
                std::cout << COL_RED << "Failed to list directory (error: " << result << ")\n"
                          << COL_RESET;
            }
            break;
        }

        case 2:
        {
            std::cout << COL_BOLD << "Enter directory path: " << COL_RESET;
            std::string new_path;
            std::getline(std::cin, new_path);
            if (!new_path.empty())
            {
                // Verify directory exists
                std::vector<StorageEntry> entries;
                int result = storage_remote_list_dir(g_storage_sock, new_path.c_str(), entries);
                if (result >= 0)
                {
                    current_path = new_path;
                    std::cout << COL_GREEN << "Directory changed\n"
                              << COL_RESET;
                }
                else
                {
                    std::cout << COL_RED << "Directory does not exist or cannot be accessed\n"
                              << COL_RESET;
                }
            }
            break;
        }

        case 3:
        {
            std::cout << COL_BOLD << "Enter filename: " << COL_RESET;
            std::string filename;
            std::getline(std::cin, filename);

            std::string full_path = storage_path_join(current_path, filename);

            std::vector<uint8_t> data;
            int result = storage_remote_read_file(g_storage_sock, full_path.c_str(), 0, 0, data);
            if (result >= 0)
            {
                std::cout << COL_GREEN << "Read " << data.size()
                          << " bytes\n"
                          << COL_RESET;

                // Ask to save locally
                std::cout << "Save to local file? [y/N]: ";
                std::string save;
                std::getline(std::cin, save);
                if (save == "y" || save == "Y")
                {
                    std::ofstream out(filename, std::ios::binary);
                    if (out.is_open())
                    {
                        out.write((const char *)data.data(), data.size());
                        out.close();
                        std::cout << COL_GREEN << "Saved to " << filename << "\n"
                                  << COL_RESET;
                    }
                    else
                    {
                        std::cout << COL_RED << "Failed to save local file\n"
                                  << COL_RESET;
                    }
                }

                // Show first few bytes if text
                if (data.size() > 0)
                {
                    std::cout << "\nFirst 100 bytes:\n";
                    for (size_t i = 0; i < std::min((size_t)100, data.size()); i++)
                    {
                        if (isprint(data[i]))
                            std::cout << data[i];
                        else
                            std::cout << '.';
                    }
                    std::cout << "\n";
                }
            }
            else
            {
                std::cout << COL_RED << "Failed to read file (error: " << result << ")\n"
                          << COL_RESET;
            }
            break;
        }

        case 4:
        {
            std::cout << COL_BOLD << "Enter local filename to upload: " << COL_RESET;
            std::string local_file;
            std::getline(std::cin, local_file);

            std::ifstream in(local_file, std::ios::binary | std::ios::ate);
            if (!in.is_open())
            {
                std::cout << COL_RED << "Cannot open local file\n"
                          << COL_RESET;
                break;
            }

            uint32_t size = (uint32_t)in.tellg();
            in.seekg(0, std::ios::beg);
            std::vector<uint8_t> data(size);
            in.read((char *)data.data(), size);
            in.close();

            std::cout << COL_BOLD << "Enter remote filename (or press Enter for same name): " << COL_RESET;
            std::string remote_file;
            std::getline(std::cin, remote_file);
            if (remote_file.empty())
            {
                remote_file = local_file;
                // Extract just filename if path given
                size_t pos = remote_file.find_last_of("/\\");
                if (pos != std::string::npos)
                {
                    remote_file = remote_file.substr(pos + 1);
                }
            }

            std::string full_path = storage_path_join(current_path, remote_file);

            std::cout << "Uploading " << size << " bytes...\n";
            int result = storage_remote_write_file(g_storage_sock, full_path.c_str(),
                                                   data.data(), size, 0, false);
            if (result == STORAGE_OK)
            {
                std::cout << COL_GREEN << "Uploaded successfully\n"
                          << COL_RESET;
            }
            else
            {
                std::cout << COL_RED << "Upload failed (error: " << result << ")\n"
                          << COL_RESET;
            }
            break;
        }

        case 5:
        {
            std::cout << COL_BOLD << "Enter filename to delete: " << COL_RESET;
            std::string filename;
            std::getline(std::cin, filename);

            std::string full_path = storage_path_join(current_path, filename);

            std::cout << COL_YELLOW << "Delete " << full_path << "? [y/N]: " << COL_RESET;
            std::string confirm;
            std::getline(std::cin, confirm);

            if (confirm == "y" || confirm == "Y")
            {
                int result = storage_remote_delete_file(g_storage_sock, full_path.c_str());
                if (result == STORAGE_OK)
                {
                    std::cout << COL_GREEN << "Deleted successfully\n"
                              << COL_RESET;
                }
                else
                {
                    std::cout << COL_RED << "Delete failed (error: " << result << ")\n"
                              << COL_RESET;
                }
            }
            break;
        }

        case 6:
        {
            std::cout << COL_BOLD << "Enter directory name to create: " << COL_RESET;
            std::string dirname;
            std::getline(std::cin, dirname);

            std::string full_path = storage_path_join(current_path, dirname);

            int result = storage_remote_mkdir(g_storage_sock, full_path.c_str());
            if (result == STORAGE_OK)
            {
                std::cout << COL_GREEN << "Directory created\n"
                          << COL_RESET;
            }
            else
            {
                std::cout << COL_RED << "Failed to create directory (error: " << result << ")\n"
                          << COL_RESET;
            }
            break;
        }

        case 7:
        {
            std::vector<std::string> drives;
            int result = storage_remote_get_drives(g_storage_sock, drives);
            if (result >= 0)
            {
                std::cout << COL_CYAN << "\nAvailable drives/mount points:\n"
                          << COL_RESET;
                for (const auto &drive : drives)
                {
                    std::cout << "  " << drive << "\n";
                }
            }
            else
            {
                std::cout << COL_RED << "Failed to get drives (error: " << result << ")\n"
                          << COL_RESET;
            }
            break;
        }

        case 8:
        {
            std::cout << COL_BOLD << "Enter filename for info: " << COL_RESET;
            std::string filename;
            std::getline(std::cin, filename);

            std::string full_path = storage_path_join(current_path, filename);

            uint64_t size, free_space;
            uint32_t attributes;
            int result = storage_remote_get_info(g_storage_sock, full_path.c_str(),
                                                 &size, &free_space, &attributes);
            if (result == STORAGE_OK)
            {
                std::cout << COL_GREEN << "File information:\n"
                          << COL_RESET;
                std::cout << "  Size: " << storage_format_size(size) << "\n";
                if (free_space > 0)
                {
                    std::cout << "  Free space: " << storage_format_size(free_space) << "\n";
                }
                std::cout << "  Attributes: " << attributes << "\n";
            }
            else
            {
                std::cout << COL_RED << "Failed to get info (error: " << result << ")\n"
                          << COL_RESET;
            }
            break;
        }

        default:
            std::cout << COL_RED << "Invalid choice\n"
                      << COL_RESET;
        }

        if (choice >= 1 && choice <= 8)
        {
            std::cout << COL_YELLOW << "\nPress Enter to continue..." << COL_RESET;
            std::cin.get();
        }
    }
}

/**
 * Display the interactive control menu during streaming
 */
static void showInteractiveMenu(NetworkSocket &conn)
{
    g_menu_active = true;

    std::cout << COL_BOLD << COL_CYAN
              << "\n╔════════════════════════════════════════╗\n"
              << "║           RGM CONTROL MENU             ║\n"
              << "╠════════════════════════════════════════╣\n"
              << "║  [p] Port Inspector                    ║\n"
              << "║      - View all TCP/UDP ports          ║\n"
              << "║      - Kill processes by port          ║\n"
              << "║                                        ║\n"
              << "║  [s] Storage Manager                   ║\n"
              << "║      - Browse receiver filesystem      ║\n"
              << "║      - Read/write files                ║\n"
              << "║                                        ║\n"
              << "║  [g] GPU/CPU Statistics                ║\n"
              << "║      - View compression ratios         ║\n"
              << "║      - Monitor performance             ║\n"
              << "║                                        ║\n"
              << "║  [t] Toggle GPU offload (currently "
              << (g_gpu_active ? "ON " : "OFF") << ")        ║\n"
              << "║  [q] Quit streaming                    ║\n"
              << "║  [m] Show this menu                    ║\n"
              << "╚════════════════════════════════════════╝\n"
              << COL_RESET;

    std::cout << COL_BOLD << "Choice: " << COL_RESET;
    char choice;
    std::cin >> choice;
    std::cin.ignore();

    switch (choice)
    {
    case 'p':
    case 'P':
        ports_interactive_menu();
        break;

    case 's':
    case 'S':
        storage_interactive_menu();
        break;

    case 'g':
    case 'G':
        gpu_interactive_stats();
        break;

    case 't':
    case 'T':
        g_gpu_active = !g_gpu_active;
        std::cout << COL_GREEN << "GPU offload "
                  << (g_gpu_active ? "enabled" : "disabled")
                  << COL_RESET << "\n";
        std::this_thread::sleep_for(std::chrono::seconds(1));
        break;

    case 'q':
    case 'Q':
        std::cout << COL_YELLOW << "Stopping stream...\n"
                  << COL_RESET;
        g_running = false;
        break;

    case 'm':
    case 'M':
        /* Menu already shown */
        break;

    default:
        std::cout << COL_YELLOW << "Unknown command. Press 'm' for menu.\n"
                  << COL_RESET;
        std::this_thread::sleep_for(std::chrono::seconds(1));
        break;
    }

    g_menu_active = false;
}

/**
 * Check for user input during streaming without blocking
 * This allows pressing keys to open various tools
 */
static void checkForUserInput(NetworkSocket &conn)
{
    // Don't check if menu is already active
    if (g_menu_active)
        return;

    struct termios oldt, newt;
    int oldf;

    /* Save terminal settings */
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO); /* Disable canonical mode and echo */
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK); /* Non-blocking mode */

    int ch = getchar();
    if (ch != EOF)
    {
        switch (ch)
        {
        case 'p':
        case 'P':
            std::cout << "\n"
                      << COL_CYAN << COL_BOLD
                      << "\n╔════════════════════════════════╗\n"
                      << "║     OPENING PORT INSPECTOR     ║\n"
                      << "╚════════════════════════════════╝\n"
                      << COL_RESET;
            ports_interactive_menu();
            break;

        case 's':
        case 'S':
            std::cout << "\n"
                      << COL_CYAN << COL_BOLD
                      << "\n╔════════════════════════════════╗\n"
                      << "║     OPENING STORAGE MANAGER    ║\n"
                      << "╚════════════════════════════════╝\n"
                      << COL_RESET;
            storage_interactive_menu();
            break;

        case 'g':
        case 'G':
            std::cout << "\n"
                      << COL_CYAN << COL_BOLD
                      << "\n╔════════════════════════════════╗\n"
                      << "║     GPU/CPU STATISTICS         ║\n"
                      << "╚════════════════════════════════╝\n"
                      << COL_RESET;
            gpu_interactive_stats();
            break;

        case 'm':
        case 'M':
            showInteractiveMenu(conn);
            break;

        case 3: /* Ctrl+C */
            std::cout << COL_YELLOW << "\nCtrl+C detected. Stopping stream...\n"
                      << COL_RESET;
            g_running = false;
            break;
        }
    }

    /* Restore terminal settings */
    fcntl(STDIN_FILENO, F_SETFL, oldf);
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
}

/* ══════════════════════════════════════════════════════════════════════════
 * MAIN
 * ══════════════════════════════════════════════════════════════════════════ */
int main()
{
    /* Set up signal handler */
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    showSplashScreen();
    getScreenDimensions(SCREEN_WIDTH, SCREEN_HEIGHT);

    std::cout << COL_CYAN << COL_BOLD
              << "========================================\n"
                 "  RGM SENDER v2.0\n"
                 "========================================\n"
              << COL_RESET
              << "  Source: " << SCREEN_WIDTH << "x" << SCREEN_HEIGHT
              << "  @ " << TARGET_FPS << " FPS\n"
              << COL_CYAN << "========================================\n"
              << COL_RESET;

    if (!initSockets())
    {
        std::cerr << COL_RED << "Socket init failed\n"
                  << COL_RESET;
        return 1;
    }

    /* Discover receivers */
    std::cout << COL_CYAN << "Discovering receivers...\n"
              << COL_RESET;
    auto receivers = discoverReceivers(5);
    if (receivers.empty())
    {
        std::cerr << COL_RED
                  << "No receivers found!\n"
                     "  Check firewall: UDP 1900, TCP 8081, TCP 8082, TCP 8083, TCP 8084\n"
                  << COL_RESET;
        cleanupSockets();
        return 1;
    }
    std::cout << listDevices(receivers);

    /* Select receiver */
    size_t choice = 0;
    if (receivers.size() > 1)
    {
        std::cout << COL_BOLD << "Select receiver (0-"
                  << receivers.size() - 1 << "): " << COL_RESET;
        if (!(std::cin >> choice) || choice >= receivers.size())
        {
            std::cerr << COL_RED << "Invalid selection\n"
                      << COL_RESET;
            cleanupSockets();
            return 1;
        }
    }
    std::cin.ignore(); // Clear newline

    const auto &sel = receivers[choice];
    std::cout << COL_GREEN << "Selected: " << sel.toString()
              << COL_RESET << "\n";

    /* Choose display mode */
    DISPLAY_MODE = selectDisplayMode();
    const char *modeLabel =
        DISPLAY_MODE == MODE_EXTEND_RIGHT ? "Extend Right" : DISPLAY_MODE == MODE_EXTEND_BELOW ? "Extend Below"
                                                                                               : "Mirror";
    std::cout << COL_GREEN << "Mode: " << modeLabel << COL_RESET << "\n";

    /* Attempt GPU offload */
    std::cout << COL_MAGENTA << "GPU offload: "
              << sel.ip_address << ":" << GPU_ACCEL_PORT << " ...\n"
              << COL_RESET;
    g_gpu_sock = gpu_remote_connect(sel.ip_address.c_str());
    if (GPU_SOCK_VALID(g_gpu_sock))
    {
        g_gpu_active = true;
        std::cout << COL_GREEN << "Remote compute (GPU offload) active\n"
                  << COL_RESET;
    }
    else
    {
        std::cout << COL_YELLOW << "Compute service unavailable – local CPU only\n"
                  << COL_RESET;
    }

    /* Connect to port inspection service */
    std::cout << COL_MAGENTA << "Port inspector: "
              << sel.ip_address << ":" << PORTS_SERVICE_PORT << " ...\n"
              << COL_RESET;
    g_ports_sock = ports_remote_connect(sel.ip_address.c_str());
    if (PORTS_SOCK_VALID(g_ports_sock))
    {
        std::cout << COL_GREEN << "Port inspector active  (press 'p' during stream)\n"
                  << COL_RESET;
    }
    else
    {
        std::cout << COL_YELLOW << "Port inspector unavailable\n"
                  << COL_RESET;
    }

    /* Connect to storage service (read-only by default) */
    std::cout << COL_MAGENTA << "Storage access: "
              << sel.ip_address << ":" << STORAGE_SERVICE_PORT << " ...\n"
              << COL_RESET;
    g_storage_sock = storage_remote_connect(sel.ip_address.c_str(), STORAGE_ACCESS_READ);
    if (STORAGE_SOCK_VALID(g_storage_sock))
    {
        std::cout << COL_GREEN << "Storage access active (read-only)  (press 's' during stream)\n"
                  << COL_RESET;
    }
    else
    {
        std::cout << COL_YELLOW << "Storage service unavailable\n"
                  << COL_RESET;
    }

    /* Connect stream socket */
    NetworkSocket conn;
    if (!conn.connect(sel.ip_address, sel.tcp_port))
    {
        if (g_gpu_active)
            gpu_remote_disconnect(g_gpu_sock);
        if (PORTS_SOCK_VALID(g_ports_sock))
            ports_remote_disconnect(g_ports_sock);
        if (STORAGE_SOCK_VALID(g_storage_sock))
            storage_remote_disconnect(g_storage_sock);
        cleanupSockets();
        return 1;
    }

    /* ── Extended handshake ── */
    struct
    {
        uint32_t sender_width;
        uint32_t sender_height;
        uint32_t fps;
        uint32_t mode; /* MODE_EXTEND_RIGHT / MODE_MIRROR / etc. */
    } hs_out = {
        htonl((uint32_t)SCREEN_WIDTH),
        htonl((uint32_t)SCREEN_HEIGHT),
        htonl((uint32_t)TARGET_FPS),
        htonl(DISPLAY_MODE)};
    if (!conn.sendAll(&hs_out, sizeof(hs_out)))
    {
        if (g_gpu_active)
            gpu_remote_disconnect(g_gpu_sock);
        if (PORTS_SOCK_VALID(g_ports_sock))
            ports_remote_disconnect(g_ports_sock);
        if (STORAGE_SOCK_VALID(g_storage_sock))
            storage_remote_disconnect(g_storage_sock);
        cleanupSockets();
        return 1;
    }

    /* Receive receiver's display size */
    struct
    {
        uint32_t recv_width;
        uint32_t recv_height;
        uint32_t status;
    } hs_in = {};
    if (!conn.recvAll(&hs_in, sizeof(hs_in)) || ntohl(hs_in.status) != 0)
    {
        std::cerr << COL_RED << "Handshake response failed\n"
                  << COL_RESET;
        if (g_gpu_active)
            gpu_remote_disconnect(g_gpu_sock);
        if (PORTS_SOCK_VALID(g_ports_sock))
            ports_remote_disconnect(g_ports_sock);
        if (STORAGE_SOCK_VALID(g_storage_sock))
            storage_remote_disconnect(g_storage_sock);
        cleanupSockets();
        return 1;
    }
    RECV_WIDTH = (int)ntohl(hs_in.recv_width);
    RECV_HEIGHT = (int)ntohl(hs_in.recv_height);

    std::cout << COL_GREEN
              << "Extended desktop active:\n"
              << "  Sender:   " << SCREEN_WIDTH << "x" << SCREEN_HEIGHT << "\n"
              << "  Receiver: " << RECV_WIDTH << "x" << RECV_HEIGHT << "\n"
              << "  Layout:   " << modeLabel << "\n"
              << "  Total:    ";
    if (DISPLAY_MODE == MODE_EXTEND_RIGHT)
        std::cout << (SCREEN_WIDTH + RECV_WIDTH) << "x"
                  << std::max(SCREEN_HEIGHT, RECV_HEIGHT) << "\n";
    else if (DISPLAY_MODE == MODE_EXTEND_BELOW)
        std::cout << std::max(SCREEN_WIDTH, RECV_WIDTH) << "x"
                  << (SCREEN_HEIGHT + RECV_HEIGHT) << "\n";
    else
        std::cout << SCREEN_WIDTH << "x" << SCREEN_HEIGHT << " (mirror)\n";
    std::cout << COL_RESET;

    std::cout << COL_GREEN << "Streaming – Press 'm' for menu, Ctrl+C to stop\n"
              << COL_RESET;

    /* ── Streaming loop ── */
    const auto frame_dur = std::chrono::microseconds(1000000 / TARGET_FPS);
    auto last_stats = std::chrono::steady_clock::now();
    auto sess_start = last_stats;
    int frames_sent = 0;
    size_t total_bytes = 0;
    int fbehind = 0;
    std::vector<uint8_t> comp_buf;

    while (g_running)
    {
        /* Check for user input without blocking */
        checkForUserInput(conn);

        auto t0 = std::chrono::steady_clock::now();

        auto frame = captureScreen();

        /* GPU offload: RLE compress on receiver */
        const uint8_t *send_ptr = frame.data();
        uint32_t send_size = (uint32_t)frame.size();

        if (g_gpu_active && GPU_SOCK_VALID(g_gpu_sock))
        {
            uint8_t *out = nullptr;
            int csz = gpu_remote_compress(g_gpu_sock, frame.data(),
                                          (uint32_t)SCREEN_WIDTH,
                                          (uint32_t)SCREEN_HEIGHT, &out);
            if (csz > 0 && out)
            {
                comp_buf.assign(out, out + csz);
                free(out);
                send_ptr = comp_buf.data();
                send_size = (uint32_t)csz;
            }
            else
            {
                gpu_remote_disconnect(g_gpu_sock);
                g_gpu_sock = GPU_INVALID_SOCK;
                g_gpu_active = false;
                std::cerr << COL_YELLOW
                          << "GPU lost – CPU fallback\n"
                          << COL_RESET;
            }
        }

        uint32_t net_sz = htonl(send_size);
        if (!conn.sendAll(&net_sz, 4) ||
            !conn.sendAll(send_ptr, send_size))
        {
            std::cerr << COL_RED << "Frame send failed\n"
                      << COL_RESET;
            break;
        }

        frames_sent++;
        total_bytes += 4 + send_size;

        auto now = std::chrono::steady_clock::now();
        auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                        now - last_stats)
                        .count();
        if (secs >= STATS_INTERVAL_SEC)
        {
            showStats(frames_sent, secs, total_bytes);
            last_stats = now;
        }

        /* Adaptive timing */
        auto elapsed = std::chrono::steady_clock::now() - t0;
        if (elapsed > frame_dur)
        {
            if (++fbehind > MAX_FRAME_SKIP)
            {
                fbehind = 0;
                continue;
            }
        }
        else
        {
            fbehind = 0;
            std::this_thread::sleep_for(frame_dur - elapsed);
        }
    }

    /* Final stats */
    auto total_s = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::steady_clock::now() - sess_start)
                       .count();
    float total_mb = (float)total_bytes / (1024.0f * 1024.0f);

    std::cout << COL_CYAN << COL_BOLD
              << "\n========================================\n"
                 "  SESSION STATISTICS\n"
                 "========================================\n"
              << COL_RESET
              << "  Mode       : " << modeLabel << "\n"
              << "  Source res : " << SCREEN_WIDTH << "x" << SCREEN_HEIGHT << "\n"
              << "  Recv res   : " << RECV_WIDTH << "x" << RECV_HEIGHT << "\n"
              << "  Frames     : " << frames_sent << "\n"
              << "  Duration   : " << total_s << " s\n";
    if (total_s > 0)
        std::cout << "  Avg FPS    : " << frames_sent / total_s << "\n"
                  << "  Data       : " << std::fixed << std::setprecision(2)
                  << total_mb << " MB\n"
                  << "  Avg BW     : " << total_mb / (float)total_s
                  << " MB/s\n";
    std::cout << COL_CYAN << "========================================\n"
              << COL_RESET;

    if (g_gpu_active)
        gpu_remote_disconnect(g_gpu_sock);
    if (PORTS_SOCK_VALID(g_ports_sock))
        ports_remote_disconnect(g_ports_sock);
    if (STORAGE_SOCK_VALID(g_storage_sock))
        storage_remote_disconnect(g_storage_sock);
    cleanupSockets();
    return 0;
}