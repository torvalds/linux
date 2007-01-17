/* TODO: csum_tcpudp_magic could be speeded up, and csum_fold as well */

#ifndef _CRIS_CHECKSUM_H
#define _CRIS_CHECKSUM_H

#include <asm/arch/checksum.h>

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
__wsum csum_partial(const void *buff, int len, __wsum sum);

/*
 * the same as csum_partial, but copies from src while it
 * checksums
 *
 * here even more important to align src and dst on a 32-bit (or even
 * better 64-bit) boundary
 */

__wsum csum_partial_copy_nocheck(const void *src, void *dst,
				       int len, __wsum sum);

/*
 *	Fold a partial checksum into a word
 */

static inline __sum16 csum_fold(__wsum csum)
{
	u32 sum = (__force u32)csum;
	sum = (sum & 0xffff) + (sum >> 16); /* add in end-around carry */
	sum = (sum & 0xffff) + (sum >> 16); /* add in end-around carry */
	return (__force __sum16)~sum;
}

extern __wsum csum_partial_copy_from_user(const void __user *src, void *dst,
						int len, __wsum sum,
						int *errptr);

/*
 *	This is a version of ip_compute_csum() optimized for IP headers,
 *	which always checksum on 4 octet boundaries.
 *
 */

static inline __sum16 ip_fast_csum(const void *iph, unsigned int ihl)
{
	return csum_fold(csum_partial(iph, ihl * 4, 0));
}
 
/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */

static inline __sum16 int csum_tcpudp_magic(__be32 saddr, __be32 daddr,
						   unsigned short len,
						   unsigned short proto,
						   __wsum sum)
{
	return csum_fold(csum_tcpudp_nofold(saddr,daddr,len,proto,sum));
}

/*
 * this routine is used for miscellaneous IP-like checksums, mainly
 * in icmp.c
 */

static inline __sum16 ip_compute_csum(const void *buff, int len)
{
	return csum_fold (csum_partial(buff, len, 0));
}

#endif
