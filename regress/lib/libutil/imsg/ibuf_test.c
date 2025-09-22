/* $OpenBSD: ibuf_test.c,v 1.5 2023/12/29 16:02:29 claudio Exp $ */
/*
 * Copyright (c) Tobias Stoeckmann <tobias@openbsd.org>
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

#include <sys/queue.h>
#include <sys/types.h>

#include <imsg.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

int
test_ibuf_open(void)
{
	struct ibuf *buf;

	if ((buf = ibuf_open(1)) == NULL)
		return 1;

	ibuf_free(buf);
	return 0;
}

int
test_ibuf_dynamic(void)
{
	struct ibuf *buf;

	if (ibuf_dynamic(100, 0) != NULL)
		return 1;

	if ((buf = ibuf_dynamic(10, SIZE_MAX)) == NULL)
		return 1;

	ibuf_free(buf);
	return 0;
}

int
test_ibuf_reserve(void)
{
	struct ibuf *buf;
	int ret;

	if ((buf = ibuf_dynamic(10, SIZE_MAX)) == NULL) {
		return 1;
	}

	if (ibuf_reserve(buf, SIZE_MAX) != NULL) {
		ibuf_free(buf);
		return 1;
	}

	if (ibuf_reserve(buf, 10) == NULL) {
		ibuf_free(buf);
		return 1;
	}

	ret = (ibuf_reserve(buf, SIZE_MAX) != NULL);

	ibuf_free(buf);
	return ret;
}

int
test_ibuf_seek(void)
{
	struct ibuf *buf;
	int ret;

	if ((buf = ibuf_open(10)) == NULL)
		return 1;

	ret = (ibuf_seek(buf, 1, SIZE_MAX) != NULL);

	ibuf_free(buf);
	return ret;
}

int
main(void)
{
	extern char *__progname;

	int ret = 0;

	if (test_ibuf_open() != 0) {
		printf("FAILED: test_ibuf_open\n");
		ret = 1;
	} else
		printf("SUCCESS: test_ibuf_open\n");

	if (test_ibuf_dynamic() != 0) {
		printf("FAILED: test_ibuf_dynamic\n");
		ret = 1;
	} else
		printf("SUCCESS: test_ibuf_dynamic\n");

	if (test_ibuf_reserve() != 0) {
		printf("FAILED: test_ibuf_reserve\n");
		ret = 1;
	} else
		printf("SUCCESS: test_ibuf_reserve\n");

	if (test_ibuf_seek() != 0) {
		printf("FAILED: test_ibuf_seek\n");
		ret = 1;
	} else
		printf("SUCCESS: test_ibuf_seek\n");

	if (ret != 0) {
		printf("FAILED: %s\n", __progname);
		return 1;
	}

	return 0;
}
