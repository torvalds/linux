/* sound/soc/s3c24xx/s3c64xx-i2s.h
 *
 * ALSA SoC Audio Layer - S3C64XX I2S driver
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *      Ben Dooks <ben@simtec.co.uk>
 *      http://armlinux.simtec.co.uk/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __SND_SOC_S3C24XX_S3C64XX_I2S_H
#define __SND_SOC_S3C24XX_S3C64XX_I2S_H __FILE__

struct clk;

#include "s3c-i2s-v2.h"

#define S3C64XX_DIV_BCLK	S3C_I2SV2_DIV_BCLK
#define S3C64XX_DIV_RCLK	S3C_I2SV2_DIV_RCLK
#define S3C64XX_DIV_PRESCALER	S3C_I2SV2_DIV_PRESCALER

#define S3C64XX_CLKSRC_PCLK	(0)
#define S3C64XX_CLKSRC_MUX	(1)

extern struct snd_soc_dai s3c64xx_i2s_dai[];

extern struct clk *s3c64xx_i2s_get_clock(struct snd_soc_dai *dai);

#endif /* __SND_SOC_S3C24XX_S3C64XX_I2S_H */
