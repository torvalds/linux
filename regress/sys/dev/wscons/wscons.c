/*	$OpenBSD: wscons.c,v 1.1 2018/12/17 19:29:55 anton Exp $	*/

/*
 * Copyright (c) 2018 Anton Lindqvist <anton@openbsd.org>
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

#include <sys/ioctl.h>

#include <err.h>
#include <errno.h>
#include <stdlib.h>

#include "util.h"

static int test_ioctl_unknown(int);

/*
 * Test that inappropriate ioctl commands are rejected.
 */
static int
test_ioctl_unknown(int fd)
{
	if (ioctl(fd, TIOCSCTTY) == -1) {
		if (errno != ENOTTY)
			err(1, "ioctl: TIOCSCTTY");
	} else {
		errx(1, "ioctl: TIOCSCTTY: not rejected");
	}
	return 0;
}

int
main(int argc, char *argv[])
{
	struct test tests[] = {
		{ "ioctl-unknown",	test_ioctl_unknown },
		{ NULL,			NULL },
	};

	return dotest(argc, argv, tests);
}
