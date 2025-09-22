/*	$OpenBSD: login_cap.h,v 1.2 2021/06/03 13:19:45 deraadt Exp $	*/
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

#ifndef _LIBC_LOGIN_CAP_H_
#define _LIBC_LOGIN_CAP_H_

#include_next <login_cap.h>

PROTO_NORMAL(login_close);
PROTO_NORMAL(login_getcapbool);
PROTO_NORMAL(login_getcapnum);
PROTO_NORMAL(login_getcapsize);
PROTO_NORMAL(login_getcapstr);
PROTO_NORMAL(login_getcaptime);
PROTO_NORMAL(login_getclass);
PROTO_NORMAL(login_getstyle);
PROTO_DEPRECATED(setclasscontext);
PROTO_NORMAL(setusercontext);

#endif /* _LIBC_LOGIN_CAP_H_ */
