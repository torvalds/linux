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

int snd_soc_link_startup(struct snd_soc_pcm_runtime *rtd,
			 struct snd_pcm_substream *substream)
{
	int ret = 0;

	if (rtd->dai_link->ops &&
	    rtd->dai_link->ops->startup)
		ret = rtd->dai_link->ops->startup(substream);

	return soc_link_ret(rtd, ret);
}

void snd_soc_link_shutdown(struct snd_soc_pcm_runtime *rtd,
			   struct snd_pcm_substream *substream)
{
	if (rtd->dai_link->ops &&
	    rtd->dai_link->ops->shutdown)
		rtd->dai_link->ops->shutdown(substream);
}

int snd_soc_link_prepare(struct snd_soc_pcm_runtime *rtd,
			 struct snd_pcm_substream *substream)
{
	int ret = 0;

	if (rtd->dai_link->ops &&
	    rtd->dai_link->ops->prepare)
		ret = rtd->dai_link->ops->prepare(substream);

	return soc_link_ret(rtd, ret);
}

int snd_soc_link_hw_params(struct snd_soc_pcm_runtime *rtd,
			   struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *params)
{
	int ret = 0;

	if (rtd->dai_link->ops &&
	    rtd->dai_link->ops->hw_params)
		ret = rtd->dai_link->ops->hw_params(substream, params);

	return soc_link_ret(rtd, ret);
}

void snd_soc_link_hw_free(struct snd_soc_pcm_runtime *rtd,
			  struct snd_pcm_substream *substream)
{
	if (rtd->dai_link->ops &&
	    rtd->dai_link->ops->hw_free)
		rtd->dai_link->ops->hw_free(substream);
}

int snd_soc_link_trigger(struct snd_soc_pcm_runtime *rtd,
			 struct snd_pcm_substream *substream, int cmd)
{
	int ret = 0;

	if (rtd->dai_link->ops &&
	    rtd->dai_link->ops->trigger)
		ret = rtd->dai_link->ops->trigger(substream, cmd);

	return soc_link_ret(rtd, ret);
}
