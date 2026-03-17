/**
 * PORTS.CPP  –  REMOTE PORT INSPECTION ENGINE
 *
 * Sender-side client + Receiver-side server.
 *
 * Receiver port collection:
 *   Linux   : /proc/net/tcp, tcp6, udp, udp6  +  /proc/PID/cmdline
 *   macOS   : popen("lsof -nP -iTCP -iUDP -sTCP:LISTEN")
 *   Windows : GetExtendedTcpTable / GetExtendedUdpTable  (iphlpapi)
 *
 * All collected entries are serialised as PortEntry wire structs and
 * streamed back to the sender after a PortResponse header.
 */

#include "ports.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <tlhelp32.h>
#include <processthreadsapi.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <sys/stat.h>
#endif

#ifdef __APPLE__
#include <sys/proc_info.h>
#include <libproc.h>
#include <sys/sysctl.h>
#endif

/* ─── C++ includes (file is compiled as C++) ─────────────────── */
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>
#include <mutex>

/* ─── Global debug flag ──────────────────────────────────────── */
static bool g_debug = false;
#define DEBUG_PRINT(fmt, ...)                                    \
    do                                                           \
    {                                                            \
        if (g_debug)                                             \
            fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__); \
    } while (0)

/* ─── I/O helpers ─────────────────────────────────────────────── */
static int send_all(int sock, const void *buf, size_t size)
{
    const char *p = (const char *)buf;
    size_t sent = 0;
    while (sent < size)
    {
        int n;
#ifdef _WIN32
        n = send(sock, p + sent, (int)(size - sent), 0);
#else
        n = (int)send(sock, p + sent, size - sent, 0);
#endif
        if (n <= 0)
        {
            if (n < 0)
            {
#ifdef _WIN32
                if (WSAGetLastError() == WSAEINTR || WSAGetLastError() == WSAEWOULDBLOCK)
                    continue;
#else
                if (errno == EINTR || errno == EAGAIN)
                    continue;
#endif
            }
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

static int recv_all(int sock, void *buf, size_t size)
{
    char *p = (char *)buf;
    size_t got = 0;
    while (got < size)
    {
        int n;
#ifdef _WIN32
        n = recv(sock, p + got, (int)(size - got), 0);
#else
        n = (int)recv(sock, p + got, size - got, 0);
#endif
        if (n <= 0)
        {
            if (n < 0)
            {
#ifdef _WIN32
                if (WSAGetLastError() == WSAEINTR || WSAGetLastError() == WSAEWOULDBLOCK)
                    continue;
#else
                if (errno == EINTR || errno == EAGAIN)
                    continue;
#endif
            }
            return -1;
        }
        got += (size_t)n;
    }
    return 0;
}

#ifdef _WIN32
/* ─── Windows socket initialization ──────────────────────────── */
static bool ensure_winsock()
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        fprintf(stderr, "[PORTS] WSAStartup failed\n");
        return false;
    }
    return true;
}
#endif

/* ════════════════════════════════════════════════════════════════
 * PLATFORM: COLLECT PORT ENTRIES
 * ════════════════════════════════════════════════════════════════ */

#ifdef _WIN32
/* ── Windows ───────────────────────────────────────────────────── */

static std::string win_process_name(DWORD pid)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return "?";
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    std::string name = "?";

    if (Process32FirstW(snap, &pe))
    {
        do
        {
            if (pe.th32ProcessID == pid)
            {
                char buffer[256];
                wcstombs(buffer, pe.szExeFile, sizeof(buffer));
                name = buffer;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    // Extract just the filename without path
    size_t pos = name.find_last_of("\\/");
    if (pos != std::string::npos)
    {
        name = name.substr(pos + 1);
    }
    return name;
}

static uint8_t win_tcp_state(DWORD s)
{
    switch (s)
    {
    case MIB_TCP_STATE_CLOSED:
        return PSTATE_CLOSE;
    case MIB_TCP_STATE_LISTEN:
        return PSTATE_LISTEN;
    case MIB_TCP_STATE_SYN_SENT:
        return PSTATE_SYN_SENT;
    case MIB_TCP_STATE_SYN_RCVD:
        return PSTATE_SYN_RECV;
    case MIB_TCP_STATE_ESTAB:
        return PSTATE_ESTABLISHED;
    case MIB_TCP_STATE_FIN_WAIT1:
        return PSTATE_FIN_WAIT1;
    case MIB_TCP_STATE_FIN_WAIT2:
        return PSTATE_FIN_WAIT2;
    case MIB_TCP_STATE_CLOSE_WAIT:
        return PSTATE_CLOSE_WAIT;
    case MIB_TCP_STATE_CLOSING:
        return PSTATE_CLOSING;
    case MIB_TCP_STATE_LAST_ACK:
        return PSTATE_LAST_ACK;
    case MIB_TCP_STATE_TIME_WAIT:
        return PSTATE_TIME_WAIT;
    default:
        return PSTATE_UNKNOWN;
    }
}

static std::vector<PortEntry> collect_ports_windows(uint8_t proto_filter)
{
    std::vector<PortEntry> out;
    DEBUG_PRINT("Collecting ports on Windows, filter=%d", proto_filter);

    /* ── TCP IPv4 ── */
    if (proto_filter == 0 || proto_filter == 2)
    {
        ULONG sz = 0;
        DWORD result;

        // First call to get size
        result = GetExtendedTcpTable(nullptr, &sz, FALSE, AF_INET,
                                     TCP_TABLE_OWNER_PID_ALL, 0);
        if (result == ERROR_INSUFFICIENT_BUFFER && sz > 0)
        {
            std::vector<BYTE> buf(sz);
            result = GetExtendedTcpTable(buf.data(), &sz, FALSE, AF_INET,
                                         TCP_TABLE_OWNER_PID_ALL, 0);
            if (result == NO_ERROR)
            {
                auto *tbl = (MIB_TCPTABLE_OWNER_PID *)buf.data();
                DEBUG_PRINT("Found %d TCP IPv4 entries", tbl->dwNumEntries);

                for (DWORD i = 0; i < tbl->dwNumEntries; i++)
                {
                    auto &row = tbl->table[i];
                    PortEntry e = {};
                    e.proto = 0;
                    e.ip_ver = 4;
                    e.state = win_tcp_state(row.dwState);
                    e.local_port = ntohs((uint16_t)row.dwLocalPort);
                    e.remote_port = ntohs((uint16_t)row.dwRemotePort);

                    // Convert IP addresses (already in network byte order)
                    memcpy(e.local_ip, &row.dwLocalAddr, 4);
                    memcpy(e.remote_ip, &row.dwRemoteAddr, 4);

                    e.pid = row.dwOwningPid;
                    auto name = win_process_name(row.dwOwningPid);
                    strncpy(e.process, name.c_str(), sizeof(e.process) - 1);
                    e.process[sizeof(e.process) - 1] = '\0';

                    out.push_back(e);
                }
            }
        }

        /* TCP IPv6 */
        sz = 0;
        result = GetExtendedTcpTable(nullptr, &sz, FALSE, AF_INET6,
                                     TCP_TABLE_OWNER_PID_ALL, 0);
        if (result == ERROR_INSUFFICIENT_BUFFER && sz > 0)
        {
            std::vector<BYTE> buf(sz);
            result = GetExtendedTcpTable(buf.data(), &sz, FALSE, AF_INET6,
                                         TCP_TABLE_OWNER_PID_ALL, 0);
            if (result == NO_ERROR)
            {
                auto *tbl = (MIB_TCP6TABLE_OWNER_PID *)buf.data();
                DEBUG_PRINT("Found %d TCP IPv6 entries", tbl->dwNumEntries);

                for (DWORD i = 0; i < tbl->dwNumEntries; i++)
                {
                    auto &row = tbl->table[i];
                    PortEntry e = {};
                    e.proto = 0;
                    e.ip_ver = 6;
                    e.state = win_tcp_state(row.dwState);
                    e.local_port = ntohs((uint16_t)row.dwLocalPort);
                    e.remote_port = ntohs((uint16_t)row.dwRemotePort);

                    // Copy IPv6 addresses (16 bytes each)
                    memcpy(e.local_ip, row.ucLocalAddr, 16);
                    memcpy(e.remote_ip, row.ucRemoteAddr, 16);

                    e.pid = row.dwOwningPid;
                    auto name = win_process_name(row.dwOwningPid);
                    strncpy(e.process, name.c_str(), sizeof(e.process) - 1);
                    e.process[sizeof(e.process) - 1] = '\0';

                    out.push_back(e);
                }
            }
        }
    }

    /* ── UDP ── */
    if (proto_filter == 1 || proto_filter == 2)
    {
        ULONG sz = 0;
        DWORD result;

        // UDP IPv4
        result = GetExtendedUdpTable(nullptr, &sz, FALSE, AF_INET,
                                     UDP_TABLE_OWNER_PID, 0);
        if (result == ERROR_INSUFFICIENT_BUFFER && sz > 0)
        {
            std::vector<BYTE> buf(sz);
            result = GetExtendedUdpTable(buf.data(), &sz, FALSE, AF_INET,
                                         UDP_TABLE_OWNER_PID, 0);
            if (result == NO_ERROR)
            {
                auto *tbl = (MIB_UDPTABLE_OWNER_PID *)buf.data();
                DEBUG_PRINT("Found %d UDP IPv4 entries", tbl->dwNumEntries);

                for (DWORD i = 0; i < tbl->dwNumEntries; i++)
                {
                    auto &row = tbl->table[i];
                    PortEntry e = {};
                    e.proto = 1;
                    e.ip_ver = 4;
                    e.state = PSTATE_UNKNOWN;
                    e.local_port = ntohs((uint16_t)row.dwLocalPort);

                    memcpy(e.local_ip, &row.dwLocalAddr, 4);

                    e.pid = row.dwOwningPid;
                    auto name = win_process_name(row.dwOwningPid);
                    strncpy(e.process, name.c_str(), sizeof(e.process) - 1);
                    e.process[sizeof(e.process) - 1] = '\0';

                    out.push_back(e);
                }
            }
        }

        // UDP IPv6 (if supported)
        sz = 0;
        result = GetExtendedUdpTable(nullptr, &sz, FALSE, AF_INET6,
                                     UDP_TABLE_OWNER_PID, 0);
        if (result == ERROR_INSUFFICIENT_BUFFER && sz > 0)
        {
            std::vector<BYTE> buf(sz);
            result = GetExtendedUdpTable(buf.data(), &sz, FALSE, AF_INET6,
                                         UDP_TABLE_OWNER_PID, 0);
            if (result == NO_ERROR)
            {
                auto *tbl = (MIB_UDP6TABLE_OWNER_PID *)buf.data();
                DEBUG_PRINT("Found %d UDP IPv6 entries", tbl->dwNumEntries);

                for (DWORD i = 0; i < tbl->dwNumEntries; i++)
                {
                    auto &row = tbl->table[i];
                    PortEntry e = {};
                    e.proto = 1;
                    e.ip_ver = 6;
                    e.state = PSTATE_UNKNOWN;
                    e.local_port = ntohs((uint16_t)row.dwLocalPort);

                    memcpy(e.local_ip, row.ucLocalAddr, 16);

                    e.pid = row.dwOwningPid;
                    auto name = win_process_name(row.dwOwningPid);
                    strncpy(e.process, name.c_str(), sizeof(e.process) - 1);
                    e.process[sizeof(e.process) - 1] = '\0';

                    out.push_back(e);
                }
            }
        }
    }

    DEBUG_PRINT("Total collected entries: %zu", out.size());
    return out;
}

#elif defined(__linux__)
/* ── Linux  (/proc/net) ────────────────────────────────────────── */

#include <dirent.h>
#include <sys/types.h>
#include <unistd.h>

static std::string linux_cmdline(uint32_t pid, bool full_path = false)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%u/cmdline", pid);
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
        return "?";

    std::string cmd;
    std::getline(f, cmd, '\0');

    if (!full_path)
    {
        // Extract just the basename
        auto pos = cmd.rfind('/');
        if (pos != std::string::npos)
            cmd = cmd.substr(pos + 1);
    }

    if (cmd.empty())
        return "?";
    if (cmd.size() > 62)
        cmd.resize(62);
    return cmd;
}

/* Convert 8-hex-char little-endian IPv4 to bytes */
static void parse_hex_ipv4(const std::string &s, uint8_t out[16])
{
    uint32_t n = (uint32_t)strtoul(s.c_str(), nullptr, 16);
    // /proc/net stores in host byte order on LE machines
    out[0] = (uint8_t)(n & 0xFF);
    out[1] = (uint8_t)((n >> 8) & 0xFF);
    out[2] = (uint8_t)((n >> 16) & 0xFF);
    out[3] = (uint8_t)((n >> 24) & 0xFF);
}

static void parse_hex_ipv6(const std::string &s, uint8_t out[16])
{
    // Format: 00000000000000000000000000000000 (32 hex chars)
    for (int i = 0; i < 16; i++)
    {
        char byte_str[3] = {s[i * 2], s[i * 2 + 1], '\0'};
        out[i] = (uint8_t)strtoul(byte_str, nullptr, 16);
    }
}

struct PidInode
{
    uint32_t pid;
    uint64_t inode;
};

/* Build a map inode-to-pid by scanning proc-pid-fd - OPTIMIZED VERSION */
static std::vector<PidInode> build_inode_map()
{
    std::vector<PidInode> map;
    DIR *proc = opendir("/proc");
    if (!proc)
    {
        DEBUG_PRINT("Cannot open /proc");
        return map;
    }

    struct dirent *entry;
    while ((entry = readdir(proc)) != NULL)
    {
        if (entry->d_type != DT_DIR)
            continue;

        // Check if directory name is a number (PID)
        char *endptr;
        long pid = strtol(entry->d_name, &endptr, 10);
        if (*endptr != '\0')
            continue; // Not a PID

        // Build path to fd directory
        char fd_path[256];
        snprintf(fd_path, sizeof(fd_path), "/proc/%ld/fd", pid);

        DIR *fd_dir = opendir(fd_path);
        if (!fd_dir)
            continue;

        struct dirent *fd_entry;
        while ((fd_entry = readdir(fd_dir)) != NULL)
        {
            if (fd_entry->d_type != DT_LNK)
                continue;

            char link_path[256];
            snprintf(link_path, sizeof(link_path), "/proc/%ld/fd/%s", pid, fd_entry->d_name);

            char link_target[256];
            ssize_t len = readlink(link_path, link_target, sizeof(link_target) - 1);
            if (len < 0)
                continue;
            link_target[len] = '\0';

            // Check for socket inode pattern: socket:[12345]
            if (strncmp(link_target, "socket:[", 8) == 0)
            {
                uint64_t inode = strtoull(link_target + 8, NULL, 10);
                map.push_back({(uint32_t)pid, inode});
                break; // Found one socket for this PID, move to next PID
            }
        }
        closedir(fd_dir);
    }
    closedir(proc);

    DEBUG_PRINT("Built inode map with %zu entries", map.size());
    return map;
}

static uint32_t inode_to_pid(const std::vector<PidInode> &map, uint64_t inode)
{
    for (const auto &e : map)
    {
        if (e.inode == inode)
            return e.pid;
    }
    return 0;
}

static std::vector<PortEntry> parse_proc_net(const char *path,
                                             uint8_t proto,
                                             bool is_v6,
                                             const std::vector<PidInode> &imap)
{
    std::vector<PortEntry> out;
    std::ifstream f(path);
    if (!f.is_open())
    {
        DEBUG_PRINT("Cannot open %s", path);
        return out;
    }

    std::string line;
    std::getline(f, line); // Skip header

    while (std::getline(f, line))
    {
        if (line.empty())
            continue;

        std::istringstream ss(line);
        std::string sl, local, remote, state_hex;
        ss >> sl >> local >> remote >> state_hex;

        // Validate required fields
        if (local.empty() || remote.empty() || state_hex.empty())
        {
            DEBUG_PRINT("Skipping malformed line: %s", line.c_str());
            continue;
        }

        // Skip the 4 fields: tx_queue rx_queue tr tm->when
        std::string drop;
        for (int i = 0; i < 4; i++)
            ss >> drop;

        std::string inode_str;
        ss >> drop >> drop >> inode_str; // retrnsmt uid timeout inode

        // Parse local address and port
        auto colon = local.find(':');
        if (colon == std::string::npos)
            continue;
        std::string ip_s = local.substr(0, colon);
        std::string port_s = local.substr(colon + 1);

        // Parse remote address and port
        auto rcolon = remote.find(':');
        if (rcolon == std::string::npos)
            continue;
        std::string rip_s = remote.substr(0, rcolon);
        std::string rport_s = remote.substr(rcolon + 1);

        PortEntry e = {};
        e.proto = proto;
        e.ip_ver = is_v6 ? 6 : 4;

        // Parse state (Linux states match our PSTATE_* values)
        e.state = (uint8_t)strtoul(state_hex.c_str(), nullptr, 16);

        // Parse ports (hex format in /proc/net)
        e.local_port = (uint16_t)strtoul(port_s.c_str(), nullptr, 16);
        e.remote_port = (uint16_t)strtoul(rport_s.c_str(), nullptr, 16);

        // Parse IP addresses
        if (is_v6)
        {
            if (ip_s.length() >= 32)
                parse_hex_ipv6(ip_s, e.local_ip);
            if (rip_s.length() >= 32)
                parse_hex_ipv6(rip_s, e.remote_ip);
        }
        else
        {
            if (ip_s.length() >= 8)
                parse_hex_ipv4(ip_s, e.local_ip);
            if (rip_s.length() >= 8)
                parse_hex_ipv4(rip_s, e.remote_ip);
        }

        // Get PID from inode
        if (!inode_str.empty())
        {
            uint64_t inode = (uint64_t)strtoull(inode_str.c_str(), nullptr, 10);
            e.pid = inode_to_pid(imap, inode);

            if (e.pid > 0)
            {
                auto name = linux_cmdline(e.pid, false);
                strncpy(e.process, name.c_str(), sizeof(e.process) - 1);
                e.process[sizeof(e.process) - 1] = '\0';

                // Get full cmdline
                auto full = linux_cmdline(e.pid, true);
                strncpy(e.cmdline, full.c_str(), sizeof(e.cmdline) - 1);
                e.cmdline[sizeof(e.cmdline) - 1] = '\0';
            }
        }

        out.push_back(e);
    }

    DEBUG_PRINT("Parsed %zu entries from %s", out.size(), path);
    return out;
}

static std::vector<PortEntry> collect_ports_linux(uint8_t proto_filter)
{
    std::vector<PortEntry> out;
    DEBUG_PRINT("Collecting ports on Linux, filter=%d", proto_filter);

    auto imap = build_inode_map();

    if (proto_filter == 0 || proto_filter == 2)
    {
        auto t4 = parse_proc_net("/proc/net/tcp", 0, false, imap);
        auto t6 = parse_proc_net("/proc/net/tcp6", 0, true, imap);
        out.insert(out.end(), t4.begin(), t4.end());
        out.insert(out.end(), t6.begin(), t6.end());
    }
    if (proto_filter == 1 || proto_filter == 2)
    {
        auto u4 = parse_proc_net("/proc/net/udp", 1, false, imap);
        auto u6 = parse_proc_net("/proc/net/udp6", 1, true, imap);
        out.insert(out.end(), u4.begin(), u4.end());
        out.insert(out.end(), u6.begin(), u6.end());
    }

    DEBUG_PRINT("Total collected entries: %zu", out.size());
    return out;
}

#elif defined(__APPLE__)
/* ── macOS (lsof parse with improved parsing) ──────────────────── */

static std::string macos_process_name(pid_t pid)
{
    char path[PROC_PIDPATHINFO_MAXSIZE];
    int ret = proc_pidpath(pid, path, sizeof(path));
    if (ret <= 0)
        return "?";

    // Extract basename
    char *last_slash = strrchr(path, '/');
    if (last_slash)
    {
        return std::string(last_slash + 1);
    }
    return std::string(path);
}

static std::vector<PortEntry> collect_ports_macos(uint8_t proto_filter)
{
    std::vector<PortEntry> out;
    DEBUG_PRINT("Collecting ports on macOS, filter=%d", proto_filter);

    // Use lsof with machine-readable output
    std::string cmd = "lsof -nP -F pcnP ";
    if (proto_filter == 0)
    {
        cmd += "-iTCP -sTCP:LISTEN";
    }
    else if (proto_filter == 1)
    {
        cmd += "-iUDP";
    }
    else
    {
        cmd += "-iTCP -sTCP:LISTEN -iUDP";
    }
    cmd += " 2>/dev/null";

    FILE *fp = popen(cmd.c_str(), "r");
    if (!fp)
    {
        DEBUG_PRINT("Failed to run lsof");
        return out;
    }

    std::map<pid_t, std::string> process_names;
    std::map<pid_t, std::vector<PortEntry>> entries_by_pid;

    char line[1024];
    PortEntry current;
    memset(&current, 0, sizeof(current));
    bool in_entry = false;
    pid_t current_pid = 0;

    while (fgets(line, sizeof(line), fp))
    {
        if (line[0] == 'p')
        { // PID
            if (in_entry && current_pid > 0)
            {
                if (current.proto == proto_filter || proto_filter == 2)
                {
                    entries_by_pid[current_pid].push_back(current);
                }
            }

            memset(&current, 0, sizeof(current));
            current_pid = atoi(line + 1);
            current.pid = current_pid;
            in_entry = true;
        }
        else if (line[0] == 'c' && in_entry)
        { // Command
            strncpy(current.process, line + 1, sizeof(current.process) - 1);
            current.process[sizeof(current.process) - 1] = '\0';
            process_names[current_pid] = current.process;
        }
        else if (line[0] == 'n' && in_entry)
        { // Name (address:port)
            char *name = line + 1;
            char *colon = strrchr(name, ':');
            if (colon)
            {
                *colon = '\0';
                char *port_str = colon + 1;

                // Parse port
                char *endptr;
                long port = strtol(port_str, &endptr, 10);
                if (*endptr == '\0' || *endptr == ' ')
                {
                    current.local_port = (uint16_t)port;
                }

                // Parse IP address
                if (strchr(name, '['))
                { // IPv6
                    current.ip_ver = 6;
                    char *ip_start = strchr(name, '[') + 1;
                    char *ip_end = strchr(ip_start, ']');
                    if (ip_end)
                    {
                        *ip_end = '\0';
                        inet_pton(AF_INET6, ip_start, current.local_ip);
                    }
                }
                else
                { // IPv4
                    current.ip_ver = 4;
                    inet_pton(AF_INET, name, current.local_ip);
                }

                // Determine protocol from name
                if (strstr(name, "UDP") || strstr(port_str, "UDP"))
                {
                    current.proto = 1;
                }
                else
                {
                    current.proto = 0;
                }

                // Set state for TCP
                if (current.proto == 0)
                {
                    if (strstr(port_str, "LISTEN"))
                    {
                        current.state = PSTATE_LISTEN;
                    }
                    else if (strstr(port_str, "ESTABLISHED"))
                    {
                        current.state = PSTATE_ESTABLISHED;
                    }
                }
            }
        }
    }

    // Add last entry
    if (in_entry && current_pid > 0)
    {
        if (current.proto == proto_filter || proto_filter == 2)
        {
            entries_by_pid[current_pid].push_back(current);
        }
    }

    pclose(fp);

    // Collect all entries
    for (const auto &pair : entries_by_pid)
    {
        for (const auto &entry : pair.second)
        {
            out.push_back(entry);
        }
    }

    DEBUG_PRINT("Total collected entries: %zu", out.size());
    return out;
}
#endif

/* ────────────────────────────────────────────────────────────────
 * Unified collect_ports() dispatch
 * ────────────────────────────────────────────────────────────────
 * proto_filter: 0=TCP  1=UDP  2=ALL
 */
static std::vector<PortEntry> collect_ports(uint8_t proto_filter)
{
#ifdef _WIN32
    return collect_ports_windows(proto_filter);
#elif defined(__linux__)
    return collect_ports_linux(proto_filter);
#elif defined(__APPLE__)
    return collect_ports_macos(proto_filter);
#else
#warning "Unsupported platform for port collection"
    return {}; /* unsupported platform */
#endif
}

/* ════════════════════════════════════════════════════════════════
 * RECEIVER-SIDE SERVER
 * ════════════════════════════════════════════════════════════════ */

void ports_service_handle_client(int client_sock)
{
    char client_ip[INET_ADDRSTRLEN] = "unknown";
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    if (getpeername(client_sock, (struct sockaddr *)&addr, &addr_len) == 0)
    {
        inet_ntop(AF_INET, &addr.sin_addr, client_ip, sizeof(client_ip));
    }

    printf("[PORTS-SVC] Client connected from %s\n", client_ip);

    for (;;)
    {
        PortRequest req;
        memset(&req, 0, sizeof(req));

        if (recv_all(client_sock, &req, sizeof(req)) < 0)
        {
            DEBUG_PRINT("Client disconnected or recv failed");
            break;
        }

        uint32_t magic = ntohl(req.magic);
        if (magic != PORTS_MAGIC)
        {
            fprintf(stderr, "[PORTS-SVC] Bad magic from %s: expected 0x%X, got 0x%X\n",
                    client_ip, PORTS_MAGIC, magic);
            break;
        }

        uint8_t op = req.operation;
        printf("[PORTS-SVC] Request op=0x%02X from %s\n", op, client_ip);

        /* PING */
        if (op == PORTS_OP_PING)
        {
            PortResponse resp = {};
            resp.magic = htonl(PORTS_MAGIC);
            resp.status = 0;
            resp.operation = PORTS_OP_PING;
            if (send_all(client_sock, &resp, sizeof(resp)) < 0)
                break;
            printf("[PORTS-SVC] Ping OK from %s\n", client_ip);
            continue;
        }

        /* Determine what to collect */
        uint8_t proto_filter = 2; /* ALL */
        if (op == PORTS_OP_LIST_TCP)
            proto_filter = 0;
        else if (op == PORTS_OP_LIST_UDP)
            proto_filter = 1;

        auto entries = collect_ports(proto_filter);

        /* Filter by specific port if requested */
        if (op == PORTS_OP_GET_PORT || op == PORTS_OP_KILL_PORT)
        {
            uint16_t target = ntohs(req.target_port);
            std::vector<PortEntry> filtered;
            for (auto &e : entries)
            {
                if (e.local_port == target)
                {
                    filtered.push_back(e);
                }
            }
            entries = std::move(filtered);
            printf("[PORTS-SVC] Filtered to %zu entries for port %u\n",
                   entries.size(), target);
        }

        /* KILL: send SIGTERM to each process on that port */
        if (op == PORTS_OP_KILL_PORT)
        {
            bool killed = false;
            for (auto &e : entries)
            {
                if (e.pid > 0)
                {
#ifdef _WIN32
                    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, e.pid);
                    if (h)
                    {
                        TerminateProcess(h, 1);
                        CloseHandle(h);
                        killed = true;
                        printf("[PORTS-SVC] Terminated PID %u (%s) on port %u\n",
                               e.pid, e.process, e.local_port);
                    }
#else
                    int ret = kill((pid_t)e.pid, SIGTERM);
                    if (ret == 0)
                    {
                        killed = true;
                        printf("[PORTS-SVC] Sent SIGTERM to PID %u (%s) on port %u\n",
                               e.pid, e.process, e.local_port);
                    }
                    else
                    {
                        printf("[PORTS-SVC] Failed to kill PID %u: %s\n",
                               e.pid, strerror(errno));
                    }
#endif
                }
            }

            PortResponse resp = {};
            resp.magic = htonl(PORTS_MAGIC);
            resp.status = killed ? 0 : 1;
            resp.operation = op;
            resp.entry_count = 0;
            if (send_all(client_sock, &resp, sizeof(resp)) < 0)
                break;
            continue;
        }

        /* Send response header for LIST/GET operations */
        PortResponse resp = {};
        resp.magic = htonl(PORTS_MAGIC);
        resp.status = 0;
        resp.operation = op;
        resp.entry_count = htonl((uint32_t)entries.size());

        if (send_all(client_sock, &resp, sizeof(resp)) < 0)
        {
            DEBUG_PRINT("Failed to send response header");
            break;
        }

        /* Send entries */
        size_t sent = 0;
        for (auto &e : entries)
        {
            // Ensure strings are null-terminated
            e.process[sizeof(e.process) - 1] = '\0';
            e.cmdline[sizeof(e.cmdline) - 1] = '\0';

            if (send_all(client_sock, &e, sizeof(e)) < 0)
            {
                DEBUG_PRINT("Failed to send entry %zu", sent);
                goto client_done;
            }
            sent++;
        }

        printf("[PORTS-SVC] Sent %zu entries for op=0x%02X to %s\n",
               entries.size(), op, client_ip);
        continue;

    client_done:
        break;
    }

    printf("[PORTS-SVC] Client %s disconnected\n", client_ip);
}

void ports_service_run(void)
{
#ifdef _WIN32
    if (!ensure_winsock())
        return;

    SOCKET server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == INVALID_SOCKET)
    {
        fprintf(stderr, "[PORTS-SVC] socket() failed: %d\n", WSAGetLastError());
        return;
    }
#else
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0)
    {
        fprintf(stderr, "[PORTS-SVC] socket() failed: %s\n", strerror(errno));
        return;
    }

    // Ignore SIGPIPE on Unix
    signal(SIGPIPE, SIG_IGN);
#endif

    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR,
               (const char *)&reuse, sizeof(reuse));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORTS_SERVICE_PORT);

    if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        fprintf(stderr, "[PORTS-SVC] bind() failed on port %d: %s\n",
                PORTS_SERVICE_PORT,
#ifdef _WIN32
                strerror(WSAGetLastError())
#else
                strerror(errno)
#endif
        );
#ifdef _WIN32
        closesocket(server);
#else
        close(server);
#endif
        return;
    }

    if (listen(server, 10) < 0)
    {
        fprintf(stderr, "[PORTS-SVC] listen() failed: %s\n",
#ifdef _WIN32
                strerror(WSAGetLastError())
#else
                strerror(errno)
#endif
        );
#ifdef _WIN32
        closesocket(server);
#else
        close(server);
#endif
        return;
    }

    printf("[PORTS-SVC] ✓ Port service listening on TCP %d\n", PORTS_SERVICE_PORT);
    printf("[PORTS-SVC] Press Ctrl+C to stop\n");

    for (;;)
    {
        struct sockaddr_in ca = {};
#ifdef _WIN32
        int calen = sizeof(ca);
        SOCKET client = accept(server, (struct sockaddr *)&ca, &calen);
        if (client == INVALID_SOCKET)
        {
            if (WSAGetLastError() == WSAEINTR)
                continue;
            fprintf(stderr, "[PORTS-SVC] accept() failed: %d\n", WSAGetLastError());
            continue;
        }
#else
        socklen_t calen = sizeof(ca);
        int client = accept(server, (struct sockaddr *)&ca, &calen);
        if (client < 0)
        {
            if (errno == EINTR)
                continue;
            fprintf(stderr, "[PORTS-SVC] accept() failed: %s\n", strerror(errno));
            continue;
        }
#endif

        printf("[PORTS-SVC] Connection from %s\n", inet_ntoa(ca.sin_addr));

        // Handle client in a separate thread for concurrent connections
#ifdef _WIN32
        // On Windows, we'll handle synchronously for simplicity
        ports_service_handle_client((int)client);
        closesocket(client);
#else
        // On Unix, fork or thread (using thread here)
        std::thread client_thread([client]()
                                  {
            ports_service_handle_client(client);
            close(client); });
        client_thread.detach();
#endif
    }

#ifdef _WIN32
    closesocket(server);
    WSACleanup();
#else
    close(server);
#endif
}

/* ════════════════════════════════════════════════════════════════
 * SENDER-SIDE CLIENT API
 * ════════════════════════════════════════════════════════════════ */

static int do_request(ports_sock_t sock,
                      uint8_t op,
                      uint16_t target_port,
                      PortEntry **out_entries,
                      uint32_t *out_count)
{
    PortRequest req = {};
    req.magic = htonl(PORTS_MAGIC);
    req.version = PORTS_VERSION;
    req.operation = op;
    req.target_port = htons(target_port);

    if (send_all((int)sock, &req, sizeof(req)) < 0)
    {
        fprintf(stderr, "[PORTS] Failed to send request\n");
        return -1;
    }

    PortResponse resp = {};
    if (recv_all((int)sock, &resp, sizeof(resp)) < 0)
    {
        fprintf(stderr, "[PORTS] Failed to receive response\n");
        return -1;
    }

    if (ntohl(resp.magic) != PORTS_MAGIC)
    {
        fprintf(stderr, "[PORTS] Invalid magic in response\n");
        return -1;
    }

    if (resp.status != 0)
    {
        fprintf(stderr, "[PORTS] Server returned error status %d\n", resp.status);
        return -1;
    }

    uint32_t count = ntohl(resp.entry_count);
    DEBUG_PRINT("Received response with %u entries", count);

    if (out_entries && out_count)
    {
        *out_count = count;
        if (count > 0)
        {
            *out_entries = (PortEntry *)calloc(count, sizeof(PortEntry));
            if (!*out_entries)
            {
                fprintf(stderr, "[PORTS] Memory allocation failed\n");
                return -1;
            }

            for (uint32_t i = 0; i < count; i++)
            {
                if (recv_all((int)sock, &(*out_entries)[i], sizeof(PortEntry)) < 0)
                {
                    fprintf(stderr, "[PORTS] Failed to receive entry %u\n", i);
                    free(*out_entries);
                    *out_entries = nullptr;
                    *out_count = 0;
                    return -1;
                }

                // Ensure null termination
                (*out_entries)[i].process[sizeof((*out_entries)[i].process) - 1] = '\0';
                (*out_entries)[i].cmdline[sizeof((*out_entries)[i].cmdline) - 1] = '\0';
            }
        }
        else
        {
            *out_entries = nullptr;
        }
    }

    return (int)count;
}

ports_sock_t ports_remote_connect(const char *receiver_ip)
{
#ifdef _WIN32
    if (!ensure_winsock())
        return PORTS_INVALID_SOCK;

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET)
    {
        fprintf(stderr, "[PORTS] socket() failed: %d\n", WSAGetLastError());
        return PORTS_INVALID_SOCK;
    }
#else
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        fprintf(stderr, "[PORTS] socket() failed: %s\n", strerror(errno));
        return PORTS_INVALID_SOCK;
    }
#endif

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORTS_SERVICE_PORT);

    if (inet_pton(AF_INET, receiver_ip, &addr.sin_addr) <= 0)
    {
        fprintf(stderr, "[PORTS] Invalid IP address: %s\n", receiver_ip);
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return PORTS_INVALID_SOCK;
    }

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        fprintf(stderr, "[PORTS] Could not connect to %s:%d: %s\n",
                receiver_ip, PORTS_SERVICE_PORT,
#ifdef _WIN32
                strerror(WSAGetLastError())
#else
                strerror(errno)
#endif
        );
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return PORTS_INVALID_SOCK;
    }

    /* Ping handshake */
    PortRequest ping = {};
    ping.magic = htonl(PORTS_MAGIC);
    ping.version = PORTS_VERSION;
    ping.operation = PORTS_OP_PING;

    if (send_all((int)sock, &ping, sizeof(ping)) < 0)
    {
        fprintf(stderr, "[PORTS] Failed to send ping\n");
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return PORTS_INVALID_SOCK;
    }

    PortResponse pong = {};
    if (recv_all((int)sock, &pong, sizeof(pong)) < 0)
    {
        fprintf(stderr, "[PORTS] Failed to receive pong\n");
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return PORTS_INVALID_SOCK;
    }

    if (ntohl(pong.magic) != PORTS_MAGIC || pong.status != 0)
    {
        fprintf(stderr, "[PORTS] Ping failed: invalid response\n");
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return PORTS_INVALID_SOCK;
    }

    printf("[PORTS] ✓ Connected to %s:%d\n", receiver_ip, PORTS_SERVICE_PORT);
    return (ports_sock_t)sock;
}

int ports_remote_list_tcp(ports_sock_t sock,
                          PortEntry **entries, uint32_t *count)
{
    return do_request(sock, PORTS_OP_LIST_TCP, 0, entries, count);
}

int ports_remote_list_udp(ports_sock_t sock,
                          PortEntry **entries, uint32_t *count)
{
    return do_request(sock, PORTS_OP_LIST_UDP, 0, entries, count);
}

int ports_remote_list_all(ports_sock_t sock,
                          PortEntry **entries, uint32_t *count)
{
    return do_request(sock, PORTS_OP_LIST_ALL, 0, entries, count);
}

int ports_remote_get_port(ports_sock_t sock,
                          uint16_t port_num,
                          PortEntry **entries, uint32_t *count)
{
    return do_request(sock, PORTS_OP_GET_PORT, port_num, entries, count);
}

int ports_remote_kill_port(ports_sock_t sock, uint16_t port_num)
{
    int result = do_request(sock, PORTS_OP_KILL_PORT, port_num, nullptr, nullptr);
    return (result >= 0) ? 0 : -1;
}

void ports_free_entries(PortEntry *entries)
{
    if (entries)
    {
        free(entries);
    }
}

void ports_remote_disconnect(ports_sock_t sock)
{
    if (PORTS_SOCK_VALID(sock))
    {
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        printf("[PORTS] Disconnected from receiver port service\n");
    }
}

/* ════════════════════════════════════════════════════════════════
 * C++ HELPERS  –  pretty printing
 * ════════════════════════════════════════════════════════════════ */

std::string ports_state_name(uint8_t s)
{
    switch (s)
    {
    case PSTATE_ESTABLISHED:
        return "ESTABLISHED";
    case PSTATE_SYN_SENT:
        return "SYN_SENT";
    case PSTATE_SYN_RECV:
        return "SYN_RECV";
    case PSTATE_FIN_WAIT1:
        return "FIN_WAIT1";
    case PSTATE_FIN_WAIT2:
        return "FIN_WAIT2";
    case PSTATE_TIME_WAIT:
        return "TIME_WAIT";
    case PSTATE_CLOSE:
        return "CLOSE";
    case PSTATE_CLOSE_WAIT:
        return "CLOSE_WAIT";
    case PSTATE_LAST_ACK:
        return "LAST_ACK";
    case PSTATE_LISTEN:
        return "LISTEN";
    case PSTATE_CLOSING:
        return "CLOSING";
    default:
        return "UNKNOWN";
    }
}

std::string ports_proto_name(uint8_t proto)
{
    return (proto == 0) ? "TCP" : "UDP";
}

static std::string ip_to_str(const uint8_t *ip, uint8_t ver)
{
    char buf[INET6_ADDRSTRLEN] = {};
    if (ver == 6)
    {
        inet_ntop(AF_INET6, ip, buf, sizeof(buf));
    }
    else
    {
        inet_ntop(AF_INET, ip, buf, sizeof(buf));
    }
    return buf;
}

std::string ports_entry_to_string(const PortEntry &e)
{
    std::ostringstream ss;

    std::string local = ip_to_str(e.local_ip, e.ip_ver) + ":" + std::to_string(e.local_port);
    std::string remote;

    if (e.remote_port == 0)
    {
        remote = "*:*";
    }
    else
    {
        remote = ip_to_str(e.remote_ip, e.ip_ver) + ":" + std::to_string(e.remote_port);
    }

    ss << std::left
       << std::setw(4) << ports_proto_name(e.proto)
       << std::setw(25) << local
       << std::setw(25) << remote
       << std::setw(14) << ports_state_name(e.state)
       << " PID=" << std::setw(7) << e.pid
       << " " << e.process;

    return ss.str();
}

/* ════════════════════════════════════════════════════════════════
 * MAIN FUNCTION (if compiled with -DPORTS_MAIN)
 * ════════════════════════════════════════════════════════════════ */

#ifdef PORTS_MAIN
static void print_usage(const char *prog)
{
    fprintf(stderr, "Remote Port Inspection Tool\n");
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s server [-v]                 # Run as service\n", prog);
    fprintf(stderr, "  %s client <ip> [port] [-v]     # Query remote ports\n", prog);
    fprintf(stderr, "  %s kill <ip> <port> [-v]       # Kill process on port\n", prog);
    fprintf(stderr, "  %s get <ip> <port> [-v]        # Get info for specific port\n", prog);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -v    Enable verbose debug output\n");
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        print_usage(argv[0]);
        return 1;
    }

    // Check for verbose flag
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-v") == 0)
        {
            g_debug = true;
            // Remove the flag from arguments
            for (int j = i; j < argc - 1; j++)
            {
                argv[j] = argv[j + 1];
            }
            argc--;
            break;
        }
    }

    std::string cmd = argv[1];

    if (cmd == "server")
    {
        printf("Starting port inspection service on port %d...\n", PORTS_SERVICE_PORT);
        ports_service_run();
    }
    else if (cmd == "client" && argc >= 3)
    {
        const char *ip = argv[2];
        uint16_t port = (argc >= 4) ? (uint16_t)atoi(argv[3]) : 0;

        ports_sock_t sock = ports_remote_connect(ip);
        if (sock == PORTS_INVALID_SOCK)
        {
            fprintf(stderr, "Failed to connect to %s\n", ip);
            return 1;
        }

        PortEntry *entries = nullptr;
        uint32_t count = 0;
        int result;

        if (port > 0)
        {
            printf("Querying port %d on %s...\n", port, ip);
            result = ports_remote_get_port(sock, port, &entries, &count);
        }
        else
        {
            printf("Querying all ports on %s...\n", ip);
            result = ports_remote_list_all(sock, &entries, &count);
        }

        if (result >= 0)
        {
            printf("\nFound %u port(s):\n", count);
            printf("%-4s %-25s %-25s %-14s %s\n",
                   "PROTO", "LOCAL", "REMOTE", "STATE", "PROCESS");
            printf("%s\n", std::string(80, '-').c_str());

            for (uint32_t i = 0; i < count; i++)
            {
                std::cout << ports_entry_to_string(entries[i]) << std::endl;
            }
            ports_free_entries(entries);
        }
        else
        {
            fprintf(stderr, "Failed to query ports\n");
        }

        ports_remote_disconnect(sock);
    }
    else if (cmd == "kill" && argc >= 4)
    {
        const char *ip = argv[2];
        uint16_t port = (uint16_t)atoi(argv[3]);

        printf("Attempting to kill process on port %d at %s...\n", port, ip);

        ports_sock_t sock = ports_remote_connect(ip);
        if (sock == PORTS_INVALID_SOCK)
        {
            fprintf(stderr, "Failed to connect to %s\n", ip);
            return 1;
        }

        int result = ports_remote_kill_port(sock, port);
        if (result == 0)
        {
            printf("Kill command sent successfully\n");
        }
        else
        {
            fprintf(stderr, "Failed to kill process on port %d\n", port);
        }

        ports_remote_disconnect(sock);
    }
    else if (cmd == "get" && argc >= 4)
    {
        const char *ip = argv[2];
        uint16_t port = (uint16_t)atoi(argv[3]);

        ports_sock_t sock = ports_remote_connect(ip);
        if (sock == PORTS_INVALID_SOCK)
        {
            fprintf(stderr, "Failed to connect to %s\n", ip);
            return 1;
        }

        PortEntry *entries = nullptr;
        uint32_t count = 0;
        int result = ports_remote_get_port(sock, port, &entries, &count);

        if (result >= 0)
        {
            if (count > 0)
            {
                printf("\nPort %d information:\n", port);
                printf("%-4s %-25s %-25s %-14s %s\n",
                       "PROTO", "LOCAL", "REMOTE", "STATE", "PROCESS");
                printf("%s\n", std::string(80, '-').c_str());

                for (uint32_t i = 0; i < count; i++)
                {
                    std::cout << ports_entry_to_string(entries[i]) << std::endl;
                }
            }
            else
            {
                printf("No process found listening on port %d\n", port);
            }
            ports_free_entries(entries);
        }
        else
        {
            fprintf(stderr, "Failed to query port %d\n", port);
        }

        ports_remote_disconnect(sock);
    }
    else
    {
        print_usage(argv[0]);
        return 1;
    }

    return 0;
}
#endif