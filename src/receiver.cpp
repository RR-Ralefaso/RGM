/**
 * ============================================================================
 * RECEIVER.CPP - SCREEN SHARING RECEIVER WITH SSDP ADVERTISING
 * ============================================================================
 * This program receives and displays screen streams from senders.
 * Features:
 * - SSDP advertising to make itself discoverable
 * - TCP server listening for sender connections
 * - Hardware-accelerated video display via SDL2
 * - Automatic detection of sender disconnection
 *
 * Workflow:
 * 1. Start SSDP advertiser thread (background)
 * 2. Listen for TCP connections on port 8081
 * 3. Accept sender connection
 * 4. Initialize SDL2 window and renderer
 * 5. Receive and display frames until sender disconnects
 */

#include <iostream>   // Console status messages
#include <cstring>    // memset() for socket structures
#include <vector>     // Frame buffer storage
#include <thread>     // SSDP advertising background thread
#include <SDL2/SDL.h> // Cross-platform window + GPU rendering
#include "discover.h" // SSDP constants and utilities

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
// CONFIGURATION CONSTANTS
// ============================================================================
#define TCP_STREAM_PORT 8081 // Port advertised via SSDP for streaming
#define SCREEN_WIDTH 1280    // Expected sender resolution
#define SCREEN_HEIGHT 720
#define BYTES_PER_PIXEL 3 // RGB24 format

/**
 * ============================================================================
 * GET LOCAL IP ADDRESS (Forward Declaration)
 * ============================================================================
 * Implemented in discover.cpp, used for building LOCATION URLs
 */
std::string getLocalIPAddress();

/**
 * ============================================================================
 * SSDP ADVERTISING THREAD
 * ============================================================================
 * Runs in background, sends periodic NOTIFY messages to multicast group
 * and responds to M-SEARCH queries from senders.
 *
 * SSDP Advertisements:
 * - Sent every 30 seconds (UPnP standard)
 * - Contain LOCATION header with our IP:PORT
 * - Include service type "urn:screen-share:receiver"
 *
 * M-SEARCH Responses:
 * - Respond directly to sender's unicast address
 * - Include same LOCATION information
 */
void advertiseViaSSDP()
{
    std::cout << "📡 Starting SSDP advertiser (239.255.255.250:1900)..." << std::endl;

    /**
     * Initialize Windows networking if needed
     */
#ifdef _WIN32
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
#endif

    /**
     * Create UDP socket for sending advertisements
     * SOCK_DGRAM = UDP protocol
     */
    int ssdp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (ssdp_socket < 0)
    {
        std::cerr << "❌ SSDP socket creation failed!" << std::endl;
        return;
    }

    /**
     * Enable broadcast capability
     * Required for sending to multicast groups
     */
    int broadcast = 1;
    setsockopt(ssdp_socket, SOL_SOCKET, SO_BROADCAST,
               (char *)&broadcast, sizeof(broadcast));

    /**
     * Get local IP address for LOCATION header
     * This tells senders how to connect to us
     */
    std::string local_ip_address = getLocalIPAddress();

    /**
     * Prepare SSDP NOTIFY message (periodic advertisement)
     * Format: HTTP-style headers with UPnP/SSDP fields
     */
    std::string ssdp_notify_message =
        "NOTIFY * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "CACHE-CONTROL: max-age=30\r\n"
        "LOCATION: http://" +
        local_ip_address + ":" + std::to_string(TCP_STREAM_PORT) + "/\r\n"
                                                                   "NT: urn:screen-share:receiver\r\n"
                                                                   "NTS: ssdp:alive\r\n"
                                                                   "SERVER: ScreenShareReceiver/1.0\r\n"
                                                                   "USN: uuid:" +
        std::to_string(std::hash<std::string>{}(local_ip_address)) + "::urn:screen-share:receiver\r\n"
                                                                     "\r\n";

    /**
     * Create separate socket for responding to M-SEARCH queries
     * Binds to SSDP port to receive discovery requests
     */
    int response_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (response_socket >= 0)
    {
        // Bind to SSDP port (1900) to receive M-SEARCH requests
        sockaddr_in response_addr;
        memset(&response_addr, 0, sizeof(response_addr));
        response_addr.sin_family = AF_INET;
        response_addr.sin_addr.s_addr = INADDR_ANY; // All interfaces
        response_addr.sin_port = htons(1900);       // SSDP port

        bind(response_socket, (sockaddr *)&response_addr, sizeof(response_addr));

        /**
         * Start response thread to handle M-SEARCH queries
         * This runs in parallel with advertisement thread
         */
        std::thread response_thread([response_socket, local_ip_address]()
                                    {
            char buffer[2048];
            sockaddr_in sender_addr;
            socklen_t sender_len = sizeof(sender_addr);
            
            while (true) {
                memset(buffer, 0, sizeof(buffer));
                
                /**
                 * Wait for incoming M-SEARCH requests
                 * recvfrom blocks until data arrives
                 */
                int received = recvfrom(response_socket, buffer, sizeof(buffer) - 1, 0,
                                       (sockaddr *)&sender_addr, &sender_len);
                
                if (received > 0) {
                    std::string request(buffer);
                    
                    /**
                     * Check if this is an M-SEARCH for our service
                     * Look for both M-SEARCH method and our service type
                     */
                    if (request.find("M-SEARCH") != std::string::npos &&
                        request.find("urn:screen-share:receiver") != std::string::npos) {
                        
                        /**
                         * Build HTTP 200 OK response
                         * Send directly to requester's unicast address
                         */
                        std::string response =
                            "HTTP/1.1 200 OK\r\n"
                            "CACHE-CONTROL: max-age=30\r\n"
                            "DATE: \r\n"
                            "EXT: \r\n"
                            "LOCATION: http://" + local_ip_address + ":" + std::to_string(TCP_STREAM_PORT) + "/\r\n"
                            "SERVER: ScreenShareReceiver/1.0\r\n"
                            "ST: urn:screen-share:receiver\r\n"
                            "USN: uuid:12345678::urn:screen-share:receiver\r\n"
                            "Content-Length: 0\r\n"
                            "\r\n";
                        
                        /**
                         * Send response back to sender
                         * This is unicast, not multicast
                         */
                        sendto(response_socket, response.c_str(), response.length(), 0,
                               (sockaddr *)&sender_addr, sender_len);
                        
                        std::cout << "📡 Responded to M-SEARCH from " 
                                  << inet_ntoa(sender_addr.sin_addr) << std::endl;
                    }
                }
            } });
        response_thread.detach(); // Run independently
    }

    /**
     * Set up multicast destination for advertisements
     * All SSDP listeners will receive these
     */
    sockaddr_in multicast_destination;
    memset(&multicast_destination, 0, sizeof(multicast_destination));
    multicast_destination.sin_family = AF_INET;
    multicast_destination.sin_port = htons(1900); // SSDP port
    inet_pton(AF_INET, "239.255.255.250", &multicast_destination.sin_addr);

    /**
     * Main advertising loop
     * Sends NOTIFY every 30 seconds (UPnP standard)
     */
    while (true)
    {
        // Send NOTIFY advertisement to multicast group
        ssize_t sent = sendto(ssdp_socket, ssdp_notify_message.c_str(),
                              ssdp_notify_message.size(), 0,
                              (sockaddr *)&multicast_destination,
                              sizeof(multicast_destination));

        if (sent > 0)
        {
            std::cout << "📡 SSDP advertisement sent (" << local_ip_address
                      << ":" << TCP_STREAM_PORT << ")" << std::endl;
        }
        else
        {
            std::cerr << "❌ Failed to send SSDP advertisement" << std::endl;
        }

        // Wait 30 seconds before next advertisement
        std::this_thread::sleep_for(std::chrono::seconds(30));
    }

    /**
     * Cleanup (never reached in normal operation)
     */
#ifdef _WIN32
    closesocket(ssdp_socket);
#else
    close(ssdp_socket);
#endif
}

/**
 * ============================================================================
 * MAIN PROGRAM
 * ============================================================================
 * Entry point - starts receiver and handles incoming streams
 */
int main()
{
    std::cout << "📺 AUTO-DISCOVERY SCREEN RECEIVER v1.0" << std::endl;
    std::cout << "TCP Port: " << TCP_STREAM_PORT << std::endl;
    std::cout << "Resolution: " << SCREEN_WIDTH << "x" << SCREEN_HEIGHT << std::endl;
    std::cout << "========================================" << std::endl;

    /**
     * STEP 1: START SSDP ADVERTISING
     * ==============================
     * Background thread makes us discoverable on network
     */
    std::thread ssdp_advertiser_thread(advertiseViaSSDP);
    ssdp_advertiser_thread.detach(); // Run independently

    /**
     * STEP 2: TCP SERVER SETUP
     * ========================
     * Create listening socket for sender connections
     */
#ifdef _WIN32
    WSADATA winsock_data;
    WSAStartup(MAKEWORD(2, 2), &winsock_data);
#endif

    // Create TCP socket
    int tcp_server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_server_socket < 0)
    {
        std::cerr << "❌ TCP server socket creation failed!" << std::endl;
        return 1;
    }

    /**
     * Bind to streaming port on all interfaces
     */
    sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;              // IPv4
    server_address.sin_port = htons(TCP_STREAM_PORT); // Port in network order
    server_address.sin_addr.s_addr = INADDR_ANY;      // All network interfaces

    if (bind(tcp_server_socket, (sockaddr *)&server_address, sizeof(server_address)) < 0)
    {
        std::cerr << "❌ Failed to bind port " << TCP_STREAM_PORT
                  << " (port in use?)" << std::endl;
        return 1;
    }

    /**
     * Start listening for connections
     * backlog = 1 means queue one pending connection
     */
    if (listen(tcp_server_socket, 1) < 0)
    {
        std::cerr << "❌ TCP listen failed!" << std::endl;
        return 1;
    }

    std::cout << "⏳ Waiting for sender connection on port " << TCP_STREAM_PORT << "..." << std::endl;

    /**
     * STEP 3: ACCEPT SENDER CONNECTION
     * ================================
     * accept() blocks until a sender connects
     */
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

    /**
     * STEP 4: SDL2 VIDEO DISPLAY SETUP
     * ================================
     * Initialize SDL2 for hardware-accelerated rendering
     */
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        std::cerr << "❌ SDL2 initialization failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    /**
     * Create viewer window
     * Half-size for easier viewing (640x360)
     */
    SDL_Window *video_window = SDL_CreateWindow(
        "🔴 LIVE SCREEN SHARE (ESC or Close to Quit)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, // Half-size display
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

    if (!video_window)
    {
        std::cerr << "❌ SDL window creation failed!" << std::endl;
        SDL_Quit();
        return 1;
    }

    /**
     * Create hardware-accelerated renderer and texture
     * SDL_TEXTUREACCESS_STREAMING = frequently updated
     */
    SDL_Renderer *sdl_renderer = SDL_CreateRenderer(video_window, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture *video_texture = SDL_CreateTexture(
        sdl_renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING,
        SCREEN_WIDTH, SCREEN_HEIGHT);

    /**
     * Allocate frame buffer for incoming pixels
     * Size = width * height * 3 bytes (RGB24)
     */
    std::vector<uint8_t> frame_pixels(SCREEN_WIDTH * SCREEN_HEIGHT * BYTES_PER_PIXEL);
    int total_frames_displayed = 0;

    /**
     * STEP 5: MAIN RECEIVE AND DISPLAY LOOP
     * =====================================
     * Continuously receive and show frames
     */
    SDL_Event sdl_event;
    bool display_running = true;

    std::cout << "🎬 Live video decode active (ESC to quit)..." << std::endl;

    while (display_running)
    {
        /**
         * Handle window events (close, ESC key)
         */
        while (SDL_PollEvent(&sdl_event))
        {
            if (sdl_event.type == SDL_QUIT ||
                (sdl_event.type == SDL_KEYDOWN && sdl_event.key.keysym.sym == SDLK_ESCAPE))
            {
                display_running = false;
            }
        }

        /**
         * Receive frame size header (first 4 bytes)
         * Network byte order (big-endian)
         */
        uint32_t frame_size_network_order;
        int header_bytes_received = recv(sender_socket, (char *)&frame_size_network_order,
                                         4, 0);
        if (header_bytes_received != 4)
        {
            std::cout << "❌ Sender disconnected" << std::endl;
            break;
        }

        /**
         * Convert from network to host byte order
         */
        uint32_t frame_size_bytes = ntohl(frame_size_network_order);

        /**
         * Receive exact frame data
         * TCP guarantees delivery order, but may fragment
         */
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

        /**
         * Upload frame to GPU texture
         * SDL_UpdateTexture copies pixel data to video memory
         */
        SDL_UpdateTexture(video_texture, NULL, frame_pixels.data(),
                          SCREEN_WIDTH * BYTES_PER_PIXEL); // Row pitch

        /**
         * Render frame to screen
         * Clear → Copy → Present (triple-buffered)
         */
        SDL_RenderClear(sdl_renderer);
        SDL_RenderCopy(sdl_renderer, video_texture, NULL, NULL);
        SDL_RenderPresent(sdl_renderer);

        total_frames_displayed++;

        /**
         * Small delay to prevent 100% CPU usage
         * 5ms = ~200fps ceiling, but sender limits to 10fps
         */
        SDL_Delay(5);
    }

cleanup_and_exit:
    std::cout << "📊 Total frames displayed: " << total_frames_displayed << std::endl;

    /**
     * STEP 6: CLEANUP
     * ==============
     * Free all resources in reverse order
     */
    SDL_DestroyTexture(video_texture);
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(video_window);
    SDL_Quit();

    /**
     * Close network sockets
     */
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