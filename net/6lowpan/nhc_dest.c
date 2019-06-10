// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	6LoWPAN IPv6 Destination Options Header compression according to
 *	RFC6282
 */

#include "nhc.h"

#define LOWPAN_NHC_DEST_IDLEN	1
#define LOWPAN_NHC_DEST_ID_0	0xe6
#define LOWPAN_NHC_DEST_MASK_0	0xfe

static void dest_nhid_setup(struct lowpan_nhc *nhc)
{
	nhc->id[0] = LOWPAN_NHC_DEST_ID_0;
	nhc->idmask[0] = LOWPAN_NHC_DEST_MASK_0;
}

LOWPAN_NHC(nhc_dest, "RFC6282 Destination Options", NEXTHDR_DEST, 0,
	   dest_nhid_setup, LOWPAN_NHC_DEST_IDLEN, NULL, NULL);

module_lowpan_nhc(nhc_dest);
MODULE_DESCRIPTION("6LoWPAN next header RFC6282 Destination Options compression");
MODULE_LICENSE("GPL");
