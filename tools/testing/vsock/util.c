// SPDX-License-Identifier: GPL-2.0-only
/*
 * vsock test utilities
 *
 * Copyright (C) 2017 Red Hat, Inc.
 *
 * Author: Stefan Hajnoczi <stefanha@redhat.com>
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#include "timeout.h"
#include "control.h"
#include "util.h"

/* Install signal handlers */
void init_signals(void)
{
	struct sigaction act = {
		.sa_handler = sigalrm,
	};

	sigaction(SIGALRM, &act, NULL);
	signal(SIGPIPE, SIG_IGN);
}

/* Parse a CID in string representation */
unsigned int parse_cid(const char *str)
{
	char *endptr = NULL;
	unsigned long n;

	errno = 0;
	n = strtoul(str, &endptr, 10);
	if (errno || *endptr != '\0') {
		fprintf(stderr, "malformed CID \"%s\"\n", str);
		exit(EXIT_FAILURE);
	}
	return n;
}

/* Connect to <cid, port> and return the file descriptor. */
int vsock_stream_connect(unsigned int cid, unsigned int port)
{
	union {
		struct sockaddr sa;
		struct sockaddr_vm svm;
	} addr = {
		.svm = {
			.svm_family = AF_VSOCK,
			.svm_port = port,
			.svm_cid = cid,
		},
	};
	int ret;
	int fd;

	control_expectln("LISTENING");

	fd = socket(AF_VSOCK, SOCK_STREAM, 0);

	timeout_begin(TIMEOUT);
	do {
		ret = connect(fd, &addr.sa, sizeof(addr.svm));
		timeout_check("connect");
	} while (ret < 0 && errno == EINTR);
	timeout_end();

	if (ret < 0) {
		int old_errno = errno;

		close(fd);
		fd = -1;
		errno = old_errno;
	}
	return fd;
}

/* Listen on <cid, port> and return the first incoming connection.  The remote
 * address is stored to clientaddrp.  clientaddrp may be NULL.
 */
int vsock_stream_accept(unsigned int cid, unsigned int port,
			struct sockaddr_vm *clientaddrp)
{
	union {
		struct sockaddr sa;
		struct sockaddr_vm svm;
	} addr = {
		.svm = {
			.svm_family = AF_VSOCK,
			.svm_port = port,
			.svm_cid = cid,
		},
	};
	union {
		struct sockaddr sa;
		struct sockaddr_vm svm;
	} clientaddr;
	socklen_t clientaddr_len = sizeof(clientaddr.svm);
	int fd;
	int client_fd;
	int old_errno;

	fd = socket(AF_VSOCK, SOCK_STREAM, 0);

	if (bind(fd, &addr.sa, sizeof(addr.svm)) < 0) {
		perror("bind");
		exit(EXIT_FAILURE);
	}

	if (listen(fd, 1) < 0) {
		perror("listen");
		exit(EXIT_FAILURE);
	}

	control_writeln("LISTENING");

	timeout_begin(TIMEOUT);
	do {
		client_fd = accept(fd, &clientaddr.sa, &clientaddr_len);
		timeout_check("accept");
	} while (client_fd < 0 && errno == EINTR);
	timeout_end();

	old_errno = errno;
	close(fd);
	errno = old_errno;

	if (client_fd < 0)
		return client_fd;

	if (clientaddr_len != sizeof(clientaddr.svm)) {
		fprintf(stderr, "unexpected addrlen from accept(2), %zu\n",
			(size_t)clientaddr_len);
		exit(EXIT_FAILURE);
	}
	if (clientaddr.sa.sa_family != AF_VSOCK) {
		fprintf(stderr, "expected AF_VSOCK from accept(2), got %d\n",
			clientaddr.sa.sa_family);
		exit(EXIT_FAILURE);
	}

	if (clientaddrp)
		*clientaddrp = clientaddr.svm;
	return client_fd;
}

/* Run test cases.  The program terminates if a failure occurs. */
void run_tests(const struct test_case *test_cases,
	       const struct test_opts *opts)
{
	int i;

	for (i = 0; test_cases[i].name; i++) {
		void (*run)(const struct test_opts *opts);

		printf("%s...", test_cases[i].name);
		fflush(stdout);

		if (opts->mode == TEST_MODE_CLIENT)
			run = test_cases[i].run_client;
		else
			run = test_cases[i].run_server;

		if (run)
			run(opts);

		printf("ok\n");
	}
}
