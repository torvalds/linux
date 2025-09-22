/*	$OpenBSD: w_fcntl.c,v 1.2 2025/08/04 04:59:31 guenther Exp $ */
/*
 * Copyright (c) 2015 Philip Guenther <guenther@openbsd.org>
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

#include <fcntl.h>
#include <stdarg.h>
#include "cancel.h"

int
fcntl(int fd, int cmd, ...)
{
	va_list ap;
	int ret;

	va_start(ap, cmd);
	switch (cmd) {
	case F_DUPFD:
	case F_DUPFD_CLOEXEC:
	case F_DUPFD_CLOFORK:
	case F_SETFD:
	case F_SETFL:
	case F_SETOWN:
		ret = HIDDEN(fcntl)(fd, cmd, va_arg(ap, int));
		break;
	case F_GETFD:
	case F_GETFL:
	case F_GETOWN:
	case F_ISATTY:
		ret = HIDDEN(fcntl)(fd, cmd);
		break;
	case F_GETLK:
	case F_SETLK:
		ret = HIDDEN(fcntl)(fd, cmd, va_arg(ap, struct flock *));
		break;
	case F_SETLKW:
		ENTER_CANCEL_POINT(1);
		ret = HIDDEN(fcntl)(fd, cmd, va_arg(ap, struct flock *));
		LEAVE_CANCEL_POINT(ret == -1);
		break;
	default:	/* should never happen? */
		ret = HIDDEN(fcntl)(fd, cmd, va_arg(ap, void *));
		break;
	}
	va_end(ap);
	return (ret);
}
DEF_CANCEL(fcntl);
