#include <iostream>   // Console output
#include <cstring>    // Memory operations
#include <SDL2/SDL.h> // Cross-platform window + rendering

#ifdef _WIN32
#include <winsock2.h> // Windows sockets
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "SDL2.lib")
#else
#include <sys/socket.h> // Unix sockets
#include <netinet/in.h> // sockaddr_in
#include <arpa/inet.h>  // inet_ntoa
#include <unistd.h>     // close()
#endif

// === CONFIGURATION ===
#define SCREEN_PORT 8081
#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720
#define BYTES_PER_PIXEL 3

int main()
{
    std::cout << "📺 CROSS-PLATFORM SCREEN SHARE RECEIVER" << std::endl;
    std::cout << "Waiting for sender on port " << SCREEN_PORT << "..." << std::endl;

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    // === TCP SERVER SETUP ===
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SCREEN_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY; // Listen on all interfaces

    if (bind(server_fd, (sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        std::cerr << "❌ Port " << SCREEN_PORT << " already in use!" << std::endl;
        return 1;
    }

    listen(server_fd, 1); // Single sender

    // === ACCEPT SENDER CONNECTION ===
    sockaddr_in sender_addr;
    socklen_t sender_len = sizeof(sender_addr);
    int sender_fd = accept(server_fd, (sockaddr *)&sender_addr, &sender_len);

    char sender_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &sender_addr.sin_addr, sender_ip, INET_ADDRSTRLEN);
    std::cout << "✅ Sender connected from " << sender_ip << std::endl;

    // === SDL2 DISPLAY SETUP ===
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        std::cerr << "❌ SDL initialization failed!" << std::endl;
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(
        "🔴 LIVE SCREEN SHARE (Press ESC to quit)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, // Half-size viewer
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture *texture = SDL_CreateTexture(renderer,
                                             SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING,
                                             SCREEN_WIDTH, SCREEN_HEIGHT);

    std::vector<uint8_t> frame_buffer(SCREEN_WIDTH * SCREEN_HEIGHT * BYTES_PER_PIXEL);
    int frame_count = 0;

    // === VIDEO DECODE & DISPLAY LOOP ===
    SDL_Event event;
    bool running = true;

    while (running)
    {
        // Handle quit events
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT ||
                (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE))
            {
                running = false;
            }
        }

        // Receive frame size (4-byte header)
        uint32_t frame_size_net;
        if (recv(sender_fd, (char *)&frame_size_net, 4, 0) != 4)
        {
            std::cout << "❌ Sender disconnected" << std::endl;
            break;
        }
        uint32_t frame_size = ntohl(frame_size_net);

        // Receive exact frame data
        int total_received = 0;
        while (total_received < frame_size)
        {
            int bytes = recv(sender_fd, frame_buffer.data() + total_received,
                             frame_size - total_received, 0);
            if (bytes <= 0)
            {
                std::cout << "❌ Receive error" << std::endl;
                goto cleanup;
            }
            total_received += bytes;
        }

        // Update SDL texture and render
        SDL_UpdateTexture(texture, NULL, frame_buffer.data(), SCREEN_WIDTH * 3);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);

        frame_count++;
        SDL_Delay(5); // Prevent 100% CPU usage
    }

cleanup:
    std::cout << "📊 Displayed " << frame_count << " frames" << std::endl;

    // Cleanup resources
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

#ifdef _WIN32
    closesocket(sender_fd);
    closesocket(server_fd);
    WSACleanup();
#else
    close(sender_fd);
    close(server_fd);
#endif

    std::cout << "🔌 Receiver shutdown complete" << std::endl;
    return 0;
}
