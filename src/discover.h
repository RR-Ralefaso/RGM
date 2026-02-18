/**
 * DISCOVER.H - SSDP ZERO-CONFIG DISCOVERY API
 */
#ifndef DISCOVER_H
#define DISCOVER_H

#include <vector>
#include <string>

struct DiscoveredDevice
{
    std::string ip_address;
    int tcp_port;
    std::string service_uuid;

    DiscoveredDevice(const std::string &ip, int port, const std::string &uuid = "")
        : ip_address(ip), tcp_port(port), service_uuid(uuid) {}

    std::string toString() const
    {
        return ip_address + ":" + std::to_string(tcp_port);
    }
};

std::vector<DiscoveredDevice> discoverReceivers();
bool hasReceivers();
std::string listDevices(const std::vector<DiscoveredDevice> &devices);
std::string getLocalIPAddress();

#endif
