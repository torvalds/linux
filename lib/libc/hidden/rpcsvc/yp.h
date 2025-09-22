/*	$OpenBSD: yp.h,v 1.2 2015/09/10 18:13:46 guenther Exp $	*/
/*
 * Copyright (c) 2015 Theo de Raadt <deraadt@openbsd.org>
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

#ifndef _LIBC_RPCSVC_YP_H_
#define	_LIBC_RPCSVC_YP_H_

#include_next <rpcsvc/yp.h>

PROTO_NORMAL(xdr_domainname);
PROTO_NORMAL(xdr_keydat);
PROTO_NORMAL(xdr_mapname);
PROTO_NORMAL(xdr_peername);
PROTO_NORMAL(xdr_valdat);
PROTO_NORMAL(xdr_ypbind_binding);
PROTO_NORMAL(xdr_ypbind_resp);
PROTO_NORMAL(xdr_ypbind_resptype);
PROTO_NORMAL(xdr_ypbind_setdom);
PROTO_NORMAL(xdr_ypmaplist);
PROTO_NORMAL(xdr_ypreq_key);
PROTO_NORMAL(xdr_ypreq_nokey);
PROTO_NORMAL(xdr_ypresp_all);
PROTO_NORMAL(xdr_ypresp_key_val);
PROTO_NORMAL(xdr_ypresp_maplist);
PROTO_NORMAL(xdr_ypresp_master);
PROTO_NORMAL(xdr_ypresp_order);
PROTO_NORMAL(xdr_ypresp_val);
PROTO_NORMAL(xdr_ypstat);

#endif /* _LIBC_RPCSVC_YP_H_ */
