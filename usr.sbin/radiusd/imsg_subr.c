/*	$OpenBSD: imsg_subr.c,v 1.3 2024/11/21 13:18:38 claudio Exp $	*/

/*
 * Copyright (c) 2015 YASUOKA Masahiko <yasuoka@yasuoka.net>
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
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/uio.h>

#include <imsg.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <poll.h>
#include <errno.h>

#include "imsg_subr.h"

/*
 * Check readability not to spin before calling imsgbuf_read(3).
 * Wait 'millisec' until it becomes readable.
 */
int
imsg_sync_read(struct imsgbuf *ibuf, int millisec)
{
	struct pollfd	 fds[1];
	int		 retval;

	fds[0].fd = ibuf->fd;
	fds[0].events = POLLIN;
	retval = poll(fds, 1, millisec);
	if (retval == 0) {
		errno = EAGAIN;
		return (-1);
	}
	if (retval > 0 && (fds[0].revents & POLLIN) != 0)
		return imsgbuf_read(ibuf);

	return (-1);
}

/*
 * Check writability not to spin before calling imsgbuf_flush(3).
 * Wait 'millisec' until it becomes writable.
 */
int
imsg_sync_flush(struct imsgbuf *ibuf, int millisec)
{
	struct pollfd	 fds[1];
	int		 retval;

	if (imsgbuf_queuelen(ibuf) == 0)
		return (0);	/* already flushed */

	fds[0].fd = ibuf->fd;
	fds[0].events = POLLOUT;
	retval = poll(fds, 1, millisec);
	if (retval == 0) {
		errno = EAGAIN;
		return (-1);
	}
	if (retval > 0 && (fds[0].revents & POLLOUT) != 0)
		return imsgbuf_flush(ibuf);

	return (-1);
}
