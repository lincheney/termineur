#include <glib-unix.h>
#include "client.h"
#include "socket.h"
#include "config.h"
#include "server.h"

int client_pipe_over_sock(GSocket* sock, char* value) {
    // send them all over
    int result = sock_send_all(sock, CONNECT_SOCK, sizeof(CONNECT_SOCK)-1);
    if (result) result = sock_send_all(sock, value, strlen(value)+1);
    if (!result) return 1;

    // now read connect up stdin, stdout

    // stdout
    GSource* source = g_socket_create_source(sock, G_IO_IN | G_IO_ERR, NULL);
    g_source_set_callback(source, (GSourceFunc)dump_socket_to_fd, GINT_TO_POINTER(STDOUT_FILENO), (GDestroyNotify)gtk_main_quit);
    g_source_attach(source, NULL);

    // stdin
    g_unix_fd_add(STDIN_FILENO, G_IO_IN | G_IO_ERR | G_IO_HUP, (GUnixFDSourceFunc)dump_fd_to_socket, sock);

    gtk_main();
    return 0;
}

int client_send_line(GSocket* sock, char* line, Buffer* buffer) {
    GError* error = NULL;
    line = g_strescape(line, "\"");
    int len = strlen(line) + 1;
    int result = sock_send_all(sock, line, len);
    free(line);
    if (! result) {
        return 1;
    }

    /* get the response */
    while (1) {
        len = g_socket_receive(sock, buffer->data + buffer->used, buffer->reserved - buffer->used, NULL, &error);
        if (len < 0) {
            g_warning("Failed to recv(): %s", error->message);
            g_error_free(error);
            return 1;
        }

        if (len == 0) {
            // unexpected eof
            g_warning("Unexpected EOF");
            return 1;
        }

        char* end = memchr(buffer->data + buffer->used, 0, len);

        /* dump existing buffer */
        int size = end ? (end - buffer->data) : (buffer->used + len);
        if (write_to_fd(STDOUT_FILENO, buffer->data, size)) {
            return 1;
        }

        if (end) {
            // end of payload
            buffer->used += len;
            buffer_shift_back(buffer, end - buffer->data + 1);
            break;
        }

        buffer->used = 0;
    }
    return 0;
}

int run_client(GSocket* sock, char** commands, int argc, char** argv, char* sock_connect) {
    Buffer* buffer = buffer_new(1024);

    /* do any --command actions */
    for (char** line = commands; *line; line++) {
        client_send_line(sock, *line, buffer);
    }

    /* open new tab/window with remaining commands */
    if ((! commands[0] || argc > 0) && ! sock_connect) {
        char* quoted_argv[argc+3];
        quoted_argv[argc+2] = NULL;

        // command
        quoted_argv[0] = default_open_action;

        // cwd
        char* cwd = g_get_current_dir();
        char* quoted_cwd = g_shell_quote(cwd);
        free(cwd);
        quoted_argv[1] = alloca(sizeof(char) * (strlen(quoted_cwd) + 5));
        strcpy(quoted_argv[1], "cwd=");
        strcpy(quoted_argv[1]+sizeof("cwd=")-1, quoted_cwd);
        free(quoted_cwd);

        // argv
        for (int i = 0; i < argc; i++) {
            quoted_argv[i+2] = g_shell_quote(argv[i]);
        }

        char* line = g_strjoinv(" ", quoted_argv);
        for (int i = 0; i < argc; i++) {
            free(quoted_argv[i+2]); // first 2 strings are static
        }
        *strchr(line, ' ') = ':';
        client_send_line(sock, line, buffer);
        free(line);
    }

    /* discard anything left in the buffer */
    buffer_free(buffer);

    if (sock_connect) {
        return client_pipe_over_sock(sock, sock_connect);
    }

    return 0;
}
