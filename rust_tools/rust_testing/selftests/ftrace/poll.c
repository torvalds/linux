// SPDX-License-Identifier: GPL-2.0
/*
 * Simple poll on a file.
 *
 * Copyright (c) 2024 Google LLC.
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUFSIZE 4096

/*
 * Usage:
 *  poll [-I|-P] [-t timeout] FILE
 */
int main(int argc, char *argv[])
{
	struct pollfd pfd = {.events = POLLIN};
	char buf[BUFSIZE];
	int timeout = -1;
	int ret, opt;

	while ((opt = getopt(argc, argv, "IPt:")) != -1) {
		switch (opt) {
		case 'I':
			pfd.events = POLLIN;
			break;
		case 'P':
			pfd.events = POLLPRI;
			break;
		case 't':
			timeout = atoi(optarg);
			break;
		default:
			fprintf(stderr, "Usage: %s [-I|-P] [-t timeout] FILE\n",
				argv[0]);
			return -1;
		}
	}
	if (optind >= argc) {
		fprintf(stderr, "Error: Polling file is not specified\n");
		return -1;
	}

	pfd.fd = open(argv[optind], O_RDONLY);
	if (pfd.fd < 0) {
		fprintf(stderr, "failed to open %s", argv[optind]);
		perror("open");
		return -1;
	}

	/* Reset poll by read if POLLIN is specified. */
	if (pfd.events & POLLIN)
		do {} while (read(pfd.fd, buf, BUFSIZE) == BUFSIZE);

	ret = poll(&pfd, 1, timeout);
	if (ret < 0 && errno != EINTR) {
		perror("poll");
		return -1;
	}
	close(pfd.fd);

	/* If timeout happned (ret == 0), exit code is 1 */
	if (ret == 0)
		return 1;

	return 0;
}
