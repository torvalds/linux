/*
 * include/asm-v850/checksum.h -- Checksum ops
 *
 *  Copyright (C) 2001,2005  NEC Corporation
 *  Copyright (C) 2001,2005  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_CHECKSUM_H__
#define __V850_CHECKSUM_H__

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

/*
 * the same as csum_partial, but copies from src while it
 * checksums
 *
 * here even more important to align src and dst on a 32-bit (or even
 * better 64-bit) boundary
 */
extern __wsum csum_partial_copy_nocheck(const void *src,
				   void *dst, int len, __wsum sum);


/*
 * the same as csum_partial_copy, but copies from user space.
 *
 * here even more important to align src and dst on a 32-bit (or even
 * better 64-bit) boundary
 */
extern __wsum csum_partial_copy_from_user (const void *src,
					     void *dst,
					     int len, __wsum sum,
					     int *csum_err);

__sum16 ip_fast_csum(const void *iph, unsigned int ihl);

/*
 *	Fold a partial checksum
 */
static inline __sum16 csum_fold (__wsum sum)
{
	unsigned int result;
	/*
			        %0		%1
	      hsw %1, %0	H     L		L     H
	      add %1, %0	H     L		H+L+C H+L
	*/
	asm ("hsw %1, %0; add %1, %0" : "=&r" (result) : "r" (sum));
	return (__force __sum16)(~result >> 16);
}


/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */
static inline __wsum
csum_tcpudp_nofold (__be32 saddr, __be32 daddr,
		    unsigned short len,
		    unsigned short proto, __wsum sum)
{
	int __carry;
	__asm__ ("add %2, %0;"
		 "setf c, %1;"
		 "add %1, %0;"
		 "add %3, %0;"
		 "setf c, %1;"
		 "add %1, %0;"
		 "add %4, %0;"
		 "setf c, %1;"
		 "add %1, %0"
		 : "=&r" (sum), "=&r" (__carry)
		 : "r" (daddr), "r" (saddr),
		 "r" ((len + proto) << 8),
		 "0" (sum));
	return sum;
}

static inline __sum16
csum_tcpudp_magic (__be32 saddr, __be32 daddr,
		   unsigned short len,
		   unsigned short proto, __wsum sum)
{
	return csum_fold (csum_tcpudp_nofold (saddr, daddr, len, proto, sum));
}

/*
 * this routine is used for miscellaneous IP-like checksums, mainly
 * in icmp.c
 */
extern __sum16 ip_compute_csum(const void *buff, int len);


#endif /* __V850_CHECKSUM_H__ */
