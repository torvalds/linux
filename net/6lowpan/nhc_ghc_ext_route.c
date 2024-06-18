// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	6LoWPAN Extension Header compression according to RFC7400
 */

#include "nhc.h"

#define LOWPAN_GHC_EXT_ROUTE_ID_0	0xb2
#define LOWPAN_GHC_EXT_ROUTE_MASK_0	0xfe

LOWPAN_NHC(ghc_ext_route, "RFC7400 Routing Extension Header", NEXTHDR_ROUTING,
	   0, LOWPAN_GHC_EXT_ROUTE_ID_0, LOWPAN_GHC_EXT_ROUTE_MASK_0, NULL, NULL);

module_lowpan_nhc(ghc_ext_route);
MODULE_DESCRIPTION("6LoWPAN generic header routing extension compression");
MODULE_LICENSE("GPL");
