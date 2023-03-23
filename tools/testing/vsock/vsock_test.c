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

#define SOCK_BUF_SIZE (2 * 1024 * 1024)
#define MAX_MSG_SIZE (32 * 1024)

static void test_seqpacket_msg_bounds_client(const struct test_opts *opts)
{
	unsigned long curr_hash;
	int page_size;
	int msg_count;
	int fd;

	fd = vsock_seqpacket_connect(opts->peer_cid, 1234);
	if (fd < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	/* Wait, until receiver sets buffer size. */
	control_expectln("SRVREADY");

	curr_hash = 0;
	page_size = getpagesize();
	msg_count = SOCK_BUF_SIZE / MAX_MSG_SIZE;

	for (int i = 0; i < msg_count; i++) {
		ssize_t send_size;
		size_t buf_size;
		int flags;
		void *buf;

		/* Use "small" buffers and "big" buffers. */
		if (i & 1)
			buf_size = page_size +
					(rand() % (MAX_MSG_SIZE - page_size));
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

		send_size = send(fd, buf, buf_size, flags);

		if (send_size < 0) {
			perror("send");
			exit(EXIT_FAILURE);
		}

		if (send_size != buf_size) {
			fprintf(stderr, "Invalid send size\n");
			exit(EXIT_FAILURE);
		}

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
	unsigned long sock_buf_size;
	unsigned long remote_hash;
	unsigned long curr_hash;
	int fd;
	char buf[MAX_MSG_SIZE];
	struct msghdr msg = {0};
	struct iovec iov = {0};

	fd = vsock_seqpacket_accept(VMADDR_CID_ANY, 1234, NULL);
	if (fd < 0) {
		perror("accept");
		exit(EXIT_FAILURE);
	}

	sock_buf_size = SOCK_BUF_SIZE;

	if (setsockopt(fd, AF_VSOCK, SO_VM_SOCKETS_BUFFER_MAX_SIZE,
		       &sock_buf_size, sizeof(sock_buf_size))) {
		perror("setsockopt(SO_VM_SOCKETS_BUFFER_MAX_SIZE)");
		exit(EXIT_FAILURE);
	}

	if (setsockopt(fd, AF_VSOCK, SO_VM_SOCKETS_BUFFER_SIZE,
		       &sock_buf_size, sizeof(sock_buf_size))) {
		perror("setsockopt(SO_VM_SOCKETS_BUFFER_SIZE)");
		exit(EXIT_FAILURE);
	}

	/* Ready to receive data. */
	control_writeln("SRVREADY");
	/* Wait, until peer sends whole data. */
	control_expectln("SENDDONE");
	iov.iov_base = buf;
	iov.iov_len = sizeof(buf);
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

	fd = vsock_seqpacket_connect(opts->peer_cid, 1234);
	if (fd < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	tv.tv_sec = RCVTIMEO_TIMEOUT_SEC;
	tv.tv_usec = 0;

	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (void *)&tv, sizeof(tv)) == -1) {
		perror("setsockopt(SO_RCVTIMEO)");
		exit(EXIT_FAILURE);
	}

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

	fd = vsock_seqpacket_accept(VMADDR_CID_ANY, 1234, NULL);
	if (fd < 0) {
		perror("accept");
		exit(EXIT_FAILURE);
	}

	control_expectln("WAITDONE");
	close(fd);
}

static void test_seqpacket_bigmsg_client(const struct test_opts *opts)
{
	unsigned long sock_buf_size;
	ssize_t send_size;
	socklen_t len;
	void *data;
	int fd;

	len = sizeof(sock_buf_size);

	fd = vsock_seqpacket_connect(opts->peer_cid, 1234);
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

	data = malloc(sock_buf_size);
	if (!data) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}

	send_size = send(fd, data, sock_buf_size, 0);
	if (send_size != -1) {
		fprintf(stderr, "expected 'send(2)' failure, got %zi\n",
			send_size);
		exit(EXIT_FAILURE);
	}

	if (errno != EMSGSIZE) {
		fprintf(stderr, "expected EMSGSIZE in 'errno', got %i\n",
			errno);
		exit(EXIT_FAILURE);
	}

	control_writeln("CLISENT");

	free(data);
	close(fd);
}

static void test_seqpacket_bigmsg_server(const struct test_opts *opts)
{
	int fd;

	fd = vsock_seqpacket_accept(VMADDR_CID_ANY, 1234, NULL);
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

	fd = vsock_seqpacket_connect(opts->peer_cid, 1234);
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

	if (send(fd, buf1, buf_size, 0) != buf_size) {
		perror("send failed");
		exit(EXIT_FAILURE);
	}

	if (send(fd, buf2, buf_size, 0) != buf_size) {
		perror("send failed");
		exit(EXIT_FAILURE);
	}

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

	fd = vsock_seqpacket_accept(VMADDR_CID_ANY, 1234, NULL);
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

	if (errno != ENOMEM) {
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

	fd = vsock_stream_accept(VMADDR_CID_ANY, 1234, NULL);
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
	unsigned long lowat_val = RCVLOWAT_BUF_SIZE;
	char buf[RCVLOWAT_BUF_SIZE];
	struct pollfd fds;
	ssize_t read_res;
	short poll_flags;
	int fd;

	fd = vsock_stream_connect(opts->peer_cid, 1234);
	if (fd < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	if (setsockopt(fd, SOL_SOCKET, SO_RCVLOWAT,
		       &lowat_val, sizeof(lowat_val))) {
		perror("setsockopt(SO_RCVLOWAT)");
		exit(EXIT_FAILURE);
	}

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
	read_res = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
	if (read_res != RCVLOWAT_BUF_SIZE) {
		fprintf(stderr, "Unexpected recv result %zi\n",
			read_res);
		exit(EXIT_FAILURE);
	}

	control_writeln("POLLDONE");

	close(fd);
}

#define INV_BUF_TEST_DATA_LEN 512

static void test_inv_buf_client(const struct test_opts *opts, bool stream)
{
	unsigned char data[INV_BUF_TEST_DATA_LEN] = {0};
	ssize_t ret;
	int fd;

	if (stream)
		fd = vsock_stream_connect(opts->peer_cid, 1234);
	else
		fd = vsock_seqpacket_connect(opts->peer_cid, 1234);

	if (fd < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	control_expectln("SENDDONE");

	/* Use invalid buffer here. */
	ret = recv(fd, NULL, sizeof(data), 0);
	if (ret != -1) {
		fprintf(stderr, "expected recv(2) failure, got %zi\n", ret);
		exit(EXIT_FAILURE);
	}

	if (errno != ENOMEM) {
		fprintf(stderr, "unexpected recv(2) errno %d\n", errno);
		exit(EXIT_FAILURE);
	}

	ret = recv(fd, data, sizeof(data), MSG_DONTWAIT);

	if (stream) {
		/* For SOCK_STREAM we must continue reading. */
		if (ret != sizeof(data)) {
			fprintf(stderr, "expected recv(2) success, got %zi\n", ret);
			exit(EXIT_FAILURE);
		}
		/* Don't check errno in case of success. */
	} else {
		/* For SOCK_SEQPACKET socket's queue must be empty. */
		if (ret != -1) {
			fprintf(stderr, "expected recv(2) failure, got %zi\n", ret);
			exit(EXIT_FAILURE);
		}

		if (errno != EAGAIN) {
			fprintf(stderr, "unexpected recv(2) errno %d\n", errno);
			exit(EXIT_FAILURE);
		}
	}

	control_writeln("DONE");

	close(fd);
}

static void test_inv_buf_server(const struct test_opts *opts, bool stream)
{
	unsigned char data[INV_BUF_TEST_DATA_LEN] = {0};
	ssize_t res;
	int fd;

	if (stream)
		fd = vsock_stream_accept(VMADDR_CID_ANY, 1234, NULL);
	else
		fd = vsock_seqpacket_accept(VMADDR_CID_ANY, 1234, NULL);

	if (fd < 0) {
		perror("accept");
		exit(EXIT_FAILURE);
	}

	res = send(fd, data, sizeof(data), 0);
	if (res != sizeof(data)) {
		fprintf(stderr, "unexpected send(2) result %zi\n", res);
		exit(EXIT_FAILURE);
	}

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
