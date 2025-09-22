/*	$OpenBSD: stat.h,v 1.3 2023/05/18 16:11:09 guenther Exp $	*/
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

#ifndef _LIBC_SYS_STAT_H_
#define	_LIBC_SYS_STAT_H_

#include_next <sys/stat.h>

PROTO_NORMAL(chflags);
PROTO_NORMAL(chflagsat);
PROTO_NORMAL(chmod);
PROTO_NORMAL(fchflags);
PROTO_NORMAL(fchmod);
PROTO_NORMAL(fchmodat);
PROTO_NORMAL(fstat);
PROTO_NORMAL(fstatat);
PROTO_NORMAL(futimens);
PROTO_DEPRECATED(isfdtype);
PROTO_NORMAL(lstat);
PROTO_NORMAL(mkdir);
PROTO_NORMAL(mkdirat);
PROTO_NORMAL(mkfifo);
PROTO_NORMAL(mkfifoat);
PROTO_NORMAL(mknod);
PROTO_NORMAL(mknodat);
PROTO_NORMAL(stat);
PROTO_NORMAL(umask);
PROTO_NORMAL(utimensat);

#endif /* !_LIBC_SYS_STAT_H_ */
