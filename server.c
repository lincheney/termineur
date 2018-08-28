#include <glib-unix.h>
#include "server.h"
#include "socket.h"
#include "config.h"
#include "window.h"
#include "utils.h"
#include "action.h"

void pipe_is_closed(GSocket* sock) {
    g_object_set_data(G_OBJECT(sock), "stdin", GINT_TO_POINTER(-1));
    g_object_set_data(G_OBJECT(sock), "stdout", GINT_TO_POINTER(-1));
}

void finalise_pipe_socket(GSocket* sock) {
    int stdin = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(sock), "stdin"));
    int stdout = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(sock), "stdout"));
    // flush all stdout; set to nonblock first
    if (stdout >= 0 && g_unix_set_fd_nonblocking(stdout, TRUE, NULL)) {
        while (dump_fd_to_socket(stdout, G_IO_IN, sock)) ;
        close(stdout);
    }
    if (stdin >= 0) {
        close(stdin);
    }
    close_socket(sock);
}

void server_pipe_over_socket(GSocket* sock, char* value, Buffer* remainder) {
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
        int pipes[2];
        int* ptr = pipes;

        GtkWidget* widget = func(terminal, action.data, &ptr);
        if (action.cleanup) {
            action.cleanup(action.data);
        }
        free(data);

        if (widget) {
            write_to_fd(pipes[0], remainder->data, remainder->used);

            // monitor the pipes
            GSource* source = g_socket_create_source(sock, G_IO_IN | G_IO_ERR | G_IO_HUP, NULL);
            g_source_set_callback(source, (GSourceFunc)dump_socket_to_fd, GINT_TO_POINTER(pipes[0]), NULL);
            g_source_attach(source, NULL);

            g_unix_fd_add_full(G_PRIORITY_DEFAULT, pipes[1], G_IO_IN | G_IO_ERR | G_IO_HUP, (GUnixFDSourceFunc)dump_fd_to_socket, sock, (GDestroyNotify)pipe_is_closed);

            // close everything when terminal exits
            g_object_set_data(G_OBJECT(sock), "stdin", GINT_TO_POINTER(pipes[0]));
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

                void* data = execute_line(buffer->data, ptr - buffer->data, TRUE);
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
    if (! config_filename) {
        config_filename = g_build_filename(g_get_user_config_dir(), "vte_terminal", "config.ini", NULL);
    }
    GtkCssProvider* css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css_provider, GLOBAL_CSS, -1, NULL);
    GdkScreen* screen = gdk_screen_get_default();
    gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);

    load_config(config_filename);
    make_new_window_full(NULL, NULL, argc, argv);

    gtk_main();
    return 0;
}
