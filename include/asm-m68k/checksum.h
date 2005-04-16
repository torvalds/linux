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

extern unsigned int csum_partial_copy_from_user(const unsigned char *src,
						unsigned char *dst,
						int len, int sum,
						int *csum_err);

extern unsigned int csum_partial_copy_nocheck(const unsigned char *src,
					      unsigned char *dst, int len,
					      int sum);

/*
 *	This is a version of ip_compute_csum() optimized for IP headers,
 *	which always checksum on 4 octet boundaries.
 *
 */
static inline unsigned short
ip_fast_csum(unsigned char *iph, unsigned int ihl)
{
	unsigned int sum = 0;
	unsigned long tmp;

	__asm__ ("subqw #1,%2\n"
		 "1:\t"
		 "movel %1@+,%3\n\t"
		 "addxl %3,%0\n\t"
		 "dbra  %2,1b\n\t"
		 "movel %0,%3\n\t"
		 "swap  %3\n\t"
		 "addxw %3,%0\n\t"
		 "clrw  %3\n\t"
		 "addxw %3,%0\n\t"
		 : "=d" (sum), "=&a" (iph), "=&d" (ihl), "=&d" (tmp)
		 : "0" (sum), "1" (iph), "2" (ihl)
		 : "memory");
	return ~sum;
}

/*
 *	Fold a partial checksum
 */

static inline unsigned int csum_fold(unsigned int sum)
{
	unsigned int tmp = sum;
	__asm__("swap %1\n\t"
		"addw %1, %0\n\t"
		"clrw %1\n\t"
		"addxw %1, %0"
		: "=&d" (sum), "=&d" (tmp)
		: "0" (sum), "1" (tmp));
	return ~sum;
}


static inline unsigned int
csum_tcpudp_nofold(unsigned long saddr, unsigned long daddr, unsigned short len,
		  unsigned short proto, unsigned int sum)
{
	__asm__ ("addl  %2,%0\n\t"
		 "addxl %3,%0\n\t"
		 "addxl %4,%0\n\t"
		 "clrl %1\n\t"
		 "addxl %1,%0"
		 : "=&d" (sum), "=d" (saddr)
		 : "g" (daddr), "1" (saddr), "d" (len + proto),
		   "0" (sum));
	return sum;
}


/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */
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

static inline unsigned short
ip_compute_csum(unsigned char * buff, int len)
{
	return csum_fold (csum_partial(buff, len, 0));
}

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
