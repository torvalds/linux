/*	$OpenBSD: toeplitz.h,v 1.11 2023/05/17 10:22:17 dlg Exp $ */

/*
 * Copyright (c) 2019 David Gwynne <dlg@openbsd.org>
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

#ifndef _SYS_NET_TOEPLITZ_H_
#define _SYS_NET_TOEPLITZ_H_

#include <sys/endian.h>

/*
 * symmetric toeplitz
 */

typedef uint16_t stoeplitz_key;

struct stoeplitz_cache {
	uint16_t	bytes[256];
};

static __unused inline uint16_t
stoeplitz_cache_entry(const struct stoeplitz_cache *scache, uint8_t byte)
{
	return (scache->bytes[byte]);
}

void		stoeplitz_cache_init(struct stoeplitz_cache *, stoeplitz_key);

uint16_t	stoeplitz_hash_ip4(const struct stoeplitz_cache *,
		    uint32_t, uint32_t);
uint16_t	stoeplitz_hash_ip4port(const struct stoeplitz_cache *,
		    uint32_t, uint32_t, uint16_t, uint16_t);

#ifdef INET6
struct in6_addr;
uint16_t	stoeplitz_hash_ip6(const struct stoeplitz_cache *,
		    const struct in6_addr *, const struct in6_addr *);
uint16_t	stoeplitz_hash_ip6port(const struct stoeplitz_cache *,
		    const struct in6_addr *, const struct in6_addr *,
		    uint16_t, uint16_t);
#endif

uint16_t	stoeplitz_hash_eaddr(const struct stoeplitz_cache *,
		    const uint8_t *);

/* hash a uint16_t in network byte order */
static __unused inline uint16_t
stoeplitz_hash_n16(const struct stoeplitz_cache *scache, uint16_t n16)
{
	uint16_t hi, lo;

	hi = stoeplitz_cache_entry(scache, n16 >> 8);
	lo = stoeplitz_cache_entry(scache, n16);

	return (hi ^ swap16(lo));
}

/* hash a uint32_t in network byte order */
static __unused inline uint16_t
stoeplitz_hash_n32(const struct stoeplitz_cache *scache, uint32_t n32)
{
	return (stoeplitz_hash_n16(scache, n32 ^ (n32 >> 16)));
}

/* hash a uint16_t in host byte order */
static __unused inline uint16_t
stoeplitz_hash_h16(const struct stoeplitz_cache *scache, uint16_t h16)
{
	uint16_t lo, hi;

	lo = stoeplitz_cache_entry(scache, h16);
	hi = stoeplitz_cache_entry(scache, h16 >> 8);

#if _BYTE_ORDER == _BIG_ENDIAN
	return (hi ^ swap16(lo));
#else
	return (swap16(hi) ^ lo);
#endif
}

static __unused inline uint16_t
stoeplitz_hash_h32(const struct stoeplitz_cache *scache, uint32_t h32)
{
	return (stoeplitz_hash_h16(scache, h32 ^ (h32 >> 16)));
}

static __unused inline uint16_t
stoeplitz_hash_h64(const struct stoeplitz_cache *scache, uint64_t h64)
{
	return (stoeplitz_hash_h32(scache, h64 ^ (h64 >> 32)));
}

/*
 * system provided symmetric toeplitz
 */

#define STOEPLITZ_KEYSEED	0x6d5a

void		stoeplitz_init(void);

void		stoeplitz_to_key(void *, size_t)
		    __bounded((__buffer__, 1, 2));

extern const struct stoeplitz_cache *const stoeplitz_cache;

#define stoeplitz_n16(_n16) \
	stoeplitz_hash_n16(stoeplitz_cache, (_n16))
#define stoeplitz_n32(_n32) \
	stoeplitz_hash_n32(stoeplitz_cache, (_n32))
#define stoeplitz_h16(_h16) \
	stoeplitz_hash_h16(stoeplitz_cache, (_h16))
#define stoeplitz_h32(_h32) \
	stoeplitz_hash_h32(stoeplitz_cache, (_h32))
#define stoeplitz_h64(_h64) \
	stoeplitz_hash_h64(stoeplitz_cache, (_h64))
#define stoeplitz_port(_p)	stoeplitz_n16((_p))
#define stoeplitz_ip4(_sa4, _da4) \
	stoeplitz_hash_ip4(stoeplitz_cache, (_sa4), (_da4))
#define stoeplitz_ip4port(_sa4, _da4, _sp, _dp) \
	stoeplitz_hash_ip4port(stoeplitz_cache, (_sa4), (_da4), (_sp), (_dp))
#ifdef INET6
#define stoeplitz_ip6(_sa6, _da6) \
	stoeplitz_hash_ip6(stoeplitz_cache, (_sa6), (_da6))
#define stoeplitz_ip6port(_sa6, _da6, _sp, _dp) \
	stoeplitz_hash_ip6port(stoeplitz_cache, (_sa6), (_da6), (_sp), (_dp))
#endif
#define stoeplitz_eaddr(_ea) \
	stoeplitz_hash_eaddr(stoeplitz_cache, (_ea))

#endif /* _SYS_NET_TOEPLITZ_H_ */
