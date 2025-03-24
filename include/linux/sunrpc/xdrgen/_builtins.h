/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 Oracle and/or its affiliates.
 *
 * This header defines XDR data type primitives specified in
 * Section 4 of RFC 4506, used by RPC programs implemented
 * in the Linux kernel.
 */

#ifndef _SUNRPC_XDRGEN__BUILTINS_H_
#define _SUNRPC_XDRGEN__BUILTINS_H_

#include <linux/sunrpc/xdr.h>

static inline bool
xdrgen_decode_void(struct xdr_stream *xdr)
{
	return true;
}

static inline bool
xdrgen_encode_void(struct xdr_stream *xdr)
{
	return true;
}

static inline bool
xdrgen_decode_bool(struct xdr_stream *xdr, bool *ptr)
{
	__be32 *p = xdr_inline_decode(xdr, XDR_UNIT);

	if (unlikely(!p))
		return false;
	*ptr = (*p != xdr_zero);
	return true;
}

static inline bool
xdrgen_encode_bool(struct xdr_stream *xdr, bool val)
{
	__be32 *p = xdr_reserve_space(xdr, XDR_UNIT);

	if (unlikely(!p))
		return false;
	*p = val ? xdr_one : xdr_zero;
	return true;
}

static inline bool
xdrgen_decode_int(struct xdr_stream *xdr, s32 *ptr)
{
	__be32 *p = xdr_inline_decode(xdr, XDR_UNIT);

	if (unlikely(!p))
		return false;
	*ptr = be32_to_cpup(p);
	return true;
}

static inline bool
xdrgen_encode_int(struct xdr_stream *xdr, s32 val)
{
	__be32 *p = xdr_reserve_space(xdr, XDR_UNIT);

	if (unlikely(!p))
		return false;
	*p = cpu_to_be32(val);
	return true;
}

static inline bool
xdrgen_decode_unsigned_int(struct xdr_stream *xdr, u32 *ptr)
{
	__be32 *p = xdr_inline_decode(xdr, XDR_UNIT);

	if (unlikely(!p))
		return false;
	*ptr = be32_to_cpup(p);
	return true;
}

static inline bool
xdrgen_encode_unsigned_int(struct xdr_stream *xdr, u32 val)
{
	__be32 *p = xdr_reserve_space(xdr, XDR_UNIT);

	if (unlikely(!p))
		return false;
	*p = cpu_to_be32(val);
	return true;
}

static inline bool
xdrgen_decode_long(struct xdr_stream *xdr, s32 *ptr)
{
	__be32 *p = xdr_inline_decode(xdr, XDR_UNIT);

	if (unlikely(!p))
		return false;
	*ptr = be32_to_cpup(p);
	return true;
}

static inline bool
xdrgen_encode_long(struct xdr_stream *xdr, s32 val)
{
	__be32 *p = xdr_reserve_space(xdr, XDR_UNIT);

	if (unlikely(!p))
		return false;
	*p = cpu_to_be32(val);
	return true;
}

static inline bool
xdrgen_decode_unsigned_long(struct xdr_stream *xdr, u32 *ptr)
{
	__be32 *p = xdr_inline_decode(xdr, XDR_UNIT);

	if (unlikely(!p))
		return false;
	*ptr = be32_to_cpup(p);
	return true;
}

static inline bool
xdrgen_encode_unsigned_long(struct xdr_stream *xdr, u32 val)
{
	__be32 *p = xdr_reserve_space(xdr, XDR_UNIT);

	if (unlikely(!p))
		return false;
	*p = cpu_to_be32(val);
	return true;
}

static inline bool
xdrgen_decode_hyper(struct xdr_stream *xdr, s64 *ptr)
{
	__be32 *p = xdr_inline_decode(xdr, XDR_UNIT * 2);

	if (unlikely(!p))
		return false;
	*ptr = get_unaligned_be64(p);
	return true;
}

static inline bool
xdrgen_encode_hyper(struct xdr_stream *xdr, s64 val)
{
	__be32 *p = xdr_reserve_space(xdr, XDR_UNIT * 2);

	if (unlikely(!p))
		return false;
	put_unaligned_be64(val, p);
	return true;
}

static inline bool
xdrgen_decode_unsigned_hyper(struct xdr_stream *xdr, u64 *ptr)
{
	__be32 *p = xdr_inline_decode(xdr, XDR_UNIT * 2);

	if (unlikely(!p))
		return false;
	*ptr = get_unaligned_be64(p);
	return true;
}

static inline bool
xdrgen_encode_unsigned_hyper(struct xdr_stream *xdr, u64 val)
{
	__be32 *p = xdr_reserve_space(xdr, XDR_UNIT * 2);

	if (unlikely(!p))
		return false;
	put_unaligned_be64(val, p);
	return true;
}

static inline bool
xdrgen_decode_string(struct xdr_stream *xdr, string *ptr, u32 maxlen)
{
	__be32 *p;
	u32 len;

	if (unlikely(xdr_stream_decode_u32(xdr, &len) < 0))
		return false;
	if (unlikely(maxlen && len > maxlen))
		return false;
	if (len != 0) {
		p = xdr_inline_decode(xdr, len);
		if (unlikely(!p))
			return false;
		ptr->data = (unsigned char *)p;
	}
	ptr->len = len;
	return true;
}

static inline bool
xdrgen_encode_string(struct xdr_stream *xdr, string val, u32 maxlen)
{
	__be32 *p = xdr_reserve_space(xdr, XDR_UNIT + xdr_align_size(val.len));

	if (unlikely(!p))
		return false;
	xdr_encode_opaque(p, val.data, val.len);
	return true;
}

static inline bool
xdrgen_decode_opaque(struct xdr_stream *xdr, opaque *ptr, u32 maxlen)
{
	__be32 *p;
	u32 len;

	if (unlikely(xdr_stream_decode_u32(xdr, &len) < 0))
		return false;
	if (unlikely(maxlen && len > maxlen))
		return false;
	if (len != 0) {
		p = xdr_inline_decode(xdr, len);
		if (unlikely(!p))
			return false;
		ptr->data = (u8 *)p;
	}
	ptr->len = len;
	return true;
}

static inline bool
xdrgen_encode_opaque(struct xdr_stream *xdr, opaque val)
{
	__be32 *p = xdr_reserve_space(xdr, XDR_UNIT + xdr_align_size(val.len));

	if (unlikely(!p))
		return false;
	xdr_encode_opaque(p, val.data, val.len);
	return true;
}

#endif /* _SUNRPC_XDRGEN__BUILTINS_H_ */
