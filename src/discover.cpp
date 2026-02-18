/**
 * DISCOVER.CPP - SSDP DISCOVERY ENGINE (COMPILER-CLEAN)
 */
#include "discover.h"
#include <iostream>
#include <cstring>
#include <chrono>
#include <thread>
#include <ctime>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/time.h>
#include <net/if.h>
#include <ifaddrs.h>
#endif

static const char *SSDP_MULTICAST_GROUP = "239.255.255.250";
static const int SSDP_MULTICAST_PORT = 1900;

std::string getLocalIPAddress()
{
    std::string localIP = "0.0.0.0";

#ifdef _WIN32
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0)
    {
        struct hostent *host = gethostbyname(hostname);
        if (host)
        {
            struct in_addr addr;
            memcpy(&addr, host->h_addr_list[0], sizeof(addr));
            localIP = inet_ntoa(addr);
        }
    }
#else
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == 0)
    {
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
        {
            if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET &&
                strcmp(ifa->ifa_name, "lo") != 0)
            {
                struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
                localIP = inet_ntoa(addr->sin_addr);
                break;
            }
        }
        freeifaddrs(ifaddr);
    }
#endif
    return localIP;
}

// [Rest of discover.cpp unchanged - it's perfect]
std::vector<DiscoveredDevice> discoverReceivers()
{
    std::vector<DiscoveredDevice> discovered_receivers;

    std::cout << "🔍 Scanning 239.255.255.250:1900 for receivers..." << std::endl;

#ifdef _WIN32
    WSADATA winsock_data;
    if (WSAStartup(MAKEWORD(2, 2), &winsock_data) != 0)
    {
        std::cerr << "❌ Winsock init failed" << std::endl;
        return discovered_receivers;
    }
#endif

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return discovered_receivers;

    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr("239.255.255.250");
    mreq.imr_interface.s_addr = INADDR_ANY;
    setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq, sizeof(mreq));

    int broadcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (char *)&broadcast, sizeof(broadcast));

    sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind_addr.sin_port = 0;
    bind(sock, (sockaddr *)&bind_addr, sizeof(bind_addr));

    std::string msearch = "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\n"
                          "MAN: \"ssdp:discover\"\r\nMX: 3\r\nST: urn:screen-share:receiver\r\n\r\n";

    sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(1900);
    inet_pton(AF_INET, "239.255.255.250", &dest.sin_addr);

    for (int i = 0; i < 3; i++)
    {
        sendto(sock, msearch.c_str(), msearch.length(), 0, (sockaddr *)&dest, sizeof(dest));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    timeval timeout = {3, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));

    char buffer[4096];
    sockaddr_in sender;
    socklen_t len = sizeof(sender);

    for (int i = 0; i < 30; i++)
    { // Try 30 times
        int bytes = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (sockaddr *)&sender, &len);
        if (bytes > 0)
        {
            buffer[bytes] = 0;
            std::string resp(buffer);
            if (resp.find("urn:screen-share:receiver") != std::string::npos)
            {
                size_t loc = resp.find("LOCATION: ");
                if (loc != std::string::npos)
                {
                    size_t end = resp.find("\r\n", loc);
                    std::string url = resp.substr(loc + 10, end - loc - 10);
                    size_t proto = url.find("://");
                    if (proto != std::string::npos)
                    {
                        size_t host = proto + 3;
                        size_t colon = url.find(":", host);
                        size_t path = url.find("/", host);
                        std::string ip;
                        int port = 8081;

                        if (colon != std::string::npos && (path == std::string::npos || colon < path))
                        {
                            ip = url.substr(host, colon - host);
                            size_t port_end = path != std::string::npos ? path : url.length();
                            port = std::stoi(url.substr(colon + 1, port_end - colon - 1));
                        }
                        else
                        {
                            ip = url.substr(host, path != std::string::npos ? path - host : std::string::npos);
                        }

                        discovered_receivers.emplace_back(ip, port);
                        std::cout << "✅ Found receiver: " << ip << ":" << port << std::endl;
                    }
                }
            }
        }
    }

#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif

    std::cout << "📋 Discovery complete: " << discovered_receivers.size() << " found" << std::endl;
    return discovered_receivers;
}

bool hasReceivers() { return !discoverReceivers().empty(); }

std::string listDevices(const std::vector<DiscoveredDevice> &devices)
{
    std::string list = "\n📱 RECEIVERS FOUND:\n";
    for (size_t i = 0; i < devices.size(); i++)
    {
        list += "  [" + std::to_string(i) + "] " + devices[i].toString() + "\n";
    }
    return devices.empty() ? list + "  None\n" : list;
}
