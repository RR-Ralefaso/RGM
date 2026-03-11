/**
 * GPU_ACCELERATE.C  –  REMOTE COMPUTE OFFLOAD ENGINE  (v2)
 *
 * Real measured timing (not hardcoded), real CPU work on receiver.
 */

#include "gpu_accelerate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <time.h>
#endif

/* Wire constants */
#define GPU_MAGIC        0x47505541u
#define GPU_VERSION      1
#define GPU_OP_PING      0xFF
#define GPU_OP_COMPRESS  0x01
#define GPU_OP_COLORCONV 0x03

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint8_t  version;
    uint8_t  operation;
    uint16_t flags;
    uint32_t width;
    uint32_t height;
    uint32_t data_size;
} GpuRequest;

typedef struct {
    uint32_t magic;
    uint8_t  status;
    uint8_t  operation;
    uint16_t flags;
    uint32_t result_size;
    float    ms_elapsed;
} GpuResponse;
#pragma pack(pop)

/* ─── Real monotonic timer (ms) ─────────────────────────────── */
static double now_ms(void)
{
#ifdef _WIN32
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    return (double)cnt.QuadPart * 1000.0 / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
#endif
}

/* ─── I/O helpers ────────────────────────────────────────────── */
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

/* ─── BGR → RGB swap ─────────────────────────────────────────── */
static void fix_colour_channels(uint8_t *pixels, uint32_t width, uint32_t height)
{
    uint32_t total = width * height;
    uint32_t i;
    for (i = 0; i < total; i++) {
        uint8_t tmp       = pixels[i * 3];
        pixels[i * 3]     = pixels[i * 3 + 2];
        pixels[i * 3 + 2] = tmp;
    }
}

/* ─── RLE compressor ─────────────────────────────────────────── */
static uint32_t rle_compress(const uint8_t *src, uint32_t src_len,
                              uint8_t *dst,       uint32_t dst_cap)
{
    uint32_t in = 0, out = 0;
    while (in + 2 < src_len) {
        if (out + 4 > dst_cap) break;
        uint8_t r   = src[in];
        uint8_t g   = src[in + 1];
        uint8_t b   = src[in + 2];
        uint8_t run = 1;
        while (run < 255
               && in + (uint32_t)run * 3 + 2 < src_len
               && src[in + run*3]     == r
               && src[in + run*3 + 1] == g
               && src[in + run*3 + 2] == b)
        { run++; }
        dst[out++] = run;
        dst[out++] = r;
        dst[out++] = g;
        dst[out++] = b;
        in += (uint32_t)run * 3;
    }
    return out;
}

/* ═══════════════════════════════════════════════════════════════
 * SENDER-SIDE CLIENT
 * ═══════════════════════════════════════════════════════════════ */

gpu_sock_t gpu_remote_connect(const char *receiver_ip)
{
#ifdef _WIN32
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return (gpu_sock_t)(-1);
#else
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;
#endif
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(GPU_ACCEL_PORT);
    inet_pton(AF_INET, receiver_ip, &addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        fprintf(stderr, "[GPU] Cannot connect to %s:%d\n", receiver_ip, GPU_ACCEL_PORT);
        return (gpu_sock_t)(-1);
    }

    GpuRequest ping;
    memset(&ping, 0, sizeof(ping));
    ping.magic     = htonl(GPU_MAGIC);
    ping.version   = GPU_VERSION;
    ping.operation = GPU_OP_PING;
    if (send_all((int)sock, &ping, sizeof(ping)) < 0) {
#ifdef _WIN32
        closesocket(sock); return (gpu_sock_t)(-1);
#else
        close(sock); return -1;
#endif
    }
    GpuResponse pong;
    if (recv_all((int)sock, &pong, sizeof(pong)) < 0 || pong.status != 0) {
#ifdef _WIN32
        closesocket(sock); return (gpu_sock_t)(-1);
#else
        close(sock); return -1;
#endif
    }
    printf("[GPU] Connected to receiver compute service at %s:%d\n",
           receiver_ip, GPU_ACCEL_PORT);
    return (gpu_sock_t)sock;
}

int gpu_remote_compress(gpu_sock_t gpu_sock,
                        const uint8_t *pixels,
                        uint32_t width, uint32_t height,
                        uint8_t **out_buf)
{
    uint32_t data_size = width * height * 3;
    GpuRequest req;
    memset(&req, 0, sizeof(req));
    req.magic     = htonl(GPU_MAGIC);
    req.version   = GPU_VERSION;
    req.operation = GPU_OP_COMPRESS;
    req.width     = htonl(width);
    req.height    = htonl(height);
    req.data_size = htonl(data_size);

    if (send_all((int)gpu_sock, &req, sizeof(req)) < 0)  return -1;
    if (send_all((int)gpu_sock, pixels, data_size) < 0)  return -1;

    GpuResponse resp;
    if (recv_all((int)gpu_sock, &resp, sizeof(resp)) < 0) return -1;
    if (resp.status != 0) return -1;

    uint32_t result_size = ntohl(resp.result_size);
    if (result_size == 0) return -1;

    *out_buf = (uint8_t *)malloc(result_size);
    if (!*out_buf) return -1;
    if (recv_all((int)gpu_sock, *out_buf, result_size) < 0) {
        free(*out_buf); *out_buf = NULL; return -1;
    }

    printf("[GPU] Compress: %u → %u bytes (%.1f%%) in %.2f ms [receiver CPU]\n",
           data_size, result_size,
           (data_size > 0) ? 100.0f * (float)result_size / (float)data_size : 0.0f,
           resp.ms_elapsed);
    return (int)result_size;
}

int gpu_remote_colorfix(gpu_sock_t gpu_sock,
                        uint8_t *pixels,
                        uint32_t width, uint32_t height)
{
    uint32_t data_size = width * height * 3;
    GpuRequest req;
    memset(&req, 0, sizeof(req));
    req.magic     = htonl(GPU_MAGIC);
    req.version   = GPU_VERSION;
    req.operation = GPU_OP_COLORCONV;
    req.width     = htonl(width);
    req.height    = htonl(height);
    req.data_size = htonl(data_size);

    if (send_all((int)gpu_sock, &req, sizeof(req)) < 0) return -1;
    if (send_all((int)gpu_sock, pixels, data_size) < 0) return -1;

    GpuResponse resp;
    if (recv_all((int)gpu_sock, &resp, sizeof(resp)) < 0) return -1;
    if (resp.status != 0) return -1;

    uint32_t result_size = ntohl(resp.result_size);
    if (result_size != data_size) return -1;
    if (recv_all((int)gpu_sock, pixels, data_size) < 0) return -1;

    printf("[GPU] Colorfix %ux%u in %.2f ms [receiver CPU]\n",
           width, height, resp.ms_elapsed);
    return 0;
}

void gpu_remote_disconnect(gpu_sock_t gpu_sock)
{
    if (GPU_SOCK_VALID(gpu_sock)) {
#ifdef _WIN32
        closesocket((SOCKET)gpu_sock);
#else
        close((int)gpu_sock);
#endif
        printf("[GPU] Disconnected from receiver compute service\n");
    }
}

/* ═══════════════════════════════════════════════════════════════
 * RECEIVER-SIDE SERVER
 * ═══════════════════════════════════════════════════════════════ */

void gpu_service_handle_client(int client_sock)
{
    printf("[GPU-SVC] Client connected\n");
    for (;;) {
        GpuRequest req;
        if (recv_all(client_sock, &req, sizeof(req)) < 0) break;
        if (ntohl(req.magic) != GPU_MAGIC) {
            fprintf(stderr, "[GPU-SVC] Bad magic\n"); break;
        }

        uint8_t  op      = req.operation;
        uint32_t width   = ntohl(req.width);
        uint32_t height  = ntohl(req.height);
        uint32_t data_sz = ntohl(req.data_size);

        if (op == GPU_OP_PING) {
            GpuResponse resp;
            memset(&resp, 0, sizeof(resp));
            resp.magic     = htonl(GPU_MAGIC);
            resp.status    = 0;
            resp.operation = GPU_OP_PING;
            send_all(client_sock, &resp, sizeof(resp));
            printf("[GPU-SVC] Ping OK\n");
            continue;
        }

        if (data_sz == 0 || data_sz > 7680u * 4320u * 3u) {
            fprintf(stderr, "[GPU-SVC] Bad data_sz %u\n", data_sz); break;
        }

        uint8_t *buf = (uint8_t *)malloc(data_sz);
        if (!buf) break;

        double t0 = now_ms();
        if (recv_all(client_sock, buf, data_sz) < 0) { free(buf); break; }

        uint8_t  *result    = NULL;
        uint32_t  result_sz = 0;
        uint8_t   status    = 0;

        if (op == GPU_OP_COMPRESS) {
            uint32_t max_out = data_sz * 2 + 4096;
            result = (uint8_t *)malloc(max_out);
            if (result) {
                result_sz = rle_compress(buf, data_sz, result, max_out);
                if (result_sz == 0) {
                    /* incompressible – send raw */
                    free(result);
                    result    = buf;
                    result_sz = data_sz;
                    buf       = NULL;
                } else {
                    free(buf); buf = NULL;
                }
            } else {
                status = 1; free(buf); buf = NULL;
            }
        } else if (op == GPU_OP_COLORCONV) {
            fix_colour_channels(buf, width, height);
            result    = buf;
            result_sz = data_sz;
            buf       = NULL;
        } else {
            fprintf(stderr, "[GPU-SVC] Unknown op 0x%02X\n", op);
            status = 2; free(buf); buf = NULL;
        }

        double t1 = now_ms();
        float  ms = (float)(t1 - t0);

        if (op == GPU_OP_COMPRESS && result_sz > 0) {
            printf("[GPU-SVC] Compressed %u → %u bytes (%.1f%%) in %.2f ms\n",
                   data_sz, result_sz,
                   100.0f * (float)result_sz / (float)data_sz, ms);
        } else if (op == GPU_OP_COLORCONV) {
            printf("[GPU-SVC] Colorfix %ux%u in %.2f ms\n", width, height, ms);
        }

        GpuResponse resp;
        memset(&resp, 0, sizeof(resp));
        resp.magic       = htonl(GPU_MAGIC);
        resp.status      = status;
        resp.operation   = op;
        resp.result_size = htonl(result_sz);
        resp.ms_elapsed  = ms;

        if (send_all(client_sock, &resp, sizeof(resp)) < 0) { free(result); break; }
        if (result && result_sz > 0)
            if (send_all(client_sock, result, result_sz) < 0) { free(result); break; }
        free(result);
    }
    printf("[GPU-SVC] Client disconnected\n");
}

void gpu_service_run(void)
{
#ifdef _WIN32
    SOCKET server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == INVALID_SOCKET) { fprintf(stderr, "[GPU-SVC] socket() failed\n"); return; }
#else
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) { fprintf(stderr, "[GPU-SVC] socket() failed\n"); return; }
#endif
    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(GPU_ACCEL_PORT);
    if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[GPU-SVC] bind() failed on port %d\n", GPU_ACCEL_PORT);
#ifdef _WIN32
        closesocket(server);
#else
        close(server);
#endif
        return;
    }
    listen(server, 4);
    printf("[GPU-SVC] Compute service listening on TCP %d\n", GPU_ACCEL_PORT);
    for (;;) {
        struct sockaddr_in ca;
        memset(&ca, 0, sizeof(ca));
#ifdef _WIN32
        int calen = sizeof(ca);
        SOCKET client = accept(server, (struct sockaddr *)&ca, &calen);
        if (client == INVALID_SOCKET) continue;
#else
        socklen_t calen = sizeof(ca);
        int client = accept(server, (struct sockaddr *)&ca, &calen);
        if (client < 0) continue;
#endif
        printf("[GPU-SVC] Connection from %s\n", inet_ntoa(ca.sin_addr));
        gpu_service_handle_client((int)client);
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
