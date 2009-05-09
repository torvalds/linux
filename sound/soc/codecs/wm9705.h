/*
 * wm9705.h  --  WM9705 Soc Audio driver
 */

#ifndef _WM9705_H
#define _WM9705_H

#define WM9705_DAI_AC97_HIFI	0
#define WM9705_DAI_AC97_AUX	1

extern struct snd_soc_dai wm9705_dai[2];
extern struct snd_soc_codec_device soc_codec_dev_wm9705;

#endif
