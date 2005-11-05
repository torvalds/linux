/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 96, 97, 98, 99, 2001 by Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Copyright (C) 2001 Thiemo Seufer.
 * Copyright (C) 2002 Maciej W. Rozycki
 */
#ifndef _ASM_CHECKSUM_H
#define _ASM_CHECKSUM_H

#include <linux/config.h>
#include <linux/in6.h>

#include <asm/uaccess.h>

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
unsigned int csum_partial(const unsigned char *buff, int len, unsigned int sum);

/*
 * this is a new version of the above that records errors it finds in *errp,
 * but continues and zeros the rest of the buffer.
 */
unsigned int csum_partial_copy_from_user(const unsigned char __user *src,
					 unsigned char *dst, int len,
					 unsigned int sum, int *errp);

/*
 * Copy and checksum to user
 */
#define HAVE_CSUM_COPY_USER
static inline unsigned int csum_and_copy_to_user (const unsigned char *src,
						  unsigned char __user *dst,
						  int len, int sum,
						  int *err_ptr)
{
	might_sleep();
	sum = csum_partial(src, len, sum);

	if (copy_to_user(dst, src, len)) {
		*err_ptr = -EFAULT;
		return -1;
	}

	return sum;
}

/*
 * the same as csum_partial, but copies from user space (but on MIPS
 * we have just one address space, so this is identical to the above)
 */
unsigned int csum_partial_copy_nocheck(const unsigned char *src, unsigned char *dst,
				       int len, unsigned int sum);

/*
 *	Fold a partial checksum without adding pseudo headers
 */
static inline unsigned short int csum_fold(unsigned int sum)
{
	__asm__(
	"	.set	push		# csum_fold\n"
	"	.set	noat		\n"
	"	sll	$1, %0, 16	\n"
	"	addu	%0, $1		\n"
	"	sltu	$1, %0, $1	\n"
	"	srl	%0, %0, 16	\n"
	"	addu	%0, $1		\n"
	"	xori	%0, 0xffff	\n"
	"	.set	pop"
	: "=r" (sum)
	: "0" (sum));

	return sum;
}

/*
 *	This is a version of ip_compute_csum() optimized for IP headers,
 *	which always checksum on 4 octet boundaries.
 *
 *	By Jorge Cwik <jorge@laser.satlink.net>, adapted for linux by
 *	Arnt Gulbrandsen.
 */
static inline unsigned short ip_fast_csum(unsigned char *iph, unsigned int ihl)
{
	unsigned int *word = (unsigned int *) iph;
	unsigned int *stop = word + ihl;
	unsigned int csum;
	int carry;

	csum = word[0];
	csum += word[1];
	carry = (csum < word[1]);
	csum += carry;

	csum += word[2];
	carry = (csum < word[2]);
	csum += carry;

	csum += word[3];
	carry = (csum < word[3]);
	csum += carry;

	word += 4;
	do {
		csum += *word;
		carry = (csum < *word);
		csum += carry;
		word++;
	} while (word != stop);

	return csum_fold(csum);
}

static inline unsigned int csum_tcpudp_nofold(unsigned long saddr,
	unsigned long daddr, unsigned short len, unsigned short proto,
	unsigned int sum)
{
	__asm__(
	"	.set	push		# csum_tcpudp_nofold\n"
	"	.set	noat		\n"
#ifdef CONFIG_32BIT
	"	addu	%0, %2		\n"
	"	sltu	$1, %0, %2	\n"
	"	addu	%0, $1		\n"

	"	addu	%0, %3		\n"
	"	sltu	$1, %0, %3	\n"
	"	addu	%0, $1		\n"

	"	addu	%0, %4		\n"
	"	sltu	$1, %0, %4	\n"
	"	addu	%0, $1		\n"
#endif
#ifdef CONFIG_64BIT
	"	daddu	%0, %2		\n"
	"	daddu	%0, %3		\n"
	"	daddu	%0, %4		\n"
	"	dsll32	$1, %0, 0	\n"
	"	daddu	%0, $1		\n"
	"	dsra32	%0, %0, 0	\n"
#endif
	"	.set	pop"
	: "=r" (sum)
	: "0" (daddr), "r"(saddr),
#ifdef __MIPSEL__
	  "r" (((unsigned long)htons(len)<<16) + proto*256),
#else
	  "r" (((unsigned long)(proto)<<16) + len),
#endif
	  "r" (sum));

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
	return csum_fold(csum_tcpudp_nofold(saddr, daddr, len, proto, sum));
}

/*
 * this routine is used for miscellaneous IP-like checksums, mainly
 * in icmp.c
 */
static inline unsigned short ip_compute_csum(unsigned char * buff, int len)
{
	return csum_fold(csum_partial(buff, len, 0));
}

#define _HAVE_ARCH_IPV6_CSUM
static __inline__ unsigned short int csum_ipv6_magic(struct in6_addr *saddr,
						     struct in6_addr *daddr,
						     __u32 len,
						     unsigned short proto,
						     unsigned int sum)
{
	__asm__(
	"	.set	push		# csum_ipv6_magic\n"
	"	.set	noreorder	\n"
	"	.set	noat		\n"
	"	addu	%0, %5		# proto (long in network byte order)\n"
	"	sltu	$1, %0, %5	\n"
	"	addu	%0, $1		\n"

	"	addu	%0, %6		# csum\n"
	"	sltu	$1, %0, %6	\n"
	"	lw	%1, 0(%2)	# four words source address\n"
	"	addu	%0, $1		\n"
	"	addu	%0, %1		\n"
	"	sltu	$1, %0, %1	\n"

	"	lw	%1, 4(%2)	\n"
	"	addu	%0, $1		\n"
	"	addu	%0, %1		\n"
	"	sltu	$1, %0, %1	\n"

	"	lw	%1, 8(%2)	\n"
	"	addu	%0, $1		\n"
	"	addu	%0, %1		\n"
	"	sltu	$1, %0, %1	\n"

	"	lw	%1, 12(%2)	\n"
	"	addu	%0, $1		\n"
	"	addu	%0, %1		\n"
	"	sltu	$1, %0, %1	\n"

	"	lw	%1, 0(%3)	\n"
	"	addu	%0, $1		\n"
	"	addu	%0, %1		\n"
	"	sltu	$1, %0, %1	\n"

	"	lw	%1, 4(%3)	\n"
	"	addu	%0, $1		\n"
	"	addu	%0, %1		\n"
	"	sltu	$1, %0, %1	\n"

	"	lw	%1, 8(%3)	\n"
	"	addu	%0, $1		\n"
	"	addu	%0, %1		\n"
	"	sltu	$1, %0, %1	\n"

	"	lw	%1, 12(%3)	\n"
	"	addu	%0, $1		\n"
	"	addu	%0, %1		\n"
	"	sltu	$1, %0, %1	\n"

	"	addu	%0, $1		# Add final carry\n"
	"	.set	pop"
	: "=r" (sum), "=r" (proto)
	: "r" (saddr), "r" (daddr),
	  "0" (htonl(len)), "1" (htonl(proto)), "r" (sum));

	return csum_fold(sum);
}

#endif /* _ASM_CHECKSUM_H */
