/* sound/soc/s3c24xx/s3c2412-i2s.c
 *
 * ALSA Soc Audio Layer - S3C2412 I2S driver
 *
 * Copyright (c) 2007 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
*/

#ifndef __SND_SOC_S3C24XX_S3C2412_I2S_H
#define __SND_SOC_S3C24XX_S3C2412_I2S_H __FILE__

#include "s3c-i2s-v2.h"

#define S3C2412_DIV_BCLK	S3C_I2SV2_DIV_BCLK
#define S3C2412_DIV_RCLK	S3C_I2SV2_DIV_RCLK
#define S3C2412_DIV_PRESCALER	S3C_I2SV2_DIV_PRESCALER

#define S3C2412_CLKSRC_PCLK	(0)
#define S3C2412_CLKSRC_I2SCLK	(1)

extern struct clk *s3c2412_get_iisclk(void);

extern struct snd_soc_dai s3c2412_i2s_dai;

#endif /* __SND_SOC_S3C24XX_S3C2412_I2S_H */
