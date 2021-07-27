// SPDX-License-Identifier: GPL-2.0-only
/*
 * vsock_test - vsock.ko test suite
 *
 * Copyright (C) 2017 Red Hat, Inc.
 *
 * Author: Stefan Hajnoczi <stefanha@redhat.com>
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <linux/kernel.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "timeout.h"
#include "control.h"
#include "util.h"

static void test_stream_connection_reset(const struct test_opts *opts)
{
	union {
		struct sockaddr sa;
		struct sockaddr_vm svm;
	} addr = {
		.svm = {
			.svm_family = AF_VSOCK,
			.svm_port = 1234,
			.svm_cid = opts->peer_cid,
		},
	};
	int ret;
	int fd;

	fd = socket(AF_VSOCK, SOCK_STREAM, 0);

	timeout_begin(TIMEOUT);
	do {
		ret = connect(fd, &addr.sa, sizeof(addr.svm));
		timeout_check("connect");
	} while (ret < 0 && errno == EINTR);
	timeout_end();

	if (ret != -1) {
		fprintf(stderr, "expected connect(2) failure, got %d\n", ret);
		exit(EXIT_FAILURE);
	}
	if (errno != ECONNRESET) {
		fprintf(stderr, "unexpected connect(2) errno %d\n", errno);
		exit(EXIT_FAILURE);
	}

	close(fd);
}

static void test_stream_bind_only_client(const struct test_opts *opts)
{
	union {
		struct sockaddr sa;
		struct sockaddr_vm svm;
	} addr = {
		.svm = {
			.svm_family = AF_VSOCK,
			.svm_port = 1234,
			.svm_cid = opts->peer_cid,
		},
	};
	int ret;
	int fd;

	/* Wait for the server to be ready */
	control_expectln("BIND");

	fd = socket(AF_VSOCK, SOCK_STREAM, 0);

	timeout_begin(TIMEOUT);
	do {
		ret = connect(fd, &addr.sa, sizeof(addr.svm));
		timeout_check("connect");
	} while (ret < 0 && errno == EINTR);
	timeout_end();

	if (ret != -1) {
		fprintf(stderr, "expected connect(2) failure, got %d\n", ret);
		exit(EXIT_FAILURE);
	}
	if (errno != ECONNRESET) {
		fprintf(stderr, "unexpected connect(2) errno %d\n", errno);
		exit(EXIT_FAILURE);
	}

	/* Notify the server that the client has finished */
	control_writeln("DONE");

	close(fd);
}

static void test_stream_bind_only_server(const struct test_opts *opts)
{
	union {
		struct sockaddr sa;
		struct sockaddr_vm svm;
	} addr = {
		.svm = {
			.svm_family = AF_VSOCK,
			.svm_port = 1234,
			.svm_cid = VMADDR_CID_ANY,
		},
	};
	int fd;

	fd = socket(AF_VSOCK, SOCK_STREAM, 0);

	if (bind(fd, &addr.sa, sizeof(addr.svm)) < 0) {
		perror("bind");
		exit(EXIT_FAILURE);
	}

	/* Notify the client that the server is ready */
	control_writeln("BIND");

	/* Wait for the client to finish */
	control_expectln("DONE");

	close(fd);
}

static void test_stream_client_close_client(const struct test_opts *opts)
{
	int fd;

	fd = vsock_stream_connect(opts->peer_cid, 1234);
	if (fd < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	send_byte(fd, 1, 0);
	close(fd);
}

static void test_stream_client_close_server(const struct test_opts *opts)
{
	int fd;

	fd = vsock_stream_accept(VMADDR_CID_ANY, 1234, NULL);
	if (fd < 0) {
		perror("accept");
		exit(EXIT_FAILURE);
	}

	/* Wait for the remote to close the connection, before check
	 * -EPIPE error on send.
	 */
	vsock_wait_remote_close(fd);

	send_byte(fd, -EPIPE, 0);
	recv_byte(fd, 1, 0);
	recv_byte(fd, 0, 0);
	close(fd);
}

static void test_stream_server_close_client(const struct test_opts *opts)
{
	int fd;

	fd = vsock_stream_connect(opts->peer_cid, 1234);
	if (fd < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	/* Wait for the remote to close the connection, before check
	 * -EPIPE error on send.
	 */
	vsock_wait_remote_close(fd);

	send_byte(fd, -EPIPE, 0);
	recv_byte(fd, 1, 0);
	recv_byte(fd, 0, 0);
	close(fd);
}

static void test_stream_server_close_server(const struct test_opts *opts)
{
	int fd;

	fd = vsock_stream_accept(VMADDR_CID_ANY, 1234, NULL);
	if (fd < 0) {
		perror("accept");
		exit(EXIT_FAILURE);
	}

	send_byte(fd, 1, 0);
	close(fd);
}

/* With the standard socket sizes, VMCI is able to support about 100
 * concurrent stream connections.
 */
#define MULTICONN_NFDS 100

static void test_stream_multiconn_client(const struct test_opts *opts)
{
	int fds[MULTICONN_NFDS];
	int i;

	for (i = 0; i < MULTICONN_NFDS; i++) {
		fds[i] = vsock_stream_connect(opts->peer_cid, 1234);
		if (fds[i] < 0) {
			perror("connect");
			exit(EXIT_FAILURE);
		}
	}

	for (i = 0; i < MULTICONN_NFDS; i++) {
		if (i % 2)
			recv_byte(fds[i], 1, 0);
		else
			send_byte(fds[i], 1, 0);
	}

	for (i = 0; i < MULTICONN_NFDS; i++)
		close(fds[i]);
}

static void test_stream_multiconn_server(const struct test_opts *opts)
{
	int fds[MULTICONN_NFDS];
	int i;

	for (i = 0; i < MULTICONN_NFDS; i++) {
		fds[i] = vsock_stream_accept(VMADDR_CID_ANY, 1234, NULL);
		if (fds[i] < 0) {
			perror("accept");
			exit(EXIT_FAILURE);
		}
	}

	for (i = 0; i < MULTICONN_NFDS; i++) {
		if (i % 2)
			send_byte(fds[i], 1, 0);
		else
			recv_byte(fds[i], 1, 0);
	}

	for (i = 0; i < MULTICONN_NFDS; i++)
		close(fds[i]);
}

static void test_stream_msg_peek_client(const struct test_opts *opts)
{
	int fd;

	fd = vsock_stream_connect(opts->peer_cid, 1234);
	if (fd < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	send_byte(fd, 1, 0);
	close(fd);
}

static void test_stream_msg_peek_server(const struct test_opts *opts)
{
	int fd;

	fd = vsock_stream_accept(VMADDR_CID_ANY, 1234, NULL);
	if (fd < 0) {
		perror("accept");
		exit(EXIT_FAILURE);
	}

	recv_byte(fd, 1, MSG_PEEK);
	recv_byte(fd, 1, 0);
	close(fd);
}

#define MESSAGES_CNT 7
static void test_seqpacket_msg_bounds_client(const struct test_opts *opts)
{
	int fd;

	fd = vsock_seqpacket_connect(opts->peer_cid, 1234);
	if (fd < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	/* Send several messages, one with MSG_EOR flag */
	for (int i = 0; i < MESSAGES_CNT; i++)
		send_byte(fd, 1, 0);

	control_writeln("SENDDONE");
	close(fd);
}

static void test_seqpacket_msg_bounds_server(const struct test_opts *opts)
{
	int fd;
	char buf[16];
	struct msghdr msg = {0};
	struct iovec iov = {0};

	fd = vsock_seqpacket_accept(VMADDR_CID_ANY, 1234, NULL);
	if (fd < 0) {
		perror("accept");
		exit(EXIT_FAILURE);
	}

	control_expectln("SENDDONE");
	iov.iov_base = buf;
	iov.iov_len = sizeof(buf);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	for (int i = 0; i < MESSAGES_CNT; i++) {
		if (recvmsg(fd, &msg, 0) != 1) {
			perror("message bound violated");
			exit(EXIT_FAILURE);
		}
	}

	close(fd);
}

#define MESSAGE_TRUNC_SZ 32
static void test_seqpacket_msg_trunc_client(const struct test_opts *opts)
{
	int fd;
	char buf[MESSAGE_TRUNC_SZ];

	fd = vsock_seqpacket_connect(opts->peer_cid, 1234);
	if (fd < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	if (send(fd, buf, sizeof(buf), 0) != sizeof(buf)) {
		perror("send failed");
		exit(EXIT_FAILURE);
	}

	control_writeln("SENDDONE");
	close(fd);
}

static void test_seqpacket_msg_trunc_server(const struct test_opts *opts)
{
	int fd;
	char buf[MESSAGE_TRUNC_SZ / 2];
	struct msghdr msg = {0};
	struct iovec iov = {0};

	fd = vsock_seqpacket_accept(VMADDR_CID_ANY, 1234, NULL);
	if (fd < 0) {
		perror("accept");
		exit(EXIT_FAILURE);
	}

	control_expectln("SENDDONE");
	iov.iov_base = buf;
	iov.iov_len = sizeof(buf);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	ssize_t ret = recvmsg(fd, &msg, MSG_TRUNC);

	if (ret != MESSAGE_TRUNC_SZ) {
		printf("%zi\n", ret);
		perror("MSG_TRUNC doesn't work");
		exit(EXIT_FAILURE);
	}

	if (!(msg.msg_flags & MSG_TRUNC)) {
		fprintf(stderr, "MSG_TRUNC expected\n");
		exit(EXIT_FAILURE);
	}

	close(fd);
}

static struct test_case test_cases[] = {
	{
		.name = "SOCK_STREAM connection reset",
		.run_client = test_stream_connection_reset,
	},
	{
		.name = "SOCK_STREAM bind only",
		.run_client = test_stream_bind_only_client,
		.run_server = test_stream_bind_only_server,
	},
	{
		.name = "SOCK_STREAM client close",
		.run_client = test_stream_client_close_client,
		.run_server = test_stream_client_close_server,
	},
	{
		.name = "SOCK_STREAM server close",
		.run_client = test_stream_server_close_client,
		.run_server = test_stream_server_close_server,
	},
	{
		.name = "SOCK_STREAM multiple connections",
		.run_client = test_stream_multiconn_client,
		.run_server = test_stream_multiconn_server,
	},
	{
		.name = "SOCK_STREAM MSG_PEEK",
		.run_client = test_stream_msg_peek_client,
		.run_server = test_stream_msg_peek_server,
	},
	{
		.name = "SOCK_SEQPACKET msg bounds",
		.run_client = test_seqpacket_msg_bounds_client,
		.run_server = test_seqpacket_msg_bounds_server,
	},
	{
		.name = "SOCK_SEQPACKET MSG_TRUNC flag",
		.run_client = test_seqpacket_msg_trunc_client,
		.run_server = test_seqpacket_msg_trunc_server,
	},
	{},
};

static const char optstring[] = "";
static const struct option longopts[] = {
	{
		.name = "control-host",
		.has_arg = required_argument,
		.val = 'H',
	},
	{
		.name = "control-port",
		.has_arg = required_argument,
		.val = 'P',
	},
	{
		.name = "mode",
		.has_arg = required_argument,
		.val = 'm',
	},
	{
		.name = "peer-cid",
		.has_arg = required_argument,
		.val = 'p',
	},
	{
		.name = "list",
		.has_arg = no_argument,
		.val = 'l',
	},
	{
		.name = "skip",
		.has_arg = required_argument,
		.val = 's',
	},
	{
		.name = "help",
		.has_arg = no_argument,
		.val = '?',
	},
	{},
};

static void usage(void)
{
	fprintf(stderr, "Usage: vsock_test [--help] [--control-host=<host>] --control-port=<port> --mode=client|server --peer-cid=<cid> [--list] [--skip=<test_id>]\n"
		"\n"
		"  Server: vsock_test --control-port=1234 --mode=server --peer-cid=3\n"
		"  Client: vsock_test --control-host=192.168.0.1 --control-port=1234 --mode=client --peer-cid=2\n"
		"\n"
		"Run vsock.ko tests.  Must be launched in both guest\n"
		"and host.  One side must use --mode=client and\n"
		"the other side must use --mode=server.\n"
		"\n"
		"A TCP control socket connection is used to coordinate tests\n"
		"between the client and the server.  The server requires a\n"
		"listen address and the client requires an address to\n"
		"connect to.\n"
		"\n"
		"The CID of the other side must be given with --peer-cid=<cid>.\n"
		"\n"
		"Options:\n"
		"  --help                 This help message\n"
		"  --control-host <host>  Server IP address to connect to\n"
		"  --control-port <port>  Server port to listen on/connect to\n"
		"  --mode client|server   Server or client mode\n"
		"  --peer-cid <cid>       CID of the other side\n"
		"  --list                 List of tests that will be executed\n"
		"  --skip <test_id>       Test ID to skip;\n"
		"                         use multiple --skip options to skip more tests\n"
		);
	exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
	const char *control_host = NULL;
	const char *control_port = NULL;
	struct test_opts opts = {
		.mode = TEST_MODE_UNSET,
		.peer_cid = VMADDR_CID_ANY,
	};

	init_signals();

	for (;;) {
		int opt = getopt_long(argc, argv, optstring, longopts, NULL);

		if (opt == -1)
			break;

		switch (opt) {
		case 'H':
			control_host = optarg;
			break;
		case 'm':
			if (strcmp(optarg, "client") == 0)
				opts.mode = TEST_MODE_CLIENT;
			else if (strcmp(optarg, "server") == 0)
				opts.mode = TEST_MODE_SERVER;
			else {
				fprintf(stderr, "--mode must be \"client\" or \"server\"\n");
				return EXIT_FAILURE;
			}
			break;
		case 'p':
			opts.peer_cid = parse_cid(optarg);
			break;
		case 'P':
			control_port = optarg;
			break;
		case 'l':
			list_tests(test_cases);
			break;
		case 's':
			skip_test(test_cases, ARRAY_SIZE(test_cases) - 1,
				  optarg);
			break;
		case '?':
		default:
			usage();
		}
	}

	if (!control_port)
		usage();
	if (opts.mode == TEST_MODE_UNSET)
		usage();
	if (opts.peer_cid == VMADDR_CID_ANY)
		usage();

	if (!control_host) {
		if (opts.mode != TEST_MODE_SERVER)
			usage();
		control_host = "0.0.0.0";
	}

	control_init(control_host, control_port,
		     opts.mode == TEST_MODE_SERVER);

	run_tests(test_cases, &opts);

	control_cleanup();
	return EXIT_SUCCESS;
}
