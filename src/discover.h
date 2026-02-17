#ifndef DISCOVER_H
#define DISCOVER_H

#include <vector>
#include <string>

/**
 * ============================================================================
 * SSDP AUTO-DISCOVERY API (UPnP Standard)
 * ============================================================================
 * PURPOSE: Zero-configuration LAN device discovery using multicast SSDP protocol
 * BENEFITS:
 *  - No manual IP entry required
 *  - No port forwarding needed
 *  - Works across subnets (TTL=2)
 *  - Industry standard (used by Chromecast, Smart TVs, IoT devices)
 */

struct DiscoveredDevice
{
    std::string ip_address;   // Receiver LAN IP (ex: "192.168.1.100")
    int tcp_port;             // TCP port for screen streaming (default: 8081)
    std::string service_uuid; // Unique device identifier (future pinning)

    /**
     * Constructor for discovered devices
     * @param ip Receiver IP address from SSDP LOCATION header
     * @param port TCP streaming port (parsed from LOCATION URL)
     * @param uuid Service UUID (optional)
     */
    DiscoveredDevice(const std::string &ip, int port, const std::string &uuid = "")
        : ip_address(ip), tcp_port(port), service_uuid(uuid) {}

    /**
     * Human-readable device string (IP:PORT format)
     */
    std::string toString() const
    {
        return ip_address + ":" + std::to_string(tcp_port);
    }
};

/**
 * MAIN DISCOVERY FUNCTION
 * ========================
 * Sends SSDP M-SEARCH multicast and collects receiver responses
 * @return Vector of all discovered screen receivers (3-second scan)
 */
std::vector<DiscoveredDevice> discoverReceivers();

/**
 * QUICK AVAILABILITY CHECK
 * ========================
 * Fast test - returns true if any receivers detected
 */
bool hasReceivers();

/**
 * FORMAT DEVICE LIST FOR UI
 * =========================
 * Creates numbered list for user selection display
 * @param devices List from discoverReceivers()
 * @return Formatted string with [0] 192.168.1.100:8081 format
 */
std::string listDevices(const std::vector<DiscoveredDevice> &devices);

#endif // DISCOVER_H
