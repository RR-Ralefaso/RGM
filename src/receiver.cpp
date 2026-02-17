#include <iostream>   // Console output for connection status
#include <cstring>    // memset() for socket address initialization
#include <vector>     // Dynamic frame buffer storage (RGB24)
#include <SDL2/SDL.h> // Cross-platform windowing and rendering

// === PLATFORM-SPECIFIC SOCKET HEADERS ===
#ifdef _WIN32
#include <winsock2.h>              // Windows socket API
#pragma comment(lib, "ws2_32.lib") // Auto-link socket library
#pragma comment(lib, "SDL2.lib")   // Auto-link SDL2 library
#else
#include <sys/socket.h> // socket(), bind(), listen(), accept()
#include <netinet/in.h> // sockaddr_in structure
#include <arpa/inet.h>  // inet_ntop() for IP display
#include <unistd.h>     // close() function
#endif

// === CONFIGURATION ===
#define SCREEN_PORT 8081  // TCP port to listen on
#define SCREEN_WIDTH 1280 // Expected sender resolution
#define SCREEN_HEIGHT 720
#define BYTES_PER_PIXEL 3 // RGB24 format

/**
 * Main receiver program
 * Creates SDL window + listens for sender connection
 */
int main()
{
    std::cout << "📺 CROSS-PLATFORM SCREEN SHARE RECEIVER v1.0" << std::endl;
    std::cout << "Listening on port " << SCREEN_PORT << std::endl
              << std::endl;

    // Windows: Initialize networking
#ifdef _WIN32
    WSADATA winsock_data;
    if (WSAStartup(MAKEWORD(2, 2), &winsock_data) != 0)
    {
        std::cerr << "❌ Windows networking failed!" << std::endl;
        return 1;
    }
#endif

    // === TCP SERVER SETUP ===
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0)
    {
        std::cerr << "❌ Server socket creation failed!" << std::endl;
        return 1;
    }

    // Bind to all interfaces on specified port
    sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;          // IPv4
    server_address.sin_port = htons(SCREEN_PORT); // Network byte order
    server_address.sin_addr.s_addr = INADDR_ANY;  // 0.0.0.0 = all network interfaces

    if (bind(server_socket, (sockaddr *)&server_address, sizeof(server_address)) < 0)
    {
        std::cerr << "❌ Port " << SCREEN_PORT << " already in use!" << std::endl;
        return 1;
    }

    // Listen for incoming sender connections (max queue = 1)
    if (listen(server_socket, 1) < 0)
    {
        std::cerr << "❌ Listen failed!" << std::endl;
        return 1;
    }

    std::cout << "⏳ Waiting for sender connection..." << std::endl;

    // === ACCEPT SENDER ===
    sockaddr_in sender_address;
    socklen_t sender_addr_len = sizeof(sender_address);
    int sender_socket = accept(server_socket, (sockaddr *)&sender_address, &sender_addr_len);

    if (sender_socket < 0)
    {
        std::cerr << "❌ Accept failed!" << std::endl;
        return 1;
    }

    // Display sender IP address
    char sender_ip[INET6_ADDRSTRLEN];
#ifdef _WIN32
    inet_ntop(AF_INET, &sender_address.sin_addr, sender_ip, INET6_ADDRSTRLEN);
#else
    inet_ntop(AF_INET, &sender_address.sin_addr, sender_ip, INET6_ADDRSTRLEN);
#endif
    std::cout << "✅ Sender connected: " << sender_ip << std::endl;

    // === SDL2 VIDEO DISPLAY SETUP ===
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        std::cerr << "❌ SDL2 initialization failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    // Create resizable viewer window (half-size for easy viewing)
    SDL_Window *video_window = SDL_CreateWindow(
        "🔴 LIVE SCREEN SHARE (ESC or Close to quit)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

    if (!video_window)
    {
        std::cerr << "❌ SDL window creation failed: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    // Hardware-accelerated renderer + RGB24 texture
    SDL_Renderer *renderer = SDL_CreateRenderer(video_window, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture *video_texture = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING,
        SCREEN_WIDTH, SCREEN_HEIGHT);

    // Frame buffer (RGB24 pixel storage)
    std::vector<uint8_t> frame_buffer(SCREEN_WIDTH * SCREEN_HEIGHT * BYTES_PER_PIXEL);
    int frame_counter = 0;

    // === MAIN VIDEO DECODE & DISPLAY LOOP ===
    SDL_Event sdl_event;
    bool is_running = true;

    std::cout << "🎬 Live video stream active! (Press ESC to quit)" << std::endl;

    while (is_running)
    {
        // Handle SDL events (window close, ESC key)
        while (SDL_PollEvent(&sdl_event))
        {
            if (sdl_event.type == SDL_QUIT ||
                (sdl_event.type == SDL_KEYDOWN && sdl_event.key.keysym.sym == SDLK_ESCAPE))
            {
                is_running = false;
            }
        }

        // Receive frame size header (first 4 bytes)
        uint32_t frame_size_network;
        int header_bytes = recv(sender_socket, (char *)&frame_size_network, 4, 0);
        if (header_bytes != 4)
        {
            std::cout << "❌ Sender disconnected" << std::endl;
            break;
        }

        uint32_t frame_size = ntohl(frame_size_network); // Network → host byte order

        // Receive exact frame data (TCP guarantees delivery)
        int total_received = 0;
        while (total_received < frame_size)
        {
            int bytes_received = recv(sender_socket,
                                      frame_buffer.data() + total_received,
                                      frame_size - total_received, 0);
            if (bytes_received <= 0)
            {
                std::cout << "❌ Receive error - sender dropped" << std::endl;
                goto cleanup;
            }
            total_received += bytes_received;
        }

        // Update SDL texture with new frame (GPU upload)
        SDL_UpdateTexture(video_texture, NULL, frame_buffer.data(),
                          SCREEN_WIDTH * BYTES_PER_PIXEL);

        // Render frame to screen
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, video_texture, NULL, NULL);
        SDL_RenderPresent(renderer);

        frame_counter++;
        SDL_Delay(5); // Prevent 100% CPU usage
    }

cleanup:
    std::cout << "📊 Displayed " << frame_counter << " frames" << std::endl;

    // === CLEANUP RESOURCES ===
    SDL_DestroyTexture(video_texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(video_window);
    SDL_Quit();

    // Close sockets
#ifdef _WIN32
    closesocket(sender_socket);
    closesocket(server_socket);
    WSACleanup();
#else
    close(sender_socket);
    close(server_socket);
#endif

    std::cout << "🔌 Receiver shutdown complete" << std::endl;
    return 0;
}
