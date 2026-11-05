// SPDX-License-Identifier: GPL-2.0-only
/* io_uring tests for vsock
 *
 * Copyright (C) 2023 SberDevices.
 *
 * Author: Arseniy Krasnov <avkrasnov@salutedevices.com>
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <liburing.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/kernel.h>
#include <linux/sockios.h>
#include <errno.h>
#include <error.h>

#include "util.h"
#include "control.h"
#include "msg_zerocopy_common.h"

#ifndef PAGE_SIZE
#define PAGE_SIZE		4096
#endif

#define RING_ENTRIES_NUM	4

#define VSOCK_TEST_DATA_MAX_IOV 3

#define HINT_CHUNK_SIZE		4096
#define HINT_BUF_GROUP		1
#define HINT_BUF_ENTRIES	4

struct vsock_io_uring_test {
	/* Number of valid elements in 'vecs'. */
	int vecs_cnt;
	struct iovec vecs[VSOCK_TEST_DATA_MAX_IOV];
};

static struct vsock_io_uring_test test_data_array[] = {
	/* All elements have page aligned base and size. */
	{
		.vecs_cnt = 3,
		{
			{ NULL, PAGE_SIZE },
			{ NULL, 2 * PAGE_SIZE },
			{ NULL, 3 * PAGE_SIZE },
		}
	},
	/* Middle element has both non-page aligned base and size. */
	{
		.vecs_cnt = 3,
		{
			{ NULL, PAGE_SIZE },
			{ (void *)1, 200  },
			{ NULL, 3 * PAGE_SIZE },
		}
	}
};

static void vsock_io_uring_client(const struct test_opts *opts,
				  const struct vsock_io_uring_test *test_data,
				  bool msg_zerocopy)
{
	struct io_uring_sqe *sqe;
	struct io_uring_cqe *cqe;
	struct io_uring ring;
	struct iovec *iovec;
	struct msghdr msg;
	int fd;

	fd = vsock_stream_connect(opts->peer_cid, opts->peer_port);
	if (fd < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	if (msg_zerocopy)
		enable_so_zerocopy_check(fd);

	iovec = alloc_test_iovec(test_data->vecs, test_data->vecs_cnt);

	if (io_uring_queue_init(RING_ENTRIES_NUM, &ring, 0))
		error(1, errno, "io_uring_queue_init");

	if (io_uring_register_buffers(&ring, iovec, test_data->vecs_cnt))
		error(1, errno, "io_uring_register_buffers");

	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = iovec;
	msg.msg_iovlen = test_data->vecs_cnt;
	sqe = io_uring_get_sqe(&ring);

	if (msg_zerocopy)
		io_uring_prep_sendmsg_zc(sqe, fd, &msg, 0);
	else
		io_uring_prep_sendmsg(sqe, fd, &msg, 0);

	if (io_uring_submit(&ring) != 1)
		error(1, errno, "io_uring_submit");

	if (io_uring_wait_cqe(&ring, &cqe))
		error(1, errno, "io_uring_wait_cqe");

	io_uring_cqe_seen(&ring, cqe);

	control_writeulong(iovec_hash_djb2(iovec, test_data->vecs_cnt));

	control_writeln("DONE");
	io_uring_queue_exit(&ring);
	free_test_iovec(test_data->vecs, iovec, test_data->vecs_cnt);
	close(fd);
}

static void vsock_io_uring_server(const struct test_opts *opts,
				  const struct vsock_io_uring_test *test_data)
{
	unsigned long remote_hash;
	unsigned long local_hash;
	struct io_uring ring;
	size_t data_len;
	size_t recv_len;
	void *data;
	int fd;

	fd = vsock_stream_accept(VMADDR_CID_ANY, opts->peer_port, NULL);
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

	if (io_uring_queue_init(RING_ENTRIES_NUM, &ring, 0))
		error(1, errno, "io_uring_queue_init");

	recv_len = 0;

	while (recv_len < data_len) {
		struct io_uring_sqe *sqe;
		struct io_uring_cqe *cqe;
		struct iovec iovec;

		sqe = io_uring_get_sqe(&ring);
		iovec.iov_base = data + recv_len;
		iovec.iov_len = data_len;

		io_uring_prep_readv(sqe, fd, &iovec, 1, 0);

		if (io_uring_submit(&ring) != 1)
			error(1, errno, "io_uring_submit");

		if (io_uring_wait_cqe(&ring, &cqe))
			error(1, errno, "io_uring_wait_cqe");

		recv_len += cqe->res;
		io_uring_cqe_seen(&ring, cqe);
	}

	if (recv_len != data_len) {
		fprintf(stderr, "expected %zu, got %zu\n", data_len,
			recv_len);
		exit(EXIT_FAILURE);
	}

	local_hash = hash_djb2(data, data_len);

	remote_hash = control_readulong();
	if (remote_hash != local_hash) {
		fprintf(stderr, "hash mismatch\n");
		exit(EXIT_FAILURE);
	}

	control_expectln("DONE");
	io_uring_queue_exit(&ring);
	free(data);
}

void test_stream_uring_server(const struct test_opts *opts)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(test_data_array); i++)
		vsock_io_uring_server(opts, &test_data_array[i]);
}

void test_stream_uring_client(const struct test_opts *opts)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(test_data_array); i++)
		vsock_io_uring_client(opts, &test_data_array[i], false);
}

void test_stream_uring_msg_zc_server(const struct test_opts *opts)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(test_data_array); i++)
		vsock_io_uring_server(opts, &test_data_array[i]);
}

void test_stream_uring_msg_zc_client(const struct test_opts *opts)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(test_data_array); i++)
		vsock_io_uring_client(opts, &test_data_array[i], true);
}

struct uring_inq_ctx {
	struct io_uring ring;
	int fd;
};

static void inq_server_init(struct uring_inq_ctx *ctx,
			    const struct test_opts *opts)
{
	ctx->fd = vsock_stream_accept(VMADDR_CID_ANY, opts->peer_port, NULL);
	if (ctx->fd < 0) {
		perror("accept");
		exit(EXIT_FAILURE);
	}

	if (io_uring_queue_init(RING_ENTRIES_NUM, &ctx->ring, 0))
		error(1, errno, "io_uring_queue_init");
}

static void inq_server_exit(struct uring_inq_ctx *ctx)
{
	io_uring_queue_exit(&ctx->ring);
	close(ctx->fd);
}

/* Submit a single receive and report both its result and its CQE flags. */
static int inq_recv(struct uring_inq_ctx *ctx, void *buf, size_t len,
		    int flags, unsigned int *cflags)
{
	struct io_uring_sqe *sqe;
	struct io_uring_cqe *cqe;
	int res;

	sqe = io_uring_get_sqe(&ctx->ring);
	io_uring_prep_recv(sqe, ctx->fd, buf, len, flags);

	if (io_uring_submit(&ctx->ring) != 1)
		error(1, errno, "io_uring_submit");

	if (io_uring_wait_cqe(&ctx->ring, &cqe))
		error(1, errno, "io_uring_wait_cqe");

	res = cqe->res;
	*cflags = cqe->flags;
	io_uring_cqe_seen(&ctx->ring, cqe);

	return res;
}

static void expect_uring_nonempty(unsigned int cflags, bool expected,
				  const char *what)
{
	bool nonempty = !!(cflags & IORING_CQE_F_SOCK_NONEMPTY);

	if (nonempty != expected) {
		fprintf(stderr, "%s: expected SOCK_NONEMPTY %d, got %d\n",
			what, expected, nonempty);
		exit(EXIT_FAILURE);
	}
}

static void expect_uring_more(unsigned int cflags, bool expected,
			      const char *what)
{
	bool more = !!(cflags & IORING_CQE_F_MORE);

	if (more != expected) {
		fprintf(stderr, "%s: expected F_MORE %d, got %d\n",
			what, expected, more);
		exit(EXIT_FAILURE);
	}
}

/* Wait until the whole payload is queued, so the hint is deterministic.
 * Return false if the test has to be skipped.
 */
static bool inq_wait_queued(int fd, int len)
{
	if (!vsock_ioctl_int(fd, SIOCINQ, len)) {
		fprintf(stderr, "Test skipped, SIOCINQ not supported.\n");
		return false;
	}

	return true;
}

static void inq_send_chunks(const struct test_opts *opts, int chunks)
{
	char buf[HINT_CHUNK_SIZE];
	int fd, i;

	fd = vsock_stream_connect(opts->peer_cid, opts->peer_port);
	if (fd < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	memset(buf, 0xa5, sizeof(buf));
	for (i = 0; i < chunks; i++)
		send_buf(fd, buf, sizeof(buf), 0, sizeof(buf));

	control_writeln("SENT");
	control_expectln("DONE");
	close(fd);
}

static void test_stream_uring_inq_client(const struct test_opts *opts)
{
	inq_send_chunks(opts, 2);
}

static void test_stream_uring_inq_server(const struct test_opts *opts)
{
	char buf[HINT_CHUNK_SIZE];
	struct uring_inq_ctx ctx;
	unsigned int cflags;
	int res;

	inq_server_init(&ctx, opts);

	control_expectln("SENT");
	if (!inq_wait_queued(ctx.fd, 2 * HINT_CHUNK_SIZE))
		goto out;

	/* Data remains after this receive, so the flag must be set. */
	res = inq_recv(&ctx, buf, sizeof(buf), 0, &cflags);
	expect_res(res, HINT_CHUNK_SIZE, "partial receive");
	expect_uring_nonempty(cflags, true, "partial receive");

	/* This receive drains the queue while the peer stays connected. */
	res = inq_recv(&ctx, buf, sizeof(buf), 0, &cflags);
	expect_res(res, HINT_CHUNK_SIZE, "draining receive");
	expect_uring_nonempty(cflags, false, "draining receive");

out:
	control_writeln("DONE");
	inq_server_exit(&ctx);
}

static void test_stream_uring_inq_eof_client(const struct test_opts *opts)
{
	char buf[HINT_CHUNK_SIZE];
	int fd;

	fd = vsock_stream_connect(opts->peer_cid, opts->peer_port);
	if (fd < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	memset(buf, 0x5a, sizeof(buf));
	send_buf(fd, buf, sizeof(buf), 0, sizeof(buf));
	control_writeln("SENT");

	control_expectln("DRAINED");
	close(fd);
	control_writeln("CLOSED");
}

static void test_stream_uring_inq_eof_server(const struct test_opts *opts)
{
	char buf[HINT_CHUNK_SIZE];
	struct uring_inq_ctx ctx;
	unsigned int cflags;
	int res;

	inq_server_init(&ctx, opts);

	control_expectln("SENT");
	if (!inq_wait_queued(ctx.fd, HINT_CHUNK_SIZE)) {
		control_writeln("DRAINED");
		control_expectln("CLOSED");
		goto out;
	}

	res = inq_recv(&ctx, buf, sizeof(buf), 0, &cflags);
	expect_res(res, HINT_CHUNK_SIZE, "drain before EOF");
	expect_uring_nonempty(cflags, false, "drain before EOF");

	control_writeln("DRAINED");
	control_expectln("CLOSED");

	/* The queue is empty and the peer is gone.  The hint stays non-zero
	 * so that this receive happens and reports EOF, as TCP does after a
	 * FIN.
	 */
	res = inq_recv(&ctx, buf, sizeof(buf), 0, &cflags);
	expect_res(res, 0, "receive at EOF");
	expect_uring_nonempty(cflags, true, "receive at EOF");

out:
	inq_server_exit(&ctx);
}

static void test_stream_uring_inq_empty_client(const struct test_opts *opts)
{
	int fd;

	fd = vsock_stream_connect(opts->peer_cid, opts->peer_port);
	if (fd < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	control_writeln("READY");
	control_expectln("DONE");
	close(fd);
}

static void test_stream_uring_inq_empty_server(const struct test_opts *opts)
{
	char buf[HINT_CHUNK_SIZE];
	struct uring_inq_ctx ctx;
	unsigned int cflags;
	int res;

	inq_server_init(&ctx, opts);

	control_expectln("READY");

	/* A failed receive must not leave a stale positive hint. */
	res = inq_recv(&ctx, buf, sizeof(buf), MSG_DONTWAIT, &cflags);
	expect_res(res, -EAGAIN, "empty nonblocking receive");
	expect_uring_nonempty(cflags, false, "empty nonblocking receive");

	control_writeln("DONE");
	inq_server_exit(&ctx);
}

static void test_stream_uring_inq_zerolen_client(const struct test_opts *opts)
{
	inq_send_chunks(opts, 1);
}

static void test_stream_uring_inq_zerolen_server(const struct test_opts *opts)
{
	char buf[HINT_CHUNK_SIZE];
	struct uring_inq_ctx ctx;
	unsigned int cflags;
	int res;

	inq_server_init(&ctx, opts);

	control_expectln("SENT");
	if (!inq_wait_queued(ctx.fd, HINT_CHUNK_SIZE))
		goto out;

	/* A zero-length request is not an error and still describes the
	 * queue behind it.
	 */
	res = inq_recv(&ctx, buf, 0, 0, &cflags);
	expect_res(res, 0, "zero-length receive");
	expect_uring_nonempty(cflags, true, "zero-length receive");

out:
	control_writeln("DONE");
	inq_server_exit(&ctx);
}

static void test_stream_uring_inq_mshot_client(const struct test_opts *opts)
{
	char buf[HINT_CHUNK_SIZE];
	int fd, i;

	fd = vsock_stream_connect(opts->peer_cid, opts->peer_port);
	if (fd < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	memset(buf, 0x3c, sizeof(buf));
	for (i = 0; i < 2; i++)
		send_buf(fd, buf, sizeof(buf), 0, sizeof(buf));
	control_writeln("SENT");

	control_expectln("DRAINED");
	close(fd);
	control_writeln("CLOSED");

	control_expectln("DONE");
}

static void test_stream_uring_inq_mshot_server(const struct test_opts *opts)
{
	static char bufs[HINT_BUF_ENTRIES][HINT_CHUNK_SIZE];
	struct io_uring_buf_ring *br;
	struct uring_inq_ctx ctx;
	struct io_uring_sqe *sqe;
	struct io_uring_cqe *cqe;
	int i, ret;

	inq_server_init(&ctx, opts);

	br = io_uring_setup_buf_ring(&ctx.ring, HINT_BUF_ENTRIES,
				     HINT_BUF_GROUP, 0, &ret);
	if (!br) {
		fprintf(stderr, "io_uring_setup_buf_ring: %d\n", ret);
		exit(EXIT_FAILURE);
	}

	for (i = 0; i < HINT_BUF_ENTRIES; i++)
		io_uring_buf_ring_add(br, bufs[i], HINT_CHUNK_SIZE, i,
				      io_uring_buf_ring_mask(HINT_BUF_ENTRIES),
				      i);
	io_uring_buf_ring_advance(br, HINT_BUF_ENTRIES);

	control_expectln("SENT");
	if (!inq_wait_queued(ctx.fd, 2 * HINT_CHUNK_SIZE)) {
		control_writeln("DRAINED");
		control_expectln("CLOSED");
		goto out;
	}

	/* Arm only once both chunks are queued, so every completion knows
	 * what is left behind it.
	 */
	sqe = io_uring_get_sqe(&ctx.ring);
	io_uring_prep_recv_multishot(sqe, ctx.fd, NULL, 0, 0);
	sqe->flags |= IOSQE_BUFFER_SELECT;
	sqe->buf_group = HINT_BUF_GROUP;

	if (io_uring_submit(&ctx.ring) != 1)
		error(1, errno, "io_uring_submit");

	/* A buffer holds one chunk, so the other one is still queued. */
	if (io_uring_wait_cqe(&ctx.ring, &cqe))
		error(1, errno, "io_uring_wait_cqe");

	expect_res(cqe->res, HINT_CHUNK_SIZE, "multishot first chunk");
	expect_uring_nonempty(cqe->flags, true, "multishot first chunk");
	expect_uring_more(cqe->flags, true, "multishot first chunk");
	io_uring_cqe_seen(&ctx.ring, cqe);

	/* This completion drains the queue and keeps the request armed. */
	if (io_uring_wait_cqe(&ctx.ring, &cqe))
		error(1, errno, "io_uring_wait_cqe");

	expect_res(cqe->res, HINT_CHUNK_SIZE, "multishot second chunk");
	expect_uring_nonempty(cqe->flags, false, "multishot second chunk");
	expect_uring_more(cqe->flags, true, "multishot second chunk");
	io_uring_cqe_seen(&ctx.ring, cqe);

	control_writeln("DRAINED");
	control_expectln("CLOSED");

	/* EOF ends multishot regardless of the hint. */
	if (io_uring_wait_cqe(&ctx.ring, &cqe))
		error(1, errno, "io_uring_wait_cqe");

	expect_res(cqe->res, 0, "multishot EOF");
	expect_uring_more(cqe->flags, false, "multishot EOF");
	io_uring_cqe_seen(&ctx.ring, cqe);

out:
	control_writeln("DONE");
	io_uring_free_buf_ring(&ctx.ring, br, HINT_BUF_ENTRIES,
			       HINT_BUF_GROUP);
	inq_server_exit(&ctx);
}

static struct test_case test_cases[] = {
	{
		.name = "SOCK_STREAM io_uring test",
		.run_server = test_stream_uring_server,
		.run_client = test_stream_uring_client,
	},
	{
		.name = "SOCK_STREAM io_uring MSG_ZEROCOPY test",
		.run_server = test_stream_uring_msg_zc_server,
		.run_client = test_stream_uring_msg_zc_client,
	},
	{
		.name = "SOCK_STREAM io_uring receive queue hint",
		.run_server = test_stream_uring_inq_server,
		.run_client = test_stream_uring_inq_client,
	},
	{
		.name = "SOCK_STREAM io_uring receive hint at EOF",
		.run_server = test_stream_uring_inq_eof_server,
		.run_client = test_stream_uring_inq_eof_client,
	},
	{
		.name = "SOCK_STREAM io_uring receive hint on empty queue",
		.run_server = test_stream_uring_inq_empty_server,
		.run_client = test_stream_uring_inq_empty_client,
	},
	{
		.name = "SOCK_STREAM io_uring receive hint zero-length",
		.run_server = test_stream_uring_inq_zerolen_server,
		.run_client = test_stream_uring_inq_zerolen_client,
	},
	{
		.name = "SOCK_STREAM io_uring multishot receive hint",
		.run_server = test_stream_uring_inq_mshot_server,
		.run_client = test_stream_uring_inq_mshot_client,
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
		.name = "help",
		.has_arg = no_argument,
		.val = '?',
	},
	{},
};

static void usage(void)
{
	fprintf(stderr, "Usage: vsock_uring_test [--help] [--control-host=<host>] --control-port=<port> --mode=client|server --peer-cid=<cid> [--peer-port=<port>]\n"
		"\n"
		"  Server: vsock_uring_test --control-port=1234 --mode=server --peer-cid=3\n"
		"  Client: vsock_uring_test --control-host=192.168.0.1 --control-port=1234 --mode=client --peer-cid=2\n"
		"\n"
		"Run transmission tests using io_uring. Usage is the same as\n"
		"in ./vsock_test\n"
		"\n"
		"Options:\n"
		"  --help                 This help message\n"
		"  --control-host <host>  Server IP address to connect to\n"
		"  --control-port <port>  Server port to listen on/connect to\n"
		"  --mode client|server   Server or client mode\n"
		"  --peer-cid <cid>       CID of the other side\n"
		"  --peer-port <port>     AF_VSOCK port used for the test [default: %d]\n",
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
			if (strcmp(optarg, "client") == 0) {
				opts.mode = TEST_MODE_CLIENT;
			} else if (strcmp(optarg, "server") == 0) {
				opts.mode = TEST_MODE_SERVER;
			} else {
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

	return 0;
}
