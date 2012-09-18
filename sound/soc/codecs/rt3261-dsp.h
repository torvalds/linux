/*
 * rt3261-dsp.h  --  RT3261 ALSA SoC DSP driver
 *
 * Copyright 2011 Realtek Microelectronics
 * Author: Johnny Hsu <johnnyhsu@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __RT3261_DSP_H__
#define __RT3261_DSP_H__

/* Debug String Length */
#define RT3261_DSP_REG_DISP_LEN 12

enum {
	RT3261_DSP_DIS,
	RT3261_DSP_AEC_NS_FENS,
	RT3261_DSP_HFBF,
	RT3261_DSP_FFP,
};

struct rt3261_dsp_param {
	u16 cmd_fmt;
	u16 addr;
	u16 data;
	u8 cmd;
};

int rt3261_dsp_write(struct snd_soc_codec *codec, struct rt3261_dsp_param *param);
unsigned int rt3261_dsp_read(struct snd_soc_codec *codec, unsigned int reg);
int rt3261_dsp_probe(struct snd_soc_codec *codec);
#ifdef CONFIG_PM
int rt3261_dsp_suspend(struct snd_soc_codec *codec, pm_message_t state);
int rt3261_dsp_resume(struct snd_soc_codec *codec);
#endif

#endif /* __RT3261_DSP_H__ */

