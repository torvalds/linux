/*
 * Renesas R-Car SRU/SCU/SSIU/SSI support
 *
 * Copyright (C) 2013 Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * Based on fsi.c
 * Kuninori Morimoto <morimoto.kuninori@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * Renesas R-Car sound device structure
 *
 * Gen1
 *
 * SRU		: Sound Routing Unit
 *  - SRC	: Sampling Rate Converter
 *  - CMD
 *    - CTU	: Channel Count Conversion Unit
 *    - MIX	: Mixer
 *    - DVC	: Digital Volume and Mute Function
 *  - SSI	: Serial Sound Interface
 *
 * Gen2
 *
 * SCU		: Sampling Rate Converter Unit
 *  - SRC	: Sampling Rate Converter
 *  - CMD
 *   - CTU	: Channel Count Conversion Unit
 *   - MIX	: Mixer
 *   - DVC	: Digital Volume and Mute Function
 * SSIU		: Serial Sound Interface Unit
 *  - SSI	: Serial Sound Interface
 */

/*
 *	driver data Image
 *
 * rsnd_priv
 *   |
 *   | ** this depends on Gen1/Gen2
 *   |
 *   +- gen
 *   |
 *   | ** these depend on data path
 *   | ** gen and platform data control it
 *   |
 *   +- rdai[0]
 *   |   |		 sru     ssiu      ssi
 *   |   +- playback -> [mod] -> [mod] -> [mod] -> ...
 *   |   |
 *   |   |		 sru     ssiu      ssi
 *   |   +- capture  -> [mod] -> [mod] -> [mod] -> ...
 *   |
 *   +- rdai[1]
 *   |   |		 sru     ssiu      ssi
 *   |   +- playback -> [mod] -> [mod] -> [mod] -> ...
 *   |   |
 *   |   |		 sru     ssiu      ssi
 *   |   +- capture  -> [mod] -> [mod] -> [mod] -> ...
 *   ...
 *   |
 *   | ** these control ssi
 *   |
 *   +- ssi
 *   |  |
 *   |  +- ssi[0]
 *   |  +- ssi[1]
 *   |  +- ssi[2]
 *   |  ...
 *   |
 *   | ** these control scu
 *   |
 *   +- scu
 *      |
 *      +- scu[0]
 *      +- scu[1]
 *      +- scu[2]
 *      ...
 *
 *
 * for_each_rsnd_dai(xx, priv, xx)
 *  rdai[0] => rdai[1] => rdai[2] => ...
 *
 * for_each_rsnd_mod(xx, rdai, xx)
 *  [mod] => [mod] => [mod] => ...
 *
 * rsnd_dai_call(xxx, fn )
 *  [mod]->fn() -> [mod]->fn() -> [mod]->fn()...
 *
 */
#include <linux/pm_runtime.h>
#include "rsnd.h"

#define RSND_RATES SNDRV_PCM_RATE_8000_96000
#define RSND_FMTS (SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S16_LE)

/*
 *	rsnd_platform functions
 */
#define rsnd_platform_call(priv, dai, func, param...)	\
	(!(priv->info->func) ? -ENODEV :		\
	 priv->info->func(param))


/*
 *	rsnd_dai functions
 */
struct rsnd_dai *rsnd_dai_get(struct rsnd_priv *priv, int id)
{
	return priv->rdai + id;
}

static struct rsnd_dai *rsnd_dai_to_rdai(struct snd_soc_dai *dai)
{
	struct rsnd_priv *priv = snd_soc_dai_get_drvdata(dai);

	return rsnd_dai_get(priv, dai->id);
}

int rsnd_dai_is_play(struct rsnd_dai *rdai, struct rsnd_dai_stream *io)
{
	return &rdai->playback == io;
}

/*
 *	rsnd_soc_dai functions
 */
int rsnd_dai_pointer_offset(struct rsnd_dai_stream *io, int additional)
{
	struct snd_pcm_substream *substream = io->substream;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int pos = io->byte_pos + additional;

	pos %= (runtime->periods * io->byte_per_period);

	return pos;
}

void rsnd_dai_pointer_update(struct rsnd_dai_stream *io, int byte)
{
	io->byte_pos += byte;

	if (io->byte_pos >= io->next_period_byte) {
		struct snd_pcm_substream *substream = io->substream;
		struct snd_pcm_runtime *runtime = substream->runtime;

		io->period_pos++;
		io->next_period_byte += io->byte_per_period;

		if (io->period_pos >= runtime->periods) {
			io->byte_pos = 0;
			io->period_pos = 0;
			io->next_period_byte = io->byte_per_period;
		}

		snd_pcm_period_elapsed(substream);
	}
}

static int rsnd_dai_stream_init(struct rsnd_dai_stream *io,
				struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	if (!list_empty(&io->head))
		return -EIO;

	INIT_LIST_HEAD(&io->head);
	io->substream		= substream;
	io->byte_pos		= 0;
	io->period_pos		= 0;
	io->byte_per_period	= runtime->period_size *
				  runtime->channels *
				  samples_to_bytes(runtime, 1);
	io->next_period_byte	= io->byte_per_period;

	return 0;
}

static
struct snd_soc_dai *rsnd_substream_to_dai(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;

	return  rtd->cpu_dai;
}

static
struct rsnd_dai_stream *rsnd_rdai_to_io(struct rsnd_dai *rdai,
					struct snd_pcm_substream *substream)
{
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return &rdai->playback;
	else
		return &rdai->capture;
}

static int rsnd_soc_dai_trigger(struct snd_pcm_substream *substream, int cmd,
			    struct snd_soc_dai *dai)
{
	struct rsnd_priv *priv = snd_soc_dai_get_drvdata(dai);
	struct rsnd_dai *rdai = rsnd_dai_to_rdai(dai);
	struct rsnd_dai_stream *io = rsnd_rdai_to_io(rdai, substream);
	struct rsnd_dai_platform_info *info = rsnd_dai_get_platform_info(rdai);
	int ssi_id = rsnd_dai_is_play(rdai, io) ?	info->ssi_id_playback :
							info->ssi_id_capture;
	int ret;
	unsigned long flags;

	rsnd_lock(priv, flags);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		ret = rsnd_dai_stream_init(io, substream);
		if (ret < 0)
			goto dai_trigger_end;

		ret = rsnd_platform_call(priv, dai, start, ssi_id);
		if (ret < 0)
			goto dai_trigger_end;

		break;
	case SNDRV_PCM_TRIGGER_STOP:
		ret = rsnd_platform_call(priv, dai, stop, ssi_id);
		if (ret < 0)
			goto dai_trigger_end;

		break;
	default:
		ret = -EINVAL;
	}

dai_trigger_end:
	rsnd_unlock(priv, flags);

	return ret;
}

static int rsnd_soc_dai_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct rsnd_dai *rdai = rsnd_dai_to_rdai(dai);

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		rdai->clk_master = 1;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		rdai->clk_master = 0;
		break;
	default:
		return -EINVAL;
	}

	/* set clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_IF:
		rdai->bit_clk_inv = 0;
		rdai->frm_clk_inv = 1;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		rdai->bit_clk_inv = 1;
		rdai->frm_clk_inv = 0;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		rdai->bit_clk_inv = 1;
		rdai->frm_clk_inv = 1;
		break;
	case SND_SOC_DAIFMT_NB_NF:
	default:
		rdai->bit_clk_inv = 0;
		rdai->frm_clk_inv = 0;
		break;
	}

	/* set format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		rdai->sys_delay = 0;
		rdai->data_alignment = 0;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		rdai->sys_delay = 1;
		rdai->data_alignment = 0;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		rdai->sys_delay = 1;
		rdai->data_alignment = 1;
		break;
	}

	return 0;
}

static const struct snd_soc_dai_ops rsnd_soc_dai_ops = {
	.trigger	= rsnd_soc_dai_trigger,
	.set_fmt	= rsnd_soc_dai_set_fmt,
};

static int rsnd_dai_probe(struct platform_device *pdev,
			  struct rcar_snd_info *info,
			  struct rsnd_priv *priv)
{
	struct snd_soc_dai_driver *drv;
	struct rsnd_dai *rdai;
	struct device *dev = rsnd_priv_to_dev(priv);
	struct rsnd_dai_platform_info *dai_info;
	int dai_nr = info->dai_info_nr;
	int i, pid, cid;

	drv  = devm_kzalloc(dev, sizeof(*drv)  * dai_nr, GFP_KERNEL);
	rdai = devm_kzalloc(dev, sizeof(*rdai) * dai_nr, GFP_KERNEL);
	if (!drv || !rdai) {
		dev_err(dev, "dai allocate failed\n");
		return -ENOMEM;
	}

	for (i = 0; i < dai_nr; i++) {
		dai_info = &info->dai_info[i];

		pid = dai_info->ssi_id_playback;
		cid = dai_info->ssi_id_capture;

		/*
		 *	init rsnd_dai
		 */
		INIT_LIST_HEAD(&rdai[i].playback.head);
		INIT_LIST_HEAD(&rdai[i].capture.head);

		rdai[i].info = dai_info;

		snprintf(rdai[i].name, RSND_DAI_NAME_SIZE, "rsnd-dai.%d", i);

		/*
		 *	init snd_soc_dai_driver
		 */
		drv[i].name	= rdai[i].name;
		drv[i].ops	= &rsnd_soc_dai_ops;
		if (pid >= 0) {
			drv[i].playback.rates		= RSND_RATES;
			drv[i].playback.formats		= RSND_FMTS;
			drv[i].playback.channels_min	= 2;
			drv[i].playback.channels_max	= 2;
		}
		if (cid >= 0) {
			drv[i].capture.rates		= RSND_RATES;
			drv[i].capture.formats		= RSND_FMTS;
			drv[i].capture.channels_min	= 2;
			drv[i].capture.channels_max	= 2;
		}

		dev_dbg(dev, "%s (%d, %d) probed", rdai[i].name, pid, cid);
	}

	priv->dai_nr	= dai_nr;
	priv->daidrv	= drv;
	priv->rdai	= rdai;

	return 0;
}

static void rsnd_dai_remove(struct platform_device *pdev,
			  struct rsnd_priv *priv)
{
}

/*
 *		pcm ops
 */
static struct snd_pcm_hardware rsnd_pcm_hardware = {
	.info =		SNDRV_PCM_INFO_INTERLEAVED	|
			SNDRV_PCM_INFO_MMAP		|
			SNDRV_PCM_INFO_MMAP_VALID	|
			SNDRV_PCM_INFO_PAUSE,
	.formats		= RSND_FMTS,
	.rates			= RSND_RATES,
	.rate_min		= 8000,
	.rate_max		= 192000,
	.channels_min		= 2,
	.channels_max		= 2,
	.buffer_bytes_max	= 64 * 1024,
	.period_bytes_min	= 32,
	.period_bytes_max	= 8192,
	.periods_min		= 1,
	.periods_max		= 32,
	.fifo_size		= 256,
};

static int rsnd_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int ret = 0;

	snd_soc_set_runtime_hwparams(substream, &rsnd_pcm_hardware);

	ret = snd_pcm_hw_constraint_integer(runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);

	return ret;
}

static int rsnd_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *hw_params)
{
	return snd_pcm_lib_malloc_pages(substream,
					params_buffer_bytes(hw_params));
}

static snd_pcm_uframes_t rsnd_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_dai *dai = rsnd_substream_to_dai(substream);
	struct rsnd_dai *rdai = rsnd_dai_to_rdai(dai);
	struct rsnd_dai_stream *io = rsnd_rdai_to_io(rdai, substream);

	return bytes_to_frames(runtime, io->byte_pos);
}

static struct snd_pcm_ops rsnd_pcm_ops = {
	.open		= rsnd_pcm_open,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= rsnd_hw_params,
	.hw_free	= snd_pcm_lib_free_pages,
	.pointer	= rsnd_pointer,
};

/*
 *		snd_soc_platform
 */

#define PREALLOC_BUFFER		(32 * 1024)
#define PREALLOC_BUFFER_MAX	(32 * 1024)

static int rsnd_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	return snd_pcm_lib_preallocate_pages_for_all(
		rtd->pcm,
		SNDRV_DMA_TYPE_DEV,
		rtd->card->snd_card->dev,
		PREALLOC_BUFFER, PREALLOC_BUFFER_MAX);
}

static void rsnd_pcm_free(struct snd_pcm *pcm)
{
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static struct snd_soc_platform_driver rsnd_soc_platform = {
	.ops		= &rsnd_pcm_ops,
	.pcm_new	= rsnd_pcm_new,
	.pcm_free	= rsnd_pcm_free,
};

static const struct snd_soc_component_driver rsnd_soc_component = {
	.name		= "rsnd",
};

/*
 *	rsnd probe
 */
static int rsnd_probe(struct platform_device *pdev)
{
	struct rcar_snd_info *info;
	struct rsnd_priv *priv;
	struct device *dev = &pdev->dev;
	int ret;

	info = pdev->dev.platform_data;
	if (!info) {
		dev_err(dev, "driver needs R-Car sound information\n");
		return -ENODEV;
	}

	/*
	 *	init priv data
	 */
	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(dev, "priv allocate failed\n");
		return -ENODEV;
	}

	priv->dev	= dev;
	priv->info	= info;
	spin_lock_init(&priv->lock);

	/*
	 *	init each module
	 */
	ret = rsnd_dai_probe(pdev, info, priv);
	if (ret < 0)
		return ret;

	/*
	 *	asoc register
	 */
	ret = snd_soc_register_platform(dev, &rsnd_soc_platform);
	if (ret < 0) {
		dev_err(dev, "cannot snd soc register\n");
		return ret;
	}

	ret = snd_soc_register_component(dev, &rsnd_soc_component,
					 priv->daidrv, rsnd_dai_nr(priv));
	if (ret < 0) {
		dev_err(dev, "cannot snd dai register\n");
		goto exit_snd_soc;
	}

	dev_set_drvdata(dev, priv);

	pm_runtime_enable(dev);

	dev_info(dev, "probed\n");
	return ret;

exit_snd_soc:
	snd_soc_unregister_platform(dev);

	return ret;
}

static int rsnd_remove(struct platform_device *pdev)
{
	struct rsnd_priv *priv = dev_get_drvdata(&pdev->dev);

	pm_runtime_disable(&pdev->dev);

	/*
	 *	remove each module
	 */
	rsnd_dai_remove(pdev, priv);

	return 0;
}

static struct platform_driver rsnd_driver = {
	.driver	= {
		.name	= "rcar_sound",
	},
	.probe		= rsnd_probe,
	.remove		= rsnd_remove,
};
module_platform_driver(rsnd_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Renesas R-Car audio driver");
MODULE_AUTHOR("Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>");
MODULE_ALIAS("platform:rcar-pcm-audio");
