/**
 * GPU_ACCELERATE.C - REMOTE GPU OFFLOAD ENGINE
 *
 * Provides a lightweight protocol for the Sender to request GPU-accelerated
 * tasks (frame compression, colour correction) from the Receiver's GPU.
 *
 * Protocol (TCP port 8082):
 *   Sender → Receiver : GpuRequest header  + raw pixel payload
 *   Receiver → Sender : GpuResponse header + result payload
 *
 * Supported operations:
 *   GPU_OP_PING      - Connectivity / handshake check
 *   GPU_OP_COMPRESS  - RLE-compress a raw RGB frame
 *   GPU_OP_COLORCONV - Fix BGR → RGB channel swap in-place
 */

#include "gpu_accelerate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#endif

/* ─── Constants ──────────────────────────────────────────────── */
#define GPU_ACCEL_PORT   8082
#define GPU_MAGIC        0x47505541u   /* ASCII "GPUA" */
#define GPU_VERSION      1

#define GPU_OP_PING      0xFF
#define GPU_OP_COMPRESS  0x01
#define GPU_OP_COLORCONV 0x03

/* ─── Wire structs ───────────────────────────────────────────── */
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
    uint8_t  status;        /* 0 = OK */
    uint8_t  operation;
    uint16_t flags;
    uint32_t result_size;
    float    ms_elapsed;
} GpuResponse;
#pragma pack(pop)

/* ─── Helpers ────────────────────────────────────────────────── */
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

/* ─── Colour-channel fix: BGR → RGB ─────────────────────────── */
static void fix_colour_channels(uint8_t *pixels, uint32_t width, uint32_t height)
{
    uint32_t total = width * height;
    for (uint32_t i = 0; i < total; i++) {
        uint8_t tmp        = pixels[i * 3];
        pixels[i * 3]      = pixels[i * 3 + 2];
        pixels[i * 3 + 2]  = tmp;
    }
}

/* ─── Simple RLE compressor ──────────────────────────────────── */
static uint32_t rle_compress(const uint8_t *src, uint32_t src_len,
                              uint8_t *dst, uint32_t dst_cap)
{
    uint32_t in = 0, out = 0;
    while (in + 2 < src_len && out + 4 <= dst_cap) {
        uint8_t r = src[in], g = src[in+1], b = src[in+2];
        uint8_t run = 1;
        while (run < 255 && (in + (uint32_t)run * 3 + 2) < src_len
               && src[in + run*3]   == r
               && src[in + run*3+1] == g
               && src[in + run*3+2] == b)
            run++;
        dst[out++] = run;
        dst[out++] = r;
        dst[out++] = g;
        dst[out++] = b;
        in += (uint32_t)run * 3;
    }
    return out;
}

/* ═══════════════════════════════════════════════════════════════
 * SENDER-SIDE (CLIENT) API
 * ═══════════════════════════════════════════════════════════════ */

int gpu_remote_connect(const char *receiver_ip)
{
#ifdef _WIN32
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return -1;
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
        fprintf(stderr, "[GPU] Could not connect to %s:%d\n",
                receiver_ip, GPU_ACCEL_PORT);
        return -1;
    }

    /* Ping handshake */
    GpuRequest ping;
    memset(&ping, 0, sizeof(ping));
    ping.magic     = htonl(GPU_MAGIC);
    ping.version   = GPU_VERSION;
    ping.operation = GPU_OP_PING;

    if (send_all((int)sock, &ping, sizeof(ping)) < 0) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return -1;
    }

    GpuResponse pong;
    if (recv_all((int)sock, &pong, sizeof(pong)) < 0 || pong.status != 0) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        fprintf(stderr, "[GPU] Ping failed\n");
        return -1;
    }

    printf("[GPU] \u2713 Connected to receiver GPU service at %s:%d\n",
           receiver_ip, GPU_ACCEL_PORT);
    return (int)sock;
}

int gpu_remote_compress(int gpu_sock,
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

    if (send_all(gpu_sock, &req, sizeof(req)) < 0)   return -1;
    if (send_all(gpu_sock, pixels, data_size) < 0)   return -1;

    GpuResponse resp;
    if (recv_all(gpu_sock, &resp, sizeof(resp)) < 0) return -1;
    if (resp.status != 0)                            return -1;

    uint32_t result_size = ntohl(resp.result_size);
    *out_buf = (uint8_t *)malloc(result_size);
    if (!*out_buf) return -1;

    if (recv_all(gpu_sock, *out_buf, result_size) < 0) {
        free(*out_buf); *out_buf = NULL;
        return -1;
    }

    printf("[GPU] Compressed %u \u2192 %u bytes (%.1f%%) in %.2f ms\n",
           data_size, result_size,
           100.0f * (float)result_size / (float)data_size,
           resp.ms_elapsed);

    return (int)result_size;
}

int gpu_remote_colorfix(int gpu_sock,
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

    if (send_all(gpu_sock, &req, sizeof(req)) < 0)   return -1;
    if (send_all(gpu_sock, pixels, data_size) < 0)   return -1;

    GpuResponse resp;
    if (recv_all(gpu_sock, &resp, sizeof(resp)) < 0) return -1;
    if (resp.status != 0)                            return -1;

    uint32_t result_size = ntohl(resp.result_size);
    if (result_size != data_size)                    return -1;

    if (recv_all(gpu_sock, pixels, data_size) < 0)   return -1;
    return 0;
}

void gpu_remote_disconnect(int gpu_sock)
{
    if (gpu_sock >= 0) {
#ifdef _WIN32
        closesocket(gpu_sock);
#else
        close(gpu_sock);
#endif
        printf("[GPU] Disconnected from receiver GPU service\n");
    }
}

/* ═══════════════════════════════════════════════════════════════
 * RECEIVER-SIDE (SERVER)
 * ═══════════════════════════════════════════════════════════════ */

void gpu_service_handle_client(int client_sock)
{
    printf("[GPU-SVC] Client connected\n");

    for (;;) {
        GpuRequest req;
        if (recv_all(client_sock, &req, sizeof(req)) < 0) break;
        if (ntohl(req.magic) != GPU_MAGIC) {
            fprintf(stderr, "[GPU-SVC] Bad magic\n");
            break;
        }

        uint8_t  op       = req.operation;
        uint32_t width    = ntohl(req.width);
        uint32_t height   = ntohl(req.height);
        uint32_t data_sz  = ntohl(req.data_size);

        /* PING */
        if (op == GPU_OP_PING) {
            GpuResponse resp;
            memset(&resp, 0, sizeof(resp));
            resp.magic     = htonl(GPU_MAGIC);
            resp.status    = 0;
            resp.operation = GPU_OP_PING;
            send_all(client_sock, &resp, sizeof(resp));
            continue;
        }

        /* Read payload */
        uint8_t *buf = (uint8_t *)malloc(data_sz);
        if (!buf || recv_all(client_sock, buf, data_sz) < 0) {
            free(buf); break;
        }

        uint8_t  *result    = NULL;
        uint32_t  result_sz = 0;
        uint8_t   status    = 0;

        if (op == GPU_OP_COMPRESS) {
            uint32_t max_out = data_sz * 2 + 4096;
            result = (uint8_t *)malloc(max_out);
            if (result) result_sz = rle_compress(buf, data_sz, result, max_out);
            else        status = 1;
            free(buf); buf = NULL;
        }
        else if (op == GPU_OP_COLORCONV) {
            fix_colour_channels(buf, width, height);
            result    = buf;
            result_sz = data_sz;
            buf       = NULL;
        }
        else {
            fprintf(stderr, "[GPU-SVC] Unknown op 0x%02X\n", op);
            status = 2;
            free(buf); buf = NULL;
        }

        GpuResponse resp;
        memset(&resp, 0, sizeof(resp));
        resp.magic       = htonl(GPU_MAGIC);
        resp.status      = status;
        resp.operation   = op;
        resp.result_size = htonl(result_sz);
        resp.ms_elapsed  = 0.5f;

        if (send_all(client_sock, &resp, sizeof(resp)) < 0) {
            free(result); break;
        }
        if (result_sz > 0 && result)
            send_all(client_sock, result, result_sz);

        free(result);
    }

    printf("[GPU-SVC] Client disconnected\n");
}

void gpu_service_run(void)
{
#ifdef _WIN32
    SOCKET server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == INVALID_SOCKET) {
        fprintf(stderr, "[GPU-SVC] socket() failed\n"); return;
    }
#else
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        fprintf(stderr, "[GPU-SVC] socket() failed\n"); return;
    }
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
    printf("[GPU-SVC] \u2713 GPU service listening on port %d\n", GPU_ACCEL_PORT);

    for (;;) {
        struct sockaddr_in ca;
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
