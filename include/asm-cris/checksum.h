/* TODO: csum_tcpudp_magic could be speeded up, and csum_fold as well */

#ifndef _CRIS_CHECKSUM_H
#define _CRIS_CHECKSUM_H

#include <asm/arch/checksum.h>

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

unsigned int csum_partial_copy_nocheck(const char *src, char *dst,
				       int len, unsigned int sum);

/*
 *	Fold a partial checksum into a word
 */

static inline unsigned int csum_fold(unsigned int sum)
{
	/* the while loop is unnecessary really, it's always enough with two
	   iterations */
	
	while(sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16); /* add in end-around carry */
	
	return ~sum;
}

extern unsigned int csum_partial_copy_from_user(const char *src, char *dst,
						int len, unsigned int sum, 
						int *errptr);

/*
 *	This is a version of ip_compute_csum() optimized for IP headers,
 *	which always checksum on 4 octet boundaries.
 *
 */

static inline unsigned short ip_fast_csum(unsigned char * iph,
					  unsigned int ihl)
{
	return csum_fold(csum_partial(iph, ihl * 4, 0));
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

#endif
