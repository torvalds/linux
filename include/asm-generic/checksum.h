/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_GENERIC_CHECKSUM_H
#define __ASM_GENERIC_CHECKSUM_H

#include <linux/bitops.h>

/*
 * computes the checksum of a memory block at buff, length len,
 * and adds in "sum" (32-bit)
 *
 * returns a 32-bit number suitable for feeding into itself
 * or csum_tcpudp_magic
 *
 * this function must be called with even lengths, except
 * for the last fragment, which may be odd
 *
 * it's best to have buff aligned on a 32-bit boundary
 */
extern __wsum csum_partial(const void *buff, int len, __wsum sum);

#ifndef ip_fast_csum
/*
 * This is a version of ip_compute_csum() optimized for IP headers,
 * which always checksum on 4 octet boundaries.
 */
extern __sum16 ip_fast_csum(const void *iph, unsigned int ihl);
#endif

#ifndef csum_fold
/*
 * Fold a partial checksum
 */
static inline __sum16 csum_fold(__wsum csum)
{
	u32 sum = (__force u32)csum;
	return (__force __sum16)((~sum - ror32(sum, 16)) >> 16);
}
#endif

#ifndef csum_tcpudp_nofold
/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */
extern __wsum
csum_tcpudp_nofold(__be32 saddr, __be32 daddr, __u32 len,
		   __u8 proto, __wsum sum);
#endif

#ifndef csum_tcpudp_magic
static inline __sum16
csum_tcpudp_magic(__be32 saddr, __be32 daddr, __u32 len,
		  __u8 proto, __wsum sum)
{
	return csum_fold(csum_tcpudp_nofold(saddr, daddr, len, proto, sum));
}
#endif

/*
 * this routine is used for miscellaneous IP-like checksums, mainly
 * in icmp.c
 */
extern __sum16 ip_compute_csum(const void *buff, int len);

#endif /* __ASM_GENERIC_CHECKSUM_H */
