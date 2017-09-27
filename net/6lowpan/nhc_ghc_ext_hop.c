/*
 *	6LoWPAN Extension Header compression according to RFC7400
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include "nhc.h"

#define LOWPAN_GHC_EXT_HOP_IDLEN	1
#define LOWPAN_GHC_EXT_HOP_ID_0		0xb0
#define LOWPAN_GHC_EXT_HOP_MASK_0	0xfe

static void hop_ghid_setup(struct lowpan_nhc *nhc)
{
	nhc->id[0] = LOWPAN_GHC_EXT_HOP_ID_0;
	nhc->idmask[0] = LOWPAN_GHC_EXT_HOP_MASK_0;
}

LOWPAN_NHC(ghc_ext_hop, "RFC7400 Hop-by-Hop Extension Header", NEXTHDR_HOP, 0,
	   hop_ghid_setup, LOWPAN_GHC_EXT_HOP_IDLEN, NULL, NULL);

module_lowpan_nhc(ghc_ext_hop);
MODULE_DESCRIPTION("6LoWPAN generic header hop-by-hop extension compression");
MODULE_LICENSE("GPL");
