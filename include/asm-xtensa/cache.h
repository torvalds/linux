/*
 * include/asm-xtensa/cacheflush.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 * 2 of the License, or (at your option) any later version.
 *
 * (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_CACHE_H
#define _XTENSA_CACHE_H

#include <xtensa/config/core.h>

#if XCHAL_ICACHE_SIZE > 0
# if (XCHAL_ICACHE_SIZE % (XCHAL_ICACHE_LINESIZE*XCHAL_ICACHE_WAYS*4)) != 0
#  error cache configuration outside expected/supported range!
# endif
#endif

#if XCHAL_DCACHE_SIZE > 0
# if (XCHAL_DCACHE_SIZE % (XCHAL_DCACHE_LINESIZE*XCHAL_DCACHE_WAYS*4)) != 0
#  error cache configuration outside expected/supported range!
# endif
#endif

#define L1_CACHE_SHIFT		XCHAL_CACHE_LINEWIDTH_MAX
#define L1_CACHE_BYTES		XCHAL_CACHE_LINESIZE_MAX

#endif	/* _XTENSA_CACHE_H */
