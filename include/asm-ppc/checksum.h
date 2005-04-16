#ifdef __KERNEL__
#ifndef _PPC_CHECKSUM_H
#define _PPC_CHECKSUM_H


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
extern unsigned int csum_partial(const unsigned char * buff, int len,
				 unsigned int sum);

/*
 * Computes the checksum of a memory block at src, length len,
 * and adds in "sum" (32-bit), while copying the block to dst.
 * If an access exception occurs on src or dst, it stores -EFAULT
 * to *src_err or *dst_err respectively (if that pointer is not
 * NULL), and, for an error on src, zeroes the rest of dst.
 *
 * Like csum_partial, this must be called with even lengths,
 * except for the last fragment.
 */
extern unsigned int csum_partial_copy_generic(const char *src, char *dst,
					      int len, unsigned int sum,
					      int *src_err, int *dst_err);

#define csum_partial_copy_from_user(src, dst, len, sum, errp)	\
	csum_partial_copy_generic((__force void *)(src), (dst), (len), (sum), (errp), NULL)

/* FIXME: this needs to be written to really do no check -- Cort */
#define csum_partial_copy_nocheck(src, dst, len, sum)	\
	csum_partial_copy_generic((src), (dst), (len), (sum), NULL, NULL)

/*
 * turns a 32-bit partial checksum (e.g. from csum_partial) into a
 * 1's complement 16-bit checksum.
 */
static inline unsigned int csum_fold(unsigned int sum)
{
	unsigned int tmp;

	/* swap the two 16-bit halves of sum */
	__asm__("rlwinm %0,%1,16,0,31" : "=r" (tmp) : "r" (sum));
	/* if there is a carry from adding the two 16-bit halves,
	   it will carry from the lower half into the upper half,
	   giving us the correct sum in the upper half. */
	sum = ~(sum + tmp) >> 16;
	return sum;
}

/*
 * this routine is used for miscellaneous IP-like checksums, mainly
 * in icmp.c
 */
static inline unsigned short ip_compute_csum(unsigned char * buff, int len)
{
	return csum_fold(csum_partial(buff, len, 0));
}

/*
 * FIXME: I swiped this one from the sparc and made minor modifications.
 * It may not be correct.  -- Cort
 */
static inline unsigned long csum_tcpudp_nofold(unsigned long saddr,
						   unsigned long daddr,
						   unsigned short len,
						   unsigned short proto,
						   unsigned int sum)
{
    __asm__("\n\
	addc %0,%0,%1 \n\
	adde %0,%0,%2 \n\
	adde %0,%0,%3 \n\
	addze %0,%0 \n\
	"
	: "=r" (sum)
	: "r" (daddr), "r"(saddr), "r"((proto<<16)+len), "0"(sum));
    return sum;
}

/*
 * This is a version of ip_compute_csum() optimized for IP headers,
 * which always checksum on 4 octet boundaries.  ihl is the number
 * of 32-bit words and is always >= 5.
 */
extern unsigned short ip_fast_csum(unsigned char * iph, unsigned int ihl);

/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */
extern unsigned short csum_tcpudp_magic(unsigned long saddr,
					unsigned long daddr,
					unsigned short len,
					unsigned short proto,
					unsigned int sum);

#endif
#endif /* __KERNEL__ */
