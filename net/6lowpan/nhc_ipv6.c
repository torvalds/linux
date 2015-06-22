/*
 *	6LoWPAN IPv6 Header compression according to RFC6282
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include "nhc.h"

#define LOWPAN_NHC_IPV6_IDLEN	1
#define LOWPAN_NHC_IPV6_ID_0	0xee
#define LOWPAN_NHC_IPV6_MASK_0	0xfe

static void ipv6_nhid_setup(struct lowpan_nhc *nhc)
{
	nhc->id[0] = LOWPAN_NHC_IPV6_ID_0;
	nhc->idmask[0] = LOWPAN_NHC_IPV6_MASK_0;
}

LOWPAN_NHC(nhc_ipv6, "RFC6282 IPv6", NEXTHDR_IPV6, 0, ipv6_nhid_setup,
	   LOWPAN_NHC_IPV6_IDLEN, NULL, NULL);

module_lowpan_nhc(nhc_ipv6);
MODULE_DESCRIPTION("6LoWPAN next header RFC6282 IPv6 compression");
MODULE_LICENSE("GPL");
