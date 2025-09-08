/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 */

#ifndef __BCM47XX_NVRAM_H
#define __BCM47XX_NVRAM_H

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/vmalloc.h>

#ifdef CONFIG_BCM47XX_NVRAM
int bcm47xx_nvram_init_from_iomem(void __iomem *nvram_start, size_t res_size);
int bcm47xx_nvram_init_from_mem(u32 base, u32 lim);
int bcm47xx_nvram_getenv(const char *name, char *val, size_t val_len);
int bcm47xx_nvram_gpio_pin(const char *name);
char *bcm47xx_nvram_get_contents(size_t *val_len);
static inline void bcm47xx_nvram_release_contents(char *nvram)
{
	vfree(nvram);
};
#else
static inline int bcm47xx_nvram_init_from_iomem(void __iomem *nvram_start,
						size_t res_size)
{
	return -ENOTSUPP;
}
static inline int bcm47xx_nvram_init_from_mem(u32 base, u32 lim)
{
	return -ENOTSUPP;
};
static inline int bcm47xx_nvram_getenv(const char *name, char *val,
				       size_t val_len)
{
	return -ENOTSUPP;
};
static inline int bcm47xx_nvram_gpio_pin(const char *name)
{
	return -ENOTSUPP;
};

static inline char *bcm47xx_nvram_get_contents(size_t *val_len)
{
	return NULL;
};

static inline void bcm47xx_nvram_release_contents(char *nvram)
{
};
#endif

#endif /* __BCM47XX_NVRAM_H */
