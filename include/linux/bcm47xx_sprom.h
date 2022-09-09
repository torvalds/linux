/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 */

#ifndef __BCM47XX_SPROM_H
#define __BCM47XX_SPROM_H

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/vmalloc.h>

struct ssb_sprom;

#ifdef CONFIG_BCM47XX_SPROM
void bcm47xx_fill_sprom(struct ssb_sprom *sprom, const char *prefix,
			bool fallback);
int bcm47xx_sprom_register_fallbacks(void);
#else
static inline void bcm47xx_fill_sprom(struct ssb_sprom *sprom,
				      const char *prefix,
				      bool fallback)
{
}

static inline int bcm47xx_sprom_register_fallbacks(void)
{
	return -ENOTSUPP;
};
#endif

#endif /* __BCM47XX_SPROM_H */
