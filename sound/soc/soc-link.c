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
	/* Positive, Zero values are not errors */
	if (ret >= 0)
		return ret;

	/* Negative values might be errors */
	switch (ret) {
	case -EPROBE_DEFER:
	case -ENOTSUPP:
		break;
	default:
		dev_err(rtd->dev,
			"ASoC: error at %s on %s: %d\n",
			func, rtd->dai_link->name, ret);
	}

	return ret;
}

/*
 * We might want to check substream by using list.
 * In such case, we can update these macros.
 */
#define soc_link_mark_push(rtd, substream, tgt)		((rtd)->mark_##tgt = substream)
#define soc_link_mark_pop(rtd, substream, tgt)		((rtd)->mark_##tgt = NULL)
#define soc_link_mark_match(rtd, substream, tgt)	((rtd)->mark_##tgt == substream)

int snd_soc_link_init(struct snd_soc_pcm_runtime *rtd)
{
	int ret = 0;

	if (rtd->dai_link->init)
		ret = rtd->dai_link->init(rtd);

	return soc_link_ret(rtd, ret);
}

void snd_soc_link_exit(struct snd_soc_pcm_runtime *rtd)
{
	if (rtd->dai_link->exit)
		rtd->dai_link->exit(rtd);
}

int snd_soc_link_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				    struct snd_pcm_hw_params *params)
{
	int ret = 0;

	if (rtd->dai_link->be_hw_params_fixup)
		ret = rtd->dai_link->be_hw_params_fixup(rtd, params);

	return soc_link_ret(rtd, ret);
}

int snd_soc_link_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	int ret = 0;

	if (rtd->dai_link->ops &&
	    rtd->dai_link->ops->startup)
		ret = rtd->dai_link->ops->startup(substream);

	/* mark substream if succeeded */
	if (ret == 0)
		soc_link_mark_push(rtd, substream, startup);

	return soc_link_ret(rtd, ret);
}

void snd_soc_link_shutdown(struct snd_pcm_substream *substream,
			   int rollback)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);

	if (rollback && !soc_link_mark_match(rtd, substream, startup))
		return;

	if (rtd->dai_link->ops &&
	    rtd->dai_link->ops->shutdown)
		rtd->dai_link->ops->shutdown(substream);

	/* remove marked substream */
	soc_link_mark_pop(rtd, substream, startup);
}

int snd_soc_link_prepare(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	int ret = 0;

	if (rtd->dai_link->ops &&
	    rtd->dai_link->ops->prepare)
		ret = rtd->dai_link->ops->prepare(substream);

	return soc_link_ret(rtd, ret);
}

int snd_soc_link_hw_params(struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	int ret = 0;

	if (rtd->dai_link->ops &&
	    rtd->dai_link->ops->hw_params)
		ret = rtd->dai_link->ops->hw_params(substream, params);

	return soc_link_ret(rtd, ret);
}

void snd_soc_link_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);

	if (rtd->dai_link->ops &&
	    rtd->dai_link->ops->hw_free)
		rtd->dai_link->ops->hw_free(substream);
}

int snd_soc_link_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	int ret = 0;

	if (rtd->dai_link->ops &&
	    rtd->dai_link->ops->trigger)
		ret = rtd->dai_link->ops->trigger(substream, cmd);

	return soc_link_ret(rtd, ret);
}

int snd_soc_link_compr_startup(struct snd_compr_stream *cstream)
{
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	int ret = 0;

	if (rtd->dai_link->compr_ops &&
	    rtd->dai_link->compr_ops->startup)
		ret = rtd->dai_link->compr_ops->startup(cstream);

	return soc_link_ret(rtd, ret);
}
EXPORT_SYMBOL_GPL(snd_soc_link_compr_startup);

void snd_soc_link_compr_shutdown(struct snd_compr_stream *cstream)
{
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;

	if (rtd->dai_link->compr_ops &&
	    rtd->dai_link->compr_ops->shutdown)
		rtd->dai_link->compr_ops->shutdown(cstream);
}
EXPORT_SYMBOL_GPL(snd_soc_link_compr_shutdown);

int snd_soc_link_compr_set_params(struct snd_compr_stream *cstream)
{
	struct snd_soc_pcm_runtime *rtd = cstream->private_data;
	int ret = 0;

	if (rtd->dai_link->compr_ops &&
	    rtd->dai_link->compr_ops->set_params)
		ret = rtd->dai_link->compr_ops->set_params(cstream);

	return soc_link_ret(rtd, ret);
}
EXPORT_SYMBOL_GPL(snd_soc_link_compr_set_params);
