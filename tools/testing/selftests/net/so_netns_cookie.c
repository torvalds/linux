// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <sched.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>

#ifndef SO_NETNS_COOKIE
#define SO_NETNS_COOKIE 71
#endif

#define pr_err(fmt, ...) \
	({ \
		fprintf(stderr, "%s:%d:" fmt ": %m\n", \
			__func__, __LINE__, ##__VA_ARGS__); \
		1; \
	})

int main(int argc, char *argvp[])
{
	uint64_t cookie1, cookie2;
	socklen_t vallen;
	int sock1, sock2;

	sock1 = socket(AF_INET, SOCK_STREAM, 0);
	if (sock1 < 0)
		return pr_err("Unable to create TCP socket");

	vallen = sizeof(cookie1);
	if (getsockopt(sock1, SOL_SOCKET, SO_NETNS_COOKIE, &cookie1, &vallen) != 0)
		return pr_err("getsockopt(SOL_SOCKET, SO_NETNS_COOKIE)");

	if (!cookie1)
		return pr_err("SO_NETNS_COOKIE returned zero cookie");

	if (unshare(CLONE_NEWNET))
		return pr_err("unshare");

	sock2 = socket(AF_INET, SOCK_STREAM, 0);
	if (sock2 < 0)
		return pr_err("Unable to create TCP socket");

	vallen = sizeof(cookie2);
	if (getsockopt(sock2, SOL_SOCKET, SO_NETNS_COOKIE, &cookie2, &vallen) != 0)
		return pr_err("getsockopt(SOL_SOCKET, SO_NETNS_COOKIE)");

	if (!cookie2)
		return pr_err("SO_NETNS_COOKIE returned zero cookie");

	if (cookie1 == cookie2)
		return pr_err("SO_NETNS_COOKIE returned identical cookies for distinct ns");

	close(sock1);
	close(sock2);
	return 0;
}
