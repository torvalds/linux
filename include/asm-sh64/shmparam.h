#ifndef __ASM_SH64_SHMPARAM_H
#define __ASM_SH64_SHMPARAM_H

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * include/asm-sh64/shmparam.h
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 *
 */

#include <asm/cache.h>

/* attach addr a multiple of this */
#define	SHMLBA	(cpu_data->dcache.sets * L1_CACHE_BYTES)

#endif /* __ASM_SH64_SHMPARAM_H */
