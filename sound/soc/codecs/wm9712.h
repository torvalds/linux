/*
 * wm9712.h  --  WM9712 Soc Audio driver
 */

#ifndef _WM9712_H
#define _WM9712_H

#define WM9712_DAI_AC97_HIFI	0
#define WM9712_DAI_AC97_AUX		1

extern struct snd_soc_dai wm9712_dai[2];
extern struct snd_soc_codec_device soc_codec_dev_wm9712;

#endif
