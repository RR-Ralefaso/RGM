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
 * Handshake (sender → receiver):
 *   uint32_t  sender_width     – sender screen width
 *   uint32_t  sender_height    – sender screen height
 *   uint32_t  fps              – target frame rate
 *   uint32_t  mode             – 0=mirror  1=extend_right  2=extend_below
 *
 * Handshake response (receiver → sender):
 *   uint32_t  receiver_width   – receiver display width
 *   uint32_t  receiver_height  – receiver display height
 *   uint32_t  status           – 0=ok
 *
 * After the handshake the sender streams frames exactly as before.
 * In extend_right mode the sender also registers a virtual desktop offset
 * so that OS-level pointer warp (optional, per platform) can move the
 * mouse to the receiver screen edge naturally.
 *
 * Platform notes
 * --------------
 * Windows : GDI capture; Winsock2; SDL2+SDL_image splash.
 * Linux   : X11 capture; POSIX sockets. Wayland: set WAYLAND_DISPLAY="".
 * macOS   : CoreGraphics capture; POSIX sockets.
 *           Link: -framework CoreGraphics -framework CoreFoundation
 *
 * Build deps: SDL2, SDL2_image, X11 (Linux), pthreads
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
#include "discover.h"
#include "gpu_accelerate.h"

/* ── Platform headers ───────────────────────────────────────────────────── */
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#  include <mstcpip.h>
#  pragma comment(lib, "ws2_32.lib")
#elif defined(__APPLE__)
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <errno.h>
#  include <CoreGraphics/CoreGraphics.h>
#else  /* Linux */
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <errno.h>
#  include <X11/Xlib.h>
#  include <X11/Xutil.h>
#endif

#include <SDL2/SDL.h>
/* SDL2_image: required for rcorp.jpeg splash (install libsdl2-image-dev) */
#if __has_include(<SDL2/SDL_image.h>)
#  include <SDL2/SDL_image.h>
#  define HAVE_SDL_IMAGE 1
#else
#  define HAVE_SDL_IMAGE 0
#  warning "SDL2_image not found – splash will use fallback rectangle."
#  warning "Fix: sudo apt install libsdl2-image-dev  (or: make install-sdl2-image)"
#endif   /* PNG + JPEG support */

/* ── Display mode ───────────────────────────────────────────────────────── */
#define MODE_MIRROR        0u
#define MODE_EXTEND_RIGHT  1u
#define MODE_EXTEND_BELOW  2u

/* ── Constants ──────────────────────────────────────────────────────────── */
#define BYTES_PER_PIXEL       3
#define CONNECTION_TIMEOUT_MS 5000
#define STATS_INTERVAL_SEC    5
#define MAX_FRAME_SKIP        3
#define MAX_FRAME_BYTES       (7680u * 4320u * 3u)

static const int SOCK_BUF = 4 * 1024 * 1024;

/* ── ANSI colours (Windows CMD does not support VT100 by default) ────────── */
#ifdef _WIN32
#  define COL_RESET   ""
#  define COL_RED     ""
#  define COL_GREEN   ""
#  define COL_YELLOW  ""
#  define COL_CYAN    ""
#  define COL_MAGENTA ""
#  define COL_BOLD    ""
#else
#  define COL_RESET   "\033[0m"
#  define COL_RED     "\033[31m"
#  define COL_GREEN   "\033[32m"
#  define COL_YELLOW  "\033[33m"
#  define COL_CYAN    "\033[36m"
#  define COL_MAGENTA "\033[35m"
#  define COL_BOLD    "\033[1m"
#endif

/* ── Globals ─────────────────────────────────────────────────────────────── */
static int SCREEN_WIDTH  = 1920;
static int SCREEN_HEIGHT = 1080;
static int TARGET_FPS    = 60;
static uint32_t DISPLAY_MODE = MODE_EXTEND_RIGHT;  /* default: extend */

/* Receiver display dimensions (filled after handshake response) */
static int RECV_WIDTH  = 1920;
static int RECV_HEIGHT = 1080;

static std::atomic<bool> g_running{true};

/* GPU offload */
static gpu_sock_t g_gpu_sock   = GPU_INVALID_SOCK;
static bool       g_gpu_active = false;

/* ══════════════════════════════════════════════════════════════════════════
 * SPLASH SCREEN  –  rcorp.jpeg preferred, RGM.png fallback
 * ══════════════════════════════════════════════════════════════════════════ */
static void showSplashScreen()
{
    std::cout << COL_CYAN << COL_BOLD
              << "========================================\n"
                 "  RGM SENDER v2.0\n"
                 "========================================\n"
              << COL_RESET;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << COL_YELLOW << "Splash: SDL_Init: "
                  << SDL_GetError() << COL_RESET << "\n";
        return;
    }

    /* Enable PNG + JPEG loading via SDL_image */
#if HAVE_SDL_IMAGE
    int img_flags = IMG_INIT_PNG | IMG_INIT_JPG;
    if ((IMG_Init(img_flags) & img_flags) != img_flags)
        std::cerr << COL_YELLOW << "Splash: SDL_image partial init\n" << COL_RESET;
#endif

    SDL_Window *win = SDL_CreateWindow(
        "RGM", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        480, 300, SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP);
    if (!win) {
#if HAVE_SDL_IMAGE
        IMG_Quit();
#endif
        SDL_Quit(); return;
    }

    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) {
        SDL_DestroyWindow(win);
#if HAVE_SDL_IMAGE
        IMG_Quit();
#endif
        SDL_Quit(); return;
    }

    /* Dark background */
    SDL_SetRenderDrawColor(ren, 12, 12, 20, 255);
    SDL_RenderClear(ren);

    /*
     * Search order: rcorp.jpeg FIRST (brand logo), then RGM.png fallback.
     * Paths cover: running from project root, from build/, and installed.
     */
    const char *paths[] = {
        "../assets/icons/rcorp.jpeg",    /* running from build/     */
        "assets/icons/rcorp.jpeg",       /* running from project root */
        "../assets/icons/RGM.png",
        "assets/icons/RGM.png",
#ifndef _WIN32
        "/usr/share/rgm/icons/rcorp.jpeg",
        "/usr/share/rgm/icons/RGM.png",
#endif
        nullptr
    };

    SDL_Surface *img = nullptr;
#if HAVE_SDL_IMAGE
    for (int i = 0; paths[i] && !img; i++) {
        img = IMG_Load(paths[i]);     /* handles JPEG and PNG natively */
        if (img)
            std::cout << COL_GREEN << "Splash: " << paths[i]
                      << COL_RESET << "\n";
    }
#endif

    if (!img) {
        /* Fallback: rcorp-styled blue rectangle */
        SDL_SetRenderDrawColor(ren, 20, 60, 140, 255);
        SDL_Rect fill = {30, 30, 420, 240};  SDL_RenderFillRect(ren, &fill);
        SDL_SetRenderDrawColor(ren, 0, 180, 255, 255);
        SDL_Rect border = {28, 28, 424, 244}; SDL_RenderDrawRect(ren, &border);
    } else {
        SDL_Texture *tex = SDL_CreateTextureFromSurface(ren, img);
        SDL_FreeSurface(img);
        if (tex) {
            int tw, th;
            SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
            /* Scale to fit 460×280 keeping aspect ratio */
            float sc = std::min(460.0f / (float)tw, 280.0f / (float)th);
            SDL_Rect dst;
            dst.w = (int)(tw * sc);  dst.h = (int)(th * sc);
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
    if (mode) {
        w = (int)CGDisplayModeGetWidth(mode);
        h = (int)CGDisplayModeGetHeight(mode);
        CGDisplayModeRelease(mode);
        std::cout << "Display: " << w << "x" << h << " (CoreGraphics)\n";
    } else {
        w = 1920; h = 1080;
        std::cerr << COL_YELLOW
                  << "CoreGraphics query failed; using 1920x1080\n"
                  << COL_RESET;
    }

#else   /* Linux / X11 */
    Display *dpy = XOpenDisplay(nullptr);
    if (dpy) {
        int sn = DefaultScreen(dpy);
        w = DisplayWidth(dpy, sn);
        h = DisplayHeight(dpy, sn);
        XCloseDisplay(dpy);
        std::cout << "Display: " << w << "x" << h << " (X11)\n";
    } else {
        w = 1920; h = 1080;
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
    bool valid()    const { return _s != INVALID_SOCKET; }
    void raw_close()      { closesocket(_s); _s = INVALID_SOCKET; }
#else
    int _s = -1;
    bool valid()    const { return _s >= 0; }
    void raw_close()      { ::close(_s); _s = -1; }
#endif

public:
    ~NetworkSocket() { close(); }

    void close() { if (valid()) raw_close(); }

    bool create() {
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
        if (!create()) return false;

        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons((uint16_t)port);
        if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
            std::cerr << COL_RED << "Invalid IP: " << ip << COL_RESET << "\n";
            return false;
        }

        /* Non-blocking connect with timeout */
#ifdef _WIN32
        u_long nb = 1; ioctlsocket(_s, FIONBIO, &nb);
#else
        int fl = fcntl(_s, F_GETFL, 0);
        fcntl(_s, F_SETFL, fl | O_NONBLOCK);
#endif
        ::connect(_s, (struct sockaddr *)&addr, sizeof(addr));

        fd_set fds; FD_ZERO(&fds); FD_SET(_s, &fds);
        struct timeval tv = { timeout_ms / 1000,
                              (timeout_ms % 1000) * 1000 };
        bool ok = false;
        if (select((int)_s + 1, nullptr, &fds, nullptr, &tv) == 1) {
            int err = 0; socklen_t len = sizeof(err);
            getsockopt(_s, SOL_SOCKET, SO_ERROR, (char *)&err, &len);
            ok = (err == 0);
        }

#ifdef _WIN32
        { u_long bl = 0; ioctlsocket(_s, FIONBIO, &bl); }
#else
        fcntl(_s, F_SETFL, fl);
#endif
        if (!ok) {
            std::cerr << COL_RED << "Timeout: " << ip << ":"
                      << port << COL_RESET << "\n";
            close(); return false;
        }

        int nd = 1, sb = SOCK_BUF;
        setsockopt(_s, IPPROTO_TCP, TCP_NODELAY, (char *)&nd, sizeof(nd));
        setsockopt(_s, SOL_SOCKET,  SO_SNDBUF,   (char *)&sb, sizeof(sb));
        std::cout << COL_GREEN << "Connected to "
                  << ip << ":" << port << COL_RESET << "\n";
        return true;
    }

    bool sendAll(const void *data, size_t size) {
        const char *p = (const char *)data;
        size_t sent = 0;
        while (sent < size && g_running) {
            int n = (int)send(_s, p + sent, (int)(size - sent), 0);
#ifdef _WIN32
            if (n == SOCKET_ERROR) {
                std::cerr << COL_RED << "Send error: "
                          << WSAGetLastError() << COL_RESET << "\n";
                return false;
            }
#else
            if (n < 0) {
                std::cerr << COL_RED << "Send error: "
                          << strerror(errno) << COL_RESET << "\n";
                return false;
            }
#endif
            if (n == 0) return false;
            sent += (size_t)n;
        }
        return sent == size;
    }

    bool recvAll(void *data, size_t size) {
        char *p = (char *)data;
        size_t got = 0;
        while (got < size) {
            int n = (int)recv(_s, p + got, (int)(size - got), 0);
            if (n <= 0) return false;
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
    if (!sdc) return px;
    HDC     mdc = CreateCompatibleDC(sdc);
    HBITMAP bmp = CreateCompatibleBitmap(sdc, SCREEN_WIDTH, SCREEN_HEIGHT);
    if (!bmp) { DeleteDC(mdc); ReleaseDC(nullptr, sdc); return px; }
    SelectObject(mdc, bmp);
    BitBlt(mdc, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, sdc, 0, 0,
           SRCCOPY | CAPTUREBLT);
    BITMAPINFOHEADER bi = {};
    bi.biSize = sizeof(bi); bi.biWidth = SCREEN_WIDTH;
    bi.biHeight = -SCREEN_HEIGHT;          /* top-down, no flip needed */
    bi.biPlanes = 1; bi.biBitCount = 24; bi.biCompression = BI_RGB;
    GetDIBits(mdc, bmp, 0, SCREEN_HEIGHT, px.data(),
              (BITMAPINFO *)&bi, DIB_RGB_COLORS);
    DeleteObject(bmp); DeleteDC(mdc); ReleaseDC(nullptr, sdc);
    /* GDI returns BGR; swap to RGB for network transmission */
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++)
        std::swap(px[i*3], px[i*3+2]);
    return px;
}

#elif defined(__APPLE__)
static std::vector<uint8_t> captureScreen()
{
    std::vector<uint8_t> px(SCREEN_WIDTH * SCREEN_HEIGHT * BYTES_PER_PIXEL, 0);
    CGImageRef img = CGDisplayCreateImage(kCGDirectMainDisplay);
    if (!img) return px;
    CGDataProviderRef dp  = CGImageGetDataProvider(img);
    CFDataRef         raw = CGDataProviderCopyData(dp);
    if (!raw) { CGImageRelease(img); return px; }
    const uint8_t *src = CFDataGetBytePtr(raw);
    size_t bpr = CGImageGetBytesPerRow(img);
    size_t bpp = CGImageGetBitsPerPixel(img) / 8; /* usually 4: BGRA */
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            size_t si = y * bpr + x * bpp;
            size_t di = ((size_t)y * SCREEN_WIDTH + x) * 3;
            px[di+0] = src[si+2]; /* R */
            px[di+1] = src[si+1]; /* G */
            px[di+2] = src[si+0]; /* B */
        }
    }
    CFRelease(raw);
    CGImageRelease(img);
    return px;
}

#else   /* Linux / X11 */
static std::vector<uint8_t> captureScreen()
{
    std::vector<uint8_t> px(SCREEN_WIDTH * SCREEN_HEIGHT * BYTES_PER_PIXEL, 0);
    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy) return px;
    Window root = RootWindow(dpy, DefaultScreen(dpy));
    XImage *xi  = XGetImage(dpy, root, 0, 0,
                             SCREEN_WIDTH, SCREEN_HEIGHT, AllPlanes, ZPixmap);
    if (!xi) { XCloseDisplay(dpy); return px; }
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            unsigned long p = XGetPixel(xi, x, y);
            size_t i = ((size_t)y * SCREEN_WIDTH + x) * 3;
            px[i+0] = (uint8_t)((p >> 16) & 0xFF); /* R */
            px[i+1] = (uint8_t)((p >>  8) & 0xFF); /* G */
            px[i+2] = (uint8_t)( p        & 0xFF); /* B */
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
              << "  Select display mode:\n" << COL_RESET
              << "  " << COL_GREEN  << "1" << COL_RESET
                      << "  Extend Right  (receiver = right monitor)\n"
              << "  " << COL_YELLOW << "2" << COL_RESET
                      << "  Extend Below  (receiver = bottom monitor)\n"
              << "  " << COL_CYAN   << "3" << COL_RESET
                      << "  Mirror        (duplicate screen)\n"
              << COL_BOLD << "  Choice [1]: " << COL_RESET;

    std::string line;
    /* consume leftover newline from previous cin >> */
    if (std::cin.peek() == '\n') std::cin.ignore();
    std::getline(std::cin, line);

    if (line == "2") return MODE_EXTEND_BELOW;
    if (line == "3") return MODE_MIRROR;
    return MODE_EXTEND_RIGHT;     /* default */
}

/* ══════════════════════════════════════════════════════════════════════════
 * STATS
 * ══════════════════════════════════════════════════════════════════════════ */
static void showStats(int frames, long elapsed, size_t bytes)
{
    if (elapsed < 1) elapsed = 1;
    float fps  = (float)frames / (float)elapsed;
    float mbps = (float)(bytes / (1024.0 * 1024.0)) / (float)elapsed;
    const char *modeStr =
        DISPLAY_MODE == MODE_EXTEND_RIGHT  ? "extend-right"  :
        DISPLAY_MODE == MODE_EXTEND_BELOW  ? "extend-below"  : "mirror";

    std::cout << COL_CYAN
              << "Frames: " << frames
              << "  FPS: "  << std::fixed << std::setprecision(1) << fps
                            << "/" << TARGET_FPS
              << "  BW: "   << std::setprecision(2) << mbps << " MB/s"
              << "  Src: "  << SCREEN_WIDTH  << "x" << SCREEN_HEIGHT
              << "  Dst: "  << RECV_WIDTH    << "x" << RECV_HEIGHT
              << "  Mode: " << modeStr
              << (g_gpu_active ? "  GPU:remote" : "  GPU:local")
              << COL_RESET << "\n";
}

/* ══════════════════════════════════════════════════════════════════════════
 * MAIN
 * ══════════════════════════════════════════════════════════════════════════ */
int main()
{
    showSplashScreen();
    getScreenDimensions(SCREEN_WIDTH, SCREEN_HEIGHT);

    std::cout << COL_CYAN << COL_BOLD
              << "========================================\n"
                 "  RGM SENDER v2.0\n"
                 "========================================\n" << COL_RESET
              << "  Source: " << SCREEN_WIDTH << "x" << SCREEN_HEIGHT
              << "  @ " << TARGET_FPS << " FPS\n"
              << COL_CYAN << "========================================\n"
              << COL_RESET;

    if (!initSockets()) {
        std::cerr << COL_RED << "Socket init failed\n" << COL_RESET;
        return 1;
    }

    /* Discover receivers */
    std::cout << COL_CYAN << "Discovering receivers...\n" << COL_RESET;
    auto receivers = discoverReceivers(5);
    if (receivers.empty()) {
        std::cerr << COL_RED
                  << "No receivers found!\n"
                     "  Check firewall: UDP 1900, TCP 8081, TCP 8082\n"
                  << COL_RESET;
        cleanupSockets(); return 1;
    }
    std::cout << listDevices(receivers);

    /* Select receiver */
    size_t choice = 0;
    if (receivers.size() > 1) {
        std::cout << COL_BOLD << "Select receiver (0-"
                  << receivers.size() - 1 << "): " << COL_RESET;
        if (!(std::cin >> choice) || choice >= receivers.size()) {
            std::cerr << COL_RED << "Invalid selection\n" << COL_RESET;
            cleanupSockets(); return 1;
        }
    }

    const auto &sel = receivers[choice];
    std::cout << COL_GREEN << "Selected: " << sel.toString()
              << COL_RESET << "\n";

    /* Choose display mode */
    DISPLAY_MODE = selectDisplayMode();
    const char *modeLabel =
        DISPLAY_MODE == MODE_EXTEND_RIGHT ? "Extend Right"  :
        DISPLAY_MODE == MODE_EXTEND_BELOW ? "Extend Below"  : "Mirror";
    std::cout << COL_GREEN << "Mode: " << modeLabel << COL_RESET << "\n";

    /* Attempt GPU offload */
    std::cout << COL_MAGENTA << "GPU offload: "
              << sel.ip_address << ":" << GPU_ACCEL_PORT << " ...\n"
              << COL_RESET;
    g_gpu_sock = gpu_remote_connect(sel.ip_address.c_str());
    if (GPU_SOCK_VALID(g_gpu_sock)) {
        g_gpu_active = true;
        std::cout << COL_GREEN << "Remote GPU active\n" << COL_RESET;
    } else {
        std::cout << COL_YELLOW << "GPU service unavailable – CPU only\n"
                  << COL_RESET;
    }

    /* Connect stream socket */
    NetworkSocket conn;
    if (!conn.connect(sel.ip_address, sel.tcp_port)) {
        if (g_gpu_active) gpu_remote_disconnect(g_gpu_sock);
        cleanupSockets(); return 1;
    }

    /* ── Extended handshake ── */
    struct {
        uint32_t sender_width;
        uint32_t sender_height;
        uint32_t fps;
        uint32_t mode;        /* MODE_EXTEND_RIGHT / MODE_MIRROR / etc. */
    } hs_out = {
        htonl((uint32_t)SCREEN_WIDTH),
        htonl((uint32_t)SCREEN_HEIGHT),
        htonl((uint32_t)TARGET_FPS),
        htonl(DISPLAY_MODE)
    };
    if (!conn.sendAll(&hs_out, sizeof(hs_out))) {
        if (g_gpu_active) gpu_remote_disconnect(g_gpu_sock);
        cleanupSockets(); return 1;
    }

    /* Receive receiver's display size */
    struct {
        uint32_t recv_width;
        uint32_t recv_height;
        uint32_t status;
    } hs_in = {};
    if (!conn.recvAll(&hs_in, sizeof(hs_in)) || ntohl(hs_in.status) != 0) {
        std::cerr << COL_RED << "Handshake response failed\n" << COL_RESET;
        if (g_gpu_active) gpu_remote_disconnect(g_gpu_sock);
        cleanupSockets(); return 1;
    }
    RECV_WIDTH  = (int)ntohl(hs_in.recv_width);
    RECV_HEIGHT = (int)ntohl(hs_in.recv_height);

    std::cout << COL_GREEN
              << "Extended desktop active:\n"
              << "  Sender:   " << SCREEN_WIDTH  << "x" << SCREEN_HEIGHT << "\n"
              << "  Receiver: " << RECV_WIDTH    << "x" << RECV_HEIGHT   << "\n"
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

    std::cout << COL_GREEN << "Streaming – Ctrl+C to stop\n" << COL_RESET;

    /* ── Streaming loop ── */
    const auto frame_dur   = std::chrono::microseconds(1000000 / TARGET_FPS);
    auto       last_stats  = std::chrono::steady_clock::now();
    auto       sess_start  = last_stats;
    int        frames_sent = 0;
    size_t     total_bytes = 0;
    int        fbehind     = 0;
    std::vector<uint8_t> comp_buf;

    while (g_running) {
        auto t0 = std::chrono::steady_clock::now();

        auto frame = captureScreen();

        /* GPU offload: RLE compress on receiver */
        const uint8_t *send_ptr  = frame.data();
        uint32_t       send_size = (uint32_t)frame.size();

        if (g_gpu_active) {
            uint8_t *out = nullptr;
            int csz = gpu_remote_compress(g_gpu_sock, frame.data(),
                                          (uint32_t)SCREEN_WIDTH,
                                          (uint32_t)SCREEN_HEIGHT, &out);
            if (csz > 0 && out) {
                comp_buf.assign(out, out + csz);
                free(out);
                send_ptr  = comp_buf.data();
                send_size = (uint32_t)csz;
            } else {
                gpu_remote_disconnect(g_gpu_sock);
                g_gpu_sock   = GPU_INVALID_SOCK;
                g_gpu_active = false;
                std::cerr << COL_YELLOW
                          << "GPU lost – CPU fallback\n" << COL_RESET;
            }
        }

        uint32_t net_sz = htonl(send_size);
        if (!conn.sendAll(&net_sz, 4) ||
            !conn.sendAll(send_ptr, send_size)) {
            std::cerr << COL_RED << "Frame send failed\n" << COL_RESET;
            break;
        }

        frames_sent++;
        total_bytes += 4 + send_size;

        auto now  = std::chrono::steady_clock::now();
        auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                        now - last_stats).count();
        if (secs >= STATS_INTERVAL_SEC) {
            showStats(frames_sent, secs, total_bytes);
            last_stats = now;
        }

        /* Adaptive timing */
        auto elapsed = std::chrono::steady_clock::now() - t0;
        if (elapsed > frame_dur) {
            if (++fbehind > MAX_FRAME_SKIP) { fbehind = 0; continue; }
        } else {
            fbehind = 0;
            std::this_thread::sleep_for(frame_dur - elapsed);
        }
    }

    /* Final stats */
    auto total_s = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - sess_start).count();
    float total_mb = (float)total_bytes / (1024.0f * 1024.0f);

    std::cout << COL_CYAN << COL_BOLD
              << "\n========================================\n"
                 "  SESSION STATISTICS\n"
                 "========================================\n" << COL_RESET
              << "  Mode       : " << modeLabel << "\n"
              << "  Source res : " << SCREEN_WIDTH  << "x" << SCREEN_HEIGHT << "\n"
              << "  Recv res   : " << RECV_WIDTH    << "x" << RECV_HEIGHT   << "\n"
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

    if (g_gpu_active) gpu_remote_disconnect(g_gpu_sock);
    cleanupSockets();
    return 0;
}
