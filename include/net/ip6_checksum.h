/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Checksumming functions for IPv6
 *
 * Authors:	Jorge Cwik, <jorge@laser.satlink.net>
 *		Arnt Gulbrandsen, <agulbra@nvg.unit.no>
 *		Borrows very liberally from tcp.c and ip.c, see those
 *		files for more names.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

/*
 *	Fixes:
 *
 *	Ralf Baechle			:	generic ipv6 checksum
 *	<ralf@waldorf-gmbh.de>
 */

#ifndef _CHECKSUM_IPV6_H
#define _CHECKSUM_IPV6_H

#include <asm/types.h>
#include <asm/byteorder.h>
#include <net/ip.h>
#include <asm/checksum.h>
#include <linux/in6.h>

#ifndef _HAVE_ARCH_IPV6_CSUM

static __inline__ unsigned short int csum_ipv6_magic(struct in6_addr *saddr,
						     struct in6_addr *daddr,
						     __u16 len,
						     unsigned short proto,
						     unsigned int csum) 
{

	int carry;
	__u32 ulen;
	__u32 uproto;

	csum += saddr->s6_addr32[0];
	carry = (csum < saddr->s6_addr32[0]);
	csum += carry;

	csum += saddr->s6_addr32[1];
	carry = (csum < saddr->s6_addr32[1]);
	csum += carry;

	csum += saddr->s6_addr32[2];
	carry = (csum < saddr->s6_addr32[2]);
	csum += carry;

	csum += saddr->s6_addr32[3];
	carry = (csum < saddr->s6_addr32[3]);
	csum += carry;

	csum += daddr->s6_addr32[0];
	carry = (csum < daddr->s6_addr32[0]);
	csum += carry;

	csum += daddr->s6_addr32[1];
	carry = (csum < daddr->s6_addr32[1]);
	csum += carry;

	csum += daddr->s6_addr32[2];
	carry = (csum < daddr->s6_addr32[2]);
	csum += carry;

	csum += daddr->s6_addr32[3];
	carry = (csum < daddr->s6_addr32[3]);
	csum += carry;

	ulen = htonl((__u32) len);
	csum += ulen;
	carry = (csum < ulen);
	csum += carry;

	uproto = htonl(proto);
	csum += uproto;
	carry = (csum < uproto);
	csum += carry;

	return csum_fold(csum);
}

#endif
#endif
