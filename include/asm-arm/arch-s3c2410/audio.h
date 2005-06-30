/* linux/include/asm-arm/arch-s3c2410/audio.h
 *
 * (c) 2004-2005 Simtec Electronics
 *	http://www.simtec.co.uk/products/SWLINUX/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C24XX - Audio platfrom_device info
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Changelog:
 *	20-Nov-2004 BJD  Created file
 *	07-Mar-2005 BJD  Added suspend/resume calls
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

	int	(*open)(struct s3c24xx_iis_ops *me, snd_pcm_substream_t *strm);
	int	(*close)(struct s3c24xx_iis_ops *me, snd_pcm_substream_t *strm);
	int	(*prepare)(struct s3c24xx_iis_ops *me, snd_pcm_substream_t *strm, snd_pcm_runtime_t *rt);
};

struct s3c24xx_platdata_iis {
	const char		*codec_clk;
	struct s3c24xx_iis_ops	*ops;
	int			(*match_dev)(struct device *dev);
};

#endif /* __ASM_ARCH_AUDIO_H */
