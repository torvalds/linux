/*
 * omap-pcm.h - OMAP PCM driver
 *
 * Copyright (C) 2014 Texas Instruments, Inc.
 *
 * Author: Peter Ujfalusi <peter.ujfalusi@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef __OMAP_PCM_H__
#define __OMAP_PCM_H__

#if IS_ENABLED(CONFIG_SND_OMAP_SOC)
int omap_pcm_platform_register(struct device *dev);
#else
static inline int omap_pcm_platform_register(struct device *dev)
{
	return 0;
}
#endif /* CONFIG_SND_OMAP_SOC */

#endif /* __OMAP_PCM_H__ */
