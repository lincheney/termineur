#ifndef SOCKET_H
#define SOCKET_H

#include <gio/gunixsocketaddress.h>

#define SOCK_PRIMARY 1
#define SOCK_SLAVE 2
#define SOCK_AUTO 3
#define SOCK_FAIL -1

typedef struct buffer {
    int used;
    int reserved;
    char* data;
} Buffer;
#define BUFFER_DEFAULT_SIZE 1024

void buffer_shift_back(Buffer* buffer, int offset);
void buffer_reserve(Buffer* buffer, int size);
Buffer* buffer_new(int size);
void buffer_free(Buffer*);

int write_to_fd(int fd, char* buffer, ssize_t size);
int dump_socket_to_fd(GSocket* sock, GIOCondition io, int fd);
gboolean dump_fd_to_socket(GIOChannel* source, GIOCondition condition, GSocket* sock);
gboolean make_sock(const char* path, GSocket** sock, GSocketAddress** addr);
int try_bind_sock(GSocket* sock, GSocketAddress* addr, GSourceFunc callback);
int connect_sock(GSocket* sock, GSocketAddress* addr);
gboolean close_socket(GSocket* sock);
gboolean sock_send_all(GSocket* sock, char* buffer, int size);
char* sock_recv_until_null(GSocket* sock);

#endif
