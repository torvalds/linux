// SPDX-License-Identifier: GPL-2.0
//
// soc-link.c
//
// Copyright (C) 2019 Renesas Electronics Corp.
// Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
//
#include <sound/soc.h>
#include <sound/soc-link.h>

#define soc_link_ret(rtd, ret) _soc_link_ret(rtd, __func__, ret)
static inline int _soc_link_ret(struct snd_soc_pcm_runtime *rtd,
				const char *func, int ret)
{
	switch (ret) {
	case -EPROBE_DEFER:
	case -ENOTSUPP:
	case 0:
		break;
	default:
		dev_err(rtd->dev,
			"ASoC: error at %s on %s: %d\n",
			func, rtd->dai_link->name, ret);
	}

	return ret;
}

int snd_soc_link_init(struct snd_soc_pcm_runtime *rtd)
{
	int ret = 0;

	if (rtd->dai_link->init)
		ret = rtd->dai_link->init(rtd);

	return soc_link_ret(rtd, ret);
}
