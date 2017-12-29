/* Control socket for client/server test execution
 *
 * Copyright (C) 2017 Red Hat, Inc.
 *
 * Author: Stefan Hajnoczi <stefanha@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

/* The client and server may need to coordinate to avoid race conditions like
 * the client attempting to connect to a socket that the server is not
 * listening on yet.  The control socket offers a communications channel for
 * such coordination tasks.
 *
 * If the client calls control_expectln("LISTENING"), then it will block until
 * the server calls control_writeln("LISTENING").  This provides a simple
 * mechanism for coordinating between the client and the server.
 */

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "timeout.h"
#include "control.h"

static int control_fd = -1;

/* Open the control socket, either in server or client mode */
void control_init(const char *control_host,
		  const char *control_port,
		  bool server)
{
	struct addrinfo hints = {
		.ai_socktype = SOCK_STREAM,
	};
	struct addrinfo *result = NULL;
	struct addrinfo *ai;
	int ret;

	ret = getaddrinfo(control_host, control_port, &hints, &result);
	if (ret != 0) {
		fprintf(stderr, "%s\n", gai_strerror(ret));
		exit(EXIT_FAILURE);
	}

	for (ai = result; ai; ai = ai->ai_next) {
		int fd;
		int val = 1;

		fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (fd < 0)
			continue;

		if (!server) {
			if (connect(fd, ai->ai_addr, ai->ai_addrlen) < 0)
				goto next;
			control_fd = fd;
			printf("Control socket connected to %s:%s.\n",
			       control_host, control_port);
			break;
		}

		if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
			       &val, sizeof(val)) < 0) {
			perror("setsockopt");
			exit(EXIT_FAILURE);
		}

		if (bind(fd, ai->ai_addr, ai->ai_addrlen) < 0)
			goto next;
		if (listen(fd, 1) < 0)
			goto next;

		printf("Control socket listening on %s:%s\n",
		       control_host, control_port);
		fflush(stdout);

		control_fd = accept(fd, NULL, 0);
		close(fd);

		if (control_fd < 0) {
			perror("accept");
			exit(EXIT_FAILURE);
		}
		printf("Control socket connection accepted...\n");
		break;

next:
		close(fd);
	}

	if (control_fd < 0) {
		fprintf(stderr, "Control socket initialization failed.  Invalid address %s:%s?\n",
			control_host, control_port);
		exit(EXIT_FAILURE);
	}

	freeaddrinfo(result);
}

/* Free resources */
void control_cleanup(void)
{
	close(control_fd);
	control_fd = -1;
}

/* Write a line to the control socket */
void control_writeln(const char *str)
{
	ssize_t len = strlen(str);
	ssize_t ret;

	timeout_begin(TIMEOUT);

	do {
		ret = send(control_fd, str, len, MSG_MORE);
		timeout_check("send");
	} while (ret < 0 && errno == EINTR);

	if (ret != len) {
		perror("send");
		exit(EXIT_FAILURE);
	}

	do {
		ret = send(control_fd, "\n", 1, 0);
		timeout_check("send");
	} while (ret < 0 && errno == EINTR);

	if (ret != 1) {
		perror("send");
		exit(EXIT_FAILURE);
	}

	timeout_end();
}

/* Return the next line from the control socket (without the trailing newline).
 *
 * The program terminates if a timeout occurs.
 *
 * The caller must free() the returned string.
 */
char *control_readln(void)
{
	char *buf = NULL;
	size_t idx = 0;
	size_t buflen = 0;

	timeout_begin(TIMEOUT);

	for (;;) {
		ssize_t ret;

		if (idx >= buflen) {
			char *new_buf;

			new_buf = realloc(buf, buflen + 80);
			if (!new_buf) {
				perror("realloc");
				exit(EXIT_FAILURE);
			}

			buf = new_buf;
			buflen += 80;
		}

		do {
			ret = recv(control_fd, &buf[idx], 1, 0);
			timeout_check("recv");
		} while (ret < 0 && errno == EINTR);

		if (ret == 0) {
			fprintf(stderr, "unexpected EOF on control socket\n");
			exit(EXIT_FAILURE);
		}

		if (ret != 1) {
			perror("recv");
			exit(EXIT_FAILURE);
		}

		if (buf[idx] == '\n') {
			buf[idx] = '\0';
			break;
		}

		idx++;
	}

	timeout_end();

	return buf;
}

/* Wait until a given line is received or a timeout occurs */
void control_expectln(const char *str)
{
	char *line;

	line = control_readln();
	if (strcmp(str, line) != 0) {
		fprintf(stderr, "expected \"%s\" on control socket, got \"%s\"\n",
			str, line);
		exit(EXIT_FAILURE);
	}

	free(line);
}
