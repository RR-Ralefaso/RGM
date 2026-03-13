/**
 * STORAGE.CPP - REMOTE STORAGE ACCESS IMPLEMENTATION
 *
 * This file implements both the sender-side client API and receiver-side
 * server for remote storage access. It allows the sender to browse,
 * read, and write files on the receiver's filesystem with proper
 * permission checks and path sanitization.
 */

#include "storage.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <cstring>
#include <chrono>
#include <thread>
#include <algorithm>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fileapi.h>
#include <direct.h>
#include <io.h>
#define mkdir _mkdir
#define access _access
#define F_OK 0
#else
#include <unistd.h>
#include <dirent.h>
#include <pwd.h>
#include <fcntl.h>
#include <sys/statvfs.h>
#endif

/* ─── ANSI colors for pretty output ────────────────────────────── */
#ifdef _WIN32
#define COL_RESET ""
#define COL_RED ""
#define COL_GREEN ""
#define COL_YELLOW ""
#define COL_CYAN ""
#define COL_MAGENTA ""
#define COL_BOLD ""
#else
#define COL_RESET "\033[0m"
#define COL_RED "\033[31m"
#define COL_GREEN "\033[32m"
#define COL_YELLOW "\033[33m"
#define COL_CYAN "\033[36m"
#define COL_MAGENTA "\033[35m"
#define COL_BOLD "\033[1m"
#endif

/* ─── Socket I/O helpers ───────────────────────────────────────── */
static int send_all(int sock, const void *buf, size_t size)
{
    const char *p = (const char *)buf;
    size_t sent = 0;
    while (sent < size)
    {
        int n = (int)send(sock, p + sent, (int)(size - sent), 0);
        if (n <= 0)
        {
            if (n < 0)
                perror("send");
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
        int n = (int)recv(sock, p + got, (int)(size - got), 0);
        if (n <= 0)
        {
            if (n < 0)
                perror("recv");
            return -1;
        }
        got += (size_t)n;
    }
    return 0;
}

/* ─── Global access mode for receiver ─────────────────────────── */
static uint32_t g_access_mode = STORAGE_ACCESS_READ; // Default: read-only

/* ════════════════════════════════════════════════════════════════
 * PATH SANITIZATION - Prevent directory traversal attacks
 * ════════════════════════════════════════════════════════════════ */

/**
 * Sanitize and validate a path to prevent directory traversal
 * Returns true if the path is safe to access
 */
static bool is_path_safe(const std::string &path, uint32_t access_mode)
{
    if (path.empty())
        return false;

    // Check for directory traversal sequences
    if (path.find("..") != std::string::npos)
    {
        std::cerr << "[STORAGE] Blocked path with '..': " << path << std::endl;
        return false;
    }

    // Check for absolute paths based on platform
#ifdef _WIN32
    // On Windows, restrict to reasonable locations unless admin
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    // Block system directories by default
    if (!(access_mode & STORAGE_ACCESS_ADMIN))
    {
        if (lower.find("c:\\windows") == 0 ||
            lower.find("c:\\program files") == 0 ||
            lower.find("c:\\programdata") == 0 ||
            lower.find("c:\\system volume information") == 0 ||
            lower.find("c:\\recovery") == 0 ||
            lower.find("c:\\$recycle.bin") == 0)
        {
            std::cerr << "[STORAGE] Blocked system directory: " << path << std::endl;
            return false;
        }
    }

    // Allow drives (C:\, D:\, etc.)
    if (path.length() >= 2 && path[1] == ':')
    {
        return true;
    }
#else
    // On Unix, block sensitive directories
    if (!(access_mode & STORAGE_ACCESS_ADMIN))
    {
        if (path.find("/etc") == 0 ||
            path.find("/dev") == 0 ||
            path.find("/proc") == 0 ||
            path.find("/sys") == 0 ||
            path.find("/boot") == 0 ||
            path.find("/root") == 0 ||
            path.find("/var") == 0)
        {
            std::cerr << "[STORAGE] Blocked system directory: " << path << std::endl;
            return false;
        }
    }

    // Block access to other users' home directories
    if (path.find("/home/") == 0 && path.length() > 6)
    {
        size_t next_slash = path.find('/', 6);
        if (next_slash != std::string::npos)
        {
            std::string username = path.substr(6, next_slash - 6);
            const char *current_user = getenv("USER");
            if (current_user && username != current_user)
            {
                std::cerr << "[STORAGE] Blocked access to other user's home: "
                          << username << std::endl;
                return false;
            }
        }
    }
#endif

    return true;
}

/* ════════════════════════════════════════════════════════════════
 * RECEIVER-SIDE STORAGE SERVICE IMPLEMENTATION
 * ════════════════════════════════════════════════════════════════ */

/**
 * Handle STORAGE_OP_LIST_DIR operation
 * Lists contents of a directory on the receiver
 */
static int handle_list_dir(int client_sock, const char *path, uint32_t access_mode)
{
    if (!is_path_safe(path, access_mode))
    {
        return STORAGE_ERR_PERMISSION;
    }

    std::vector<uint8_t> buffer;
    uint32_t entry_count = 0;

#ifdef _WIN32
    // Windows implementation using FindFirstFile/FindNextFile
    std::string search_path = std::string(path);
    if (search_path.back() != '\\' && search_path.back() != '/')
        search_path += '\\';
    search_path += "*";

    WIN32_FIND_DATAA ffd;
    HANDLE hFind = FindFirstFileA(search_path.c_str(), &ffd);

    if (hFind == INVALID_HANDLE_VALUE)
    {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
            return STORAGE_ERR_NOT_FOUND;
        return STORAGE_ERR_IO;
    }

    do
    {
        // Skip . and ..
        if (strcmp(ffd.cFileName, ".") == 0 || strcmp(ffd.cFileName, "..") == 0)
            continue;

        StorageEntry entry;
        memset(&entry, 0, sizeof(entry));

        entry.type = (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? STORAGE_TYPE_DIR : STORAGE_TYPE_FILE;
        entry.size = ((uint64_t)ffd.nFileSizeHigh << 32) | ffd.nFileSizeLow;

        // Convert FILETIME to Unix timestamp
        ULARGE_INTEGER uli;
        uli.LowPart = ffd.ftLastWriteTime.dwLowDateTime;
        uli.HighPart = ffd.ftLastWriteTime.dwHighDateTime;
        // Convert from Windows FILETIME (100-ns intervals since Jan 1 1601)
        // to Unix timestamp (seconds since Jan 1 1970)
        entry.modified = (uli.QuadPart - 116444736000000000ULL) / 10000000ULL;

        entry.attributes = ffd.dwFileAttributes;
        entry.name_len = (uint32_t)strlen(ffd.cFileName);

        // Store entry header
        size_t offset = buffer.size();
        buffer.resize(offset + sizeof(StorageEntry) + entry.name_len);
        memcpy(&buffer[offset], &entry, sizeof(StorageEntry));
        memcpy(&buffer[offset + sizeof(StorageEntry)], ffd.cFileName, entry.name_len);
        entry_count++;

    } while (FindNextFileA(hFind, &ffd) != 0);

    FindClose(hFind);

#else
    // Unix/Linux implementation using opendir/readdir
    DIR *dir = opendir(path);
    if (!dir)
    {
        return STORAGE_ERR_NOT_FOUND;
    }

    struct dirent *de;
    while ((de = readdir(dir)) != NULL)
    {
        // Skip . and ..
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;

        std::string full_path = std::string(path) + "/" + de->d_name;
        struct stat st;

        StorageEntry entry;
        memset(&entry, 0, sizeof(entry));

        if (stat(full_path.c_str(), &st) == 0)
        {
            entry.type = S_ISDIR(st.st_mode) ? STORAGE_TYPE_DIR : STORAGE_TYPE_FILE;
            entry.size = st.st_size;
            entry.modified = st.st_mtime;
            entry.attributes = st.st_mode;
        }
        else
        {
            // If stat fails, use dirent info
            entry.type = (de->d_type == DT_DIR) ? STORAGE_TYPE_DIR : STORAGE_TYPE_FILE;
            entry.size = 0;
            entry.modified = 0;
        }

        entry.name_len = (uint32_t)strlen(de->d_name);

        // Store entry header
        size_t offset = buffer.size();
        buffer.resize(offset + sizeof(StorageEntry) + entry.name_len);
        memcpy(&buffer[offset], &entry, sizeof(StorageEntry));
        memcpy(&buffer[offset + sizeof(StorageEntry)], de->d_name, entry.name_len);
        entry_count++;
    }

    closedir(dir);
#endif

    // Send response header
    StorageResponse resp;
    memset(&resp, 0, sizeof(resp));
    resp.magic = htonl(STORAGE_MAGIC);
    resp.status = STORAGE_OK;
    resp.operation = STORAGE_OP_LIST_DIR;
    resp.entry_count = htonl(entry_count);
    resp.data_len = htonl((uint32_t)buffer.size());

    if (send_all(client_sock, &resp, sizeof(resp)) < 0)
        return STORAGE_ERR_IO;

    // Send directory entries if any
    if (buffer.size() > 0)
    {
        if (send_all(client_sock, buffer.data(), buffer.size()) < 0)
            return STORAGE_ERR_IO;
    }

    return STORAGE_OK;
}

/**
 * Handle STORAGE_OP_READ_FILE operation
 * Reads a file on the receiver and sends contents to sender
 */
static int handle_read_file(int client_sock, const char *path,
                            uint64_t offset, uint32_t size,
                            uint32_t access_mode)
{
    // Check read permission
    if (!(access_mode & STORAGE_ACCESS_READ))
    {
        return STORAGE_ERR_PERMISSION;
    }

    if (!is_path_safe(path, access_mode))
    {
        return STORAGE_ERR_PERMISSION;
    }

    // Open file
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        return STORAGE_ERR_NOT_FOUND;
    }

    // Get file size
    uint64_t file_size = file.tellg();

    // Validate offset
    if (offset >= file_size)
    {
        file.close();
        return STORAGE_ERR_INVALID;
    }

    // Calculate read size (0 means entire file)
    uint64_t read_size = (size == 0) ? (file_size - offset) : std::min((uint64_t)size, file_size - offset);

    // Seek to offset and read
    file.seekg(offset, std::ios::beg);
    std::vector<uint8_t> buffer(read_size);
    file.read((char *)buffer.data(), read_size);

    if (!file)
    {
        file.close();
        return STORAGE_ERR_IO;
    }

    file.close();

    // Send response header
    StorageResponse resp;
    memset(&resp, 0, sizeof(resp));
    resp.magic = htonl(STORAGE_MAGIC);
    resp.status = STORAGE_OK;
    resp.operation = STORAGE_OP_READ_FILE;
    resp.data_len = htonl((uint32_t)read_size);
    resp.file_size = hton64(file_size);

    if (send_all(client_sock, &resp, sizeof(resp)) < 0)
        return STORAGE_ERR_IO;

    // Send file data
    if (read_size > 0)
    {
        if (send_all(client_sock, buffer.data(), (size_t)read_size) < 0)
            return STORAGE_ERR_IO;
    }

    return STORAGE_OK;
}

/**
 * Handle STORAGE_OP_WRITE_FILE operation
 * Writes data to a file on the receiver
 */
static int handle_write_file(int client_sock, const char *path,
                             const uint8_t *data, uint32_t size,
                             uint64_t offset, bool append,
                             uint32_t access_mode)
{
    // Check write permission
    if (!(access_mode & STORAGE_ACCESS_WRITE))
    {
        return STORAGE_ERR_PERMISSION;
    }

    if (!is_path_safe(path, access_mode))
    {
        return STORAGE_ERR_PERMISSION;
    }

    // Determine open mode
    std::ios::openmode mode = std::ios::binary;
    if (append)
    {
        mode |= std::ios::app;
    }
    else
    {
        mode |= std::ios::in | std::ios::out;
    }

    // Open file
    std::fstream file(path, mode);
    if (!file.is_open())
    {
        // Try creating the file
        file.open(path, std::ios::binary | std::ios::out);
        if (!file.is_open())
        {
            return STORAGE_ERR_PERMISSION;
        }
        file.close();
        file.open(path, mode);
        if (!file.is_open())
        {
            return STORAGE_ERR_IO;
        }
    }

    // Seek to offset if not appending
    if (!append && offset > 0)
    {
        file.seekp(offset, std::ios::beg);
    }

    // Write data
    file.write((const char *)data, size);

    if (!file)
    {
        file.close();
        return STORAGE_ERR_IO;
    }

    file.close();

    // Send response
    StorageResponse resp;
    memset(&resp, 0, sizeof(resp));
    resp.magic = htonl(STORAGE_MAGIC);
    resp.status = STORAGE_OK;
    resp.operation = STORAGE_OP_WRITE_FILE;
    resp.data_len = htonl(size);

    if (send_all(client_sock, &resp, sizeof(resp)) < 0)
        return STORAGE_ERR_IO;

    return STORAGE_OK;
}

/**
 * Handle STORAGE_OP_DELETE_FILE operation
 * Deletes a file on the receiver
 */
static int handle_delete_file(int client_sock, const char *path,
                              uint32_t access_mode)
{
    // Check write permission (delete requires write)
    if (!(access_mode & STORAGE_ACCESS_WRITE))
    {
        return STORAGE_ERR_PERMISSION;
    }

    if (!is_path_safe(path, access_mode))
    {
        return STORAGE_ERR_PERMISSION;
    }

    // Delete file
    if (remove(path) != 0)
    {
        return STORAGE_ERR_IO;
    }

    // Send response
    StorageResponse resp;
    memset(&resp, 0, sizeof(resp));
    resp.magic = htonl(STORAGE_MAGIC);
    resp.status = STORAGE_OK;
    resp.operation = STORAGE_OP_DELETE_FILE;

    if (send_all(client_sock, &resp, sizeof(resp)) < 0)
        return STORAGE_ERR_IO;

    return STORAGE_OK;
}

/**
 * Handle STORAGE_OP_MKDIR operation
 * Creates a directory on the receiver
 */
static int handle_mkdir(int client_sock, const char *path,
                        uint32_t access_mode)
{
    // Check write permission
    if (!(access_mode & STORAGE_ACCESS_WRITE))
    {
        return STORAGE_ERR_PERMISSION;
    }

    if (!is_path_safe(path, access_mode))
    {
        return STORAGE_ERR_PERMISSION;
    }

#ifdef _WIN32
    if (_mkdir(path) != 0)
    {
        return STORAGE_ERR_IO;
    }
#else
    if (mkdir(path, 0755) != 0)
    {
        return STORAGE_ERR_IO;
    }
#endif

    // Send response
    StorageResponse resp;
    memset(&resp, 0, sizeof(resp));
    resp.magic = htonl(STORAGE_MAGIC);
    resp.status = STORAGE_OK;
    resp.operation = STORAGE_OP_MKDIR;

    if (send_all(client_sock, &resp, sizeof(resp)) < 0)
        return STORAGE_ERR_IO;

    return STORAGE_OK;
}

/**
 * Handle STORAGE_OP_GET_INFO operation
 * Gets file or drive information
 */
static int handle_get_info(int client_sock, const char *path,
                           uint32_t access_mode)
{
    if (!is_path_safe(path, access_mode))
    {
        return STORAGE_ERR_PERMISSION;
    }

    uint64_t size = 0;
    uint64_t free_space = 0;
    uint32_t attributes = 0;

#ifdef _WIN32
    // Try as drive first
    if (strlen(path) == 3 && path[1] == ':' && (path[2] == '\\' || path[2] == '/'))
    {
        ULARGE_INTEGER freeBytesAvailable, totalBytes, totalFreeBytes;
        if (GetDiskFreeSpaceExA(path, &freeBytesAvailable, &totalBytes, &totalFreeBytes))
        {
            size = totalBytes.QuadPart;
            free_space = totalFreeBytes.QuadPart;
            attributes = GetDriveTypeA(path);
        }
    }
    else
    {
        // Regular file
        struct _stat st;
        if (_stat(path, &st) == 0)
        {
            size = st.st_size;
            attributes = st.st_mode;
        }
        else
        {
            return STORAGE_ERR_NOT_FOUND;
        }
    }
#else
    // Try as mount point first
    struct statvfs vfs;
    if (statvfs(path, &vfs) == 0)
    {
        size = (uint64_t)vfs.f_blocks * vfs.f_frsize;
        free_space = (uint64_t)vfs.f_bfree * vfs.f_frsize;
        attributes = vfs.f_flag;
    }
    else
    {
        // Regular file
        struct stat st;
        if (stat(path, &st) == 0)
        {
            size = st.st_size;
            attributes = st.st_mode;
        }
        else
        {
            return STORAGE_ERR_NOT_FOUND;
        }
    }
#endif

    // Send response
    StorageResponse resp;
    memset(&resp, 0, sizeof(resp));
    resp.magic = htonl(STORAGE_MAGIC);
    resp.status = STORAGE_OK;
    resp.operation = STORAGE_OP_GET_INFO;
    resp.file_size = hton64(size);
    resp.free_space = hton64(free_space);
    resp.attributes = htonl(attributes);

    if (send_all(client_sock, &resp, sizeof(resp)) < 0)
        return STORAGE_ERR_IO;

    return STORAGE_OK;
}

/**
 * Handle STORAGE_OP_GET_DRIVES operation
 * Gets list of drives (Windows) or mount points (Unix)
 */
static int handle_get_drives(int client_sock, uint32_t access_mode)
{
    std::vector<uint8_t> buffer;
    uint32_t entry_count = 0;

#ifdef _WIN32
    // Windows: get drive letters
    DWORD drives = GetLogicalDrives();
    char drive[] = "A:\\";

    for (int i = 0; i < 26; i++)
    {
        if (drives & (1 << i))
        {
            drive[0] = 'A' + i;

            // Get drive type
            UINT type = GetDriveTypeA(drive);
            if (type != DRIVE_NO_ROOT_DIR)
            {
                StorageEntry entry;
                memset(&entry, 0, sizeof(entry));
                entry.type = STORAGE_TYPE_DRIVE;
                entry.name_len = 3; // "C:\"

                // Get free space
                ULARGE_INTEGER freeBytesAvailable, totalBytes, totalFreeBytes;
                if (GetDiskFreeSpaceExA(drive, &freeBytesAvailable, &totalBytes, &totalFreeBytes))
                {
                    entry.size = totalBytes.QuadPart;
                    entry.attributes = (uint32_t)type;
                }

                size_t offset = buffer.size();
                buffer.resize(offset + sizeof(StorageEntry) + entry.name_len);
                memcpy(&buffer[offset], &entry, sizeof(StorageEntry));
                memcpy(&buffer[offset + sizeof(StorageEntry)], drive, entry.name_len);
                entry_count++;
            }
        }
    }
#else
    // Unix: get mount points from /proc/mounts
    std::ifstream mounts("/proc/mounts");
    if (mounts.is_open())
    {
        std::string line;
        while (std::getline(mounts, line))
        {
            // Parse mount point (second field)
            size_t space1 = line.find(' ');
            if (space1 == std::string::npos)
                continue;
            size_t space2 = line.find(' ', space1 + 1);
            if (space2 == std::string::npos)
                continue;

            std::string mount_point = line.substr(space1 + 1, space2 - space1 - 1);

            // Skip certain mount points
            if (mount_point == "/" || mount_point == "/dev" ||
                mount_point == "/proc" || mount_point == "/sys")
            {
                continue;
            }

            StorageEntry entry;
            memset(&entry, 0, sizeof(entry));
            entry.type = STORAGE_TYPE_DRIVE;
            entry.name_len = (uint32_t)mount_point.length();

            // Get free space using statvfs
            struct statvfs vfs;
            if (statvfs(mount_point.c_str(), &vfs) == 0)
            {
                entry.size = (uint64_t)vfs.f_blocks * vfs.f_frsize;
            }

            size_t offset = buffer.size();
            buffer.resize(offset + sizeof(StorageEntry) + entry.name_len);
            memcpy(&buffer[offset], &entry, sizeof(StorageEntry));
            memcpy(&buffer[offset + sizeof(StorageEntry)],
                   mount_point.c_str(), entry.name_len);
            entry_count++;
        }
        mounts.close();
    }
#endif

    // Send response header
    StorageResponse resp;
    memset(&resp, 0, sizeof(resp));
    resp.magic = htonl(STORAGE_MAGIC);
    resp.status = STORAGE_OK;
    resp.operation = STORAGE_OP_GET_DRIVES;
    resp.entry_count = htonl(entry_count);
    resp.data_len = htonl((uint32_t)buffer.size());

    if (send_all(client_sock, &resp, sizeof(resp)) < 0)
        return STORAGE_ERR_IO;

    // Send drive entries
    if (buffer.size() > 0)
    {
        if (send_all(client_sock, buffer.data(), buffer.size()) < 0)
            return STORAGE_ERR_IO;
    }

    return STORAGE_OK;
}

/* ════════════════════════════════════════════════════════════════
 * RECEIVER-SIDE CLIENT HANDLER
 * ════════════════════════════════════════════════════════════════ */

/**
 * Handle a single client connection to the storage service
 * Processes requests until client disconnects
 */
void storage_service_handle_client(int client_sock)
{
    printf("[STORAGE-SVC] Client connected\n");

    // Receive initial access mode from client
    uint32_t client_access_mode = STORAGE_ACCESS_READ;
    recv(client_sock, (char *)&client_access_mode, sizeof(client_access_mode), 0);
    client_access_mode = ntohl(client_access_mode);

    printf("[STORAGE-SVC] Client access mode: %s%s%s\n",
           (client_access_mode & STORAGE_ACCESS_READ) ? "READ " : "",
           (client_access_mode & STORAGE_ACCESS_WRITE) ? "WRITE " : "",
           (client_access_mode & STORAGE_ACCESS_ADMIN) ? "ADMIN" : "");

    for (;;)
    {
        // Receive request header
        StorageRequest req;
        if (recv_all(client_sock, &req, sizeof(req)) < 0)
        {
            printf("[STORAGE-SVC] Client disconnected\n");
            break;
        }

        // Validate magic
        if (ntohl(req.magic) != STORAGE_MAGIC)
        {
            fprintf(stderr, "[STORAGE-SVC] Invalid magic from client\n");
            break;
        }

        uint8_t op = req.operation;
        uint32_t path_len = ntohl(req.path_len);
        uint32_t data_len = ntohl(req.data_len);
        uint64_t offset = ntoh64(req.offset);
        uint16_t flags = ntohs(req.flags);

        // Handle PING specially
        if (op == STORAGE_OP_PING)
        {
            StorageResponse resp;
            memset(&resp, 0, sizeof(resp));
            resp.magic = htonl(STORAGE_MAGIC);
            resp.status = STORAGE_OK;
            resp.operation = STORAGE_OP_PING;
            send_all(client_sock, &resp, sizeof(resp));
            printf("[STORAGE-SVC] Ping from client\n");
            continue;
        }

        // Read path if present
        std::string path;
        if (path_len > 0)
        {
            std::vector<char> path_buf(path_len + 1);
            if (recv_all(client_sock, path_buf.data(), path_len) < 0)
                break;
            path_buf[path_len] = '\0';
            path = path_buf.data();
        }

        // Read data if present (for write operations)
        std::vector<uint8_t> data;
        if (data_len > 0 && op == STORAGE_OP_WRITE_FILE)
        {
            data.resize(data_len);
            if (recv_all(client_sock, data.data(), data_len) < 0)
                break;
        }

        int result = STORAGE_OK;

        // Dispatch based on operation
        switch (op)
        {
        case STORAGE_OP_LIST_DIR:
            result = handle_list_dir(client_sock, path.c_str(), client_access_mode);
            break;

        case STORAGE_OP_READ_FILE:
            result = handle_read_file(client_sock, path.c_str(), offset,
                                      data_len, client_access_mode);
            break;

        case STORAGE_OP_WRITE_FILE:
            result = handle_write_file(client_sock, path.c_str(),
                                       data.data(), data_len, offset,
                                       (flags & 1) != 0, client_access_mode);
            break;

        case STORAGE_OP_DELETE_FILE:
            result = handle_delete_file(client_sock, path.c_str(), client_access_mode);
            break;

        case STORAGE_OP_MKDIR:
            result = handle_mkdir(client_sock, path.c_str(), client_access_mode);
            break;

        case STORAGE_OP_GET_INFO:
            result = handle_get_info(client_sock, path.c_str(), client_access_mode);
            break;

        case STORAGE_OP_GET_DRIVES:
            result = handle_get_drives(client_sock, client_access_mode);
            break;

        default:
            fprintf(stderr, "[STORAGE-SVC] Unknown operation: 0x%02X\n", op);
            result = STORAGE_ERR_INVALID;
            break;
        }

        // If handler didn't send response (error), send error response
        if (result != STORAGE_OK && result != STORAGE_ERR_IO)
        {
            StorageResponse resp;
            memset(&resp, 0, sizeof(resp));
            resp.magic = htonl(STORAGE_MAGIC);
            resp.status = (uint8_t)(-result); // Convert to positive error code
            resp.operation = op;
            send_all(client_sock, &resp, sizeof(resp));
        }
    }

#ifdef _WIN32
    closesocket(client_sock);
#else
    close(client_sock);
#endif
    printf("[STORAGE-SVC] Client disconnected\n");
}

/**
 * Main storage service thread function
 * Listens for connections and spawns handlers
 */
void storage_service_run(void)
{
#ifdef _WIN32
    SOCKET server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == INVALID_SOCKET)
    {
        fprintf(stderr, "[STORAGE-SVC] socket() failed\n");
        return;
    }
#else
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0)
    {
        fprintf(stderr, "[STORAGE-SVC] socket() failed\n");
        return;
    }
#endif

    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(STORAGE_SERVICE_PORT);

    if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        fprintf(stderr, "[STORAGE-SVC] bind() failed on port %d\n", STORAGE_SERVICE_PORT);
#ifdef _WIN32
        closesocket(server);
#else
        close(server);
#endif
        return;
    }

    listen(server, 5);
    printf("[STORAGE-SVC] Storage service listening on TCP %d\n", STORAGE_SERVICE_PORT);

    for (;;)
    {
        struct sockaddr_in ca;
        memset(&ca, 0, sizeof(ca));
#ifdef _WIN32
        int calen = sizeof(ca);
        SOCKET client = accept(server, (struct sockaddr *)&ca, &calen);
        if (client == INVALID_SOCKET)
            continue;
#else
        socklen_t calen = sizeof(ca);
        int client = accept(server, (struct sockaddr *)&ca, &calen);
        if (client < 0)
            continue;
#endif

        printf("[STORAGE-SVC] Connection from %s\n", inet_ntoa(ca.sin_addr));
        storage_service_handle_client(client);
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

/**
 * Helper: send request and receive response
 */
static int do_storage_request(storage_sock_t sock,
                              uint8_t op,
                              const std::string &path,
                              const uint8_t *data,
                              uint32_t data_len,
                              uint64_t offset,
                              uint16_t flags,
                              std::vector<uint8_t> *response_data,
                              StorageResponse *response_header)
{
    // Prepare request
    StorageRequest req;
    memset(&req, 0, sizeof(req));
    req.magic = htonl(STORAGE_MAGIC);
    req.version = STORAGE_VERSION;
    req.operation = op;
    req.flags = htons(flags);
    req.access_mode = 0; // Set by connect
    req.path_len = htonl((uint32_t)path.length());
    req.data_len = htonl(data_len);
    req.offset = hton64(offset);

    // Send request
    if (send_all((int)sock, &req, sizeof(req)) < 0)
        return STORAGE_ERR_IO;

    // Send path if present
    if (!path.empty())
    {
        if (send_all((int)sock, path.c_str(), path.length()) < 0)
            return STORAGE_ERR_IO;
    }

    // Send data if present
    if (data && data_len > 0)
    {
        if (send_all((int)sock, data, data_len) < 0)
            return STORAGE_ERR_IO;
    }

    // Receive response header
    StorageResponse resp;
    if (recv_all((int)sock, &resp, sizeof(resp)) < 0)
        return STORAGE_ERR_IO;

    if (ntohl(resp.magic) != STORAGE_MAGIC)
        return STORAGE_ERR_INVALID;

    if (resp.status != 0)
    {
        return -(int)resp.status;
    }

    // Copy response header if requested
    if (response_header)
    {
        *response_header = resp;
    }

    // Read response data if present and requested
    uint32_t resp_data_len = ntohl(resp.data_len);
    if (response_data && resp_data_len > 0)
    {
        response_data->resize(resp_data_len);
        if (recv_all((int)sock, response_data->data(), resp_data_len) < 0)
            return STORAGE_ERR_IO;
    }

    return STORAGE_OK;
}

/**
 * Connect to receiver's storage service
 */
storage_sock_t storage_remote_connect(const char *receiver_ip, uint32_t access_mode)
{
#ifdef _WIN32
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET)
        return STORAGE_INVALID_SOCK;
#else
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return STORAGE_INVALID_SOCK;
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(STORAGE_SERVICE_PORT);
    inet_pton(AF_INET, receiver_ip, &addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        fprintf(stderr, "[STORAGE] Failed to connect to %s:%d\n",
                receiver_ip, STORAGE_SERVICE_PORT);
        return STORAGE_INVALID_SOCK;
    }

    // Send access mode
    uint32_t net_mode = htonl(access_mode);
    send_all(sock, &net_mode, sizeof(net_mode));

    // Ping to verify connection
    StorageRequest ping;
    memset(&ping, 0, sizeof(ping));
    ping.magic = htonl(STORAGE_MAGIC);
    ping.version = STORAGE_VERSION;
    ping.operation = STORAGE_OP_PING;

    if (send_all(sock, &ping, sizeof(ping)) < 0)
    {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return STORAGE_INVALID_SOCK;
    }

    StorageResponse pong;
    if (recv_all(sock, &pong, sizeof(pong)) < 0 || pong.status != 0)
    {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        fprintf(stderr, "[STORAGE] Ping failed\n");
        return STORAGE_INVALID_SOCK;
    }

    printf("[STORAGE] Connected to %s:%d (mode: %s%s%s)\n",
           receiver_ip, STORAGE_SERVICE_PORT,
           (access_mode & STORAGE_ACCESS_READ) ? "READ " : "",
           (access_mode & STORAGE_ACCESS_WRITE) ? "WRITE " : "",
           (access_mode & STORAGE_ACCESS_ADMIN) ? "ADMIN" : "");

    return (storage_sock_t)sock;
}

/**
 * List directory contents
 */
int storage_remote_list_dir(storage_sock_t sock, const char *path,
                            std::vector<StorageEntry> &entries)
{
    std::vector<uint8_t> response_data;
    StorageResponse resp;

    int result = do_storage_request(sock, STORAGE_OP_LIST_DIR, path,
                                    nullptr, 0, 0, 0, &response_data, &resp);

    if (result != STORAGE_OK)
        return result;

    // Parse entries from response data
    entries.clear();
    uint32_t entry_count = ntohl(resp.entry_count);
    const uint8_t *ptr = response_data.data();
    const uint8_t *end = ptr + response_data.size();

    for (uint32_t i = 0; i < entry_count && ptr < end; i++)
    {
        if (ptr + sizeof(StorageEntry) > end)
            break;

        StorageEntry entry;
        memcpy(&entry, ptr, sizeof(StorageEntry));
        ptr += sizeof(StorageEntry);

        // Convert network byte order
        entry.size = ntoh64(entry.size);
        entry.modified = ntoh64(entry.modified);
        entry.attributes = ntohl(entry.attributes);
        entry.name_len = ntohl(entry.name_len);

        if (ptr + entry.name_len > end)
            break;

        // Get filename
        std::string name((const char *)ptr, entry.name_len);
        ptr += entry.name_len;

        // Store in vector with name (we'll store separately)
        entries.push_back(entry);

        // In a real implementation, you'd store the name with the entry
        // For now, we'll just keep the entry without the name
    }

    return entry_count;
}

/**
 * Read file from receiver
 */
int storage_remote_read_file(storage_sock_t sock, const char *path,
                             uint64_t offset, uint32_t size,
                             std::vector<uint8_t> &data)
{
    StorageResponse resp;
    return do_storage_request(sock, STORAGE_OP_READ_FILE, path,
                              nullptr, size, offset, 0, &data, &resp);
}

/**
 * Write file to receiver
 */
int storage_remote_write_file(storage_sock_t sock, const char *path,
                              const uint8_t *data, uint32_t size,
                              uint64_t offset, bool append)
{
    uint16_t flags = append ? 1 : 0;
    return do_storage_request(sock, STORAGE_OP_WRITE_FILE, path,
                              data, size, offset, flags, nullptr, nullptr);
}

/**
 * Delete file on receiver
 */
int storage_remote_delete_file(storage_sock_t sock, const char *path)
{
    return do_storage_request(sock, STORAGE_OP_DELETE_FILE, path,
                              nullptr, 0, 0, 0, nullptr, nullptr);
}

/**
 * Create directory on receiver
 */
int storage_remote_mkdir(storage_sock_t sock, const char *path)
{
    return do_storage_request(sock, STORAGE_OP_MKDIR, path,
                              nullptr, 0, 0, 0, nullptr, nullptr);
}

/**
 * Get file/drive information
 */
int storage_remote_get_info(storage_sock_t sock, const char *path,
                            uint64_t *size, uint64_t *free_space,
                            uint32_t *attributes)
{
    StorageResponse resp;
    int result = do_storage_request(sock, STORAGE_OP_GET_INFO, path,
                                    nullptr, 0, 0, 0, nullptr, &resp);

    if (result == STORAGE_OK)
    {
        if (size)
            *size = ntoh64(resp.file_size);
        if (free_space)
            *free_space = ntoh64(resp.free_space);
        if (attributes)
            *attributes = ntohl(resp.attributes);
    }

    return result;
}

/**
 * Get drives/mount points from receiver
 */
int storage_remote_get_drives(storage_sock_t sock, std::vector<std::string> &drives)
{
    std::vector<uint8_t> response_data;
    StorageResponse resp;

    int result = do_storage_request(sock, STORAGE_OP_GET_DRIVES, "",
                                    nullptr, 0, 0, 0, &response_data, &resp);

    if (result != STORAGE_OK)
        return result;

    // Parse drives from response data
    drives.clear();
    uint32_t entry_count = ntohl(resp.entry_count);
    const uint8_t *ptr = response_data.data();
    const uint8_t *end = ptr + response_data.size();

    for (uint32_t i = 0; i < entry_count && ptr < end; i++)
    {
        if (ptr + sizeof(StorageEntry) > end)
            break;

        // Skip entry header
        const StorageEntry *entry = (const StorageEntry *)ptr;
        ptr += sizeof(StorageEntry);

        uint32_t name_len = ntohl(entry->name_len);
        if (ptr + name_len > end)
            break;

        drives.push_back(std::string((const char *)ptr, name_len));
        ptr += name_len;
    }

    return entry_count;
}

/**
 * Disconnect from storage service
 */
void storage_remote_disconnect(storage_sock_t sock)
{
    if (STORAGE_SOCK_VALID(sock))
    {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        printf("[STORAGE] Disconnected from receiver storage service\n");
    }
}

/* ════════════════════════════════════════════════════════════════
 * INTERACTIVE MENU HELPER FUNCTIONS
 * ════════════════════════════════════════════════════════════════ */

/**
 * Join path components with proper separator
 */
std::string storage_path_join(const std::string &dir, const std::string &file)
{
    if (dir.empty())
        return file;
    if (file.empty())
        return dir;

    std::string result = dir;

    // Add separator if needed
    char last = result.back();
#ifdef _WIN32
    if (last != '\\' && last != '/')
    {
        result += '\\';
    }
#else
    if (last != '/')
    {
        result += '/';
    }
#endif

    result += file;
    return result;
}

/**
 * Format file size for display
 */
std::string storage_format_size(uint64_t size)
{
    const char *units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    int unit = 0;
    double s = (double)size;

    while (s >= 1024.0 && unit < 5)
    {
        s /= 1024.0;
        unit++;
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f %s", s, units[unit]);
    return buf;
}

/**
 * Print directory listing
 */
void storage_print_directory(const std::vector<StorageEntry> &entries)
{
    std::cout << COL_CYAN << COL_BOLD
              << std::left
              << std::setw(5) << "Type"
              << std::setw(40) << "Name"
              << std::setw(12) << "Size"
              << "Modified"
              << COL_RESET << "\n"
              << std::string(80, '-') << "\n";

    for (const auto &entry : entries)
    {
        std::string type_str = (entry.type == STORAGE_TYPE_DIR) ? "[DIR]" : "[FILE]";
        if (entry.type == STORAGE_TYPE_DRIVE)
            type_str = "[DRV]";

        // Note: In a real implementation, you'd have the filename stored
        // For now, we'll use a placeholder
        std::cout << type_str
                  << "  "
                  << std::left << std::setw(40) << "<name>"
                  << std::right << std::setw(10) << storage_format_size(entry.size)
                  << "  " << entry.modified
                  << "\n";
    }
}