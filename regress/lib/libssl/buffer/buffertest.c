/* $OpenBSD: buffertest.c,v 1.6 2022/07/22 19:34:55 jsing Exp $ */
/*
 * Copyright (c) 2019, 2022 Joel Sing <jsing@openbsd.org>
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tls_internal.h"

uint8_t testdata[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
};

struct read_state {
	uint8_t *buf;
	size_t len;
	size_t offset;
};

static ssize_t
read_cb(void *buf, size_t buflen, void *cb_arg)
{
	struct read_state *rs = cb_arg;
	ssize_t n;

	if (rs->offset > rs->len)
		return TLS_IO_EOF;

	if ((size_t)(n = buflen) > (rs->len - rs->offset))
		n = rs->len - rs->offset;

	if (n == 0)
		return TLS_IO_WANT_POLLIN;

	memcpy(buf, &rs->buf[rs->offset], n);
	rs->offset += n;

	return n;
}

struct extend_test {
	size_t extend_len;
	size_t read_len;
	ssize_t want_ret;
};

const struct extend_test extend_tests[] = {
	{
		.extend_len = 4,
		.read_len = 0,
		.want_ret = TLS_IO_WANT_POLLIN,
	},
	{
		.extend_len = 4,
		.read_len = 8,
		.want_ret = 4,
	},
	{
		.extend_len = 12,
		.read_len = 8,
		.want_ret = TLS_IO_WANT_POLLIN,
	},
	{
		.extend_len = 12,
		.read_len = 10,
		.want_ret = TLS_IO_WANT_POLLIN,
	},
	{
		.extend_len = 12,
		.read_len = 12,
		.want_ret = 12,
	},
	{
		.extend_len = 16,
		.read_len = 16,
		.want_ret = 16,
	},
	{
		.extend_len = 20,
		.read_len = 1,
		.want_ret = TLS_IO_EOF,
	},
};

#define N_EXTEND_TESTS (sizeof(extend_tests) / sizeof(extend_tests[0]))

static int
tls_buffer_extend_test(void)
{
	const struct extend_test *et;
	struct tls_buffer *buf;
	struct read_state rs;
	uint8_t *data = NULL;
	size_t i, data_len;
	ssize_t ret;
	CBS cbs;
	int failed = 1;

	rs.buf = testdata;
	rs.offset = 0;

	if ((buf = tls_buffer_new(0)) == NULL)
		errx(1, "tls_buffer_new");

	for (i = 0; i < N_EXTEND_TESTS; i++) {
		et = &extend_tests[i];
		rs.len = et->read_len;

		ret = tls_buffer_extend(buf, et->extend_len, read_cb, &rs);
		if (ret != extend_tests[i].want_ret) {
			fprintf(stderr, "FAIL: Test %zd - extend returned %zd, "
			    "want %zd\n", i, ret, et->want_ret);
			goto failed;
		}

		if (!tls_buffer_data(buf, &cbs)) {
			fprintf(stderr, "FAIL: Test %zd - failed to get data\n",
			    i);
			goto failed;
		}

		if (!CBS_mem_equal(&cbs, testdata, CBS_len(&cbs))) {
			fprintf(stderr, "FAIL: Test %zd - extend buffer "
			    "mismatch", i);
			goto failed;
		}
	}

	if (!tls_buffer_finish(buf, &data, &data_len)) {
		fprintf(stderr, "FAIL: failed to finish\n");
		goto failed;
	}

	tls_buffer_free(buf);
	buf = NULL;

	if (data_len != sizeof(testdata)) {
		fprintf(stderr, "FAIL: got data length %zu, want %zu\n",
		    data_len, sizeof(testdata));
		goto failed;
	}
	if (memcmp(data, testdata, data_len) != 0) {
		fprintf(stderr, "FAIL: data mismatch\n");
		goto failed;
	}

	failed = 0;

 failed:
	tls_buffer_free(buf);
	free(data);

	return failed;
}

struct read_write_test {
	uint8_t pattern;
	size_t read;
	size_t write;
	size_t append;
	ssize_t want;
};

const struct read_write_test read_write_tests[] = {
	{
		.read = 2048,
		.want = TLS_IO_WANT_POLLIN,
	},
	{
		.pattern = 0xdb,
		.write = 2048,
		.want = 2048,
	},
	{
		.pattern = 0xbd,
		.append = 2048,
		.want = 1,
	},
	{
		.pattern = 0xdb,
		.read = 2048,
		.want = 2048,
	},
	{
		.pattern = 0xfe,
		.append = 1024,
		.want = 1,
	},
	{
		.pattern = 0xbd,
		.read = 1000,
		.want = 1000,
	},
	{
		.pattern = 0xbd,
		.read = 1048,
		.want = 1048,
	},
	{
		.pattern = 0xdb,
		.write = 2048,
		.want = 2048,
	},
	{
		.pattern = 0xbd,
		.append = 1024,
		.want = 1,
	},
	{
		.pattern = 0xee,
		.append = 4096,
		.want = 1,
	},
	{
		.pattern = 0xfe,
		.append = 1,
		.want = 0,
	},
	{
		.pattern = 0xfe,
		.write = 1,
		.want = TLS_IO_FAILURE,
	},
	{
		.pattern = 0xfe,
		.read = 1024,
		.want = 1024,
	},
	{
		.pattern = 0xdb,
		.read = 2048,
		.want = 2048,
	},
	{
		.pattern = 0xbd,
		.read = 1024,
		.want = 1024,
	},
	{
		.pattern = 0xee,
		.read = 1024,
		.want = 1024,
	},
	{
		.pattern = 0xee,
		.read = 4096,
		.want = 3072,
	},
	{
		.read = 2048,
		.want = TLS_IO_WANT_POLLIN,
	},
};

#define N_READ_WRITE_TESTS (sizeof(read_write_tests) / sizeof(read_write_tests[0]))

static int
tls_buffer_read_write_test(void)
{
	const struct read_write_test *rwt;
	struct tls_buffer *buf = NULL;
	uint8_t *rbuf = NULL, *wbuf = NULL;
	ssize_t n;
	size_t i;
	int ret;
	int failed = 1;

	if ((buf = tls_buffer_new(0)) == NULL)
		errx(1, "tls_buffer_new");

	tls_buffer_set_capacity_limit(buf, 8192);

	for (i = 0; i < N_READ_WRITE_TESTS; i++) {
		rwt = &read_write_tests[i];

		if (rwt->append > 0) {
			free(wbuf);
			if ((wbuf = malloc(rwt->append)) == NULL)
				errx(1, "malloc");
			memset(wbuf, rwt->pattern, rwt->append);
			if ((ret = tls_buffer_append(buf, wbuf, rwt->append)) !=
			    rwt->want) {
				fprintf(stderr, "FAIL: test %zu - "
				    "tls_buffer_append() = %d, want %zu\n",
				    i, ret, rwt->want);
				goto failed;
			}
		}

		if (rwt->write > 0) {
			free(wbuf);
			if ((wbuf = malloc(rwt->write)) == NULL)
				errx(1, "malloc");
			memset(wbuf, rwt->pattern, rwt->write);
			if ((n = tls_buffer_write(buf, wbuf, rwt->write)) !=
			    rwt->want) {
				fprintf(stderr, "FAIL: test %zu - "
				    "tls_buffer_write() = %zi, want %zu\n",
				    i, n, rwt->want);
				goto failed;
			}
		}

		if (rwt->read > 0) {
			free(rbuf);
			if ((rbuf = calloc(1, rwt->read)) == NULL)
				errx(1, "malloc");
			if ((n = tls_buffer_read(buf, rbuf, rwt->read)) !=
			    rwt->want) {
				fprintf(stderr, "FAIL: test %zu - "
				    "tls_buffer_read() = %zi, want %zu\n",
				    i, n, rwt->want);
				goto failed;
			}
			if (rwt->want > 0) {
				free(wbuf);
				if ((wbuf = malloc(rwt->want)) == NULL)
					errx(1, "malloc");
				memset(wbuf, rwt->pattern, rwt->want);
				if (memcmp(rbuf, wbuf, rwt->want) != 0) {
					fprintf(stderr, "FAIL: test %zu - "
					    "read byte mismatch\n", i);
					goto failed;
				}
			}
		}
	}

	failed = 0;

 failed:
	tls_buffer_free(buf);
	free(rbuf);
	free(wbuf);

	return failed;
}

int
main(int argc, char **argv)
{
	int failed = 0;

	failed |= tls_buffer_extend_test();
	failed |= tls_buffer_read_write_test();

	return failed;
}
