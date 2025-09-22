/*	$OpenBSD: opentap.c,v 1.1 2016/09/28 12:40:35 bluhm Exp $ */

/*
 * Copyright (c) 2014 Alexander Bluhm <bluhm@openbsd.org>
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

#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/socket.h>

void usage(void);

void
usage(void)
{
	fprintf(stderr, "usage: sudo %s fd# tap#\n", getprogname());
	fprintf(stderr, "  fd#   number of file descriptor for fd passing\n");
	fprintf(stderr, "  tap#  number of tap device to open\n");
	exit(2);
}

int
main(int argc, char *argv[])
{
	int		 fd, tap;
	char		 dev[FILENAME_MAX];
	const char	*errstr;
	struct msghdr	 msg;
	struct cmsghdr	*cmsg;
	union {
		struct cmsghdr	 hdr;
		unsigned char	 buf[CMSG_SPACE(sizeof(int))];
	} cmsgbuf;

	if (argc != 3)
		usage();

	fd = strtonum(argv[1], 0, INT_MAX, &errstr);
	if (errstr)
		errx(2, "file descriptor number %s: %s", errstr, argv[1]);
	tap = strtonum(argv[2], 0, INT_MAX, &errstr);
	if (errstr)
		errx(2, "tap device number %s: %s", errstr, argv[2]);
	snprintf(dev, FILENAME_MAX, "/dev/tap%d", tap);

	if ((tap = open(dev, O_RDWR)) == -1)
		err(1, "open %s", dev);

	memset(&msg, 0, sizeof(msg));
	msg.msg_control = &cmsgbuf.buf;
	msg.msg_controllen = sizeof(cmsgbuf.buf);

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_len = CMSG_LEN(sizeof(int));
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	*(int *)CMSG_DATA(cmsg) = tap;

	if (sendmsg(fd, &msg, 0) == -1)
		err(1, "sendmsg %d", fd);

	return 0;
}
