/*	$OpenBSD: bio_mem.c,v 1.1 2022/12/08 17:49:02 tb Exp $	*/
/*
 * Copyright (c) 2022 Joel Sing <jsing@openbsd.org>
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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/bio.h>
#include <openssl/buffer.h>

static int
bio_mem_test(void)
{
	uint8_t *data = NULL;
	size_t data_len;
	uint8_t *rodata;
	long rodata_len;
	BUF_MEM *pbuf;
	BUF_MEM *buf = NULL;
	BIO *bio = NULL;
	int ret;
	int failed = 1;

	data_len = 4096;
	if ((data = malloc(data_len)) == NULL)
		err(1, "malloc");

	memset(data, 0xdb, data_len);
	data[0] = 0x01;
	data[data_len - 1] = 0xff;

	if ((bio = BIO_new(BIO_s_mem())) == NULL) {
		fprintf(stderr, "FAIL: BIO_new() returned NULL\n");
		goto failure;
	}
	if ((ret = BIO_write(bio, data, data_len)) != (int)data_len) {
		fprintf(stderr, "FAIL: BIO_write() = %d, want %zu\n", ret,
		    data_len);
		goto failure;
	}
	if ((rodata_len = BIO_get_mem_data(bio, &rodata)) != (long)data_len) {
		fprintf(stderr, "FAIL: BIO_get_mem_data() = %ld, want %zu\n",
		    rodata_len, data_len);
		goto failure;
	}
	if (rodata[0] != 0x01) {
		fprintf(stderr, "FAIL: got 0x%x, want 0x%x\n", rodata[0], 0x01);
		goto failure;
	}
	if (rodata[rodata_len - 1] != 0xff) {
		fprintf(stderr, "FAIL: got 0x%x, want 0x%x\n",
		    rodata[rodata_len - 1], 0xff);
		goto failure;
	}

	if (!BIO_get_mem_ptr(bio, &pbuf)) {
		fprintf(stderr, "FAIL: BIO_get_mem_ptr() failed\n");
		goto failure;
	}
	if (pbuf->length != data_len) {
		fprintf(stderr, "FAIL: Got buffer with length %zu, want %zu\n",
		    pbuf->length, data_len);
		goto failure;
	}
	if (memcmp(pbuf->data, data, data_len) != 0) {
		fprintf(stderr, "FAIL: Got buffer with differing data\n");
		goto failure;
	}
	pbuf = NULL;

	if ((buf = BUF_MEM_new()) == NULL) {
		fprintf(stderr, "FAIL: BUF_MEM_new() returned NULL\n");
		goto failure;
	}
	if (!BIO_set_mem_buf(bio, buf, BIO_NOCLOSE)) {
		fprintf(stderr, "FAIL: BUF_set_mem_buf() failed\n");
		goto failure;
	}
	if ((ret = BIO_puts(bio, "Hello\n")) != 6) {
		fprintf(stderr, "FAIL: BUF_puts() = %d, want %d\n", ret, 6);
		goto failure;
	}
	if ((ret = BIO_puts(bio, "World\n")) != 6) {
		fprintf(stderr, "FAIL: BUF_puts() = %d, want %d\n", ret, 6);
		goto failure;
	}
	if (buf->length != 12) {
		fprintf(stderr, "FAIL: buffer has length %zu, want %d\n",
		    buf->length, 12);
		goto failure;
	}
	buf->length = 11;
	if ((ret = BIO_gets(bio, data, data_len)) != 6) {
		fprintf(stderr, "FAIL: BUF_gets() = %d, want %d\n", ret, 6);
		goto failure;
	}
	if (strcmp(data, "Hello\n") != 0) {
		fprintf(stderr, "FAIL: BUF_gets() returned '%s', want '%s'\n",
		    data, "Hello\\n");
		goto failure;
	}
	if ((ret = BIO_gets(bio, data, data_len)) != 5) {
		fprintf(stderr, "FAIL: BUF_gets() = %d, want %d\n", ret, 5);
		goto failure;
	}
	if (strcmp(data, "World") != 0) {
		fprintf(stderr, "FAIL: BUF_gets() returned '%s', want '%s'\n",
		    data, "World");
		goto failure;
	}

	if (!BIO_eof(bio)) {
		fprintf(stderr, "FAIL: BIO is not EOF\n");
		goto failure;
	}
	if ((ret = BIO_read(bio, data, data_len)) != -1) {
		fprintf(stderr, "FAIL: BIO_read() = %d, want -1\n", ret);
		goto failure;
	}
	if (!BIO_set_mem_eof_return(bio, -2)) {
		fprintf(stderr, "FAIL: BIO_set_mem_eof_return() failed\n");
		goto failure;
	}
	if ((ret = BIO_read(bio, data, data_len)) != -2) {
		fprintf(stderr, "FAIL: BIO_read() = %d, want -2\n", ret);
		goto failure;
	}

	failed = 0;

 failure:
	free(data);
	BUF_MEM_free(buf);
	BIO_free(bio);

	return failed;
}

static int
bio_mem_small_io_test(void)
{
	uint8_t buf[2];
	int i, j, ret;
	BIO *bio;
	int failed = 1;

	memset(buf, 0xdb, sizeof(buf));

	if ((bio = BIO_new(BIO_s_mem())) == NULL) {
		fprintf(stderr, "FAIL: BIO_new() returned NULL\n");
		goto failure;
	}

	for (i = 0; i < 100; i++) {
		if (!BIO_reset(bio)) {
			fprintf(stderr, "FAIL: BIO_reset() failed\n");
			goto failure;
		}
		for (j = 0; j < 25000; j++) {
			ret = BIO_write(bio, buf, sizeof(buf));
			if (ret != sizeof(buf)) {
				fprintf(stderr, "FAIL: BIO_write() = %d, "
				    "want %zu\n", ret, sizeof(buf));
				goto failure;
			}
		}
		for (j = 0; j < 25000; j++) {
			ret = BIO_read(bio, buf, sizeof(buf));
			if (ret != sizeof(buf)) {
				fprintf(stderr, "FAIL: BIO_read() = %d, "
				    "want %zu\n", ret, sizeof(buf));
				goto failure;
			}
			ret = BIO_write(bio, buf, sizeof(buf));
			if (ret != sizeof(buf)) {
				fprintf(stderr, "FAIL: BIO_write() = %d, "
				    "want %zu\n", ret, sizeof(buf));
				goto failure;
			}
		}
		for (j = 0; j < 25000; j++) {
			ret = BIO_read(bio, buf, sizeof(buf));
			if (ret != sizeof(buf)) {
				fprintf(stderr, "FAIL: BIO_read() = %d, "
				    "want %zu\n", ret, sizeof(buf));
				goto failure;
			}
		}
		if (!BIO_eof(bio)) {
			fprintf(stderr, "FAIL: BIO not EOF\n");
			goto failure;
		}
	}

	if (buf[0] != 0xdb || buf[1] != 0xdb) {
		fprintf(stderr, "FAIL: buf = {0x%x, 0x%x}, want {0xdb, 0xdb}\n",
		    buf[0], buf[1]);
		goto failure;
	}

	failed = 0;

 failure:
	BIO_free(bio);

	return failed;
}

static int
bio_mem_readonly_test(void)
{
	uint8_t *data = NULL;
	size_t data_len;
	uint8_t buf[2048];
	BIO *bio = NULL;
	int ret;
	int failed = 1;

	data_len = 4096;
	if ((data = malloc(data_len)) == NULL)
		err(1, "malloc");

	memset(data, 0xdb, data_len);
	data[0] = 0x01;
	data[data_len - 1] = 0xff;

	if ((bio = BIO_new_mem_buf(data, data_len)) == NULL) {
		fprintf(stderr, "FAIL: BIO_new_mem_buf failed\n");
		goto failure;
	}
	if ((ret = BIO_read(bio, buf, 1)) != 1) {
		fprintf(stderr, "FAIL: BIO_read() = %d, want %zu\n", ret,
		    sizeof(buf));
		goto failure;
	}
	if (buf[0] != 0x01) {
		fprintf(stderr, "FAIL: got 0x%x, want 0x%x\n", buf[0], 0x01);
		goto failure;
	}
	if ((ret = BIO_read(bio, buf, sizeof(buf))) != sizeof(buf)) {
		fprintf(stderr, "FAIL: BIO_read() = %d, want %zu\n", ret,
		    sizeof(buf));
		goto failure;
	}
	if (buf[0] != 0xdb) {
		fprintf(stderr, "FAIL: got 0x%x, want 0x%x\n", buf[0], 0xdb);
		goto failure;
	}
	if ((ret = BIO_write(bio, buf, 1)) != -1) {
		fprintf(stderr, "FAIL: BIO_write() = %d, want -1\n", ret);
		goto failure;
	}
	if (BIO_eof(bio)) {
		fprintf(stderr, "FAIL: BIO is EOF\n");
		goto failure;
	}
	if (BIO_ctrl_pending(bio) != 2047) {
		fprintf(stderr, "FAIL: BIO_ctrl_pending() = %zu, want 2047\n",
		    BIO_ctrl_pending(bio));
		goto failure;
	}
	if ((ret = BIO_read(bio, buf, sizeof(buf))) != 2047) {
		fprintf(stderr, "FAIL: BIO_read() = %d, want 2047\n", ret);
		goto failure;
	}
	if (buf[2045] != 0xdb) {
		fprintf(stderr, "FAIL: got 0x%x, want 0x%x\n", buf[2045], 0xdb);
		goto failure;
	}
	if (buf[2046] != 0xff) {
		fprintf(stderr, "FAIL: got 0x%x, want 0x%x\n", buf[2046], 0xff);
		goto failure;
	}
	if (!BIO_eof(bio)) {
		fprintf(stderr, "FAIL: BIO is not EOF\n");
		goto failure;
	}
	if (BIO_ctrl_pending(bio) != 0) {
		fprintf(stderr, "FAIL: BIO_ctrl_pending() = %zu, want 0\n",
		    BIO_ctrl_pending(bio));
		goto failure;
	}

	if (!BIO_reset(bio)) {
		fprintf(stderr, "FAIL: failed to reset bio\n");
		goto failure;
	}
	if (BIO_eof(bio)) {
		fprintf(stderr, "FAIL: BIO is EOF\n");
		goto failure;
	}
	if (BIO_ctrl_pending(bio) != 4096) {
		fprintf(stderr, "FAIL: BIO_ctrl_pending() = %zu, want 4096\n",
		    BIO_ctrl_pending(bio));
		goto failure;
	}
	if ((ret = BIO_read(bio, buf, 2)) != 2) {
		fprintf(stderr, "FAIL: BIO_read() = %d, want 2\n", ret);
		goto failure;
	}
	if (buf[0] != 0x01) {
		fprintf(stderr, "FAIL: got 0x%x, want 0x%x\n", buf[0], 0x01);
		goto failure;
	}
	if (buf[1] != 0xdb) {
		fprintf(stderr, "FAIL: got 0x%x, want 0x%x\n", buf[1], 0xdb);
		goto failure;
	}

	failed = 0;

 failure:
	BIO_free(bio);
	free(data);

	return failed;
}

int
main(int argc, char **argv)
{
	int failed = 0;

	failed |= bio_mem_test();
	failed |= bio_mem_small_io_test();
	failed |= bio_mem_readonly_test();

	return failed;
}
