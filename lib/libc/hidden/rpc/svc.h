/*	$OpenBSD: svc.h,v 1.1 2015/09/13 15:36:56 guenther Exp $	*/
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

#ifndef _LIBC_RPC_SVC_H
#define _LIBC_RPC_SVC_H

#include_next <rpc/svc.h>

__BEGIN_HIDDEN_DECLS
int	__xprt_register(SVCXPRT *);
__END_HIDDEN_DECLS

PROTO_NORMAL(svc_getreq);
PROTO_NORMAL(svc_getreq_common);
PROTO_NORMAL(svc_getreq_poll);
PROTO_DEPRECATED(svc_getreqset);
PROTO_NORMAL(svc_getreqset2);
PROTO_NORMAL(svc_register);
PROTO_DEPRECATED(svc_run);
PROTO_NORMAL(svc_sendreply);
PROTO_DEPRECATED(svc_unregister);
PROTO_NORMAL(svcerr_auth);
PROTO_NORMAL(svcerr_decode);
PROTO_DEPRECATED(svcerr_noproc);
PROTO_NORMAL(svcerr_noprog);
PROTO_NORMAL(svcerr_progvers);
PROTO_DEPRECATED(svcerr_systemerr);
PROTO_DEPRECATED(svcerr_weakauth);
PROTO_DEPRECATED(svcfd_create);
PROTO_DEPRECATED(svcraw_create);
PROTO_NORMAL(svctcp_create);
PROTO_NORMAL(svcudp_bufcreate);
PROTO_NORMAL(svcudp_create);
PROTO_DEPRECATED(svcudp_enablecache);
PROTO_DEPRECATED(xprt_register);
PROTO_NORMAL(xprt_unregister);

#endif /* !_LIBC_RPC_SVC_H */
