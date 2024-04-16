// SPDX-License-Identifier: BSD-3-Clause
/*
 * linux/net/sunrpc/auth_gss/auth_gss_internal.h
 *
 * Internal definitions for RPCSEC_GSS client authentication
 *
 * Copyright (c) 2000 The Regents of the University of Michigan.
 * All rights reserved.
 *
 */
#include <linux/err.h>
#include <linux/string.h>
#include <linux/sunrpc/xdr.h>

static inline const void *
simple_get_bytes(const void *p, const void *end, void *res, size_t len)
{
	const void *q = (const void *)((const char *)p + len);
	if (unlikely(q > end || q < p))
		return ERR_PTR(-EFAULT);
	memcpy(res, p, len);
	return q;
}

static inline const void *
simple_get_netobj(const void *p, const void *end, struct xdr_netobj *dest)
{
	const void *q;
	unsigned int len;

	p = simple_get_bytes(p, end, &len, sizeof(len));
	if (IS_ERR(p))
		return p;
	q = (const void *)((const char *)p + len);
	if (unlikely(q > end || q < p))
		return ERR_PTR(-EFAULT);
	if (len) {
		dest->data = kmemdup(p, len, GFP_KERNEL);
		if (unlikely(dest->data == NULL))
			return ERR_PTR(-ENOMEM);
	} else
		dest->data = NULL;
	dest->len = len;
	return q;
}
