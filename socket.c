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
    Buffer* buffer = malloc(sizeof(Buffer));
    buffer->used = 0;
    buffer->reserved = 0;
    buffer->data = NULL;
    buffer_reserve(buffer, size);
    return buffer;
}

#define READ_SIZE 1024

int sock_recv(GSocket* sock, GIOCondition io, Buffer* buffer) {
    if (io & G_IO_IN) {
        GError* error = NULL;
        if (buffer->reserved - buffer->used < READ_SIZE) {
            buffer_reserve(buffer, buffer->reserved+READ_SIZE);
        }

        int len = g_socket_receive(sock, buffer->data + buffer->used, READ_SIZE, NULL, &error);
        buffer->used += len;

        if (len < 0) {
            g_warning("Failed to recv(): %s\n", error->message);
            g_error_free(error);

        } else if (len >= 0) {
            char* start = buffer->data + buffer->used - len;

            while (buffer->used > 0) {
                char* end = buffer->data + buffer->used;
                // search for \0 or \n
                char* ptr;
                for (ptr = start; ptr < end && *ptr != 0 && *ptr != '\n'; ptr++) ;
                // no terminator found
                if (ptr == end) break;

                *ptr = '\0'; // end of line
                void* data = execute_line(buffer->data, ptr - buffer->data, TRUE);
                if (data) {
                    sock_send_all(sock, data, strlen(data)+1);
                } else {
                    sock_send_all(sock, "", 1);
                }
                free(data);

                // shift by length of line
                buffer_shift_back(buffer, end - buffer->data + 1);
                // search from beginning now
                start = buffer->data;
            }

            if (len == 0) {
                // leftovers in the buffer
                if (buffer->used > 0) {
                    g_warning("Unprocessed buffer contents, %i bytes remaining", buffer->used);
                }
                close_socket(sock);
                return G_SOURCE_REMOVE;
            }
        }
    }

    if (io & G_IO_ERR) {
        close_socket(sock);
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

int accept_connection(GSocket* sock, GIOCondition io) {
    GError* error = NULL;
    sock = g_socket_accept(sock, NULL, &error);

    if (sock) {
        Buffer* buffer = buffer_new(READ_SIZE);
        GSource* source = g_socket_create_source(sock, G_IO_IN | G_IO_ERR, NULL);
        g_source_set_callback(source, (GSourceFunc)sock_recv, buffer, free);
        g_source_attach(source, NULL);
    } else {
        g_warning("Failed on accept(): %s\n", error->message);
        g_error_free(error);
    }
    return G_SOURCE_CONTINUE;
}

gboolean make_sock(const char* path, GSocket** sock, GSocketAddress** addr) {
    GError* error = NULL;
    *addr = g_unix_socket_address_new_with_type(path, -1, G_UNIX_SOCKET_ADDRESS_ABSTRACT);
    *sock = g_socket_new(g_socket_address_get_family(*addr), G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT, &error);

    if (! *sock) {
        g_warning("Failed to create socket: %s\n", error->message);
        g_error_free(error);
        return FALSE;
    }
    return TRUE;
}

int try_bind_sock(GSocket* sock, GSocketAddress* addr) {
    GError* error = NULL;
    if (! g_socket_bind(sock, addr, TRUE, &error)) {
        if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_ADDRESS_IN_USE)) {
            return 0;
        }

        g_warning("Failed on bind(): %s\n", error->message);
        g_error_free(error);
        return -1;
    }

    if (g_socket_listen(sock, &error)) {
        GSource* source = g_socket_create_source(sock, G_IO_IN, NULL);
        g_source_set_callback(source, (GSourceFunc)accept_connection, NULL, NULL);
        g_source_attach(source, NULL);
        return 1;
    }

    g_warning("Failed on listen(): %s\n", error->message);
    g_error_free(error);
    return -1;
}

int connect_sock(GSocket* sock, GSocketAddress* addr) {
    GError* error = NULL;
    if (g_socket_connect(sock, addr, NULL, &error)) {
        return 1;
    /* } else if (type == SOCK_SLAVE) { */
        /* // was a forced slave connection */
        /* g_print("No terminal is running\n"); */
    }

    g_warning("Failed on connect(): %s\n", error->message);
    g_error_free(error);
    return -1;
}

gboolean close_socket(GSocket* sock) {
    GError* error = NULL;
    if (! g_socket_close(sock, &error)) {
        g_warning("Failed to close socket: %s\n", error->message);
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
        int sent = g_socket_send(sock, buffer, size, NULL, &error);
        if (sent < 0) {
            g_warning("Failed on send(): %s\n", error->message);
            g_error_free(error);
            return FALSE;
        }
        size -= sent;
        buffer += sent;
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
            g_warning("Failed to recv(): %s\n", error->message);
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
