/*	$OpenBSD: main.c,v 1.1.1.1 2018/04/10 23:00:53 bluhm Exp $	*/
/*
 * Copyright (c) 2018 Alexander Bluhm <bluhm@openbsd.org>
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
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "header.h"

int
main(int argc, char *argv[])
{
	char *cmd;
	pid_t self;
	int fdpre, fdpost, ret;

	if ((fdpre = open("/dev/null", O_RDONLY)) == -1)
		err(1, "open pre");
	if (pledge("exec stdio proc recvfd rpath sendfd", NULL) == -1)
		err(1, "pledge");
	if ((fdpost = open("/dev/null", O_RDONLY)) == -1)
		err(1, "open post");

	fdops(fdpre, fdpost);

	self = getpid();
	if (asprintf(&cmd, "/usr/bin/fstat -p %d", self) == -1)
		err(1, "asprintf");
	ret = system(cmd);
	switch (ret) {
	case -1:
		err(1, "system");
	case 0:
		break;
	default:
		errx(1, "'%s' failed: %d", cmd, ret);
	}
	free(cmd);

	return 0;
}
