#include "server.h"
#include "socket.h"
#include "config.h"
#include "window.h"

int server_recv(GSocket* sock, GIOCondition io, Buffer* buffer) {
    if (io & G_IO_IN) {
        GError* error = NULL;
        if (buffer->reserved - buffer->used < BUFFER_DEFAULT_SIZE) {
            buffer_reserve(buffer, buffer->reserved+BUFFER_DEFAULT_SIZE);
        }

        int len = g_socket_receive(sock, buffer->data + buffer->used, BUFFER_DEFAULT_SIZE, NULL, &error);
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
