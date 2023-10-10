// SPDX-License-Identifier: GPL-2.0-only
/* MSG_ZEROCOPY feature tests for vsock
 *
 * Copyright (C) 2023 SberDevices.
 *
 * Author: Arseniy Krasnov <avkrasnov@salutedevices.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <poll.h>
#include <linux/errqueue.h>
#include <linux/kernel.h>
#include <errno.h>

#include "control.h"
#include "vsock_test_zerocopy.h"
#include "msg_zerocopy_common.h"

#ifndef PAGE_SIZE
#define PAGE_SIZE		4096
#endif

#define VSOCK_TEST_DATA_MAX_IOV 3

struct vsock_test_data {
	/* This test case if for SOCK_STREAM only. */
	bool stream_only;
	/* Data must be zerocopied. This field is checked against
	 * field 'ee_code' of the 'struct sock_extended_err', which
	 * contains bit to detect that zerocopy transmission was
	 * fallbacked to copy mode.
	 */
	bool zerocopied;
	/* Enable SO_ZEROCOPY option on the socket. Without enabled
	 * SO_ZEROCOPY, every MSG_ZEROCOPY transmission will behave
	 * like without MSG_ZEROCOPY flag.
	 */
	bool so_zerocopy;
	/* 'errno' after 'sendmsg()' call. */
	int sendmsg_errno;
	/* Number of valid elements in 'vecs'. */
	int vecs_cnt;
	struct iovec vecs[VSOCK_TEST_DATA_MAX_IOV];
};

static struct vsock_test_data test_data_array[] = {
	/* Last element has non-page aligned size. */
	{
		.zerocopied = true,
		.so_zerocopy = true,
		.sendmsg_errno = 0,
		.vecs_cnt = 3,
		{
			{ NULL, PAGE_SIZE },
			{ NULL, PAGE_SIZE },
			{ NULL, 200 }
		}
	},
	/* All elements have page aligned base and size. */
	{
		.zerocopied = true,
		.so_zerocopy = true,
		.sendmsg_errno = 0,
		.vecs_cnt = 3,
		{
			{ NULL, PAGE_SIZE },
			{ NULL, PAGE_SIZE * 2 },
			{ NULL, PAGE_SIZE * 3 }
		}
	},
	/* All elements have page aligned base and size. But
	 * data length is bigger than 64Kb.
	 */
	{
		.zerocopied = true,
		.so_zerocopy = true,
		.sendmsg_errno = 0,
		.vecs_cnt = 3,
		{
			{ NULL, PAGE_SIZE * 16 },
			{ NULL, PAGE_SIZE * 16 },
			{ NULL, PAGE_SIZE * 16 }
		}
	},
	/* Middle element has both non-page aligned base and size. */
	{
		.zerocopied = true,
		.so_zerocopy = true,
		.sendmsg_errno = 0,
		.vecs_cnt = 3,
		{
			{ NULL, PAGE_SIZE },
			{ (void *)1, 100 },
			{ NULL, PAGE_SIZE }
		}
	},
	/* Middle element is unmapped. */
	{
		.zerocopied = false,
		.so_zerocopy = true,
		.sendmsg_errno = ENOMEM,
		.vecs_cnt = 3,
		{
			{ NULL, PAGE_SIZE },
			{ MAP_FAILED, PAGE_SIZE },
			{ NULL, PAGE_SIZE }
		}
	},
	/* Valid data, but SO_ZEROCOPY is off. This
	 * will trigger fallback to copy.
	 */
	{
		.zerocopied = false,
		.so_zerocopy = false,
		.sendmsg_errno = 0,
		.vecs_cnt = 1,
		{
			{ NULL, PAGE_SIZE }
		}
	},
	/* Valid data, but message is bigger than peer's
	 * buffer, so this will trigger fallback to copy.
	 * This test is for SOCK_STREAM only, because
	 * for SOCK_SEQPACKET, 'sendmsg()' returns EMSGSIZE.
	 */
	{
		.stream_only = true,
		.zerocopied = false,
		.so_zerocopy = true,
		.sendmsg_errno = 0,
		.vecs_cnt = 1,
		{
			{ NULL, 100 * PAGE_SIZE }
		}
	},
};

#define POLL_TIMEOUT_MS		100

static void test_client(const struct test_opts *opts,
			const struct vsock_test_data *test_data,
			bool sock_seqpacket)
{
	struct pollfd fds = { 0 };
	struct msghdr msg = { 0 };
	ssize_t sendmsg_res;
	struct iovec *iovec;
	int fd;

	if (sock_seqpacket)
		fd = vsock_seqpacket_connect(opts->peer_cid, 1234);
	else
		fd = vsock_stream_connect(opts->peer_cid, 1234);

	if (fd < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	if (test_data->so_zerocopy)
		enable_so_zerocopy(fd);

	iovec = alloc_test_iovec(test_data->vecs, test_data->vecs_cnt);

	msg.msg_iov = iovec;
	msg.msg_iovlen = test_data->vecs_cnt;

	errno = 0;

	sendmsg_res = sendmsg(fd, &msg, MSG_ZEROCOPY);
	if (errno != test_data->sendmsg_errno) {
		fprintf(stderr, "expected 'errno' == %i, got %i\n",
			test_data->sendmsg_errno, errno);
		exit(EXIT_FAILURE);
	}

	if (!errno) {
		if (sendmsg_res != iovec_bytes(iovec, test_data->vecs_cnt)) {
			fprintf(stderr, "expected 'sendmsg()' == %li, got %li\n",
				iovec_bytes(iovec, test_data->vecs_cnt),
				sendmsg_res);
			exit(EXIT_FAILURE);
		}
	}

	fds.fd = fd;
	fds.events = 0;

	if (poll(&fds, 1, POLL_TIMEOUT_MS) < 0) {
		perror("poll");
		exit(EXIT_FAILURE);
	}

	if (fds.revents & POLLERR) {
		vsock_recv_completion(fd, &test_data->zerocopied);
	} else if (test_data->so_zerocopy && !test_data->sendmsg_errno) {
		/* If we don't have data in the error queue, but
		 * SO_ZEROCOPY was enabled and 'sendmsg()' was
		 * successful - this is an error.
		 */
		fprintf(stderr, "POLLERR expected\n");
		exit(EXIT_FAILURE);
	}

	if (!test_data->sendmsg_errno)
		control_writeulong(iovec_hash_djb2(iovec, test_data->vecs_cnt));
	else
		control_writeulong(0);

	control_writeln("DONE");
	free_test_iovec(test_data->vecs, iovec, test_data->vecs_cnt);
	close(fd);
}

void test_stream_msgzcopy_client(const struct test_opts *opts)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(test_data_array); i++)
		test_client(opts, &test_data_array[i], false);
}

void test_seqpacket_msgzcopy_client(const struct test_opts *opts)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(test_data_array); i++) {
		if (test_data_array[i].stream_only)
			continue;

		test_client(opts, &test_data_array[i], true);
	}
}

static void test_server(const struct test_opts *opts,
			const struct vsock_test_data *test_data,
			bool sock_seqpacket)
{
	unsigned long remote_hash;
	unsigned long local_hash;
	ssize_t total_bytes_rec;
	unsigned char *data;
	size_t data_len;
	int fd;

	if (sock_seqpacket)
		fd = vsock_seqpacket_accept(VMADDR_CID_ANY, 1234, NULL);
	else
		fd = vsock_stream_accept(VMADDR_CID_ANY, 1234, NULL);

	if (fd < 0) {
		perror("accept");
		exit(EXIT_FAILURE);
	}

	data_len = iovec_bytes(test_data->vecs, test_data->vecs_cnt);

	data = malloc(data_len);
	if (!data) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}

	total_bytes_rec = 0;

	while (total_bytes_rec != data_len) {
		ssize_t bytes_rec;

		bytes_rec = read(fd, data + total_bytes_rec,
				 data_len - total_bytes_rec);
		if (bytes_rec <= 0)
			break;

		total_bytes_rec += bytes_rec;
	}

	if (test_data->sendmsg_errno == 0)
		local_hash = hash_djb2(data, data_len);
	else
		local_hash = 0;

	free(data);

	/* Waiting for some result. */
	remote_hash = control_readulong();
	if (remote_hash != local_hash) {
		fprintf(stderr, "hash mismatch\n");
		exit(EXIT_FAILURE);
	}

	control_expectln("DONE");
	close(fd);
}

void test_stream_msgzcopy_server(const struct test_opts *opts)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(test_data_array); i++)
		test_server(opts, &test_data_array[i], false);
}

void test_seqpacket_msgzcopy_server(const struct test_opts *opts)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(test_data_array); i++) {
		if (test_data_array[i].stream_only)
			continue;

		test_server(opts, &test_data_array[i], true);
	}
}

void test_stream_msgzcopy_empty_errq_client(const struct test_opts *opts)
{
	struct msghdr msg = { 0 };
	char cmsg_data[128];
	ssize_t res;
	int fd;

	fd = vsock_stream_connect(opts->peer_cid, 1234);
	if (fd < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	msg.msg_control = cmsg_data;
	msg.msg_controllen = sizeof(cmsg_data);

	res = recvmsg(fd, &msg, MSG_ERRQUEUE);
	if (res != -1) {
		fprintf(stderr, "expected 'recvmsg(2)' failure, got %zi\n",
			res);
		exit(EXIT_FAILURE);
	}

	control_writeln("DONE");
	close(fd);
}

void test_stream_msgzcopy_empty_errq_server(const struct test_opts *opts)
{
	int fd;

	fd = vsock_stream_accept(VMADDR_CID_ANY, 1234, NULL);
	if (fd < 0) {
		perror("accept");
		exit(EXIT_FAILURE);
	}

	control_expectln("DONE");
	close(fd);
}
