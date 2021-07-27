/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Encode/decode NLM basic data types
 *
 * Basic NLMv3 XDR data types are not defined in an IETF standards
 * document.  X/Open has a description of these data types that
 * is useful.  See Chapter 10 of "Protocols for Interworking:
 * XNFS, Version 3W".
 *
 * Basic NLMv4 XDR data types are defined in Appendix II.1.4 of
 * RFC 1813: "NFS Version 3 Protocol Specification".
 *
 * Author: Chuck Lever <chuck.lever@oracle.com>
 *
 * Copyright (c) 2020, Oracle and/or its affiliates.
 */

#ifndef _LOCKD_SVCXDR_H_
#define _LOCKD_SVCXDR_H_

static inline bool
svcxdr_decode_stats(struct xdr_stream *xdr, __be32 *status)
{
	__be32 *p;

	p = xdr_inline_decode(xdr, XDR_UNIT);
	if (!p)
		return false;
	*status = *p;

	return true;
}

static inline bool
svcxdr_encode_stats(struct xdr_stream *xdr, __be32 status)
{
	__be32 *p;

	p = xdr_reserve_space(xdr, XDR_UNIT);
	if (!p)
		return false;
	*p = status;

	return true;
}

static inline bool
svcxdr_decode_string(struct xdr_stream *xdr, char **data, unsigned int *data_len)
{
	__be32 *p;
	u32 len;

	if (xdr_stream_decode_u32(xdr, &len) < 0)
		return false;
	if (len > NLM_MAXSTRLEN)
		return false;
	p = xdr_inline_decode(xdr, len);
	if (!p)
		return false;
	*data_len = len;
	*data = (char *)p;

	return true;
}

/*
 * NLM cookies are defined by specification to be a variable-length
 * XDR opaque no longer than 1024 bytes. However, this implementation
 * limits their length to 32 bytes, and treats zero-length cookies
 * specially.
 */
static inline bool
svcxdr_decode_cookie(struct xdr_stream *xdr, struct nlm_cookie *cookie)
{
	__be32 *p;
	u32 len;

	if (xdr_stream_decode_u32(xdr, &len) < 0)
		return false;
	if (len > NLM_MAXCOOKIELEN)
		return false;
	if (!len)
		goto out_hpux;

	p = xdr_inline_decode(xdr, len);
	if (!p)
		return false;
	cookie->len = len;
	memcpy(cookie->data, p, len);

	return true;

	/* apparently HPUX can return empty cookies */
out_hpux:
	cookie->len = 4;
	memset(cookie->data, 0, 4);
	return true;
}

static inline bool
svcxdr_encode_cookie(struct xdr_stream *xdr, const struct nlm_cookie *cookie)
{
	__be32 *p;

	if (xdr_stream_encode_u32(xdr, cookie->len) < 0)
		return false;
	p = xdr_reserve_space(xdr, cookie->len);
	if (!p)
		return false;
	memcpy(p, cookie->data, cookie->len);

	return true;
}

static inline bool
svcxdr_decode_owner(struct xdr_stream *xdr, struct xdr_netobj *obj)
{
	__be32 *p;
	u32 len;

	if (xdr_stream_decode_u32(xdr, &len) < 0)
		return false;
	if (len > XDR_MAX_NETOBJ)
		return false;
	p = xdr_inline_decode(xdr, len);
	if (!p)
		return false;
	obj->len = len;
	obj->data = (u8 *)p;

	return true;
}

static inline bool
svcxdr_encode_owner(struct xdr_stream *xdr, const struct xdr_netobj *obj)
{
	unsigned int quadlen = XDR_QUADLEN(obj->len);
	__be32 *p;

	if (xdr_stream_encode_u32(xdr, obj->len) < 0)
		return false;
	p = xdr_reserve_space(xdr, obj->len);
	if (!p)
		return false;
	p[quadlen - 1] = 0;	/* XDR pad */
	memcpy(p, obj->data, obj->len);

	return true;
}

#endif /* _LOCKD_SVCXDR_H_ */
