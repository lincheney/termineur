#include <glib-unix.h>
#include "server.h"
#include "socket.h"
#include "config.h"
#include "window.h"
#include "utils.h"
#include "action.h"

void finalise_pipe_socket(GSocket* sock) {
    int stdout = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(sock), "stdout"));
    // flush all stdout; set to nonblock first
    if (stdout >= 0 && g_unix_set_fd_nonblocking(stdout, TRUE, NULL)) {
        while (dump_fd_to_socket(stdout, G_IO_IN, sock)) ;
        close(stdout);
    }
    close_socket(sock);
}

void server_pipe_over_socket(GSocket* sock, char* value, Buffer* remainder) {
    if (strlen(value) < 2) {
        g_warning("Invalid connection format: %s", value);
        return;
    }

    char flag = value[0] - '0';
    int connect_stdin = flag & 1;
    int connect_stdout = flag & 2;
    value += 2;

    char* copy = strdup(value);
    Action action = lookup_action(copy);
    free(copy);

    ConnectActionFunc func = (ConnectActionFunc)action.func;
    if (
            func != (ConnectActionFunc)new_tab
            && func != (ConnectActionFunc)new_window
            && func != (ConnectActionFunc)split_left
            && func != (ConnectActionFunc)split_right
            && func != (ConnectActionFunc)split_above
            && func != (ConnectActionFunc)split_below
    ) {
        g_warning("Invalid connection action: %s", value);
        return;
    }

    VteTerminal* terminal = get_active_terminal(NULL);
    if (terminal) {
        char* data = NULL;
        int pipes[2] = {connect_stdin, connect_stdout};
        int* ptr = pipes;

        GtkWidget* widget = func(terminal, action.data, &ptr);
        if (action.cleanup) {
            action.cleanup(action.data);
        }
        free(data);

        if (widget) {
            /*
             * connect up:
             *      socket read -> pipes[0]
             *      pipes[1]    -> socket write
             * keep socket read closes, close pipes[0] but keep the socket open
             *
             * the client knows the process is still running so long as the socket is open
             */

            if (pipes[0] >= 0) {
                write_to_fd(pipes[0], remainder->data, remainder->used);
                GSource* source = g_socket_create_source(sock, G_IO_IN | G_IO_ERR | G_IO_HUP, NULL);
                g_source_set_callback(source, (GSourceFunc)dump_socket_to_fd, GINT_TO_POINTER(pipes[0]), (GDestroyNotify)close);
                g_source_attach(source, NULL);
            }

            if (pipes[1] >= 0) {
                g_unix_fd_add_full(
                        G_PRIORITY_DEFAULT, pipes[1],
                        G_IO_IN | G_IO_ERR | G_IO_HUP,
                        (GUnixFDSourceFunc)dump_fd_to_socket, sock, NULL
                );
            }

            // close everything when terminal exits
            g_object_set_data(G_OBJECT(sock), "stdout", GINT_TO_POINTER(pipes[1]));
            terminal = g_object_get_data(G_OBJECT(widget), "terminal");
            g_signal_connect_swapped(terminal, "destroy", G_CALLBACK(finalise_pipe_socket), sock);
        }
    }
}

int server_recv(GSocket* sock, GIOCondition io, Buffer* buffer) {
    if (io & G_IO_IN) {
        GError* error = NULL;
        if (buffer->reserved - buffer->used < BUFFER_DEFAULT_SIZE) {
            buffer_reserve(buffer, buffer->reserved+BUFFER_DEFAULT_SIZE);
        }

        int len = g_socket_receive(sock, buffer->data + buffer->used, BUFFER_DEFAULT_SIZE, NULL, &error);
        buffer->used += len;

        if (len < 0) {
            g_warning("Failed to recv(): %s", error->message);
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
                char *sock_connect;
                if ((sock_connect = STR_STRIP_PREFIX(buffer->data, CONNECT_SOCK))) {
                    // dup as the shift below will invalidate the data
                    sock_connect = strdup(sock_connect);
                    // shift by length of line
                    buffer_shift_back(buffer, ptr - buffer->data + 1);
                    server_pipe_over_socket(sock, sock_connect, buffer);
                    free(sock_connect);

                    return G_SOURCE_REMOVE;
                }

                void* data = execute_line(buffer->data, ptr - buffer->data, TRUE, TRUE);
                int result;
                if (data) {
                    result = sock_send_all(sock, data, strlen(data)+1);
                } else {
                    result = sock_send_all(sock, "", 1);
                }
                free(data);

                if (! result) {
                    return 1;
                }

                // shift by length of line
                buffer_shift_back(buffer, ptr - buffer->data + 1);
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

int run_server(int argc, char** argv) {
    GtkCssProvider* css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css_provider, GLOBAL_CSS, -1, NULL);
    GdkScreen* screen = gdk_screen_get_default();
    gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);

    load_config(config_filename);
    GtkWidget* window = make_new_window_full(NULL, NULL, argc, argv);
    trigger_action(get_active_terminal(window), EVENT_KEY, START_EVENT);

    g_unix_signal_add(SIGINT, (GSourceFunc)gtk_main_quit, NULL);
    gtk_main();
    return 0;
}
