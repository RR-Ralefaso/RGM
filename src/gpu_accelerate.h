/**
 * GPU_ACCELERATE.H - REMOTE GPU OFFLOAD API
 *
 * Include this in sender.cpp to use the receiver's GPU.
 * Include this in receiver.cpp to run the GPU service.
 */
#ifndef GPU_ACCELERATE_H
#define GPU_ACCELERATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* ─── Socket type abstraction ─────────────────────────────────── */
#ifdef _WIN32
#include <winsock2.h>
    typedef SOCKET gpu_sock_t;
#define GPU_INVALID_SOCK INVALID_SOCKET
#define GPU_SOCK_VALID(s) ((s) != INVALID_SOCKET)
#else
typedef int gpu_sock_t;
#define GPU_INVALID_SOCK (-1)
#define GPU_SOCK_VALID(s) ((s) >= 0)
#endif

/* ─── Constants ───────────────────────────────────────────────── */
#define GPU_ACCEL_PORT 8082

    /* ── Sender-side (client) ─────────────────────────────────────── */

    /** Connect to the receiver's GPU service. Returns socket fd or -1. */
    gpu_sock_t gpu_remote_connect(const char *receiver_ip);

    /** Compress a raw RGB frame on the receiver's GPU.
     *  Allocates *out_buf; caller must free(). Returns compressed size or -1. */
    int gpu_remote_compress(gpu_sock_t gpu_sock,
                            const uint8_t *pixels,
                            uint32_t width, uint32_t height,
                            uint8_t **out_buf);

    /** Fix BGR→RGB colour channels using the receiver's GPU.
     *  Modifies `pixels` in-place. Returns 0 on success. */
    int gpu_remote_colorfix(gpu_sock_t gpu_sock,
                            uint8_t *pixels,
                            uint32_t width, uint32_t height);

    /** Close GPU service connection. */
    void gpu_remote_disconnect(gpu_sock_t gpu_sock);

    /* ── Receiver-side (server) ───────────────────────────────────── */

    /** Handle one connected GPU-service client (blocking until disconnect). */
    void gpu_service_handle_client(int client_sock);

    /** Start the GPU service listener – run in a dedicated thread (blocking). */
    void gpu_service_run(void);

    /**
     * Request GPU statistics from receiver
     */
    int gpu_request_stats(gpu_sock_t sock);

    /**
     * Print GPU statistics
     */
    void gpu_print_stats(gpu_sock_t sock);

#ifdef __cplusplus
}
#endif

#endif /* GPU_ACCELERATE_H */