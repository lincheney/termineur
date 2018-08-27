#include <errno.h>
#include "client.h"
#include "socket.h"
#include "config.h"

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
            g_warning("Failed to recv(): %s\n", error->message);
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
        ssize_t written = 0;
        while (written < size) {
            int result = write(STDOUT_FILENO, buffer->data + written, size - written);
            if (result < 0 && errno == EPIPE) {
                break;
            }
            if (result < 0) {
                g_warning("Error writing to stdout");
                return 1;
            }
            written += result;
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

int run_client(GSocket* sock, char** commands, int argc, char** argv) {
    Buffer* buffer = buffer_new(1024);

    for (char** line = commands; *line; line++) {
        client_send_line(sock, *line, buffer);
    }

    if (! commands[0] || argc > 0) {
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

    buffer_free(buffer);
    return 0;
}
