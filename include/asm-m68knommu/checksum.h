#ifndef _M68K_CHECKSUM_H
#define _M68K_CHECKSUM_H

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

unsigned int csum_partial_copy(const unsigned char *src, unsigned char *dst,
	int len, int sum);


/*
 * the same as csum_partial_copy, but copies from user space.
 *
 * here even more important to align src and dst on a 32-bit (or even
 * better 64-bit) boundary
 */

extern unsigned int csum_partial_copy_from_user(const unsigned char *src,
	unsigned char *dst, int len, int sum, int *csum_err);

#define csum_partial_copy_nocheck(src, dst, len, sum)	\
	csum_partial_copy((src), (dst), (len), (sum))

unsigned short ip_fast_csum(unsigned char *iph, unsigned int ihl);

/*
 *	Fold a partial checksum
 */

static inline unsigned int csum_fold(unsigned int sum)
{
#ifdef CONFIG_COLDFIRE
	sum = (sum & 0xffff) + (sum >> 16);
	sum = (sum & 0xffff) + (sum >> 16);
#else
	unsigned int tmp = sum;
	__asm__("swap %1\n\t"
		"addw %1, %0\n\t"
		"clrw %1\n\t"
		"addxw %1, %0"
		: "=&d" (sum), "=&d" (tmp)
		: "0" (sum), "1" (sum));
#endif
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
	__asm__ ("addl  %1,%0\n\t"
		 "addxl %4,%0\n\t"
		 "addxl %5,%0\n\t"
		 "clrl %1\n\t"
		 "addxl %1,%0"
		 : "=&d" (sum), "=&d" (saddr)
		 : "0" (daddr), "1" (saddr), "d" (len + proto),
		   "d"(sum));
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
static __inline__ unsigned short int
csum_ipv6_magic(struct in6_addr *saddr, struct in6_addr *daddr,
		__u32 len, unsigned short proto, unsigned int sum) 
{
	register unsigned long tmp;
	__asm__("addl %2@,%0\n\t"
		"movel %2@(4),%1\n\t"
		"addxl %1,%0\n\t"
		"movel %2@(8),%1\n\t"
		"addxl %1,%0\n\t"
		"movel %2@(12),%1\n\t"
		"addxl %1,%0\n\t"
		"movel %3@,%1\n\t"
		"addxl %1,%0\n\t"
		"movel %3@(4),%1\n\t"
		"addxl %1,%0\n\t"
		"movel %3@(8),%1\n\t"
		"addxl %1,%0\n\t"
		"movel %3@(12),%1\n\t"
		"addxl %1,%0\n\t"
		"addxl %4,%0\n\t"
		"clrl %1\n\t"
		"addxl %1,%0"
		: "=&d" (sum), "=&d" (tmp)
		: "a" (saddr), "a" (daddr), "d" (len + proto),
		  "0" (sum));

	return csum_fold(sum);
}

#endif /* _M68K_CHECKSUM_H */
