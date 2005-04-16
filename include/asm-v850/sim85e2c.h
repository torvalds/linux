/*
 * include/asm-v850/sim85e2c.h -- Machine-dependent defs for
 *	V850E2 RTL simulator
 *
 *  Copyright (C) 2002  NEC Corporation
 *  Copyright (C) 2002  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_SIM85E2C_H__
#define __V850_SIM85E2C_H__

/* Use generic sim85e2 settings, other than the various names.  */
#include <asm/sim85e2.h>

#define CPU_MODEL	"v850e2"
#define CPU_MODEL_LONG	"NEC V850E2"
#define PLATFORM	"sim85e2c"
#define PLATFORM_LONG	"SIM85E2C V850E2 simulator"

#endif /* __V850_SIM85E2C_H__ */
