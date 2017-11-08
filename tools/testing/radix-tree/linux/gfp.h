/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _GFP_H
#define _GFP_H

#include <linux/types.h>

#define __GFP_BITS_SHIFT 26
#define __GFP_BITS_MASK ((gfp_t)((1 << __GFP_BITS_SHIFT) - 1))

#define __GFP_HIGH		0x20u
#define __GFP_IO		0x40u
#define __GFP_FS		0x80u
#define __GFP_NOWARN		0x200u
#define __GFP_ATOMIC		0x80000u
#define __GFP_ACCOUNT		0x100000u
#define __GFP_DIRECT_RECLAIM	0x400000u
#define __GFP_KSWAPD_RECLAIM	0x2000000u

#define __GFP_RECLAIM	(__GFP_DIRECT_RECLAIM|__GFP_KSWAPD_RECLAIM)

#define GFP_ATOMIC	(__GFP_HIGH|__GFP_ATOMIC|__GFP_KSWAPD_RECLAIM)
#define GFP_KERNEL	(__GFP_RECLAIM | __GFP_IO | __GFP_FS)
#define GFP_NOWAIT	(__GFP_KSWAPD_RECLAIM)


static inline bool gfpflags_allow_blocking(const gfp_t gfp_flags)
{
	return !!(gfp_flags & __GFP_DIRECT_RECLAIM);
}

#endif
