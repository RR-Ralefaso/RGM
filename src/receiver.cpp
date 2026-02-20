/**
 * RECEIVER.CPP - IMPROVED VERSION
 * Better error handling, socket options, and thread management
 */
#include <iostream>
#include <cstring>
#include <vector>
#include <thread>
#include <chrono>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <SDL2/SDL.h>
#include "discover.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#endif

#define TCP_STREAM_PORT 8081
#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720
#define BYTES_PER_PIXEL 3
#define SSDP_ADDRESS "239.255.255.250"
#define SSDP_PORT 1900

// Global flag for thread shutdown
std::atomic<bool> g_running{true};

/**
 * Initialize sockets (Windows only)
 */
bool initSockets()
{
#ifdef _WIN32
    WSADATA wsa;
    int result = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (result != 0)
    {
        std::cerr << "❌ WSAStartup failed: " << result << std::endl;
        return false;
    }
#endif
    return true;
}

/**
 * Cleanup sockets
 */
void cleanupSockets()
{
#ifdef _WIN32
    WSACleanup();
#endif
}

/**
 * Create a socket with proper error handling
 */
int createSocket(int domain, int type, int protocol)
{
#ifdef _WIN32
    SOCKET sock = socket(domain, type, protocol);
    if (sock == INVALID_SOCKET)
    {
        std::cerr << "❌ Socket creation failed: " << WSAGetLastError() << std::endl;
        return -1;
    }
    return (int)sock;
#else
    int sock = socket(domain, type, protocol);
    if (sock < 0)
    {
        std::cerr << "❌ Socket creation failed: " << strerror(errno) << std::endl;
        return -1;
    }
    return sock;
#endif
}

/**
 * Close a socket
 */
void closeSocket(int sock)
{
    if (sock >= 0)
    {
#ifdef _WIN32
        closesocket((SOCKET)sock);
#else
        close(sock);
#endif
    }
}

/**
 * SSDP advertisement thread function
 * Announces this receiver on the network
 */
void ssdpAdvertisementThread()
{
    std::cout << "📡 Starting SSDP advertiser thread..." << std::endl;

    // Create socket for receiving M-SEARCH queries
    int response_sock = createSocket(AF_INET, SOCK_DGRAM, 0);
    if (response_sock < 0)
    {
        std::cerr << "❌ Failed to create SSDP response socket" << std::endl;
        return;
    }

    // Allow address reuse
    int reuse = 1;
    setsockopt(response_sock, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));

    // Join multicast group
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(SSDP_ADDRESS);
    mreq.imr_interface.s_addr = INADDR_ANY;

    if (setsockopt(response_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                   (char *)&mreq, sizeof(mreq)) < 0)
    {
        std::cerr << "❌ Failed to join multicast group" << std::endl;
        closeSocket(response_sock);
        return;
    }

    // Bind to SSDP port
    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind_addr.sin_port = htons(SSDP_PORT);

    if (bind(response_sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0)
    {
        std::cerr << "❌ Failed to bind to port " << SSDP_PORT << std::endl;
        closeSocket(response_sock);
        return;
    }

    std::cout << "📡 Listening for SSDP M-SEARCH queries on port " << SSDP_PORT << std::endl;

    // Thread for responding to M-SEARCH queries
    std::thread response_thread([response_sock]()
                                {
        char buffer[2048];
        struct sockaddr_in sender;
        socklen_t sender_len = sizeof(sender);
        std::string local_ip = getLocalIPAddress();
        
        while (g_running)
        {
            // Set receive timeout to check g_running periodically
            struct timeval tv;
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            setsockopt(response_sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));
            
            int bytes = recvfrom(response_sock, buffer, sizeof(buffer) - 1, 0,
                               (struct sockaddr*)&sender, &sender_len);
            
            if (!g_running)
                break;
                
            if (bytes > 0)
            {
                buffer[bytes] = '\0';
                std::string request(buffer);
                
                // Check for M-SEARCH with our service type
                if (request.find("M-SEARCH") != std::string::npos && 
                    request.find("urn:screen-share:receiver") != std::string::npos)
                {
                    std::cout << "📡 Received M-SEARCH from " 
                              << inet_ntoa(sender.sin_addr) << std::endl;
                    
                    // Build response
                    std::string response = 
                        "HTTP/1.1 200 OK\r\n"
                        "CACHE-CONTROL: max-age=30\r\n"
                        "DATE: " + std::to_string(time(nullptr)) + "\r\n"
                        "LOCATION: http://" + local_ip + ":" + 
                        std::to_string(TCP_STREAM_PORT) + "/\r\n"
                        "SERVER: ScreenShare/1.0\r\n"
                        "ST: urn:screen-share:receiver\r\n"
                        "USN: uuid:screen-share-" + local_ip + "\r\n"
                        "\r\n";
                    
                    // Send response
                    sendto(response_sock, response.c_str(), response.length(), 0,
                           (struct sockaddr*)&sender, sender_len);
                    
                    std::cout << "📡 Sent SSDP response to " 
                              << inet_ntoa(sender.sin_addr) << std::endl;
                }
            }
        } });

    // Create socket for sending NOTIFY announcements
    int notify_sock = createSocket(AF_INET, SOCK_DGRAM, 0);
    if (notify_sock >= 0)
    {
        // Allow broadcast
        int broadcast = 1;
        setsockopt(notify_sock, SOL_SOCKET, SO_BROADCAST,
                   (char *)&broadcast, sizeof(broadcast));

        // Set TTL for multicast
        int ttl = 4;
        setsockopt(notify_sock, IPPROTO_IP, IP_MULTICAST_TTL,
                   (char *)&ttl, sizeof(ttl));

        // Prepare NOTIFY message
        std::string local_ip = getLocalIPAddress();
        std::string notify_msg =
            "NOTIFY * HTTP/1.1\r\n"
            "HOST: " +
            std::string(SSDP_ADDRESS) + ":" + std::to_string(SSDP_PORT) + "\r\n"
                                                                          "CACHE-CONTROL: max-age=30\r\n"
                                                                          "LOCATION: http://" +
            local_ip + ":" + std::to_string(TCP_STREAM_PORT) + "/\r\n"
                                                               "NT: urn:screen-share:receiver\r\n"
                                                               "NTS: ssdp:alive\r\n"
                                                               "SERVER: ScreenShare/1.0\r\n"
                                                               "USN: uuid:screen-share-" +
            local_ip + "\r\n"
                       "\r\n";

        // Multicast address
        struct sockaddr_in dest_addr;
        memset(&dest_addr, 0, sizeof(dest_addr));
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(SSDP_PORT);
        inet_pton(AF_INET, SSDP_ADDRESS, &dest_addr.sin_addr);

        std::cout << "📡 Sending SSDP NOTIFY announcements every 30 seconds" << std::endl;

        // Send NOTIFY announcements while running
        int notify_count = 0;
        while (g_running)
        {
            int sent = sendto(notify_sock, notify_msg.c_str(), notify_msg.length(), 0,
                              (struct sockaddr *)&dest_addr, sizeof(dest_addr));

            if (sent > 0)
            {
                notify_count++;
                std::cout << "📡 SSDP NOTIFY #" << notify_count << " sent" << std::endl;
            }

            // Wait 30 seconds, checking g_running every second
            for (int i = 0; i < 30 && g_running; i++)
                std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        closeSocket(notify_sock);
    }

    // Wait for response thread to finish
    g_running = false; // Signal response thread to stop
    response_thread.join();
    closeSocket(response_sock);

    std::cout << "📡 SSDP advertiser stopped" << std::endl;
}

/**
 * Handle a single client connection
 * Returns true if connection was handled successfully
 */
bool handleClientConnection(int client_sock)
{
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        std::cerr << "❌ SDL initialization failed: " << SDL_GetError() << std::endl;
        return false;
    }

    // Create window (half size for display)
    SDL_Window *window = SDL_CreateWindow("Screen Share Receiver",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          SCREEN_WIDTH / 2,
                                          SCREEN_HEIGHT / 2,
                                          SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

    if (!window)
    {
        std::cerr << "❌ Window creation failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return false;
    }

    // Create renderer
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1,
                                                SDL_RENDERER_ACCELERATED |
                                                    SDL_RENDERER_PRESENTVSYNC);

    if (!renderer)
    {
        std::cerr << "❌ Renderer creation failed: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
    }

    // Create texture for streaming
    SDL_Texture *texture = SDL_CreateTexture(renderer,
                                             SDL_PIXELFORMAT_RGB24,
                                             SDL_TEXTUREACCESS_STREAMING,
                                             SCREEN_WIDTH,
                                             SCREEN_HEIGHT);

    if (!texture)
    {
        std::cerr << "❌ Texture creation failed: " << SDL_GetError() << std::endl;
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
    }

    std::cout << "✅ SDL initialized successfully" << std::endl;

    // Set socket receive timeout to prevent blocking forever
#ifdef _WIN32
    int timeout = 5000; // 5 seconds
    setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    std::vector<uint8_t> frame(SCREEN_WIDTH * SCREEN_HEIGHT * BYTES_PER_PIXEL);
    SDL_Event event;
    bool streaming = true;
    int frames_received = 0;
    auto start_time = std::chrono::steady_clock::now();

    while (streaming && g_running)
    {
        // Handle SDL events
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
            {
                streaming = false;
                g_running = false;
            }
            else if (event.type == SDL_KEYDOWN)
            {
                if (event.key.keysym.sym == SDLK_ESCAPE ||
                    event.key.keysym.sym == SDLK_q)
                {
                    streaming = false;
                    g_running = false;
                }
            }
        }

        // Receive frame size (4 bytes)
        uint32_t net_frame_size;
        int bytes_received = recv(client_sock, (char *)&net_frame_size, 4, 0);

        if (bytes_received <= 0)
        {
            if (bytes_received == 0)
                std::cout << "🔌 Sender disconnected" << std::endl;
            else
                std::cerr << "❌ Error receiving frame size" << std::endl;
            break;
        }

        // Convert from network byte order
        uint32_t frame_size = ntohl(net_frame_size);

        // Validate frame size (sanity check)
        if (frame_size != frame.size())
        {
            std::cerr << "❌ Invalid frame size: " << frame_size
                      << " (expected " << frame.size() << ")" << std::endl;
            break;
        }

        // Receive frame data
        size_t total_received = 0;
        while (total_received < frame_size)
        {
            bytes_received = recv(client_sock,
                                  (char *)frame.data() + total_received,
                                  frame_size - total_received, 0);

            if (bytes_received <= 0)
            {
                std::cerr << "❌ Error receiving frame data" << std::endl;
                streaming = false;
                break;
            }

            total_received += bytes_received;
        }

        if (!streaming)
            break;

        // Update texture and render
        SDL_UpdateTexture(texture, NULL, frame.data(),
                          SCREEN_WIDTH * BYTES_PER_PIXEL);

        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);

        frames_received++;

        // Show stats every 100 frames
        if (frames_received % 100 == 0)
        {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                               now - start_time)
                               .count();

            float fps = frames_received / (float)elapsed;
            std::cout << "📊 Frames: " << frames_received
                      << " | FPS: " << fps << std::endl;
        }
    }

    // Cleanup SDL
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    std::cout << "📊 Total frames received: " << frames_received << std::endl;

    return true;
}

int main()
{
    std::cout << "========================================" << std::endl;
    std::cout << "📺 SCREEN SHARE RECEIVER v2.0" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Local IP: " << getLocalIPAddress() << std::endl;
    std::cout << "TCP Port: " << TCP_STREAM_PORT << std::endl;
    std::cout << "SSDP:     " << SSDP_ADDRESS << ":" << SSDP_PORT << std::endl;
    std::cout << "========================================" << std::endl;

    // Initialize sockets
    if (!initSockets())
    {
        std::cerr << "❌ Failed to initialize sockets" << std::endl;
        return 1;
    }

    // Start SSDP advertisement in a separate thread
    std::thread ssdp_thread(ssdpAdvertisementThread);

    // Create TCP server socket
    int server_sock = createSocket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0)
    {
        cleanupSockets();
        return 1;
    }

    // Allow address reuse (fixes "address already in use" errors)
    int reuse = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR,
                   (char *)&reuse, sizeof(reuse)) < 0)
    {
        std::cerr << "⚠️  Failed to set SO_REUSEADDR" << std::endl;
    }

    // Bind to port
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(TCP_STREAM_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        std::cerr << "❌ Failed to bind to port " << TCP_STREAM_PORT << std::endl;
        closeSocket(server_sock);
        cleanupSockets();
        return 1;
    }

    // Listen for connections
    if (listen(server_sock, 5) < 0) // Backlog of 5
    {
        std::cerr << "❌ Failed to listen on socket" << std::endl;
        closeSocket(server_sock);
        cleanupSockets();
        return 1;
    }

    std::cout << "⏳ Waiting for sender connection on port "
              << TCP_STREAM_PORT << "..." << std::endl;

    // Main loop - accept and handle connections
    while (g_running)
    {
        // Accept a connection (with timeout using select)
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(server_sock, &readfds);

        struct timeval tv;
        tv.tv_sec = 1; // 1 second timeout
        tv.tv_usec = 0;

        int activity = select(server_sock + 1, &readfds, NULL, NULL, &tv);

        if (!g_running)
            break;

        if (activity < 0)
        {
            std::cerr << "❌ Select error" << std::endl;
            continue;
        }

        if (activity == 0)
            continue; // Timeout, check g_running again

        // Accept connection
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_len);

        if (client_sock < 0)
        {
            std::cerr << "❌ Failed to accept connection" << std::endl;
            continue;
        }

        std::cout << "✅ Sender connected from "
                  << inet_ntoa(client_addr.sin_addr) << std::endl;

        // Handle the client connection
        handleClientConnection(client_sock);

        // Close client socket
        closeSocket(client_sock);

        std::cout << "⏳ Waiting for next sender..." << std::endl;
    }

    // Cleanup
    closeSocket(server_sock);
    g_running = false;
    ssdp_thread.join();
    cleanupSockets();

    std::cout << "📺 Receiver shut down" << std::endl;
    return 0;
}