/**
 * ============================================================================
 * DISCOVER.H - SSDP AUTO-DISCOVERY API HEADER
 * ============================================================================
 * This header defines the interface for UPnP/SSDP-based zero-configuration
 * network discovery of screen receivers. It provides functions to discover
 * devices on the local network without manual IP configuration.
 *
 * The SSDP protocol works by:
 * 1. Sending multicast M-SEARCH requests to 239.255.255.250:1900
 * 2. Receiving HTTP-style responses from receivers
 * 3. Parsing LOCATION headers to extract IP:PORT endpoints
 */

#ifndef DISCOVER_H
#define DISCOVER_H

#include <vector> // For dynamic list of discovered devices
#include <string> // For IP address and service UUID strings

/**
 * ============================================================================
 * DISCOVEREDDEVICE STRUCTURE
 * ============================================================================
 * Represents a single screen receiver found on the network.
 * Stores all necessary connection information for the sender.
 */
struct DiscoveredDevice
{
    std::string ip_address;   // IPv4 address as string (e.g., "192.168.1.100")
    int tcp_port;             // TCP port for screen streaming (typically 8081)
    std::string service_uuid; // Universal Unique ID for service identification

    /**
     * Constructor - Creates a discovered device record
     * @param ip The IP address extracted from SSDP LOCATION header
     * @param port The TCP port parsed from URL (defaults to 8081)
     * @param uuid Optional service identifier for future features
     */
    DiscoveredDevice(const std::string &ip, int port, const std::string &uuid = "")
        : ip_address(ip), tcp_port(port), service_uuid(uuid) {}

    /**
     * Converts device info to human-readable format
     * @return String like "192.168.1.100:8081"
     */
    std::string toString() const
    {
        return ip_address + ":" + std::to_string(tcp_port);
    }
};

/**
 * ============================================================================
 * PRIMARY DISCOVERY FUNCTION
 * ============================================================================
 * Performs network scan for screen receivers using SSDP.
 * @return Vector of all discovered devices (empty if none found)
 *
 * Implementation details:
 * - Creates UDP multicast socket
 * - Sends M-SEARCH request for "urn:screen-share:receiver" service
 * - Waits 3 seconds for responses
 * - Parses LOCATION headers for IP and port
 * - Deduplicates devices by IP address
 */
std::vector<DiscoveredDevice> discoverReceivers();

/**
 * ============================================================================
 * QUICK AVAILABILITY CHECK
 * ============================================================================
 * Fast test to check if any receivers exist on network.
 * @return true if at least one receiver responds
 *
 * Used for initial UI feedback or quick network checks.
 */
bool hasReceivers();

/**
 * ============================================================================
 * DEVICE LIST FORMATTER
 * ============================================================================
 * Creates a formatted string for displaying devices in console UI.
 * @param devices Vector of discovered devices from discoverReceivers()
 * @return Formatted string with numbered list:
 *         📱 DISCOVERED SCREEN RECEIVERS:
 *           [0] 192.168.1.100:8081
 *           [1] 192.168.1.101:8081
 */
std::string listDevices(const std::vector<DiscoveredDevice> &devices);

#endif // DISCOVER_H