/*	$OpenBSD: msg.h,v 1.1 2016/05/07 19:05:22 guenther Exp $	*/
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

#ifndef _LIBC_SYS_MSG_H_
#define _LIBC_SYS_MSG_H_

#include_next <sys/msg.h>

PROTO_NORMAL(msgctl);
PROTO_NORMAL(msgget);
PROTO_CANCEL(msgrcv);
PROTO_CANCEL(msgsnd);

#endif /* !_LIBC_SYS_MSG_H_ */
