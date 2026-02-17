#include "discover.h"
#include <iostream> // Console output for discovery progress
#include <cstring>  // memset() for socket address initialization
#include <chrono>   // Timing (not used directly but available)

#ifdef _WIN32
#include <winsock2.h>              // Windows socket API (WSAStartup, sendto, recvfrom)
#pragma comment(lib, "ws2_32.lib") // Auto-link Winsock library
#else
#include <sys/socket.h> // POSIX socket functions (socket, sendto, recvfrom)
#include <netinet/in.h> // sockaddr_in structure definition
#include <arpa/inet.h>  // inet_pton() IP address conversion
#include <unistd.h>     // close() socket cleanup
#include <sys/time.h>   // timeval structure for receive timeout
#endif

// ============================================================================
// SSDP PROTOCOL CONSTANTS (UPnP/SSDP Standard)
// ============================================================================
static const char *SSDP_MULTICAST_GROUP = "239.255.255.250";                // SSDP multicast IP
static const int SSDP_MULTICAST_PORT = 1900;                                // Standard SSDP port
static const int DISCOVERY_TIMEOUT_SECONDS = 3;                             // 3-second discovery window
static const char *SCREEN_SHARE_SERVICE_TYPE = "urn:screen-share:receiver"; // Our custom service UUID

/**
 * ============================================================================
 * PRIMARY DISCOVERY FUNCTION - Network-Aware Device Scanner
 * ============================================================================
 * 1. Creates multicast UDP socket
 * 2. Sends SSDP M-SEARCH request to 239.255.255.250:1900
 * 3. Listens for HTTP 200 OK responses with LOCATION headers
 * 4. Parses IP:PORT from LOCATION: http://192.168.1.100:8081/
 * 5. Deduplicates devices by IP address
 *
 * @return Vector of DiscoveredDevice structs (empty if none found)
 */
std::vector<DiscoveredDevice> discoverReceivers()
{
    std::vector<DiscoveredDevice> discovered_receivers;

    // === INITIALIZATION ===
    std::cout << "🔍 [SSDP DISCOVERY] Scanning " << SSDP_MULTICAST_GROUP
              << ":" << SSDP_MULTICAST_PORT << " for screen receivers..." << std::endl;

    // Windows: Initialize Winsock (required one-time call)
#ifdef _WIN32
    WSADATA winsock_data;
    if (WSAStartup(MAKEWORD(2, 2), &winsock_data) != 0)
    {
        std::cerr << "❌ [SSDP] Windows networking initialization failed" << std::endl;
        return discovered_receivers;
    }
#endif

    // === CREATE MULTICAST UDP SOCKET ===
    int multicast_socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (multicast_socket_fd < 0)
    {
        std::cerr << "❌ [SSDP] Failed to create multicast socket" << std::endl;
        return discovered_receivers;
    }

    // === CONFIGURE SOCKET FOR MULTICAST ===
    // Allow address reuse (multiple apps can bind to 239.255.255.250:1900)
    int reuse_address_option = 1;
    setsockopt(multicast_socket_fd, SOL_SOCKET, SO_REUSEADDR,
               (char *)&reuse_address_option, sizeof(reuse_address_option));

    // Limit multicast to local network (TTL = 2 hops max)
    int multicast_ttl = 2;
    setsockopt(multicast_socket_fd, IPPROTO_IP, IP_MULTICAST_TTL,
               (char *)&multicast_ttl, sizeof(multicast_ttl));

    // === SEND SSDP M-SEARCH DISCOVERY REQUEST ===
    // Standard UPnP/SSDP discovery message format
    const char *ssdp_msearch_request =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 3\r\n"                         // Maximum 3-second response delay
        "ST: urn:screen-share:receiver\r\n" // Request OUR specific service type
        "\r\n";                             // Empty line ends HTTP headers

    // Multicast destination address structure
    sockaddr_in multicast_destination;
    memset(&multicast_destination, 0, sizeof(multicast_destination));
    multicast_destination.sin_family = AF_INET;                                // IPv4 protocol
    multicast_destination.sin_port = htons(SSDP_MULTICAST_PORT);               // Network byte order
    inet_pton(AF_INET, SSDP_MULTICAST_GROUP, &multicast_destination.sin_addr); // String → binary IP

    // Transmit discovery request to ALL multicast receivers on LAN
    ssize_t bytes_sent = sendto(multicast_socket_fd, ssdp_msearch_request,
                                strlen(ssdp_msearch_request), 0,
                                (sockaddr *)&multicast_destination, sizeof(multicast_destination));

    if (bytes_sent < 0)
    {
        std::cerr << "❌ [SSDP] Failed to send M-SEARCH request" << std::endl;
    }
    else
    {
        std::cout << "📡 [SSDP] M-SEARCH sent (" << bytes_sent << " bytes)" << std::endl;
    }

    // === LISTEN FOR RECEIVER RESPONSES ===
    // Set 3-second receive timeout
    timeval receive_timeout;
    receive_timeout.tv_sec = DISCOVERY_TIMEOUT_SECONDS;
    receive_timeout.tv_usec = 0;
    setsockopt(multicast_socket_fd, SOL_SOCKET, SO_RCVTIMEO,
               (char *)&receive_timeout, sizeof(receive_timeout));

    // Response buffer for HTTP 200 OK messages
    char http_response_buffer[2048];
    sockaddr_in sender_address;
    socklen_t sender_address_length = sizeof(sender_address);

    // Receive loop - process all responses until timeout
    while (true)
    {
        // Receive multicast response from any receiver
        ssize_t bytes_received = recvfrom(multicast_socket_fd, http_response_buffer,
                                          sizeof(http_response_buffer) - 1, 0,
                                          (sockaddr *)&sender_address, &sender_address_length);

        // Timeout reached - end of discovery phase
        if (bytes_received <= 0)
        {
            break;
        }

        // Null-terminate received HTTP response
        http_response_buffer[bytes_received] = '\0';
        std::string http_response(http_response_buffer);

        // === PARSE LOCATION HEADER ===
        // Extract receiver endpoint from: LOCATION: http://192.168.1.100:8081/
        size_t location_header_start = http_response.find("LOCATION: ");
        if (location_header_start == std::string::npos)
        {
            continue; // Skip responses without LOCATION header
        }

        // Find end of LOCATION line
        size_t location_line_end = http_response.find("\r\n", location_header_start);
        std::string location_url = http_response.substr(location_header_start + 9, // Skip "LOCATION: "
                                                        location_line_end - location_header_start - 9);

        // === PARSE URL COMPONENTS ===
        // URL format: "http://192.168.1.100:8081/"
        size_t protocol_delimiter = location_url.find("://");
        if (protocol_delimiter == std::string::npos)
        {
            continue; // Malformed URL
        }

        // Extract IP address portion
        size_t ip_address_start = protocol_delimiter + 3; // Skip "://"
        size_t colon_position = location_url.find(":", ip_address_start);
        size_t path_position = location_url.find("/", colon_position);

        // Extract IP address
        std::string receiver_ip_address = location_url.substr(ip_address_start,
                                                              colon_position - ip_address_start);

        // Default TCP streaming port
        int receiver_tcp_port = 8081;
        if (colon_position != std::string::npos)
        {
            // Extract port number from "8081/"
            size_t port_end = (path_position != std::string::npos) ? path_position : location_url.size();
            std::string port_string = location_url.substr(colon_position + 1, port_end - colon_position - 1);
            receiver_tcp_port = std::stoi(port_string);
        }

        // === DEDUPLICATE DEVICES ===
        // Prevent adding same receiver multiple times
        bool device_already_discovered = false;
        for (const auto &existing_device : discovered_receivers)
        {
            if (existing_device.ip_address == receiver_ip_address)
            {
                device_already_discovered = true;
                break;
            }
        }

        // Add new unique receiver to list
        if (!device_already_discovered)
        {
            discovered_receivers.emplace_back(receiver_ip_address, receiver_tcp_port);
            std::cout << "✅ [SSDP] Discovered receiver: " << receiver_ip_address
                      << ":" << receiver_tcp_port << std::endl;
        }
    }

    // === CLEANUP DISCOVERY SOCKET ===
#ifdef _WIN32
    closesocket(multicast_socket_fd);
#else
    close(multicast_socket_fd);
#endif

    std::cout << "📋 [SSDP] Discovery complete: " << discovered_receivers.size()
              << " receiver(s) found" << std::endl;

    return discovered_receivers;
}

/**
 * ============================================================================
 * FAST AVAILABILITY CHECK
 * ============================================================================
 * Returns true if any screen receivers detected on network
 */
bool hasReceivers()
{
    std::vector<DiscoveredDevice> devices = discoverReceivers();
    return !devices.empty();
}

/**
 * ============================================================================
 * USER INTERFACE HELPER - Format Device List
 * ============================================================================
 * Creates numbered list for user selection:
 * 📱 DISCOVERED RECEIVERS:
 *   [0] 192.168.1.100:8081
 *   [1] 192.168.1.101:8081
 */
std::string listDevices(const std::vector<DiscoveredDevice> &devices)
{
    std::string formatted_list = "\n📱 DISCOVERED SCREEN RECEIVERS:\n";

    for (size_t index = 0; index < devices.size(); ++index)
    {
        formatted_list += "  [" + std::to_string(index) + "] " + devices[index].toString() + "\n";
    }

    formatted_list += "\n";
    return formatted_list;
}
