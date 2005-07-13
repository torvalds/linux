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
extern unsigned int csum_partial (const unsigned char * buff, int len,
				  unsigned int sum);

/*
 * the same as csum_partial, but copies from src while it
 * checksums
 *
 * here even more important to align src and dst on a 32-bit (or even
 * better 64-bit) boundary
 */
extern unsigned csum_partial_copy (const unsigned char *src,
				   unsigned char *dst, int len, unsigned sum);


/*
 * the same as csum_partial_copy, but copies from user space.
 *
 * here even more important to align src and dst on a 32-bit (or even
 * better 64-bit) boundary
 */
extern unsigned csum_partial_copy_from_user (const unsigned char *src,
					     unsigned char *dst,
					     int len, unsigned sum,
					     int *csum_err);

#define csum_partial_copy_nocheck(src, dst, len, sum)	\
	csum_partial_copy ((src), (dst), (len), (sum))

unsigned short ip_fast_csum (unsigned char *iph, unsigned int ihl);

/*
 *	Fold a partial checksum
 */
static inline unsigned int csum_fold (unsigned long sum)
{
	unsigned int result;
	/*
			        %0		%1
	      hsw %1, %0	H     L		L     H
	      add %1, %0	H     L		H+L+C H+L
	*/
	asm ("hsw %1, %0; add %1, %0" : "=&r" (result) : "r" (sum));
	return (~result) >> 16;
}


/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */
static inline unsigned int
csum_tcpudp_nofold (unsigned long saddr, unsigned long daddr,
		    unsigned short len,
		    unsigned short proto, unsigned int sum)
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
		 "r" (ntohs (len) + (proto << 8)),
		 "0" (sum));
	return sum;
}

static inline unsigned short int
csum_tcpudp_magic (unsigned long saddr, unsigned long daddr,
		   unsigned short len,
		   unsigned short proto, unsigned int sum)
{
	return csum_fold (csum_tcpudp_nofold (saddr, daddr, len, proto, sum));
}

/*
 * this routine is used for miscellaneous IP-like checksums, mainly
 * in icmp.c
 */
extern unsigned short ip_compute_csum (const unsigned char * buff, int len);


#endif /* __V850_CHECKSUM_H__ */
