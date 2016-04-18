/*
 *	6LoWPAN ICMPv6 compression according to RFC7400
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include "nhc.h"

#define LOWPAN_GHC_ICMPV6_IDLEN		1
#define LOWPAN_GHC_ICMPV6_ID_0		0xdf
#define LOWPAN_GHC_ICMPV6_MASK_0	0xff

static void icmpv6_ghid_setup(struct lowpan_nhc *nhc)
{
	nhc->id[0] = LOWPAN_GHC_ICMPV6_ID_0;
	nhc->idmask[0] = LOWPAN_GHC_ICMPV6_MASK_0;
}

LOWPAN_NHC(ghc_icmpv6, "RFC7400 ICMPv6", NEXTHDR_ICMP, 0,
	   icmpv6_ghid_setup, LOWPAN_GHC_ICMPV6_IDLEN, NULL, NULL);

module_lowpan_nhc(ghc_icmpv6);
MODULE_DESCRIPTION("6LoWPAN generic header ICMPv6 compression");
MODULE_LICENSE("GPL");
