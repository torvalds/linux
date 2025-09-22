/*	$OpenBSD: grp.h,v 1.3 2018/09/13 12:31:15 millert Exp $	*/
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

#ifndef _LIBC_GRP_H_
#define	_LIBC_GRP_H_

#include_next <grp.h>

__BEGIN_HIDDEN_DECLS
struct group *_getgrent_yp(int *);
__END_HIDDEN_DECLS

PROTO_NORMAL(endgrent);
PROTO_DEPRECATED(getgrent);
PROTO_DEPRECATED(getgrgid);
PROTO_NORMAL(getgrgid_r);
PROTO_DEPRECATED(getgrnam);
PROTO_NORMAL(getgrnam_r);
PROTO_DEPRECATED(gid_from_group);
PROTO_DEPRECATED(group_from_gid);
PROTO_NORMAL(setgrent);
PROTO_NORMAL(setgroupent);

#endif /* !_LIBC_GRP_H_ */
