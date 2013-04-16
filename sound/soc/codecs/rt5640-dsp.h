/*
 * rt5640-dsp.h  --  RT5640 ALSA SoC DSP driver
 *
 * Copyright 2011 Realtek Microelectronics
 * Author: Johnny Hsu <johnnyhsu@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __RT5640_DSP_H__
#define __RT5640_DSP_H__

/* Debug String Length */
#define RT5640_DSP_REG_DISP_LEN 12

enum {
	RT5640_DSP_DIS,
	RT5640_DSP_AEC_NS_FENS,
	RT5640_DSP_HFBF,
	RT5640_DSP_FFP,
};

struct rt5640_dsp_param {
	u16 cmd_fmt;
	u16 addr;
	u16 data;
	u8 cmd;
};

int rt5640_dsp_probe(struct snd_soc_codec *codec);
int rt56xx_dsp_ioctl_common(struct snd_hwdep *hw, struct file *file, unsigned int cmd, unsigned long arg);
#ifdef CONFIG_PM
int rt5640_dsp_suspend(struct snd_soc_codec *codec, pm_message_t state);
int rt5640_dsp_resume(struct snd_soc_codec *codec);
#endif

#endif /* __RT5640_DSP_H__ */

