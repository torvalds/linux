/*	$OpenBSD: clnt.h,v 1.1 2015/09/13 15:36:56 guenther Exp $	*/
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

#ifndef _LIBC_RPC_CLNT_H_
#define _LIBC_RPC_CLNT_H_

#include_next <rpc/clnt.h>

PROTO_DEPRECATED(clnt_create);
PROTO_NORMAL(clnt_pcreateerror);
PROTO_DEPRECATED(clnt_perrno);
PROTO_NORMAL(clnt_perror);
PROTO_NORMAL(clnt_spcreateerror);
PROTO_NORMAL(clnt_sperrno);
PROTO_NORMAL(clnt_sperror);
PROTO_DEPRECATED(clntraw_create);
PROTO_NORMAL(clnttcp_create);
PROTO_NORMAL(clntudp_bufcreate);
PROTO_NORMAL(clntudp_create);

#endif /* !_LIBC_RPC_CLNT_H */
