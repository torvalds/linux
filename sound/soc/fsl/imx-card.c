// SPDX-License-Identifier: GPL-2.0+
// Copyright 2017-2021 NXP

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/i2c.h>
#include <linux/clk.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <sound/pcm.h>
#include <sound/soc-dapm.h>
#include <sound/simple_card_utils.h>

#include "fsl_sai.h"

#define IMX_CARD_MCLK_22P5792MHZ  22579200
#define IMX_CARD_MCLK_24P576MHZ   24576000

enum codec_type {
	CODEC_DUMMY = 0,
	CODEC_AK5558 = 1,
	CODEC_AK4458,
	CODEC_AK4497,
	CODEC_AK5552,
	CODEC_CS42888,
};

/*
 * Mapping LRCK fs and frame width, table 3 & 4 in datasheet
 * @rmin: min rate
 * @rmax: max rate
 * @wmin: min frame ratio
 * @wmax: max frame ratio
 */
struct imx_akcodec_fs_mul {
	unsigned int rmin;
	unsigned int rmax;
	unsigned int wmin;
	unsigned int wmax;
};

/*
 * Mapping TDM mode and frame width
 */
struct imx_akcodec_tdm_fs_mul {
	unsigned int min;
	unsigned int max;
	unsigned int mul;
};

/*
 * struct imx_card_plat_data - specific info for codecs
 *
 * @fs_mul: ratio of mclk/fs for normal mode
 * @tdm_fs_mul: ratio of mclk/fs for tdm mode
 * @support_rates: supported sample rate
 * @support_tdm_rates: supported sample rate for tdm mode
 * @support_channels: supported channels
 * @support_tdm_channels: supported channels for tdm mode
 * @num_fs_mul: ARRAY_SIZE of fs_mul
 * @num_tdm_fs_mul: ARRAY_SIZE of tdm_fs_mul
 * @num_rates: ARRAY_SIZE of support_rates
 * @num_tdm_rates: ARRAY_SIZE of support_tdm_rates
 * @num_channels: ARRAY_SIZE of support_channels
 * @num_tdm_channels: ARRAY_SIZE of support_tdm_channels
 * @type: codec type
 */
struct imx_card_plat_data {
	struct imx_akcodec_fs_mul  *fs_mul;
	struct imx_akcodec_tdm_fs_mul  *tdm_fs_mul;
	const u32 *support_rates;
	const u32 *support_tdm_rates;
	const u32 *support_channels;
	const u32 *support_tdm_channels;
	unsigned int num_fs_mul;
	unsigned int num_tdm_fs_mul;
	unsigned int num_rates;
	unsigned int num_tdm_rates;
	unsigned int num_channels;
	unsigned int num_tdm_channels;
	unsigned int num_codecs;
	enum codec_type type;
};

/*
 * struct dai_link_data - specific info for dai link
 *
 * @slots: slot number
 * @slot_width: slot width value
 * @cpu_sysclk_id: sysclk id for cpu dai
 * @one2one_ratio: true if mclk equal to bclk
 */
struct dai_link_data {
	unsigned int slots;
	unsigned int slot_width;
	unsigned int cpu_sysclk_id;
	bool one2one_ratio;
};

/*
 * struct imx_card_data - platform device data
 *
 * @plat_data: pointer of imx_card_plat_data
 * @dapm_routes: pointer of dapm_routes
 * @link_data: private data for dai link
 * @card: card instance
 * @num_dapm_routes: number of dapm_routes
 * @asrc_rate: asrc rates
 * @asrc_format: asrc format
 */
struct imx_card_data {
	struct imx_card_plat_data *plat_data;
	struct snd_soc_dapm_route *dapm_routes;
	struct dai_link_data *link_data;
	struct snd_soc_card card;
	int num_dapm_routes;
	u32 asrc_rate;
	snd_pcm_format_t asrc_format;
};

static struct imx_akcodec_fs_mul ak4458_fs_mul[] = {
	/* Normal, < 32kHz */
	{ .rmin = 8000,   .rmax = 24000,  .wmin = 256,  .wmax = 1024, },
	/* Normal, 32kHz */
	{ .rmin = 32000,  .rmax = 32000,  .wmin = 256,  .wmax = 1024, },
	/* Normal */
	{ .rmin = 44100,  .rmax = 48000,  .wmin = 256,  .wmax = 768,  },
	/* Double */
	{ .rmin = 88200,  .rmax = 96000,  .wmin = 256,  .wmax = 512,  },
	/* Quad */
	{ .rmin = 176400, .rmax = 192000, .wmin = 128,  .wmax = 256,  },
	/* Oct */
	{ .rmin = 352800, .rmax = 384000, .wmin = 32,   .wmax = 128,  },
	/* Hex */
	{ .rmin = 705600, .rmax = 768000, .wmin = 16,   .wmax = 64,   },
};

static struct imx_akcodec_tdm_fs_mul ak4458_tdm_fs_mul[] = {
	/*
	 * Table 13	- Audio Interface Format
	 * For TDM mode, MCLK should is set to
	 * obtained from 2 * slots * slot_width
	 */
	{ .min = 128,	.max = 128,	.mul = 256  }, /* TDM128 */
	{ .min = 256,	.max = 256,	.mul = 512  }, /* TDM256 */
	{ .min = 512,	.max = 512,	.mul = 1024  }, /* TDM512 */
};

static struct imx_akcodec_fs_mul ak4497_fs_mul[] = {
	/**
	 * Table 7      - mapping multiplier and speed mode
	 * Tables 8 & 9 - mapping speed mode and LRCK fs
	 */
	{ .rmin = 8000,   .rmax = 32000,  .wmin = 256,  .wmax = 1024, }, /* Normal, <= 32kHz */
	{ .rmin = 44100,  .rmax = 48000,  .wmin = 256,  .wmax = 512, }, /* Normal */
	{ .rmin = 88200,  .rmax = 96000,  .wmin = 256,  .wmax = 256, }, /* Double */
	{ .rmin = 176400, .rmax = 192000, .wmin = 128,  .wmax = 128, }, /* Quad */
	{ .rmin = 352800, .rmax = 384000, .wmin = 128,  .wmax = 128, }, /* Oct */
	{ .rmin = 705600, .rmax = 768000, .wmin = 64,   .wmax = 64, }, /* Hex */
};

/*
 * Auto MCLK selection based on LRCK for Normal Mode
 * (Table 4 from datasheet)
 */
static struct imx_akcodec_fs_mul ak5558_fs_mul[] = {
	{ .rmin = 8000,   .rmax = 32000,  .wmin = 512,  .wmax = 1024, },
	{ .rmin = 44100,  .rmax = 48000,  .wmin = 512,  .wmax = 512, },
	{ .rmin = 88200,  .rmax = 96000,  .wmin = 256,  .wmax = 256, },
	{ .rmin = 176400, .rmax = 192000, .wmin = 128,  .wmax = 128, },
	{ .rmin = 352800, .rmax = 384000, .wmin = 64,   .wmax = 64, },
	{ .rmin = 705600, .rmax = 768000, .wmin = 32,   .wmax = 32, },
};

/*
 * MCLK and BCLK selection based on TDM mode
 * because of SAI we also add the restriction: MCLK >= 2 * BCLK
 * (Table 9 from datasheet)
 */
static struct imx_akcodec_tdm_fs_mul ak5558_tdm_fs_mul[] = {
	{ .min = 128,	.max = 128,	.mul = 256 },
	{ .min = 256,	.max = 256,	.mul = 512 },
	{ .min = 512,	.max = 512,	.mul = 1024 },
};

static struct imx_akcodec_fs_mul cs42888_fs_mul[] = {
	{ .rmin = 8000,   .rmax = 48000,  .wmin = 256,  .wmax = 1024, },
	{ .rmin = 64000,  .rmax = 96000,  .wmin = 128,  .wmax = 512, },
	{ .rmin = 176400, .rmax = 192000, .wmin = 64,  .wmax = 256, },
};

static struct imx_akcodec_tdm_fs_mul cs42888_tdm_fs_mul[] = {
	{ .min = 256,	.max = 256,	.mul = 256 },
};

static const u32 akcodec_rates[] = {
	8000, 11025, 16000, 22050, 32000, 44100, 48000, 88200,
	96000, 176400, 192000, 352800, 384000, 705600, 768000,
};

static const u32 akcodec_tdm_rates[] = {
	8000, 16000, 32000, 48000, 96000,
};

static const u32 ak4458_channels[] = {
	1, 2, 4, 6, 8, 10, 12, 14, 16,
};

static const u32 ak4458_tdm_channels[] = {
	1, 2, 3, 4, 5, 6, 7, 8, 16,
};

static const u32 ak5558_channels[] = {
	1, 2, 4, 6, 8,
};

static const u32 ak5558_tdm_channels[] = {
	1, 2, 3, 4, 5, 6, 7, 8,
};

static const u32 cs42888_channels[] = {
	1, 2, 4, 6, 8,
};

static const u32 cs42888_tdm_channels[] = {
	1, 2, 3, 4, 5, 6, 7, 8,
};

static bool format_is_dsd(struct snd_pcm_hw_params *params)
{
	snd_pcm_format_t format = params_format(params);

	switch (format) {
	case SNDRV_PCM_FORMAT_DSD_U8:
	case SNDRV_PCM_FORMAT_DSD_U16_LE:
	case SNDRV_PCM_FORMAT_DSD_U16_BE:
	case SNDRV_PCM_FORMAT_DSD_U32_LE:
	case SNDRV_PCM_FORMAT_DSD_U32_BE:
		return true;
	default:
		return false;
	}
}

static bool format_is_tdm(struct dai_link_data *link_data)
{
	if (link_data->slots > 2)
		return true;
	else
		return false;
}

static bool codec_is_akcodec(unsigned int type)
{
	switch (type) {
	case CODEC_AK4458:
	case CODEC_AK4497:
	case CODEC_AK5558:
	case CODEC_AK5552:
	case CODEC_CS42888:
		return true;
	default:
		break;
	}
	return false;
}

static unsigned long akcodec_get_mclk_rate(struct snd_pcm_substream *substream,
					   struct snd_pcm_hw_params *params,
					   int slots, int slot_width)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct imx_card_data *data = snd_soc_card_get_drvdata(rtd->card);
	const struct imx_card_plat_data *plat_data = data->plat_data;
	struct dai_link_data *link_data = &data->link_data[rtd->id];
	unsigned int width = slots * slot_width;
	unsigned int rate = params_rate(params);
	int i;

	if (format_is_tdm(link_data)) {
		for (i = 0; i < plat_data->num_tdm_fs_mul; i++) {
			/* min = max = slots * slots_width */
			if (width != plat_data->tdm_fs_mul[i].min)
				continue;
			return rate * plat_data->tdm_fs_mul[i].mul;
		}
	} else {
		for (i = 0; i < plat_data->num_fs_mul; i++) {
			if (rate >= plat_data->fs_mul[i].rmin &&
			    rate <= plat_data->fs_mul[i].rmax) {
				width = max(width, plat_data->fs_mul[i].wmin);
				width = min(width, plat_data->fs_mul[i].wmax);

				/* Adjust SAI bclk:mclk ratio */
				width *= link_data->one2one_ratio ? 1 : 2;

				return rate * width;
			}
		}
	}

	/* Let DAI manage clk frequency by default */
	return 0;
}

static int imx_aif_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct snd_soc_card *card = rtd->card;
	struct imx_card_data *data = snd_soc_card_get_drvdata(card);
	struct dai_link_data *link_data = &data->link_data[rtd->id];
	struct imx_card_plat_data *plat_data = data->plat_data;
	struct device *dev = card->dev;
	struct snd_soc_dai *codec_dai;
	unsigned long mclk_freq;
	unsigned int fmt = rtd->dai_link->dai_fmt;
	unsigned int slots, slot_width;
	int ret, i;

	slots = link_data->slots;
	slot_width = link_data->slot_width;

	if (!format_is_tdm(link_data)) {
		if (format_is_dsd(params)) {
			slots = 1;
			slot_width = params_width(params);
			fmt = (rtd->dai_link->dai_fmt & ~SND_SOC_DAIFMT_FORMAT_MASK) |
			      SND_SOC_DAIFMT_PDM;
		} else {
			slots = 2;
			slot_width = params_physical_width(params);
			fmt = (rtd->dai_link->dai_fmt & ~SND_SOC_DAIFMT_FORMAT_MASK) |
			      SND_SOC_DAIFMT_I2S;
		}
	}

	ret = snd_soc_dai_set_fmt(cpu_dai, snd_soc_daifmt_clock_provider_flipped(fmt));
	if (ret && ret != -ENOTSUPP) {
		dev_err(dev, "failed to set cpu dai fmt: %d\n", ret);
		return ret;
	}
	ret = snd_soc_dai_set_tdm_slot(cpu_dai,
				       BIT(slots) - 1,
				       BIT(slots) - 1,
				       slots, slot_width);
	if (ret && ret != -ENOTSUPP) {
		dev_err(dev, "failed to set cpu dai tdm slot: %d\n", ret);
		return ret;
	}

	for_each_rtd_codec_dais(rtd, i, codec_dai) {
		ret = snd_soc_dai_set_fmt(codec_dai, fmt);
		if (ret && ret != -ENOTSUPP) {
			dev_err(dev, "failed to set codec dai[%d] fmt: %d\n", i, ret);
			return ret;
		}

		if (format_is_tdm(link_data)) {
			ret = snd_soc_dai_set_tdm_slot(codec_dai,
						       BIT(slots) - 1,
						       BIT(slots) - 1,
						       slots, slot_width);
			if (ret && ret != -ENOTSUPP) {
				dev_err(dev, "failed to set codec dai[%d] tdm slot: %d\n", i, ret);
				return ret;
			}
		}
	}

	/* Set MCLK freq */
	if (codec_is_akcodec(plat_data->type))
		mclk_freq = akcodec_get_mclk_rate(substream, params, slots, slot_width);
	else
		mclk_freq = params_rate(params) * slots * slot_width;

	if (format_is_dsd(params)) {
		/* Use the maximum freq from DSD512 (512*44100 = 22579200) */
		if (!(params_rate(params) % 11025))
			mclk_freq = IMX_CARD_MCLK_22P5792MHZ;
		else
			mclk_freq = IMX_CARD_MCLK_24P576MHZ;
	}

	ret = snd_soc_dai_set_sysclk(cpu_dai, link_data->cpu_sysclk_id, mclk_freq,
				     SND_SOC_CLOCK_OUT);
	if (ret && ret != -ENOTSUPP) {
		dev_err(dev, "failed to set cpui dai mclk1 rate (%lu): %d\n", mclk_freq, ret);
		return ret;
	}
	ret = snd_soc_dai_set_sysclk(codec_dai, 0, mclk_freq, SND_SOC_CLOCK_IN);
	if (ret && ret != -ENOTSUPP) {
		dev_err(dev, "failed to set codec dai mclk rate (%lu): %d\n", mclk_freq, ret);
		return ret;
	}

	return 0;
}

static int ak5558_hw_rule_rate(struct snd_pcm_hw_params *p, struct snd_pcm_hw_rule *r)
{
	struct dai_link_data *link_data = r->private;
	struct snd_interval t = { .min = 8000, .max = 8000, };
	unsigned long mclk_freq;
	unsigned int fs;
	int i;

	fs = hw_param_interval(p, SNDRV_PCM_HW_PARAM_SAMPLE_BITS)->min;
	fs *= link_data->slots;

	/* Identify maximum supported rate */
	for (i = 0; i < ARRAY_SIZE(akcodec_rates); i++) {
		mclk_freq = fs * akcodec_rates[i];
		/* Adjust SAI bclk:mclk ratio */
		mclk_freq *= link_data->one2one_ratio ? 1 : 2;

		/* Skip rates for which MCLK is beyond supported value */
		if (mclk_freq > 36864000)
			continue;

		if (t.max < akcodec_rates[i])
			t.max = akcodec_rates[i];
	}

	return snd_interval_refine(hw_param_interval(p, r->var), &t);
}

static int imx_aif_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_card *card = rtd->card;
	struct imx_card_data *data = snd_soc_card_get_drvdata(card);
	struct dai_link_data *link_data = &data->link_data[rtd->id];
	static struct snd_pcm_hw_constraint_list constraint_rates;
	static struct snd_pcm_hw_constraint_list constraint_channels;
	int ret = 0;

	if (format_is_tdm(link_data)) {
		constraint_channels.list = data->plat_data->support_tdm_channels;
		constraint_channels.count = data->plat_data->num_tdm_channels;
		constraint_rates.list = data->plat_data->support_tdm_rates;
		constraint_rates.count = data->plat_data->num_tdm_rates;
	} else {
		constraint_channels.list = data->plat_data->support_channels;
		constraint_channels.count = data->plat_data->num_channels;
		constraint_rates.list = data->plat_data->support_rates;
		constraint_rates.count = data->plat_data->num_rates;
	}

	if (constraint_channels.count) {
		ret = snd_pcm_hw_constraint_list(runtime, 0,
						 SNDRV_PCM_HW_PARAM_CHANNELS,
						 &constraint_channels);
		if (ret)
			return ret;
	}

	if (constraint_rates.count) {
		ret = snd_pcm_hw_constraint_list(runtime, 0,
						 SNDRV_PCM_HW_PARAM_RATE,
						 &constraint_rates);
		if (ret)
			return ret;
	}

	if (data->plat_data->type == CODEC_AK5558)
		ret = snd_pcm_hw_rule_add(substream->runtime, 0,
					  SNDRV_PCM_HW_PARAM_RATE,
					  ak5558_hw_rule_rate, link_data,
					  SNDRV_PCM_HW_PARAM_SAMPLE_BITS, -1);

	return ret;
}

static const struct snd_soc_ops imx_aif_ops = {
	.hw_params = imx_aif_hw_params,
	.startup = imx_aif_startup,
};

static const struct snd_soc_ops imx_aif_ops_be = {
	.hw_params = imx_aif_hw_params,
};

static int be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
			      struct snd_pcm_hw_params *params)
{
	struct snd_soc_card *card = rtd->card;
	struct imx_card_data *data = snd_soc_card_get_drvdata(card);
	struct snd_interval *rate;
	struct snd_mask *mask;

	rate = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	rate->max = data->asrc_rate;
	rate->min = data->asrc_rate;

	mask = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);
	snd_mask_none(mask);
	snd_mask_set(mask, (__force unsigned int)data->asrc_format);

	return 0;
}

static int imx_card_parse_of(struct imx_card_data *data)
{
	struct imx_card_plat_data *plat_data = data->plat_data;
	struct snd_soc_card *card = &data->card;
	struct snd_soc_dai_link_component *dlc;
	struct device_node *platform = NULL;
	struct device_node *codec = NULL;
	struct device_node *cpu = NULL;
	struct device_node *np;
	struct device *dev = card->dev;
	struct snd_soc_dai_link *link;
	struct dai_link_data *link_data;
	struct of_phandle_args args;
	bool playback_only, capture_only;
	int ret, num_links;
	u32 asrc_fmt = 0;
	u32 width;

	ret = snd_soc_of_parse_card_name(card, "model");
	if (ret) {
		dev_err(dev, "Error parsing card name: %d\n", ret);
		return ret;
	}

	/* DAPM routes */
	if (of_property_present(dev->of_node, "audio-routing")) {
		ret = snd_soc_of_parse_audio_routing(card, "audio-routing");
		if (ret)
			return ret;
	}

	/* Populate links */
	num_links = of_get_child_count(dev->of_node);

	/* Allocate the DAI link array */
	card->dai_link = devm_kcalloc(dev, num_links, sizeof(*link), GFP_KERNEL);
	if (!card->dai_link)
		return -ENOMEM;

	data->link_data = devm_kcalloc(dev, num_links, sizeof(*link_data), GFP_KERNEL);
	if (!data->link_data)
		return -ENOMEM;

	card->num_links = num_links;
	link = card->dai_link;
	link_data = data->link_data;

	for_each_child_of_node(dev->of_node, np) {
		dlc = devm_kzalloc(dev, 2 * sizeof(*dlc), GFP_KERNEL);
		if (!dlc) {
			ret = -ENOMEM;
			goto err_put_np;
		}

		link->cpus	= &dlc[0];
		link->platforms	= &dlc[1];

		link->num_cpus		= 1;
		link->num_platforms	= 1;

		ret = of_property_read_string(np, "link-name", &link->name);
		if (ret) {
			dev_err(card->dev, "error getting codec dai_link name\n");
			goto err_put_np;
		}

		cpu = of_get_child_by_name(np, "cpu");
		if (!cpu) {
			dev_err(dev, "%s: Can't find cpu DT node\n", link->name);
			ret = -EINVAL;
			goto err;
		}

		ret = snd_soc_of_get_dlc(cpu, &args, link->cpus, 0);
		if (ret) {
			dev_err_probe(card->dev, ret,
				      "%s: error getting cpu dai info\n", link->name);
			goto err;
		}

		if (of_node_name_eq(args.np, "sai")) {
			/* sai sysclk id */
			link_data->cpu_sysclk_id = FSL_SAI_CLK_MAST1;

			/* sai may support mclk/bclk = 1 */
			if (of_property_read_bool(np, "fsl,mclk-equal-bclk")) {
				link_data->one2one_ratio = true;
			} else {
				int i;

				/*
				 * i.MX8MQ don't support one2one ratio, then
				 * with ak4497 only 16bit case is supported.
				 */
				for (i = 0; i < ARRAY_SIZE(ak4497_fs_mul); i++) {
					if (ak4497_fs_mul[i].rmin == 705600 &&
					    ak4497_fs_mul[i].rmax == 768000) {
						ak4497_fs_mul[i].wmin = 32;
						ak4497_fs_mul[i].wmax = 32;
					}
				}
			}
		}

		link->platforms->of_node = link->cpus->of_node;
		link->id = args.args[0];

		codec = of_get_child_by_name(np, "codec");
		if (codec) {
			ret = snd_soc_of_get_dai_link_codecs(dev, codec, link);
			if (ret < 0) {
				dev_err_probe(dev, ret, "%s: codec dai not found\n",
						link->name);
				goto err;
			}

			plat_data->num_codecs = link->num_codecs;

			/* Check the akcodec type */
			if (!strcmp(link->codecs->dai_name, "ak4458-aif"))
				plat_data->type = CODEC_AK4458;
			else if (!strcmp(link->codecs->dai_name, "ak4497-aif"))
				plat_data->type = CODEC_AK4497;
			else if (!strcmp(link->codecs->dai_name, "ak5558-aif"))
				plat_data->type = CODEC_AK5558;
			else if (!strcmp(link->codecs->dai_name, "ak5552-aif"))
				plat_data->type = CODEC_AK5552;
			else if (!strcmp(link->codecs->dai_name, "cs42888"))
				plat_data->type = CODEC_CS42888;

		} else {
			link->codecs	 = &snd_soc_dummy_dlc;
			link->num_codecs = 1;
		}

		if (!strncmp(link->name, "HiFi-ASRC-FE", 12)) {
			/* DPCM frontend */
			link->dynamic = 1;
			link->dpcm_merged_chan = 1;

			ret = of_property_read_u32(args.np, "fsl,asrc-rate", &data->asrc_rate);
			if (ret) {
				dev_err(dev, "failed to get output rate\n");
				ret = -EINVAL;
				goto err;
			}

			ret = of_property_read_u32(args.np, "fsl,asrc-format", &asrc_fmt);
			data->asrc_format = (__force snd_pcm_format_t)asrc_fmt;
			if (ret) {
				/* Fallback to old binding; translate to asrc_format */
				ret = of_property_read_u32(args.np, "fsl,asrc-width", &width);
				if (ret) {
					dev_err(dev,
						"failed to decide output format\n");
					goto err;
				}

				if (width == 24)
					data->asrc_format = SNDRV_PCM_FORMAT_S24_LE;
				else
					data->asrc_format = SNDRV_PCM_FORMAT_S16_LE;
			}
		} else if (!strncmp(link->name, "HiFi-ASRC-BE", 12)) {
			/* DPCM backend */
			link->no_pcm = 1;
			link->platforms->of_node = NULL;
			link->platforms->name = "snd-soc-dummy";

			link->be_hw_params_fixup = be_hw_params_fixup;
			link->ops = &imx_aif_ops_be;
		} else {
			link->ops = &imx_aif_ops;
		}

		graph_util_parse_link_direction(np, &playback_only, &capture_only);
		link->playback_only = playback_only;
		link->capture_only = capture_only;

		/* Get dai fmt */
		ret = simple_util_parse_daifmt(dev, np, codec,
					       NULL, &link->dai_fmt);
		if (ret)
			link->dai_fmt = SND_SOC_DAIFMT_NB_NF |
					SND_SOC_DAIFMT_CBC_CFC |
					SND_SOC_DAIFMT_I2S;

		/* Get tdm slot */
		snd_soc_of_parse_tdm_slot(np, NULL, NULL,
					  &link_data->slots,
					  &link_data->slot_width);
		/* default value */
		if (!link_data->slots)
			link_data->slots = 2;

		if (!link_data->slot_width)
			link_data->slot_width = 32;

		link->ignore_pmdown_time = 1;
		link->stream_name = link->name;
		link++;
		link_data++;

		of_node_put(cpu);
		of_node_put(codec);
		of_node_put(platform);

		cpu = NULL;
		codec = NULL;
		platform = NULL;
	}

	return 0;
err:
	of_node_put(cpu);
	of_node_put(codec);
	of_node_put(platform);
err_put_np:
	of_node_put(np);
	return ret;
}

static int imx_card_probe(struct platform_device *pdev)
{
	struct snd_soc_dai_link *link_be = NULL, *link;
	struct imx_card_plat_data *plat_data;
	struct imx_card_data *data;
	int ret, i;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	plat_data = devm_kzalloc(&pdev->dev, sizeof(*plat_data), GFP_KERNEL);
	if (!plat_data)
		return -ENOMEM;

	data->plat_data = plat_data;
	data->card.dev = &pdev->dev;
	data->card.owner = THIS_MODULE;

	dev_set_drvdata(&pdev->dev, &data->card);
	snd_soc_card_set_drvdata(&data->card, data);
	ret = imx_card_parse_of(data);
	if (ret)
		return ret;

	data->num_dapm_routes = plat_data->num_codecs + 1;
	data->dapm_routes = devm_kcalloc(&pdev->dev, data->num_dapm_routes,
					 sizeof(struct snd_soc_dapm_route),
					 GFP_KERNEL);
	if (!data->dapm_routes)
		return -ENOMEM;

	/* configure the dapm routes */
	switch (plat_data->type) {
	case CODEC_AK4458:
	case CODEC_AK4497:
		if (plat_data->num_codecs == 1) {
			data->dapm_routes[0].sink = "Playback";
			data->dapm_routes[0].source = "CPU-Playback";
			i = 1;
		} else {
			for (i = 0; i < plat_data->num_codecs; i++) {
				data->dapm_routes[i].sink =
					devm_kasprintf(&pdev->dev, GFP_KERNEL, "%d %s",
						       i + 1, "Playback");
				if (!data->dapm_routes[i].sink)
					return -ENOMEM;
				data->dapm_routes[i].source = "CPU-Playback";
			}
		}
		data->dapm_routes[i].sink = "CPU-Playback";
		data->dapm_routes[i].source = "ASRC-Playback";
		break;
	case CODEC_AK5558:
	case CODEC_AK5552:
		if (plat_data->num_codecs == 1) {
			data->dapm_routes[0].sink = "CPU-Capture";
			data->dapm_routes[0].source = "Capture";
			i = 1;
		} else {
			for (i = 0; i < plat_data->num_codecs; i++) {
				data->dapm_routes[i].source =
					devm_kasprintf(&pdev->dev, GFP_KERNEL, "%d %s",
						       i + 1, "Capture");
				if (!data->dapm_routes[i].source)
					return -ENOMEM;
				data->dapm_routes[i].sink = "CPU-Capture";
			}
		}
		data->dapm_routes[i].sink = "ASRC-Capture";
		data->dapm_routes[i].source = "CPU-Capture";
		break;
	case CODEC_CS42888:
		data->dapm_routes[0].sink = "Playback";
		data->dapm_routes[0].source = "CPU-Playback";
		data->dapm_routes[1].sink = "CPU-Capture";
		data->dapm_routes[1].source = "Capture";
		break;
	default:
		break;
	}

	/* default platform data for akcodecs */
	if (codec_is_akcodec(plat_data->type)) {
		plat_data->support_rates = akcodec_rates;
		plat_data->num_rates = ARRAY_SIZE(akcodec_rates);
		plat_data->support_tdm_rates = akcodec_tdm_rates;
		plat_data->num_tdm_rates = ARRAY_SIZE(akcodec_tdm_rates);

		switch (plat_data->type) {
		case CODEC_AK4458:
			plat_data->fs_mul = ak4458_fs_mul;
			plat_data->num_fs_mul = ARRAY_SIZE(ak4458_fs_mul);
			plat_data->tdm_fs_mul = ak4458_tdm_fs_mul;
			plat_data->num_tdm_fs_mul = ARRAY_SIZE(ak4458_tdm_fs_mul);
			plat_data->support_channels = ak4458_channels;
			plat_data->num_channels = ARRAY_SIZE(ak4458_channels);
			plat_data->support_tdm_channels = ak4458_tdm_channels;
			plat_data->num_tdm_channels = ARRAY_SIZE(ak4458_tdm_channels);
			break;
		case CODEC_AK4497:
			plat_data->fs_mul = ak4497_fs_mul;
			plat_data->num_fs_mul = ARRAY_SIZE(ak4497_fs_mul);
			plat_data->support_channels = ak4458_channels;
			plat_data->num_channels = ARRAY_SIZE(ak4458_channels);
			break;
		case CODEC_AK5558:
		case CODEC_AK5552:
			plat_data->fs_mul = ak5558_fs_mul;
			plat_data->num_fs_mul = ARRAY_SIZE(ak5558_fs_mul);
			plat_data->tdm_fs_mul = ak5558_tdm_fs_mul;
			plat_data->num_tdm_fs_mul = ARRAY_SIZE(ak5558_tdm_fs_mul);
			plat_data->support_channels = ak5558_channels;
			plat_data->num_channels = ARRAY_SIZE(ak5558_channels);
			plat_data->support_tdm_channels = ak5558_tdm_channels;
			plat_data->num_tdm_channels = ARRAY_SIZE(ak5558_tdm_channels);
			break;
		case CODEC_CS42888:
			plat_data->fs_mul = cs42888_fs_mul;
			plat_data->num_fs_mul = ARRAY_SIZE(cs42888_fs_mul);
			plat_data->tdm_fs_mul = cs42888_tdm_fs_mul;
			plat_data->num_tdm_fs_mul = ARRAY_SIZE(cs42888_tdm_fs_mul);
			plat_data->support_channels = cs42888_channels;
			plat_data->num_channels = ARRAY_SIZE(cs42888_channels);
			plat_data->support_tdm_channels = cs42888_tdm_channels;
			plat_data->num_tdm_channels = ARRAY_SIZE(cs42888_tdm_channels);
			break;
		default:
			break;
		}
	}

	/* with asrc as front end */
	if (data->card.num_links == 3) {
		data->card.dapm_routes = data->dapm_routes;
		data->card.num_dapm_routes = data->num_dapm_routes;
		for_each_card_prelinks(&data->card, i, link) {
			if (link->no_pcm == 1)
				link_be = link;
		}
		for_each_card_prelinks(&data->card, i, link) {
			if (link->dynamic == 1 && link_be) {
				link->playback_only = link_be->playback_only;
				link->capture_only  = link_be->capture_only;
			}
		}
	}

	ret = devm_snd_soc_register_card(&pdev->dev, &data->card);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "snd_soc_register_card failed\n");

	return 0;
}

static const struct of_device_id imx_card_dt_ids[] = {
	{ .compatible = "fsl,imx-audio-card", },
	{ },
};
MODULE_DEVICE_TABLE(of, imx_card_dt_ids);

static struct platform_driver imx_card_driver = {
	.driver = {
		.name = "imx-card",
		.pm = &snd_soc_pm_ops,
		.of_match_table = imx_card_dt_ids,
	},
	.probe = imx_card_probe,
};
module_platform_driver(imx_card_driver);

MODULE_DESCRIPTION("Freescale i.MX ASoC Machine Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:imx-card");
