// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek ALSA SoC Audio DAI eTDM Control
 *
 * Copyright (c) 2023 MediaTek Inc.
 * Authors: Vic Wu <vic.wu@mediatek.com>
 *          Maso Huang <maso.huang@mediatek.com>
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/regmap.h>
#include <sound/pcm_params.h>
#include "mt7986-afe-common.h"
#include "mt7986-reg.h"

#define HOPPING_CLK  0
#define APLL_CLK     1
#define MTK_DAI_ETDM_FORMAT_I2S   0
#define MTK_DAI_ETDM_FORMAT_DSPA  4
#define MTK_DAI_ETDM_FORMAT_DSPB  5

enum {
	MTK_ETDM_RATE_8K = 0,
	MTK_ETDM_RATE_12K = 1,
	MTK_ETDM_RATE_16K = 2,
	MTK_ETDM_RATE_24K = 3,
	MTK_ETDM_RATE_32K = 4,
	MTK_ETDM_RATE_48K = 5,
	MTK_ETDM_RATE_96K = 7,
	MTK_ETDM_RATE_192K = 9,
	MTK_ETDM_RATE_11K = 16,
	MTK_ETDM_RATE_22K = 17,
	MTK_ETDM_RATE_44K = 18,
	MTK_ETDM_RATE_88K = 19,
	MTK_ETDM_RATE_176K = 20,
};

struct mtk_dai_etdm_priv {
	bool bck_inv;
	bool lrck_inv;
	bool slave_mode;
	unsigned int format;
};

static unsigned int mt7986_etdm_rate_transform(struct device *dev, unsigned int rate)
{
	switch (rate) {
	case 8000:
		return MTK_ETDM_RATE_8K;
	case 11025:
		return MTK_ETDM_RATE_11K;
	case 12000:
		return MTK_ETDM_RATE_12K;
	case 16000:
		return MTK_ETDM_RATE_16K;
	case 22050:
		return MTK_ETDM_RATE_22K;
	case 24000:
		return MTK_ETDM_RATE_24K;
	case 32000:
		return MTK_ETDM_RATE_32K;
	case 44100:
		return MTK_ETDM_RATE_44K;
	case 48000:
		return MTK_ETDM_RATE_48K;
	case 88200:
		return MTK_ETDM_RATE_88K;
	case 96000:
		return MTK_ETDM_RATE_96K;
	case 176400:
		return MTK_ETDM_RATE_176K;
	case 192000:
		return MTK_ETDM_RATE_192K;
	default:
		dev_warn(dev, "%s(), rate %u invalid, using %d!!!\n",
			 __func__, rate, MTK_ETDM_RATE_48K);
		return MTK_ETDM_RATE_48K;
	}
}

static int get_etdm_wlen(unsigned int bitwidth)
{
	return bitwidth <= 16 ? 16 : 32;
}

/* dai component */
/* interconnection */

static const struct snd_kcontrol_new o124_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I032_Switch", AFE_CONN124_1, 0, 1, 0),
};

static const struct snd_kcontrol_new o125_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I033_Switch", AFE_CONN125_1, 1, 1, 0),
};

static const struct snd_soc_dapm_widget mtk_dai_etdm_widgets[] = {

	/* DL */
	SND_SOC_DAPM_MIXER("I150", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I151", SND_SOC_NOPM, 0, 0, NULL, 0),
	/* UL */
	SND_SOC_DAPM_MIXER("O124", SND_SOC_NOPM, 0, 0, o124_mix, ARRAY_SIZE(o124_mix)),
	SND_SOC_DAPM_MIXER("O125", SND_SOC_NOPM, 0, 0, o125_mix, ARRAY_SIZE(o125_mix)),
};

static const struct snd_soc_dapm_route mtk_dai_etdm_routes[] = {
	{"I150", NULL, "ETDM Capture"},
	{"I151", NULL, "ETDM Capture"},
	{"ETDM Playback", NULL, "O124"},
	{"ETDM Playback", NULL, "O125"},
	{"O124", "I032_Switch", "I032"},
	{"O125", "I033_Switch", "I033"},
};

/* dai ops */
static int mtk_dai_etdm_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt7986_afe_private *afe_priv = afe->platform_priv;
	int ret;

	ret = clk_bulk_prepare_enable(afe_priv->num_clks, afe_priv->clks);
	if (ret)
		return dev_err_probe(afe->dev, ret, "Failed to enable clocks\n");

	regmap_update_bits(afe->regmap, AUDIO_TOP_CON2, CLK_OUT5_PDN_MASK, 0);
	regmap_update_bits(afe->regmap, AUDIO_TOP_CON2, CLK_IN5_PDN_MASK, 0);

	return 0;
}

static void mtk_dai_etdm_shutdown(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt7986_afe_private *afe_priv = afe->platform_priv;

	regmap_update_bits(afe->regmap, AUDIO_TOP_CON2, CLK_OUT5_PDN_MASK,
			   CLK_OUT5_PDN);
	regmap_update_bits(afe->regmap, AUDIO_TOP_CON2, CLK_IN5_PDN_MASK,
			   CLK_IN5_PDN);

	clk_bulk_disable_unprepare(afe_priv->num_clks, afe_priv->clks);
}

static unsigned int get_etdm_ch_fixup(unsigned int channels)
{
	if (channels > 16)
		return 24;
	else if (channels > 8)
		return 16;
	else if (channels > 4)
		return 8;
	else if (channels > 2)
		return 4;
	else
		return 2;
}

static int mtk_dai_etdm_config(struct mtk_base_afe *afe,
			       struct snd_pcm_hw_params *params,
			       struct snd_soc_dai *dai,
			       int stream)
{
	struct mt7986_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_etdm_priv *etdm_data = afe_priv->dai_priv[dai->id];
	unsigned int rate = params_rate(params);
	unsigned int etdm_rate = mt7986_etdm_rate_transform(afe->dev, rate);
	unsigned int afe_rate = mt7986_afe_rate_transform(afe->dev, rate);
	unsigned int channels = params_channels(params);
	unsigned int bit_width = params_width(params);
	unsigned int wlen = get_etdm_wlen(bit_width);
	unsigned int val = 0;
	unsigned int mask = 0;

	dev_dbg(afe->dev, "%s(), stream %d, rate %u, bitwidth %u\n",
		 __func__, stream, rate, bit_width);

	/* CON0 */
	mask |= ETDM_BIT_LEN_MASK;
	val |= FIELD_PREP(ETDM_BIT_LEN_MASK, bit_width - 1);
	mask |= ETDM_WRD_LEN_MASK;
	val |= FIELD_PREP(ETDM_WRD_LEN_MASK, wlen - 1);
	mask |= ETDM_FMT_MASK;
	val |= FIELD_PREP(ETDM_FMT_MASK, etdm_data->format);
	mask |= ETDM_CH_NUM_MASK;
	val |= FIELD_PREP(ETDM_CH_NUM_MASK, get_etdm_ch_fixup(channels) - 1);
	mask |= RELATCH_SRC_MASK;
	val |= FIELD_PREP(RELATCH_SRC_MASK, APLL_CLK);

	switch (stream) {
	case SNDRV_PCM_STREAM_PLAYBACK:
		/* set ETDM_OUT5_CON0 */
		regmap_update_bits(afe->regmap, ETDM_OUT5_CON0, mask, val);

		/* set ETDM_OUT5_CON4 */
		regmap_update_bits(afe->regmap, ETDM_OUT5_CON4,
				   OUT_RELATCH_MASK, OUT_RELATCH(afe_rate));
		regmap_update_bits(afe->regmap, ETDM_OUT5_CON4,
				   OUT_CLK_SRC_MASK, OUT_CLK_SRC(APLL_CLK));
		regmap_update_bits(afe->regmap, ETDM_OUT5_CON4,
				   OUT_SEL_FS_MASK, OUT_SEL_FS(etdm_rate));

		/* set ETDM_OUT5_CON5 */
		regmap_update_bits(afe->regmap, ETDM_OUT5_CON5,
				   ETDM_CLK_DIV_MASK, ETDM_CLK_DIV);
		break;
	case SNDRV_PCM_STREAM_CAPTURE:
		/* set ETDM_IN5_CON0 */
		regmap_update_bits(afe->regmap, ETDM_IN5_CON0, mask, val);
		regmap_update_bits(afe->regmap, ETDM_IN5_CON0,
				   ETDM_SYNC_MASK, ETDM_SYNC);

		/* set ETDM_IN5_CON2 */
		regmap_update_bits(afe->regmap, ETDM_IN5_CON2,
				   IN_CLK_SRC_MASK, IN_CLK_SRC(APLL_CLK));

		/* set ETDM_IN5_CON3 */
		regmap_update_bits(afe->regmap, ETDM_IN5_CON3,
				   IN_SEL_FS_MASK, IN_SEL_FS(etdm_rate));

		/* set ETDM_IN5_CON4 */
		regmap_update_bits(afe->regmap, ETDM_IN5_CON4,
				   IN_RELATCH_MASK, IN_RELATCH(afe_rate));
		break;
	default:
		break;
	}

	return 0;
}

static int mtk_dai_etdm_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	unsigned int rate = params_rate(params);
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);

	switch (rate) {
	case 8000:
	case 12000:
	case 16000:
	case 24000:
	case 32000:
	case 48000:
	case 96000:
	case 192000:
		mtk_dai_etdm_config(afe, params, dai, SNDRV_PCM_STREAM_PLAYBACK);
		mtk_dai_etdm_config(afe, params, dai, SNDRV_PCM_STREAM_CAPTURE);
		return 0;
	default:
		dev_err(afe->dev,
			"Sample rate %d invalid. Supported rates: 8/12/16/24/32/48/96/192 kHz\n",
			rate);
		return -EINVAL;
	}
}

static int mtk_dai_etdm_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);

	dev_dbg(afe->dev, "%s(), cmd %d, dai id %d\n", __func__, cmd, dai->id);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		regmap_update_bits(afe->regmap, ETDM_IN5_CON0, ETDM_EN_MASK,
				   ETDM_EN);
		regmap_update_bits(afe->regmap, ETDM_OUT5_CON0, ETDM_EN_MASK,
				   ETDM_EN);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		regmap_update_bits(afe->regmap, ETDM_IN5_CON0, ETDM_EN_MASK,
				   0);
		regmap_update_bits(afe->regmap, ETDM_OUT5_CON0, ETDM_EN_MASK,
				   0);
		break;
	default:
		break;
	}

	return 0;
}

static int mtk_dai_etdm_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt7986_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_etdm_priv *etdm_data;
	void *priv_data;

	switch (dai->id) {
	case MT7986_DAI_ETDM:
		break;
	default:
		dev_warn(afe->dev, "%s(), id %d not support\n",
			 __func__, dai->id);
		return -EINVAL;
	}

	priv_data = devm_kzalloc(afe->dev, sizeof(struct mtk_dai_etdm_priv),
				 GFP_KERNEL);
	if (!priv_data)
		return -ENOMEM;

	afe_priv->dai_priv[dai->id] = priv_data;
	etdm_data = afe_priv->dai_priv[dai->id];

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		etdm_data->format = MTK_DAI_ETDM_FORMAT_I2S;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		etdm_data->format = MTK_DAI_ETDM_FORMAT_DSPA;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		etdm_data->format = MTK_DAI_ETDM_FORMAT_DSPB;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		etdm_data->bck_inv = false;
		etdm_data->lrck_inv = false;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		etdm_data->bck_inv = false;
		etdm_data->lrck_inv = true;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		etdm_data->bck_inv = true;
		etdm_data->lrck_inv = false;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		etdm_data->bck_inv = true;
		etdm_data->lrck_inv = true;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		etdm_data->slave_mode = true;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		etdm_data->slave_mode = false;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct snd_soc_dai_ops mtk_dai_etdm_ops = {
	.startup = mtk_dai_etdm_startup,
	.shutdown = mtk_dai_etdm_shutdown,
	.hw_params = mtk_dai_etdm_hw_params,
	.trigger = mtk_dai_etdm_trigger,
	.set_fmt = mtk_dai_etdm_set_fmt,
};

/* dai driver */
#define MTK_ETDM_RATES (SNDRV_PCM_RATE_8000_48000 |\
			SNDRV_PCM_RATE_88200 |\
			SNDRV_PCM_RATE_96000 |\
			SNDRV_PCM_RATE_176400 |\
			SNDRV_PCM_RATE_192000)

#define MTK_ETDM_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			  SNDRV_PCM_FMTBIT_S24_LE |\
			  SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver mtk_dai_etdm_driver[] = {
	{
		.name = "ETDM",
		.id = MT7986_DAI_ETDM,
		.capture = {
			.stream_name = "ETDM Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_ETDM_RATES,
			.formats = MTK_ETDM_FORMATS,
		},
		.playback = {
			.stream_name = "ETDM Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_ETDM_RATES,
			.formats = MTK_ETDM_FORMATS,
		},
		.ops = &mtk_dai_etdm_ops,
		.symmetric_rate = 1,
		.symmetric_sample_bits = 1,
	},
};

int mt7986_dai_etdm_register(struct mtk_base_afe *afe)
{
	struct mtk_base_afe_dai *dai;

	dai = devm_kzalloc(afe->dev, sizeof(*dai), GFP_KERNEL);
	if (!dai)
		return -ENOMEM;

	list_add(&dai->list, &afe->sub_dais);

	dai->dai_drivers = mtk_dai_etdm_driver;
	dai->num_dai_drivers = ARRAY_SIZE(mtk_dai_etdm_driver);

	dai->dapm_widgets = mtk_dai_etdm_widgets;
	dai->num_dapm_widgets = ARRAY_SIZE(mtk_dai_etdm_widgets);
	dai->dapm_routes = mtk_dai_etdm_routes;
	dai->num_dapm_routes = ARRAY_SIZE(mtk_dai_etdm_routes);

	return 0;
}
