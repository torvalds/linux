// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	6LoWPAN UDP compression according to RFC7400
 */

#include "nhc.h"

#define LOWPAN_GHC_UDP_IDLEN	1
#define LOWPAN_GHC_UDP_ID_0	0xd0
#define LOWPAN_GHC_UDP_MASK_0	0xf8

static void udp_ghid_setup(struct lowpan_nhc *nhc)
{
	nhc->id[0] = LOWPAN_GHC_UDP_ID_0;
	nhc->idmask[0] = LOWPAN_GHC_UDP_MASK_0;
}

LOWPAN_NHC(ghc_udp, "RFC7400 UDP", NEXTHDR_UDP, 0,
	   udp_ghid_setup, LOWPAN_GHC_UDP_IDLEN, NULL, NULL);

module_lowpan_nhc(ghc_udp);
MODULE_DESCRIPTION("6LoWPAN generic header UDP compression");
MODULE_LICENSE("GPL");
