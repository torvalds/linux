#ifdef __KERNEL__
#ifndef _ASM_M32R_CHECKSUM_H
#define _ASM_M32R_CHECKSUM_H

/*
 * include/asm-m32r/checksum.h
 *
 * IP/TCP/UDP checksum routines
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Some code taken from mips and parisc architecture.
 *
 *    Copyright (C) 2001, 2002  Hiroyuki Kondo, Hirokazu Takata
 *    Copyright (C) 2004  Hirokazu Takata <takata at linux-m32r.org>
 */

#include <linux/in6.h>

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
asmlinkage unsigned int csum_partial(const unsigned char *buff,
				     int len, unsigned int sum);

/*
 * The same as csum_partial, but copies from src while it checksums.
 *
 * Here even more important to align src and dst on a 32-bit (or even
 * better 64-bit) boundary
 */
extern unsigned int csum_partial_copy_nocheck(const unsigned char *src,
					      unsigned char *dst,
                                              int len, unsigned int sum);

/*
 * This is a new version of the above that records errors it finds in *errp,
 * but continues and zeros thre rest of the buffer.
 */
extern unsigned int csum_partial_copy_from_user(const unsigned char __user *src,
                                                unsigned char *dst,
                                                int len, unsigned int sum,
                                                int *err_ptr);

/*
 *	Fold a partial checksum
 */

static inline unsigned int csum_fold(unsigned int sum)
{
	unsigned long tmpreg;
	__asm__(
		"	sll3	%1, %0, #16 \n"
		"	cmp	%0, %0 \n"
		"	addx	%0, %1 \n"
		"	ldi	%1, #0 \n"
		"	srli	%0, #16 \n"
		"	addx	%0, %1 \n"
		"	xor3	%0, %0, #0x0000ffff \n"
		: "=r" (sum), "=&r" (tmpreg)
		: "0"  (sum)
		: "cbit"
	);
	return sum;
}

/*
 * This is a version of ip_compute_csum() optimized for IP headers,
 * which always checksum on 4 octet boundaries.
 */
static inline unsigned short ip_fast_csum(unsigned char * iph,
					  unsigned int ihl) {
	unsigned long sum, tmpreg0, tmpreg1;

	__asm__ __volatile__(
		"	ld	%0, @%1+ \n"
		"	addi	%2, #-4 \n"
		"#	bgez	%2, 2f \n"
		"	cmp	%0, %0 \n"
		"	ld	%3, @%1+ \n"
		"	ld	%4, @%1+ \n"
		"	addx	%0, %3 \n"
		"	ld	%3, @%1+ \n"
		"	addx	%0, %4 \n"
		"	addx	%0, %3 \n"
		"	.fillinsn\n"
		"1: \n"
		"	ld	%4, @%1+ \n"
		"	addi	%2, #-1 \n"
		"	addx	%0, %4 \n"
		"	bgtz	%2, 1b \n"
		"\n"
		"	ldi	%3, #0 \n"
		"	addx	%0, %3 \n"
		"	.fillinsn\n"
		"2: \n"
	/* Since the input registers which are loaded with iph and ipl
	   are modified, we must also specify them as outputs, or gcc
	   will assume they contain their original values. */
	: "=&r" (sum), "=r" (iph), "=r" (ihl), "=&r" (tmpreg0), "=&r" (tmpreg1)
	: "1" (iph), "2" (ihl)
	: "cbit", "memory");

	return csum_fold(sum);
}

static inline unsigned long csum_tcpudp_nofold(unsigned long saddr,
					       unsigned long daddr,
					       unsigned short len,
					       unsigned short proto,
					       unsigned int sum)
{
#if defined(__LITTLE_ENDIAN)
	unsigned long len_proto = (ntohs(len)<<16)+proto*256;
#else
	unsigned long len_proto = (proto<<16)+len;
#endif
	unsigned long tmpreg;

	__asm__(
		"	cmp	%0, %0 \n"
		"	addx	%0, %2 \n"
		"	addx	%0, %3 \n"
		"	addx	%0, %4 \n"
		"	ldi	%1, #0 \n"
		"	addx	%0, %1 \n"
		: "=r" (sum), "=&r" (tmpreg)
		: "r" (daddr), "r" (saddr), "r" (len_proto), "0" (sum)
		: "cbit"
	);

	return sum;
}

/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */
static inline unsigned short int csum_tcpudp_magic(unsigned long saddr,
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

static inline unsigned short ip_compute_csum(unsigned char * buff, int len) {
	return csum_fold (csum_partial(buff, len, 0));
}

#define _HAVE_ARCH_IPV6_CSUM
static inline unsigned short int csum_ipv6_magic(struct in6_addr *saddr,
						 struct in6_addr *daddr,
						 __u16 len,
						 unsigned short proto,
						 unsigned int sum)
{
	unsigned long tmpreg0, tmpreg1, tmpreg2, tmpreg3;
	__asm__(
		"	ld	%1, @(%5) \n"
		"	ld	%2, @(4,%5) \n"
		"	ld	%3, @(8,%5) \n"
		"	ld	%4, @(12,%5) \n"
		"	add	%0, %1 \n"
		"	addx	%0, %2 \n"
		"	addx	%0, %3 \n"
		"	addx	%0, %4 \n"
		"	ld	%1, @(%6) \n"
		"	ld	%2, @(4,%6) \n"
		"	ld	%3, @(8,%6) \n"
		"	ld	%4, @(12,%6) \n"
		"	addx	%0, %1 \n"
		"	addx	%0, %2 \n"
		"	addx	%0, %3 \n"
		"	addx	%0, %4 \n"
		"	addx	%0, %7 \n"
		"	addx	%0, %8 \n"
		"	ldi	%1, #0 \n"
		"	addx	%0, %1 \n"
		: "=&r" (sum), "=&r" (tmpreg0), "=&r" (tmpreg1),
		  "=&r" (tmpreg2), "=&r" (tmpreg3)
		: "r" (saddr), "r" (daddr),
		  "r" (htonl((__u32) (len))), "r" (htonl(proto)), "0" (sum)
		: "cbit"
	);

	return csum_fold(sum);
}

#endif /* _ASM_M32R_CHECKSUM_H */
#endif /* __KERNEL__ */
