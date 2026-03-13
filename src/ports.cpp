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
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <iphlpapi.h>
#  include <tlhelp32.h>
#  pragma comment(lib, "iphlpapi.lib")
#  pragma comment(lib, "ws2_32.lib")
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <signal.h>
#endif

#ifdef __APPLE__
#  include <sys/proc_info.h>
#  include <libproc.h>
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

/* ─── I/O helpers ─────────────────────────────────────────────── */
static int send_all(int sock, const void *buf, size_t size)
{
    const char *p = (const char *)buf;
    size_t sent = 0;
    while (sent < size) {
        int n = (int)send(sock, p + sent, (int)(size - sent), 0);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

static int recv_all(int sock, void *buf, size_t size)
{
    char *p = (char *)buf;
    size_t got = 0;
    while (got < size) {
        int n = (int)recv(sock, p + got, (int)(size - got), 0);
        if (n <= 0) return -1;
        got += (size_t)n;
    }
    return 0;
}

/* ════════════════════════════════════════════════════════════════
 * PLATFORM: COLLECT PORT ENTRIES
 * ════════════════════════════════════════════════════════════════ */

#ifdef _WIN32
/* ── Windows ───────────────────────────────────────────────────── */

static std::string win_process_name(DWORD pid)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return "?";
    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);
    std::string name = "?";
    if (Process32First(snap, &pe)) {
        do {
            if (pe.th32ProcessID == pid) {
                name = pe.szExeFile;
                break;
            }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return name;
}

static uint8_t win_tcp_state(DWORD s)
{
    switch (s) {
        case MIB_TCP_STATE_CLOSED:     return PSTATE_CLOSE;
        case MIB_TCP_STATE_LISTEN:     return PSTATE_LISTEN;
        case MIB_TCP_STATE_SYN_SENT:   return PSTATE_SYN_SENT;
        case MIB_TCP_STATE_SYN_RCVD:   return PSTATE_SYN_RECV;
        case MIB_TCP_STATE_ESTAB:      return PSTATE_ESTABLISHED;
        case MIB_TCP_STATE_FIN_WAIT1:  return PSTATE_FIN_WAIT1;
        case MIB_TCP_STATE_FIN_WAIT2:  return PSTATE_FIN_WAIT2;
        case MIB_TCP_STATE_CLOSE_WAIT: return PSTATE_CLOSE_WAIT;
        case MIB_TCP_STATE_CLOSING:    return PSTATE_CLOSING;
        case MIB_TCP_STATE_LAST_ACK:   return PSTATE_LAST_ACK;
        case MIB_TCP_STATE_TIME_WAIT:  return PSTATE_TIME_WAIT;
        default:                       return PSTATE_UNKNOWN;
    }
}

static std::vector<PortEntry> collect_ports_windows(uint8_t proto_filter)
{
    std::vector<PortEntry> out;

    /* ── TCP ── */
    if (proto_filter == 0 /* TCP */ || proto_filter == 2 /* ALL */) {
        ULONG sz = 0;
        GetExtendedTcpTable(nullptr, &sz, FALSE, AF_INET,
                            TCP_TABLE_OWNER_PID_ALL, 0);
        std::vector<BYTE> buf(sz);
        if (GetExtendedTcpTable(buf.data(), &sz, FALSE, AF_INET,
                                TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR)
        {
            auto *tbl = (MIB_TCPTABLE_OWNER_PID *)buf.data();
            for (DWORD i = 0; i < tbl->dwNumEntries; i++) {
                auto &row = tbl->table[i];
                PortEntry e = {};
                e.proto      = 0;
                e.ip_ver     = 4;
                e.state      = win_tcp_state(row.dwState);
                e.local_port = ntohs((uint16_t)row.dwLocalPort);
                e.remote_port= ntohs((uint16_t)row.dwRemotePort);
                memcpy(e.local_ip,  &row.dwLocalAddr,  4);
                memcpy(e.remote_ip, &row.dwRemoteAddr, 4);
                e.pid        = row.dwOwningPid;
                auto name = win_process_name(row.dwOwningPid);
                strncpy(e.process, name.c_str(), sizeof(e.process)-1);
                out.push_back(e);
            }
        }

        /* IPv6 TCP */
        sz = 0;
        GetExtendedTcpTable(nullptr, &sz, FALSE, AF_INET6,
                            TCP_TABLE_OWNER_PID_ALL, 0);
        buf.resize(sz);
        if (sz > 0 && GetExtendedTcpTable(buf.data(), &sz, FALSE, AF_INET6,
                                TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR)
        {
            auto *tbl = (MIB_TCP6TABLE_OWNER_PID *)buf.data();
            for (DWORD i = 0; i < tbl->dwNumEntries; i++) {
                auto &row = tbl->table[i];
                PortEntry e = {};
                e.proto      = 0;
                e.ip_ver     = 6;
                e.state      = win_tcp_state(row.State);
                e.local_port = ntohs((uint16_t)row.dwLocalPort);
                e.remote_port= ntohs((uint16_t)row.dwRemotePort);
                memcpy(e.local_ip,  row.ucLocalAddr,  16);
                memcpy(e.remote_ip, row.ucRemoteAddr, 16);
                e.pid        = row.dwOwningPid;
                auto name = win_process_name(row.dwOwningPid);
                strncpy(e.process, name.c_str(), sizeof(e.process)-1);
                out.push_back(e);
            }
        }
    }

    /* ── UDP ── */
    if (proto_filter == 1 /* UDP */ || proto_filter == 2 /* ALL */) {
        ULONG sz = 0;
        GetExtendedUdpTable(nullptr, &sz, FALSE, AF_INET,
                            UDP_TABLE_OWNER_PID, 0);
        std::vector<BYTE> buf(sz);
        if (GetExtendedUdpTable(buf.data(), &sz, FALSE, AF_INET,
                                UDP_TABLE_OWNER_PID, 0) == NO_ERROR)
        {
            auto *tbl = (MIB_UDPTABLE_OWNER_PID *)buf.data();
            for (DWORD i = 0; i < tbl->dwNumEntries; i++) {
                auto &row = tbl->table[i];
                PortEntry e = {};
                e.proto      = 1;
                e.ip_ver     = 4;
                e.state      = PSTATE_UNKNOWN; /* UDP has no state */
                e.local_port = ntohs((uint16_t)row.dwLocalPort);
                memcpy(e.local_ip, &row.dwLocalAddr, 4);
                e.pid        = row.dwOwningPid;
                auto name = win_process_name(row.dwOwningPid);
                strncpy(e.process, name.c_str(), sizeof(e.process)-1);
                out.push_back(e);
            }
        }
    }

    return out;
}

#elif defined(__linux__)
/* ── Linux  (/proc/net) ────────────────────────────────────────── */

static std::string linux_cmdline(uint32_t pid)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%u/cmdline", pid);
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return "?";
    std::string cmd;
    std::getline(f, cmd, '\0');
    /* strip path, keep basename */
    auto pos = cmd.rfind('/');
    if (pos != std::string::npos) cmd = cmd.substr(pos + 1);
    if (cmd.size() > 62) cmd.resize(62);
    return cmd.empty() ? "?" : cmd;
}

/* Convert 8-hex-char little-endian IPv4 to bytes */
static void parse_hex_ipv4(const std::string &s, uint8_t out[16])
{
    uint32_t n = (uint32_t)strtoul(s.c_str(), nullptr, 16);
    /* /proc/net stores in host byte order on LE machines → already LE */
    out[0] = (uint8_t)(n        & 0xFF);
    out[1] = (uint8_t)((n >> 8) & 0xFF);
    out[2] = (uint8_t)((n >>16) & 0xFF);
    out[3] = (uint8_t)((n >>24) & 0xFF);
}

static void parse_hex_ipv6(const std::string &s, uint8_t out[16])
{
    for (int i = 0; i < 4; i++) {
        uint32_t n = (uint32_t)strtoul(s.substr(i*8, 8).c_str(), nullptr, 16);
        out[i*4+0] = (uint8_t)(n        & 0xFF);
        out[i*4+1] = (uint8_t)((n >> 8) & 0xFF);
        out[i*4+2] = (uint8_t)((n >>16) & 0xFF);
        out[i*4+3] = (uint8_t)((n >>24) & 0xFF);
    }
}

struct PidInode { uint32_t pid; uint64_t inode; };

/* Build a map inode-to-pid by scanning proc-pid-fd */
static std::vector<PidInode> build_inode_map()
{
    std::vector<PidInode> map;
    /* iterate /proc/1..65535 */
    for (int pid = 1; pid < 65536; pid++) {
        char fd_dir[32];
        snprintf(fd_dir, sizeof(fd_dir), "/proc/%d/fd", pid);
        /* opendir is expensive but correct; use a quick stat shortcut */
        char path[64];
        /* just check a few fds */
        for (int fd = 0; fd < 256; fd++) {
            snprintf(path, sizeof(path), "/proc/%d/fd/%d", pid, fd);
            char lnk[128] = {};
            ssize_t n = readlink(path, lnk, sizeof(lnk) - 1);
            if (n < 0) continue;
            lnk[n] = '\0';
            /* format: "socket:[12345]" */
            if (strncmp(lnk, "socket:[", 8) == 0) {
                uint64_t inode = (uint64_t)strtoull(lnk + 8, nullptr, 10);
                map.push_back({(uint32_t)pid, inode});
            }
        }
    }
    return map;
}

static uint32_t inode_to_pid(const std::vector<PidInode> &map, uint64_t inode)
{
    for (auto &e : map)
        if (e.inode == inode) return e.pid;
    return 0;
}

static std::vector<PortEntry> parse_proc_net(const char *path,
                                             uint8_t proto,
                                             bool is_v6,
                                             const std::vector<PidInode> &imap)
{
    std::vector<PortEntry> out;
    std::ifstream f(path);
    if (!f.is_open()) return out;

    std::string line;
    std::getline(f, line); /* skip header */

    while (std::getline(f, line)) {
        /* fields: sl  local_addr  rem_addr  state  ... inode */
        std::istringstream ss(line);
        std::string sl, local, remote, state_hex, drop;
        ss >> sl >> local >> remote >> state_hex;
        /* skip 4 fields (tx_queue, rx_queue, tr, tm->when) */
        for (int i = 0; i < 4; i++) ss >> drop;
        std::string inode_str;
        ss >> drop >> drop >> inode_str; /* uid retransmit inode */

        if (local.empty()) continue;

        /* parse local_addr = HHHHHHHH:PPPP */
        auto colon = local.find(':');
        if (colon == std::string::npos) continue;
        std::string ip_s   = local.substr(0, colon);
        std::string port_s = local.substr(colon + 1);

        auto rcolon = remote.find(':');
        std::string rip_s   = (rcolon != std::string::npos) ? remote.substr(0, rcolon) : "";
        std::string rport_s = (rcolon != std::string::npos) ? remote.substr(rcolon + 1) : "0";

        PortEntry e = {};
        e.proto  = proto;
        e.ip_ver = is_v6 ? 6 : 4;

        uint8_t state_u = (uint8_t)strtoul(state_hex.c_str(), nullptr, 16);
        e.state  = state_u;   /* Linux states match our PSTATE_* values */

        e.local_port  = (uint16_t)strtoul(port_s.c_str(),  nullptr, 16);
        e.remote_port = (uint16_t)strtoul(rport_s.c_str(), nullptr, 16);

        if (is_v6) {
            if (ip_s.size() == 32)  parse_hex_ipv6(ip_s,  e.local_ip);
            if (rip_s.size() == 32) parse_hex_ipv6(rip_s, e.remote_ip);
        } else {
            if (ip_s.size() == 8)   parse_hex_ipv4(ip_s,  e.local_ip);
            if (rip_s.size() == 8)  parse_hex_ipv4(rip_s, e.remote_ip);
        }

        uint64_t inode = (uint64_t)strtoull(inode_str.c_str(), nullptr, 10);
        e.pid = inode_to_pid(imap, inode);
        if (e.pid > 0) {
            auto cmd = linux_cmdline(e.pid);
            strncpy(e.process, cmd.c_str(), sizeof(e.process) - 1);
            /* full cmdline */
            char cpath[64];
            snprintf(cpath, sizeof(cpath), "/proc/%u/cmdline", e.pid);
            std::ifstream cf(cpath, std::ios::binary);
            if (cf.is_open()) {
                std::string full;
                std::getline(cf, full, '\0');
                if (full.size() > sizeof(e.cmdline) - 1)
                    full.resize(sizeof(e.cmdline) - 1);
                strncpy(e.cmdline, full.c_str(), sizeof(e.cmdline) - 1);
            }
        }
        out.push_back(e);
    }
    return out;
}

static std::vector<PortEntry> collect_ports_linux(uint8_t proto_filter)
{
    std::vector<PortEntry> out;
    auto imap = build_inode_map();

    if (proto_filter == 0 || proto_filter == 2) {
        auto t4 = parse_proc_net("/proc/net/tcp",  0, false, imap);
        auto t6 = parse_proc_net("/proc/net/tcp6", 0, true,  imap);
        out.insert(out.end(), t4.begin(), t4.end());
        out.insert(out.end(), t6.begin(), t6.end());
    }
    if (proto_filter == 1 || proto_filter == 2) {
        auto u4 = parse_proc_net("/proc/net/udp",  1, false, imap);
        auto u6 = parse_proc_net("/proc/net/udp6", 1, true,  imap);
        out.insert(out.end(), u4.begin(), u4.end());
        out.insert(out.end(), u6.begin(), u6.end());
    }
    return out;
}

#elif defined(__APPLE__)
/* ── macOS (lsof parse) ────────────────────────────────────────── */

static std::vector<PortEntry> collect_ports_macos(uint8_t proto_filter)
{
    std::vector<PortEntry> out;

    /* Build lsof command */
    std::string cmd = "lsof -nP ";
    if (proto_filter == 0)      cmd += "-iTCP";
    else if (proto_filter == 1) cmd += "-iUDP";
    else                        cmd += "-iTCP -iUDP";
    cmd += " 2>/dev/null";

    FILE *fp = popen(cmd.c_str(), "r");
    if (!fp) return out;

    char line[512];
    /* Skip header */
    fgets(line, sizeof(line), fp);

    while (fgets(line, sizeof(line), fp)) {
        /* lsof columns: COMMAND  PID  USER  FD  TYPE  DEVICE  SIZE/OFF  NODE  NAME */
        char cmd_s[64]={}, pid_s[16]={}, user[32]={}, fd_s[16]={},
             type_s[16]={}, dev[32]={}, size[32]={}, node[32]={}, name[256]={};
        int n = sscanf(line, "%63s %15s %31s %15s %15s %31s %31s %31s %255s",
                       cmd_s, pid_s, user, fd_s, type_s, dev, size, node, name);
        if (n < 9) continue;

        /* name format: *:PORT, addr:PORT->addr:PORT, [::]:PORT, etc */
        std::string ns(name);
        uint8_t proto = (strncmp(type_s, "IPv6", 4) == 0 ||
                         strncmp(type_s, "IP", 2) == 0) ? 0 : 0;
        if (strstr(cmd_s, "UDP") || strstr(ns.c_str(), "UDP")) proto = 1;

        /* Look for TCP/UDP in FD field */
        /* Determine proto from node field */
        if (strstr(node, "TCP") || strstr(fd_s, "TCP")) proto = 0;
        else if (strstr(node, "UDP") || strstr(fd_s, "UDP")) proto = 1;

        /* Extract local port */
        auto arrow = ns.find("->");
        std::string local_part = (arrow != std::string::npos) ? ns.substr(0, arrow) : ns;
        auto lcolon = local_part.rfind(':');
        if (lcolon == std::string::npos) continue;
        std::string lport_s = local_part.substr(lcolon + 1);
        uint16_t lport = 0;
        try { lport = (uint16_t)std::stoi(lport_s); } catch (...) { continue; }

        /* Extract remote port */
        uint16_t rport = 0;
        if (arrow != std::string::npos) {
            std::string remote_part = ns.substr(arrow + 2);
            auto rcolon = remote_part.rfind(':');
            if (rcolon != std::string::npos) {
                try { rport = (uint16_t)std::stoi(remote_part.substr(rcolon+1)); }
                catch (...) {}
            }
        }

        PortEntry e = {};
        e.proto      = proto;
        e.ip_ver     = (strstr(name, ":") && strstr(name, "[")) ? 6 : 4;
        e.local_port = lport;
        e.remote_port= rport;
        e.state      = PSTATE_LISTEN; /* default */
        if (strstr(name, "(ESTABLISHED)")) e.state = PSTATE_ESTABLISHED;
        else if (strstr(name, "(LISTEN)"))  e.state = PSTATE_LISTEN;
        try { e.pid = (uint32_t)std::stoi(pid_s); } catch (...) {}
        strncpy(e.process, cmd_s, sizeof(e.process) - 1);
        out.push_back(e);
    }

    pclose(fp);
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
    return {};  /* unsupported platform */
#endif
}

/* ════════════════════════════════════════════════════════════════
 * RECEIVER-SIDE SERVER
 * ════════════════════════════════════════════════════════════════ */

void ports_service_handle_client(int client_sock)
{
    printf("[PORTS-SVC] Client connected\n");

    for (;;) {
        PortRequest req;
        if (recv_all(client_sock, &req, sizeof(req)) < 0) break;
        if (ntohl(req.magic) != PORTS_MAGIC) {
            fprintf(stderr, "[PORTS-SVC] Bad magic\n");
            break;
        }

        uint8_t op = req.operation;

        /* PING */
        if (op == PORTS_OP_PING) {
            PortResponse resp = {};
            resp.magic     = htonl(PORTS_MAGIC);
            resp.status    = 0;
            resp.operation = PORTS_OP_PING;
            send_all(client_sock, &resp, sizeof(resp));
            printf("[PORTS-SVC] Ping OK\n");
            continue;
        }

        /* Determine what to collect */
        uint8_t proto_filter = 2; /* ALL */
        if (op == PORTS_OP_LIST_TCP) proto_filter = 0;
        else if (op == PORTS_OP_LIST_UDP) proto_filter = 1;

        auto entries = collect_ports(proto_filter);

        /* Filter by specific port if requested */
        if (op == PORTS_OP_GET_PORT || op == PORTS_OP_KILL_PORT) {
            uint16_t target = ntohs(req.target_port);
            std::vector<PortEntry> filtered;
            for (auto &e : entries)
                if (e.local_port == target)
                    filtered.push_back(e);
            entries = std::move(filtered);
        }

        /* KILL: send SIGTERM to each process on that port */
        if (op == PORTS_OP_KILL_PORT) {
            bool killed = false;
            for (auto &e : entries) {
                if (e.pid > 0) {
#ifdef _WIN32
                    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, e.pid);
                    if (h) { TerminateProcess(h, 0); CloseHandle(h); killed = true; }
#else
                    kill((pid_t)e.pid, SIGTERM);
                    killed = true;
#endif
                    printf("[PORTS-SVC] Sent SIGTERM to PID %u (%s) on port %u\n",
                           e.pid, e.process, e.local_port);
                }
            }
            PortResponse resp = {};
            resp.magic     = htonl(PORTS_MAGIC);
            resp.status    = killed ? 0 : 1;
            resp.operation = op;
            resp.entry_count = 0;
            send_all(client_sock, &resp, sizeof(resp));
            continue;
        }

        /* Send response header */
        PortResponse resp = {};
        resp.magic       = htonl(PORTS_MAGIC);
        resp.status      = 0;
        resp.operation   = op;
        resp.entry_count = htonl((uint32_t)entries.size());
        if (send_all(client_sock, &resp, sizeof(resp)) < 0) break;

        /* Send entries */
        for (auto &e : entries) {
            if (send_all(client_sock, &e, sizeof(e)) < 0) {
                goto client_done;
            }
        }
        printf("[PORTS-SVC] Sent %zu entries for op=0x%02X\n", entries.size(), op);
        continue;
    client_done:
        break;
    }

    printf("[PORTS-SVC] Client disconnected\n");
}

void ports_service_run(void)
{
#ifdef _WIN32
    SOCKET server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == INVALID_SOCKET) {
        fprintf(stderr, "[PORTS-SVC] socket() failed\n"); return;
    }
#else
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        fprintf(stderr, "[PORTS-SVC] socket() failed\n"); return;
    }
#endif

    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR,
               (char *)&reuse, sizeof(reuse));

    struct sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(PORTS_SERVICE_PORT);

    if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[PORTS-SVC] bind() failed on port %d\n",
                PORTS_SERVICE_PORT);
#ifdef _WIN32
        closesocket(server);
#else
        close(server);
#endif
        return;
    }

    listen(server, 4);
    printf("[PORTS-SVC] ✓ Port service listening on TCP %d\n",
           PORTS_SERVICE_PORT);

    for (;;) {
        struct sockaddr_in ca = {};
#ifdef _WIN32
        int calen = sizeof(ca);
        SOCKET client = accept(server, (struct sockaddr *)&ca, &calen);
        if (client == INVALID_SOCKET) continue;
#else
        socklen_t calen = sizeof(ca);
        int client = accept(server, (struct sockaddr *)&ca, &calen);
        if (client < 0) continue;
#endif
        printf("[PORTS-SVC] Connection from %s\n", inet_ntoa(ca.sin_addr));
        ports_service_handle_client((int)client);
#ifdef _WIN32
        closesocket(client);
#else
        close(client);
#endif
    }
#ifdef _WIN32
    closesocket(server);
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
    req.magic       = htonl(PORTS_MAGIC);
    req.version     = PORTS_VERSION;
    req.operation   = op;
    req.target_port = htons(target_port);

    if (send_all((int)sock, &req, sizeof(req)) < 0) return -1;

    PortResponse resp = {};
    if (recv_all((int)sock, &resp, sizeof(resp)) < 0) return -1;
    if (ntohl(resp.magic) != PORTS_MAGIC || resp.status != 0) return -1;

    uint32_t count = ntohl(resp.entry_count);
    if (out_entries && out_count) {
        *out_count = count;
        if (count > 0) {
            *out_entries = (PortEntry *)calloc(count, sizeof(PortEntry));
            if (!*out_entries) return -1;
            for (uint32_t i = 0; i < count; i++) {
                if (recv_all((int)sock, &(*out_entries)[i], sizeof(PortEntry)) < 0) {
                    free(*out_entries);
                    *out_entries = nullptr;
                    *out_count   = 0;
                    return -1;
                }
            }
        } else {
            *out_entries = nullptr;
        }
    }
    return (int)count;
}

ports_sock_t ports_remote_connect(const char *receiver_ip)
{
#ifdef _WIN32
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return PORTS_INVALID_SOCK;
#else
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return PORTS_INVALID_SOCK;
#endif

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(PORTS_SERVICE_PORT);
    inet_pton(AF_INET, receiver_ip, &addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        fprintf(stderr, "[PORTS] Could not connect to %s:%d\n",
                receiver_ip, PORTS_SERVICE_PORT);
        return PORTS_INVALID_SOCK;
    }

    /* Ping handshake */
    PortRequest ping = {};
    ping.magic     = htonl(PORTS_MAGIC);
    ping.version   = PORTS_VERSION;
    ping.operation = PORTS_OP_PING;
    if (send_all((int)sock, &ping, sizeof(ping)) < 0) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return PORTS_INVALID_SOCK;
    }
    PortResponse pong = {};
    if (recv_all((int)sock, &pong, sizeof(pong)) < 0 || pong.status != 0) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        fprintf(stderr, "[PORTS] Ping failed\n");
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
    return do_request(sock, PORTS_OP_KILL_PORT, port_num, nullptr, nullptr);
}

void ports_free_entries(PortEntry *entries)
{
    free(entries);
}

void ports_remote_disconnect(ports_sock_t sock)
{
    if (PORTS_SOCK_VALID(sock)) {
#ifdef _WIN32
        closesocket(sock);
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
    switch (s) {
        case PSTATE_ESTABLISHED: return "ESTABLISHED";
        case PSTATE_SYN_SENT:    return "SYN_SENT";
        case PSTATE_SYN_RECV:    return "SYN_RECV";
        case PSTATE_FIN_WAIT1:   return "FIN_WAIT1";
        case PSTATE_FIN_WAIT2:   return "FIN_WAIT2";
        case PSTATE_TIME_WAIT:   return "TIME_WAIT";
        case PSTATE_CLOSE:       return "CLOSE";
        case PSTATE_CLOSE_WAIT:  return "CLOSE_WAIT";
        case PSTATE_LAST_ACK:    return "LAST_ACK";
        case PSTATE_LISTEN:      return "LISTEN";
        case PSTATE_CLOSING:     return "CLOSING";
        default:                 return "UNKNOWN";
    }
}

std::string ports_proto_name(uint8_t proto)
{
    return (proto == 0) ? "TCP" : "UDP";
}

static std::string ip_to_str(const uint8_t *ip, uint8_t ver)
{
    if (ver == 6) {
        char buf[INET6_ADDRSTRLEN] = {};
        inet_ntop(AF_INET6, ip, buf, sizeof(buf));
        return buf;
    }
    char buf[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, ip, buf, sizeof(buf));
    return buf;
}

std::string ports_entry_to_string(const PortEntry &e)
{
    std::ostringstream ss;
    ss << std::left
       << std::setw(4)  << ports_proto_name(e.proto)
       << std::setw(22) << (ip_to_str(e.local_ip, e.ip_ver) + ":" + std::to_string(e.local_port))
       << std::setw(22) << (e.remote_port ? ip_to_str(e.remote_ip, e.ip_ver) + ":" + std::to_string(e.remote_port) : "*:*")
       << std::setw(14) << ports_state_name(e.state)
       << "PID=" << std::setw(7) << e.pid
       << e.process;
    return ss.str();
}



std::string ports_ip_to_string(const uint8_t *ip, uint8_t ver)
{
    char buf[INET6_ADDRSTRLEN] = {0};
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
