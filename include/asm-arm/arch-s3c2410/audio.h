/* linux/include/asm-arm/arch-s3c2410/audio.h
 *
 * Copyright (c) 2004-2005 Simtec Electronics
 *	http://www.simtec.co.uk/products/SWLINUX/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C24XX - Audio platfrom_device info
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_AUDIO_H
#define __ASM_ARCH_AUDIO_H __FILE__

/* struct s3c24xx_iis_ops
 *
 * called from the s3c24xx audio core to deal with the architecture
 * or the codec's setup and control.
 *
 * the pointer to itself is passed through in case the caller wants to
 * embed this in an larger structure for easy reference to it's context.
*/

struct s3c24xx_iis_ops {
	struct module *owner;

	int	(*startup)(struct s3c24xx_iis_ops *me);
	void	(*shutdown)(struct s3c24xx_iis_ops *me);
	int	(*suspend)(struct s3c24xx_iis_ops *me);
	int	(*resume)(struct s3c24xx_iis_ops *me);

	int	(*open)(struct s3c24xx_iis_ops *me, struct snd_pcm_substream *strm);
	int	(*close)(struct s3c24xx_iis_ops *me, struct snd_pcm_substream *strm);
	int	(*prepare)(struct s3c24xx_iis_ops *me, struct snd_pcm_substream *strm, struct snd_pcm_runtime *rt);
};

struct s3c24xx_platdata_iis {
	const char		*codec_clk;
	struct s3c24xx_iis_ops	*ops;
	int			(*match_dev)(struct device *dev);
};

#endif /* __ASM_ARCH_AUDIO_H */
