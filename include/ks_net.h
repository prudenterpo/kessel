#ifndef KS_NET_H
#define KS_NET_H

#include <stdint.h>
#include <stddef.h>

int ks_listen(const char* host, uint16_t port);
int ks_accept(int listen_fd);
int ks_set_nonblock(int fd);

int ks_readline(int fd, char* out, size_t cap);

int ks_writeall(int fd, const void* buf, size_t len);

#endif
