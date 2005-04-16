/*
 * include/asm-v850/sim85e2s.h -- Machine-dependent defs for
 *	V850E2 RTL simulator
 *
 *  Copyright (C) 2003  NEC Electronics Corporation
 *  Copyright (C) 2003  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_SIM85E2S_H__
#define __V850_SIM85E2S_H__

#include <asm/sim85e2.h>	/* Use generic sim85e2 settings.  */
#if 0
#include <asm/v850e2_cache.h>	/* + cache */
#endif

#define CPU_MODEL	"v850e2"
#define CPU_MODEL_LONG	"NEC V850E2"
#define PLATFORM	"sim85e2s"
#define PLATFORM_LONG	"SIM85E2S V850E2 simulator"

#endif /* __V850_SIM85E2S_H__ */
