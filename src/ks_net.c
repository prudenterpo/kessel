#include "ks_net.h"
#include "ks_log.h"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
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

#include <string.h>
#include <stdio.h>

static int ks_close(int fd) {
#if defined(_WIN32)
    return closesocket(fd);
#else
    return close(fd);
#endif
}

int ks_set_nonblock(int fd) {
#if defined(_WIN32)
    u_long m = 1;
    return ioctlsocket(fd, FIONBIO, &m) == 0 ? 0 : -1;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

int ks_listen(const char* host, uint16_t port) {
#if defined(_WIN32)
    WSADATA w;
    WSAStartup(MAKEWORD(2,2), &w);
#endif
    int fd = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        ks_log_err("socket failed");
        return -1;
    }

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(host);

    if (bind(fd, (const struct sockaddr*)&addr, sizeof(addr)) != 0) {
        ks_log_err("bind failed");
        ks_close(fd);
        return -1;
    }
    if (listen(fd, 128) != 0) {
        ks_log_err("listen failed");
        ks_close(fd);
        return -1;
    }
    return fd;
}

int ks_accept(int listen_fd) {
    struct sockaddr_in caddr;
    socklen_t clen = sizeof(caddr);
    int cfd = (int)accept(listen_fd, (struct sockaddr*)&caddr, &clen);
    if (cfd < 0) return -1;
    return cfd;
}

int ks_readline(int fd, char* out, size_t cap) {
    size_t n = 0;
    while (n + 1 < cap) {
        char c;
#if defined(_WIN32)
        int r = recv(fd, &c, 1, 0);
#else
        int r = (int)recv(fd, &c, 1, 0);
#endif
        if (r == 0) return -1;
        if (r < 0) return -1;
        out[n++] = c;
        if (c == '\n') {
            if (n >= 2 && out[n-2] == '\r') {
                out[n-2] = 0;
                return (int)(n-2);
            }
            out[n-1] = 0;
            return (int)(n-1);
        }
    }
    return -1;
}

int ks_writeall(int fd, const void* buf, size_t len) {
    const char* p = (const char*)buf;
    size_t off = 0;
    while (off < len) {
#if defined(_WIN32)
        int w = send(fd, p + off, (int)(len -off), 0);
#else
        ssize_t w = send(fd, p + off, len - off, 0);
#endif
        if (w <= 0) return -1;
        off += (size_t)w;
    }
    return 0;
}
