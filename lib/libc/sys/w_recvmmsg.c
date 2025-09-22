/*	$OpenBSD: w_recvmmsg.c,v 1.1 2022/09/09 13:52:59 mbuhl Exp $ */
/*
 * Copyright (c) 2022 Moritz Buhl <mbuhl@openbsd.org>
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

#include <sys/socket.h>
#include "cancel.h"

int
recvmmsg(int fd, struct mmsghdr *mmsg, unsigned int vlen, int flags,
    struct timespec *ts)
{
	int ret;

	ENTER_CANCEL_POINT(1);
	ret = HIDDEN(recvmmsg)(fd, mmsg, vlen, flags, ts);
	LEAVE_CANCEL_POINT(ret == -1);
	return (ret);
}
DEF_CANCEL(recvmmsg);
