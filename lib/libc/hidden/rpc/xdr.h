/*	$OpenBSD: xdr.h,v 1.1 2015/09/13 15:36:56 guenther Exp $	*/
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

#ifndef _LIBC_RPC_XDR_H
#define _LIBC_RPC_XDR_H

#include_next <rpc/xdr.h>

PROTO_NORMAL(xdr_array);
PROTO_NORMAL(xdr_bool);
PROTO_NORMAL(xdr_bytes);
PROTO_DEPRECATED(xdr_char);
PROTO_DEPRECATED(xdr_double);
PROTO_NORMAL(xdr_enum);
PROTO_DEPRECATED(xdr_float);
PROTO_NORMAL(xdr_free);
PROTO_NORMAL(xdr_int);
PROTO_DEPRECATED(xdr_int16_t);
PROTO_DEPRECATED(xdr_int32_t);
PROTO_DEPRECATED(xdr_int64_t);
PROTO_NORMAL(xdr_long);
PROTO_DEPRECATED(xdr_netobj);
PROTO_NORMAL(xdr_opaque);
PROTO_NORMAL(xdr_pointer);
PROTO_NORMAL(xdr_reference);
PROTO_NORMAL(xdr_short);
PROTO_NORMAL(xdr_string);
PROTO_DEPRECATED(xdr_u_char);
PROTO_NORMAL(xdr_u_int);
PROTO_DEPRECATED(xdr_u_int16_t);
PROTO_NORMAL(xdr_u_int32_t);
PROTO_DEPRECATED(xdr_u_int64_t);
PROTO_NORMAL(xdr_u_long);
PROTO_NORMAL(xdr_u_short);
PROTO_NORMAL(xdr_union);
PROTO_DEPRECATED(xdr_vector);
PROTO_NORMAL(xdr_void);
PROTO_DEPRECATED(xdr_wrapstring);
PROTO_NORMAL(xdrmem_create);
PROTO_NORMAL(xdrrec_create);
PROTO_NORMAL(xdrrec_endofrecord);
PROTO_NORMAL(xdrrec_eof);
PROTO_NORMAL(xdrrec_skiprecord);
#ifdef _STDIO_H_
PROTO_DEPRECATED(xdrstdio_create);
#endif

#endif /* !_LIBC_RPC_XDR_H */
