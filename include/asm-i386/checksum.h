#ifndef _I386_CHECKSUM_H
#define _I386_CHECKSUM_H

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
asmlinkage unsigned int csum_partial(const unsigned char * buff, int len, unsigned int sum);

/*
 * the same as csum_partial, but copies from src while it
 * checksums, and handles user-space pointer exceptions correctly, when needed.
 *
 * here even more important to align src and dst on a 32-bit (or even
 * better 64-bit) boundary
 */

asmlinkage unsigned int csum_partial_copy_generic(const unsigned char *src, unsigned char *dst,
						  int len, int sum, int *src_err_ptr, int *dst_err_ptr);

/*
 *	Note: when you get a NULL pointer exception here this means someone
 *	passed in an incorrect kernel address to one of these functions.
 *
 *	If you use these functions directly please don't forget the
 *	verify_area().
 */
static __inline__
unsigned int csum_partial_copy_nocheck (const unsigned char *src, unsigned char *dst,
					int len, int sum)
{
	return csum_partial_copy_generic ( src, dst, len, sum, NULL, NULL);
}

static __inline__
unsigned int csum_partial_copy_from_user(const unsigned char __user *src, unsigned char *dst,
						int len, int sum, int *err_ptr)
{
	might_sleep();
	return csum_partial_copy_generic((__force unsigned char *)src, dst,
					len, sum, err_ptr, NULL);
}

/*
 *	This is a version of ip_compute_csum() optimized for IP headers,
 *	which always checksum on 4 octet boundaries.
 *
 *	By Jorge Cwik <jorge@laser.satlink.net>, adapted for linux by
 *	Arnt Gulbrandsen.
 */
static inline unsigned short ip_fast_csum(unsigned char * iph,
					  unsigned int ihl)
{
	unsigned int sum;

	__asm__ __volatile__(
	    "movl (%1), %0	;\n"
	    "subl $4, %2	;\n"
	    "jbe 2f		;\n"
	    "addl 4(%1), %0	;\n"
	    "adcl 8(%1), %0	;\n"
	    "adcl 12(%1), %0	;\n"
"1:	    adcl 16(%1), %0	;\n"
	    "lea 4(%1), %1	;\n"
	    "decl %2		;\n"
	    "jne 1b		;\n"
	    "adcl $0, %0	;\n"
	    "movl %0, %2	;\n"
	    "shrl $16, %0	;\n"
	    "addw %w2, %w0	;\n"
	    "adcl $0, %0	;\n"
	    "notl %0		;\n"
"2:				;\n"
	/* Since the input registers which are loaded with iph and ipl
	   are modified, we must also specify them as outputs, or gcc
	   will assume they contain their original values. */
	: "=r" (sum), "=r" (iph), "=r" (ihl)
	: "1" (iph), "2" (ihl)
	: "memory");
	return(sum);
}

/*
 *	Fold a partial checksum
 */

static inline unsigned int csum_fold(unsigned int sum)
{
	__asm__(
		"addl %1, %0		;\n"
		"adcl $0xffff, %0	;\n"
		: "=r" (sum)
		: "r" (sum << 16), "0" (sum & 0xffff0000)
	);
	return (~sum) >> 16;
}

static inline unsigned long csum_tcpudp_nofold(unsigned long saddr,
						   unsigned long daddr,
						   unsigned short len,
						   unsigned short proto,
						   unsigned int sum)
{
    __asm__(
	"addl %1, %0	;\n"
	"adcl %2, %0	;\n"
	"adcl %3, %0	;\n"
	"adcl $0, %0	;\n"
	: "=r" (sum)
	: "g" (daddr), "g"(saddr), "g"((ntohs(len)<<16)+proto*256), "0"(sum));
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

static inline unsigned short ip_compute_csum(unsigned char * buff, int len)
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
	__asm__(
		"addl 0(%1), %0		;\n"
		"adcl 4(%1), %0		;\n"
		"adcl 8(%1), %0		;\n"
		"adcl 12(%1), %0	;\n"
		"adcl 0(%2), %0		;\n"
		"adcl 4(%2), %0		;\n"
		"adcl 8(%2), %0		;\n"
		"adcl 12(%2), %0	;\n"
		"adcl %3, %0		;\n"
		"adcl %4, %0		;\n"
		"adcl $0, %0		;\n"
		: "=&r" (sum)
		: "r" (saddr), "r" (daddr),
		  "r"(htonl(len)), "r"(htonl(proto)), "0"(sum));

	return csum_fold(sum);
}

/*
 *	Copy and checksum to user
 */
#define HAVE_CSUM_COPY_USER
static __inline__ unsigned int csum_and_copy_to_user(const unsigned char *src,
						     unsigned char __user *dst,
						     int len, int sum, 
						     int *err_ptr)
{
	might_sleep();
	if (access_ok(VERIFY_WRITE, dst, len))
		return csum_partial_copy_generic(src, (__force unsigned char *)dst, len, sum, NULL, err_ptr);

	if (len)
		*err_ptr = -EFAULT;

	return -1; /* invalid checksum */
}

#endif
