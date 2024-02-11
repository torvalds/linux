// SPDX-License-Identifier: GPL-2.0
/*
 * mt8188-mt6359.c  --  MT8188-MT6359 ALSA SoC machine driver
 *
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Trevor Wu <trevor.wu@mediatek.com>
 */

#include <linux/bitfield.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <sound/jack.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include "mt8188-afe-common.h"
#include "../../codecs/nau8825.h"
#include "../../codecs/mt6359.h"
#include "../../codecs/rt5682.h"
#include "../common/mtk-afe-platform-driver.h"
#include "../common/mtk-soundcard-driver.h"
#include "../common/mtk-dsp-sof-common.h"
#include "../common/mtk-soc-card.h"

#define CKSYS_AUD_TOP_CFG	0x032c
 #define RG_TEST_ON		BIT(0)
 #define RG_TEST_TYPE		BIT(2)
#define CKSYS_AUD_TOP_MON	0x0330
 #define TEST_MISO_COUNT_1	GENMASK(3, 0)
 #define TEST_MISO_COUNT_2	GENMASK(7, 4)
 #define TEST_MISO_DONE_1	BIT(28)
 #define TEST_MISO_DONE_2	BIT(29)

#define NAU8825_HS_PRESENT	BIT(0)
#define RT5682S_HS_PRESENT	BIT(1)
#define ES8326_HS_PRESENT	BIT(2)
#define MAX98390_TWO_AMP	BIT(3)
/*
 * Maxim MAX98390
 */
#define MAX98390_CODEC_DAI     "max98390-aif1"
#define MAX98390_DEV0_NAME     "max98390.0-0038" /* rear right */
#define MAX98390_DEV1_NAME     "max98390.0-0039" /* rear left */
#define MAX98390_DEV2_NAME     "max98390.0-003a" /* front right */
#define MAX98390_DEV3_NAME     "max98390.0-003b" /* front left */

/*
 * Nau88l25
 */
#define NAU8825_CODEC_DAI  "nau8825-hifi"

/*
 * ES8326
 */
#define ES8326_CODEC_DAI  "ES8326 HiFi"

#define SOF_DMA_DL2 "SOF_DMA_DL2"
#define SOF_DMA_DL3 "SOF_DMA_DL3"
#define SOF_DMA_UL4 "SOF_DMA_UL4"
#define SOF_DMA_UL5 "SOF_DMA_UL5"

#define RT5682S_CODEC_DAI     "rt5682s-aif1"

/* FE */
SND_SOC_DAILINK_DEFS(playback2,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL2")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(playback3,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL3")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(playback6,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL6")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(playback7,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL7")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(playback8,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL8")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(playback10,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL10")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(playback11,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL11")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(capture1,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL1")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(capture2,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL2")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(capture3,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL3")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(capture4,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL4")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(capture5,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL5")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(capture6,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL6")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(capture8,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL8")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(capture9,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL9")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(capture10,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL10")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

/* BE */
SND_SOC_DAILINK_DEFS(dl_src,
		     DAILINK_COMP_ARRAY(COMP_CPU("DL_SRC")),
		     DAILINK_COMP_ARRAY(COMP_CODEC("mt6359-sound",
						   "mt6359-snd-codec-aif1")),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(dptx,
		     DAILINK_COMP_ARRAY(COMP_CPU("DPTX")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(etdm1_in,
		     DAILINK_COMP_ARRAY(COMP_CPU("ETDM1_IN")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(etdm2_in,
		     DAILINK_COMP_ARRAY(COMP_CPU("ETDM2_IN")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(etdm1_out,
		     DAILINK_COMP_ARRAY(COMP_CPU("ETDM1_OUT")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(etdm2_out,
		     DAILINK_COMP_ARRAY(COMP_CPU("ETDM2_OUT")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(etdm3_out,
		     DAILINK_COMP_ARRAY(COMP_CPU("ETDM3_OUT")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(pcm1,
		     DAILINK_COMP_ARRAY(COMP_CPU("PCM1")),
		     DAILINK_COMP_ARRAY(COMP_DUMMY()),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(ul_src,
		     DAILINK_COMP_ARRAY(COMP_CPU("UL_SRC")),
		     DAILINK_COMP_ARRAY(COMP_CODEC("mt6359-sound",
						   "mt6359-snd-codec-aif1"),
					COMP_CODEC("dmic-codec",
						   "dmic-hifi")),
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

static const struct sof_conn_stream g_sof_conn_streams[] = {
	{
		.sof_link = "AFE_SOF_DL2",
		.sof_dma = SOF_DMA_DL2,
		.stream_dir = SNDRV_PCM_STREAM_PLAYBACK
	},
	{
		.sof_link = "AFE_SOF_DL3",
		.sof_dma = SOF_DMA_DL3,
		.stream_dir = SNDRV_PCM_STREAM_PLAYBACK
	},
	{
		.sof_link = "AFE_SOF_UL4",
		.sof_dma = SOF_DMA_UL4,
		.stream_dir = SNDRV_PCM_STREAM_CAPTURE
	},
	{
		.sof_link = "AFE_SOF_UL5",
		.sof_dma = SOF_DMA_UL5,
		.stream_dir = SNDRV_PCM_STREAM_CAPTURE
	},
};

struct mt8188_mt6359_priv {
	struct snd_soc_jack dp_jack;
	struct snd_soc_jack hdmi_jack;
	struct snd_soc_jack headset_jack;
	void *private_data;
};

static struct snd_soc_jack_pin mt8188_hdmi_jack_pins[] = {
	{
		.pin = "HDMI",
		.mask = SND_JACK_LINEOUT,
	},
};

static struct snd_soc_jack_pin mt8188_dp_jack_pins[] = {
	{
		.pin = "DP",
		.mask = SND_JACK_LINEOUT,
	},
};

static struct snd_soc_jack_pin nau8825_jack_pins[] = {
	{
		.pin    = "Headphone Jack",
		.mask   = SND_JACK_HEADPHONE,
	},
	{
		.pin    = "Headset Mic",
		.mask   = SND_JACK_MICROPHONE,
	},
};

struct mt8188_card_data {
	const char *name;
	unsigned long quirk;
};

static const struct snd_kcontrol_new mt8188_dumb_spk_controls[] = {
	SOC_DAPM_PIN_SWITCH("Ext Spk"),
};

static const struct snd_soc_dapm_widget mt8188_dumb_spk_widgets[] = {
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
};

static const struct snd_kcontrol_new mt8188_dual_spk_controls[] = {
	SOC_DAPM_PIN_SWITCH("Left Spk"),
	SOC_DAPM_PIN_SWITCH("Right Spk"),
};

static const struct snd_soc_dapm_widget mt8188_dual_spk_widgets[] = {
	SND_SOC_DAPM_SPK("Left Spk", NULL),
	SND_SOC_DAPM_SPK("Right Spk", NULL),
};

static const struct snd_kcontrol_new mt8188_rear_spk_controls[] = {
	SOC_DAPM_PIN_SWITCH("Rear Left Spk"),
	SOC_DAPM_PIN_SWITCH("Rear Right Spk"),
};

static const struct snd_soc_dapm_widget mt8188_rear_spk_widgets[] = {
	SND_SOC_DAPM_SPK("Rear Left Spk", NULL),
	SND_SOC_DAPM_SPK("Rear Right Spk", NULL),
};

static const struct snd_soc_dapm_widget mt8188_mt6359_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_SINK("HDMI"),
	SND_SOC_DAPM_SINK("DP"),
	SND_SOC_DAPM_MIXER(SOF_DMA_DL2, SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER(SOF_DMA_DL3, SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER(SOF_DMA_UL4, SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER(SOF_DMA_UL5, SND_SOC_NOPM, 0, 0, NULL, 0),

	/* dynamic pinctrl */
	SND_SOC_DAPM_PINCTRL("ETDM_SPK_PIN", "aud_etdm_spk_on", "aud_etdm_spk_off"),
	SND_SOC_DAPM_PINCTRL("ETDM_HP_PIN", "aud_etdm_hp_on", "aud_etdm_hp_off"),
	SND_SOC_DAPM_PINCTRL("MTKAIF_PIN", "aud_mtkaif_on", "aud_mtkaif_off"),
};

static const struct snd_kcontrol_new mt8188_mt6359_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
};

static const struct snd_soc_dapm_widget mt8188_nau8825_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
};

static const struct snd_kcontrol_new mt8188_nau8825_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
};

static const struct snd_soc_dapm_route mt8188_mt6359_routes[] = {
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

static int mt8188_mt6359_mtkaif_calibration(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *cmpnt_afe =
		snd_soc_rtdcom_lookup(rtd, AFE_PCM_NAME);
	struct snd_soc_component *cmpnt_codec =
		snd_soc_rtd_to_codec(rtd, 0)->component;
	struct snd_soc_dapm_widget *pin_w = NULL, *w;
	struct mtk_base_afe *afe;
	struct mt8188_afe_private *afe_priv;
	struct mtkaif_param *param;
	int chosen_phase_1, chosen_phase_2;
	int prev_cycle_1, prev_cycle_2;
	u8 test_done_1, test_done_2;
	int cycle_1, cycle_2;
	int mtkaif_chosen_phase[MT8188_MTKAIF_MISO_NUM];
	int mtkaif_phase_cycle[MT8188_MTKAIF_MISO_NUM];
	int mtkaif_calibration_num_phase;
	bool mtkaif_calibration_ok;
	u32 monitor = 0;
	int counter;
	int phase;
	int i;

	if (!cmpnt_afe)
		return -EINVAL;

	afe = snd_soc_component_get_drvdata(cmpnt_afe);
	afe_priv = afe->platform_priv;
	param = &afe_priv->mtkaif_params;

	dev_dbg(afe->dev, "%s(), start\n", __func__);

	param->mtkaif_calibration_ok = false;
	for (i = 0; i < MT8188_MTKAIF_MISO_NUM; i++) {
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

	for_each_card_widgets(rtd->card, w) {
		if (!strcmp(w->name, "MTKAIF_PIN")) {
			pin_w = w;
			break;
		}
	}

	if (pin_w)
		dapm_pinctrl_event(pin_w, NULL, SND_SOC_DAPM_PRE_PMU);
	else
		dev_dbg(afe->dev, "%s(), no pinmux widget, please check if default on\n", __func__);

	pm_runtime_get_sync(afe->dev);
	mt6359_mtkaif_calibration_enable(cmpnt_codec);

	/* set test type to synchronizer pulse */
	regmap_write(afe_priv->topckgen, CKSYS_AUD_TOP_CFG, RG_TEST_TYPE);
	mtkaif_calibration_num_phase = 42;	/* mt6359: 0 ~ 42 */
	mtkaif_calibration_ok = true;

	for (phase = 0;
	     phase <= mtkaif_calibration_num_phase && mtkaif_calibration_ok;
	     phase++) {
		mt6359_set_mtkaif_calibration_phase(cmpnt_codec,
						    phase, phase, phase);

		regmap_set_bits(afe_priv->topckgen, CKSYS_AUD_TOP_CFG, RG_TEST_ON);

		test_done_1 = 0;
		test_done_2 = 0;

		cycle_1 = -1;
		cycle_2 = -1;

		counter = 0;
		while (!(test_done_1 & test_done_2)) {
			regmap_read(afe_priv->topckgen,
				    CKSYS_AUD_TOP_MON, &monitor);
			test_done_1 = FIELD_GET(TEST_MISO_DONE_1, monitor);
			test_done_2 = FIELD_GET(TEST_MISO_DONE_2, monitor);

			if (test_done_1 == 1)
				cycle_1 = FIELD_GET(TEST_MISO_COUNT_1, monitor);

			if (test_done_2 == 1)
				cycle_2 = FIELD_GET(TEST_MISO_COUNT_2, monitor);

			/* handle if never test done */
			if (++counter > 10000) {
				dev_err(afe->dev, "%s(), test fail, cycle_1 %d, cycle_2 %d, monitor 0x%x\n",
					__func__, cycle_1, cycle_2, monitor);
				mtkaif_calibration_ok = false;
				break;
			}
		}

		if (phase == 0) {
			prev_cycle_1 = cycle_1;
			prev_cycle_2 = cycle_2;
		}

		if (cycle_1 != prev_cycle_1 &&
		    mtkaif_chosen_phase[MT8188_MTKAIF_MISO_0] < 0) {
			mtkaif_chosen_phase[MT8188_MTKAIF_MISO_0] = phase - 1;
			mtkaif_phase_cycle[MT8188_MTKAIF_MISO_0] = prev_cycle_1;
		}

		if (cycle_2 != prev_cycle_2 &&
		    mtkaif_chosen_phase[MT8188_MTKAIF_MISO_1] < 0) {
			mtkaif_chosen_phase[MT8188_MTKAIF_MISO_1] = phase - 1;
			mtkaif_phase_cycle[MT8188_MTKAIF_MISO_1] = prev_cycle_2;
		}

		regmap_clear_bits(afe_priv->topckgen, CKSYS_AUD_TOP_CFG, RG_TEST_ON);

		if (mtkaif_chosen_phase[MT8188_MTKAIF_MISO_0] >= 0 &&
		    mtkaif_chosen_phase[MT8188_MTKAIF_MISO_1] >= 0)
			break;
	}

	if (mtkaif_chosen_phase[MT8188_MTKAIF_MISO_0] < 0) {
		mtkaif_calibration_ok = false;
		chosen_phase_1 = 0;
	} else {
		chosen_phase_1 = mtkaif_chosen_phase[MT8188_MTKAIF_MISO_0];
	}

	if (mtkaif_chosen_phase[MT8188_MTKAIF_MISO_1] < 0) {
		mtkaif_calibration_ok = false;
		chosen_phase_2 = 0;
	} else {
		chosen_phase_2 = mtkaif_chosen_phase[MT8188_MTKAIF_MISO_1];
	}

	mt6359_set_mtkaif_calibration_phase(cmpnt_codec,
					    chosen_phase_1,
					    chosen_phase_2,
					    0);

	mt6359_mtkaif_calibration_disable(cmpnt_codec);
	pm_runtime_put(afe->dev);

	param->mtkaif_calibration_ok = mtkaif_calibration_ok;
	param->mtkaif_chosen_phase[MT8188_MTKAIF_MISO_0] = chosen_phase_1;
	param->mtkaif_chosen_phase[MT8188_MTKAIF_MISO_1] = chosen_phase_2;

	for (i = 0; i < MT8188_MTKAIF_MISO_NUM; i++)
		param->mtkaif_phase_cycle[i] = mtkaif_phase_cycle[i];

	if (pin_w)
		dapm_pinctrl_event(pin_w, NULL, SND_SOC_DAPM_POST_PMD);

	dev_dbg(afe->dev, "%s(), end, calibration ok %d\n",
		__func__, param->mtkaif_calibration_ok);

	return 0;
}

static int mt8188_mt6359_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *cmpnt_codec =
		snd_soc_rtd_to_codec(rtd, 0)->component;

	/* set mtkaif protocol */
	mt6359_set_mtkaif_protocol(cmpnt_codec,
				   MT6359_MTKAIF_PROTOCOL_2_CLK_P2);

	/* mtkaif calibration */
	mt8188_mt6359_mtkaif_calibration(rtd);

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
	DAI_LINK_UL_SRC_BE,
	DAI_LINK_REGULAR_LAST = DAI_LINK_UL_SRC_BE,
	DAI_LINK_SOF_START,
	DAI_LINK_SOF_DL2_BE = DAI_LINK_SOF_START,
	DAI_LINK_SOF_DL3_BE,
	DAI_LINK_SOF_UL4_BE,
	DAI_LINK_SOF_UL5_BE,
	DAI_LINK_SOF_END = DAI_LINK_SOF_UL5_BE,
};

#define	DAI_LINK_REGULAR_NUM	(DAI_LINK_REGULAR_LAST + 1)

static int mt8188_dptx_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	unsigned int rate = params_rate(params);
	unsigned int mclk_fs_ratio = 256;
	unsigned int mclk_fs = rate * mclk_fs_ratio;
	struct snd_soc_dai *dai = snd_soc_rtd_to_cpu(rtd, 0);

	return snd_soc_dai_set_sysclk(dai, 0, mclk_fs, SND_SOC_CLOCK_OUT);
}

static const struct snd_soc_ops mt8188_dptx_ops = {
	.hw_params = mt8188_dptx_hw_params,
};

static int mt8188_dptx_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				       struct snd_pcm_hw_params *params)
{
	/* fix BE i2s format to 32bit, clean param mask first */
	snd_mask_reset_range(hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT),
			     0, (__force unsigned int)SNDRV_PCM_FORMAT_LAST);

	params_set_format(params, SNDRV_PCM_FORMAT_S32_LE);

	return 0;
}

static int mt8188_hdmi_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	struct mtk_soc_card_data *soc_card_data = snd_soc_card_get_drvdata(rtd->card);
	struct mt8188_mt6359_priv *priv = soc_card_data->mach_priv;
	struct snd_soc_component *component = snd_soc_rtd_to_codec(rtd, 0)->component;
	int ret = 0;

	ret = snd_soc_card_jack_new_pins(rtd->card, "HDMI Jack",
					 SND_JACK_LINEOUT, &priv->hdmi_jack,
					 mt8188_hdmi_jack_pins,
					 ARRAY_SIZE(mt8188_hdmi_jack_pins));
	if (ret) {
		dev_err(rtd->dev, "%s, new jack failed: %d\n", __func__, ret);
		return ret;
	}

	ret = snd_soc_component_set_jack(component, &priv->hdmi_jack, NULL);
	if (ret) {
		dev_err(rtd->dev, "%s, set jack failed on %s (ret=%d)\n",
			__func__, component->name, ret);
		return ret;
	}

	return 0;
}

static int mt8188_dptx_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	struct mtk_soc_card_data *soc_card_data = snd_soc_card_get_drvdata(rtd->card);
	struct mt8188_mt6359_priv *priv = soc_card_data->mach_priv;
	struct snd_soc_component *component = snd_soc_rtd_to_codec(rtd, 0)->component;
	int ret = 0;

	ret = snd_soc_card_jack_new_pins(rtd->card, "DP Jack", SND_JACK_LINEOUT,
					 &priv->dp_jack, mt8188_dp_jack_pins,
					 ARRAY_SIZE(mt8188_dp_jack_pins));
	if (ret) {
		dev_err(rtd->dev, "%s, new jack failed: %d\n", __func__, ret);
		return ret;
	}

	ret = snd_soc_component_set_jack(component, &priv->dp_jack, NULL);
	if (ret) {
		dev_err(rtd->dev, "%s, set jack failed on %s (ret=%d)\n",
			__func__, component->name, ret);
		return ret;
	}

	return 0;
}

static int mt8188_dumb_amp_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	int ret = 0;

	ret = snd_soc_dapm_new_controls(&card->dapm, mt8188_dumb_spk_widgets,
					ARRAY_SIZE(mt8188_dumb_spk_widgets));
	if (ret) {
		dev_err(rtd->dev, "unable to add Dumb Speaker dapm, ret %d\n", ret);
		return ret;
	}

	ret = snd_soc_add_card_controls(card, mt8188_dumb_spk_controls,
					ARRAY_SIZE(mt8188_dumb_spk_controls));
	if (ret) {
		dev_err(rtd->dev, "unable to add Dumb card controls, ret %d\n", ret);
		return ret;
	}

	return 0;
}

static int mt8188_max98390_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	unsigned int bit_width = params_width(params);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai;
	int i;

	snd_soc_dai_set_tdm_slot(cpu_dai, 0xf, 0xf, 4, bit_width);

	for_each_rtd_codec_dais(rtd, i, codec_dai) {
		if (!strcmp(codec_dai->component->name, MAX98390_DEV0_NAME))
			snd_soc_dai_set_tdm_slot(codec_dai, 0x8, 0x3, 4, bit_width);

		if (!strcmp(codec_dai->component->name, MAX98390_DEV1_NAME))
			snd_soc_dai_set_tdm_slot(codec_dai, 0x4, 0x3, 4, bit_width);

		if (!strcmp(codec_dai->component->name, MAX98390_DEV2_NAME))
			snd_soc_dai_set_tdm_slot(codec_dai, 0x2, 0x3, 4, bit_width);

		if (!strcmp(codec_dai->component->name, MAX98390_DEV3_NAME))
			snd_soc_dai_set_tdm_slot(codec_dai, 0x1, 0x3, 4, bit_width);
	}
	return 0;
}

static const struct snd_soc_ops mt8188_max98390_ops = {
	.hw_params = mt8188_max98390_hw_params,
};

static int mt8188_max98390_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	int ret;

	/* add regular speakers dapm route */
	ret = snd_soc_dapm_new_controls(&card->dapm, mt8188_dual_spk_widgets,
					ARRAY_SIZE(mt8188_dual_spk_widgets));
	if (ret) {
		dev_err(rtd->dev, "unable to add Left/Right Speaker widget, ret %d\n", ret);
		return ret;
	}

	ret = snd_soc_add_card_controls(card, mt8188_dual_spk_controls,
					ARRAY_SIZE(mt8188_dual_spk_controls));
	if (ret) {
		dev_err(rtd->dev, "unable to add Left/Right card controls, ret %d\n", ret);
		return ret;
	}

	if (rtd->dai_link->num_codecs <= 2)
		return 0;

	/* add widgets/controls/dapm for rear speakers */
	ret = snd_soc_dapm_new_controls(&card->dapm, mt8188_rear_spk_widgets,
					ARRAY_SIZE(mt8188_rear_spk_widgets));
	if (ret) {
		dev_err(rtd->dev, "unable to add Rear Speaker widget, ret %d\n", ret);
		/* Don't need to add routes if widget addition failed */
		return ret;
	}

	ret = snd_soc_add_card_controls(card, mt8188_rear_spk_controls,
					ARRAY_SIZE(mt8188_rear_spk_controls));
	if (ret) {
		dev_err(rtd->dev, "unable to add Rear card controls, ret %d\n", ret);
		return ret;
	}

	return 0;
}

static int mt8188_headset_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct mtk_soc_card_data *soc_card_data = snd_soc_card_get_drvdata(card);
	struct mt8188_mt6359_priv *priv = soc_card_data->mach_priv;
	struct snd_soc_component *component = snd_soc_rtd_to_codec(rtd, 0)->component;
	struct snd_soc_jack *jack = &priv->headset_jack;
	int ret;

	ret = snd_soc_dapm_new_controls(&card->dapm, mt8188_nau8825_widgets,
					ARRAY_SIZE(mt8188_nau8825_widgets));
	if (ret) {
		dev_err(rtd->dev, "unable to add nau8825 card widget, ret %d\n", ret);
		return ret;
	}

	ret = snd_soc_add_card_controls(card, mt8188_nau8825_controls,
					ARRAY_SIZE(mt8188_nau8825_controls));
	if (ret) {
		dev_err(rtd->dev, "unable to add nau8825 card controls, ret %d\n", ret);
		return ret;
	}

	ret = snd_soc_card_jack_new_pins(rtd->card, "Headset Jack",
					 SND_JACK_HEADSET | SND_JACK_BTN_0 |
					 SND_JACK_BTN_1 | SND_JACK_BTN_2 |
					 SND_JACK_BTN_3,
					 jack,
					 nau8825_jack_pins,
					 ARRAY_SIZE(nau8825_jack_pins));
	if (ret) {
		dev_err(rtd->dev, "Headset Jack creation failed: %d\n", ret);
		return ret;
	}

	snd_jack_set_key(jack->jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_1, KEY_VOICECOMMAND);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_2, KEY_VOLUMEUP);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_3, KEY_VOLUMEDOWN);
	ret = snd_soc_component_set_jack(component, jack, NULL);

	if (ret) {
		dev_err(rtd->dev, "Headset Jack call-back failed: %d\n", ret);
		return ret;
	}

	return 0;
};

static void mt8188_headset_codec_exit(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *component = snd_soc_rtd_to_codec(rtd, 0)->component;

	snd_soc_component_set_jack(component, NULL, NULL);
}


static int mt8188_nau8825_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	unsigned int rate = params_rate(params);
	unsigned int bit_width = params_width(params);
	int clk_freq, ret;

	clk_freq = rate * 2 * bit_width;

	/* Configure clock for codec */
	ret = snd_soc_dai_set_sysclk(codec_dai, NAU8825_CLK_FLL_BLK, 0,
				     SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(codec_dai->dev, "can't set BCLK clock %d\n", ret);
		return ret;
	}

	/* Configure pll for codec */
	ret = snd_soc_dai_set_pll(codec_dai, 0, 0, clk_freq,
				  params_rate(params) * 256);
	if (ret < 0) {
		dev_err(codec_dai->dev, "can't set BCLK: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct snd_soc_ops mt8188_nau8825_ops = {
	.hw_params = mt8188_nau8825_hw_params,
};

static int mt8188_rt5682s_i2s_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
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

	ret = snd_soc_dai_set_pll(codec_dai, RT5682_PLL1, RT5682_PLL1_S_BCLK1,
				  rate * 32, rate * 512);
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

	return snd_soc_dai_set_sysclk(cpu_dai, 0, rate * 128,
				      SND_SOC_CLOCK_OUT);
}

static const struct snd_soc_ops mt8188_rt5682s_i2s_ops = {
	.hw_params = mt8188_rt5682s_i2s_hw_params,
};

static int mt8188_sof_be_hw_params(struct snd_pcm_substream *substream,
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

static const struct snd_soc_ops mt8188_sof_be_ops = {
	.hw_params = mt8188_sof_be_hw_params,
};

static int mt8188_es8326_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	unsigned int rate = params_rate(params);
	int ret;

	/* Configure MCLK for codec */
	ret = snd_soc_dai_set_sysclk(codec_dai, 0, rate * 256, SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(codec_dai->dev, "can't set MCLK %d\n", ret);
		return ret;
	}

	/* Configure MCLK for cpu */
	return snd_soc_dai_set_sysclk(cpu_dai, 0, rate * 256, SND_SOC_CLOCK_OUT);
}

static const struct snd_soc_ops mt8188_es8326_ops = {
	.hw_params = mt8188_es8326_hw_params,
};

static struct snd_soc_dai_link mt8188_mt6359_dai_links[] = {
	/* FE */
	[DAI_LINK_DL2_FE] = {
		.name = "DL2_FE",
		.stream_name = "DL2 Playback",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST,
		},
		.dynamic = 1,
		.dpcm_playback = 1,
		.dpcm_merged_chan = 1,
		.dpcm_merged_rate = 1,
		.dpcm_merged_format = 1,
		SND_SOC_DAILINK_REG(playback2),
	},
	[DAI_LINK_DL3_FE] = {
		.name = "DL3_FE",
		.stream_name = "DL3 Playback",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST,
		},
		.dynamic = 1,
		.dpcm_playback = 1,
		.dpcm_merged_chan = 1,
		.dpcm_merged_rate = 1,
		.dpcm_merged_format = 1,
		SND_SOC_DAILINK_REG(playback3),
	},
	[DAI_LINK_DL6_FE] = {
		.name = "DL6_FE",
		.stream_name = "DL6 Playback",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST,
		},
		.dynamic = 1,
		.dpcm_playback = 1,
		.dpcm_merged_chan = 1,
		.dpcm_merged_rate = 1,
		.dpcm_merged_format = 1,
		SND_SOC_DAILINK_REG(playback6),
	},
	[DAI_LINK_DL7_FE] = {
		.name = "DL7_FE",
		.stream_name = "DL7 Playback",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_PRE,
			SND_SOC_DPCM_TRIGGER_PRE,
		},
		.dynamic = 1,
		.dpcm_playback = 1,
		SND_SOC_DAILINK_REG(playback7),
	},
	[DAI_LINK_DL8_FE] = {
		.name = "DL8_FE",
		.stream_name = "DL8 Playback",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST,
		},
		.dynamic = 1,
		.dpcm_playback = 1,
		SND_SOC_DAILINK_REG(playback8),
	},
	[DAI_LINK_DL10_FE] = {
		.name = "DL10_FE",
		.stream_name = "DL10 Playback",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST,
		},
		.dynamic = 1,
		.dpcm_playback = 1,
		SND_SOC_DAILINK_REG(playback10),
	},
	[DAI_LINK_DL11_FE] = {
		.name = "DL11_FE",
		.stream_name = "DL11 Playback",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST,
		},
		.dynamic = 1,
		.dpcm_playback = 1,
		SND_SOC_DAILINK_REG(playback11),
	},
	[DAI_LINK_UL1_FE] = {
		.name = "UL1_FE",
		.stream_name = "UL1 Capture",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_PRE,
			SND_SOC_DPCM_TRIGGER_PRE,
		},
		.dynamic = 1,
		.dpcm_capture = 1,
		SND_SOC_DAILINK_REG(capture1),
	},
	[DAI_LINK_UL2_FE] = {
		.name = "UL2_FE",
		.stream_name = "UL2 Capture",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST,
		},
		.dynamic = 1,
		.dpcm_capture = 1,
		SND_SOC_DAILINK_REG(capture2),
	},
	[DAI_LINK_UL3_FE] = {
		.name = "UL3_FE",
		.stream_name = "UL3 Capture",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST,
		},
		.dynamic = 1,
		.dpcm_capture = 1,
		SND_SOC_DAILINK_REG(capture3),
	},
	[DAI_LINK_UL4_FE] = {
		.name = "UL4_FE",
		.stream_name = "UL4 Capture",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST,
		},
		.dynamic = 1,
		.dpcm_capture = 1,
		.dpcm_merged_chan = 1,
		.dpcm_merged_rate = 1,
		.dpcm_merged_format = 1,
		SND_SOC_DAILINK_REG(capture4),
	},
	[DAI_LINK_UL5_FE] = {
		.name = "UL5_FE",
		.stream_name = "UL5 Capture",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST,
		},
		.dynamic = 1,
		.dpcm_capture = 1,
		.dpcm_merged_chan = 1,
		.dpcm_merged_rate = 1,
		.dpcm_merged_format = 1,
		SND_SOC_DAILINK_REG(capture5),
	},
	[DAI_LINK_UL6_FE] = {
		.name = "UL6_FE",
		.stream_name = "UL6 Capture",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_PRE,
			SND_SOC_DPCM_TRIGGER_PRE,
		},
		.dynamic = 1,
		.dpcm_capture = 1,
		SND_SOC_DAILINK_REG(capture6),
	},
	[DAI_LINK_UL8_FE] = {
		.name = "UL8_FE",
		.stream_name = "UL8 Capture",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST,
		},
		.dynamic = 1,
		.dpcm_capture = 1,
		SND_SOC_DAILINK_REG(capture8),
	},
	[DAI_LINK_UL9_FE] = {
		.name = "UL9_FE",
		.stream_name = "UL9 Capture",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST,
		},
		.dynamic = 1,
		.dpcm_capture = 1,
		SND_SOC_DAILINK_REG(capture9),
	},
	[DAI_LINK_UL10_FE] = {
		.name = "UL10_FE",
		.stream_name = "UL10 Capture",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST,
		},
		.dynamic = 1,
		.dpcm_capture = 1,
		SND_SOC_DAILINK_REG(capture10),
	},
	/* BE */
	[DAI_LINK_DL_SRC_BE] = {
		.name = "DL_SRC_BE",
		.no_pcm = 1,
		.dpcm_playback = 1,
		SND_SOC_DAILINK_REG(dl_src),
	},
	[DAI_LINK_DPTX_BE] = {
		.name = "DPTX_BE",
		.ops = &mt8188_dptx_ops,
		.be_hw_params_fixup = mt8188_dptx_hw_params_fixup,
		.no_pcm = 1,
		.dpcm_playback = 1,
		SND_SOC_DAILINK_REG(dptx),
	},
	[DAI_LINK_ETDM1_IN_BE] = {
		.name = "ETDM1_IN_BE",
		.no_pcm = 1,
		.dai_fmt = SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBP_CFP,
		.dpcm_capture = 1,
		.ignore_suspend = 1,
		SND_SOC_DAILINK_REG(etdm1_in),
	},
	[DAI_LINK_ETDM2_IN_BE] = {
		.name = "ETDM2_IN_BE",
		.no_pcm = 1,
		.dai_fmt = SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBP_CFP,
		.dpcm_capture = 1,
		SND_SOC_DAILINK_REG(etdm2_in),
	},
	[DAI_LINK_ETDM1_OUT_BE] = {
		.name = "ETDM1_OUT_BE",
		.no_pcm = 1,
		.dai_fmt = SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBC_CFC,
		.dpcm_playback = 1,
		SND_SOC_DAILINK_REG(etdm1_out),
	},
	[DAI_LINK_ETDM2_OUT_BE] = {
		.name = "ETDM2_OUT_BE",
		.no_pcm = 1,
		.dai_fmt = SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBC_CFC,
		.dpcm_playback = 1,
		SND_SOC_DAILINK_REG(etdm2_out),
	},
	[DAI_LINK_ETDM3_OUT_BE] = {
		.name = "ETDM3_OUT_BE",
		.no_pcm = 1,
		.dai_fmt = SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBC_CFC,
		.dpcm_playback = 1,
		SND_SOC_DAILINK_REG(etdm3_out),
	},
	[DAI_LINK_PCM1_BE] = {
		.name = "PCM1_BE",
		.no_pcm = 1,
		.dai_fmt = SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBC_CFC,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		SND_SOC_DAILINK_REG(pcm1),
	},
	[DAI_LINK_UL_SRC_BE] = {
		.name = "UL_SRC_BE",
		.no_pcm = 1,
		.dpcm_capture = 1,
		SND_SOC_DAILINK_REG(ul_src),
	},

	/* SOF BE */
	[DAI_LINK_SOF_DL2_BE] = {
		.name = "AFE_SOF_DL2",
		.no_pcm = 1,
		.dpcm_playback = 1,
		.ops = &mt8188_sof_be_ops,
		SND_SOC_DAILINK_REG(AFE_SOF_DL2),
	},
	[DAI_LINK_SOF_DL3_BE] = {
		.name = "AFE_SOF_DL3",
		.no_pcm = 1,
		.dpcm_playback = 1,
		.ops = &mt8188_sof_be_ops,
		SND_SOC_DAILINK_REG(AFE_SOF_DL3),
	},
	[DAI_LINK_SOF_UL4_BE] = {
		.name = "AFE_SOF_UL4",
		.no_pcm = 1,
		.dpcm_capture = 1,
		.ops = &mt8188_sof_be_ops,
		SND_SOC_DAILINK_REG(AFE_SOF_UL4),
	},
	[DAI_LINK_SOF_UL5_BE] = {
		.name = "AFE_SOF_UL5",
		.no_pcm = 1,
		.dpcm_capture = 1,
		.ops = &mt8188_sof_be_ops,
		SND_SOC_DAILINK_REG(AFE_SOF_UL5),
	},
};

static void mt8188_fixup_controls(struct snd_soc_card *card)
{
	struct mtk_soc_card_data *soc_card_data = snd_soc_card_get_drvdata(card);
	struct mt8188_mt6359_priv *priv = soc_card_data->mach_priv;
	struct mt8188_card_data *card_data = (struct mt8188_card_data *)priv->private_data;
	struct snd_kcontrol *kctl;

	if (card_data->quirk & (NAU8825_HS_PRESENT | RT5682S_HS_PRESENT | ES8326_HS_PRESENT)) {
		struct snd_soc_dapm_widget *w, *next_w;

		for_each_card_widgets_safe(card, w, next_w) {
			if (strcmp(w->name, "Headphone"))
				continue;

			snd_soc_dapm_free_widget(w);
		}

		kctl = snd_ctl_find_id_mixer(card->snd_card, "Headphone Switch");
		if (kctl)
			snd_ctl_remove(card->snd_card, kctl);
		else
			dev_warn(card->dev, "Cannot find ctl : Headphone Switch\n");
	}
}

static struct snd_soc_card mt8188_mt6359_soc_card = {
	.owner = THIS_MODULE,
	.dai_link = mt8188_mt6359_dai_links,
	.num_links = ARRAY_SIZE(mt8188_mt6359_dai_links),
	.dapm_widgets = mt8188_mt6359_widgets,
	.num_dapm_widgets = ARRAY_SIZE(mt8188_mt6359_widgets),
	.dapm_routes = mt8188_mt6359_routes,
	.num_dapm_routes = ARRAY_SIZE(mt8188_mt6359_routes),
	.controls = mt8188_mt6359_controls,
	.num_controls = ARRAY_SIZE(mt8188_mt6359_controls),
	.fixup_controls = mt8188_fixup_controls,
};

static int mt8188_mt6359_dev_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &mt8188_mt6359_soc_card;
	struct device_node *platform_node;
	struct device_node *adsp_node;
	struct mtk_soc_card_data *soc_card_data;
	struct mt8188_mt6359_priv *priv;
	struct mt8188_card_data *card_data;
	struct snd_soc_dai_link *dai_link;
	bool init_mt6359 = false;
	bool init_es8326 = false;
	bool init_nau8825 = false;
	bool init_rt5682s = false;
	bool init_max98390 = false;
	bool init_dumb = false;
	int ret, i;

	card_data = (struct mt8188_card_data *)of_device_get_match_data(&pdev->dev);
	card->dev = &pdev->dev;

	ret = snd_soc_of_parse_card_name(card, "model");
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "%s new card name parsing error\n",
				     __func__);

	if (!card->name)
		card->name = card_data->name;

	if (of_property_read_bool(pdev->dev.of_node, "audio-routing")) {
		ret = snd_soc_of_parse_audio_routing(card, "audio-routing");
		if (ret)
			return ret;
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	soc_card_data = devm_kzalloc(&pdev->dev, sizeof(*card_data), GFP_KERNEL);
	if (!soc_card_data)
		return -ENOMEM;

	soc_card_data->mach_priv = priv;

	adsp_node = of_parse_phandle(pdev->dev.of_node, "mediatek,adsp", 0);
	if (adsp_node) {
		struct mtk_sof_priv *sof_priv;

		sof_priv = devm_kzalloc(&pdev->dev, sizeof(*sof_priv), GFP_KERNEL);
		if (!sof_priv) {
			ret = -ENOMEM;
			goto err_adsp_node;
		}
		sof_priv->conn_streams = g_sof_conn_streams;
		sof_priv->num_streams = ARRAY_SIZE(g_sof_conn_streams);
		soc_card_data->sof_priv = sof_priv;
		card->probe = mtk_sof_card_probe;
		card->late_probe = mtk_sof_card_late_probe;
		if (!card->topology_shortname_created) {
			snprintf(card->topology_shortname, 32, "sof-%s", card->name);
			card->topology_shortname_created = true;
		}
		card->name = card->topology_shortname;
	}

	if (of_property_read_bool(pdev->dev.of_node, "mediatek,dai-link")) {
		ret = mtk_sof_dailink_parse_of(card, pdev->dev.of_node,
					       "mediatek,dai-link",
					       mt8188_mt6359_dai_links,
					       ARRAY_SIZE(mt8188_mt6359_dai_links));
		if (ret) {
			dev_err_probe(&pdev->dev, ret, "Parse dai-link fail\n");
			goto err_adsp_node;
		}
	} else {
		if (!adsp_node)
			card->num_links = DAI_LINK_REGULAR_NUM;
	}

	platform_node = of_parse_phandle(pdev->dev.of_node,
					 "mediatek,platform", 0);
	if (!platform_node) {
		ret = dev_err_probe(&pdev->dev, -EINVAL,
				    "Property 'platform' missing or invalid\n");
		goto err_adsp_node;

	}

	ret = parse_dai_link_info(card);
	if (ret)
		goto err;

	for_each_card_prelinks(card, i, dai_link) {
		if (!dai_link->platforms->name) {
			if (!strncmp(dai_link->name, "AFE_SOF", strlen("AFE_SOF")) && adsp_node)
				dai_link->platforms->of_node = adsp_node;
			else
				dai_link->platforms->of_node = platform_node;
		}

		if (strcmp(dai_link->name, "DPTX_BE") == 0) {
			if (strcmp(dai_link->codecs->dai_name, "snd-soc-dummy-dai"))
				dai_link->init = mt8188_dptx_codec_init;
		} else if (strcmp(dai_link->name, "ETDM3_OUT_BE") == 0) {
			if (strcmp(dai_link->codecs->dai_name, "snd-soc-dummy-dai"))
				dai_link->init = mt8188_hdmi_codec_init;
		} else if (strcmp(dai_link->name, "DL_SRC_BE") == 0 ||
			   strcmp(dai_link->name, "UL_SRC_BE") == 0) {
			if (!init_mt6359) {
				dai_link->init = mt8188_mt6359_init;
				init_mt6359 = true;
			}
		} else if (strcmp(dai_link->name, "ETDM1_OUT_BE") == 0 ||
			   strcmp(dai_link->name, "ETDM2_OUT_BE") == 0 ||
			   strcmp(dai_link->name, "ETDM1_IN_BE") == 0 ||
			   strcmp(dai_link->name, "ETDM2_IN_BE") == 0) {
			if (!strcmp(dai_link->codecs->dai_name, MAX98390_CODEC_DAI)) {
				/*
				 * The TDM protocol settings with fixed 4 slots are defined in
				 * mt8188_max98390_ops. Two amps is I2S mode,
				 * SOC and codec don't require TDM settings.
				 */
				if (!(card_data->quirk & MAX98390_TWO_AMP)) {
					dai_link->ops = &mt8188_max98390_ops;
				}
				if (!init_max98390) {
					dai_link->init = mt8188_max98390_codec_init;
					init_max98390 = true;
				}
			} else if (!strcmp(dai_link->codecs->dai_name, NAU8825_CODEC_DAI)) {
				dai_link->ops = &mt8188_nau8825_ops;
				if (!init_nau8825) {
					dai_link->init = mt8188_headset_codec_init;
					dai_link->exit = mt8188_headset_codec_exit;
					init_nau8825 = true;
				}
			} else if (!strcmp(dai_link->codecs->dai_name, RT5682S_CODEC_DAI)) {
				dai_link->ops = &mt8188_rt5682s_i2s_ops;
				if (!init_rt5682s) {
					dai_link->init = mt8188_headset_codec_init;
					dai_link->exit = mt8188_headset_codec_exit;
					init_rt5682s = true;
				}
			} else if (!strcmp(dai_link->codecs->dai_name, ES8326_CODEC_DAI)) {
				dai_link->ops = &mt8188_es8326_ops;
				if (!init_es8326) {
					dai_link->init = mt8188_headset_codec_init;
					dai_link->exit = mt8188_headset_codec_exit;
					init_es8326 = true;
				}
			} else {
				if (strcmp(dai_link->codecs->dai_name, "snd-soc-dummy-dai")) {
					if (!init_dumb) {
						dai_link->init = mt8188_dumb_amp_init;
						init_dumb = true;
					}
				}
			}
		}
	}

	priv->private_data = card_data;
	snd_soc_card_set_drvdata(card, soc_card_data);

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret)
		dev_err_probe(&pdev->dev, ret, "%s snd_soc_register_card fail\n",
			      __func__);
err:
	of_node_put(platform_node);
	clean_card_reference(card);

err_adsp_node:
	of_node_put(adsp_node);

	return ret;
}

static struct mt8188_card_data mt8188_evb_card = {
	.name = "mt8188_mt6359",
};

static struct mt8188_card_data mt8188_nau8825_card = {
	.name = "mt8188_nau8825",
	.quirk = NAU8825_HS_PRESENT,
};

static struct mt8188_card_data mt8188_rt5682s_card = {
	.name = "mt8188_rt5682s",
	.quirk = RT5682S_HS_PRESENT | MAX98390_TWO_AMP,
};

static struct mt8188_card_data mt8188_es8326_card = {
	.name = "mt8188_es8326",
	.quirk = ES8326_HS_PRESENT | MAX98390_TWO_AMP,
};

static const struct of_device_id mt8188_mt6359_dt_match[] = {
	{ .compatible = "mediatek,mt8188-mt6359-evb", .data = &mt8188_evb_card, },
	{ .compatible = "mediatek,mt8188-nau8825", .data = &mt8188_nau8825_card, },
	{ .compatible = "mediatek,mt8188-rt5682s", .data = &mt8188_rt5682s_card, },
	{ .compatible = "mediatek,mt8188-es8326", .data = &mt8188_es8326_card, },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, mt8188_mt6359_dt_match);

static struct platform_driver mt8188_mt6359_driver = {
	.driver = {
		.name = "mt8188_mt6359",
		.of_match_table = mt8188_mt6359_dt_match,
		.pm = &snd_soc_pm_ops,
	},
	.probe = mt8188_mt6359_dev_probe,
};

module_platform_driver(mt8188_mt6359_driver);

/* Module information */
MODULE_DESCRIPTION("MT8188-MT6359 ALSA SoC machine driver");
MODULE_AUTHOR("Trevor Wu <trevor.wu@mediatek.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("mt8188 mt6359 soc card");
