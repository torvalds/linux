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
#include <stdint.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <assert.h>
#include <sys/epoll.h>

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

/* Wait for the remote to close the connection */
void vsock_wait_remote_close(int fd)
{
	struct epoll_event ev;
	int epollfd, nfds;

	epollfd = epoll_create1(0);
	if (epollfd == -1) {
		perror("epoll_create1");
		exit(EXIT_FAILURE);
	}

	ev.events = EPOLLRDHUP | EPOLLHUP;
	ev.data.fd = fd;
	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
		perror("epoll_ctl");
		exit(EXIT_FAILURE);
	}

	nfds = epoll_wait(epollfd, &ev, 1, TIMEOUT * 1000);
	if (nfds == -1) {
		perror("epoll_wait");
		exit(EXIT_FAILURE);
	}

	if (nfds == 0) {
		fprintf(stderr, "epoll_wait timed out\n");
		exit(EXIT_FAILURE);
	}

	assert(nfds == 1);
	assert(ev.events & (EPOLLRDHUP | EPOLLHUP));
	assert(ev.data.fd == fd);

	close(epollfd);
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

/* Transmit one byte and check the return value.
 *
 * expected_ret:
 *  <0 Negative errno (for testing errors)
 *   0 End-of-file
 *   1 Success
 */
void send_byte(int fd, int expected_ret, int flags)
{
	const uint8_t byte = 'A';
	ssize_t nwritten;

	timeout_begin(TIMEOUT);
	do {
		nwritten = send(fd, &byte, sizeof(byte), flags);
		timeout_check("write");
	} while (nwritten < 0 && errno == EINTR);
	timeout_end();

	if (expected_ret < 0) {
		if (nwritten != -1) {
			fprintf(stderr, "bogus send(2) return value %zd\n",
				nwritten);
			exit(EXIT_FAILURE);
		}
		if (errno != -expected_ret) {
			perror("write");
			exit(EXIT_FAILURE);
		}
		return;
	}

	if (nwritten < 0) {
		perror("write");
		exit(EXIT_FAILURE);
	}
	if (nwritten == 0) {
		if (expected_ret == 0)
			return;

		fprintf(stderr, "unexpected EOF while sending byte\n");
		exit(EXIT_FAILURE);
	}
	if (nwritten != sizeof(byte)) {
		fprintf(stderr, "bogus send(2) return value %zd\n", nwritten);
		exit(EXIT_FAILURE);
	}
}

/* Receive one byte and check the return value.
 *
 * expected_ret:
 *  <0 Negative errno (for testing errors)
 *   0 End-of-file
 *   1 Success
 */
void recv_byte(int fd, int expected_ret, int flags)
{
	uint8_t byte;
	ssize_t nread;

	timeout_begin(TIMEOUT);
	do {
		nread = recv(fd, &byte, sizeof(byte), flags);
		timeout_check("read");
	} while (nread < 0 && errno == EINTR);
	timeout_end();

	if (expected_ret < 0) {
		if (nread != -1) {
			fprintf(stderr, "bogus recv(2) return value %zd\n",
				nread);
			exit(EXIT_FAILURE);
		}
		if (errno != -expected_ret) {
			perror("read");
			exit(EXIT_FAILURE);
		}
		return;
	}

	if (nread < 0) {
		perror("read");
		exit(EXIT_FAILURE);
	}
	if (nread == 0) {
		if (expected_ret == 0)
			return;

		fprintf(stderr, "unexpected EOF while receiving byte\n");
		exit(EXIT_FAILURE);
	}
	if (nread != sizeof(byte)) {
		fprintf(stderr, "bogus recv(2) return value %zd\n", nread);
		exit(EXIT_FAILURE);
	}
	if (byte != 'A') {
		fprintf(stderr, "unexpected byte read %c\n", byte);
		exit(EXIT_FAILURE);
	}
}

/* Run test cases.  The program terminates if a failure occurs. */
void run_tests(const struct test_case *test_cases,
	       const struct test_opts *opts)
{
	int i;

	for (i = 0; test_cases[i].name; i++) {
		void (*run)(const struct test_opts *opts);
		char *line;

		printf("%d - %s...", i, test_cases[i].name);
		fflush(stdout);

		/* Full barrier before executing the next test.  This
		 * ensures that client and server are executing the
		 * same test case.  In particular, it means whoever is
		 * faster will not see the peer still executing the
		 * last test.  This is important because port numbers
		 * can be used by multiple test cases.
		 */
		if (test_cases[i].skip)
			control_writeln("SKIP");
		else
			control_writeln("NEXT");

		line = control_readln();
		if (control_cmpln(line, "SKIP", false) || test_cases[i].skip) {

			printf("skipped\n");

			free(line);
			continue;
		}

		control_cmpln(line, "NEXT", true);
		free(line);

		if (opts->mode == TEST_MODE_CLIENT)
			run = test_cases[i].run_client;
		else
			run = test_cases[i].run_server;

		if (run)
			run(opts);

		printf("ok\n");
	}
}

void list_tests(const struct test_case *test_cases)
{
	int i;

	printf("ID\tTest name\n");

	for (i = 0; test_cases[i].name; i++)
		printf("%d\t%s\n", i, test_cases[i].name);

	exit(EXIT_FAILURE);
}

void skip_test(struct test_case *test_cases, size_t test_cases_len,
	       const char *test_id_str)
{
	unsigned long test_id;
	char *endptr = NULL;

	errno = 0;
	test_id = strtoul(test_id_str, &endptr, 10);
	if (errno || *endptr != '\0') {
		fprintf(stderr, "malformed test ID \"%s\"\n", test_id_str);
		exit(EXIT_FAILURE);
	}

	if (test_id >= test_cases_len) {
		fprintf(stderr, "test ID (%lu) larger than the max allowed (%lu)\n",
			test_id, test_cases_len - 1);
		exit(EXIT_FAILURE);
	}

	test_cases[test_id].skip = true;
}
