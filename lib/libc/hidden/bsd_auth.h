/*	$OpenBSD: bsd_auth.h,v 1.2 2019/12/04 09:50:47 deraadt Exp $	*/
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

#ifndef _LIBC_BSD_AUTH_H_
#define _LIBC_BSD_AUTH_H_

#include_next <bsd_auth.h>

__BEGIN_HIDDEN_DECLS
int _auth_validuser(const char *name);
__END_HIDDEN_DECLS

PROTO_NORMAL(auth_approval);
PROTO_NORMAL(auth_call);
PROTO_NORMAL(auth_cat);
PROTO_NORMAL(auth_challenge);
PROTO_NORMAL(auth_check_change);
PROTO_NORMAL(auth_check_expire);
PROTO_NORMAL(auth_checknologin);
PROTO_NORMAL(auth_clean);
PROTO_NORMAL(auth_close);
PROTO_NORMAL(auth_clrenv);
PROTO_NORMAL(auth_clroption);
PROTO_NORMAL(auth_clroptions);
PROTO_NORMAL(auth_getitem);
PROTO_NORMAL(auth_getpwd);
PROTO_NORMAL(auth_getstate);
PROTO_NORMAL(auth_getvalue);
PROTO_NORMAL(auth_mkvalue);
PROTO_NORMAL(auth_open);
PROTO_NORMAL(auth_set_va_list);
PROTO_NORMAL(auth_setdata);
PROTO_NORMAL(auth_setenv);
PROTO_NORMAL(auth_setitem);
PROTO_NORMAL(auth_setoption);
PROTO_NORMAL(auth_setpwd);
PROTO_NORMAL(auth_setstate);
PROTO_NORMAL(auth_userchallenge);
PROTO_NORMAL(auth_usercheck);
PROTO_NORMAL(auth_userokay);
PROTO_NORMAL(auth_userresponse);
PROTO_NORMAL(auth_verify);

#endif /* _LIBC_BSD_AUTH_H_ */
