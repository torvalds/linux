/*	$OpenBSD: resolv.h,v 1.1 2015/10/05 02:57:16 guenther Exp $	*/
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

#ifndef _LIBC_RESOLV_H_
#define	_LIBC_RESOLV_H_

#include_next <resolv.h>

PROTO_STD_DEPRECATED(__b64_ntop);
PROTO_STD_DEPRECATED(__b64_pton);
PROTO_STD_DEPRECATED(__dn_comp);
PROTO_STD_DEPRECATED(__dn_skipname);
PROTO_NORMAL(__p_class);
PROTO_NORMAL(__p_type);
PROTO_STD_DEPRECATED(__putlong);
PROTO_STD_DEPRECATED(__putshort);
PROTO_STD_DEPRECATED(__res_dnok);
PROTO_NORMAL(__res_hnok);
PROTO_STD_DEPRECATED(__res_mailok);
PROTO_STD_DEPRECATED(__res_ownok);
PROTO_NORMAL(__res_randomid);
PROTO_STD_DEPRECATED(__res_send);
PROTO_NORMAL(__sym_ntos);

PROTO_NORMAL(dn_expand);
PROTO_NORMAL(res_init);
PROTO_DEPRECATED(res_mkquery);
PROTO_NORMAL(res_query);
PROTO_DEPRECATED(res_querydomain);
PROTO_DEPRECATED(res_search);

#endif /* !_LIBC_RESOLV_H_ */
