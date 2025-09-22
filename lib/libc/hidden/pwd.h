/*	$OpenBSD: pwd.h,v 1.4 2018/09/13 12:31:15 millert Exp $	*/
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

#ifndef _LIBC_PWD_H_
#define	_LIBC_PWD_H_

#include_next <pwd.h>

__BEGIN_HIDDEN_DECLS
int	_bcrypt_autorounds(void);
__END_HIDDEN_DECLS


PROTO_NORMAL(bcrypt);
PROTO_NORMAL(bcrypt_checkpass);
PROTO_DEPRECATED(bcrypt_gensalt);
PROTO_NORMAL(bcrypt_newhash);
PROTO_DEPRECATED(endpwent);
PROTO_DEPRECATED(getpwent);
PROTO_DEPRECATED(getpwnam);
PROTO_NORMAL(getpwnam_r);
PROTO_NORMAL(getpwnam_shadow);
PROTO_DEPRECATED(getpwuid);
PROTO_NORMAL(getpwuid_r);
PROTO_NORMAL(getpwuid_shadow);
PROTO_NORMAL(pw_dup);
PROTO_NORMAL(setpassent);
PROTO_DEPRECATED(setpwent);
PROTO_DEPRECATED(uid_from_user);
PROTO_DEPRECATED(user_from_uid);

#endif /* !_LIBC_PWD_H_ */
