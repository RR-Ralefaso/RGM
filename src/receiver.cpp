#include <iostream>
#include <cstring>
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <SDL.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "SDL2.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <SDL2/SDL.h>
#endif

#define PORT 8081
#define WIDTH 1280
#define HEIGHT 720

// Initialize Windows networking
void initSockets()
{
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
}

int main()
{
    initSockets();

    // === SETUP SERVER ===
    std::cout << "📺 Starting screen receiver on port " << PORT << "..." << std::endl;

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    server.sin_addr.s_addr = INADDR_ANY; // Listen on all interfaces

    bind(server_fd, (struct sockaddr *)&server, sizeof(server));
    listen(server_fd, 1); // Single client

    // === ACCEPT SENDER ===
    sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);

    std::cout << "✅ Screen sender connected!" << std::endl;
    std::cout << "🎬 Opening display window..." << std::endl;

    // === SDL DISPLAY SETUP ===
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window *window = SDL_CreateWindow("🔴 LIVE SCREEN SHARE",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          WIDTH / 2, HEIGHT / 2, // Half size for viewing
                                          SDL_WINDOW_SHOWN);

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture *texture = SDL_CreateTexture(renderer,
                                             SDL_PIXELFORMAT_RGB24,
                                             SDL_TEXTUREACCESS_STREAMING,
                                             WIDTH, HEIGHT);

    // Pixel buffer for incoming frames
    unsigned char *pixels = new unsigned char[WIDTH * HEIGHT * 3];
    int frameCount = 0;

    // === FRAME RECEIVE & DISPLAY LOOP ===
    while (true)
    {
        // 1. READ FRAME SIZE (4 bytes)
        uint32_t frameSize_net;
        if (recv(client_fd, (char *)&frameSize_net, 4, 0) != 4)
        {
            break;
        }
        int frameSize = ntohl(frameSize_net);

        // 2. READ EXACT FRAME DATA
        int totalReceived = 0;
        while (totalReceived < frameSize)
        {
            int bytes = recv(client_fd, pixels + totalReceived,
                             frameSize - totalReceived, 0);
            if (bytes <= 0)
                goto cleanup;
            totalReceived += bytes;
        }

        // 3. DISPLAY FRAME
        SDL_UpdateTexture(texture, NULL, pixels, WIDTH * 3);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);

        frameCount++;
        SDL_Delay(10); // Prevent over-rendering
    }

cleanup:
    std::cout << "📊 Received " << frameCount << " frames" << std::endl;
    std::cout << "🔌 Connection closed" << std::endl;

    // Cleanup
    delete[] pixels;
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

#ifdef _WIN32
    closesocket(client_fd);
    closesocket(server_fd);
#else
    close(client_fd);
    close(server_fd);
#endif
    return 0;
}
