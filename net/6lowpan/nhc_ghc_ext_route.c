// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	6LoWPAN Extension Header compression according to RFC7400
 */

#include "nhc.h"

#define LOWPAN_GHC_EXT_ROUTE_IDLEN	1
#define LOWPAN_GHC_EXT_ROUTE_ID_0	0xb2
#define LOWPAN_GHC_EXT_ROUTE_MASK_0	0xfe

static void route_ghid_setup(struct lowpan_nhc *nhc)
{
	nhc->id[0] = LOWPAN_GHC_EXT_ROUTE_ID_0;
	nhc->idmask[0] = LOWPAN_GHC_EXT_ROUTE_MASK_0;
}

LOWPAN_NHC(ghc_ext_route, "RFC7400 Routing Extension Header", NEXTHDR_ROUTING,
	   0, route_ghid_setup, LOWPAN_GHC_EXT_ROUTE_IDLEN, NULL, NULL);

module_lowpan_nhc(ghc_ext_route);
MODULE_DESCRIPTION("6LoWPAN generic header routing extension compression");
MODULE_LICENSE("GPL");
