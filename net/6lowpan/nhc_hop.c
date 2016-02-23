/*
 *	6LoWPAN IPv6 Hop-by-Hop Options Header compression according to RFC6282
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include "nhc.h"

#define LOWPAN_NHC_HOP_IDLEN	1
#define LOWPAN_NHC_HOP_ID_0	0xe0
#define LOWPAN_NHC_HOP_MASK_0	0xfe

static void hop_nhid_setup(struct lowpan_nhc *nhc)
{
	nhc->id[0] = LOWPAN_NHC_HOP_ID_0;
	nhc->idmask[0] = LOWPAN_NHC_HOP_MASK_0;
}

LOWPAN_NHC(nhc_hop, "RFC6282 Hop-by-Hop Options", NEXTHDR_HOP, 0,
	   hop_nhid_setup, LOWPAN_NHC_HOP_IDLEN, NULL, NULL);

module_lowpan_nhc(nhc_hop);
MODULE_DESCRIPTION("6LoWPAN next header RFC6282 Hop-by-Hop Options compression");
MODULE_LICENSE("GPL");
