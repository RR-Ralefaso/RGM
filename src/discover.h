/**
 * DISCOVER.H - SSDP ZERO-CONFIG DISCOVERY API
 */
#ifndef DISCOVER_H
#define DISCOVER_H

#include <vector>
#include <string>

// Socket helper functions (declarations only)
bool initSockets();
void cleanupSockets();

struct DiscoveredDevice
{
    std::string ip_address;
    int tcp_port;
    std::string service_uuid;
    std::string location_url;

    DiscoveredDevice(const std::string &ip, int port, const std::string &uuid = "")
        : ip_address(ip), tcp_port(port), service_uuid(uuid)
    {
        location_url = "http://" + ip + ":" + std::to_string(port) + "/";
    }

    std::string toString() const
    {
        return ip_address + ":" + std::to_string(tcp_port);
    }
};

// Discovery functions
std::vector<DiscoveredDevice> discoverReceivers(int timeout_seconds = 5);
bool hasReceivers();
std::string listDevices(const std::vector<DiscoveredDevice> &devices);
std::string getLocalIPAddress();
bool testTcpConnection(const std::string &ip, int port, int timeout_ms = 1000);

#endif