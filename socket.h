#ifndef SOCKET_H
#define SOCKET_H

#include <gio/gunixsocketaddress.h>

#define SOCK_PRIMARY 1
#define SOCK_SLAVE 2
#define SOCK_AUTO 3
#define SOCK_FAIL -1

typedef struct buffer {
    int size;
    char data[2048];
} Buffer;

gboolean make_sock(const char* path, GSocket** sock, GSocketAddress** addr);
int try_bind_sock(GSocket* sock, GSocketAddress* addr);
int connect_sock(GSocket* sock, GSocketAddress* addr);
gboolean close_socket(GSocket* sock);
gboolean sock_send_all(GSocket* sock, char* buffer, int size);
char* sock_recv_until_null(GSocket* sock);

#endif
