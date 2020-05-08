/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NETWORK_HELPERS_H
#define __NETWORK_HELPERS_H
#include <sys/socket.h>
#include <sys/types.h>

int start_server(int family, int type);
int connect_to_fd(int family, int type, int server_fd);

#endif
