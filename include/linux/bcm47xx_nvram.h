/*
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#ifndef __BCM47XX_NVRAM_H
#define __BCM47XX_NVRAM_H

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/vmalloc.h>

#ifdef CONFIG_BCM47XX_NVRAM
int bcm47xx_nvram_init_from_mem(u32 base, u32 lim);
int bcm47xx_nvram_getenv(const char *name, char *val, size_t val_len);
int bcm47xx_nvram_gpio_pin(const char *name);
char *bcm47xx_nvram_get_contents(size_t *val_len);
static inline void bcm47xx_nvram_release_contents(char *nvram)
{
	vfree(nvram);
};
#else
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
