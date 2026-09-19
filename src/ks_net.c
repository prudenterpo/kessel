#include "ks_net.h"
#include "ks_log.h"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <limits.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#endif

int ks_close(int fd) {
#if defined(_WIN32)
    return closesocket((SOCKET)fd) == 0 ? 0 : -1;
#else
    return close(fd);
#endif
}

static int ks_error_would_block(void) {
#if defined(_WIN32)
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

static int ks_error_interrupted(void) {
#if defined(_WIN32)
    return WSAGetLastError() == WSAEINTR;
#else
    return errno == EINTR;
#endif
}

int ks_set_nonblock(int fd) {
#if defined(_WIN32)
    u_long m = 1;
    return ioctlsocket((SOCKET)fd, FIONBIO, &m) == 0 ? 0 : -1;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

int ks_listen(const char* host, uint16_t port) {
#if defined(_WIN32)
    WSADATA w;
    if (WSAStartup(MAKEWORD(2, 2), &w) != 0) {
        ks_log_err("winsock startup failed");
        return -1;
    }
#endif

#if defined(_WIN32)
    SOCKET socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_fd == INVALID_SOCKET) {
#else
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
#endif
        ks_log_err("socket failed");
        return -1;
    }
    int fd = (int)socket_fd;

    int yes = 1;
#if defined(_WIN32)
    int option_len = (int)sizeof(yes);
#else
    socklen_t option_len = (socklen_t)sizeof(yes);
#endif
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes,
                   option_len) != 0) {
        ks_log_err("setsockopt failed");
        ks_close(fd);
        return -1;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (host == NULL || host[0] == '\0') {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        ks_log_err("invalid listen address");
        ks_close(fd);
        return -1;
    }

    if (bind(socket_fd, (const struct sockaddr*)&addr, sizeof(addr)) != 0) {
        ks_log_err("bind failed");
        ks_close(fd);
        return -1;
    }
    if (listen(socket_fd, 128) != 0) {
        ks_log_err("listen failed");
        ks_close(fd);
        return -1;
    }
    if (ks_set_nonblock(fd) != 0) {
        ks_log_err("failed to make listener non-blocking");
        ks_close(fd);
        return -1;
    }
    return fd;
}

int ks_accept(int listen_fd) {
    struct sockaddr_in caddr;
#if defined(_WIN32)
    int clen = (int)sizeof(caddr);
    SOCKET accepted_fd;
#else
    socklen_t clen = sizeof(caddr);
    int accepted_fd;
#endif

    do {
#if defined(_WIN32)
        accepted_fd = accept((SOCKET)listen_fd, (struct sockaddr*)&caddr, &clen);
#else
        accepted_fd = accept(listen_fd, (struct sockaddr*)&caddr, &clen);
#endif
    } while (
#if defined(_WIN32)
        accepted_fd == INVALID_SOCKET && ks_error_interrupted()
#else
        accepted_fd < 0 && ks_error_interrupted()
#endif
    );

#if defined(_WIN32)
    if (accepted_fd == INVALID_SOCKET) {
#else
    if (accepted_fd < 0) {
#endif
        return ks_error_would_block() ? KS_NET_ACCEPT_WOULD_BLOCK
                                      : KS_NET_ACCEPT_ERROR;
    }

    int cfd = (int)accepted_fd;
    if (ks_set_nonblock(cfd) != 0) {
        ks_close(cfd);
        return KS_NET_ACCEPT_ERROR;
    }
#if !defined(_WIN32) && defined(SO_NOSIGPIPE)
    int yes = 1;
    (void)setsockopt(cfd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
    return cfd;
}

ks_io_status ks_recv(int fd, void* buf, size_t len, size_t* received) {
    if (received != NULL) {
        *received = 0;
    }
    if (len == 0) {
        return KS_IO_OK;
    }

    for (;;) {
#if defined(_WIN32)
        int chunk = len > (size_t)INT_MAX ? INT_MAX : (int)len;
        int result = recv((SOCKET)fd, buf, chunk, 0);
#else
        ssize_t result = recv(fd, buf, len, 0);
#endif
        if (result > 0) {
            if (received != NULL) {
                *received = (size_t)result;
            }
            return KS_IO_OK;
        }
        if (result == 0) {
            return KS_IO_CLOSED;
        }
        if (ks_error_interrupted()) {
            continue;
        }
        return ks_error_would_block() ? KS_IO_WOULD_BLOCK : KS_IO_ERROR;
    }
}

ks_io_status ks_send(int fd, const void* buf, size_t len, size_t* sent) {
    if (sent != NULL) {
        *sent = 0;
    }
    if (len == 0) {
        return KS_IO_OK;
    }

    for (;;) {
#if defined(_WIN32)
        int chunk = len > (size_t)INT_MAX ? INT_MAX : (int)len;
        int result = send((SOCKET)fd, (const char*)buf, chunk, 0);
#else
        int flags = 0;
#if defined(MSG_NOSIGNAL)
        flags = MSG_NOSIGNAL;
#endif
        ssize_t result = send(fd, buf, len, flags);
#endif
        if (result > 0) {
            if (sent != NULL) {
                *sent = (size_t)result;
            }
            return KS_IO_OK;
        }
        if (result == 0) {
            return KS_IO_CLOSED;
        }
        if (ks_error_interrupted()) {
            continue;
        }
        return ks_error_would_block() ? KS_IO_WOULD_BLOCK : KS_IO_ERROR;
    }
}
