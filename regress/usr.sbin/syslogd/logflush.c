/*	$OpenBSD: logflush.c,v 1.2 2021/10/24 21:24:21 deraadt Exp $	*/

/*
 * Copyright (c) 2021 Alexander Bluhm <bluhm@openbsd.org>
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
#include <sys/socket.h>

#include <err.h>
#include <fcntl.h>
#include <paths.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <unistd.h>

__dead void
usage()
{
	fprintf(stderr, "usage: %s\n", getprogname());
	exit(2);
}

int
main(int argc, char *argv[])
{
	int pair[2], klog, val;

	if (argc != 1)
		usage();

	if (socketpair(AF_UNIX, SOCK_DGRAM, PF_UNSPEC, pair) == -1)
		err(1, "socketpair");

	val = 1<<20;
	if (setsockopt(pair[0], SOL_SOCKET, SO_RCVBUF, &val, sizeof(val)) == -1)
		err(1, "setsockopt SO_RCVBUF");
	if (setsockopt(pair[1], SOL_SOCKET, SO_SNDBUF, &val, sizeof(val)) == -1)
		err(1, "setsockopt SO_SNDBUF");

	if ((klog = open(_PATH_KLOG, O_RDONLY)) == -1)
		err(1, "open %s", _PATH_KLOG);
	/* Use /dev/klog to register sendsyslog(2) receiver. */
	if (ioctl(klog, LIOCSFD, &pair[1]) == -1)
		err(1, "ioctl klog LIOCSFD sendsyslog");
	close(pair[1]);

	/* Send message via libc, flushes log stash in kernel. */
	openlog("syslogd-regress", LOG_PID, LOG_SYSLOG);
	syslog(LOG_DEBUG, "logflush");

	return 0;
}
