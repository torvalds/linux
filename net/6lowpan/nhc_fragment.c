/*
 *	6LoWPAN IPv6 Fragment Header compression according to RFC6282
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include "nhc.h"

#define LOWPAN_NHC_FRAGMENT_IDLEN	1
#define LOWPAN_NHC_FRAGMENT_ID_0	0xe4
#define LOWPAN_NHC_FRAGMENT_MASK_0	0xfe

static void fragment_nhid_setup(struct lowpan_nhc *nhc)
{
	nhc->id[0] = LOWPAN_NHC_FRAGMENT_ID_0;
	nhc->idmask[0] = LOWPAN_NHC_FRAGMENT_MASK_0;
}

LOWPAN_NHC(nhc_fragment, "RFC6282 Fragment", NEXTHDR_FRAGMENT, 0,
	   fragment_nhid_setup, LOWPAN_NHC_FRAGMENT_IDLEN, NULL, NULL);

module_lowpan_nhc(nhc_fragment);
MODULE_DESCRIPTION("6LoWPAN next header RFC6282 Fragment compression");
MODULE_LICENSE("GPL");
