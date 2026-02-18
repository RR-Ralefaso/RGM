/**
 * ============================================================================
 * DISCOVER.CPP - SSDP AUTO-DISCOVERY IMPLEMENTATION
 * ============================================================================
 * This file implements the UPnP/SSDP discovery protocol to find screen
 * receivers on the local network. It handles:
 * - Creating and configuring multicast UDP sockets
 * - Sending SSDP M-SEARCH discovery requests
 * - Receiving and parsing HTTP-style responses
 * - Extracting IP addresses and ports from LOCATION headers
 *
 * SSDP Protocol Details:
 * - Multicast Group: 239.255.255.250
 * - Port: 1900
 * - Service Type: urn:screen-share:receiver (custom)
 * - Response Timeout: 3 seconds
 */

#include "discover.h" // Our discovery API definitions
#include <iostream>   // Console output for discovery progress
#include <cstring>    // memset() for socket structure initialization
#include <chrono>     // Timing for response window
#include <thread>     // For small delays between multicast sends

/**
 * ============================================================================
 * PLATFORM-SPECIFIC NETWORKING HEADERS
 * ============================================================================
 * Windows uses Winsock2, Linux/POSIX uses standard sockets
 */
#ifdef _WIN32
#include <winsock2.h>              // Windows socket API
#include <ws2tcpip.h>              // Windows IP helper functions
#pragma comment(lib, "ws2_32.lib") // Auto-link Winsock library
#else
#include <sys/socket.h> // POSIX socket functions
#include <netinet/in.h> // sockaddr_in structure
#include <arpa/inet.h>  // inet_pton() for IP conversion
#include <unistd.h>     // close() for socket cleanup
#include <sys/time.h>   // timeval for receive timeout
#include <net/if.h>     // Network interface utilities
#include <ifaddrs.h>    // Interface address enumeration
#endif

// ============================================================================
// SSDP PROTOCOL CONSTANTS
// ============================================================================
static const char *SSDP_MULTICAST_GROUP = "239.255.255.250";                // Standard SSDP multicast address
static const int SSDP_MULTICAST_PORT = 1900;                                // Standard SSDP port
static const int DISCOVERY_TIMEOUT_SECONDS = 3;                             // How long to wait for responses
static const char *SCREEN_SHARE_SERVICE_TYPE = "urn:screen-share:receiver"; // Our service ID

/**
 * ============================================================================
 * GET LOCAL IP ADDRESS HELPER
 * ============================================================================
 * Determines the primary local IP address of this machine.
 * Used by receiver for building LOCATION URLs in advertisements.
 *
 * @return String containing local IP (e.g., "192.168.1.100")
 *         Returns "0.0.0.0" if detection fails
 *
 * Implementation:
 * - Windows: Uses gethostname() + gethostbyname()
 * - Linux: Uses getifaddrs() to iterate network interfaces
 * - Skips loopback interface (127.0.0.1)
 */
std::string getLocalIPAddress()
{
    std::string localIP = "0.0.0.0"; // Default fallback

#ifdef _WIN32
    // Windows implementation
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
    // Linux/Unix implementation using interface enumeration
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == 0)
    {
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
        {
            if (ifa->ifa_addr == NULL)
                continue;
            if (ifa->ifa_addr->sa_family == AF_INET)
            { // IPv4 only
                // Skip loopback interface (127.0.0.1)
                if (strcmp(ifa->ifa_name, "lo") != 0)
                {
                    struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
                    localIP = inet_ntoa(addr->sin_addr);
                    break; // Use first non-loopback interface
                }
            }
        }
        freeifaddrs(ifaddr); // Clean up
    }
#endif

    return localIP;
}

/**
 * ============================================================================
 * PRIMARY DISCOVERY FUNCTION
 * ============================================================================
 * Performs SSDP network scan to find all screen receivers.
 *
 * Algorithm:
 * 1. Create UDP socket for multicast communication
 * 2. Configure socket options (broadcast, TTL, timeout)
 * 3. Send M-SEARCH request 3 times for reliability
 * 4. Listen for responses during 3-second window
 * 5. Parse each response to extract device info
 * 6. Deduplicate devices by IP address
 *
 * @return Vector of unique discovered devices
 */
std::vector<DiscoveredDevice> discoverReceivers()
{
    std::vector<DiscoveredDevice> discovered_receivers; // Results container

    std::cout << "🔍 [SSDP DISCOVERY] Scanning " << SSDP_MULTICAST_GROUP
              << ":" << SSDP_MULTICAST_PORT << " for screen receivers..." << std::endl;

    /**
     * Windows: Initialize Winsock networking stack
     * Required before any socket operations on Windows
     */
#ifdef _WIN32
    WSADATA winsock_data;
    if (WSAStartup(MAKEWORD(2, 2), &winsock_data) != 0)
    {
        std::cerr << "❌ [SSDP] Windows networking initialization failed" << std::endl;
        return discovered_receivers; // Return empty list on failure
    }
#endif

    /**
     * Create UDP socket for discovery
     * SOCK_DGRAM = UDP (datagram) protocol
     */
    int multicast_socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (multicast_socket_fd < 0)
    {
        std::cerr << "❌ [SSDP] Failed to create socket" << std::endl;
        return discovered_receivers;
    }

    /**
     * Configure socket for broadcast capability
     * Allows sending to multicast groups
     */
    int broadcast = 1;
    setsockopt(multicast_socket_fd, SOL_SOCKET, SO_BROADCAST,
               (char *)&broadcast, sizeof(broadcast));

    /**
     * Set Time-To-Live for multicast packets
     * TTL=2 ensures packets stay within local network
     */
    int ttl = 2;
    setsockopt(multicast_socket_fd, IPPROTO_IP, IP_MULTICAST_TTL,
               (char *)&ttl, sizeof(ttl));

    /**
     * Bind socket to any available port
     * Port 0 lets OS assign a random port
     */
    sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr)); // Zero out structure
    bind_addr.sin_family = AF_INET;           // IPv4
    bind_addr.sin_addr.s_addr = INADDR_ANY;   // Listen on all interfaces
    bind_addr.sin_port = 0;                   // OS-assigned port

    if (bind(multicast_socket_fd, (sockaddr *)&bind_addr, sizeof(bind_addr)) < 0)
    {
        std::cerr << "❌ [SSDP] Failed to bind socket" << std::endl;
#ifdef _WIN32
        closesocket(multicast_socket_fd);
#else
        close(multicast_socket_fd);
#endif
        return discovered_receivers;
    }

    /**
     * Prepare SSDP M-SEARCH discovery message
     * Standard UPnP format with our custom service type
     */
    std::string ssdp_msearch_request =
        "M-SEARCH * HTTP/1.1\r\n"           // Method and protocol
        "HOST: 239.255.255.250:1900\r\n"    // Multicast destination
        "MAN: \"ssdp:discover\"\r\n"        // Required discovery header
        "MX: 3\r\n"                         // Maximum wait time (seconds)
        "ST: urn:screen-share:receiver\r\n" // Service type we're looking for
        "USER-AGENT: ScreenShare/1.0\r\n"   // Client identifier
        "\r\n";                             // Empty line ends HTTP headers

    /**
     * Set up multicast destination address
     * All SSDP listeners will receive this
     */
    sockaddr_in multicast_destination;
    memset(&multicast_destination, 0, sizeof(multicast_destination));
    multicast_destination.sin_family = AF_INET;                                // IPv4
    multicast_destination.sin_port = htons(SSDP_MULTICAST_PORT);               // 1900 in network byte order
    inet_pton(AF_INET, SSDP_MULTICAST_GROUP, &multicast_destination.sin_addr); // Convert IP string to binary

    /**
     * Send M-SEARCH request multiple times for reliability
     * UDP packets can be lost, so we retry
     */
    for (int i = 0; i < 3; i++)
    {
        ssize_t bytes_sent = sendto(multicast_socket_fd,
                                    ssdp_msearch_request.c_str(),
                                    ssdp_msearch_request.length(), 0,
                                    (sockaddr *)&multicast_destination,
                                    sizeof(multicast_destination));

        if (bytes_sent < 0)
        {
            std::cerr << "❌ [SSDP] Failed to send M-SEARCH request (attempt " << (i + 1) << ")" << std::endl;
        }
        else
        {
            std::cout << "📡 [SSDP] M-SEARCH sent (" << bytes_sent << " bytes) - attempt " << (i + 1) << std::endl;
        }

        // Small delay between sends to avoid network flooding
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    /**
     * Set receive timeout for responses
     * Prevents socket from blocking forever
     */
    timeval receive_timeout;
    receive_timeout.tv_sec = DISCOVERY_TIMEOUT_SECONDS; // 3 seconds
    receive_timeout.tv_usec = 0;                        // 0 microseconds
    setsockopt(multicast_socket_fd, SOL_SOCKET, SO_RCVTIMEO,
               (char *)&receive_timeout, sizeof(receive_timeout));

    // Buffer for receiving HTTP responses
    char http_response_buffer[4096];
    sockaddr_in sender_address; // Address of responding device
    socklen_t sender_address_length = sizeof(sender_address);

    time_t start_time = time(nullptr); // Record discovery start time

    std::cout << "📡 [SSDP] Listening for responses..." << std::endl;

    /**
     * Main response collection loop
     * Runs for DISCOVERY_TIMEOUT_SECONDS seconds
     */
    while (time(nullptr) - start_time < DISCOVERY_TIMEOUT_SECONDS)
    {
        memset(http_response_buffer, 0, sizeof(http_response_buffer)); // Clear buffer

        /**
         * Receive response from any device
         * recvfrom fills sender_address with source info
         */
        ssize_t bytes_received = recvfrom(multicast_socket_fd, http_response_buffer,
                                          sizeof(http_response_buffer) - 1, 0,
                                          (sockaddr *)&sender_address,
                                          &sender_address_length);

        if (bytes_received <= 0)
        {
            continue; // Timeout or no data, keep listening
        }

        http_response_buffer[bytes_received] = '\0';     // Null-terminate response
        std::string http_response(http_response_buffer); // Convert to string for parsing

        /**
         * Get sender IP address for logging and fallback
         */
        char sender_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(sender_address.sin_addr), sender_ip, INET_ADDRSTRLEN);

        std::cout << "📨 [SSDP] Received response from " << sender_ip << std::endl;

        /**
         * Check if this response is for our service
         * Look for our service type or HTTP 200 OK
         */
        if (http_response.find("urn:screen-share:receiver") != std::string::npos ||
            http_response.find("ST: urn:screen-share:receiver") != std::string::npos ||
            http_response.find("200 OK") != std::string::npos)
        {
            /**
             * Parse LOCATION header to extract IP and port
             * LOCATION: http://192.168.1.100:8081/
             */
            size_t location_start = http_response.find("LOCATION: ");
            if (location_start != std::string::npos)
            {
                // Find end of LOCATION line
                size_t location_end = http_response.find("\r\n", location_start);
                std::string location = http_response.substr(location_start + 10,
                                                            location_end - location_start - 10);

                std::cout << "📍 [SSDP] Found LOCATION: " << location << std::endl;

                /**
                 * Parse URL components
                 * Format: http://IP:PORT/
                 */
                size_t protocol_end = location.find("://");
                if (protocol_end != std::string::npos)
                {
                    size_t host_start = protocol_end + 3;               // Skip "://"
                    size_t port_colon = location.find(":", host_start); // Find port separator
                    size_t path_start = location.find("/", host_start); // Find path start

                    std::string ip_address;
                    int tcp_port = 8081; // Default port if not specified

                    if (port_colon != std::string::npos &&
                        (path_start == std::string::npos || port_colon < path_start))
                    {
                        // URL includes port number
                        ip_address = location.substr(host_start, port_colon - host_start);

                        // Extract port number
                        size_t port_end = (path_start != std::string::npos) ? path_start : location.length();
                        std::string port_str = location.substr(port_colon + 1, port_end - port_colon - 1);
                        try
                        {
                            tcp_port = std::stoi(port_str);
                        }
                        catch (...)
                        {
                            tcp_port = 8081; // Fallback on parse error
                        }
                    }
                    else
                    {
                        // No port number in URL
                        if (path_start != std::string::npos)
                            ip_address = location.substr(host_start, path_start - host_start);
                        else
                            ip_address = location.substr(host_start);
                    }

                    /**
                     * Check for duplicate devices (by IP)
                     * Prevents adding same device multiple times
                     */
                    bool already_exists = false;
                    for (const auto &dev : discovered_receivers)
                    {
                        if (dev.ip_address == ip_address)
                        {
                            already_exists = true;
                            break;
                        }
                    }

                    // Add new unique device to list
                    if (!already_exists && !ip_address.empty())
                    {
                        discovered_receivers.emplace_back(ip_address, tcp_port);
                        std::cout << "✅ [SSDP] Discovered receiver: " << ip_address
                                  << ":" << tcp_port << std::endl;
                    }
                }
            }
            else
            {
                /**
                 * Fallback: Use sender IP if no LOCATION header
                 * Some SSDP implementations might omit LOCATION
                 */
                std::string sender_ip_str(sender_ip);

                bool already_exists = false;
                for (const auto &dev : discovered_receivers)
                {
                    if (dev.ip_address == sender_ip_str)
                    {
                        already_exists = true;
                        break;
                    }
                }

                if (!already_exists)
                {
                    discovered_receivers.emplace_back(sender_ip_str, 8081);
                    std::cout << "✅ [SSDP] Discovered receiver from source IP: "
                              << sender_ip_str << ":8081" << std::endl;
                }
            }
        }
    }

    /**
     * Clean up socket and Windows networking
     */
#ifdef _WIN32
    closesocket(multicast_socket_fd);
    WSACleanup(); // Unload Winsock
#else
    close(multicast_socket_fd);
#endif

    std::cout << "📋 [SSDP] Discovery complete: " << discovered_receivers.size()
              << " receiver(s) found" << std::endl;

    return discovered_receivers; // Return all discovered devices
}

/**
 * ============================================================================
 * QUICK AVAILABILITY CHECK
 * ============================================================================
 * Convenience function to check if any receivers exist.
 * @return true if at least one receiver discovered
 */
bool hasReceivers()
{
    std::vector<DiscoveredDevice> devices = discoverReceivers(); // Run discovery
    return !devices.empty();                                     // Return true if any devices found
}

/**
 * ============================================================================
 * DEVICE LIST FORMATTER
 * ============================================================================
 * Creates user-friendly display of discovered devices.
 * @param devices Vector of discovered devices
 * @return Formatted string for console output
 */
std::string listDevices(const std::vector<DiscoveredDevice> &devices)
{
    std::string formatted_list = "\n📱 DISCOVERED SCREEN RECEIVERS:\n";

    for (size_t index = 0; index < devices.size(); ++index)
    {
        // Add each device with numbered index [0], [1], etc.
        formatted_list += "  [" + std::to_string(index) + "] " + devices[index].toString() + "\n";
    }

    if (devices.empty())
    {
        formatted_list += "  No receivers found on network\n";
    }

    formatted_list += "\n";
    return formatted_list;
}