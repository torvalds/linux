/*
 * rt_codec_ioctl.h  --  RT56XX ALSA SoC audio driver IO control
 *
 * Copyright 2012 Realtek Microelectronics
 * Author: Bard <bardliao@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __RT56XX_IOCTL_H__
#define __RT56XX_IOCTL_H__

#include <sound/hwdep.h>
#include <linux/ioctl.h>

struct rt_codec_cmd {
	size_t number;
	int __user *buf;
};

struct rt_codec_ops {
	int (*index_write)(struct snd_soc_codec *codec,
		unsigned int reg, unsigned int value);
	unsigned int (*index_read)(struct snd_soc_codec *codec,
				unsigned int reg);
	int (*index_update_bits)(struct snd_soc_codec *codec,
		unsigned int reg, unsigned int mask, unsigned int value);
	int (*ioctl_common)(struct snd_hwdep *hw, struct file *file,
			unsigned int cmd, unsigned long arg);
};

enum {
	RT_READ_CODEC_REG_IOCTL = _IOR('R', 0x01, struct rt_codec_cmd),
	RT_WRITE_CODEC_REG_IOCTL = _IOW('R', 0x01, struct rt_codec_cmd),
	RT_READ_ALL_CODEC_REG_IOCTL = _IOR('R', 0x02, struct rt_codec_cmd),
	RT_READ_CODEC_INDEX_IOCTL = _IOR('R', 0x03, struct rt_codec_cmd),
	RT_WRITE_CODEC_INDEX_IOCTL = _IOW('R', 0x03, struct rt_codec_cmd),
	RT_READ_CODEC_DSP_IOCTL = _IOR('R', 0x04, struct rt_codec_cmd),
	RT_WRITE_CODEC_DSP_IOCTL = _IOW('R', 0x04, struct rt_codec_cmd),
	RT_SET_CODEC_HWEQ_IOCTL = _IOW('R', 0x05, struct rt_codec_cmd),
	RT_GET_CODEC_HWEQ_IOCTL = _IOR('R', 0x05, struct rt_codec_cmd),
	RT_SET_CODEC_SPK_VOL_IOCTL = _IOW('R', 0x06, struct rt_codec_cmd),
	RT_GET_CODEC_SPK_VOL_IOCTL = _IOR('R', 0x06, struct rt_codec_cmd),
	RT_SET_CODEC_MIC_GAIN_IOCTL = _IOW('R', 0x07, struct rt_codec_cmd),
	RT_GET_CODEC_MIC_GAIN_IOCTL = _IOR('R', 0x07, struct rt_codec_cmd),
	RT_SET_CODEC_3D_SPK_IOCTL = _IOW('R', 0x08, struct rt_codec_cmd),
	RT_GET_CODEC_3D_SPK_IOCTL = _IOR('R', 0x08, struct rt_codec_cmd),
	RT_SET_CODEC_MP3PLUS_IOCTL = _IOW('R', 0x09, struct rt_codec_cmd),
	RT_GET_CODEC_MP3PLUS_IOCTL = _IOR('R', 0x09, struct rt_codec_cmd),
	RT_SET_CODEC_3D_HEADPHONE_IOCTL = _IOW('R', 0x0a, struct rt_codec_cmd),
	RT_GET_CODEC_3D_HEADPHONE_IOCTL = _IOR('R', 0x0a, struct rt_codec_cmd),
	RT_SET_CODEC_BASS_BACK_IOCTL = _IOW('R', 0x0b, struct rt_codec_cmd),
	RT_GET_CODEC_BASS_BACK_IOCTL = _IOR('R', 0x0b, struct rt_codec_cmd),
	RT_SET_CODEC_DIPOLE_SPK_IOCTL = _IOW('R', 0x0c, struct rt_codec_cmd),
	RT_GET_CODEC_DIPOLE_SPK_IOCTL = _IOR('R', 0x0c, struct rt_codec_cmd),
	RT_SET_CODEC_DRC_AGC_ENABLE_IOCTL = _IOW('R', 0x0d, struct rt_codec_cmd),
	RT_GET_CODEC_DRC_AGC_ENABLE_IOCTL = _IOR('R', 0x0d, struct rt_codec_cmd),
	RT_SET_CODEC_DSP_MODE_IOCTL = _IOW('R', 0x0e, struct rt_codec_cmd),
	RT_GET_CODEC_DSP_MODE_IOCTL = _IOR('R', 0x0e, struct rt_codec_cmd),
	RT_SET_CODEC_WNR_ENABLE_IOCTL = _IOW('R', 0x0f, struct rt_codec_cmd),
	RT_GET_CODEC_WNR_ENABLE_IOCTL = _IOR('R', 0x0f, struct rt_codec_cmd),
	RT_SET_CODEC_DRC_AGC_PAR_IOCTL = _IOW('R', 0x10, struct rt_codec_cmd),
	RT_GET_CODEC_DRC_AGC_PAR_IOCTL = _IOR('R', 0x10, struct rt_codec_cmd),
	RT_SET_CODEC_DIGI_BOOST_GAIN_IOCTL = _IOW('R', 0x11, struct rt_codec_cmd),
	RT_GET_CODEC_DIGI_BOOST_GAIN_IOCTL = _IOR('R', 0x11, struct rt_codec_cmd),
	RT_SET_CODEC_NOISE_GATE_IOCTL = _IOW('R', 0x12, struct rt_codec_cmd),
	RT_GET_CODEC_NOISE_GATE_IOCTL = _IOR('R', 0x12, struct rt_codec_cmd),
	RT_SET_CODEC_DRC_AGC_COMP_IOCTL = _IOW('R', 0x13, struct rt_codec_cmd),
	RT_GET_CODEC_DRC_AGC_COMP_IOCTL = _IOR('R', 0x13, struct rt_codec_cmd),
	RT_GET_CODEC_ID = _IOR('R', 0x30, struct rt_codec_cmd),
};

int realtek_ce_init_hwdep(struct snd_soc_codec *codec);
struct rt_codec_ops *rt_codec_get_ioctl_ops(void);

#endif /* __RT56XX_IOCTL_H__ */
