/*
 * AESS IP block reset
 *
 * Copyright (C) 2012 Texas Instruments, Inc.
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */
#ifndef __SOUND_AESS_H__
#define __SOUND_AESS_H__

#include <linux/kernel.h>
#include <linux/io.h>

/*
 * AESS_AUTO_GATING_ENABLE_OFFSET: offset in bytes of the AESS IP
 *     block's AESS_AUTO_GATING_ENABLE__1 register from the IP block's
 *     base address
 */
#define AESS_AUTO_GATING_ENABLE_OFFSET			0x07c

/* Register bitfields in the AESS_AUTO_GATING_ENABLE__1 register */
#define AESS_AUTO_GATING_ENABLE_SHIFT			0

/**
 * aess_enable_autogating - enable AESS internal autogating
 * @oh: struct omap_hwmod *
 *
 * Enable internal autogating on the AESS.  This allows the AESS to
 * indicate that it is idle to the OMAP PRCM.  Returns 0.
 */
static inline void aess_enable_autogating(void __iomem *base)
{
	u32 v;

	/* Set AESS_AUTO_GATING_ENABLE__1.ENABLE to allow idle entry */
	v = 1 << AESS_AUTO_GATING_ENABLE_SHIFT;
	writel(v, base + AESS_AUTO_GATING_ENABLE_OFFSET);
}

#endif /* __SOUND_AESS_H__ */
