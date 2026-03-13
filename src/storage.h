/**
 * STORAGE.H - REMOTE STORAGE ACCESS API
 *
 * Allows sender to browse, read, and write files on receiver
 *
 * Features:
 * - Browse directory contents
 * - Read files with offset support
 * - Write files with append/overwrite options
 * - Delete files (with permission)
 * - Create directories
 * - Get drive/mount point information
 *
 * Security:
 * - Path sanitization to prevent directory traversal
 * - Read-only mode by default
 * - Admin mode for elevated access (optional)
 */

#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>

#ifdef __cplusplus
extern "C"
{
#endif

/* ─── Socket type abstraction ─────────────────────────────────── */
#ifdef _WIN32
#include <winsock2.h>
    typedef SOCKET storage_sock_t;
#define STORAGE_INVALID_SOCK INVALID_SOCKET
#define STORAGE_SOCK_VALID(s) ((s) != INVALID_SOCKET)
#else
typedef int storage_sock_t;
#define STORAGE_INVALID_SOCK (-1)
#define STORAGE_SOCK_VALID(s) ((s) >= 0)
#endif

/* ─── Constants ───────────────────────────────────────────────── */
#define STORAGE_SERVICE_PORT 8084
#define STORAGE_MAGIC 0x53544F52u /* "STOR" */
#define STORAGE_VERSION 1u

/* Operation codes */
#define STORAGE_OP_PING 0xF0u
#define STORAGE_OP_LIST_DIR 0x01u
#define STORAGE_OP_READ_FILE 0x02u
#define STORAGE_OP_WRITE_FILE 0x03u
#define STORAGE_OP_DELETE_FILE 0x04u
#define STORAGE_OP_MKDIR 0x05u
#define STORAGE_OP_GET_INFO 0x06u
#define STORAGE_OP_GET_DRIVES 0x07u /* Windows drive letters */

/* Access modes */
#define STORAGE_ACCESS_READ 0x01
#define STORAGE_ACCESS_WRITE 0x02
#define STORAGE_ACCESS_ADMIN 0x04 /* Administrative access */

/* File types */
#define STORAGE_TYPE_FILE 0
#define STORAGE_TYPE_DIR 1
#define STORAGE_TYPE_DRIVE 2

/* Error codes */
#define STORAGE_OK 0
#define STORAGE_ERR_PERMISSION -1
#define STORAGE_ERR_NOT_FOUND -2
#define STORAGE_ERR_IO -3
#define STORAGE_ERR_INVALID -4

/* ─── Wire structs (packed) ──────────────────────────────────── */
#pragma pack(push, 1)

    /**
     * Storage Request Header
     * Sent from sender to receiver for each operation
     */
    typedef struct
    {
        uint32_t magic;       /* STORAGE_MAGIC */
        uint8_t version;      /* Protocol version */
        uint8_t operation;    /* Operation code */
        uint16_t flags;       /* Operation-specific flags */
        uint32_t access_mode; /* Requested access level */
        uint32_t path_len;    /* Length of path string (if any) */
        uint32_t data_len;    /* Length of data (for write operations) */
        uint64_t offset;      /* File offset for read/write */
        uint32_t reserved;    /* Reserved for future use */
    } StorageRequest;

    /**
     * Storage Response Header
     * Sent from receiver to sender in response to operations
     */
    typedef struct
    {
        uint32_t magic;       /* STORAGE_MAGIC */
        uint8_t status;       /* 0 = OK, negative = error code */
        uint8_t operation;    /* Echo of operation code */
        uint16_t flags;       /* Response flags */
        uint32_t entry_count; /* Number of entries (for list operations) */
        uint32_t data_len;    /* Length of following data */
        uint64_t file_size;   /* File size (for get_info) */
        uint64_t free_space;  /* Free space (for drives) */
        uint32_t attributes;  /* File attributes */
        uint32_t reserved;
    } StorageResponse;

    /**
     * Directory Entry
     * Followed by variable-length filename
     */
    typedef struct
    {
        uint8_t type; /* STORAGE_TYPE_* */
        uint8_t pad[3];
        uint64_t size;       /* File size (0 for directories) */
        uint64_t modified;   /* Modification time (Unix timestamp) */
        uint32_t attributes; /* Platform-specific attributes */
        uint32_t name_len;   /* Length of filename that follows */
        /* Followed by filename (UTF-8) */
    } StorageEntry;

#pragma pack(pop)

    /* ─── Sender-side (client) API ────────────────────────────────── */

    /**
     * Connect to receiver's storage service
     * @param receiver_ip IP address of receiver
     * @param access_mode Requested access level (STORAGE_ACCESS_* flags)
     * @return Socket handle or STORAGE_INVALID_SOCK on error
     */
    storage_sock_t storage_remote_connect(const char *receiver_ip, uint32_t access_mode);

    /**
     * List directory contents on receiver
     * @param sock Storage service socket
     * @param path Directory path to list
     * @param entries Vector to store directory entries
     * @return Number of entries or negative error code
     */
    int storage_remote_list_dir(storage_sock_t sock, const char *path,
                                std::vector<StorageEntry> &entries);

    /**
     * Read file contents from receiver
     * @param sock Storage service socket
     * @param path File path to read
     * @param offset Starting offset (0 for beginning)
     * @param size Number of bytes to read (0 for entire file)
     * @param data Vector to store file data
     * @return Number of bytes read or negative error code
     */
    int storage_remote_read_file(storage_sock_t sock, const char *path,
                                 uint64_t offset, uint32_t size,
                                 std::vector<uint8_t> &data);

    /**
     * Write file contents to receiver
     * @param sock Storage service socket
     * @param path File path to write
     * @param data Data to write
     * @param size Size of data
     * @param offset Offset to write at (0 for beginning)
     * @param append If true, append to end instead of overwriting
     * @return Number of bytes written or negative error code
     */
    int storage_remote_write_file(storage_sock_t sock, const char *path,
                                  const uint8_t *data, uint32_t size,
                                  uint64_t offset, bool append);

    /**
     * Delete file on receiver
     * @param sock Storage service socket
     * @param path File path to delete
     * @return 0 on success, negative error code on failure
     */
    int storage_remote_delete_file(storage_sock_t sock, const char *path);

    /**
     * Create directory on receiver
     * @param sock Storage service socket
     * @param path Directory path to create
     * @return 0 on success, negative error code on failure
     */
    int storage_remote_mkdir(storage_sock_t sock, const char *path);

    /**
     * Get file/drive information on receiver
     * @param sock Storage service socket
     * @param path Path to query
     * @param size Pointer to store file size
     * @param free_space Pointer to store free space (for drives)
     * @param attributes Pointer to store file attributes
     * @return 0 on success, negative error code on failure
     */
    int storage_remote_get_info(storage_sock_t sock, const char *path,
                                uint64_t *size, uint64_t *free_space,
                                uint32_t *attributes);

    /**
     * Get drive list (Windows) or mount points (Unix) on receiver
     * @param sock Storage service socket
     * @param drives Vector to store drive/mount point strings
     * @return Number of drives or negative error code
     */
    int storage_remote_get_drives(storage_sock_t sock,
                                  std::vector<std::string> &drives);

    /**
     * Disconnect from storage service
     * @param sock Storage service socket
     */
    void storage_remote_disconnect(storage_sock_t sock);

    /* ─── Interactive menu helpers ───────────────────────────────── */

    /**
     * Join path components with proper separator
     */
    std::string storage_path_join(const std::string &dir, const std::string &file);

    /**
     * Format file size for display
     */
    std::string storage_format_size(uint64_t size);

    /**
     * Print directory listing
     */
    void storage_print_directory(const std::vector<StorageEntry> &entries);

    /* ─── Receiver-side (server) API ─────────────────────────────── */

    /**
     * Handle one storage service client connection
     * @param client_sock Connected client socket
     */
    void storage_service_handle_client(int client_sock);

    /**
     * Start the storage service listener
     * Runs in a loop, accepting and handling connections
     */
    void storage_service_run(void);

#ifdef __cplusplus
}

/* Network byte order conversion for 64-bit values */
static inline uint64_t hton64(uint64_t val)
{
#ifdef _WIN32
    return htonll(val);
#else
    return htobe64(val);
#endif
}

static inline uint64_t ntoh64(uint64_t val)
{
#ifdef _WIN32
    return ntohll(val);
#else
    return be64toh(val);
#endif
}

#endif

#endif /* STORAGE_H */