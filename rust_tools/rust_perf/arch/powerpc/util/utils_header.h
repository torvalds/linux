/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_UTIL_HEADER_H
#define __PERF_UTIL_HEADER_H

#include <linux/stringify.h>

#define mfspr(rn)       ({unsigned long rval; \
			asm volatile("mfspr %0," __stringify(rn) \
				: "=r" (rval)); rval; })

#define SPRN_PVR        0x11F   /* Processor Version Register */
#define PVR_VER(pvr)    (((pvr) >>  16) & 0xFFFF) /* Version field */
#define PVR_REV(pvr)    (((pvr) >>   0) & 0xFFFF) /* Revision field */

#endif /* __PERF_UTIL_HEADER_H */
