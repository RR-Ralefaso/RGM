#include <iostream>   // Console status messages
#include <cstring>    // memset() for socket structures
#include <vector>     // Frame buffer storage (RGB24)
#include <thread>     // SSDP advertising background thread
#include <SDL2/SDL.h> // Cross-platform window + GPU rendering
#include "discover.h" // SSDP constants (multicast group, service type)

/**
 * ============================================================================
 * PLATFORM-SPECIFIC NETWORKING HEADERS
 * ============================================================================
 */
#ifdef _WIN32
#include <winsock2.h>              // Windows socket API
#pragma comment(lib, "ws2_32.lib") // Auto-link Winsock
#else
#include <sys/socket.h> // POSIX sockets
#include <netinet/in.h> // sockaddr_in
#include <arpa/inet.h>  // inet_pton
#include <unistd.h>     // close()
#endif

// ============================================================================
// CONFIGURATION
// ============================================================================
#define TCP_STREAM_PORT 8081 // TCP port advertised via SSDP
#define SCREEN_WIDTH 1280    // Expected sender resolution
#define SCREEN_HEIGHT 720
#define BYTES_PER_PIXEL 3 // RGB24 format

/**
 * ============================================================================
 * SSDP ADVERTISING THREAD - Makes receiver discoverable
 * ============================================================================
 * Runs in background, sends NOTIFY messages every 30 seconds to multicast group
 * Senders detect these advertisements via M-SEARCH responses
 */
void advertiseViaSSDP()
{
    std::cout << "📡 Starting SSDP advertiser (239.255.255.250:1900)..." << std::endl;

#ifdef _WIN32
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
#endif

    // Create UDP socket for SSDP advertisements
    int ssdp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (ssdp_socket < 0)
    {
        std::cerr << "❌ SSDP socket creation failed!" << std::endl;
        return;
    }

    // Join SSDP multicast group (receive our own messages for testing)
    struct ip_mreq multicast_membership;
    multicast_membership.imr_multiaddr.s_addr = inet_addr("239.255.255.250");
    multicast_membership.imr_interface.s_addr = INADDR_ANY;
    setsockopt(ssdp_socket, IPPROTO_IP, IP_ADD_MEMBERSHIP,
               (char *)&multicast_membership, sizeof(multicast_membership));

    // TODO: Auto-detect local IP address (instead of hardcoded)
    std::string local_ip_address = "192.168.1.100";

    // SSDP NOTIFY message - advertises our availability
    std::string ssdp_notify_message =
        "NOTIFY * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "CACHE-CONTROL: max-age=30\r\n"
        "LOCATION: http://" +
        local_ip_address + ":" +
        std::to_string(TCP_STREAM_PORT) + "/\r\n"
                                          "NT: urn:screen-share:receiver\r\n"
                                          "NTS: ssdp:alive\r\n"
                                          "SERVER: ScreenShareReceiver/1.0\r\n"
                                          "\r\n";

    // Multicast destination (all SSDP receivers)
    sockaddr_in multicast_destination;
    memset(&multicast_destination, 0, sizeof(multicast_destination));
    multicast_destination.sin_family = AF_INET;
    multicast_destination.sin_port = htons(1900); // SSDP port
    inet_pton(AF_INET, "239.255.255.250", &multicast_destination.sin_addr);

    // Infinite advertising loop (UPnP standard: every 30 seconds)
    while (true)
    {
        sendto(ssdp_socket, ssdp_notify_message.c_str(),
               ssdp_notify_message.size(), 0,
               (sockaddr *)&multicast_destination, sizeof(multicast_destination));

        std::cout << "📡 SSDP advertisement sent (" << local_ip_address
                  << ":" << TCP_STREAM_PORT << ")" << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(30));
    }

    // Socket auto-closed when thread ends
#ifdef _WIN32
    closesocket(ssdp_socket);
#else
    close(ssdp_socket);
#endif
}

/**
 * ============================================================================
 * MAIN PROGRAM - Receives + Displays Screen Stream
 * ============================================================================
 * 1. Starts SSDP advertiser thread (makes us discoverable)
 * 2. Listens for TCP connections on port 8081
 * 3. Receives length-prefixed RGB24 frames
 * 4. Displays via hardware-accelerated SDL2 rendering
 */
int main()
{
    std::cout << "📺 AUTO-DISCOVERY SCREEN RECEIVER v1.0" << std::endl;
    std::cout << "TCP Port: " << TCP_STREAM_PORT << std::endl;
    std::cout << "Resolution: " << SCREEN_WIDTH << "x" << SCREEN_HEIGHT << std::endl;
    std::cout << "========================================" << std::endl;

    // === STEP 1: START SSDP ADVERTISING (BACKGROUND THREAD) ===
    std::thread ssdp_advertiser_thread(advertiseViaSSDP);
    ssdp_advertiser_thread.detach(); // Run independently

    // === STEP 2: TCP SERVER SETUP ===
#ifdef _WIN32
    WSADATA winsock_data;
    WSAStartup(MAKEWORD(2, 2), &winsock_data);
#endif

    int tcp_server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_server_socket < 0)
    {
        std::cerr << "❌ TCP server socket creation failed!" << std::endl;
        return 1;
    }

    // Bind to all interfaces on streaming port
    sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;              // IPv4
    server_address.sin_port = htons(TCP_STREAM_PORT); // Network byte order
    server_address.sin_addr.s_addr = INADDR_ANY;      // 0.0.0.0 = all network interfaces

    if (bind(tcp_server_socket, (sockaddr *)&server_address, sizeof(server_address)) < 0)
    {
        std::cerr << "❌ Failed to bind port " << TCP_STREAM_PORT
                  << " (port in use?)" << std::endl;
        return 1;
    }

    // Listen for sender connections (queue up to 1 connection)
    if (listen(tcp_server_socket, 1) < 0)
    {
        std::cerr << "❌ TCP listen failed!" << std::endl;
        return 1;
    }

    std::cout << "⏳ Waiting for sender connection on port " << TCP_STREAM_PORT << "..." << std::endl;

    // === STEP 3: ACCEPT SENDER CONNECTION ===
    sockaddr_in sender_address;
    socklen_t sender_address_length = sizeof(sender_address);
    int sender_socket = accept(tcp_server_socket, (sockaddr *)&sender_address,
                               &sender_address_length);

    if (sender_socket < 0)
    {
        std::cerr << "❌ Failed to accept sender connection!" << std::endl;
        return 1;
    }

    std::cout << "✅ Sender connected! Starting video display..." << std::endl;

    // === STEP 4: SDL2 VIDEO DISPLAY SETUP ===
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        std::cerr << "❌ SDL2 initialization failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    // Create resizable viewer window (half-size for easy viewing)
    SDL_Window *video_window = SDL_CreateWindow(
        "🔴 LIVE SCREEN SHARE (ESC or Close to Quit)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, // Half-size viewer
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

    if (!video_window)
    {
        std::cerr << "❌ SDL window creation failed!" << std::endl;
        SDL_Quit();
        return 1;
    }

    // Hardware-accelerated renderer + RGB24 texture
    SDL_Renderer *sdl_renderer = SDL_CreateRenderer(video_window, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture *video_texture = SDL_CreateTexture(
        sdl_renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING,
        SCREEN_WIDTH, SCREEN_HEIGHT);

    // Frame buffer for incoming pixel data
    std::vector<uint8_t> frame_pixels(SCREEN_WIDTH * SCREEN_HEIGHT * BYTES_PER_PIXEL);
    int total_frames_displayed = 0;

    // === STEP 5: MAIN VIDEO DECODE + DISPLAY LOOP ===
    SDL_Event sdl_event;
    bool display_running = true;

    std::cout << "🎬 Live video decode active (ESC to quit)..." << std::endl;

    while (display_running)
    {
        // Handle window close/ESC key events
        while (SDL_PollEvent(&sdl_event))
        {
            if (sdl_event.type == SDL_QUIT ||
                (sdl_event.type == SDL_KEYDOWN && sdl_event.key.keysym.sym == SDLK_ESCAPE))
            {
                display_running = false;
            }
        }

        // Receive frame size header (first 4 bytes of each frame)
        uint32_t frame_size_network_order;
        int header_bytes_received = recv(sender_socket, (char *)&frame_size_network_order,
                                         4, 0);
        if (header_bytes_received != 4)
        {
            std::cout << "❌ Sender disconnected" << std::endl;
            break;
        }

        uint32_t frame_size_bytes = ntohl(frame_size_network_order); // Network → host order

        // Receive exact frame data (TCP guarantees delivery order)
        int total_bytes_received = 0;
        while (total_bytes_received < frame_size_bytes)
        {
            int bytes_this_receive = recv(sender_socket,
                                          frame_pixels.data() + total_bytes_received,
                                          frame_size_bytes - total_bytes_received, 0);
            if (bytes_this_receive <= 0)
            {
                std::cout << "❌ Receive error - sender dropped connection" << std::endl;
                goto cleanup_and_exit;
            }
            total_bytes_received += bytes_this_receive;
        }

        // GPU texture update (hardware upload)
        SDL_UpdateTexture(video_texture, NULL, frame_pixels.data(),
                          SCREEN_WIDTH * BYTES_PER_PIXEL);

        // Render frame to screen (triple-buffered, VSync)
        SDL_RenderClear(sdl_renderer);
        SDL_RenderCopy(sdl_renderer, video_texture, NULL, NULL);
        SDL_RenderPresent(sdl_renderer);

        total_frames_displayed++;

        // Prevent 100% CPU usage
        SDL_Delay(5);
    }

cleanup_and_exit:
    std::cout << "📊 Total frames displayed: " << total_frames_displayed << std::endl;

    // === STEP 6: CLEANUP RESOURCES ===
    SDL_DestroyTexture(video_texture);
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(video_window);
    SDL_Quit();

    // Close network sockets
#ifdef _WIN32
    closesocket(sender_socket);
    closesocket(tcp_server_socket);
    WSACleanup();
#else
    close(sender_socket);
    close(tcp_server_socket);
#endif

    std::cout << "🔌 Receiver shutdown complete" << std::endl;
    return 0;
}
