// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	6LoWPAN IPv6 Fragment Header compression according to RFC6282
 */

#include "nhc.h"

#define LOWPAN_NHC_FRAGMENT_ID_0	0xe4
#define LOWPAN_NHC_FRAGMENT_MASK_0	0xfe

LOWPAN_NHC(nhc_fragment, "RFC6282 Fragment", NEXTHDR_FRAGMENT, 0,
	   LOWPAN_NHC_FRAGMENT_ID_0, LOWPAN_NHC_FRAGMENT_MASK_0, NULL, NULL);

module_lowpan_nhc(nhc_fragment);
MODULE_DESCRIPTION("6LoWPAN next header RFC6282 Fragment compression");
MODULE_LICENSE("GPL");
