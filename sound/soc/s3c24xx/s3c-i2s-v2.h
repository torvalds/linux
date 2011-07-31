/* sound/soc/s3c24xx/s3c-i2s-v2.h
 *
 * ALSA Soc Audio Layer - S3C_I2SV2 I2S driver
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

/* This code is the core support for the I2S block found in a number of
 * Samsung SoC devices which is unofficially named I2S-V2. Currently the
 * S3C2412 and the S3C64XX series use this block to provide 1 or 2 I2S
 * channels via configurable GPIO.
 */

#ifndef __SND_SOC_S3C24XX_S3C_I2SV2_I2S_H
#define __SND_SOC_S3C24XX_S3C_I2SV2_I2S_H __FILE__

#define S3C_I2SV2_DIV_BCLK	(1)
#define S3C_I2SV2_DIV_RCLK	(2)
#define S3C_I2SV2_DIV_PRESCALER	(3)

#define S3C_I2SV2_CLKSRC_PCLK		0
#define S3C_I2SV2_CLKSRC_AUDIOBUS	1
#define S3C_I2SV2_CLKSRC_CDCLK		2

/* Set this flag for I2S controllers that have the bit IISMOD[12]
 * bridge/break RCLK signal and external Xi2sCDCLK pin.
 */
#define S3C_FEATURE_CDCLKCON	(1 << 0)

/**
 * struct s3c_i2sv2_info - S3C I2S-V2 information
 * @dev: The parent device passed to use from the probe.
 * @regs: The pointer to the device registe block.
 * @feature: Set of bit-flags indicating features of the controller.
 * @master: True if the I2S core is the I2S bit clock master.
 * @dma_playback: DMA information for playback channel.
 * @dma_capture: DMA information for capture channel.
 * @suspend_iismod: PM save for the IISMOD register.
 * @suspend_iiscon: PM save for the IISCON register.
 * @suspend_iispsr: PM save for the IISPSR register.
 *
 * This is the private codec state for the hardware associated with an
 * I2S channel such as the register mappings and clock sources.
 */
struct s3c_i2sv2_info {
	struct device	*dev;
	void __iomem	*regs;

	u32		feature;

	struct clk	*iis_pclk;
	struct clk	*iis_cclk;

	unsigned char	 master;

	struct s3c_dma_params	*dma_playback;
	struct s3c_dma_params	*dma_capture;

	u32		 suspend_iismod;
	u32		 suspend_iiscon;
	u32		 suspend_iispsr;
};

extern struct clk *s3c_i2sv2_get_clock(struct snd_soc_dai *cpu_dai);

struct s3c_i2sv2_rate_calc {
	unsigned int	clk_div;	/* for prescaler */
	unsigned int	fs_div;		/* for root frame clock */
};

extern int s3c_i2sv2_iis_calc_rate(struct s3c_i2sv2_rate_calc *info,
				   unsigned int *fstab,
				   unsigned int rate, struct clk *clk);

/**
 * s3c_i2sv2_probe - probe for i2s device helper
 * @pdev: The platform device supplied to the original probe.
 * @dai: The ASoC DAI structure supplied to the original probe.
 * @i2s: Our local i2s structure to fill in.
 * @base: The base address for the registers.
 */
extern int s3c_i2sv2_probe(struct platform_device *pdev,
			   struct snd_soc_dai *dai,
			   struct s3c_i2sv2_info *i2s,
			   unsigned long base);

/**
 * s3c_i2sv2_register_dai - register dai with soc core
 * @dai: The snd_soc_dai structure to register
 *
 * Fill in any missing fields and then register the given dai with the
 * soc core.
 */
extern int s3c_i2sv2_register_dai(struct snd_soc_dai *dai);

#endif /* __SND_SOC_S3C24XX_S3C_I2SV2_I2S_H */
