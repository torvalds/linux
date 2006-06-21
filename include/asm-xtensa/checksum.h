/*
 * include/asm-xtensa/checksum.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_CHECKSUM_H
#define _XTENSA_CHECKSUM_H

#include <linux/in6.h>
#include <xtensa/config/core.h>

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
asmlinkage unsigned int csum_partial(const unsigned char * buff, int len, unsigned int sum);

/*
 * the same as csum_partial, but copies from src while it
 * checksums, and handles user-space pointer exceptions correctly, when needed.
 *
 * here even more important to align src and dst on a 32-bit (or even
 * better 64-bit) boundary
 */

asmlinkage unsigned int csum_partial_copy_generic( const char *src, char *dst, int len, int sum,
						   int *src_err_ptr, int *dst_err_ptr);

/*
 *	Note: when you get a NULL pointer exception here this means someone
 *	passed in an incorrect kernel address to one of these functions.
 *
 *	If you use these functions directly please don't forget the
 *	verify_area().
 */
static inline
unsigned int csum_partial_copy_nocheck ( const char *src, char *dst,
					int len, int sum)
{
	return csum_partial_copy_generic ( src, dst, len, sum, NULL, NULL);
}

static inline
unsigned int csum_partial_copy_from_user ( const char *src, char *dst,
						int len, int sum, int *err_ptr)
{
	return csum_partial_copy_generic ( src, dst, len, sum, err_ptr, NULL);
}

/*
 * These are the old (and unsafe) way of doing checksums, a warning message will be
 * printed if they are used and an exeption occurs.
 *
 * these functions should go away after some time.
 */

#define csum_partial_copy_fromuser csum_partial_copy
unsigned int csum_partial_copy( const char *src, char *dst, int len, int sum);

/*
 *	Fold a partial checksum
 */

static __inline__ unsigned int csum_fold(unsigned int sum)
{
	unsigned int __dummy;
	__asm__("extui	%1, %0, 16, 16\n\t"
		"extui	%0 ,%0, 0, 16\n\t"
		"add	%0, %0, %1\n\t"
		"slli	%1, %0, 16\n\t"
		"add	%0, %0, %1\n\t"
		"extui	%0, %0, 16, 16\n\t"
		"neg	%0, %0\n\t"
		"addi	%0, %0, -1\n\t"
		"extui	%0, %0, 0, 16\n\t"
		: "=r" (sum), "=&r" (__dummy)
		: "0" (sum));
	return sum;
}

/*
 *	This is a version of ip_compute_csum() optimized for IP headers,
 *	which always checksum on 4 octet boundaries.
 */
static __inline__ unsigned short ip_fast_csum(unsigned char * iph, unsigned int ihl)
{
	unsigned int sum, tmp, endaddr;

	__asm__ __volatile__(
		"sub		%0, %0, %0\n\t"
#if XCHAL_HAVE_LOOPS
		"loopgtz	%2, 2f\n\t"
#else
		"beqz		%2, 2f\n\t"
		"slli		%4, %2, 2\n\t"
		"add		%4, %4, %1\n\t"
		"0:\t"
#endif
		"l32i		%3, %1, 0\n\t"
		"add		%0, %0, %3\n\t"
		"bgeu		%0, %3, 1f\n\t"
		"addi		%0, %0, 1\n\t"
		"1:\t"
		"addi		%1, %1, 4\n\t"
#if !XCHAL_HAVE_LOOPS
		"blt		%1, %4, 0b\n\t"
#endif
		"2:\t"
	/* Since the input registers which are loaded with iph and ihl
	   are modified, we must also specify them as outputs, or gcc
	   will assume they contain their original values. */
		: "=r" (sum), "=r" (iph), "=r" (ihl), "=&r" (tmp), "=&r" (endaddr)
		: "1" (iph), "2" (ihl));

	return	csum_fold(sum);
}

static __inline__ unsigned long csum_tcpudp_nofold(unsigned long saddr,
						   unsigned long daddr,
						   unsigned short len,
						   unsigned short proto,
						   unsigned int sum)
{

#ifdef __XTENSA_EL__
	unsigned long len_proto = (ntohs(len)<<16)+proto*256;
#elif defined(__XTENSA_EB__)
	unsigned long len_proto = (proto<<16)+len;
#else
# error processor byte order undefined!
#endif
	__asm__("add	%0, %0, %1\n\t"
		"bgeu	%0, %1, 1f\n\t"
		"addi	%0, %0, 1\n\t"
		"1:\t"
		"add	%0, %0, %2\n\t"
		"bgeu	%0, %2, 1f\n\t"
		"addi	%0, %0, 1\n\t"
		"1:\t"
		"add	%0, %0, %3\n\t"
		"bgeu	%0, %3, 1f\n\t"
		"addi	%0, %0, 1\n\t"
		"1:\t"
		: "=r" (sum), "=r" (len_proto)
		: "r" (daddr), "r" (saddr), "1" (len_proto), "0" (sum));
	return sum;
}

/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */
static __inline__ unsigned short int csum_tcpudp_magic(unsigned long saddr,
						       unsigned long daddr,
						       unsigned short len,
						       unsigned short proto,
						       unsigned int sum)
{
	return csum_fold(csum_tcpudp_nofold(saddr,daddr,len,proto,sum));
}

/*
 * this routine is used for miscellaneous IP-like checksums, mainly
 * in icmp.c
 */

static __inline__ unsigned short ip_compute_csum(unsigned char * buff, int len)
{
    return csum_fold (csum_partial(buff, len, 0));
}

#define _HAVE_ARCH_IPV6_CSUM
static __inline__ unsigned short int csum_ipv6_magic(struct in6_addr *saddr,
						     struct in6_addr *daddr,
						     __u32 len,
						     unsigned short proto,
						     unsigned int sum)
{
	unsigned int __dummy;
	__asm__("l32i	%1, %2, 0\n\t"
		"add	%0, %0, %1\n\t"
		"bgeu	%0, %1, 1f\n\t"
		"addi	%0, %0, 1\n\t"
		"1:\t"
		"l32i	%1, %2, 4\n\t"
		"add	%0, %0, %1\n\t"
		"bgeu	%0, %1, 1f\n\t"
		"addi	%0, %0, 1\n\t"
		"1:\t"
		"l32i	%1, %2, 8\n\t"
		"add	%0, %0, %1\n\t"
		"bgeu	%0, %1, 1f\n\t"
		"addi	%0, %0, 1\n\t"
		"1:\t"
		"l32i	%1, %2, 12\n\t"
		"add	%0, %0, %1\n\t"
		"bgeu	%0, %1, 1f\n\t"
		"addi	%0, %0, 1\n\t"
		"1:\t"
		"l32i	%1, %3, 0\n\t"
		"add	%0, %0, %1\n\t"
		"bgeu	%0, %1, 1f\n\t"
		"addi	%0, %0, 1\n\t"
		"1:\t"
		"l32i	%1, %3, 4\n\t"
		"add	%0, %0, %1\n\t"
		"bgeu	%0, %1, 1f\n\t"
		"addi	%0, %0, 1\n\t"
		"1:\t"
		"l32i	%1, %3, 8\n\t"
		"add	%0, %0, %1\n\t"
		"bgeu	%0, %1, 1f\n\t"
		"addi	%0, %0, 1\n\t"
		"1:\t"
		"l32i	%1, %3, 12\n\t"
		"add	%0, %0, %1\n\t"
		"bgeu	%0, %1, 1f\n\t"
		"addi	%0, %0, 1\n\t"
		"1:\t"
		"add	%0, %0, %4\n\t"
		"bgeu	%0, %4, 1f\n\t"
		"addi	%0, %0, 1\n\t"
		"1:\t"
		"add	%0, %0, %5\n\t"
		"bgeu	%0, %5, 1f\n\t"
		"addi	%0, %0, 1\n\t"
		"1:\t"
		: "=r" (sum), "=&r" (__dummy)
		: "r" (saddr), "r" (daddr),
		  "r" (htonl(len)), "r" (htonl(proto)), "0" (sum));

	return csum_fold(sum);
}

/*
 *	Copy and checksum to user
 */
#define HAVE_CSUM_COPY_USER
static __inline__ unsigned int csum_and_copy_to_user (const char *src, char *dst,
				    int len, int sum, int *err_ptr)
{
	if (access_ok(VERIFY_WRITE, dst, len))
		return csum_partial_copy_generic(src, dst, len, sum, NULL, err_ptr);

	if (len)
		*err_ptr = -EFAULT;

	return -1; /* invalid checksum */
}
#endif
