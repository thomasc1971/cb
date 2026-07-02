#ifndef CB_COMPAT_H
#define CB_COMPAT_H

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- open_memstream compat --- */

#ifdef HAVE_OPEN_MEMSTREAM
#define cb_open_memstream(bufp, sizep) open_memstream(bufp, sizep)
#define cb_close_memstream(f)          fclose(f)
#else
FILE *cb_open_memstream(char **bufp, size_t *sizep);
int cb_close_memstream(FILE *f);
#endif

/* --- Socket compat (POSIX vs Winsock2) --- */

#ifdef _WIN32

// clang-format off
// winsock2.h must be included before windows.h
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <sys/types.h>
// clang-format on

typedef SOCKET cb_socket_t;
#define CB_INVALID_SOCKET INVALID_SOCKET
#define CB_SOCKET_ERROR   SOCKET_ERROR

#define cb_close_socket(fd) closesocket(fd)
#define cb_sock_errno       WSAGetLastError()
#define cb_sock_eintr       WSAEINTR
#define cb_sock_strerror(e) cb_wsa_strerror(e)

typedef WSAPOLLFD cb_pollfd;
#define cb_poll WSAPoll

#define CB_SHUT_RDWR SD_BOTH

int cb_wsa_startup(void);
void cb_wsa_cleanup(void);
const char *cb_wsa_strerror(int err);

/* On Windows, SO_RCVTIMEO/SO_SNDTIMEO take a DWORD (ms), not struct timeval */
static inline int cb_set_sock_timeout(cb_socket_t fd, int timeout_sec)
{
    DWORD ms = (DWORD)(timeout_sec * 1000);
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&ms, sizeof(ms)) < 0)
        return -1;
    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&ms, sizeof(ms)) < 0)
        return -1;
    return 0;
}

#else

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

typedef int cb_socket_t;
#define CB_INVALID_SOCKET (-1)
#define CB_SOCKET_ERROR   (-1)

#define cb_close_socket(fd) close(fd)
#define cb_sock_errno       errno
#define cb_sock_eintr       EINTR
#define cb_sock_strerror(e) strerror(e)

typedef struct pollfd cb_pollfd;
#define cb_poll             poll

#define CB_SHUT_RDWR SHUT_RDWR

static inline int cb_wsa_startup(void) { return 0; }
static inline void cb_wsa_cleanup(void) {}

static inline int cb_set_sock_timeout(cb_socket_t fd, int timeout_sec)
{
    struct timeval tv = { .tv_sec = timeout_sec, .tv_usec = 0 };
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
        return -1;
    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0)
        return -1;
    return 0;
}

#endif /* _WIN32 */

/* --- Filesystem compat (for tests) --- */

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <process.h>
#define cb_mkdir(path, mode) _mkdir(path)
#define cb_rmdir(path)       _rmdir(path)
#define cb_unlink(path)      _unlink(path)
#define cb_getpid()          _getpid()
#define cb_dup(fd)           _dup(fd)
#define cb_dup2(fd1, fd2)    _dup2(fd1, fd2)
#define cb_close(fd)         _close(fd)
#else
#include <sys/stat.h>
#include <unistd.h>
#define cb_mkdir(path, mode) mkdir(path, mode)
#define cb_rmdir(path)       rmdir(path)
#define cb_unlink(path)      unlink(path)
#define cb_getpid()          getpid()
#define cb_dup(fd)           dup(fd)
#define cb_dup2(fd1, fd2)    dup2(fd1, fd2)
#define cb_close(fd)         close(fd)
#endif

/* --- strncasecmp compat --- */

#ifdef _WIN32
#define strncasecmp _strnicmp
#define strcasecmp  _stricmp
#endif

/* --- setenv/unsetenv compat --- */

int cb_setenv(const char *name, const char *value, int overwrite);
int cb_unsetenv(const char *name);

/* --- Config directory --- */

/* Returns the platform-appropriate config directory.
 * Linux/macOS: $HOME/.config
 * Windows: %APPDATA% (or %USERPROFILE%\AppData\Roaming)
 * Returns NULL if no suitable directory is found. */
const char *cb_config_dir(void);

/* --- Base64 encoding/decoding --- */

char *base64_encode(const unsigned char *data, size_t len);
unsigned char *base64_decode(const char *str, size_t *out_len);

/* --- URL encoding for query parameters --- */

char *url_encode(const char *str);

#endif /* CB_COMPAT_H */
