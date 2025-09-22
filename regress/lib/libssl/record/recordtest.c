/* $OpenBSD: recordtest.c,v 1.5 2022/06/10 22:00:15 tb Exp $ */
/*
 * Copyright (c) 2019 Joel Sing <jsing@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <err.h>
#include <string.h>

#include <openssl/ssl.h>

#include "tls13_internal.h"
#include "tls13_record.h"

/* Valid record. */
static uint8_t test_record_1[] = {
	0x16, 0x03, 0x03, 0x00, 0x7a, 0x02, 0x00, 0x00,
	0x76, 0x03, 0x03, 0x14, 0xae, 0x2b, 0x6d, 0x58,
	0xe9, 0x79, 0x9d, 0xd4, 0x90, 0x52, 0x90, 0x13,
	0x1c, 0x08, 0xaa, 0x3f, 0x5b, 0xfb, 0x64, 0xfe,
	0x9a, 0xca, 0x73, 0x6d, 0x87, 0x8d, 0x8b, 0x3b,
	0x70, 0x14, 0xa3, 0x20, 0xd7, 0x50, 0xa4, 0xe5,
	0x17, 0x42, 0x5d, 0xce, 0xe6, 0xfe, 0x1b, 0x59,
	0x27, 0x6b, 0xff, 0xc8, 0x40, 0xc7, 0xac, 0x16,
	0x32, 0xe6, 0x5b, 0xd2, 0xd9, 0xd4, 0xb5, 0x3f,
	0x8f, 0x74, 0x6e, 0x7d, 0x13, 0x02, 0x00, 0x00,
	0x2e, 0x00, 0x33, 0x00, 0x24, 0x00, 0x1d, 0x00,
	0x20, 0x72, 0xb0, 0xaf, 0x7f, 0xf5, 0x89, 0x0f,
	0xcd, 0x6e, 0x45, 0xb1, 0x51, 0xa0, 0xbd, 0x1e,
	0xee, 0x7e, 0xf1, 0xa5, 0xc5, 0xc6, 0x7e, 0x5f,
	0x6a, 0xca, 0xc9, 0xe4, 0xae, 0xb9, 0x50, 0x76,
	0x0a, 0x00, 0x2b, 0x00, 0x02, 0x03, 0x04,
};

/* Truncated record. */
static uint8_t test_record_2[] = {
	0x17, 0x03, 0x03, 0x41, 0x00, 0x02, 0x00, 0x00,
};

/* Oversized and truncated record. */
static uint8_t test_record_3[] = {
	0x17, 0x03, 0x03, 0x41, 0x01, 0x02, 0x00, 0x00,
};

static void
hexdump(const unsigned char *buf, size_t len)
{
	size_t i;

	for (i = 1; i <= len; i++)
		fprintf(stderr, " 0x%02x,%s", buf[i - 1], i % 8 ? "" : "\n");
	if (len % 8 != 0)
		fprintf(stderr, "\n");
}

struct rw_state {
	uint8_t *buf;
	size_t len;
	size_t offset;
	uint8_t eof;
};

static ssize_t
read_cb(void *buf, size_t buflen, void *cb_arg)
{
	struct rw_state *rs = cb_arg;
	ssize_t n;

	if (rs->eof)
		return TLS13_IO_EOF;

	if ((size_t)(n = buflen) > (rs->len - rs->offset))
		n = rs->len - rs->offset;

	if (n == 0)
		return TLS13_IO_WANT_POLLIN;

	memcpy(buf, &rs->buf[rs->offset], n);
	rs->offset += n;

	return n;
}

static ssize_t
write_cb(const void *buf, size_t buflen, void *cb_arg)
{
	struct rw_state *ws = cb_arg;
	ssize_t n;

	if (ws->eof)
		return TLS13_IO_EOF;

	if ((size_t)(n = buflen) > (ws->len - ws->offset))
		n = ws->len - ws->offset;

	if (n == 0)
		return TLS13_IO_WANT_POLLOUT;

	memcpy(&ws->buf[ws->offset], buf, n);
	ws->offset += n;

	return n;
}

struct record_test {
	size_t rw_len;
	int eof;
	ssize_t want_ret;
};

struct record_recv_test {
	uint8_t *read_buf;
	struct record_test rt[10];
	uint8_t want_content_type;
	uint8_t *want_data;
	size_t want_len;
};

struct record_recv_test record_recv_tests[] = {
	{
		.read_buf = test_record_1,
		.rt = {
			{
				.rw_len = sizeof(test_record_1),
				.want_ret = sizeof(test_record_1),
			},
		},
		.want_content_type = SSL3_RT_HANDSHAKE,
		.want_data = test_record_1,
		.want_len = sizeof(test_record_1),
	},
	{
		.read_buf = test_record_1,
		.rt = {
			{
				.rw_len = 0,
				.want_ret = TLS13_IO_WANT_POLLIN,
			},
			{
				.rw_len = sizeof(test_record_1),
				.want_ret = sizeof(test_record_1),
			},
		},
		.want_content_type = SSL3_RT_HANDSHAKE,
		.want_data = test_record_1,
		.want_len = sizeof(test_record_1),
	},
	{
		.read_buf = test_record_1,
		.rt = {
			{
				.rw_len = 0,
				.want_ret = TLS13_IO_WANT_POLLIN,
			},
			{
				.rw_len = 5,
				.want_ret = TLS13_IO_WANT_POLLIN,
			},
			{
				.rw_len = sizeof(test_record_1),
				.want_ret = sizeof(test_record_1),
			},
		},
		.want_content_type = SSL3_RT_HANDSHAKE,
		.want_data = test_record_1,
		.want_len = sizeof(test_record_1),
	},
	{
		.read_buf = test_record_1,
		.rt = {
			{
				.rw_len = 0,
				.want_ret = TLS13_IO_WANT_POLLIN,
			},
			{
				.rw_len = 2,
				.want_ret = TLS13_IO_WANT_POLLIN,
			},
			{
				.rw_len = 6,
				.want_ret = TLS13_IO_WANT_POLLIN,
			},
			{
				.rw_len = sizeof(test_record_1),
				.want_ret = sizeof(test_record_1),
			},
		},
		.want_content_type = SSL3_RT_HANDSHAKE,
		.want_data = test_record_1,
		.want_len = sizeof(test_record_1),
	},
	{
		.read_buf = test_record_1,
		.rt = {
			{
				.rw_len = 4,
				.want_ret = TLS13_IO_WANT_POLLIN,
			},
			{
				.eof = 1,
				.want_ret = TLS13_IO_EOF,
			},
		},
	},
	{
		.read_buf = test_record_1,
		.rt = {
			{
				.eof = 1,
				.want_ret = TLS13_IO_EOF,
			},
		},
	},
	{
		.read_buf = test_record_2,
		.rt = {
			{
				.rw_len = sizeof(test_record_2),
				.want_ret = TLS13_IO_WANT_POLLIN,
			},
			{
				.eof = 1,
				.want_ret = TLS13_IO_EOF,
			},
		},
		.want_content_type = SSL3_RT_APPLICATION_DATA,
	},
	{
		.read_buf = test_record_3,
		.rt = {
			{
				.rw_len = sizeof(test_record_3),
				.want_ret = TLS13_IO_RECORD_OVERFLOW,
			},
		},
	},
};

#define N_RECORD_RECV_TESTS (sizeof(record_recv_tests) / sizeof(record_recv_tests[0]))

struct record_send_test {
	uint8_t *data;
	size_t data_len;
	struct record_test rt[10];
	uint8_t *want_data;
	size_t want_len;
};

struct record_send_test record_send_tests[] = {
	{
		.data = test_record_1,
		.data_len = sizeof(test_record_1),
		.rt = {
			{
				.rw_len = sizeof(test_record_1),
				.want_ret = sizeof(test_record_1),
			},
		},
		.want_data = test_record_1,
		.want_len = sizeof(test_record_1),
	},
	{
		.data = test_record_1,
		.data_len = sizeof(test_record_1),
		.rt = {
			{
				.rw_len = 0,
				.want_ret = TLS13_IO_WANT_POLLOUT,
			},
			{
				.rw_len = sizeof(test_record_1),
				.want_ret = sizeof(test_record_1),
			},
		},
		.want_data = test_record_1,
		.want_len = sizeof(test_record_1),
	},
	{
		.data = test_record_1,
		.data_len = sizeof(test_record_1),
		.rt = {
			{
				.rw_len = 0,
				.want_ret = TLS13_IO_WANT_POLLOUT,
			},
			{
				.rw_len = 5,
				.want_ret = TLS13_IO_WANT_POLLOUT,
			},
			{
				.rw_len = sizeof(test_record_1),
				.want_ret = sizeof(test_record_1),
			},
		},
		.want_data = test_record_1,
		.want_len = sizeof(test_record_1),
	},
	{
		.data = test_record_1,
		.data_len = sizeof(test_record_1),
		.rt = {
			{
				.rw_len = 0,
				.want_ret = TLS13_IO_WANT_POLLOUT,
			},
			{
				.rw_len = 2,
				.want_ret = TLS13_IO_WANT_POLLOUT,
			},
			{
				.rw_len = 6,
				.want_ret = TLS13_IO_WANT_POLLOUT,
			},
			{
				.rw_len = sizeof(test_record_1),
				.want_ret = sizeof(test_record_1),
			},
		},
		.want_data = test_record_1,
		.want_len = sizeof(test_record_1),
	},
	{
		.data = test_record_1,
		.data_len = sizeof(test_record_1),
		.rt = {
			{
				.rw_len = 4,
				.want_ret = TLS13_IO_WANT_POLLOUT,
			},
			{
				.eof = 1,
				.want_ret = TLS13_IO_EOF,
			},
		},
		.want_data = test_record_1,
		.want_len = 4,
	},
	{
		.data = test_record_1,
		.data_len = sizeof(test_record_1),
		.rt = {
			{
				.rw_len = 0,
				.want_ret = TLS13_IO_WANT_POLLOUT,
			},
			{
				.eof = 1,
				.want_ret = TLS13_IO_EOF,
			},
		},
		.want_data = NULL,
		.want_len = 0,
	},
};

#define N_RECORD_SEND_TESTS (sizeof(record_send_tests) / sizeof(record_send_tests[0]))

static int
test_record_recv(size_t test_no, struct record_recv_test *rrt)
{
	struct tls13_record *rec;
	struct rw_state rs;
	int failed = 1;
	ssize_t ret;
	size_t i;
	CBS cbs;

	rs.buf = rrt->read_buf;
	rs.offset = 0;

	if ((rec = tls13_record_new()) == NULL)
		errx(1, "tls13_record_new");

	for (i = 0; rrt->rt[i].rw_len != 0 || rrt->rt[i].want_ret != 0; i++) {
		rs.eof = rrt->rt[i].eof;
		rs.len = rrt->rt[i].rw_len;

		ret = tls13_record_recv(rec, read_cb, &rs);
		if (ret != rrt->rt[i].want_ret) {
			fprintf(stderr, "FAIL: Test %zu/%zu - tls_record_recv "
			    "returned %zd, want %zd\n", test_no, i, ret,
			    rrt->rt[i].want_ret);
			goto failure;
		}
	}

	if (tls13_record_content_type(rec) != rrt->want_content_type) {
		fprintf(stderr, "FAIL: Test %zu - got content type %u, "
		    "want %u\n", test_no, tls13_record_content_type(rec),
		    rrt->want_content_type);
		goto failure;
	}

	tls13_record_data(rec, &cbs);
	if (rrt->want_data == NULL) {
		if (CBS_data(&cbs) != NULL || CBS_len(&cbs) != 0) {
			fprintf(stderr, "FAIL: Test %zu - got CBS with data, "
			    "want NULL\n", test_no);
			goto failure;
		}
		goto done;
	}
	if (!CBS_mem_equal(&cbs, rrt->want_data, rrt->want_len)) {
		fprintf(stderr, "FAIL: Test %zu - data mismatch\n", test_no);
		fprintf(stderr, "Got record data:\n");
		hexdump(CBS_data(&cbs), CBS_len(&cbs));
		fprintf(stderr, "Want record data:\n");
		hexdump(rrt->want_data, rrt->want_len);
		goto failure;
	}

	if (!tls13_record_header(rec, &cbs)) {
		fprintf(stderr, "FAIL: Test %zu - fail to get record "
		    "header", test_no);
		goto failure;
	}
	if (!CBS_mem_equal(&cbs, rrt->want_data, TLS13_RECORD_HEADER_LEN)) {
		fprintf(stderr, "FAIL: Test %zu - header mismatch\n", test_no);
		fprintf(stderr, "Got record header:\n");
		hexdump(CBS_data(&cbs), CBS_len(&cbs));
		fprintf(stderr, "Want record header:\n");
		hexdump(rrt->want_data, rrt->want_len);
		goto failure;
	}

	if (!tls13_record_content(rec, &cbs)) {
		fprintf(stderr, "FAIL: Test %zu - fail to get record "
		    "content", test_no);
		goto failure;
	}
	if (!CBS_mem_equal(&cbs, rrt->want_data + TLS13_RECORD_HEADER_LEN,
	    rrt->want_len - TLS13_RECORD_HEADER_LEN)) {
		fprintf(stderr, "FAIL: Test %zu - content mismatch\n", test_no);
		fprintf(stderr, "Got record content:\n");
		hexdump(CBS_data(&cbs), CBS_len(&cbs));
		fprintf(stderr, "Want record content:\n");
		hexdump(rrt->want_data, rrt->want_len);
		goto failure;
	}

 done:
	failed = 0;

 failure:
	tls13_record_free(rec);

	return failed;
}

static int
test_record_send(size_t test_no, struct record_send_test *rst)
{
	uint8_t *data = NULL;
	struct tls13_record *rec;
	struct rw_state ws;
	int failed = 1;
	ssize_t ret;
	size_t i;

	if ((ws.buf = malloc(TLS13_RECORD_MAX_LEN)) == NULL)
		errx(1, "malloc");

	ws.offset = 0;

	if ((rec = tls13_record_new()) == NULL)
		errx(1, "tls13_record_new");

	if ((data = malloc(rst->data_len)) == NULL)
		errx(1, "malloc");
	memcpy(data, rst->data, rst->data_len);

	if (!tls13_record_set_data(rec, data, rst->data_len)) {
		fprintf(stderr, "FAIL: Test %zu - failed to set record data\n",
		    test_no);
		goto failure;
	}
	data = NULL;

	for (i = 0; rst->rt[i].rw_len != 0 || rst->rt[i].want_ret != 0; i++) {
		ws.eof = rst->rt[i].eof;
		ws.len = rst->rt[i].rw_len;

		ret = tls13_record_send(rec, write_cb, &ws);
		if (ret != rst->rt[i].want_ret) {
			fprintf(stderr, "FAIL: Test %zu/%zu - tls_record_send "
			    "returned %zd, want %zd\n", test_no, i, ret,
			    rst->rt[i].want_ret);
			goto failure;
		}
	}

	if (rst->want_data != NULL &&
	    memcmp(ws.buf, rst->want_data, rst->want_len) != 0) {
		fprintf(stderr, "FAIL: Test %zu - content mismatch\n", test_no);
		fprintf(stderr, "Got record data:\n");
		hexdump(rst->data, rst->data_len);
		fprintf(stderr, "Want record data:\n");
		hexdump(rst->want_data, rst->want_len);
		goto failure;
	}

	failed = 0;

 failure:
	tls13_record_free(rec);
	free(ws.buf);

	return failed;
}

static int
test_recv_records(void)
{
	int failed = 0;
	size_t i;

	for (i = 0; i < N_RECORD_RECV_TESTS; i++)
		failed |= test_record_recv(i, &record_recv_tests[i]);

	return failed;
}

static int
test_send_records(void)
{
	int failed = 0;
	size_t i;

	for (i = 0; i < N_RECORD_SEND_TESTS; i++)
		failed |= test_record_send(i, &record_send_tests[i]);

	return failed;
}

int
main(int argc, char **argv)
{
	int failed = 0;

	failed |= test_recv_records();
	failed |= test_send_records();

	return failed;
}
