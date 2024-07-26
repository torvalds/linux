/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024, SUSE LLC
 *
 * Authors: Enzo Matsumiya <ematsumiya@suse.de>
 *
 * Definitions and optmized helpers for LZ77 compression.
 */
#ifndef _SMB_COMPRESS_LZ77_H
#define _SMB_COMPRESS_LZ77_H

#include <linux/uaccess.h>
#ifdef CONFIG_CIFS_COMPRESSION
#include <asm/ptrace.h>
#include <linux/kernel.h>
#include <linux/string.h>
#ifndef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
#include <asm-generic/unaligned.h>
#endif

#define LZ77_HASH_LOG		13
#define LZ77_HASH_SIZE		(1 << LZ77_HASH_LOG)
#define LZ77_HASH_MASK		lz77_hash_mask(LZ77_HASH_LOG)

/* We can increase this for better compression (but worse performance). */
#define LZ77_MATCH_MIN_LEN	3
/* From MS-XCA, but it's arbitrarily chosen. */
#define LZ77_MATCH_MAX_LEN	S32_MAX
/*
 * Check this to ensure we don't match the current position, which would
 * end up doing a verbatim copy of the input, and actually overflowing
 * the output buffer because of the encoded metadata.
 */
#define LZ77_MATCH_MIN_DIST	1
/* How far back in the buffer can we try to find a match (i.e. window size) */
#define LZ77_MATCH_MAX_DIST	8192

#define LZ77_STEPSIZE_16	sizeof(u16)
#define LZ77_STEPSIZE_32	sizeof(u32)
#define LZ77_STEPSIZE_64	sizeof(u64)

struct lz77_flags {
	u8 *pos;
	size_t count;
	long val;
};

static __always_inline u32 lz77_hash_mask(const unsigned int log2)
{
	return ((1 << log2) - 1);
}

static __always_inline u32 lz77_hash64(const u64 v, const unsigned int log2)
{
	const u64 prime5bytes = 889523592379ULL;

	return (u32)(((v << 24) * prime5bytes) >> (64 - log2));
}

static __always_inline u32 lz77_hash32(const u32 v, const unsigned int log2)
{
	return ((v * 2654435769LL) >> (32 - log2)) & lz77_hash_mask(log2);
}

static __always_inline u32 lz77_log2(unsigned int x)
{
	return x ? ((u32)(31 - __builtin_clz(x))) : 0;
}

#ifdef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
static __always_inline u8 lz77_read8(const void *ptr)
{
	return *(u8 *)ptr;
}

static __always_inline u16 lz77_read16(const void *ptr)
{
	return *(u16 *)ptr;
}

static __always_inline u32 lz77_read32(const void *ptr)
{
	return *(u32 *)ptr;
}

static __always_inline u64 lz77_read64(const void *ptr)
{
	return *(u64 *)ptr;
}

static __always_inline void lz77_write8(void *ptr, const u8 v)
{
	*(u8 *)ptr = v;
}

static __always_inline void lz77_write16(void *ptr, const u16 v)
{
	*(u16 *)ptr = v;
}

static __always_inline void lz77_write32(void *ptr, const u32 v)
{
	*(u32 *)ptr = v;
}

static __always_inline void lz77_write64(void *ptr, const u64 v)
{
	*(u64 *)ptr = v;
}

static __always_inline void lz77_write_ptr16(void *ptr, const void *vp)
{
	*(u16 *)ptr = *(const u16 *)vp;
}

static __always_inline void lz77_write_ptr32(void *ptr, const void *vp)
{
	*(u32 *)ptr = *(const u32 *)vp;
}

static __always_inline void lz77_write_ptr64(void *ptr, const void *vp)
{
	*(u64 *)ptr = *(const u64 *)vp;
}

static __always_inline long lz77_copy(u8 *dst, const u8 *src, size_t count)
{
	return copy_from_kernel_nofault(dst, src, count);
}
#else /* CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS */
static __always_inline u8 lz77_read8(const void *ptr)
{
	return get_unaligned((u8 *)ptr);
}

static __always_inline u16 lz77_read16(const void *ptr)
{
	return lz77_read8(ptr) | (lz77_read8(ptr + 1) << 8);
}

static __always_inline u32 lz77_read32(const void *ptr)
{
	return lz77_read16(ptr) | (lz77_read16(ptr + 2) << 16);
}

static __always_inline u64 lz77_read64(const void *ptr)
{
	return lz77_read32(ptr) | ((u64)lz77_read32(ptr + 4) << 32);
}

static __always_inline void lz77_write8(void *ptr, const u8 v)
{
	put_unaligned(v, (u8 *)ptr);
}

static __always_inline void lz77_write16(void *ptr, const u16 v)
{
	lz77_write8(ptr, v & 0xff);
	lz77_write8(ptr + 1, (v >> 8) & 0xff);
}

static __always_inline void lz77_write32(void *ptr, const u32 v)
{
	lz77_write16(ptr, v & 0xffff);
	lz77_write16(ptr + 2, (v >> 16) & 0xffff);
}

static __always_inline void lz77_write64(void *ptr, const u64 v)
{
	lz77_write32(ptr, v & 0xffffffff);
	lz77_write32(ptr + 4, (v >> 32) & 0xffffffff);
}

static __always_inline void lz77_write_ptr16(void *ptr, const void *vp)
{
	const u16 v = lz77_read16(vp);

	lz77_write16(ptr, v);
}

static __always_inline void lz77_write_ptr32(void *ptr, const void *vp)
{
	const u32 v = lz77_read32(vp);

	lz77_write32(ptr, v);
}

static __always_inline void lz77_write_ptr64(void *ptr, const void *vp)
{
	const u64 v = lz77_read64(vp);

	lz77_write64(ptr, v);
}
static __always_inline long lz77_copy(u8 *dst, const u8 *src, size_t count)
{
	memcpy(dst, src, count);
	return 0;
}
#endif /* !CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS */

static __always_inline unsigned int __count_common_bytes(const unsigned long diff)
{
#ifdef __has_builtin
#  if __has_builtin(__builtin_ctzll)
	return (unsigned int)__builtin_ctzll(diff) >> 3;
#  endif
#else
	/* count trailing zeroes */
	unsigned long bits = 0, i, z = 0;

	bits |= diff;
	for (i = 0; i < 64; i++) {
		if (bits[i])
			break;
		z++;
	}

	return (unsigned int)z >> 3;
#endif
}

static __always_inline size_t lz77_match(const u8 *match, const u8 *cur, const u8 *end)
{
	const u8 *start = cur;

	if (cur == match)
		return 0;

	if (likely(cur < end - (LZ77_STEPSIZE_64 - 1))) {
		u64 const diff = lz77_read64(cur) ^ lz77_read64(match);

		if (!diff) {
			cur += LZ77_STEPSIZE_64;
			match += LZ77_STEPSIZE_64;
		} else {
			return __count_common_bytes(diff);
		}
	}

	while (likely(cur < end - (LZ77_STEPSIZE_64 - 1))) {
		u64 const diff = lz77_read64(cur) ^ lz77_read64(match);

		if (!diff) {
			cur += LZ77_STEPSIZE_64;
			match += LZ77_STEPSIZE_64;
			continue;
		}

		cur += __count_common_bytes(diff);
		return (size_t)(cur - start);
	}

	if (cur < end - 3 && !(lz77_read32(cur) ^ lz77_read32(match))) {
		cur += LZ77_STEPSIZE_32;
		match += LZ77_STEPSIZE_32;
	}

	if (cur < end - 1 && lz77_read16(cur) == lz77_read16(match)) {
		cur += LZ77_STEPSIZE_16;
		match += LZ77_STEPSIZE_16;
	}

	if (cur < end && *cur == *match)
		cur++;

	return (size_t)(cur - start);
}

static __always_inline unsigned long lz77_max(unsigned long a, unsigned long b)
{
	int m = (a < b) - 1;

	return (a & m) | (b & ~m);
}

static __always_inline unsigned long lz77_min(unsigned long a, unsigned long b)
{
	int m = (a > b) - 1;

	return (a & m) | (b & ~m);
}

int lz77_compress(const u8 *src, size_t src_len, u8 *dst, size_t *dst_len);
/* when CONFIG_CIFS_COMPRESSION not set lz77_compress() is not called */
#endif /* !CONFIG_CIFS_COMPRESSION */
#endif /* _SMB_COMPRESS_LZ77_H */
