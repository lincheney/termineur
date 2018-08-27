#include <gtk/gtk.h>

#include "config.h"
#include "window.h"
#include "socket.h"
#include "utils.h"
#include "server.h"
#include "client.h"

char* commands[255];

void print_help(int argc, char** argv) {
    fprintf(stderr,
            "usage: %s [-i|--id ID] [-c|--config CONFIG] [-C|--command COMMAND] [ARGS...]\n" \
            "       %s -h|--help\n" \
        , argv[0], argv[0]);
}

char** parse_args(int* argc, char** argv) {

#define MATCH_FLAG(flag, dest) \
        if (STR_STARTSWITH(argv[i], (flag))) { \
            if (argv[i][sizeof(flag)-1] == '=') { \
                /* -f=value */ \
                dest = argv[i] + sizeof(flag); \
            } else if (argv[i][1] != '-' && argv[i][sizeof(flag)-1]) { \
                /* -fvalue */ \
                dest = argv[i] + sizeof(flag) - 1; \
            } else if (i + 1 < *argc) { \
                /* -f value */ \
                i ++; \
                dest = argv[i]; \
            } else { \
                print_help(*argc, argv); \
                fprintf(stderr, "%s: expected one argument\n", flag); \
                exit(1); \
            } \
            continue; \
        }

    int i, command_ix = -1;
    // skip arg0
    for (i = 1; i < *argc; i ++) {
        if (STR_EQUAL(argv[i], "-h") || STR_EQUAL(argv[i], "--help")) {
            print_help(*argc, argv);
            exit(0);
        }

        MATCH_FLAG("-c", config_filename);
        MATCH_FLAG("--config", config_filename);
        MATCH_FLAG("-i", app_id);
        MATCH_FLAG("--id", app_id);
        MATCH_FLAG("-C", command_ix ++; commands[command_ix]);
        MATCH_FLAG("--command", command_ix ++; commands[command_ix]);
        if (STR_EQUAL(argv[i], "--")) {
            i ++;
        }
        break;
    }

    command_ix ++;
    commands[command_ix] = 0;
    *argc -= i;
    if (! argc) return NULL;
    return argv+i;
}

char* make_app_id() {
    char buffer[256];
    if (! app_id) {
        const char* id = g_getenv(APP_PREFIX "_ID");
        if (id) {
            app_id = strdup(id);
        }
    }

    if (! app_id) {
        const char* display = gdk_display_get_name(gdk_display_get_default());
        snprintf(buffer, sizeof(buffer), APP_PREFIX ".x%s", display+1);
        app_id = strndup(buffer, sizeof(buffer));
    } else if (STR_EQUAL(app_id, "")) {
        app_id = NULL;
    }

    if (app_id) {
        g_setenv(APP_PREFIX "_ID", app_id, TRUE);
    }

    return app_id;
}

int main(int argc, char *argv[]) {
    int status = 0;
    gtk_init(&argc, &argv);
    argv = parse_args(&argc, argv);

    app_id = make_app_id();
    if (! app_id) {
        run_server(argc, argv);
        return 0;
    }

    GSocket* sock;
    GSocketAddress* addr;
    if (! make_sock(app_id, &sock, &addr)) {
        return 1;
    }

    status = commands[0] ? 0 : try_bind_sock(sock, addr, (GSourceFunc)server_recv);
    if (status > 0) {
        status = run_server(argc, argv);
    } else if (status < 0) {
        return 1;
    } else if (connect_sock(sock, addr) >= 0) {
        status = run_client(sock, commands, argc, argv);
    }
    close_socket(sock);

    return status;
}
