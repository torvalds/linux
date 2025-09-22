/*	$OpenBSD: in.h,v 1.1 2015/09/14 11:01:47 guenther Exp $	*/
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

#ifndef _LIBC_NETINET_IN_H_
#define	_LIBC_NETINET_IN_H_

#include_next <netinet/in.h>

extern PROTO_DEPRECATED(in6addr_any);
extern PROTO_DEPRECATED(in6addr_loopback);
extern PROTO_DEPRECATED(in6addr_intfacelocal_allnodes);
extern PROTO_DEPRECATED(in6addr_linklocal_allnodes);
extern PROTO_DEPRECATED(in6addr_linklocal_allrouters);

PROTO_NORMAL(bindresvport);
PROTO_NORMAL(bindresvport_sa);
PROTO_DEPRECATED(inet6_opt_append);
PROTO_DEPRECATED(inet6_opt_find);
PROTO_DEPRECATED(inet6_opt_finish);
PROTO_DEPRECATED(inet6_opt_get_val);
PROTO_DEPRECATED(inet6_opt_init);
PROTO_DEPRECATED(inet6_opt_next);
PROTO_DEPRECATED(inet6_opt_set_val);
PROTO_DEPRECATED(inet6_rth_add);
PROTO_DEPRECATED(inet6_rth_getaddr);
PROTO_DEPRECATED(inet6_rth_init);
PROTO_DEPRECATED(inet6_rth_reverse);
PROTO_DEPRECATED(inet6_rth_segments);
PROTO_NORMAL(inet6_rth_space);

#endif /* _LIBC_NETINET_IN_H_ */

