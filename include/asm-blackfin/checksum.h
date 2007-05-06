#ifndef _BFIN_CHECKSUM_H
#define _BFIN_CHECKSUM_H

/*
 * MODIFIED FOR BFIN April 30, 2001 akbar.hussain@lineo.com
 *
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
unsigned int csum_partial(const unsigned char *buff, int len, unsigned int sum);

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
						unsigned char *dst, int len,
						int sum, int *csum_err);

#define csum_partial_copy_nocheck(src, dst, len, sum)	\
	csum_partial_copy((src), (dst), (len), (sum))

unsigned short ip_fast_csum(unsigned char *iph, unsigned int ihl);

/*
 *	Fold a partial checksum
 */

static inline unsigned int csum_fold(unsigned int sum)
{
	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);
	return ((~(sum << 16)) >> 16);
}

/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */

static inline unsigned int
csum_tcpudp_nofold(unsigned long saddr, unsigned long daddr, unsigned short len,
		   unsigned short proto, unsigned int sum)
{

	__asm__ ("%0 = %0 + %1;\n\t"
		 "CC = AC0;\n\t"
		 "if !CC jump 4;\n\t"
		 "%0 = %0 + %4;\n\t"
		 "%0 = %0 + %2;\n\t"
		 "CC = AC0;\n\t"
                 "if !CC jump 4;\n\t"
                 "%0 = %0 + %4;\n\t"
 		 "%0 = %0 + %3;\n\t"
		 "CC = AC0;\n\t"
                 "if !CC jump 4;\n\t"
                 "%0 = %0 + %4;\n\t"
                 "NOP;\n\t"
 		 : "=d" (sum)
		 : "d" (daddr), "d" (saddr), "d" ((ntohs(len)<<16)+proto*256), "d" (1), "0"(sum));

	return (sum);
}

static inline unsigned short int
csum_tcpudp_magic(unsigned long saddr, unsigned long daddr, unsigned short len,
		  unsigned short proto, unsigned int sum)
{
	return csum_fold(csum_tcpudp_nofold(saddr, daddr, len, proto, sum));
}

/*
 * this routine is used for miscellaneous IP-like checksums, mainly
 * in icmp.c
 */

extern unsigned short ip_compute_csum(const unsigned char *buff, int len);

#endif				/* _BFIN_CHECKSUM_H */
