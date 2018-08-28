#ifndef SERVER_H
#define SERVER_H

#include <gio/gio.h>
#include "socket.h"

#define CONNECT_SOCK "CONNECT_SOCK:"

int server_recv(GSocket* sock, GIOCondition io, Buffer* buffer);
int run_server(int argc, char** argv);

#endif

