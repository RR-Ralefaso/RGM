/**
 * RECEIVER.CPP - SCREEN EXTENDER DISPLAY, SSDP ADVERTISING, GPU SERVICE
 *
 * Cross-platform: Windows (Winsock2) / Linux / macOS (POSIX).
 *
 * Screen Extender Mode
 * --------------------
 * When the sender sends MODE_EXTEND_RIGHT or MODE_EXTEND_BELOW, the receiver
 * opens a BORDERLESS FULLSCREEN window that covers its entire display,
 * presenting itself as a seamless second monitor.
 *
 * In extend mode the window:
 *  - Is borderless and positioned at (0, 0) on the receiver display
 *  - Covers the full receiver display (SDL_WINDOW_FULLSCREEN_DESKTOP)
 *  - Has no title bar or window chrome, so it looks like a real monitor
 *  - Shows a subtle edge glow where the displays logically join
 *
 * In mirror mode a normal resizable window is used (original behaviour).
 *
 * Extended handshake (receiver → sender):
 *   Receiver reads sender's dimensions + mode, opens the appropriate window,
 *   then sends back its own display size so sender can display total layout.
 *
 * Cross-platform notes
 * --------------------
 * - plat_sock_t / PLAT_VALID / PLAT_CLOSE unify all socket use.
 * - ACCEPT_LEN_T = int (Windows) / socklen_t (POSIX).
 * - Signal: SetConsoleCtrlHandler (Windows) / sigaction (POSIX).
 * - SDL_WINDOW_FULLSCREEN_DESKTOP is cross-platform in SDL2.
 */

#include <iostream>
#include <cstring>
#include <vector>
#include <thread>
#include <chrono>
#include <cstdint>
#include <atomic>
#include <iomanip>
#include <algorithm>
#include <ctime>
#include <string>

#include <SDL2/SDL.h>
/* SDL2_image: required for rcorp.jpeg splash (install libsdl2-image-dev) */
#if __has_include(<SDL2/SDL_image.h>)
#include <SDL2/SDL_image.h>
#define HAVE_SDL_IMAGE 1
#else
#define HAVE_SDL_IMAGE 0
#warning "SDL2_image not found – splash will use fallback rectangle."
#warning "Fix: sudo apt install libsdl2-image-dev  (or: make install-sdl2-image)"
#endif /* rcorp.jpeg splash */
#include "discover.h"
#include "gpu_accelerate.h"

/* ── Platform headers ───────────────────────────────────────────────────── */
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET plat_sock_t;
#define PLAT_INVALID INVALID_SOCKET
#define PLAT_CLOSE(s) closesocket(s)
#define PLAT_VALID(s) ((s) != INVALID_SOCKET)
#define ACCEPT_LEN_T int
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
typedef int plat_sock_t;
#define PLAT_INVALID (-1)
#define PLAT_CLOSE(s) close(s)
#define PLAT_VALID(s) ((s) >= 0)
#define ACCEPT_LEN_T socklen_t
#endif

/* ── Display mode constants (must match sender.cpp) ─────────────────────── */
#define MODE_MIRROR 0u
#define MODE_EXTEND_RIGHT 1u
#define MODE_EXTEND_BELOW 2u

/* ── Constants ──────────────────────────────────────────────────────────── */
#define TCP_STREAM_PORT 8081
#define BYTES_PER_PIXEL 3
#define SSDP_ADDR "239.255.255.250"
#define SSDP_PORT 1900
#define MAX_FRAME_BYTES (7680u * 4320u * 3u)

static const int SOCK_BUF = 4 * 1024 * 1024;

/* ── Globals ─────────────────────────────────────────────────────────────── */
static int SCREEN_WIDTH = 1920; /* sender's resolution     */
static int SCREEN_HEIGHT = 1080;
static int TARGET_FPS = 60;
static uint32_t DISPLAY_MODE = MODE_EXTEND_RIGHT;

/* This receiver's own display size (queried via SDL2) */
static int MY_DISPLAY_WIDTH = 1920;
static int MY_DISPLAY_HEIGHT = 1080;

static std::atomic<bool> g_running{true};

/* ── Platform signal handler ─────────────────────────────────────────────── */
#ifdef _WIN32
static BOOL WINAPI consoleHandler(DWORD sig)
{
    if (sig == CTRL_C_EVENT || sig == CTRL_BREAK_EVENT ||
        sig == CTRL_CLOSE_EVENT)
    {
        g_running = false;
        return TRUE;
    }
    return FALSE;
}
#else
static void sigHandler(int) { g_running = false; }
#endif

/* ── I/O helpers ─────────────────────────────────────────────────────────── */
static int recv_all(plat_sock_t s, void *buf, size_t len)
{
    char *p = (char *)buf;
    size_t got = 0;
    while (got < len)
    {
        int n = (int)recv(s, p + got, (int)(len - got), 0);
        if (n <= 0)
            return -1;
        got += (size_t)n;
    }
    return 0;
}

static int send_all_p(plat_sock_t s, const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    size_t sent = 0;
    while (sent < len)
    {
        int n = (int)send(s, p + sent, (int)(len - sent), 0);
        if (n <= 0)
            return -1;
        sent += (size_t)n;
    }
    return 0;
}

/* ── Forward declaration ─────────────────────────────────────────────────── */
static bool handleClientConnection(plat_sock_t client);

/* ══════════════════════════════════════════════════════════════════════════
 * SPLASH SCREEN  –  rcorp.jpeg preferred, RGM.png fallback
 * ══════════════════════════════════════════════════════════════════════════ */
static void showSplashScreen()
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
        return;

#if HAVE_SDL_IMAGE
    int img_flags = IMG_INIT_PNG | IMG_INIT_JPG;
    if ((IMG_Init(img_flags) & img_flags) != img_flags)
        std::cerr << "Splash: SDL_image partial init\n";
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

    SDL_SetRenderDrawColor(ren, 12, 12, 20, 255);
    SDL_RenderClear(ren);

    /* rcorp.jpeg first, RGM.png fallback */
    const char *paths[] = {
        "../assets/icons/rcorp.jpeg",
        "assets/icons/rcorp.jpeg",
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
        img = IMG_Load(paths[i]);
#endif

    if (!img)
    {
        /* Fallback: rcorp-styled blue rectangle */
        SDL_SetRenderDrawColor(ren, 20, 60, 140, 255);
        SDL_Rect fill = {30, 30, 420, 240};
        SDL_RenderFillRect(ren, &fill);
        SDL_SetRenderDrawColor(ren, 0, 180, 255, 255);
        SDL_Rect bdr = {28, 28, 424, 244};
        SDL_RenderDrawRect(ren, &bdr);
    }
    else
    {
        SDL_Texture *tex = SDL_CreateTextureFromSurface(ren, img);
        SDL_FreeSurface(img);
        if (tex)
        {
            int tw, th;
            SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
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
 * QUERY THIS RECEIVER'S DISPLAY SIZE via SDL2
 * ══════════════════════════════════════════════════════════════════════════ */
static void queryMyDisplaySize()
{
    /* Temporarily init SDL just to get display bounds */
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
        return;
    SDL_DisplayMode dm = {};
    if (SDL_GetCurrentDisplayMode(0, &dm) == 0)
    {
        MY_DISPLAY_WIDTH = dm.w;
        MY_DISPLAY_HEIGHT = dm.h;
    }
    SDL_Quit();
}

/* ══════════════════════════════════════════════════════════════════════════
 * SSDP ADVERTISEMENT THREAD
 * ══════════════════════════════════════════════════════════════════════════ */
static void ssdpAdvertisementThread()
{
    std::cout << "SSDP: starting on port " << SSDP_PORT << "\n";

    plat_sock_t rsock = socket(AF_INET, SOCK_DGRAM, 0);
    if (!PLAT_VALID(rsock))
    {
        std::cerr << "SSDP: socket() failed\n";
        return;
    }

    int reuse = 1, sb = SOCK_BUF;
    setsockopt(rsock, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));
    setsockopt(rsock, SOL_SOCKET, SO_RCVBUF, (char *)&sb, sizeof(sb));

    struct ip_mreq mreq = {};
    mreq.imr_multiaddr.s_addr = inet_addr(SSDP_ADDR);
    mreq.imr_interface.s_addr = INADDR_ANY;
    if (setsockopt(rsock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                   (char *)&mreq, sizeof(mreq)) < 0)
    {
        std::cerr << "SSDP: multicast join failed\n";
        PLAT_CLOSE(rsock);
        return;
    }

    struct sockaddr_in baddr = {};
    baddr.sin_family = AF_INET;
    baddr.sin_addr.s_addr = INADDR_ANY;
    baddr.sin_port = htons(SSDP_PORT);
    if (bind(rsock, (struct sockaddr *)&baddr, sizeof(baddr)) < 0)
    {
        std::cerr << "SSDP: bind failed on port " << SSDP_PORT << "\n";
        PLAT_CLOSE(rsock);
        return;
    }
    std::cout << "SSDP: listening\n";

    /* M-SEARCH response thread */
    std::thread resp([rsock]()
                     {
        char buf[2048];
        struct sockaddr_in from = {};
        ACCEPT_LEN_T flen = sizeof(from);
        std::string lip = getLocalIPAddress();
        while (g_running) {
            struct timeval tv = {1, 0};
            setsockopt(rsock, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(tv));
            int n = (int)recvfrom(rsock, buf, (int)sizeof(buf) - 1, 0,
                                  (struct sockaddr *)&from,
                                  (ACCEPT_LEN_T *)&flen);
            if (!g_running || n <= 0) continue;
            buf[n] = '\0';
            std::string req(buf);
            if (req.find("M-SEARCH")                == std::string::npos) continue;
            if (req.find("urn:screen-share:receiver") == std::string::npos) continue;
            std::string resp_str =
                "HTTP/1.1 200 OK\r\n"
                "CACHE-CONTROL: max-age=30\r\n"
                "DATE: " + std::to_string((long long)time(nullptr)) + "\r\n"
                "LOCATION: http://" + lip + ":" +
                    std::to_string(TCP_STREAM_PORT) + "/\r\n"
                "SERVER: ScreenShare/1.0\r\n"
                "ST: urn:screen-share:receiver\r\n"
                "USN: uuid:screen-share-" + lip + "\r\n\r\n";
            sendto(rsock, resp_str.c_str(), (int)resp_str.size(), 0,
                   (struct sockaddr *)&from, (ACCEPT_LEN_T)flen);
            std::cout << "SSDP: replied to " << inet_ntoa(from.sin_addr) << "\n";
        } });

    /* NOTIFY thread */
    plat_sock_t nsock = socket(AF_INET, SOCK_DGRAM, 0);
    if (PLAT_VALID(nsock))
    {
        int bc = 1, ttl = 4;
        setsockopt(nsock, SOL_SOCKET, SO_BROADCAST, (char *)&bc, sizeof(bc));
        setsockopt(nsock, IPPROTO_IP, IP_MULTICAST_TTL, (char *)&ttl, sizeof(ttl));
        std::string lip = getLocalIPAddress();
        std::string notify =
            "NOTIFY * HTTP/1.1\r\n"
            "HOST: " +
            std::string(SSDP_ADDR) + ":" +
            std::to_string(SSDP_PORT) + "\r\n"
                                        "CACHE-CONTROL: max-age=30\r\n"
                                        "LOCATION: http://" +
            lip + ":" +
            std::to_string(TCP_STREAM_PORT) + "/\r\n"
                                              "NT: urn:screen-share:receiver\r\n"
                                              "NTS: ssdp:alive\r\n"
                                              "SERVER: ScreenShare/1.0\r\n"
                                              "USN: uuid:screen-share-" +
            lip + "\r\n\r\n";
        struct sockaddr_in dest = {};
        dest.sin_family = AF_INET;
        dest.sin_port = htons(SSDP_PORT);
        inet_pton(AF_INET, SSDP_ADDR, &dest.sin_addr);
        int nc = 0;
        while (g_running)
        {
            sendto(nsock, notify.c_str(), (int)notify.size(), 0,
                   (struct sockaddr *)&dest, sizeof(dest));
            std::cout << "SSDP: NOTIFY #" << ++nc << "\n";
            for (int i = 0; i < 30 && g_running; i++)
                std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        PLAT_CLOSE(nsock);
    }

    resp.join();
    PLAT_CLOSE(rsock);
    std::cout << "SSDP: stopped\n";
}

/* ══════════════════════════════════════════════════════════════════════════
 * GPU SERVICE THREAD
 * ══════════════════════════════════════════════════════════════════════════ */
static void gpuServiceThread() { gpu_service_run(); }

/* ══════════════════════════════════════════════════════════════════════════
 * CLIENT CONNECTION HANDLER  –  screen extender + mirror
 * ══════════════════════════════════════════════════════════════════════════ */
static bool handleClientConnection(plat_sock_t client)
{
    int sb = SOCK_BUF;
    setsockopt(client, SOL_SOCKET, SO_RCVBUF, (char *)&sb, sizeof(sb));
#ifdef _WIN32
    DWORD to = 10000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (char *)&to, sizeof(to));
#else
    struct timeval tv = {10, 0};
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    /* ── Receive extended handshake ── */
    struct
    {
        uint32_t sender_width;
        uint32_t sender_height;
        uint32_t fps;
        uint32_t mode;
    } hs_in = {};

    if (recv_all(client, &hs_in, sizeof(hs_in)) < 0)
    {
        std::cerr << "Handshake receive failed\n";
        return false;
    }

    SCREEN_WIDTH = (int)ntohl(hs_in.sender_width);
    SCREEN_HEIGHT = (int)ntohl(hs_in.sender_height);
    TARGET_FPS = (int)ntohl(hs_in.fps);
    DISPLAY_MODE = ntohl(hs_in.mode);

    /* Validate */
    if (SCREEN_WIDTH <= 0 || SCREEN_WIDTH > 7680 ||
        SCREEN_HEIGHT <= 0 || SCREEN_HEIGHT > 4320 ||
        TARGET_FPS <= 0 || TARGET_FPS > 240)
    {
        std::cerr << "Invalid handshake\n";
        return false;
    }

    const char *modeStr =
        DISPLAY_MODE == MODE_EXTEND_RIGHT ? "extend-right" : DISPLAY_MODE == MODE_EXTEND_BELOW ? "extend-below"
                                                                                               : "mirror";

    std::cout << "Sender: " << SCREEN_WIDTH << "x" << SCREEN_HEIGHT
              << " @ " << TARGET_FPS << " FPS  mode=" << modeStr << "\n";

    /* ── Send back our display size ── */
    struct
    {
        uint32_t recv_width;
        uint32_t recv_height;
        uint32_t status;
    } hs_out = {
        htonl((uint32_t)MY_DISPLAY_WIDTH),
        htonl((uint32_t)MY_DISPLAY_HEIGHT),
        htonl(0u)};
    if (send_all_p(client, &hs_out, sizeof(hs_out)) < 0)
    {
        std::cerr << "Handshake response send failed\n";
        return false;
    }

    /* ── SDL init ── */
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        std::cerr << "SDL_Init: " << SDL_GetError() << "\n";
        return false;
    }

    /*
     * Window strategy
     * ───────────────
     * EXTEND modes → borderless fullscreen on the receiver's own display.
     *   SDL_WINDOW_FULLSCREEN_DESKTOP makes the window cover the entire
     *   display without changing the video mode — cross-platform and safe.
     *
     * MIRROR mode → normal resizable window, scaled to fit.
     */
    SDL_Window *win = nullptr;
    SDL_Renderer *ren = nullptr;

    if (DISPLAY_MODE == MODE_EXTEND_RIGHT ||
        DISPLAY_MODE == MODE_EXTEND_BELOW)
    {
        /* Borderless fullscreen — looks like a real second monitor */
        win = SDL_CreateWindow(
            "RGM Extended Display",
            SDL_WINDOWPOS_UNDEFINED_DISPLAY(0),
            SDL_WINDOWPOS_UNDEFINED_DISPLAY(0),
            MY_DISPLAY_WIDTH, MY_DISPLAY_HEIGHT,
            SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP);
    }
    else
    {
        /* Mirror: scale to a normal window */
        int ww = SCREEN_WIDTH, wh = SCREEN_HEIGHT;
        const int MAX_W = MY_DISPLAY_WIDTH - 60;
        const int MAX_H = MY_DISPLAY_HEIGHT - 100;
        if (ww > MAX_W || wh > MAX_H)
        {
            float sc = std::min((float)MAX_W / (float)ww,
                                (float)MAX_H / (float)wh);
            ww = (int)(ww * sc);
            wh = (int)(wh * sc);
        }
        win = SDL_CreateWindow(
            "RGM Mirror",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            ww, wh,
            SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    }

    if (!win)
    {
        std::cerr << "Window: " << SDL_GetError() << "\n";
        SDL_Quit();
        return false;
    }

    ren = SDL_CreateRenderer(win, -1,
                             SDL_RENDERER_ACCELERATED |
                                 SDL_RENDERER_PRESENTVSYNC);
    if (!ren)
    {
        std::cerr << "Renderer: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(win);
        SDL_Quit();
        return false;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

    /* In extend mode: scale sender's stream to fill our display */
    if (DISPLAY_MODE == MODE_EXTEND_RIGHT ||
        DISPLAY_MODE == MODE_EXTEND_BELOW)
    {
        SDL_RenderSetLogicalSize(ren, MY_DISPLAY_WIDTH, MY_DISPLAY_HEIGHT);
    }

    /* Texture at sender's native resolution (renderer scales it) */
    SDL_Texture *tex = SDL_CreateTexture(
        ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING,
        SCREEN_WIDTH, SCREEN_HEIGHT);
    if (!tex)
    {
        std::cerr << "Texture: " << SDL_GetError() << "\n";
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return false;
    }

    std::cout << "Display ready: "
              << SCREEN_WIDTH << "x" << SCREEN_HEIGHT
              << " → " << MY_DISPLAY_WIDTH << "x" << MY_DISPLAY_HEIGHT
              << " (" << modeStr << ")\n"
              << "ESC or Q to disconnect\n";

    /* Draw an initial "connecting" background so the screen goes dark
       rather than showing desktop bleed-through while waiting for frames */
    SDL_SetRenderDrawColor(ren, 5, 5, 15, 255);
    SDL_RenderClear(ren);

    /* Edge indicator: draw a thin coloured line on the join edge */
    if (DISPLAY_MODE == MODE_EXTEND_RIGHT)
    {
        SDL_SetRenderDrawColor(ren, 0, 140, 255, 255);
        SDL_RenderDrawLine(ren, 0, 0, 0, MY_DISPLAY_HEIGHT - 1);
    }
    else if (DISPLAY_MODE == MODE_EXTEND_BELOW)
    {
        SDL_SetRenderDrawColor(ren, 0, 140, 255, 255);
        SDL_RenderDrawLine(ren, 0, 0, MY_DISPLAY_WIDTH - 1, 0);
    }
    SDL_RenderPresent(ren);

    /* ── Receive and render frames ── */
    size_t full_frame = (size_t)SCREEN_WIDTH * SCREEN_HEIGHT * BYTES_PER_PIXEL;
    std::vector<uint8_t> frame;
    frame.reserve(full_frame);

    SDL_Event ev;
    bool streaming = true;
    int frames_rx = 0;
    auto start_time = std::chrono::steady_clock::now();

    while (streaming && g_running)
    {

        /* Event pump */
        while (SDL_PollEvent(&ev))
        {
            if (ev.type == SDL_QUIT)
            {
                streaming = false;
                g_running = false;
            }
            else if (ev.type == SDL_KEYDOWN)
            {
                SDL_Keycode k = ev.key.keysym.sym;
                if (k == SDLK_ESCAPE || k == SDLK_q)
                {
                    streaming = false;
                    g_running = false;
                }
                /* F11: toggle fullscreen (extend mode only) */
                if (k == SDLK_F11 &&
                    (DISPLAY_MODE == MODE_EXTEND_RIGHT ||
                     DISPLAY_MODE == MODE_EXTEND_BELOW))
                {
                    Uint32 flags = SDL_GetWindowFlags(win);
                    SDL_SetWindowFullscreen(
                        win,
                        (flags & SDL_WINDOW_FULLSCREEN_DESKTOP)
                            ? 0
                            : SDL_WINDOW_FULLSCREEN_DESKTOP);
                }
            }
        }
        if (!streaming)
            break;

        /* 4-byte size header */
        uint32_t net_sz = 0;
        int n = (int)recv(client, (char *)&net_sz, 4, 0);
        if (n <= 0)
        {
            if (n == 0)
                std::cout << "Sender disconnected\n";
            else
                std::cerr << "Recv error (size)\n";
            break;
        }
        uint32_t frame_sz = ntohl(net_sz);
        if (frame_sz == 0 || frame_sz > MAX_FRAME_BYTES)
        {
            std::cerr << "Bad frame size: " << frame_sz << "\n";
            break;
        }

        /* Frame data */
        frame.resize(frame_sz);
        size_t got = 0;
        while (got < frame_sz)
        {
            n = (int)recv(client, (char *)frame.data() + got,
                          (int)(frame_sz - got), 0);
            if (n <= 0)
            {
                streaming = false;
                break;
            }
            got += (size_t)n;
        }
        if (!streaming)
            break;

        /*
         * RLE decompression
         * If the frame is smaller than a raw RGB frame it was RLE-compressed
         * by the GPU service. Decompress: stream of [run r g b] tuples.
         */
        if (frame_sz < (uint32_t)full_frame)
        {
            std::vector<uint8_t> dec;
            dec.reserve(full_frame);
            const uint8_t *src = frame.data();
            const uint8_t *end = src + frame_sz;
            while (src + 3 < end && dec.size() < full_frame)
            {
                uint8_t run = *src++;
                uint8_t r = *src++;
                uint8_t g = *src++;
                uint8_t b = *src++;
                for (uint8_t j = 0;
                     j < run && dec.size() + 3 <= full_frame; j++)
                {
                    dec.push_back(r);
                    dec.push_back(g);
                    dec.push_back(b);
                }
            }
            while (dec.size() < full_frame)
                dec.push_back(0);
            frame = std::move(dec);
        }

        /* Render */
        SDL_UpdateTexture(tex, nullptr, frame.data(),
                          SCREEN_WIDTH * BYTES_PER_PIXEL);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);

        /* Re-draw edge glow on top of each frame in extend mode */
        if (DISPLAY_MODE == MODE_EXTEND_RIGHT)
        {
            SDL_SetRenderDrawColor(ren, 0, 140, 255, 120);
            SDL_RenderDrawLine(ren, 0, 0, 0, MY_DISPLAY_HEIGHT - 1);
            SDL_RenderDrawLine(ren, 1, 0, 1, MY_DISPLAY_HEIGHT - 1);
        }
        else if (DISPLAY_MODE == MODE_EXTEND_BELOW)
        {
            SDL_SetRenderDrawColor(ren, 0, 140, 255, 120);
            SDL_RenderDrawLine(ren, 0, 0, MY_DISPLAY_WIDTH - 1, 0);
            SDL_RenderDrawLine(ren, 0, 1, MY_DISPLAY_WIDTH - 1, 1);
        }

        SDL_RenderPresent(ren);
        frames_rx++;

        if (frames_rx % 100 == 0)
        {
            auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::steady_clock::now() - start_time)
                            .count();
            if (secs > 0)
                std::cout << "Frames: " << frames_rx
                          << "  FPS: " << std::fixed << std::setprecision(1)
                          << (float)frames_rx / (float)secs
                          << "  (" << modeStr << ")\n";
        }
    }

    /* Summary */
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - start_time)
                    .count();
    std::cout << "========================================\n"
              << "  RECEIVER SESSION\n"
              << "========================================\n"
              << "  Mode     : " << modeStr << "\n"
              << "  Src res  : " << SCREEN_WIDTH << "x" << SCREEN_HEIGHT << "\n"
              << "  My res   : " << MY_DISPLAY_WIDTH << "x" << MY_DISPLAY_HEIGHT << "\n"
              << "  Frames   : " << frames_rx << "\n"
              << "  Duration : " << secs << " s\n";
    if (secs > 0)
        std::cout << "  Avg FPS  : " << frames_rx / secs << "\n";
    std::cout << "========================================\n";

    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return true;
}

/* ══════════════════════════════════════════════════════════════════════════
 * MAIN
 * ══════════════════════════════════════════════════════════════════════════ */
int main()
{
#ifdef _WIN32
    SetConsoleCtrlHandler(consoleHandler, TRUE);
#else
    struct sigaction sa = {};
    sa.sa_handler = sigHandler;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
#endif

    showSplashScreen();
    queryMyDisplaySize();

    std::cout << "========================================\n"
              << "  RGM RECEIVER v2.0  –  Screen Extender\n"
              << "========================================\n"
              << "  Local IP   : " << getLocalIPAddress() << "\n"
              << "  My display : " << MY_DISPLAY_WIDTH
              << "x" << MY_DISPLAY_HEIGHT << "\n"
              << "  Stream TCP : " << TCP_STREAM_PORT << "\n"
              << "  GPU TCP    : " << GPU_ACCEL_PORT << "\n"
              << "  SSDP UDP   : " << SSDP_ADDR << ":" << SSDP_PORT << "\n"
              << "  Modes      : extend-right | extend-below | mirror\n"
              << "========================================\n";

    if (!initSockets())
    {
        std::cerr << "Socket init failed\n";
        return 1;
    }

    std::thread gpu_thread(gpuServiceThread);
    std::thread ssdp_thread(ssdpAdvertisementThread);

    /* TCP stream server */
    plat_sock_t srv = socket(AF_INET, SOCK_STREAM, 0);
    if (!PLAT_VALID(srv))
    {
        std::cerr << "Server socket failed\n";
        g_running = false;
        ssdp_thread.join();
        gpu_thread.join();
        cleanupSockets();
        return 1;
    }

    int reuse = 1, sb = SOCK_BUF;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));
    setsockopt(srv, SOL_SOCKET, SO_RCVBUF, (char *)&sb, sizeof(sb));

    struct sockaddr_in saddr = {};
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(TCP_STREAM_PORT);
    saddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(srv, (struct sockaddr *)&saddr, sizeof(saddr)) < 0)
    {
        std::cerr << "Bind failed on " << TCP_STREAM_PORT << "\n";
        PLAT_CLOSE(srv);
        g_running = false;
        ssdp_thread.join();
        gpu_thread.join();
        cleanupSockets();
        return 1;
    }
    listen(srv, 5);
    std::cout << "Waiting for sender on TCP " << TCP_STREAM_PORT << " ...\n";

    while (g_running)
    {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(srv, &fds);
        struct timeval tv = {1, 0};
        int act = select((int)srv + 1, &fds, nullptr, nullptr, &tv);
        if (act < 0 || !g_running)
            break;
        if (act == 0)
            continue;

        struct sockaddr_in cli = {};
        ACCEPT_LEN_T cli_len = sizeof(cli);
        plat_sock_t client = accept(srv, (struct sockaddr *)&cli,
                                    (ACCEPT_LEN_T *)&cli_len);
        if (!PLAT_VALID(client))
            continue;

        std::cout << "Sender connected: " << inet_ntoa(cli.sin_addr) << "\n";
        handleClientConnection(client);
        PLAT_CLOSE(client);
        if (g_running)
            std::cout << "Waiting for next sender...\n";
    }

    PLAT_CLOSE(srv);
    g_running = false; /* signal threads before joining */
    ssdp_thread.join();
    gpu_thread.join();
    cleanupSockets();
    std::cout << "Receiver shut down\n";
    return 0;
}