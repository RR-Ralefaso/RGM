/**
 * PORTS.H  –  REMOTE PORT INSPECTION API
 *
 * Lets the Sender query every listening TCP/UDP port on the Receiver,
 * including process name, PID, local/remote address, and state.
 *
 * Protocol (TCP port 8083):
 *   Sender  → Receiver : PortRequest  header
 *   Receiver → Sender  : PortResponse header  +  N × PortEntry records
 *
 * Supported operations
 *   PORTS_OP_PING         –  connectivity / handshake check
 *   PORTS_OP_LIST_TCP     –  list all TCP sockets
 *   PORTS_OP_LIST_UDP     –  list all UDP sockets
 *   PORTS_OP_LIST_ALL     –  list TCP + UDP combined
 *   PORTS_OP_GET_PORT     –  detail one specific port number
 *   PORTS_OP_KILL_PORT    –  request receiver to kill the process on a port
 *
 * Cross-platform:
 *   Linux   : /proc/net/tcp|tcp6|udp|udp6  + /proc/NNN/cmdline
 *   macOS   : getifaddrs / proc_pidinfo (if available) or lsof parse
 *   Windows : GetExtendedTcpTable / GetExtendedUdpTable (iphlpapi)
 */

#ifndef PORTS_H
#define PORTS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
#  include <string>
#  include <vector>
extern "C" {
#endif

/* ─── Socket type abstraction ─────────────────────────────────── */
#ifdef _WIN32
#  include <winsock2.h>
   typedef SOCKET ports_sock_t;
#  define PORTS_INVALID_SOCK  INVALID_SOCKET
#  define PORTS_SOCK_VALID(s) ((s) != INVALID_SOCKET)
#else
   typedef int ports_sock_t;
#  define PORTS_INVALID_SOCK  (-1)
#  define PORTS_SOCK_VALID(s) ((s) >= 0)
#endif

/* ─── Constants ───────────────────────────────────────────────── */
#define PORTS_SERVICE_PORT   8083
#define PORTS_MAGIC          0x504F5254u   /* "PORT" */
#define PORTS_VERSION        1u

#define PORTS_OP_PING        0xF0u
#define PORTS_OP_LIST_TCP    0x01u
#define PORTS_OP_LIST_UDP    0x02u
#define PORTS_OP_LIST_ALL    0x03u
#define PORTS_OP_GET_PORT    0x04u
#define PORTS_OP_KILL_PORT   0x05u

/* Socket states (mirrors Linux TCP states, mapped on other platforms) */
#define PSTATE_ESTABLISHED   1
#define PSTATE_SYN_SENT      2
#define PSTATE_SYN_RECV      3
#define PSTATE_FIN_WAIT1     4
#define PSTATE_FIN_WAIT2     5
#define PSTATE_TIME_WAIT     6
#define PSTATE_CLOSE         7
#define PSTATE_CLOSE_WAIT    8
#define PSTATE_LAST_ACK      9
#define PSTATE_LISTEN        10
#define PSTATE_CLOSING       11
#define PSTATE_UNKNOWN       0

/* ─── Wire structs (packed, no padding) ──────────────────────── */
#pragma pack(push, 1)

typedef struct {
    uint32_t magic;
    uint8_t  version;
    uint8_t  operation;
    uint16_t flags;
    uint16_t target_port;  /* for PORTS_OP_GET_PORT / KILL_PORT */
    uint16_t reserved;
    uint32_t reserved2;
} PortRequest;

typedef struct {
    uint32_t magic;
    uint8_t  status;        /* 0 = OK, non-zero = error */
    uint8_t  operation;
    uint16_t flags;
    uint32_t entry_count;   /* number of PortEntry records following */
    uint32_t reserved;
} PortResponse;

/* One socket/port entry sent over the wire */
typedef struct {
    uint8_t  proto;         /* 0=TCP 1=UDP */
    uint8_t  ip_ver;        /* 4 or 6 */
    uint8_t  state;         /* PSTATE_* */
    uint8_t  pad;
    uint16_t local_port;
    uint16_t remote_port;
    uint8_t  local_ip[16];  /* IPv4 stored in first 4 bytes */
    uint8_t  remote_ip[16];
    uint32_t pid;
    char     process[64];   /* null-terminated process name */
    char     cmdline[128];  /* null-terminated truncated cmdline */
} PortEntry;

#pragma pack(pop)

/* ─── Sender-side (client) API ────────────────────────────────── */

/** Connect to the receiver's port service. Returns socket or PORTS_INVALID_SOCK. */
ports_sock_t ports_remote_connect(const char *receiver_ip);

/** List all TCP ports on the receiver.
 *  Caller must free(*entries) with ports_free_entries(). Returns count or -1. */
int ports_remote_list_tcp(ports_sock_t sock,
                          PortEntry **entries, uint32_t *count);

/** List all UDP ports on the receiver. */
int ports_remote_list_udp(ports_sock_t sock,
                          PortEntry **entries, uint32_t *count);

/** List all ports (TCP + UDP). */
int ports_remote_list_all(ports_sock_t sock,
                          PortEntry **entries, uint32_t *count);

/** Query details for one specific port number. Returns 1 if found, 0 if not. */
int ports_remote_get_port(ports_sock_t sock,
                          uint16_t port_num,
                          PortEntry **entries, uint32_t *count);

/** Ask the receiver to kill the process listening on a given port.
 *  Returns 0 on success, -1 on error/denial. */
int ports_remote_kill_port(ports_sock_t sock, uint16_t port_num);

/** Free entries array returned by the above calls. */
void ports_free_entries(PortEntry *entries);

/** Close the port-service connection. */
void ports_remote_disconnect(ports_sock_t sock);

/* ─── Receiver-side (server) API ─────────────────────────────── */

/** Handle one connected client (blocking until disconnect). */
void ports_service_handle_client(int client_sock);

/** Start the port service listener (blocking, run in a thread). */
void ports_service_run(void);

#ifdef __cplusplus
}   /* extern "C" */

/* ─── C++ helper: pretty-print a PortEntry ────────────────────── */
#include <string>
std::string ports_entry_to_string(const PortEntry &e);
std::string ports_state_name(uint8_t state);
std::string ports_proto_name(uint8_t proto);

#endif  /* __cplusplus */

#endif  /* PORTS_H */
