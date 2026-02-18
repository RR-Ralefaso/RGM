/**
 * RECEIVER.CPP - FIXED VERSION (Compiles with -Wall -Wextra)
 */
#include <iostream>
#include <cstring>
#include <vector>
#include <thread>
#include <chrono>
#include <cstdint>
#include <SDL2/SDL.h>
#include "discover.h"

#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#define TCP_STREAM_PORT 8081
#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720
#define BYTES_PER_PIXEL 3

void advertiseViaSSDP()
{
    std::cout << "📡 Starting SSDP advertiser (239.255.255.250:1900)..." << std::endl;

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    // ✅ FIXED: Response socket with multicast join
    int response_sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr("239.255.255.250");
    mreq.imr_interface.s_addr = INADDR_ANY;
    setsockopt(response_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq, sizeof(mreq));

    // ✅ FIXED: Proper memset initialization
    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind_addr.sin_port = htons(1900);
    bind(response_sock, (sockaddr *)&bind_addr, sizeof(bind_addr));

    // Response thread with proper capture
    std::thread response_thread([&]()
                                {
        char buffer[2048];
        struct sockaddr_in sender;
        socklen_t sender_len = sizeof(sender);
        
        while (true) {
            int bytes = recvfrom(response_sock, buffer, sizeof(buffer)-1, 0,
                               (sockaddr*)&sender, &sender_len);
            if (bytes > 0) {
                buffer[bytes] = '\0';
                std::string req(buffer);
                if (req.find("M-SEARCH") != std::string::npos && 
                    req.find("urn:screen-share:receiver") != std::string::npos) {
                    
                    std::string local_ip = getLocalIPAddress();
                    std::string response = 
                        "HTTP/1.1 200 OK\r\n"
                        "CACHE-CONTROL: max-age=30\r\n"
                        "LOCATION: http://" + local_ip + ":8081/\r\n"
                        "ST: urn:screen-share:receiver\r\n"
                        "SERVER: ScreenShare/1.0\r\n\r\n";
                    
                    sendto(response_sock, response.c_str(), response.length(), 0,
                           (sockaddr*)&sender, sender_len);
                    std::cout << "📡 SSDP M-SEARCH response sent" << std::endl;
                }
            }
        } });
    response_thread.detach();

    // NOTIFY advertisements
    std::string local_ip = getLocalIPAddress();
    std::string notify =
        "NOTIFY * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\n"
        "CACHE-CONTROL: max-age=30\r\nLOCATION: http://" +
        local_ip + ":8081/\r\n"
                   "NT: urn:screen-share:receiver\r\nNTS: ssdp:alive\r\n\r\n";

    int notify_sock = socket(AF_INET, SOCK_DGRAM, 0);
    int broadcast = 1;
    setsockopt(notify_sock, SOL_SOCKET, SO_BROADCAST, (char *)&broadcast, sizeof(broadcast));

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(1900);
    inet_pton(AF_INET, "239.255.255.250", &dest.sin_addr);

    while (true)
    {
        sendto(notify_sock, notify.c_str(), notify.length(), 0, (sockaddr *)&dest, sizeof(dest));
        std::cout << "📡 SSDP NOTIFY sent (" << local_ip << ":8081)" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(30));
    }
}

int main()
{
    std::cout << "📺 SCREEN RECEIVER v1.0 - Port 8081" << std::endl;

    std::thread ssdp_thread(advertiseViaSSDP);
    ssdp_thread.detach();

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    int server_sock = socket(AF_INET, SOCK_STREAM, 0);

    // ✅ FIXED: Proper sockaddr_in initialization
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(TCP_STREAM_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_sock, (sockaddr *)&server_addr, sizeof(server_addr));
    listen(server_sock, 1);

    std::cout << "⏳ Waiting for sender connection..." << std::endl;

    struct sockaddr_in sender_addr;
    socklen_t addr_len = sizeof(sender_addr);
    int client_sock = accept(server_sock, (sockaddr *)&sender_addr, &addr_len);

    std::cout << "✅ Sender connected! Starting video..." << std::endl;

    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window *window = SDL_CreateWindow("LIVE SCREEN SHARE",
                                          SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, SDL_WINDOW_SHOWN);

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24,
                                             SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, SCREEN_HEIGHT);

    std::vector<uint8_t> frame(SCREEN_WIDTH * SCREEN_HEIGHT * BYTES_PER_PIXEL);
    SDL_Event event;
    bool running = true;
    int frames = 0;

    while (running)
    {
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT ||
                (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE))
                running = false;
        }

        uint32_t frame_size;
        if (recv(client_sock, (char *)&frame_size, 4, 0) != 4)
            break;
        frame_size = ntohl(frame_size);

        int total = 0;
        while (total < (int)frame_size)
        {
            int bytes = recv(client_sock, (char *)frame.data() + total, frame_size - total, 0);
            if (bytes <= 0)
                goto cleanup;
            total += bytes;
        }

        SDL_UpdateTexture(texture, NULL, frame.data(), SCREEN_WIDTH * BYTES_PER_PIXEL);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
        frames++;
        SDL_Delay(5);
    }

cleanup:
    std::cout << "📊 Frames displayed: " << frames << std::endl;
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

#ifdef _WIN32
    closesocket(client_sock);
    closesocket(server_sock);
    WSACleanup();
#else
    close(client_sock);
    close(server_sock);
#endif
    return 0;
}
