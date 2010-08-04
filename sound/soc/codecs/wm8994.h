/*
 * wm8994.h  --  WM8994 Soc Audio driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _WM8994_H
#define _WM8994_H

#include <sound/soc.h>

extern struct snd_soc_codec_device soc_codec_dev_wm8994;
extern struct snd_soc_dai wm8994_dai[];

/* Sources for AIF1/2 SYSCLK - use with set_dai_sysclk() */
#define WM8994_SYSCLK_MCLK1 1
#define WM8994_SYSCLK_MCLK2 2
#define WM8994_SYSCLK_FLL1  3
#define WM8994_SYSCLK_FLL2  4

#define WM8994_FLL1 1
#define WM8994_FLL2 2

#define WM8994_FLL_SRC_MCLK1  1
#define WM8994_FLL_SRC_MCLK2  2
#define WM8994_FLL_SRC_LRCLK  3
#define WM8994_FLL_SRC_BCLK   4

int wm8994_mic_detect(struct snd_soc_codec *codec, struct snd_soc_jack *jack,
		      int micbias, int det, int shrt);

#endif
