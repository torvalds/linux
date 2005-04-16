/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1997, 1999 by Ralf Baechle
 * Copyright (c) 1999 Silicon Graphics, Inc.
 */
#ifndef _ASM_BCACHE_H
#define _ASM_BCACHE_H

#include <linux/config.h>

/* Some R4000 / R4400 / R4600 / R5000 machines may have a non-dma-coherent,
   chipset implemented caches.  On machines with other CPUs the CPU does the
   cache thing itself. */
struct bcache_ops {
	void (*bc_enable)(void);
	void (*bc_disable)(void);
	void (*bc_wback_inv)(unsigned long page, unsigned long size);
	void (*bc_inv)(unsigned long page, unsigned long size);
};

extern void indy_sc_init(void);
extern void sni_pcimt_sc_init(void);

#ifdef CONFIG_BOARD_SCACHE

extern struct bcache_ops *bcops;

static inline void bc_enable(void)
{
	bcops->bc_enable();
}

static inline void bc_disable(void)
{
	bcops->bc_disable();
}

static inline void bc_wback_inv(unsigned long page, unsigned long size)
{
	bcops->bc_wback_inv(page, size);
}

static inline void bc_inv(unsigned long page, unsigned long size)
{
	bcops->bc_inv(page, size);
}

#else /* !defined(CONFIG_BOARD_SCACHE) */

/* Not R4000 / R4400 / R4600 / R5000.  */

#define bc_enable() do { } while (0)
#define bc_disable() do { } while (0)
#define bc_wback_inv(page, size) do { } while (0)
#define bc_inv(page, size) do { } while (0)

#endif /* !defined(CONFIG_BOARD_SCACHE) */

#endif /* _ASM_BCACHE_H */
