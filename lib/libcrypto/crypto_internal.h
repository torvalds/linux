/*	$OpenBSD: crypto_internal.h,v 1.16 2025/07/22 09:18:02 jsing Exp $ */
/*
 * Copyright (c) 2023 Joel Sing <jsing@openbsd.org>
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

#include <endian.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "crypto_arch.h"

#ifndef HEADER_CRYPTO_INTERNAL_H
#define HEADER_CRYPTO_INTERNAL_H

#define CTASSERT(x) \
    extern char _ctassert[(x) ? 1 : -1] __attribute__((__unused__))

/*
 * Constant time functions for size_t.
 */
#ifndef HAVE_CRYPTO_CT_NE_ZERO
static inline int
crypto_ct_ne_zero(size_t v)
{
	return (v | ~(v - 1)) >> ((sizeof(v) * 8) - 1);
}
#endif

#ifndef HAVE_CRYPTO_CT_NE_ZERO_MASK
static inline size_t
crypto_ct_ne_zero_mask(size_t v)
{
	return 0 - crypto_ct_ne_zero(v);
}
#endif

#ifndef HAVE_CRYPTO_CT_EQ_ZERO
static inline int
crypto_ct_eq_zero(size_t v)
{
	return 1 - crypto_ct_ne_zero(v);
}
#endif

#ifndef HAVE_CRYPTO_CT_EQ_ZERO_MASK_U8
static inline size_t
crypto_ct_eq_zero_mask(size_t v)
{
	return 0 - crypto_ct_eq_zero(v);
}
#endif

#ifndef HAVE_CRYPTO_CT_LT
static inline int
crypto_ct_lt(size_t a, size_t b)
{
	return (((a - b) | (b & ~a)) & (b | ~a)) >>
	    (sizeof(size_t) * 8 - 1);
}
#endif

#ifndef HAVE_CRYPTO_CT_LT_MASK
static inline size_t
crypto_ct_lt_mask(size_t a, size_t b)
{
	return 0 - crypto_ct_lt(a, b);
}
#endif

#ifndef HAVE_CRYPTO_CT_GT
static inline int
crypto_ct_gt(size_t a, size_t b)
{
	return crypto_ct_lt(b, a);
}
#endif

#ifndef HAVE_CRYPTO_CT_GT_MASK
static inline size_t
crypto_ct_gt_mask(size_t a, size_t b)
{
	return 0 - crypto_ct_gt(a, b);
}
#endif

/*
 * Constant time operations for uint8_t.
 */
#ifndef HAVE_CRYPTO_CT_NE_ZERO_U8
static inline int
crypto_ct_ne_zero_u8(uint8_t v)
{
	return (uint8_t)(v | ~(v - 1)) >> ((sizeof(v) * 8) - 1);
}
#endif

#ifndef HAVE_CRYPTO_CT_NE_ZERO_MASK_U8
static inline uint8_t
crypto_ct_ne_zero_mask_u8(uint8_t v)
{
	return 0 - crypto_ct_ne_zero_u8(v);
}
#endif

#ifndef HAVE_CRYPTO_CT_EQ_ZERO_U8
static inline int
crypto_ct_eq_zero_u8(uint8_t v)
{
	return 1 - crypto_ct_ne_zero_u8(v);
}
#endif

#ifndef HAVE_CRYPTO_CT_EQ_ZERO_MASK_U8
static inline uint8_t
crypto_ct_eq_zero_mask_u8(uint8_t v)
{
	return 0 - crypto_ct_eq_zero_u8(v);
}
#endif

#ifndef HAVE_CRYPTO_CT_NE_U8
static inline int
crypto_ct_ne_u8(uint8_t a, uint8_t b)
{
	return crypto_ct_ne_zero_u8(a - b);
}
#endif

#ifndef HAVE_CRYPTO_CT_NE_MASK_U8
static inline uint8_t
crypto_ct_ne_mask_u8(uint8_t a, uint8_t b)
{
	return 0 - crypto_ct_ne_u8(a, b);
}
#endif

#ifndef HAVE_CRYPTO_CT_EQ_U8
static inline int
crypto_ct_eq_u8(uint8_t a, uint8_t b)
{
	return crypto_ct_eq_zero_u8(a - b);
}
#endif

#ifndef HAVE_CRYPTO_CT_EQ_MASK_U8
static inline uint8_t
crypto_ct_eq_mask_u8(uint8_t a, uint8_t b)
{
	return 0 - crypto_ct_eq_u8(a, b);
}
#endif

/*
 * crypto_load_be32toh() loads a 32 bit unsigned big endian value as a 32 bit
 * unsigned host endian value, from the specified address in memory. The memory
 * address may have any alignment.
 */
#ifndef HAVE_CRYPTO_LOAD_BE32TOH
static inline uint32_t
crypto_load_be32toh(const uint8_t *src)
{
	uint32_t v;

	memcpy(&v, src, sizeof(v));

	return be32toh(v);
}
#endif

/*
 * crypto_store_htobe32() stores a 32 bit unsigned host endian value as a 32 bit
 * unsigned big endian value, at the specified address in memory. The memory
 * address may have any alignment.
 */
#ifndef HAVE_CRYPTO_STORE_HTOBE32
static inline void
crypto_store_htobe32(uint8_t *dst, uint32_t v)
{
	v = htobe32(v);
	memcpy(dst, &v, sizeof(v));
}
#endif

/*
 * crypto_load_be64toh() loads a 64 bit unsigned big endian value as a 64 bit
 * unsigned host endian value, from the specified address in memory. The memory
 * address may have any alignment.
 */
#ifndef HAVE_CRYPTO_LOAD_BE64TOH
static inline uint64_t
crypto_load_be64toh(const uint8_t *src)
{
	uint64_t v;

	memcpy(&v, src, sizeof(v));

	return be64toh(v);
}
#endif

/*
 * crypto_store_htobe64() stores a 64 bit unsigned host endian value as a 64 bit
 * unsigned big endian value, at the specified address in memory. The memory
 * address may have any alignment.
 */
#ifndef HAVE_CRYPTO_STORE_HTOBE64
static inline void
crypto_store_htobe64(uint8_t *dst, uint64_t v)
{
	v = htobe64(v);
	memcpy(dst, &v, sizeof(v));
}
#endif

/*
 * crypto_load_le32toh() loads a 32 bit unsigned little endian value as a 32 bit
 * unsigned host endian value, from the specified address in memory. The memory
 * address may have any alignment.
 */
#ifndef HAVE_CRYPTO_LOAD_LE32TOH
static inline uint32_t
crypto_load_le32toh(const uint8_t *src)
{
	uint32_t v;

	memcpy(&v, src, sizeof(v));

	return le32toh(v);
}
#endif

/*
 * crypto_store_htole32() stores a 32 bit unsigned host endian value as a 32 bit
 * unsigned little endian value, at the specified address in memory. The memory
 * address may have any alignment.
 */
#ifndef HAVE_CRYPTO_STORE_HTOLE32
static inline void
crypto_store_htole32(uint8_t *dst, uint32_t v)
{
	v = htole32(v);
	memcpy(dst, &v, sizeof(v));
}
#endif

#ifndef HAVE_CRYPTO_ADD_U32DW_U64
static inline void
crypto_add_u32dw_u64(uint32_t *h, uint32_t *l, uint64_t v)
{
	v += ((uint64_t)*h << 32) | *l;
	*h = v >> 32;
	*l = v;
}
#endif

#ifndef HAVE_CRYPTO_ROL_U32
static inline uint32_t
crypto_rol_u32(uint32_t v, size_t shift)
{
	return (v << shift) | (v >> (32 - shift));
}
#endif

#ifndef HAVE_CRYPTO_ROR_U32
static inline uint32_t
crypto_ror_u32(uint32_t v, size_t shift)
{
	return (v << (32 - shift)) | (v >> shift);
}
#endif

#ifndef HAVE_CRYPTO_ROL_U64
static inline uint64_t
crypto_rol_u64(uint64_t v, size_t shift)
{
	return (v << shift) | (v >> (64 - shift));
}
#endif

#ifndef HAVE_CRYPTO_ROR_U64
static inline uint64_t
crypto_ror_u64(uint64_t v, size_t shift)
{
	return (v << (64 - shift)) | (v >> shift);
}
#endif

void crypto_cpu_caps_init(void);

#endif
