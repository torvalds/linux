/*
 * rockchip-pcm.h - ALSA PCM interface for the Rockchip rk28 SoC
 *
 * Driver for rockchip iis audio
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ROCKCHIP_PCM_H
#define _ROCKCHIP_PCM_H

int rockchip_pcm_platform_register(struct device *dev);
int rockchip_pcm_platform_unregister(struct device *dev);

#endif /* _ROCKCHIP_PCM_H */
