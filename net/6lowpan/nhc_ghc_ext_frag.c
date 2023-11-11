// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	6LoWPAN Extension Header compression according to RFC7400
 */

#include "nhc.h"

#define LOWPAN_GHC_EXT_FRAG_ID_0	0xb4
#define LOWPAN_GHC_EXT_FRAG_MASK_0	0xfe

LOWPAN_NHC(ghc_ext_frag, "RFC7400 Fragmentation Extension Header",
	   NEXTHDR_FRAGMENT, 0, LOWPAN_GHC_EXT_FRAG_ID_0,
	   LOWPAN_GHC_EXT_FRAG_MASK_0, NULL, NULL);

module_lowpan_nhc(ghc_ext_frag);
MODULE_DESCRIPTION("6LoWPAN generic header fragmentation extension compression");
MODULE_LICENSE("GPL");
