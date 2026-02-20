/**
 * DISCOVER.CPP - SSDP DISCOVERY ENGINE
 */
#include "discover.h"
#include <iostream>
#include <cstring>
#include <chrono>
#include <thread>
#include <ctime>
#include <algorithm>

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
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#endif

static const char *SSDP_MULTICAST_GROUP = "239.255.255.250";
static const int SSDP_MULTICAST_PORT = 1900;

/**
 * Initialize sockets (Windows only)
 */
bool initSockets()
{
#ifdef _WIN32
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0)
    {
        std::cerr << "❌ WSAStartup failed: " << result << std::endl;
        return false;
    }
#endif
    return true;
}

/**
 * Cleanup sockets (Windows only)
 */
void cleanupSockets()
{
#ifdef _WIN32
    WSACleanup();
#endif
}

/**
 * Get the local IP address of this machine
 */
std::string getLocalIPAddress()
{
    std::string localIP = "127.0.0.1";

#ifdef _WIN32
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0)
    {
        struct addrinfo hints, *res;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        if (getaddrinfo(hostname, NULL, &hints, &res) == 0)
        {
            struct sockaddr_in *addr = (struct sockaddr_in *)res->ai_addr;
            localIP = inet_ntoa(addr->sin_addr);
            freeaddrinfo(res);
        }
    }
#else
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == 0)
    {
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
        {
            if (ifa->ifa_addr &&
                ifa->ifa_addr->sa_family == AF_INET &&
                (ifa->ifa_flags & IFF_UP) &&
                !(ifa->ifa_flags & IFF_LOOPBACK))
            {
                struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
                char *ip = inet_ntoa(addr->sin_addr);

                if (strncmp(ip, "192.168.", 8) != 0)
                {
                    localIP = ip;
                    break;
                }
                else if (localIP == "127.0.0.1")
                {
                    localIP = ip;
                }
            }
        }
        freeifaddrs(ifaddr);
    }
#endif
    return localIP;
}

/**
 * Test if a TCP connection can be established to a receiver
 */
bool testTcpConnection(const std::string &ip, int port, int timeout_ms)
{
#ifdef _WIN32
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET)
        return false;
#else
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return false;
#endif

#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);
#else
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    int result = connect(sock, (struct sockaddr *)&addr, sizeof(addr));

    bool connected = false;

#ifdef _WIN32
    if (result == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK)
#else
    if (result < 0 && errno == EINPROGRESS)
#endif
    {
        fd_set fdset;
        FD_ZERO(&fdset);
        FD_SET(sock, &fdset);

        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        if (select(sock + 1, NULL, &fdset, NULL, &tv) == 1)
        {
            int so_error;
            socklen_t len = sizeof(so_error);
            getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&so_error, &len);
            if (so_error == 0)
                connected = true;
        }
    }

#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif

    return connected;
}

/**
 * Parse SSDP response to extract IP and port
 */
bool parseSsdpResponse(const std::string &response, std::string &ip, int &port)
{
    size_t loc_pos = response.find("LOCATION: ");
    if (loc_pos == std::string::npos)
        loc_pos = response.find("Location: ");

    if (loc_pos == std::string::npos)
        return false;

    size_t line_end = response.find("\r\n", loc_pos);
    if (line_end == std::string::npos)
        line_end = response.find("\n", loc_pos);

    if (line_end == std::string::npos)
        return false;

    std::string url = response.substr(loc_pos + 10, line_end - loc_pos - 10);

    url.erase(0, url.find_first_not_of(" \t\r\n"));
    url.erase(url.find_last_not_of(" \t\r\n") + 1);

    size_t protocol_pos = url.find("://");
    if (protocol_pos == std::string::npos)
        return false;

    size_t host_start = protocol_pos + 3;
    size_t host_end = url.find_first_of(":/", host_start);

    if (host_end == std::string::npos)
    {
        ip = url.substr(host_start);
        port = 8081;
    }
    else
    {
        ip = url.substr(host_start, host_end - host_start);

        if (url[host_end] == ':')
        {
            size_t port_end = url.find("/", host_end);
            std::string port_str = url.substr(host_end + 1,
                                              (port_end == std::string::npos) ? std::string::npos : port_end - host_end - 1);
            try
            {
                port = std::stoi(port_str);
            }
            catch (...)
            {
                port = 8081;
            }
        }
        else
        {
            port = 8081;
        }
    }

    return true;
}

/**
 * Discover receivers on the network
 */
std::vector<DiscoveredDevice> discoverReceivers(int timeout_seconds)
{
    std::vector<DiscoveredDevice> discovered_receivers;

    std::cout << "🔍 Scanning for receivers on 239.255.255.250:1900..." << std::endl;

    if (!initSockets())
        return discovered_receivers;

#ifdef _WIN32
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET)
    {
        std::cerr << "❌ Failed to create socket" << std::endl;
        cleanupSockets();
        return discovered_receivers;
    }
#else
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        std::cerr << "❌ Failed to create socket" << std::endl;
        cleanupSockets();
        return discovered_receivers;
    }
#endif

    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));

    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(SSDP_MULTICAST_GROUP);
    mreq.imr_interface.s_addr = INADDR_ANY;

    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq, sizeof(mreq)) < 0)
    {
        std::cerr << "❌ Failed to join multicast group" << std::endl;
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        cleanupSockets();
        return discovered_receivers;
    }

    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind_addr.sin_port = 0;

    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0)
    {
        std::cerr << "❌ Failed to bind socket" << std::endl;
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        cleanupSockets();
        return discovered_receivers;
    }

    struct timeval tv;
    tv.tv_sec = timeout_seconds;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(tv));

    std::string msearch =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 3\r\n"
        "ST: urn:screen-share:receiver\r\n"
        "USER-AGENT: ScreenShare/1.0\r\n"
        "\r\n";

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(SSDP_MULTICAST_PORT);
    inet_pton(AF_INET, SSDP_MULTICAST_GROUP, &dest_addr.sin_addr);

    std::cout << "📡 Sending SSDP M-SEARCH requests..." << std::endl;
    for (int i = 0; i < 3; i++)
    {
        sendto(sock, msearch.c_str(), msearch.length(), 0,
               (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "📡 Waiting for responses (" << timeout_seconds << " seconds)..." << std::endl;

    char buffer[8192];
    struct sockaddr_in sender_addr;
    socklen_t sender_len = sizeof(sender_addr);

    int response_count = 0;
    while (response_count < 30)
    {
        int bytes = recvfrom(sock, buffer, sizeof(buffer) - 1, 0,
                             (struct sockaddr *)&sender_addr, &sender_len);

        if (bytes < 0)
        {
            break;
        }

        buffer[bytes] = '\0';
        std::string response(buffer);

        if (response.find("urn:screen-share:receiver") != std::string::npos ||
            response.find("screen-share") != std::string::npos)
        {
            std::string ip;
            int port = 8081;

            if (parseSsdpResponse(response, ip, port))
            {
                std::cout << "✅ Found potential receiver: " << ip << ":" << port << std::endl;

                std::cout << "   Testing connection..." << std::endl;
                if (testTcpConnection(ip, port, 500))
                {
                    discovered_receivers.emplace_back(ip, port);
                    std::cout << "   ✅ Connection successful!" << std::endl;
                }
                else
                {
                    std::cout << "   ❌ Connection failed (port not open)" << std::endl;
                }
            }
            response_count++;
        }
    }

#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif

    cleanupSockets();

    discovered_receivers.erase(
        std::unique(discovered_receivers.begin(), discovered_receivers.end(),
                    [](const DiscoveredDevice &a, const DiscoveredDevice &b)
                    {
                        return a.ip_address == b.ip_address && a.tcp_port == b.tcp_port;
                    }),
        discovered_receivers.end());

    std::cout << "📋 Discovery complete: " << discovered_receivers.size() << " receiver(s) available" << std::endl;
    return discovered_receivers;
}

/**
 * Check if any receivers are available
 */
bool hasReceivers()
{
    return !discoverReceivers(2).empty();
}

/**
 * Format list of devices for display
 */
std::string listDevices(const std::vector<DiscoveredDevice> &devices)
{
    std::string list = "\n📱 RECEIVERS FOUND:\n";
    if (devices.empty())
    {
        list += "  None found\n";
    }
    else
    {
        for (size_t i = 0; i < devices.size(); i++)
        {
            list += "  [" + std::to_string(i) + "] " + devices[i].toString();

            if (devices[i].ip_address == "127.0.0.1" ||
                devices[i].ip_address == getLocalIPAddress())
            {
                list += " (THIS MACHINE)";
            }
            list += "\n";
        }
    }
    return list;
}