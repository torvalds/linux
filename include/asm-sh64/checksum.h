#ifndef __ASM_SH64_CHECKSUM_H
#define __ASM_SH64_CHECKSUM_H

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * include/asm-sh64/checksum.h
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 *
 */

#include <asm/registers.h>

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
asmlinkage __wsum csum_partial(const void *buff, int len, __wsum sum);

/*
 *	Note: when you get a NULL pointer exception here this means someone
 *	passed in an incorrect kernel address to one of these functions.
 *
 *	If you use these functions directly please don't forget the
 *	access_ok().
 */


__wsum csum_partial_copy_nocheck(const void *src, void *dst, int len,
				       __wsum sum);

__wsum csum_partial_copy_from_user(const void __user *src, void *dst,
					 int len, __wsum sum, int *err_ptr);

static inline __sum16 csum_fold(__wsum csum)
{
	u32 sum = (__force u32)csum;
        sum = (sum & 0xffff) + (sum >> 16);
        sum = (sum & 0xffff) + (sum >> 16);
        return (__force __sum16)~sum;
}

__sum16 ip_fast_csum(const void *iph, unsigned int ihl);

__wsum csum_tcpudp_nofold(__be32 saddr, __be32 daddr,
				 unsigned short len, unsigned short proto,
				 __wsum sum);

/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */
static inline __sum16 csum_tcpudp_magic(__be32 saddr, __be32 daddr,
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
	return csum_fold(csum_partial(buff, len, 0));
}

#endif /* __ASM_SH64_CHECKSUM_H */

