#include <errno.h>
#include "socket.h"
#include "config.h"

void buffer_shift_back(Buffer* buffer, int offset) {
    int length = buffer->used - offset;
    if (length > 0) {
        memmove(buffer->data, buffer->data + offset, length);
    }
    memset(buffer->data + length, 0, buffer->used - length);
    buffer->used = length;
}

void buffer_reserve(Buffer* buffer, int size) {
    buffer->data = realloc(buffer->data, size+1);
    if (buffer->reserved < size) {
        memset(buffer->data + buffer->reserved+1, 0, size - buffer->reserved);
    }
    buffer->reserved = size;
}

Buffer* buffer_new(int size) {
    size = size ? size : BUFFER_DEFAULT_SIZE;
    Buffer* buffer = malloc(sizeof(Buffer));
    buffer->used = 0;
    buffer->reserved = 0;
    buffer->data = NULL;
    buffer_reserve(buffer, size);
    return buffer;
}

void buffer_free(Buffer* buffer) {
    free(buffer->data);
    free(buffer);
}

int write_to_fd(int fd, char* buffer, ssize_t size) {
    ssize_t written = 0;
    while (written < size) {
        int result = write(fd, buffer + written, size - written);
        if (result < 0 && errno == EPIPE) {
            return 0;
        }
        if (result < 0) {
            g_warning("Error writing to %i: %s", fd, strerror(errno));
            return -1;
        }
        written += result;
    }
    return size;
}

int dump_socket_to_fd(GSocket* sock, GIOCondition io, int fd) {
    if (io & G_IO_IN) {
        GError* error = NULL;
        char buffer[BUFFER_DEFAULT_SIZE];

        int len = g_socket_receive(sock, buffer, sizeof(buffer), NULL, &error);
        if (len < 0) {
            g_warning("Failed to recv(): %s", error->message);
            g_error_free(error);
        } else if (len == 0) {
            shutdown_socket(sock, TRUE, FALSE);
            return G_SOURCE_REMOVE;
        } else if (write_to_fd(fd, buffer, len) == 0) {
            // fd is closed
            return G_SOURCE_REMOVE;
        }
    }

    if (io & G_IO_ERR) {
        g_warning("error on socket");
        shutdown_socket(sock, TRUE, FALSE);
        return G_SOURCE_REMOVE;
    }

    if (io & (G_IO_HUP | G_IO_NVAL)) {
        shutdown_socket(sock, TRUE, FALSE);
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

gboolean dump_fd_to_socket(int fd, GIOCondition io, GSocket* sock) {
    if (io & G_IO_IN) {
        char buffer[BUFFER_DEFAULT_SIZE];
        ssize_t len = read(fd, buffer, sizeof(buffer));
        if (len < 0) {
            if (errno != EAGAIN) {
                g_warning("Failed to read from %i: %s", fd, strerror(errno));
            }
            return G_SOURCE_REMOVE;
        }
        if (len == 0) {
            // closed
            return G_SOURCE_REMOVE;
        }
        if (sock_send_all(sock, buffer, len) <= 0) {
            return G_SOURCE_REMOVE;
        }
    }

    if (io & G_IO_ERR) {
        g_warning("error on %i", fd);
        close(fd);
        return G_SOURCE_REMOVE;
    }

    if (io & (G_IO_HUP | G_IO_NVAL)) {
        close(fd);
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

int accept_connection(GSocket* sock, GIOCondition io, GSourceFunc callback) {
    GError* error = NULL;
    sock = g_socket_accept(sock, NULL, &error);

    if (sock) {
        Buffer* buffer = buffer_new(0);
        GSource* source = g_socket_create_source(sock, G_IO_IN | G_IO_ERR, NULL);
        g_source_set_callback(source, callback, buffer, (GDestroyNotify)buffer_free);
        g_source_attach(source, NULL);
    } else {
        g_warning("Failed on accept(): %s", error->message);
        g_error_free(error);
    }
    return G_SOURCE_CONTINUE;
}

gboolean make_sock(const char* path, GSocket** sock, GSocketAddress** addr) {
    GError* error = NULL;
    *addr = g_unix_socket_address_new_with_type(path, -1, G_UNIX_SOCKET_ADDRESS_ABSTRACT);
    *sock = g_socket_new(g_socket_address_get_family(*addr), G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT, &error);

    if (! *sock) {
        g_warning("Failed to create socket: %s", error->message);
        g_error_free(error);
        return FALSE;
    }
    return TRUE;
}

int try_bind_sock(GSocket* sock, GSocketAddress* addr, GSourceFunc callback) {
    GError* error = NULL;
    if (! g_socket_bind(sock, addr, TRUE, &error)) {
        if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_ADDRESS_IN_USE)) {
            return 0;
        }

        g_warning("Failed on bind(): %s", error->message);
        g_error_free(error);
        return -1;
    }

    if (g_socket_listen(sock, &error)) {
        GSource* source = g_socket_create_source(sock, G_IO_IN, NULL);
        g_source_set_callback(source, (GSourceFunc)accept_connection, callback, NULL);
        g_source_attach(source, NULL);
        return 1;
    }

    g_warning("Failed on listen(): %s", error->message);
    g_error_free(error);
    return -1;
}

int connect_sock(GSocket* sock, GSocketAddress* addr) {
    GError* error = NULL;
    if (g_socket_connect(sock, addr, NULL, &error)) {
        return 1;
    }

    g_warning("Failed on connect(): %s", error->message);
    g_error_free(error);
    return -1;
}

gboolean shutdown_socket(GSocket* sock, gboolean shutdown_read, gboolean shutdown_write) {
    GError* error = NULL;
    if (! g_socket_shutdown(sock, shutdown_read, shutdown_write, &error)) {
        if (error->domain != G_IO_ERROR || error->code != G_IO_ERROR_CLOSED) {
            g_warning("Failed to shutdown socket: %s", error->message);
        }
        g_error_free(error);
        return FALSE;
    }
    return TRUE;
}

gboolean close_socket(GSocket* sock) {
    GError* error = NULL;
    if (! g_socket_close(sock, &error)) {
        g_warning("Failed to close socket: %s", error->message);
        g_error_free(error);
        g_object_unref(sock);
        return FALSE;
    }
    g_object_unref(sock);
    return TRUE;
}

gboolean sock_send_all(GSocket* sock, char* buffer, int size) {
    GError* error = NULL;
    while (size > 0) {
        int result = g_socket_send(sock, buffer, size, NULL, &error);
        if (result < 0) {
            if (error->domain != G_IO_ERROR || error->code != G_IO_ERROR_BROKEN_PIPE) {
                g_warning("Failed on send(): %s", error->message);
            }
            g_error_free(error);
            close_socket(sock);
            return FALSE;
        }
        size -= result;
        buffer += result;
    }
    return TRUE;
}

char* sock_recv_until_null(GSocket* sock) {
    GError* error = NULL;
    int total_size = 1024;
    int size = 0;
    char* buffer = malloc(sizeof(char) * total_size);
    int len = -1;

    while (len) {
        if (size >= total_size) {
            total_size *= 2;
            buffer = realloc(buffer, sizeof(char) * total_size);
        }

        len = g_socket_receive(sock, buffer + size, total_size - size, NULL, &error);
        if (len < 0) {
            g_warning("Failed to recv(): %s", error->message);
            g_error_free(error);
            return NULL;
        }

        size += len;
        if (memchr(buffer + size - len, 0, len)) {
            break;
        }
    }

    return buffer;
}
