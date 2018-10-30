/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _XFRM_HASH_H
#define _XFRM_HASH_H

#include <linux/xfrm.h>
#include <linux/socket.h>
#include <linux/jhash.h>

static inline unsigned int __xfrm4_addr_hash(const xfrm_address_t *addr)
{
	return ntohl(addr->a4);
}

static inline unsigned int __xfrm6_addr_hash(const xfrm_address_t *addr)
{
	return jhash2((__force u32 *)addr->a6, 4, 0);
}

static inline unsigned int __xfrm4_daddr_saddr_hash(const xfrm_address_t *daddr,
						    const xfrm_address_t *saddr)
{
	u32 sum = (__force u32)daddr->a4 + (__force u32)saddr->a4;
	return ntohl((__force __be32)sum);
}

static inline unsigned int __xfrm6_daddr_saddr_hash(const xfrm_address_t *daddr,
						    const xfrm_address_t *saddr)
{
	return __xfrm6_addr_hash(daddr) ^ __xfrm6_addr_hash(saddr);
}

static inline u32 __bits2mask32(__u8 bits)
{
	u32 mask32 = 0xffffffff;

	if (bits == 0)
		mask32 = 0;
	else if (bits < 32)
		mask32 <<= (32 - bits);

	return mask32;
}

static inline unsigned int __xfrm4_dpref_spref_hash(const xfrm_address_t *daddr,
						    const xfrm_address_t *saddr,
						    __u8 dbits,
						    __u8 sbits)
{
	return jhash_2words(ntohl(daddr->a4) & __bits2mask32(dbits),
			    ntohl(saddr->a4) & __bits2mask32(sbits),
			    0);
}

static inline unsigned int __xfrm6_pref_hash(const xfrm_address_t *addr,
					     __u8 prefixlen)
{
	unsigned int pdw;
	unsigned int pbi;
	u32 initval = 0;

	pdw = prefixlen >> 5;     /* num of whole u32 in prefix */
	pbi = prefixlen &  0x1f;  /* num of bits in incomplete u32 in prefix */

	if (pbi) {
		__be32 mask;

		mask = htonl((0xffffffff) << (32 - pbi));

		initval = (__force u32)(addr->a6[pdw] & mask);
	}

	return jhash2((__force u32 *)addr->a6, pdw, initval);
}

static inline unsigned int __xfrm6_dpref_spref_hash(const xfrm_address_t *daddr,
						    const xfrm_address_t *saddr,
						    __u8 dbits,
						    __u8 sbits)
{
	return __xfrm6_pref_hash(daddr, dbits) ^
	       __xfrm6_pref_hash(saddr, sbits);
}

static inline unsigned int __xfrm_dst_hash(const xfrm_address_t *daddr,
					   const xfrm_address_t *saddr,
					   u32 reqid, unsigned short family,
					   unsigned int hmask)
{
	unsigned int h = family ^ reqid;
	switch (family) {
	case AF_INET:
		h ^= __xfrm4_daddr_saddr_hash(daddr, saddr);
		break;
	case AF_INET6:
		h ^= __xfrm6_daddr_saddr_hash(daddr, saddr);
		break;
	}
	return (h ^ (h >> 16)) & hmask;
}

static inline unsigned int __xfrm_src_hash(const xfrm_address_t *daddr,
					   const xfrm_address_t *saddr,
					   unsigned short family,
					   unsigned int hmask)
{
	unsigned int h = family;
	switch (family) {
	case AF_INET:
		h ^= __xfrm4_daddr_saddr_hash(daddr, saddr);
		break;
	case AF_INET6:
		h ^= __xfrm6_daddr_saddr_hash(daddr, saddr);
		break;
	}
	return (h ^ (h >> 16)) & hmask;
}

static inline unsigned int
__xfrm_spi_hash(const xfrm_address_t *daddr, __be32 spi, u8 proto,
		unsigned short family, unsigned int hmask)
{
	unsigned int h = (__force u32)spi ^ proto;
	switch (family) {
	case AF_INET:
		h ^= __xfrm4_addr_hash(daddr);
		break;
	case AF_INET6:
		h ^= __xfrm6_addr_hash(daddr);
		break;
	}
	return (h ^ (h >> 10) ^ (h >> 20)) & hmask;
}

static inline unsigned int __idx_hash(u32 index, unsigned int hmask)
{
	return (index ^ (index >> 8)) & hmask;
}

static inline unsigned int __sel_hash(const struct xfrm_selector *sel,
				      unsigned short family, unsigned int hmask,
				      u8 dbits, u8 sbits)
{
	const xfrm_address_t *daddr = &sel->daddr;
	const xfrm_address_t *saddr = &sel->saddr;
	unsigned int h = 0;

	switch (family) {
	case AF_INET:
		if (sel->prefixlen_d < dbits ||
		    sel->prefixlen_s < sbits)
			return hmask + 1;

		h = __xfrm4_dpref_spref_hash(daddr, saddr, dbits, sbits);
		break;

	case AF_INET6:
		if (sel->prefixlen_d < dbits ||
		    sel->prefixlen_s < sbits)
			return hmask + 1;

		h = __xfrm6_dpref_spref_hash(daddr, saddr, dbits, sbits);
		break;
	}
	h ^= (h >> 16);
	return h & hmask;
}

static inline unsigned int __addr_hash(const xfrm_address_t *daddr,
				       const xfrm_address_t *saddr,
				       unsigned short family,
				       unsigned int hmask,
				       u8 dbits, u8 sbits)
{
	unsigned int h = 0;

	switch (family) {
	case AF_INET:
		h = __xfrm4_dpref_spref_hash(daddr, saddr, dbits, sbits);
		break;

	case AF_INET6:
		h = __xfrm6_dpref_spref_hash(daddr, saddr, dbits, sbits);
		break;
	}
	h ^= (h >> 16);
	return h & hmask;
}

struct hlist_head *xfrm_hash_alloc(unsigned int sz);
void xfrm_hash_free(struct hlist_head *n, unsigned int sz);

#endif /* _XFRM_HASH_H */
