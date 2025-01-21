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
#include <time.h>
#include <sys/mman.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <linux/sockios.h>

#include "vsock_test_zerocopy.h"
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
			.svm_port = opts->peer_port,
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
			.svm_port = opts->peer_port,
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
			.svm_port = opts->peer_port,
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

	fd = vsock_stream_connect(opts->peer_cid, opts->peer_port);
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

	fd = vsock_stream_accept(VMADDR_CID_ANY, opts->peer_port, NULL);
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

	fd = vsock_stream_connect(opts->peer_cid, opts->peer_port);
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

	fd = vsock_stream_accept(VMADDR_CID_ANY, opts->peer_port, NULL);
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
		fds[i] = vsock_stream_connect(opts->peer_cid, opts->peer_port);
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
		fds[i] = vsock_stream_accept(VMADDR_CID_ANY, opts->peer_port, NULL);
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

#define MSG_PEEK_BUF_LEN 64

static void test_msg_peek_client(const struct test_opts *opts,
				 bool seqpacket)
{
	unsigned char buf[MSG_PEEK_BUF_LEN];
	int fd;
	int i;

	if (seqpacket)
		fd = vsock_seqpacket_connect(opts->peer_cid, opts->peer_port);
	else
		fd = vsock_stream_connect(opts->peer_cid, opts->peer_port);

	if (fd < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	for (i = 0; i < sizeof(buf); i++)
		buf[i] = rand() & 0xFF;

	control_expectln("SRVREADY");

	send_buf(fd, buf, sizeof(buf), 0, sizeof(buf));

	close(fd);
}

static void test_msg_peek_server(const struct test_opts *opts,
				 bool seqpacket)
{
	unsigned char buf_half[MSG_PEEK_BUF_LEN / 2];
	unsigned char buf_normal[MSG_PEEK_BUF_LEN];
	unsigned char buf_peek[MSG_PEEK_BUF_LEN];
	int fd;

	if (seqpacket)
		fd = vsock_seqpacket_accept(VMADDR_CID_ANY, opts->peer_port, NULL);
	else
		fd = vsock_stream_accept(VMADDR_CID_ANY, opts->peer_port, NULL);

	if (fd < 0) {
		perror("accept");
		exit(EXIT_FAILURE);
	}

	/* Peek from empty socket. */
	recv_buf(fd, buf_peek, sizeof(buf_peek), MSG_PEEK | MSG_DONTWAIT,
		 -EAGAIN);

	control_writeln("SRVREADY");

	/* Peek part of data. */
	recv_buf(fd, buf_half, sizeof(buf_half), MSG_PEEK, sizeof(buf_half));

	/* Peek whole data. */
	recv_buf(fd, buf_peek, sizeof(buf_peek), MSG_PEEK, sizeof(buf_peek));

	/* Compare partial and full peek. */
	if (memcmp(buf_half, buf_peek, sizeof(buf_half))) {
		fprintf(stderr, "Partial peek data mismatch\n");
		exit(EXIT_FAILURE);
	}

	if (seqpacket) {
		/* This type of socket supports MSG_TRUNC flag,
		 * so check it with MSG_PEEK. We must get length
		 * of the message.
		 */
		recv_buf(fd, buf_half, sizeof(buf_half), MSG_PEEK | MSG_TRUNC,
			 sizeof(buf_peek));
	}

	recv_buf(fd, buf_normal, sizeof(buf_normal), 0, sizeof(buf_normal));

	/* Compare full peek and normal read. */
	if (memcmp(buf_peek, buf_normal, sizeof(buf_peek))) {
		fprintf(stderr, "Full peek data mismatch\n");
		exit(EXIT_FAILURE);
	}

	close(fd);
}

static void test_stream_msg_peek_client(const struct test_opts *opts)
{
	return test_msg_peek_client(opts, false);
}

static void test_stream_msg_peek_server(const struct test_opts *opts)
{
	return test_msg_peek_server(opts, false);
}

#define SOCK_BUF_SIZE (2 * 1024 * 1024)
#define MAX_MSG_PAGES 4

static void test_seqpacket_msg_bounds_client(const struct test_opts *opts)
{
	unsigned long curr_hash;
	size_t max_msg_size;
	int page_size;
	int msg_count;
	int fd;

	fd = vsock_seqpacket_connect(opts->peer_cid, opts->peer_port);
	if (fd < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	/* Wait, until receiver sets buffer size. */
	control_expectln("SRVREADY");

	curr_hash = 0;
	page_size = getpagesize();
	max_msg_size = MAX_MSG_PAGES * page_size;
	msg_count = SOCK_BUF_SIZE / max_msg_size;

	for (int i = 0; i < msg_count; i++) {
		size_t buf_size;
		int flags;
		void *buf;

		/* Use "small" buffers and "big" buffers. */
		if (i & 1)
			buf_size = page_size +
					(rand() % (max_msg_size - page_size));
		else
			buf_size = 1 + (rand() % page_size);

		buf = malloc(buf_size);

		if (!buf) {
			perror("malloc");
			exit(EXIT_FAILURE);
		}

		memset(buf, rand() & 0xff, buf_size);
		/* Set at least one MSG_EOR + some random. */
		if (i == (msg_count / 2) || (rand() & 1)) {
			flags = MSG_EOR;
			curr_hash++;
		} else {
			flags = 0;
		}

		send_buf(fd, buf, buf_size, flags, buf_size);

		/*
		 * Hash sum is computed at both client and server in
		 * the same way:
		 * H += hash('message data')
		 * Such hash "controls" both data integrity and message
		 * bounds. After data exchange, both sums are compared
		 * using control socket, and if message bounds wasn't
		 * broken - two values must be equal.
		 */
		curr_hash += hash_djb2(buf, buf_size);
		free(buf);
	}

	control_writeln("SENDDONE");
	control_writeulong(curr_hash);
	close(fd);
}

static void test_seqpacket_msg_bounds_server(const struct test_opts *opts)
{
	unsigned long long sock_buf_size;
	unsigned long remote_hash;
	unsigned long curr_hash;
	int fd;
	struct msghdr msg = {0};
	struct iovec iov = {0};

	fd = vsock_seqpacket_accept(VMADDR_CID_ANY, opts->peer_port, NULL);
	if (fd < 0) {
		perror("accept");
		exit(EXIT_FAILURE);
	}

	sock_buf_size = SOCK_BUF_SIZE;

	setsockopt_ull_check(fd, AF_VSOCK, SO_VM_SOCKETS_BUFFER_MAX_SIZE,
			     sock_buf_size,
			     "setsockopt(SO_VM_SOCKETS_BUFFER_MAX_SIZE)");

	setsockopt_ull_check(fd, AF_VSOCK, SO_VM_SOCKETS_BUFFER_SIZE,
			     sock_buf_size,
			     "setsockopt(SO_VM_SOCKETS_BUFFER_SIZE)");

	/* Ready to receive data. */
	control_writeln("SRVREADY");
	/* Wait, until peer sends whole data. */
	control_expectln("SENDDONE");
	iov.iov_len = MAX_MSG_PAGES * getpagesize();
	iov.iov_base = malloc(iov.iov_len);
	if (!iov.iov_base) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	curr_hash = 0;

	while (1) {
		ssize_t recv_size;

		recv_size = recvmsg(fd, &msg, 0);

		if (!recv_size)
			break;

		if (recv_size < 0) {
			perror("recvmsg");
			exit(EXIT_FAILURE);
		}

		if (msg.msg_flags & MSG_EOR)
			curr_hash++;

		curr_hash += hash_djb2(msg.msg_iov[0].iov_base, recv_size);
	}

	free(iov.iov_base);
	close(fd);
	remote_hash = control_readulong();

	if (curr_hash != remote_hash) {
		fprintf(stderr, "Message bounds broken\n");
		exit(EXIT_FAILURE);
	}
}

#define MESSAGE_TRUNC_SZ 32
static void test_seqpacket_msg_trunc_client(const struct test_opts *opts)
{
	int fd;
	char buf[MESSAGE_TRUNC_SZ];

	fd = vsock_seqpacket_connect(opts->peer_cid, opts->peer_port);
	if (fd < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	send_buf(fd, buf, sizeof(buf), 0, sizeof(buf));

	control_writeln("SENDDONE");
	close(fd);
}

static void test_seqpacket_msg_trunc_server(const struct test_opts *opts)
{
	int fd;
	char buf[MESSAGE_TRUNC_SZ / 2];
	struct msghdr msg = {0};
	struct iovec iov = {0};

	fd = vsock_seqpacket_accept(VMADDR_CID_ANY, opts->peer_port, NULL);
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

static time_t current_nsec(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_REALTIME, &ts)) {
		perror("clock_gettime(3) failed");
		exit(EXIT_FAILURE);
	}

	return (ts.tv_sec * 1000000000ULL) + ts.tv_nsec;
}

#define RCVTIMEO_TIMEOUT_SEC 1
#define READ_OVERHEAD_NSEC 250000000 /* 0.25 sec */

static void test_seqpacket_timeout_client(const struct test_opts *opts)
{
	int fd;
	struct timeval tv;
	char dummy;
	time_t read_enter_ns;
	time_t read_overhead_ns;

	fd = vsock_seqpacket_connect(opts->peer_cid, opts->peer_port);
	if (fd < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	tv.tv_sec = RCVTIMEO_TIMEOUT_SEC;
	tv.tv_usec = 0;

	setsockopt_timeval_check(fd, SOL_SOCKET, SO_RCVTIMEO, tv,
				 "setsockopt(SO_RCVTIMEO)");

	read_enter_ns = current_nsec();

	if (read(fd, &dummy, sizeof(dummy)) != -1) {
		fprintf(stderr,
			"expected 'dummy' read(2) failure\n");
		exit(EXIT_FAILURE);
	}

	if (errno != EAGAIN) {
		perror("EAGAIN expected");
		exit(EXIT_FAILURE);
	}

	read_overhead_ns = current_nsec() - read_enter_ns -
			1000000000ULL * RCVTIMEO_TIMEOUT_SEC;

	if (read_overhead_ns > READ_OVERHEAD_NSEC) {
		fprintf(stderr,
			"too much time in read(2), %lu > %i ns\n",
			read_overhead_ns, READ_OVERHEAD_NSEC);
		exit(EXIT_FAILURE);
	}

	control_writeln("WAITDONE");
	close(fd);
}

static void test_seqpacket_timeout_server(const struct test_opts *opts)
{
	int fd;

	fd = vsock_seqpacket_accept(VMADDR_CID_ANY, opts->peer_port, NULL);
	if (fd < 0) {
		perror("accept");
		exit(EXIT_FAILURE);
	}

	control_expectln("WAITDONE");
	close(fd);
}

static void test_seqpacket_bigmsg_client(const struct test_opts *opts)
{
	unsigned long long sock_buf_size;
	size_t buf_size;
	socklen_t len;
	void *data;
	int fd;

	len = sizeof(sock_buf_size);

	fd = vsock_seqpacket_connect(opts->peer_cid, opts->peer_port);
	if (fd < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	if (getsockopt(fd, AF_VSOCK, SO_VM_SOCKETS_BUFFER_SIZE,
		       &sock_buf_size, &len)) {
		perror("getsockopt");
		exit(EXIT_FAILURE);
	}

	sock_buf_size++;

	/* size_t can be < unsigned long long */
	buf_size = (size_t)sock_buf_size;
	if (buf_size != sock_buf_size) {
		fprintf(stderr, "Returned BUFFER_SIZE too large\n");
		exit(EXIT_FAILURE);
	}

	data = malloc(buf_size);
	if (!data) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}

	send_buf(fd, data, buf_size, 0, -EMSGSIZE);

	control_writeln("CLISENT");

	free(data);
	close(fd);
}

static void test_seqpacket_bigmsg_server(const struct test_opts *opts)
{
	int fd;

	fd = vsock_seqpacket_accept(VMADDR_CID_ANY, opts->peer_port, NULL);
	if (fd < 0) {
		perror("accept");
		exit(EXIT_FAILURE);
	}

	control_expectln("CLISENT");

	close(fd);
}

#define BUF_PATTERN_1 'a'
#define BUF_PATTERN_2 'b'

static void test_seqpacket_invalid_rec_buffer_client(const struct test_opts *opts)
{
	int fd;
	unsigned char *buf1;
	unsigned char *buf2;
	int buf_size = getpagesize() * 3;

	fd = vsock_seqpacket_connect(opts->peer_cid, opts->peer_port);
	if (fd < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	buf1 = malloc(buf_size);
	if (!buf1) {
		perror("'malloc()' for 'buf1'");
		exit(EXIT_FAILURE);
	}

	buf2 = malloc(buf_size);
	if (!buf2) {
		perror("'malloc()' for 'buf2'");
		exit(EXIT_FAILURE);
	}

	memset(buf1, BUF_PATTERN_1, buf_size);
	memset(buf2, BUF_PATTERN_2, buf_size);

	send_buf(fd, buf1, buf_size, 0, buf_size);

	send_buf(fd, buf2, buf_size, 0, buf_size);

	close(fd);
}

static void test_seqpacket_invalid_rec_buffer_server(const struct test_opts *opts)
{
	int fd;
	unsigned char *broken_buf;
	unsigned char *valid_buf;
	int page_size = getpagesize();
	int buf_size = page_size * 3;
	ssize_t res;
	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	int i;

	fd = vsock_seqpacket_accept(VMADDR_CID_ANY, opts->peer_port, NULL);
	if (fd < 0) {
		perror("accept");
		exit(EXIT_FAILURE);
	}

	/* Setup first buffer. */
	broken_buf = mmap(NULL, buf_size, prot, flags, -1, 0);
	if (broken_buf == MAP_FAILED) {
		perror("mmap for 'broken_buf'");
		exit(EXIT_FAILURE);
	}

	/* Unmap "hole" in buffer. */
	if (munmap(broken_buf + page_size, page_size)) {
		perror("'broken_buf' setup");
		exit(EXIT_FAILURE);
	}

	valid_buf = mmap(NULL, buf_size, prot, flags, -1, 0);
	if (valid_buf == MAP_FAILED) {
		perror("mmap for 'valid_buf'");
		exit(EXIT_FAILURE);
	}

	/* Try to fill buffer with unmapped middle. */
	res = read(fd, broken_buf, buf_size);
	if (res != -1) {
		fprintf(stderr,
			"expected 'broken_buf' read(2) failure, got %zi\n",
			res);
		exit(EXIT_FAILURE);
	}

	if (errno != EFAULT) {
		perror("unexpected errno of 'broken_buf'");
		exit(EXIT_FAILURE);
	}

	/* Try to fill valid buffer. */
	res = read(fd, valid_buf, buf_size);
	if (res < 0) {
		perror("unexpected 'valid_buf' read(2) failure");
		exit(EXIT_FAILURE);
	}

	if (res != buf_size) {
		fprintf(stderr,
			"invalid 'valid_buf' read(2), expected %i, got %zi\n",
			buf_size, res);
		exit(EXIT_FAILURE);
	}

	for (i = 0; i < buf_size; i++) {
		if (valid_buf[i] != BUF_PATTERN_2) {
			fprintf(stderr,
				"invalid pattern for 'valid_buf' at %i, expected %hhX, got %hhX\n",
				i, BUF_PATTERN_2, valid_buf[i]);
			exit(EXIT_FAILURE);
		}
	}

	/* Unmap buffers. */
	munmap(broken_buf, page_size);
	munmap(broken_buf + page_size * 2, page_size);
	munmap(valid_buf, buf_size);
	close(fd);
}

#define RCVLOWAT_BUF_SIZE 128

static void test_stream_poll_rcvlowat_server(const struct test_opts *opts)
{
	int fd;
	int i;

	fd = vsock_stream_accept(VMADDR_CID_ANY, opts->peer_port, NULL);
	if (fd < 0) {
		perror("accept");
		exit(EXIT_FAILURE);
	}

	/* Send 1 byte. */
	send_byte(fd, 1, 0);

	control_writeln("SRVSENT");

	/* Wait until client is ready to receive rest of data. */
	control_expectln("CLNSENT");

	for (i = 0; i < RCVLOWAT_BUF_SIZE - 1; i++)
		send_byte(fd, 1, 0);

	/* Keep socket in active state. */
	control_expectln("POLLDONE");

	close(fd);
}

static void test_stream_poll_rcvlowat_client(const struct test_opts *opts)
{
	int lowat_val = RCVLOWAT_BUF_SIZE;
	char buf[RCVLOWAT_BUF_SIZE];
	struct pollfd fds;
	short poll_flags;
	int fd;

	fd = vsock_stream_connect(opts->peer_cid, opts->peer_port);
	if (fd < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	setsockopt_int_check(fd, SOL_SOCKET, SO_RCVLOWAT,
			     lowat_val, "setsockopt(SO_RCVLOWAT)");

	control_expectln("SRVSENT");

	/* At this point, server sent 1 byte. */
	fds.fd = fd;
	poll_flags = POLLIN | POLLRDNORM;
	fds.events = poll_flags;

	/* Try to wait for 1 sec. */
	if (poll(&fds, 1, 1000) < 0) {
		perror("poll");
		exit(EXIT_FAILURE);
	}

	/* poll() must return nothing. */
	if (fds.revents) {
		fprintf(stderr, "Unexpected poll result %hx\n",
			fds.revents);
		exit(EXIT_FAILURE);
	}

	/* Tell server to send rest of data. */
	control_writeln("CLNSENT");

	/* Poll for data. */
	if (poll(&fds, 1, 10000) < 0) {
		perror("poll");
		exit(EXIT_FAILURE);
	}

	/* Only these two bits are expected. */
	if (fds.revents != poll_flags) {
		fprintf(stderr, "Unexpected poll result %hx\n",
			fds.revents);
		exit(EXIT_FAILURE);
	}

	/* Use MSG_DONTWAIT, if call is going to wait, EAGAIN
	 * will be returned.
	 */
	recv_buf(fd, buf, sizeof(buf), MSG_DONTWAIT, RCVLOWAT_BUF_SIZE);

	control_writeln("POLLDONE");

	close(fd);
}

#define INV_BUF_TEST_DATA_LEN 512

static void test_inv_buf_client(const struct test_opts *opts, bool stream)
{
	unsigned char data[INV_BUF_TEST_DATA_LEN] = {0};
	ssize_t expected_ret;
	int fd;

	if (stream)
		fd = vsock_stream_connect(opts->peer_cid, opts->peer_port);
	else
		fd = vsock_seqpacket_connect(opts->peer_cid, opts->peer_port);

	if (fd < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	control_expectln("SENDDONE");

	/* Use invalid buffer here. */
	recv_buf(fd, NULL, sizeof(data), 0, -EFAULT);

	if (stream) {
		/* For SOCK_STREAM we must continue reading. */
		expected_ret = sizeof(data);
	} else {
		/* For SOCK_SEQPACKET socket's queue must be empty. */
		expected_ret = -EAGAIN;
	}

	recv_buf(fd, data, sizeof(data), MSG_DONTWAIT, expected_ret);

	control_writeln("DONE");

	close(fd);
}

static void test_inv_buf_server(const struct test_opts *opts, bool stream)
{
	unsigned char data[INV_BUF_TEST_DATA_LEN] = {0};
	int fd;

	if (stream)
		fd = vsock_stream_accept(VMADDR_CID_ANY, opts->peer_port, NULL);
	else
		fd = vsock_seqpacket_accept(VMADDR_CID_ANY, opts->peer_port, NULL);

	if (fd < 0) {
		perror("accept");
		exit(EXIT_FAILURE);
	}

	send_buf(fd, data, sizeof(data), 0, sizeof(data));

	control_writeln("SENDDONE");

	control_expectln("DONE");

	close(fd);
}

static void test_stream_inv_buf_client(const struct test_opts *opts)
{
	test_inv_buf_client(opts, true);
}

static void test_stream_inv_buf_server(const struct test_opts *opts)
{
	test_inv_buf_server(opts, true);
}

static void test_seqpacket_inv_buf_client(const struct test_opts *opts)
{
	test_inv_buf_client(opts, false);
}

static void test_seqpacket_inv_buf_server(const struct test_opts *opts)
{
	test_inv_buf_server(opts, false);
}

#define HELLO_STR "HELLO"
#define WORLD_STR "WORLD"

static void test_stream_virtio_skb_merge_client(const struct test_opts *opts)
{
	int fd;

	fd = vsock_stream_connect(opts->peer_cid, opts->peer_port);
	if (fd < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	/* Send first skbuff. */
	send_buf(fd, HELLO_STR, strlen(HELLO_STR), 0, strlen(HELLO_STR));

	control_writeln("SEND0");
	/* Peer reads part of first skbuff. */
	control_expectln("REPLY0");

	/* Send second skbuff, it will be appended to the first. */
	send_buf(fd, WORLD_STR, strlen(WORLD_STR), 0, strlen(WORLD_STR));

	control_writeln("SEND1");
	/* Peer reads merged skbuff packet. */
	control_expectln("REPLY1");

	close(fd);
}

static void test_stream_virtio_skb_merge_server(const struct test_opts *opts)
{
	size_t read = 0, to_read;
	unsigned char buf[64];
	int fd;

	fd = vsock_stream_accept(VMADDR_CID_ANY, opts->peer_port, NULL);
	if (fd < 0) {
		perror("accept");
		exit(EXIT_FAILURE);
	}

	control_expectln("SEND0");

	/* Read skbuff partially. */
	to_read = 2;
	recv_buf(fd, buf + read, to_read, 0, to_read);
	read += to_read;

	control_writeln("REPLY0");
	control_expectln("SEND1");

	/* Read the rest of both buffers */
	to_read = strlen(HELLO_STR WORLD_STR) - read;
	recv_buf(fd, buf + read, to_read, 0, to_read);
	read += to_read;

	/* No more bytes should be there */
	to_read = sizeof(buf) - read;
	recv_buf(fd, buf + read, to_read, MSG_DONTWAIT, -EAGAIN);

	if (memcmp(buf, HELLO_STR WORLD_STR, strlen(HELLO_STR WORLD_STR))) {
		fprintf(stderr, "pattern mismatch\n");
		exit(EXIT_FAILURE);
	}

	control_writeln("REPLY1");

	close(fd);
}

static void test_seqpacket_msg_peek_client(const struct test_opts *opts)
{
	return test_msg_peek_client(opts, true);
}

static void test_seqpacket_msg_peek_server(const struct test_opts *opts)
{
	return test_msg_peek_server(opts, true);
}

static sig_atomic_t have_sigpipe;

static void sigpipe(int signo)
{
	have_sigpipe = 1;
}

static void test_stream_check_sigpipe(int fd)
{
	ssize_t res;

	have_sigpipe = 0;

	res = send(fd, "A", 1, 0);
	if (res != -1) {
		fprintf(stderr, "expected send(2) failure, got %zi\n", res);
		exit(EXIT_FAILURE);
	}

	if (!have_sigpipe) {
		fprintf(stderr, "SIGPIPE expected\n");
		exit(EXIT_FAILURE);
	}

	have_sigpipe = 0;

	res = send(fd, "A", 1, MSG_NOSIGNAL);
	if (res != -1) {
		fprintf(stderr, "expected send(2) failure, got %zi\n", res);
		exit(EXIT_FAILURE);
	}

	if (have_sigpipe) {
		fprintf(stderr, "SIGPIPE not expected\n");
		exit(EXIT_FAILURE);
	}
}

static void test_stream_shutwr_client(const struct test_opts *opts)
{
	int fd;

	struct sigaction act = {
		.sa_handler = sigpipe,
	};

	sigaction(SIGPIPE, &act, NULL);

	fd = vsock_stream_connect(opts->peer_cid, opts->peer_port);
	if (fd < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	if (shutdown(fd, SHUT_WR)) {
		perror("shutdown");
		exit(EXIT_FAILURE);
	}

	test_stream_check_sigpipe(fd);

	control_writeln("CLIENTDONE");

	close(fd);
}

static void test_stream_shutwr_server(const struct test_opts *opts)
{
	int fd;

	fd = vsock_stream_accept(VMADDR_CID_ANY, opts->peer_port, NULL);
	if (fd < 0) {
		perror("accept");
		exit(EXIT_FAILURE);
	}

	control_expectln("CLIENTDONE");

	close(fd);
}

static void test_stream_shutrd_client(const struct test_opts *opts)
{
	int fd;

	struct sigaction act = {
		.sa_handler = sigpipe,
	};

	sigaction(SIGPIPE, &act, NULL);

	fd = vsock_stream_connect(opts->peer_cid, opts->peer_port);
	if (fd < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	control_expectln("SHUTRDDONE");

	test_stream_check_sigpipe(fd);

	control_writeln("CLIENTDONE");

	close(fd);
}

static void test_stream_shutrd_server(const struct test_opts *opts)
{
	int fd;

	fd = vsock_stream_accept(VMADDR_CID_ANY, opts->peer_port, NULL);
	if (fd < 0) {
		perror("accept");
		exit(EXIT_FAILURE);
	}

	if (shutdown(fd, SHUT_RD)) {
		perror("shutdown");
		exit(EXIT_FAILURE);
	}

	control_writeln("SHUTRDDONE");
	control_expectln("CLIENTDONE");

	close(fd);
}

static void test_double_bind_connect_server(const struct test_opts *opts)
{
	int listen_fd, client_fd, i;
	struct sockaddr_vm sa_client;
	socklen_t socklen_client = sizeof(sa_client);

	listen_fd = vsock_stream_listen(VMADDR_CID_ANY, opts->peer_port);

	for (i = 0; i < 2; i++) {
		control_writeln("LISTENING");

		timeout_begin(TIMEOUT);
		do {
			client_fd = accept(listen_fd, (struct sockaddr *)&sa_client,
					   &socklen_client);
			timeout_check("accept");
		} while (client_fd < 0 && errno == EINTR);
		timeout_end();

		if (client_fd < 0) {
			perror("accept");
			exit(EXIT_FAILURE);
		}

		/* Waiting for remote peer to close connection */
		vsock_wait_remote_close(client_fd);
	}

	close(listen_fd);
}

static void test_double_bind_connect_client(const struct test_opts *opts)
{
	int i, client_fd;

	for (i = 0; i < 2; i++) {
		/* Wait until server is ready to accept a new connection */
		control_expectln("LISTENING");

		/* We use 'peer_port + 1' as "some" port for the 'bind()'
		 * call. It is safe for overflow, but must be considered,
		 * when running multiple test applications simultaneously
		 * where 'peer-port' argument differs by 1.
		 */
		client_fd = vsock_bind_connect(opts->peer_cid, opts->peer_port,
					       opts->peer_port + 1, SOCK_STREAM);

		close(client_fd);
	}
}

#define MSG_BUF_IOCTL_LEN 64
static void test_unsent_bytes_server(const struct test_opts *opts, int type)
{
	unsigned char buf[MSG_BUF_IOCTL_LEN];
	int client_fd;

	client_fd = vsock_accept(VMADDR_CID_ANY, opts->peer_port, NULL, type);
	if (client_fd < 0) {
		perror("accept");
		exit(EXIT_FAILURE);
	}

	recv_buf(client_fd, buf, sizeof(buf), 0, sizeof(buf));
	control_writeln("RECEIVED");

	close(client_fd);
}

static void test_unsent_bytes_client(const struct test_opts *opts, int type)
{
	unsigned char buf[MSG_BUF_IOCTL_LEN];
	int ret, fd, sock_bytes_unsent;

	fd = vsock_connect(opts->peer_cid, opts->peer_port, type);
	if (fd < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	for (int i = 0; i < sizeof(buf); i++)
		buf[i] = rand() & 0xFF;

	send_buf(fd, buf, sizeof(buf), 0, sizeof(buf));
	control_expectln("RECEIVED");

	ret = ioctl(fd, SIOCOUTQ, &sock_bytes_unsent);
	if (ret < 0) {
		if (errno == EOPNOTSUPP) {
			fprintf(stderr, "Test skipped, SIOCOUTQ not supported.\n");
		} else {
			perror("ioctl");
			exit(EXIT_FAILURE);
		}
	} else if (ret == 0 && sock_bytes_unsent != 0) {
		fprintf(stderr,
			"Unexpected 'SIOCOUTQ' value, expected 0, got %i\n",
			sock_bytes_unsent);
		exit(EXIT_FAILURE);
	}

	close(fd);
}

static void test_stream_unsent_bytes_client(const struct test_opts *opts)
{
	test_unsent_bytes_client(opts, SOCK_STREAM);
}

static void test_stream_unsent_bytes_server(const struct test_opts *opts)
{
	test_unsent_bytes_server(opts, SOCK_STREAM);
}

static void test_seqpacket_unsent_bytes_client(const struct test_opts *opts)
{
	test_unsent_bytes_client(opts, SOCK_SEQPACKET);
}

static void test_seqpacket_unsent_bytes_server(const struct test_opts *opts)
{
	test_unsent_bytes_server(opts, SOCK_SEQPACKET);
}

#define RCVLOWAT_CREDIT_UPD_BUF_SIZE	(1024 * 128)
/* This define is the same as in 'include/linux/virtio_vsock.h':
 * it is used to decide when to send credit update message during
 * reading from rx queue of a socket. Value and its usage in
 * kernel is important for this test.
 */
#define VIRTIO_VSOCK_MAX_PKT_BUF_SIZE	(1024 * 64)

static void test_stream_rcvlowat_def_cred_upd_client(const struct test_opts *opts)
{
	size_t buf_size;
	void *buf;
	int fd;

	fd = vsock_stream_connect(opts->peer_cid, opts->peer_port);
	if (fd < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	/* Send 1 byte more than peer's buffer size. */
	buf_size = RCVLOWAT_CREDIT_UPD_BUF_SIZE + 1;

	buf = malloc(buf_size);
	if (!buf) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}

	/* Wait until peer sets needed buffer size. */
	recv_byte(fd, 1, 0);

	if (send(fd, buf, buf_size, 0) != buf_size) {
		perror("send failed");
		exit(EXIT_FAILURE);
	}

	free(buf);
	close(fd);
}

static void test_stream_credit_update_test(const struct test_opts *opts,
					   bool low_rx_bytes_test)
{
	int recv_buf_size;
	struct pollfd fds;
	size_t buf_size;
	unsigned long long sock_buf_size;
	void *buf;
	int fd;

	fd = vsock_stream_accept(VMADDR_CID_ANY, opts->peer_port, NULL);
	if (fd < 0) {
		perror("accept");
		exit(EXIT_FAILURE);
	}

	buf_size = RCVLOWAT_CREDIT_UPD_BUF_SIZE;

	/* size_t can be < unsigned long long */
	sock_buf_size = buf_size;

	setsockopt_ull_check(fd, AF_VSOCK, SO_VM_SOCKETS_BUFFER_SIZE,
			     sock_buf_size,
			     "setsockopt(SO_VM_SOCKETS_BUFFER_SIZE)");

	if (low_rx_bytes_test) {
		/* Set new SO_RCVLOWAT here. This enables sending credit
		 * update when number of bytes if our rx queue become <
		 * SO_RCVLOWAT value.
		 */
		recv_buf_size = 1 + VIRTIO_VSOCK_MAX_PKT_BUF_SIZE;

		setsockopt_int_check(fd, SOL_SOCKET, SO_RCVLOWAT,
				     recv_buf_size, "setsockopt(SO_RCVLOWAT)");
	}

	/* Send one dummy byte here, because 'setsockopt()' above also
	 * sends special packet which tells sender to update our buffer
	 * size. This 'send_byte()' will serialize such packet with data
	 * reads in a loop below. Sender starts transmission only when
	 * it receives this single byte.
	 */
	send_byte(fd, 1, 0);

	buf = malloc(buf_size);
	if (!buf) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}

	/* Wait until there will be 128KB of data in rx queue. */
	while (1) {
		ssize_t res;

		res = recv(fd, buf, buf_size, MSG_PEEK);
		if (res == buf_size)
			break;

		if (res <= 0) {
			fprintf(stderr, "unexpected 'recv()' return: %zi\n", res);
			exit(EXIT_FAILURE);
		}
	}

	/* There is 128KB of data in the socket's rx queue, dequeue first
	 * 64KB, credit update is sent if 'low_rx_bytes_test' == true.
	 * Otherwise, credit update is sent in 'if (!low_rx_bytes_test)'.
	 */
	recv_buf_size = VIRTIO_VSOCK_MAX_PKT_BUF_SIZE;
	recv_buf(fd, buf, recv_buf_size, 0, recv_buf_size);

	if (!low_rx_bytes_test) {
		recv_buf_size++;

		/* Updating SO_RCVLOWAT will send credit update. */
		setsockopt_int_check(fd, SOL_SOCKET, SO_RCVLOWAT,
				     recv_buf_size, "setsockopt(SO_RCVLOWAT)");
	}

	fds.fd = fd;
	fds.events = POLLIN | POLLRDNORM | POLLERR |
		     POLLRDHUP | POLLHUP;

	/* This 'poll()' will return once we receive last byte
	 * sent by client.
	 */
	if (poll(&fds, 1, -1) < 0) {
		perror("poll");
		exit(EXIT_FAILURE);
	}

	if (fds.revents & POLLERR) {
		fprintf(stderr, "'poll()' error\n");
		exit(EXIT_FAILURE);
	}

	if (fds.revents & (POLLIN | POLLRDNORM)) {
		recv_buf(fd, buf, recv_buf_size, MSG_DONTWAIT, recv_buf_size);
	} else {
		/* These flags must be set, as there is at
		 * least 64KB of data ready to read.
		 */
		fprintf(stderr, "POLLIN | POLLRDNORM expected\n");
		exit(EXIT_FAILURE);
	}

	free(buf);
	close(fd);
}

static void test_stream_cred_upd_on_low_rx_bytes(const struct test_opts *opts)
{
	test_stream_credit_update_test(opts, true);
}

static void test_stream_cred_upd_on_set_rcvlowat(const struct test_opts *opts)
{
	test_stream_credit_update_test(opts, false);
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
	{
		.name = "SOCK_SEQPACKET timeout",
		.run_client = test_seqpacket_timeout_client,
		.run_server = test_seqpacket_timeout_server,
	},
	{
		.name = "SOCK_SEQPACKET invalid receive buffer",
		.run_client = test_seqpacket_invalid_rec_buffer_client,
		.run_server = test_seqpacket_invalid_rec_buffer_server,
	},
	{
		.name = "SOCK_STREAM poll() + SO_RCVLOWAT",
		.run_client = test_stream_poll_rcvlowat_client,
		.run_server = test_stream_poll_rcvlowat_server,
	},
	{
		.name = "SOCK_SEQPACKET big message",
		.run_client = test_seqpacket_bigmsg_client,
		.run_server = test_seqpacket_bigmsg_server,
	},
	{
		.name = "SOCK_STREAM test invalid buffer",
		.run_client = test_stream_inv_buf_client,
		.run_server = test_stream_inv_buf_server,
	},
	{
		.name = "SOCK_SEQPACKET test invalid buffer",
		.run_client = test_seqpacket_inv_buf_client,
		.run_server = test_seqpacket_inv_buf_server,
	},
	{
		.name = "SOCK_STREAM virtio skb merge",
		.run_client = test_stream_virtio_skb_merge_client,
		.run_server = test_stream_virtio_skb_merge_server,
	},
	{
		.name = "SOCK_SEQPACKET MSG_PEEK",
		.run_client = test_seqpacket_msg_peek_client,
		.run_server = test_seqpacket_msg_peek_server,
	},
	{
		.name = "SOCK_STREAM SHUT_WR",
		.run_client = test_stream_shutwr_client,
		.run_server = test_stream_shutwr_server,
	},
	{
		.name = "SOCK_STREAM SHUT_RD",
		.run_client = test_stream_shutrd_client,
		.run_server = test_stream_shutrd_server,
	},
	{
		.name = "SOCK_STREAM MSG_ZEROCOPY",
		.run_client = test_stream_msgzcopy_client,
		.run_server = test_stream_msgzcopy_server,
	},
	{
		.name = "SOCK_SEQPACKET MSG_ZEROCOPY",
		.run_client = test_seqpacket_msgzcopy_client,
		.run_server = test_seqpacket_msgzcopy_server,
	},
	{
		.name = "SOCK_STREAM MSG_ZEROCOPY empty MSG_ERRQUEUE",
		.run_client = test_stream_msgzcopy_empty_errq_client,
		.run_server = test_stream_msgzcopy_empty_errq_server,
	},
	{
		.name = "SOCK_STREAM double bind connect",
		.run_client = test_double_bind_connect_client,
		.run_server = test_double_bind_connect_server,
	},
	{
		.name = "SOCK_STREAM virtio credit update + SO_RCVLOWAT",
		.run_client = test_stream_rcvlowat_def_cred_upd_client,
		.run_server = test_stream_cred_upd_on_set_rcvlowat,
	},
	{
		.name = "SOCK_STREAM virtio credit update + low rx_bytes",
		.run_client = test_stream_rcvlowat_def_cred_upd_client,
		.run_server = test_stream_cred_upd_on_low_rx_bytes,
	},
	{
		.name = "SOCK_STREAM ioctl(SIOCOUTQ) 0 unsent bytes",
		.run_client = test_stream_unsent_bytes_client,
		.run_server = test_stream_unsent_bytes_server,
	},
	{
		.name = "SOCK_SEQPACKET ioctl(SIOCOUTQ) 0 unsent bytes",
		.run_client = test_seqpacket_unsent_bytes_client,
		.run_server = test_seqpacket_unsent_bytes_server,
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
		.name = "peer-port",
		.has_arg = required_argument,
		.val = 'q',
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
	fprintf(stderr, "Usage: vsock_test [--help] [--control-host=<host>] --control-port=<port> --mode=client|server --peer-cid=<cid> [--peer-port=<port>] [--list] [--skip=<test_id>]\n"
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
		"During the test, two AF_VSOCK ports will be used: the port\n"
		"specified with --peer-port=<port> (or the default port)\n"
		"and the next one.\n"
		"\n"
		"Options:\n"
		"  --help                 This help message\n"
		"  --control-host <host>  Server IP address to connect to\n"
		"  --control-port <port>  Server port to listen on/connect to\n"
		"  --mode client|server   Server or client mode\n"
		"  --peer-cid <cid>       CID of the other side\n"
		"  --peer-port <port>     AF_VSOCK port used for the test [default: %d]\n"
		"  --list                 List of tests that will be executed\n"
		"  --skip <test_id>       Test ID to skip;\n"
		"                         use multiple --skip options to skip more tests\n",
		DEFAULT_PEER_PORT
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
		.peer_port = DEFAULT_PEER_PORT,
	};

	srand(time(NULL));
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
		case 'q':
			opts.peer_port = parse_port(optarg);
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
