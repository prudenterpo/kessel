#ifndef KS_NET_H
#define KS_NET_H

#include <stddef.h>
#include <stdint.h>

enum {
    KS_NET_ACCEPT_ERROR = -1,
    KS_NET_ACCEPT_WOULD_BLOCK = -2
};

typedef enum ks_io_status {
    KS_IO_ERROR = -1,
    KS_IO_CLOSED = 0,
    KS_IO_OK = 1,
    KS_IO_WOULD_BLOCK = 2
} ks_io_status;

int ks_listen(const char* host, uint16_t port);
int ks_accept(int listen_fd);
int ks_set_nonblock(int fd);
int ks_close(int fd);

ks_io_status ks_recv(int fd, void* buf, size_t len, size_t* received);
ks_io_status ks_send(int fd, const void* buf, size_t len, size_t* sent);

#endif
