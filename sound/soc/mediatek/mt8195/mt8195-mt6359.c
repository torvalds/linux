// SPDX-License-Identifier: GPL-2.0
/*
 * mt8195-mt6359.c  --
 *	MT8195-MT6359 ALSA SoC machine driver code
 *
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Trevor Wu <trevor.wu@mediatek.com>
 *	   YC Hung <yc.hung@mediatek.com>
 */

#include <linux/input.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <sound/jack.h>
#include <sound/pcm_params.h>
#include <sound/rt5682.h>
#include <sound/soc.h>
#include "../../codecs/mt6359.h"
#include "../../codecs/rt1011.h"
#include "../../codecs/rt5682.h"
#include "../common/mtk-afe-platform-driver.h"
#include "../common/mtk-dsp-sof-common.h"
#include "../common/mtk-soc-card.h"
#include "../common/mtk-soundcard-driver.h"
#include "mt8195-afe-clk.h"
#include "mt8195-afe-common.h"

#define RT1011_SPEAKER_AMP_PRESENT		BIT(0)
#define RT1019_SPEAKER_AMP_PRESENT		BIT(1)
#define MAX98390_SPEAKER_AMP_PRESENT		BIT(2)

#define DUMB_CODEC_INIT				BIT(0)
#define MT6359_CODEC_INIT			BIT(1)
#define RT1011_CODEC_INIT			BIT(2)
#define RT1019_CODEC_INIT			BIT(3)
#define MAX98390_CODEC_INIT			BIT(4)
#define RT5682_CODEC_INIT			BIT(5)

#define RT1011_CODEC_DAI	"rt1011-aif"
#define RT1011_DEV0_NAME	"rt1011.2-0038"
#define RT1011_DEV1_NAME	"rt1011.2-0039"

#define RT1019_CODEC_DAI	"HiFi"
#define RT1019_DEV0_NAME	"rt1019p"

#define MAX98390_CODEC_DAI	"max98390-aif1"
#define MAX98390_DEV0_NAME	"max98390.2-0038" /* right */
#define MAX98390_DEV1_NAME	"max98390.2-0039" /* left */

#define RT5682_CODEC_DAI	"rt5682-aif1"
#define RT5682_DEV0_NAME	"rt5682.2-001a"

#define RT5682S_CODEC_DAI	"rt5682s-aif1"
#define RT5682S_DEV0_NAME	"rt5682s.2-001a"

#define SOF_DMA_DL2 "SOF_DMA_DL2"
#define SOF_DMA_DL3 "SOF_DMA_DL3"
#define SOF_DMA_UL4 "SOF_DMA_UL4"
#define SOF_DMA_UL5 "SOF_DMA_UL5"

struct mt8195_mt6359_priv {
	struct clk *i2so1_mclk;
};

enum mt8195_jacks {
	MT8195_JACK_HEADSET,
	MT8195_JACK_DP,
	MT8195_JACK_HDMI,
	MT8195_JACK_MAX,
};

/* Headset jack detection DAPM pins */
static struct snd_soc_jack_pin mt8195_jack_pins[] = {
	{
		.pin = "Headphone",
		.mask = SND_JACK_HEADPHONE,
	},
	{
		.pin = "Headset Mic",
		.mask = SND_JACK_MICROPHONE,
	},
};

static const struct snd_soc_dapm_widget mt8195_mt6359_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIXER(SOF_DMA_DL2, SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER(SOF_DMA_DL3, SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER(SOF_DMA_UL4, SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER(SOF_DMA_UL5, SND_SOC_NOPM, 0, 0, NULL, 0),
};

static const struct snd_soc_dapm_route mt8195_mt6359_routes[] = {
	/* headset */
	{ "Headphone", NULL, "HPOL" },
	{ "Headphone", NULL, "HPOR" },
	{ "IN1P", NULL, "Headset Mic" },
	/* SOF Uplink */
	{SOF_DMA_UL4, NULL, "O034"},
	{SOF_DMA_UL4, NULL, "O035"},
	{SOF_DMA_UL5, NULL, "O036"},
	{SOF_DMA_UL5, NULL, "O037"},
	/* SOF Downlink */
	{"I070", NULL, SOF_DMA_DL2},
	{"I071", NULL, SOF_DMA_DL2},
	{"I020", NULL, SOF_DMA_DL3},
	{"I021", NULL, SOF_DMA_DL3},
};

static const struct snd_kcontrol_new mt8195_mt6359_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
};

static const struct snd_soc_dapm_widget mt8195_dual_speaker_widgets[] = {
	SND_SOC_DAPM_SPK("Left Spk", NULL),
	SND_SOC_DAPM_SPK("Right Spk", NULL),
};

static const struct snd_kcontrol_new mt8195_dual_speaker_controls[] = {
	SOC_DAPM_PIN_SWITCH("Left Spk"),
	SOC_DAPM_PIN_SWITCH("Right Spk"),
};

static const struct snd_soc_dapm_widget mt8195_speaker_widgets[] = {
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
};

static const struct snd_kcontrol_new mt8195_speaker_controls[] = {
	SOC_DAPM_PIN_SWITCH("Ext Spk"),
};

static const struct snd_soc_dapm_route mt8195_rt1011_routes[] = {
	{ "Left Spk", NULL, "Left SPO" },
	{ "Right Spk", NULL, "Right SPO" },
};

static const struct snd_soc_dapm_route mt8195_rt1019_routes[] = {
	{ "Ext Spk", NULL, "Speaker" },
};

static const struct snd_soc_dapm_route mt8195_max98390_routes[] = {
	{ "Left Spk", NULL, "Left BE_OUT" },
	{ "Right Spk", NULL, "Right BE_OUT" },
};

#define CKSYS_AUD_TOP_CFG 0x032c
#define CKSYS_AUD_TOP_MON 0x0330

static int mt8195_mt6359_mtkaif_calibration(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *cmpnt_afe =
		snd_soc_rtdcom_lookup(rtd, AFE_PCM_NAME);
	struct snd_soc_component *cmpnt_codec =
		snd_soc_rtd_to_codec(rtd, 0)->component;
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt_afe);
	struct mt8195_afe_private *afe_priv = afe->platform_priv;
	struct mtkaif_param *param = &afe_priv->mtkaif_params;
	int chosen_phase_1, chosen_phase_2, chosen_phase_3;
	int prev_cycle_1, prev_cycle_2, prev_cycle_3;
	int test_done_1, test_done_2, test_done_3;
	int cycle_1, cycle_2, cycle_3;
	int mtkaif_chosen_phase[MT8195_MTKAIF_MISO_NUM];
	int mtkaif_phase_cycle[MT8195_MTKAIF_MISO_NUM];
	int mtkaif_calibration_num_phase;
	bool mtkaif_calibration_ok;
	unsigned int monitor = 0;
	int counter;
	int phase;
	int i;

	dev_dbg(afe->dev, "%s(), start\n", __func__);

	param->mtkaif_calibration_ok = false;
	for (i = 0; i < MT8195_MTKAIF_MISO_NUM; i++) {
		param->mtkaif_chosen_phase[i] = -1;
		param->mtkaif_phase_cycle[i] = 0;
		mtkaif_chosen_phase[i] = -1;
		mtkaif_phase_cycle[i] = 0;
	}

	if (IS_ERR(afe_priv->topckgen)) {
		dev_info(afe->dev, "%s() Cannot find topckgen controller\n",
			 __func__);
		return 0;
	}

	pm_runtime_get_sync(afe->dev);
	mt6359_mtkaif_calibration_enable(cmpnt_codec);

	/* set test type to synchronizer pulse */
	regmap_update_bits(afe_priv->topckgen,
			   CKSYS_AUD_TOP_CFG, 0xffff, 0x4);
	mtkaif_calibration_num_phase = 42;	/* mt6359: 0 ~ 42 */
	mtkaif_calibration_ok = true;

	for (phase = 0;
	     phase <= mtkaif_calibration_num_phase && mtkaif_calibration_ok;
	     phase++) {
		mt6359_set_mtkaif_calibration_phase(cmpnt_codec,
						    phase, phase, phase);

		regmap_update_bits(afe_priv->topckgen,
				   CKSYS_AUD_TOP_CFG, 0x1, 0x1);

		test_done_1 = 0;
		test_done_2 = 0;
		test_done_3 = 0;
		cycle_1 = -1;
		cycle_2 = -1;
		cycle_3 = -1;
		counter = 0;
		while (!(test_done_1 & test_done_2 & test_done_3)) {
			regmap_read(afe_priv->topckgen,
				    CKSYS_AUD_TOP_MON, &monitor);
			test_done_1 = (monitor >> 28) & 0x1;
			test_done_2 = (monitor >> 29) & 0x1;
			test_done_3 = (monitor >> 30) & 0x1;
			if (test_done_1 == 1)
				cycle_1 = monitor & 0xf;

			if (test_done_2 == 1)
				cycle_2 = (monitor >> 4) & 0xf;

			if (test_done_3 == 1)
				cycle_3 = (monitor >> 8) & 0xf;

			/* handle if never test done */
			if (++counter > 10000) {
				dev_info(afe->dev, "%s(), test fail, cycle_1 %d, cycle_2 %d, cycle_3 %d, monitor 0x%x\n",
					 __func__,
					 cycle_1, cycle_2, cycle_3, monitor);
				mtkaif_calibration_ok = false;
				break;
			}
		}

		if (phase == 0) {
			prev_cycle_1 = cycle_1;
			prev_cycle_2 = cycle_2;
			prev_cycle_3 = cycle_3;
		}

		if (cycle_1 != prev_cycle_1 &&
		    mtkaif_chosen_phase[MT8195_MTKAIF_MISO_0] < 0) {
			mtkaif_chosen_phase[MT8195_MTKAIF_MISO_0] = phase - 1;
			mtkaif_phase_cycle[MT8195_MTKAIF_MISO_0] = prev_cycle_1;
		}

		if (cycle_2 != prev_cycle_2 &&
		    mtkaif_chosen_phase[MT8195_MTKAIF_MISO_1] < 0) {
			mtkaif_chosen_phase[MT8195_MTKAIF_MISO_1] = phase - 1;
			mtkaif_phase_cycle[MT8195_MTKAIF_MISO_1] = prev_cycle_2;
		}

		if (cycle_3 != prev_cycle_3 &&
		    mtkaif_chosen_phase[MT8195_MTKAIF_MISO_2] < 0) {
			mtkaif_chosen_phase[MT8195_MTKAIF_MISO_2] = phase - 1;
			mtkaif_phase_cycle[MT8195_MTKAIF_MISO_2] = prev_cycle_3;
		}

		regmap_update_bits(afe_priv->topckgen,
				   CKSYS_AUD_TOP_CFG, 0x1, 0x0);

		if (mtkaif_chosen_phase[MT8195_MTKAIF_MISO_0] >= 0 &&
		    mtkaif_chosen_phase[MT8195_MTKAIF_MISO_1] >= 0 &&
		    mtkaif_chosen_phase[MT8195_MTKAIF_MISO_2] >= 0)
			break;
	}

	if (mtkaif_chosen_phase[MT8195_MTKAIF_MISO_0] < 0) {
		mtkaif_calibration_ok = false;
		chosen_phase_1 = 0;
	} else {
		chosen_phase_1 = mtkaif_chosen_phase[MT8195_MTKAIF_MISO_0];
	}

	if (mtkaif_chosen_phase[MT8195_MTKAIF_MISO_1] < 0) {
		mtkaif_calibration_ok = false;
		chosen_phase_2 = 0;
	} else {
		chosen_phase_2 = mtkaif_chosen_phase[MT8195_MTKAIF_MISO_1];
	}

	if (mtkaif_chosen_phase[MT8195_MTKAIF_MISO_2] < 0) {
		mtkaif_calibration_ok = false;
		chosen_phase_3 = 0;
	} else {
		chosen_phase_3 = mtkaif_chosen_phase[MT8195_MTKAIF_MISO_2];
	}

	mt6359_set_mtkaif_calibration_phase(cmpnt_codec,
					    chosen_phase_1,
					    chosen_phase_2,
					    chosen_phase_3);

	mt6359_mtkaif_calibration_disable(cmpnt_codec);
	pm_runtime_put(afe->dev);

	param->mtkaif_calibration_ok = mtkaif_calibration_ok;
	param->mtkaif_chosen_phase[MT8195_MTKAIF_MISO_0] = chosen_phase_1;
	param->mtkaif_chosen_phase[MT8195_MTKAIF_MISO_1] = chosen_phase_2;
	param->mtkaif_chosen_phase[MT8195_MTKAIF_MISO_2] = chosen_phase_3;
	for (i = 0; i < MT8195_MTKAIF_MISO_NUM; i++)
		param->mtkaif_phase_cycle[i] = mtkaif_phase_cycle[i];

	dev_info(afe->dev, "%s(), end, calibration ok %d\n",
		 __func__, param->mtkaif_calibration_ok);

	return 0;
}

static int mt8195_mt6359_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *cmpnt_codec =
		snd_soc_rtd_to_codec(rtd, 0)->component;

	/* set mtkaif protocol */
	mt6359_set_mtkaif_protocol(cmpnt_codec,
				   MT6359_MTKAIF_PROTOCOL_2_CLK_P2);

	/* mtkaif calibration */
	mt8195_mt6359_mtkaif_calibration(rtd);

	return 0;
}

static int mt8195_hdmitx_dptx_startup(struct snd_pcm_substream *substream)
{
	return mtk_soundcard_startup(substream, MTK_CONSTRAINT_HDMIDP);
}

static const struct snd_soc_ops mt8195_hdmitx_dptx_playback_ops = {
	.startup = mt8195_hdmitx_dptx_startup,
};

static int mt8195_dptx_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);

	return snd_soc_dai_set_sysclk(cpu_dai, 0, params_rate(params) * 256,
				      SND_SOC_CLOCK_OUT);
}

static const struct snd_soc_ops mt8195_dptx_ops = {
	.hw_params = mt8195_dptx_hw_params,
};

static int mt8195_dptx_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	struct mtk_soc_card_data *soc_card_data = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_jack *jack = &soc_card_data->card_data->jacks[MT8195_JACK_DP];
	struct snd_soc_component *cmpnt_codec =
		snd_soc_rtd_to_codec(rtd, 0)->component;
	int ret;

	ret = snd_soc_card_jack_new(rtd->card, "DP Jack", SND_JACK_LINEOUT, jack);
	if (ret)
		return ret;

	return snd_soc_component_set_jack(cmpnt_codec, jack, NULL);
}

static int mt8195_hdmi_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	struct mtk_soc_card_data *soc_card_data = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_jack *jack = &soc_card_data->card_data->jacks[MT8195_JACK_HDMI];
	struct snd_soc_component *cmpnt_codec =
		snd_soc_rtd_to_codec(rtd, 0)->component;
	int ret;

	ret = snd_soc_card_jack_new(rtd->card, "HDMI Jack", SND_JACK_LINEOUT, jack);
	if (ret)
		return ret;

	return snd_soc_component_set_jack(cmpnt_codec, jack, NULL);
}

static int mt8195_dptx_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				       struct snd_pcm_hw_params *params)
{
	/* fix BE i2s format to S24_LE, clean param mask first */
	snd_mask_reset_range(hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT),
			     0, (__force unsigned int)SNDRV_PCM_FORMAT_LAST);

	params_set_format(params, SNDRV_PCM_FORMAT_S24_LE);

	return 0;
}

static int mt8195_rt5682_etdm_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	unsigned int rate = params_rate(params);
	int bitwidth;
	int ret;

	bitwidth = snd_pcm_format_width(params_format(params));
	if (bitwidth < 0) {
		dev_err(card->dev, "invalid bit width: %d\n", bitwidth);
		return bitwidth;
	}

	ret = snd_soc_dai_set_tdm_slot(codec_dai, 0x00, 0x0, 0x2, bitwidth);
	if (ret) {
		dev_err(card->dev, "failed to set tdm slot\n");
		return ret;
	}

	ret = snd_soc_dai_set_pll(codec_dai, RT5682_PLL1, RT5682_PLL1_S_MCLK,
				  rate * 256, rate * 512);
	if (ret) {
		dev_err(card->dev, "failed to set pll\n");
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(codec_dai, RT5682_SCLK_S_PLL1,
				     rate * 512, SND_SOC_CLOCK_IN);
	if (ret) {
		dev_err(card->dev, "failed to set sysclk\n");
		return ret;
	}

	return snd_soc_dai_set_sysclk(cpu_dai, 0, rate * 256,
				      SND_SOC_CLOCK_OUT);
}

static const struct snd_soc_ops mt8195_rt5682_etdm_ops = {
	.hw_params = mt8195_rt5682_etdm_hw_params,
};

static int mt8195_rt5682_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *cmpnt_codec =
		snd_soc_rtd_to_codec(rtd, 0)->component;
	struct mtk_soc_card_data *soc_card_data = snd_soc_card_get_drvdata(rtd->card);
	struct mt8195_mt6359_priv *priv = soc_card_data->mach_priv;
	struct snd_soc_jack *jack = &soc_card_data->card_data->jacks[MT8195_JACK_HEADSET];
	struct snd_soc_component *cmpnt_afe =
		snd_soc_rtdcom_lookup(rtd, AFE_PCM_NAME);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt_afe);
	struct mt8195_afe_private *afe_priv = afe->platform_priv;
	int ret;

	priv->i2so1_mclk = afe_priv->clk[MT8195_CLK_TOP_APLL12_DIV2];

	ret = snd_soc_card_jack_new_pins(rtd->card, "Headset Jack",
				    SND_JACK_HEADSET | SND_JACK_BTN_0 |
				    SND_JACK_BTN_1 | SND_JACK_BTN_2 |
				    SND_JACK_BTN_3,
				    jack, mt8195_jack_pins,
				    ARRAY_SIZE(mt8195_jack_pins));
	if (ret) {
		dev_err(rtd->dev, "Headset Jack creation failed: %d\n", ret);
		return ret;
	}

	snd_jack_set_key(jack->jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_1, KEY_VOICECOMMAND);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_2, KEY_VOLUMEUP);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_3, KEY_VOLUMEDOWN);

	ret = snd_soc_component_set_jack(cmpnt_codec, jack, NULL);
	if (ret) {
		dev_err(rtd->dev, "Headset Jack set failed: %d\n", ret);
		return ret;
	}

	return 0;
};

static int mt8195_rt1011_etdm_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai;
	struct snd_soc_card *card = rtd->card;
	int srate, i, ret;

	srate = params_rate(params);

	for_each_rtd_codec_dais(rtd, i, codec_dai) {
		ret = snd_soc_dai_set_pll(codec_dai, 0, RT1011_PLL1_S_BCLK,
					  64 * srate, 256 * srate);
		if (ret < 0) {
			dev_err(card->dev, "codec_dai clock not set\n");
			return ret;
		}

		ret = snd_soc_dai_set_sysclk(codec_dai,
					     RT1011_FS_SYS_PRE_S_PLL1,
					     256 * srate, SND_SOC_CLOCK_IN);
		if (ret < 0) {
			dev_err(card->dev, "codec_dai clock not set\n");
			return ret;
		}
	}
	return 0;
}

static const struct snd_soc_ops mt8195_rt1011_etdm_ops = {
	.hw_params = mt8195_rt1011_etdm_hw_params,
};

static int mt8195_sof_be_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_component *cmpnt_afe = NULL;
	struct snd_soc_pcm_runtime *runtime;

	/* find afe component */
	for_each_card_rtds(rtd->card, runtime) {
		cmpnt_afe = snd_soc_rtdcom_lookup(runtime, AFE_PCM_NAME);
		if (cmpnt_afe)
			break;
	}

	if (cmpnt_afe && !pm_runtime_active(cmpnt_afe->dev)) {
		dev_err(rtd->dev, "afe pm runtime is not active!!\n");
		return -EINVAL;
	}

	return 0;
}

static const struct snd_soc_ops mt8195_sof_be_ops = {
	.hw_params = mt8195_sof_be_hw_params,
};

static int mt8195_rt1011_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	int ret;

	ret = snd_soc_dapm_new_controls(&card->dapm, mt8195_dual_speaker_widgets,
					ARRAY_SIZE(mt8195_dual_speaker_widgets));
	if (ret) {
		dev_err(rtd->dev, "unable to add dapm controls, ret %d\n", ret);
		/* Don't need to add routes if widget addition failed */
		return ret;
	}

	ret = snd_soc_add_card_controls(card, mt8195_dual_speaker_controls,
					ARRAY_SIZE(mt8195_dual_speaker_controls));
	if (ret) {
		dev_err(rtd->dev, "unable to add card controls, ret %d\n", ret);
		return ret;
	}

	ret = snd_soc_dapm_add_routes(&card->dapm, mt8195_rt1011_routes,
				      ARRAY_SIZE(mt8195_rt1011_routes));
	if (ret)
		dev_err(rtd->dev, "unable to add dapm routes, ret %d\n", ret);

	return ret;
}

static int mt8195_dumb_amp_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	int ret;

	ret = snd_soc_dapm_new_controls(&card->dapm, mt8195_speaker_widgets,
					ARRAY_SIZE(mt8195_speaker_widgets));
	if (ret) {
		dev_err(rtd->dev, "unable to add dapm controls, ret %d\n", ret);
		/* Don't need to add routes if widget addition failed */
		return ret;
	}

	ret = snd_soc_add_card_controls(card, mt8195_speaker_controls,
					ARRAY_SIZE(mt8195_speaker_controls));
	if (ret) {
		dev_err(rtd->dev, "unable to add card controls, ret %d\n", ret);
		return ret;
	}

	return 0;
}

static int mt8195_rt1019_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	int ret;

	ret = mt8195_dumb_amp_init(rtd);
	if (ret)
		return ret;

	ret = snd_soc_dapm_add_routes(&card->dapm, mt8195_rt1019_routes,
				      ARRAY_SIZE(mt8195_rt1019_routes));
	if (ret)
		dev_err(rtd->dev, "unable to add dapm routes, ret %d\n", ret);

	return ret;
}

static int mt8195_max98390_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	int ret;

	ret = snd_soc_dapm_new_controls(&card->dapm, mt8195_dual_speaker_widgets,
					ARRAY_SIZE(mt8195_dual_speaker_widgets));
	if (ret) {
		dev_err(rtd->dev, "unable to add dapm controls, ret %d\n", ret);
		/* Don't need to add routes if widget addition failed */
		return ret;
	}

	ret = snd_soc_add_card_controls(card, mt8195_dual_speaker_controls,
					ARRAY_SIZE(mt8195_dual_speaker_controls));
	if (ret) {
		dev_err(rtd->dev, "unable to add card controls, ret %d\n", ret);
		return ret;
	}

	ret = snd_soc_dapm_add_routes(&card->dapm, mt8195_max98390_routes,
				      ARRAY_SIZE(mt8195_max98390_routes));
	if (ret)
		dev_err(rtd->dev, "unable to add dapm routes, ret %d\n", ret);

	return ret;
}

static int mt8195_etdm_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				       struct snd_pcm_hw_params *params)
{
	/* fix BE i2s format to S24_LE, clean param mask first */
	snd_mask_reset_range(hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT),
			     0, (__force unsigned int)SNDRV_PCM_FORMAT_LAST);

	params_set_format(params, SNDRV_PCM_FORMAT_S24_LE);

	return 0;
}

static int mt8195_set_bias_level_post(struct snd_soc_card *card,
	struct snd_soc_dapm_context *dapm, enum snd_soc_bias_level level)
{
	struct snd_soc_component *component = dapm->component;
	struct mtk_soc_card_data *soc_card_data = snd_soc_card_get_drvdata(card);
	struct mt8195_mt6359_priv *priv = soc_card_data->mach_priv;
	int ret;

	/*
	 * It's required to control mclk directly in the set_bias_level_post
	 * function for rt5682 and rt5682s codec, or the unexpected pop happens
	 * at the end of playback.
	 */
	if (!component ||
	    (strcmp(component->name, RT5682_DEV0_NAME) &&
	    strcmp(component->name, RT5682S_DEV0_NAME)))
		return 0;

	switch (level) {
	case SND_SOC_BIAS_OFF:
		if (!__clk_is_enabled(priv->i2so1_mclk))
			return 0;

		clk_disable_unprepare(priv->i2so1_mclk);
		dev_dbg(card->dev, "Disable i2so1 mclk\n");
		break;
	case SND_SOC_BIAS_ON:
		ret = clk_prepare_enable(priv->i2so1_mclk);
		if (ret) {
			dev_err(card->dev, "Can't enable i2so1 mclk: %d\n", ret);
			return ret;
		}
		dev_dbg(card->dev, "Enable i2so1 mclk\n");
		break;
	default:
		break;
	}

	return 0;
}

enum {
	DAI_LINK_DL2_FE,
	DAI_LINK_DL3_FE,
	DAI_LINK_DL6_FE,
	DAI_LINK_DL7_FE,
	DAI_LINK_DL8_FE,
	DAI_LINK_DL10_FE,
	DAI_LINK_DL11_FE,
	DAI_LINK_UL1_FE,
	DAI_LINK_UL2_FE,
	DAI_LINK_UL3_FE,
	DAI_LINK_UL4_FE,
	DAI_LINK_UL5_FE,
	DAI_LINK_UL6_FE,
	DAI_LINK_UL8_FE,
	DAI_LINK_UL9_FE,
	DAI_LINK_UL10_FE,
	DAI_LINK_DL_SRC_BE,
	DAI_LINK_DPTX_BE,
	DAI_LINK_ETDM1_IN_BE,
	DAI_LINK_ETDM2_IN_BE,
	DAI_LINK_ETDM1_OUT_BE,
	DAI_LINK_ETDM2_OUT_BE,
	DAI_LINK_ETDM3_OUT_BE,
	DAI_LINK_PCM1_BE,
	DAI_LINK_UL_SRC1_BE,
	DAI_LINK_UL_SRC2_BE,
	DAI_LINK_REGULAR_LAST = DAI_LINK_UL_SRC2_BE,
	DAI_LINK_SOF_START,
	DAI_LINK_SOF_DL2_BE = DAI_LINK_SOF_START,
	DAI_LINK_SOF_DL3_BE,
	DAI_LINK_SOF_UL4_BE,
	DAI_LINK_SOF_UL5_BE,
	DAI_LINK_SOF_END = DAI_LINK_SOF_UL5_BE,
};

#define	DAI_LINK_REGULAR_NUM	(DAI_LINK_REGULAR_LAST + 1)

/* FE */
SND_SOC_DAILINK_DEFS(DL2_FE,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL2")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(DL3_FE,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL3")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(DL6_FE,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL6")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(DL7_FE,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL7")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(DL8_FE,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL8")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(DL10_FE,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL10")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(DL11_FE,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL11")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(UL1_FE,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL1")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(UL2_FE,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL2")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(UL3_FE,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL3")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(UL4_FE,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL4")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(UL5_FE,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL5")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(UL6_FE,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL6")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(UL8_FE,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL8")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(UL9_FE,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL9")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(UL10_FE,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL10")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

/* BE */
SND_SOC_DAILINK_DEFS(DL_SRC_BE,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL_SRC")),
		     DAILINK_COMP_ARRAY(COMP_CODEC("mt6359-sound",
						   "mt6359-snd-codec-aif1")),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(DPTX_BE,
		     DAILINK_COMP_ARRAY(COMP_CPU("DPTX")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(ETDM1_IN_BE,
		     DAILINK_COMP_ARRAY(COMP_CPU("ETDM1_IN")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(ETDM2_IN_BE,
		     DAILINK_COMP_ARRAY(COMP_CPU("ETDM2_IN")),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(ETDM1_OUT_BE,
		     DAILINK_COMP_ARRAY(COMP_CPU("ETDM1_OUT")),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(ETDM2_OUT_BE,
		     DAILINK_COMP_ARRAY(COMP_CPU("ETDM2_OUT")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(ETDM3_OUT_BE,
		     DAILINK_COMP_ARRAY(COMP_CPU("ETDM3_OUT")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(PCM1_BE,
		     DAILINK_COMP_ARRAY(COMP_CPU("PCM1")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(UL_SRC1_BE,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL_SRC1")),
		     DAILINK_COMP_ARRAY(COMP_CODEC("mt6359-sound",
						   "mt6359-snd-codec-aif1"),
					COMP_CODEC("dmic-codec",
						   "dmic-hifi")),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(UL_SRC2_BE,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL_SRC2")),
		     DAILINK_COMP_ARRAY(COMP_CODEC("mt6359-sound",
						   "mt6359-snd-codec-aif2")),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(AFE_SOF_DL2,
		     DAILINK_COMP_ARRAY(COMP_CPU("SOF_DL2")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(AFE_SOF_DL3,
		     DAILINK_COMP_ARRAY(COMP_CPU("SOF_DL3")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(AFE_SOF_UL4,
		     DAILINK_COMP_ARRAY(COMP_CPU("SOF_UL4")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(AFE_SOF_UL5,
		     DAILINK_COMP_ARRAY(COMP_CPU("SOF_UL5")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

/* codec */
SND_SOC_DAILINK_DEF(rt1019_comps,
		    DAILINK_COMP_ARRAY(COMP_CODEC(RT1019_DEV0_NAME,
						  RT1019_CODEC_DAI)));

SND_SOC_DAILINK_DEF(rt1011_comps,
		    DAILINK_COMP_ARRAY(COMP_CODEC(RT1011_DEV0_NAME,
						  RT1011_CODEC_DAI),
				       COMP_CODEC(RT1011_DEV1_NAME,
						  RT1011_CODEC_DAI)));

SND_SOC_DAILINK_DEF(max98390_comps,
		    DAILINK_COMP_ARRAY(COMP_CODEC(MAX98390_DEV0_NAME,
						  MAX98390_CODEC_DAI),
				       COMP_CODEC(MAX98390_DEV1_NAME,
						  MAX98390_CODEC_DAI)));

static const struct sof_conn_stream g_sof_conn_streams[] = {
	{ "ETDM2_OUT_BE", "AFE_SOF_DL2", SOF_DMA_DL2, SNDRV_PCM_STREAM_PLAYBACK},
	{ "ETDM1_OUT_BE", "AFE_SOF_DL3", SOF_DMA_DL3, SNDRV_PCM_STREAM_PLAYBACK},
	{ "UL_SRC1_BE", "AFE_SOF_UL4", SOF_DMA_UL4, SNDRV_PCM_STREAM_CAPTURE},
	{ "ETDM2_IN_BE", "AFE_SOF_UL5", SOF_DMA_UL5, SNDRV_PCM_STREAM_CAPTURE},
};

static struct snd_soc_dai_link mt8195_mt6359_dai_links[] = {
	/* FE */
	[DAI_LINK_DL2_FE] = {
		.name = "DL2_FE",
		.stream_name = "DL2 Playback",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST,
		},
		.dynamic = 1,
		.playback_only = 1,
		.ops = &mtk_soundcard_common_playback_ops,
		SND_SOC_DAILINK_REG(DL2_FE),
	},
	[DAI_LINK_DL3_FE] = {
		.name = "DL3_FE",
		.stream_name = "DL3 Playback",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST,
		},
		.dynamic = 1,
		.playback_only = 1,
		.ops = &mtk_soundcard_common_playback_ops,
		SND_SOC_DAILINK_REG(DL3_FE),
	},
	[DAI_LINK_DL6_FE] = {
		.name = "DL6_FE",
		.stream_name = "DL6 Playback",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST,
		},
		.dynamic = 1,
		.playback_only = 1,
		.ops = &mtk_soundcard_common_playback_ops,
		SND_SOC_DAILINK_REG(DL6_FE),
	},
	[DAI_LINK_DL7_FE] = {
		.name = "DL7_FE",
		.stream_name = "DL7 Playback",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_PRE,
			SND_SOC_DPCM_TRIGGER_PRE,
		},
		.dynamic = 1,
		.playback_only = 1,
		SND_SOC_DAILINK_REG(DL7_FE),
	},
	[DAI_LINK_DL8_FE] = {
		.name = "DL8_FE",
		.stream_name = "DL8 Playback",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST,
		},
		.dynamic = 1,
		.playback_only = 1,
		.ops = &mtk_soundcard_common_playback_ops,
		SND_SOC_DAILINK_REG(DL8_FE),
	},
	[DAI_LINK_DL10_FE] = {
		.name = "DL10_FE",
		.stream_name = "DL10 Playback",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST,
		},
		.dynamic = 1,
		.playback_only = 1,
		.ops = &mt8195_hdmitx_dptx_playback_ops,
		SND_SOC_DAILINK_REG(DL10_FE),
	},
	[DAI_LINK_DL11_FE] = {
		.name = "DL11_FE",
		.stream_name = "DL11 Playback",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST,
		},
		.dynamic = 1,
		.playback_only = 1,
		.ops = &mtk_soundcard_common_playback_ops,
		SND_SOC_DAILINK_REG(DL11_FE),
	},
	[DAI_LINK_UL1_FE] = {
		.name = "UL1_FE",
		.stream_name = "UL1 Capture",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_PRE,
			SND_SOC_DPCM_TRIGGER_PRE,
		},
		.dynamic = 1,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(UL1_FE),
	},
	[DAI_LINK_UL2_FE] = {
		.name = "UL2_FE",
		.stream_name = "UL2 Capture",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST,
		},
		.dynamic = 1,
		.capture_only = 1,
		.ops = &mtk_soundcard_common_capture_ops,
		SND_SOC_DAILINK_REG(UL2_FE),
	},
	[DAI_LINK_UL3_FE] = {
		.name = "UL3_FE",
		.stream_name = "UL3 Capture",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST,
		},
		.dynamic = 1,
		.capture_only = 1,
		.ops = &mtk_soundcard_common_capture_ops,
		SND_SOC_DAILINK_REG(UL3_FE),
	},
	[DAI_LINK_UL4_FE] = {
		.name = "UL4_FE",
		.stream_name = "UL4 Capture",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST,
		},
		.dynamic = 1,
		.capture_only = 1,
		.ops = &mtk_soundcard_common_capture_ops,
		SND_SOC_DAILINK_REG(UL4_FE),
	},
	[DAI_LINK_UL5_FE] = {
		.name = "UL5_FE",
		.stream_name = "UL5 Capture",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST,
		},
		.dynamic = 1,
		.capture_only = 1,
		.ops = &mtk_soundcard_common_capture_ops,
		SND_SOC_DAILINK_REG(UL5_FE),
	},
	[DAI_LINK_UL6_FE] = {
		.name = "UL6_FE",
		.stream_name = "UL6 Capture",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_PRE,
			SND_SOC_DPCM_TRIGGER_PRE,
		},
		.dynamic = 1,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(UL6_FE),
	},
	[DAI_LINK_UL8_FE] = {
		.name = "UL8_FE",
		.stream_name = "UL8 Capture",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST,
		},
		.dynamic = 1,
		.capture_only = 1,
		.ops = &mtk_soundcard_common_capture_ops,
		SND_SOC_DAILINK_REG(UL8_FE),
	},
	[DAI_LINK_UL9_FE] = {
		.name = "UL9_FE",
		.stream_name = "UL9 Capture",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST,
		},
		.dynamic = 1,
		.capture_only = 1,
		.ops = &mtk_soundcard_common_capture_ops,
		SND_SOC_DAILINK_REG(UL9_FE),
	},
	[DAI_LINK_UL10_FE] = {
		.name = "UL10_FE",
		.stream_name = "UL10 Capture",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST,
		},
		.dynamic = 1,
		.capture_only = 1,
		.ops = &mtk_soundcard_common_capture_ops,
		SND_SOC_DAILINK_REG(UL10_FE),
	},
	/* BE */
	[DAI_LINK_DL_SRC_BE] = {
		.name = "DL_SRC_BE",
		.no_pcm = 1,
		.playback_only = 1,
		SND_SOC_DAILINK_REG(DL_SRC_BE),
	},
	[DAI_LINK_DPTX_BE] = {
		.name = "DPTX_BE",
		.no_pcm = 1,
		.playback_only = 1,
		.ops = &mt8195_dptx_ops,
		.be_hw_params_fixup = mt8195_dptx_hw_params_fixup,
		SND_SOC_DAILINK_REG(DPTX_BE),
	},
	[DAI_LINK_ETDM1_IN_BE] = {
		.name = "ETDM1_IN_BE",
		.no_pcm = 1,
		.dai_fmt = SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBC_CFC,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(ETDM1_IN_BE),
	},
	[DAI_LINK_ETDM2_IN_BE] = {
		.name = "ETDM2_IN_BE",
		.no_pcm = 1,
		.dai_fmt = SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBC_CFC,
		.capture_only = 1,
		.be_hw_params_fixup = mt8195_etdm_hw_params_fixup,
		SND_SOC_DAILINK_REG(ETDM2_IN_BE),
	},
	[DAI_LINK_ETDM1_OUT_BE] = {
		.name = "ETDM1_OUT_BE",
		.no_pcm = 1,
		.dai_fmt = SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBC_CFC,
		.playback_only = 1,
		.be_hw_params_fixup = mt8195_etdm_hw_params_fixup,
		SND_SOC_DAILINK_REG(ETDM1_OUT_BE),
	},
	[DAI_LINK_ETDM2_OUT_BE] = {
		.name = "ETDM2_OUT_BE",
		.no_pcm = 1,
		.dai_fmt = SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBC_CFC,
		.playback_only = 1,
		SND_SOC_DAILINK_REG(ETDM2_OUT_BE),
	},
	[DAI_LINK_ETDM3_OUT_BE] = {
		.name = "ETDM3_OUT_BE",
		.no_pcm = 1,
		.dai_fmt = SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBC_CFC,
		.playback_only = 1,
		SND_SOC_DAILINK_REG(ETDM3_OUT_BE),
	},
	[DAI_LINK_PCM1_BE] = {
		.name = "PCM1_BE",
		.no_pcm = 1,
		.dai_fmt = SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBC_CFC,
		SND_SOC_DAILINK_REG(PCM1_BE),
	},
	[DAI_LINK_UL_SRC1_BE] = {
		.name = "UL_SRC1_BE",
		.no_pcm = 1,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(UL_SRC1_BE),
	},
	[DAI_LINK_UL_SRC2_BE] = {
		.name = "UL_SRC2_BE",
		.no_pcm = 1,
		.capture_only = 1,
		SND_SOC_DAILINK_REG(UL_SRC2_BE),
	},
	/* SOF BE */
	[DAI_LINK_SOF_DL2_BE] = {
		.name = "AFE_SOF_DL2",
		.no_pcm = 1,
		.playback_only = 1,
		.ops = &mt8195_sof_be_ops,
		SND_SOC_DAILINK_REG(AFE_SOF_DL2),
	},
	[DAI_LINK_SOF_DL3_BE] = {
		.name = "AFE_SOF_DL3",
		.no_pcm = 1,
		.playback_only = 1,
		.ops = &mt8195_sof_be_ops,
		SND_SOC_DAILINK_REG(AFE_SOF_DL3),
	},
	[DAI_LINK_SOF_UL4_BE] = {
		.name = "AFE_SOF_UL4",
		.no_pcm = 1,
		.capture_only = 1,
		.ops = &mt8195_sof_be_ops,
		SND_SOC_DAILINK_REG(AFE_SOF_UL4),
	},
	[DAI_LINK_SOF_UL5_BE] = {
		.name = "AFE_SOF_UL5",
		.no_pcm = 1,
		.capture_only = 1,
		.ops = &mt8195_sof_be_ops,
		SND_SOC_DAILINK_REG(AFE_SOF_UL5),
	},
};

static struct snd_soc_codec_conf rt1011_codec_conf[] = {
	{
		.dlc = COMP_CODEC_CONF(RT1011_DEV0_NAME),
		.name_prefix = "Left",
	},
	{
		.dlc = COMP_CODEC_CONF(RT1011_DEV1_NAME),
		.name_prefix = "Right",
	},
};

static struct snd_soc_codec_conf max98390_codec_conf[] = {
	{
		.dlc = COMP_CODEC_CONF(MAX98390_DEV0_NAME),
		.name_prefix = "Right",
	},
	{
		.dlc = COMP_CODEC_CONF(MAX98390_DEV1_NAME),
		.name_prefix = "Left",
	},
};

static struct snd_soc_card mt8195_mt6359_soc_card = {
	.owner = THIS_MODULE,
	.dai_link = mt8195_mt6359_dai_links,
	.num_links = ARRAY_SIZE(mt8195_mt6359_dai_links),
	.controls = mt8195_mt6359_controls,
	.num_controls = ARRAY_SIZE(mt8195_mt6359_controls),
	.dapm_widgets = mt8195_mt6359_widgets,
	.num_dapm_widgets = ARRAY_SIZE(mt8195_mt6359_widgets),
	.dapm_routes = mt8195_mt6359_routes,
	.num_dapm_routes = ARRAY_SIZE(mt8195_mt6359_routes),
	.set_bias_level_post = mt8195_set_bias_level_post,
};

/* fixup the BE DAI link to match any values from topology */
static int mt8195_dai_link_fixup(struct snd_soc_pcm_runtime *rtd,
				 struct snd_pcm_hw_params *params)
{
	int ret;

	ret = mtk_sof_dai_link_fixup(rtd, params);

	if (!strcmp(rtd->dai_link->name, "ETDM2_IN_BE") ||
	    !strcmp(rtd->dai_link->name, "ETDM1_OUT_BE")) {
		mt8195_etdm_hw_params_fixup(rtd, params);
	}

	return ret;
}

static int mt8195_mt6359_legacy_probe(struct mtk_soc_card_data *soc_card_data)
{
	struct mtk_platform_card_data *card_data = soc_card_data->card_data;
	struct snd_soc_card *card = card_data->card;
	struct device_node *codec_node, *dp_node, *hdmi_node;
	struct snd_soc_dai_link *dai_link;
	struct device *dev = card->dev;
	bool is5682s, init6359 = false;
	int i;

	if (strstr(card->name, "_5682s")) {
		codec_node = of_find_compatible_node(NULL, NULL, "realtek,rt5682s");
		is5682s = true;
	} else {
		codec_node = of_find_compatible_node(NULL, NULL, "realtek,rt5682i");
		is5682s = false;
	}

	dp_node = of_parse_phandle(dev->of_node, "mediatek,dptx-codec", 0);
	hdmi_node = of_parse_phandle(dev->of_node, "mediatek,hdmi-codec", 0);

	for_each_card_prelinks(card, i, dai_link) {
		if (strcmp(dai_link->name, "DPTX_BE") == 0) {
			if (!dp_node) {
				dev_dbg(dev, "No property 'dptx-codec'\n");
			} else {
				dai_link->codecs->of_node = dp_node;
				dai_link->codecs->name = NULL;
				dai_link->codecs->dai_name = "i2s-hifi";
				dai_link->init = mt8195_dptx_codec_init;
			}
		} else if (strcmp(dai_link->name, "ETDM3_OUT_BE") == 0) {
			if (!hdmi_node) {
				dev_dbg(dev, "No property 'hdmi-codec'\n");
			} else {
				dai_link->codecs->of_node = hdmi_node;
				dai_link->codecs->name = NULL;
				dai_link->codecs->dai_name = "i2s-hifi";
				dai_link->init = mt8195_hdmi_codec_init;
			}
		} else if (strcmp(dai_link->name, "ETDM1_OUT_BE") == 0) {
			if (!codec_node) {
				dev_err(dev, "Codec not found!\n");
			} else {
				dai_link->codecs->of_node = codec_node;
				dai_link->codecs->name = NULL;
				dai_link->codecs->dai_name =
					is5682s ? RT5682S_CODEC_DAI : RT5682_CODEC_DAI;
				dai_link->init = mt8195_rt5682_init;
				dai_link->ops = &mt8195_rt5682_etdm_ops;
			}
		} else if (strcmp(dai_link->name, "ETDM2_IN_BE") == 0) {
			if (!codec_node) {
				dev_err(dev, "Codec not found!\n");
			} else {
				dai_link->codecs->of_node = codec_node;
				dai_link->codecs->name = NULL;
				dai_link->codecs->dai_name =
					is5682s ? RT5682S_CODEC_DAI : RT5682_CODEC_DAI;
				dai_link->ops = &mt8195_rt5682_etdm_ops;
			}
		} else if (strcmp(dai_link->name, "DL_SRC_BE") == 0 ||
			   strcmp(dai_link->name, "UL_SRC1_BE") == 0 ||
			   strcmp(dai_link->name, "UL_SRC2_BE") == 0) {
			if (!init6359) {
				dai_link->init = mt8195_mt6359_init;
				init6359 = true;
			}
		} else if (strcmp(dai_link->name, "ETDM2_OUT_BE") == 0) {
			switch (card_data->flags) {
			case RT1011_SPEAKER_AMP_PRESENT:
				dai_link->codecs = rt1011_comps;
				dai_link->num_codecs = ARRAY_SIZE(rt1011_comps);
				dai_link->init = mt8195_rt1011_init;
				dai_link->ops = &mt8195_rt1011_etdm_ops;
				dai_link->be_hw_params_fixup = mt8195_etdm_hw_params_fixup;
				card->codec_conf = rt1011_codec_conf;
				card->num_configs = ARRAY_SIZE(rt1011_codec_conf);
				break;
			case RT1019_SPEAKER_AMP_PRESENT:
				dai_link->codecs = rt1019_comps;
				dai_link->num_codecs = ARRAY_SIZE(rt1019_comps);
				dai_link->init = mt8195_rt1019_init;
				break;
			case MAX98390_SPEAKER_AMP_PRESENT:
				dai_link->codecs = max98390_comps;
				dai_link->num_codecs = ARRAY_SIZE(max98390_comps);
				dai_link->init = mt8195_max98390_init;
				card->codec_conf = max98390_codec_conf;
				card->num_configs = ARRAY_SIZE(max98390_codec_conf);
				break;
			default:
				break;
			}
		}
	}

	return 0;
}

static int mt8195_mt6359_soc_card_probe(struct mtk_soc_card_data *soc_card_data, bool legacy)
{
	struct mtk_platform_card_data *card_data = soc_card_data->card_data;
	struct snd_soc_card *card = card_data->card;
	struct mt8195_mt6359_priv *mach_priv;
	struct snd_soc_dai_link *dai_link;
	u8 codec_init = 0;
	int i;

	mach_priv = devm_kzalloc(card->dev, sizeof(*mach_priv), GFP_KERNEL);
	if (!mach_priv)
		return -ENOMEM;

	soc_card_data->mach_priv = mach_priv;

	if (legacy)
		return mt8195_mt6359_legacy_probe(soc_card_data);

	for_each_card_prelinks(card, i, dai_link) {
		if (strcmp(dai_link->name, "DPTX_BE") == 0) {
			if (dai_link->num_codecs &&
			    strcmp(dai_link->codecs->dai_name, "snd-soc-dummy-dai"))
				dai_link->init = mt8195_dptx_codec_init;
		} else if (strcmp(dai_link->name, "ETDM3_OUT_BE") == 0) {
			if (dai_link->num_codecs &&
			    strcmp(dai_link->codecs->dai_name, "snd-soc-dummy-dai"))
				dai_link->init = mt8195_hdmi_codec_init;
		} else if (strcmp(dai_link->name, "DL_SRC_BE") == 0 ||
			   strcmp(dai_link->name, "UL_SRC1_BE") == 0 ||
			   strcmp(dai_link->name, "UL_SRC2_BE") == 0) {
			if (!(codec_init & MT6359_CODEC_INIT)) {
				dai_link->init = mt8195_mt6359_init;
				codec_init |= MT6359_CODEC_INIT;
			}
		} else if (strcmp(dai_link->name, "ETDM1_OUT_BE") == 0 ||
			   strcmp(dai_link->name, "ETDM2_OUT_BE") == 0 ||
			   strcmp(dai_link->name, "ETDM1_IN_BE") == 0 ||
			   strcmp(dai_link->name, "ETDM2_IN_BE") == 0) {
			if (!dai_link->num_codecs)
				continue;

			if (!strcmp(dai_link->codecs->dai_name, MAX98390_CODEC_DAI)) {
				if (!(codec_init & MAX98390_CODEC_INIT)) {
					dai_link->init = mt8195_max98390_init;
					codec_init |= MAX98390_CODEC_INIT;
				}
			} else if (!strcmp(dai_link->codecs->dai_name, RT1011_CODEC_DAI)) {
				dai_link->ops = &mt8195_rt1011_etdm_ops;
				if (!(codec_init & RT1011_CODEC_INIT)) {
					dai_link->init = mt8195_rt1011_init;
					codec_init |= RT1011_CODEC_INIT;
				}
			} else if (!strcmp(dai_link->codecs->dai_name, RT1019_CODEC_DAI)) {
				if (!(codec_init & RT1019_CODEC_INIT)) {
					dai_link->init = mt8195_rt1019_init;
					codec_init |= RT1019_CODEC_INIT;
				}
			} else if (!strcmp(dai_link->codecs->dai_name, RT5682_CODEC_DAI) ||
				   !strcmp(dai_link->codecs->dai_name, RT5682S_CODEC_DAI)) {
				dai_link->ops = &mt8195_rt5682_etdm_ops;
				if (!(codec_init & RT5682_CODEC_INIT)) {
					dai_link->init = mt8195_rt5682_init;
					codec_init |= RT5682_CODEC_INIT;
				}
			} else {
				if (strcmp(dai_link->codecs->dai_name, "snd-soc-dummy-dai")) {
					if (!(codec_init & DUMB_CODEC_INIT)) {
						dai_link->init = mt8195_dumb_amp_init;
						codec_init |= DUMB_CODEC_INIT;
					}
				}
			}
		}
	}

	return 0;
}

static const unsigned int mt8195_pcm_playback_channels[] = { 2 };
static const unsigned int mt8195_pcm_capture_channels[] = { 1, 2 };
static const unsigned int mt8195_pcm_hdmidp_channels[] = { 2, 4, 6, 8 };
static const unsigned int mt8195_pcm_rates[] = { 48000 };

static const struct snd_pcm_hw_constraint_list mt8195_rate_constraint = {
	.list = mt8195_pcm_rates,
	.count = ARRAY_SIZE(mt8195_pcm_rates)
};

static const struct mtk_pcm_constraints_data mt8195_pcm_constraints[MTK_CONSTRAINT_HDMIDP + 1] = {
	[MTK_CONSTRAINT_PLAYBACK] = {
		.channels = &(const struct snd_pcm_hw_constraint_list) {
			.list = mt8195_pcm_playback_channels,
			.count = ARRAY_SIZE(mt8195_pcm_playback_channels)
		},
		.rates = &mt8195_rate_constraint,
	},
	[MTK_CONSTRAINT_CAPTURE] = {
		.channels =  &(const struct snd_pcm_hw_constraint_list) {
			.list = mt8195_pcm_capture_channels,
			.count = ARRAY_SIZE(mt8195_pcm_capture_channels)
		},
		.rates = &mt8195_rate_constraint,
	},
	[MTK_CONSTRAINT_HDMIDP] = {
		.channels =  &(const struct snd_pcm_hw_constraint_list) {
			.list = mt8195_pcm_hdmidp_channels,
			.count = ARRAY_SIZE(mt8195_pcm_hdmidp_channels)
		},
		.rates = &mt8195_rate_constraint,
	},
};

static const struct mtk_sof_priv mt8195_sof_priv = {
	.conn_streams = g_sof_conn_streams,
	.num_streams = ARRAY_SIZE(g_sof_conn_streams),
	.sof_dai_link_fixup = mt8195_dai_link_fixup
};

static const struct mtk_soundcard_pdata mt8195_mt6359_rt1019_rt5682_card = {
	.card_name = "mt8195_r1019_5682",
	.card_data = &(struct mtk_platform_card_data) {
		.card = &mt8195_mt6359_soc_card,
		.num_jacks = MT8195_JACK_MAX,
		.pcm_constraints = mt8195_pcm_constraints,
		.num_pcm_constraints = ARRAY_SIZE(mt8195_pcm_constraints),
		.flags = RT1019_SPEAKER_AMP_PRESENT
	},
	.sof_priv = &mt8195_sof_priv,
	.soc_probe = mt8195_mt6359_soc_card_probe
};

static const struct mtk_soundcard_pdata mt8195_mt6359_rt1011_rt5682_card = {
	.card_name = "mt8195_r1011_5682",
	.card_data = &(struct mtk_platform_card_data) {
		.card = &mt8195_mt6359_soc_card,
		.num_jacks = MT8195_JACK_MAX,
		.pcm_constraints = mt8195_pcm_constraints,
		.num_pcm_constraints = ARRAY_SIZE(mt8195_pcm_constraints),
		.flags = RT1011_SPEAKER_AMP_PRESENT
	},
	.sof_priv = &mt8195_sof_priv,
	.soc_probe = mt8195_mt6359_soc_card_probe
};

static const struct mtk_soundcard_pdata mt8195_mt6359_max98390_rt5682_card = {
	.card_name = "mt8195_m98390_r5682",
	.card_data = &(struct mtk_platform_card_data) {
		.card = &mt8195_mt6359_soc_card,
		.num_jacks = MT8195_JACK_MAX,
		.pcm_constraints = mt8195_pcm_constraints,
		.num_pcm_constraints = ARRAY_SIZE(mt8195_pcm_constraints),
		.flags = MAX98390_SPEAKER_AMP_PRESENT
	},
	.sof_priv = &mt8195_sof_priv,
	.soc_probe = mt8195_mt6359_soc_card_probe
};

static const struct of_device_id mt8195_mt6359_dt_match[] = {
	{
		.compatible = "mediatek,mt8195_mt6359_rt1019_rt5682",
		.data = &mt8195_mt6359_rt1019_rt5682_card,
	},
	{
		.compatible = "mediatek,mt8195_mt6359_rt1011_rt5682",
		.data = &mt8195_mt6359_rt1011_rt5682_card,
	},
	{
		.compatible = "mediatek,mt8195_mt6359_max98390_rt5682",
		.data = &mt8195_mt6359_max98390_rt5682_card,
	},
	{},
};
MODULE_DEVICE_TABLE(of, mt8195_mt6359_dt_match);

static struct platform_driver mt8195_mt6359_driver = {
	.driver = {
		.name = "mt8195_mt6359",
		.of_match_table = mt8195_mt6359_dt_match,
		.pm = &snd_soc_pm_ops,
	},
	.probe = mtk_soundcard_common_probe,
};

module_platform_driver(mt8195_mt6359_driver);

/* Module information */
MODULE_DESCRIPTION("MT8195-MT6359 ALSA SoC machine driver");
MODULE_AUTHOR("Trevor Wu <trevor.wu@mediatek.com>");
MODULE_AUTHOR("YC Hung <yc.hung@mediatek.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("mt8195_mt6359 soc card");
