// SPDX-License-Identifier: GPL-2.0
/*
 * Airoha ALSA SoC Audio DAI eTDM Control
 *
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/regmap.h>
#include <sound/pcm_params.h>
#include "an7581-afe-common.h"
#include "an7581-reg.h"

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
	MTK_ETDM_RATE_96K = 6,
	MTK_ETDM_RATE_192K = 7,
	MTK_ETDM_RATE_384K = 8,
	MTK_ETDM_RATE_7K = 16,
	MTK_ETDM_RATE_11K = 17,
	MTK_ETDM_RATE_14K = 18,
	MTK_ETDM_RATE_22K = 19,
	MTK_ETDM_RATE_29K = 20,
	MTK_ETDM_RATE_44K = 21,
	MTK_ETDM_RATE_88K = 22,
	MTK_ETDM_RATE_176K = 23,
	MTK_ETDM_RATE_352K = 24,
};

struct mtk_dai_etdm_priv {
	bool bck_inv;
	bool lrck_inv;
	bool slave_mode;
	unsigned int format;
};

static unsigned int an7581_etdm_rate_transform(struct device *dev, unsigned int rate)
{
	switch (rate) {
	case 7350:
		return MTK_ETDM_RATE_7K;
	case 8000:
		return MTK_ETDM_RATE_8K;
	case 11025:
		return MTK_ETDM_RATE_11K;
	case 12000:
		return MTK_ETDM_RATE_12K;
	case 14700:
		return MTK_ETDM_RATE_14K;
	case 16000:
		return MTK_ETDM_RATE_16K;
	case 22050:
		return MTK_ETDM_RATE_22K;
	case 24000:
		return MTK_ETDM_RATE_24K;
	case 29400:
		return MTK_ETDM_RATE_29K;
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
	case 352800:
		return MTK_ETDM_RATE_352K;
	case 384000:
		return MTK_ETDM_RATE_384K;
	default:
		dev_warn_ratelimited(dev, "%s(), rate %u invalid, using %d!\n",
				     __func__, rate, MTK_ETDM_RATE_48K);
		return MTK_ETDM_RATE_48K;
	}
}

static int get_etdm_wlen(unsigned int bitwidth)
{
	return bitwidth <= 16 ? 16 : 32;
}

static const struct snd_soc_dapm_widget mtk_dai_etdm_widgets[] = {
	/* DL */
	SND_SOC_DAPM_MIXER("I150", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I151", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* UL */
	SND_SOC_DAPM_MIXER("O124", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("O125", SND_SOC_NOPM, 0, 0, NULL, 0),
};

static const struct snd_soc_dapm_route mtk_dai_etdm_routes[] = {
	{"I150", NULL, "ETDM Capture"},
	{"I151", NULL, "ETDM Capture"},
	{"ETDM Playback", NULL, "O124"},
	{"ETDM Playback", NULL, "O125"},
	{"O124", NULL, "I032"},
	{"O125", NULL, "I033"},
};

/* dai ops */
static int mtk_dai_etdm_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct an7581_afe_private *afe_priv = afe->platform_priv;

	guard(mutex)(&afe_priv->user_lock);

	if (!afe_priv->users)
		regmap_set_bits(afe->regmap, AFE_DAC_CON0,
				BIT(AFE_AFE_ENABLE_SHIFT));
	afe_priv->users++;

	return 0;
}

static void mtk_dai_etdm_shutdown(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct an7581_afe_private *afe_priv = afe->platform_priv;

	guard(mutex)(&afe_priv->user_lock);

	afe_priv->users--;
	if (!afe_priv->users)
		regmap_clear_bits(afe->regmap, AFE_DAC_CON0,
				  BIT(AFE_AFE_ENABLE_SHIFT));
}

static unsigned int get_etdm_ch_fixup(unsigned int channels)
{
	if (channels > 16)
		return 24;

	if (channels > 8)
		return 16;

	if (channels > 4)
		return 8;

	if (channels > 2)
		return 4;

	return 2;
}

static int mtk_dai_etdm_config(struct mtk_base_afe *afe,
			       struct snd_pcm_hw_params *params,
			       struct snd_soc_dai *dai,
			       int stream)
{
	struct an7581_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_etdm_priv *etdm_data = afe_priv->dai_priv[dai->id];
	unsigned int rate = params_rate(params);
	unsigned int etdm_rate = an7581_etdm_rate_transform(afe->dev, rate);
	unsigned int bit_width = params_width(params);
	unsigned int mask, mask1;
	unsigned int val, val1;

	dev_dbg(afe->dev, "%s(), stream %d, rate %u, bitwidth %u\n",
		__func__, stream, rate, params_width(params));

	/* CON0 */
	mask = ETDM_SLAVE_MODE | ETDM_BIT_LEN | ETDM_WRD_LEN |
	       ETDM_FMT | ETDM_CH_NUM;
	val = FIELD_PREP(ETDM_BIT_LEN, params_width(params) - 1) |
	      FIELD_PREP(ETDM_WRD_LEN, get_etdm_wlen(bit_width) - 1) |
	      FIELD_PREP(ETDM_FMT, etdm_data->format) |
	      FIELD_PREP(ETDM_CH_NUM,
			 get_etdm_ch_fixup(params_channels(params)) - 1);
	if (etdm_data->slave_mode)
		val |= ETDM_SLAVE_MODE;

	/* CON1 */
	mask1 = EDTM_LRCK_AUTO_MODE | EDTM_CKEN_SEL | EDTM_LRCK_AUTO_OFF |
		 EDTM_INITIAL_POINT | EDTM_INITIAL_COUNT;
	val1 = EDTM_LRCK_AUTO_MODE | EDTM_CKEN_SEL | EDTM_LRCK_AUTO_OFF |
		FIELD_PREP(EDTM_INITIAL_POINT, 14) | FIELD_PREP(EDTM_INITIAL_COUNT, 14);

	switch (stream) {
	case SNDRV_PCM_STREAM_PLAYBACK:
		regmap_update_bits(afe->regmap, ETDM_OUT1_CON0, mask, val);

		mask1 |= EDTM_DIRECT_INPUT_MASTER_BCK;
		val1 |= EDTM_DIRECT_INPUT_MASTER_BCK;

		regmap_update_bits(afe->regmap, ETDM_OUT1_CON1, mask1, val1);

		regmap_update_bits(afe->regmap, ETDM_OUT1_CON4, OUT_SEL_FS,
				   FIELD_PREP(OUT_SEL_FS, etdm_rate));

		regmap_update_bits(afe->irqs[AN7581_IRQ_0].regmap,
				   afe->irqs[AN7581_IRQ_0].irq_data->irq_en_reg,
				   AFE_IRQ_EN_SEL, AFE_IRQ_EN_SEL_I2SOUT);
		break;
	case SNDRV_PCM_STREAM_CAPTURE:
		regmap_update_bits(afe->regmap, ETDM_IN1_CON0, mask, val);

		regmap_update_bits(afe->regmap, ETDM_IN1_CON1, mask1, val1);

		regmap_update_bits(afe->regmap, ETDM_IN1_CON3, IN_SEL_FS,
				   FIELD_PREP(IN_SEL_FS, etdm_rate));

		regmap_update_bits(afe->irqs[AN7581_IRQ_1].regmap,
				   afe->irqs[AN7581_IRQ_1].irq_data->irq_en_reg,
				   AFE_IRQ_EN_SEL, AFE_IRQ_EN_SEL_I2SIN);
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

	regmap_update_bits(afe->regmap, ETDM_COWORK_CON0,
			   EDTM_IN1_SLAVE_SEL,
			   EDTM_IN1_SLAVE_FROM_ETDMIN1_SLAVE);
	regmap_update_bits(afe->regmap, ETDM_COWORK_CON0,
			   EDTM_OUT1_SLAVE_SEL,
			   EDTM_OUT1_SLAVE_FROM_ETDMOUT1_SLAVE);
	regmap_update_bits(afe->regmap, ETDM_COWORK_CON1,
			   EDTM_IN1_SDATA0_SEL,
			   EDTM_IN1_SDATA0_FROM_PAD);

	switch (rate) {
	case 7350:
	case 8000:
	case 11025:
	case 12000:
	case 14700:
	case 16000:
	case 22050:
	case 24000:
	case 29400:
	case 32000:
	case 44100:
	case 48000:
	case 88200:
	case 96000:
	case 176400:
	case 192000:
	case 352800:
	case 384000:
		return mtk_dai_etdm_config(afe, params, dai, substream->stream);
	default:
		dev_err(afe->dev, "Sample rate %d invalid\n", rate);
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
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			regmap_set_bits(afe->regmap, ETDM_OUT1_CON0,
					ETDM_OUT_EN);

		if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
			regmap_set_bits(afe->regmap, ETDM_IN1_CON0,
					ETDM_IN_EN);

		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			regmap_clear_bits(afe->regmap, ETDM_OUT1_CON0,
					  ETDM_OUT_EN);

		if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
			regmap_clear_bits(afe->regmap, ETDM_IN1_CON0,
					  ETDM_IN_EN);

		break;
	default:
		break;
	}

	return 0;
}

static int mtk_dai_etdm_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct an7581_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_etdm_priv *etdm_data;
	void *priv_data;

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

	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_BP_FP:
		etdm_data->slave_mode = false;
		break;
	case SND_SOC_DAIFMT_BC_FC:
		etdm_data->slave_mode = true;
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
#define MTK_ETDM_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			  SNDRV_PCM_FMTBIT_S24_LE |\
			  SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver mtk_dai_etdm_driver[] = {
	{
		.name = "ETDM",
		.id = AN7581_DAI_ETDM,
		.capture = {
			.stream_name = "ETDM Capture",
			.channels_min = 1,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = MTK_ETDM_FORMATS,
		},
		.playback = {
			.stream_name = "ETDM Playback",
			.channels_min = 1,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = MTK_ETDM_FORMATS,
		},
		.ops = &mtk_dai_etdm_ops,
		.symmetric_rate = 1,
		.symmetric_sample_bits = 1,
	},
};

int an7581_dai_etdm_register(struct mtk_base_afe *afe)
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
