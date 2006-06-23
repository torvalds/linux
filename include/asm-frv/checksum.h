/* checksum.h: FRV checksumming
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_CHECKSUM_H
#define _ASM_CHECKSUM_H

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
unsigned int csum_partial(const unsigned char * buff, int len, unsigned int sum);

/*
 * the same as csum_partial, but copies from src while it
 * checksums
 *
 * here even more important to align src and dst on a 32-bit (or even
 * better 64-bit) boundary
 */
unsigned int csum_partial_copy(const char *src, char *dst, int len, int sum);

/*
 * the same as csum_partial_copy, but copies from user space.
 *
 * here even more important to align src and dst on a 32-bit (or even
 * better 64-bit) boundary
 */
extern unsigned int csum_partial_copy_from_user(const char __user *src, char *dst,
						int len, int sum, int *csum_err);

#define csum_partial_copy_nocheck(src, dst, len, sum)	\
	csum_partial_copy((src), (dst), (len), (sum))

/*
 *	This is a version of ip_compute_csum() optimized for IP headers,
 *	which always checksum on 4 octet boundaries.
 *
 */
static inline
unsigned short ip_fast_csum(unsigned char *iph, unsigned int ihl)
{
	unsigned int tmp, inc, sum = 0;

	asm("	addcc		gr0,gr0,gr0,icc0\n" /* clear icc0.C */
	    "	subi		%1,#4,%1	\n"
	    "0:					\n"
	    "	ldu.p		@(%1,%3),%4	\n"
	    "	subicc		%2,#1,%2,icc1	\n"
	    "	addxcc.p	%4,%0,%0,icc0	\n"
	    "	bhi		icc1,#2,0b	\n"

	    /* fold the 33-bit result into 16-bits */
	    "	addxcc		gr0,%0,%0,icc0	\n"
	    "	srli		%0,#16,%1	\n"
	    "	sethi		#0,%0		\n"
	    "	add		%1,%0,%0	\n"
	    "	srli		%0,#16,%1	\n"
	    "	add		%1,%0,%0	\n"

	    : "=r" (sum), "=r" (iph), "=r" (ihl), "=r" (inc), "=&r"(tmp)
	    : "0" (sum), "1" (iph), "2" (ihl), "3" (4),
	    "m"(*(volatile struct { int _[100]; } *)iph)
	    : "icc0", "icc1"
	    );

	return ~sum;
}

/*
 *	Fold a partial checksum
 */
static inline unsigned int csum_fold(unsigned int sum)
{
	unsigned int tmp;

	asm("	srli		%0,#16,%1	\n"
	    "	sethi		#0,%0		\n"
	    "	add		%1,%0,%0	\n"
	    "	srli		%0,#16,%1	\n"
	    "	add		%1,%0,%0	\n"
	    : "=r"(sum), "=&r"(tmp)
	    : "0"(sum)
	    );

	return ~sum;
}

/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */
static inline unsigned int
csum_tcpudp_nofold(unsigned long saddr, unsigned long daddr, unsigned short len,
		  unsigned short proto, unsigned int sum)
{
	asm("	addcc		%1,%0,%0,icc0	\n"
	    "	addxcc		%2,%0,%0,icc0	\n"
	    "	addxcc		%3,%0,%0,icc0	\n"
	    "	addxcc		gr0,%0,%0,icc0	\n"
	    : "=r" (sum)
	    : "r" (daddr), "r" (saddr), "r" (len + proto), "0"(sum)
	    : "icc0"
	    );
	return sum;
}

static inline unsigned short int
csum_tcpudp_magic(unsigned long saddr, unsigned long daddr, unsigned short len,
		  unsigned short proto, unsigned int sum)
{
	return csum_fold(csum_tcpudp_nofold(saddr,daddr,len,proto,sum));
}

/*
 * this routine is used for miscellaneous IP-like checksums, mainly
 * in icmp.c
 */
extern unsigned short ip_compute_csum(const unsigned char * buff, int len);

#define _HAVE_ARCH_IPV6_CSUM
static inline unsigned short int
csum_ipv6_magic(struct in6_addr *saddr, struct in6_addr *daddr,
		__u32 len, unsigned short proto, unsigned int sum)
{
	unsigned long tmp, tmp2;

	asm("	addcc		%2,%0,%0,icc0	\n"

	    /* add up the source addr */
	    "	ldi		@(%3,0),%1	\n"
	    "	addxcc		%1,%0,%0,icc0	\n"
	    "	ldi		@(%3,4),%2	\n"
	    "	addxcc		%2,%0,%0,icc0	\n"
	    "	ldi		@(%3,8),%1	\n"
	    "	addxcc		%1,%0,%0,icc0	\n"
	    "	ldi		@(%3,12),%2	\n"
	    "	addxcc		%2,%0,%0,icc0	\n"

	    /* add up the dest addr */
	    "	ldi		@(%4,0),%1	\n"
	    "	addxcc		%1,%0,%0,icc0	\n"
	    "	ldi		@(%4,4),%2	\n"
	    "	addxcc		%2,%0,%0,icc0	\n"
	    "	ldi		@(%4,8),%1	\n"
	    "	addxcc		%1,%0,%0,icc0	\n"
	    "	ldi		@(%4,12),%2	\n"
	    "	addxcc		%2,%0,%0,icc0	\n"

	    /* fold the 33-bit result into 16-bits */
	    "	addxcc		gr0,%0,%0,icc0	\n"
	    "	srli		%0,#16,%1	\n"
	    "	sethi		#0,%0		\n"
	    "	add		%1,%0,%0	\n"
	    "	srli		%0,#16,%1	\n"
	    "	add		%1,%0,%0	\n"

	    : "=r" (sum), "=&r" (tmp), "=r" (tmp2)
	    : "r" (saddr), "r" (daddr), "0" (sum), "2" (len + proto)
	    : "icc0"
	    );

	return ~sum;
}

#endif /* _ASM_CHECKSUM_H */
