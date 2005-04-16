/* $Id: checksum.h,v 1.19 2002/02/09 19:49:31 davem Exp $ */
#ifndef __SPARC64_CHECKSUM_H
#define __SPARC64_CHECKSUM_H

/*  checksum.h:  IP/UDP/TCP checksum routines on the V9.
 *
 *  Copyright(C) 1995 Linus Torvalds
 *  Copyright(C) 1995 Miguel de Icaza
 *  Copyright(C) 1996 David S. Miller
 *  Copyright(C) 1996 Eddie C. Dost
 *  Copyright(C) 1997 Jakub Jelinek
 *
 * derived from:
 *	Alpha checksum c-code
 *      ix86 inline assembly
 *      RFC1071 Computing the Internet Checksum
 */

#include <linux/in6.h>
#include <asm/uaccess.h>

/* computes the checksum of a memory block at buff, length len,
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
extern unsigned int csum_partial(const unsigned char * buff, int len, unsigned int sum);

/* the same as csum_partial, but copies from user space while it
 * checksums
 *
 * here even more important to align src and dst on a 32-bit (or even
 * better 64-bit) boundary
 */
extern unsigned int csum_partial_copy_nocheck(const unsigned char *src,
					      unsigned char *dst,
					      int len, unsigned int sum);

extern long __csum_partial_copy_from_user(const unsigned char __user *src,
					  unsigned char *dst, int len,
					  unsigned int sum);

static inline unsigned int
csum_partial_copy_from_user(const unsigned char __user *src,
			    unsigned char *dst, int len,
			    unsigned int sum, int *err)
{
	long ret = __csum_partial_copy_from_user(src, dst, len, sum);
	if (ret < 0)
		*err = -EFAULT;
	return (unsigned int) ret;
}

/* 
 *	Copy and checksum to user
 */
#define HAVE_CSUM_COPY_USER
extern long __csum_partial_copy_to_user(const unsigned char *src,
					unsigned char __user *dst, int len,
					  unsigned int sum);

static inline unsigned int
csum_and_copy_to_user(const unsigned char *src,
		      unsigned char __user *dst, int len,
		      unsigned int sum, int *err)
{
	long ret = __csum_partial_copy_to_user(src, dst, len, sum);
	if (ret < 0)
		*err = -EFAULT;
	return (unsigned int) ret;
}

/* ihl is always 5 or greater, almost always is 5, and iph is word aligned
 * the majority of the time.
 */
extern unsigned short ip_fast_csum(__const__ unsigned char *iph,
				   unsigned int ihl);

/* Fold a partial checksum without adding pseudo headers. */
static inline unsigned short csum_fold(unsigned int sum)
{
	unsigned int tmp;

	__asm__ __volatile__(
"	addcc		%0, %1, %1\n"
"	srl		%1, 16, %1\n"
"	addc		%1, %%g0, %1\n"
"	xnor		%%g0, %1, %0\n"
	: "=&r" (sum), "=r" (tmp)
	: "0" (sum), "1" (sum<<16)
	: "cc");
	return (sum & 0xffff);
}

static inline unsigned long csum_tcpudp_nofold(unsigned long saddr,
					       unsigned long daddr,
					       unsigned int len,
					       unsigned short proto,
					       unsigned int sum)
{
	__asm__ __volatile__(
"	addcc		%1, %0, %0\n"
"	addccc		%2, %0, %0\n"
"	addccc		%3, %0, %0\n"
"	addc		%0, %%g0, %0\n"
	: "=r" (sum), "=r" (saddr)
	: "r" (daddr), "r" ((proto<<16)+len), "0" (sum), "1" (saddr)
	: "cc");
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

#define _HAVE_ARCH_IPV6_CSUM

static inline unsigned short int csum_ipv6_magic(struct in6_addr *saddr,
						 struct in6_addr *daddr,
						 __u32 len,
						 unsigned short proto,
						 unsigned int sum) 
{
	__asm__ __volatile__ (
"	addcc		%3, %4, %%g7\n"
"	addccc		%5, %%g7, %%g7\n"
"	lduw		[%2 + 0x0c], %%g2\n"
"	lduw		[%2 + 0x08], %%g3\n"
"	addccc		%%g2, %%g7, %%g7\n"
"	lduw		[%2 + 0x04], %%g2\n"
"	addccc		%%g3, %%g7, %%g7\n"
"	lduw		[%2 + 0x00], %%g3\n"
"	addccc		%%g2, %%g7, %%g7\n"
"	lduw		[%1 + 0x0c], %%g2\n"
"	addccc		%%g3, %%g7, %%g7\n"
"	lduw		[%1 + 0x08], %%g3\n"
"	addccc		%%g2, %%g7, %%g7\n"
"	lduw		[%1 + 0x04], %%g2\n"
"	addccc		%%g3, %%g7, %%g7\n"
"	lduw		[%1 + 0x00], %%g3\n"
"	addccc		%%g2, %%g7, %%g7\n"
"	addccc		%%g3, %%g7, %0\n"
"	addc		0, %0, %0\n"
	: "=&r" (sum)
	: "r" (saddr), "r" (daddr), "r"(htonl(len)),
	  "r"(htonl(proto)), "r"(sum)
	: "g2", "g3", "g7", "cc");

	return csum_fold(sum);
}

/* this routine is used for miscellaneous IP-like checksums, mainly in icmp.c */
static inline unsigned short ip_compute_csum(unsigned char * buff, int len)
{
	return csum_fold(csum_partial(buff, len, 0));
}

#endif /* !(__SPARC64_CHECKSUM_H) */
