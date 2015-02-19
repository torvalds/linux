/*
 * wm8996.c - WM8996 audio codec interface
 *
 * Copyright 2011-2 Wolfson Microelectronics PLC.
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/gcd.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <trace/events/asoc.h>

#include <sound/wm8996.h>
#include "wm8996.h"

#define WM8996_AIFS 2

#define HPOUT1L 1
#define HPOUT1R 2
#define HPOUT2L 4
#define HPOUT2R 8

#define WM8996_NUM_SUPPLIES 3
static const char *wm8996_supply_names[WM8996_NUM_SUPPLIES] = {
	"DBVDD",
	"AVDD1",
	"AVDD2",
};

struct wm8996_priv {
	struct device *dev;
	struct regmap *regmap;
	struct snd_soc_codec *codec;

	int ldo1ena;

	int sysclk;
	int sysclk_src;

	int fll_src;
	int fll_fref;
	int fll_fout;

	struct completion fll_lock;

	u16 dcs_pending;
	struct completion dcs_done;

	u16 hpout_ena;
	u16 hpout_pending;

	struct regulator_bulk_data supplies[WM8996_NUM_SUPPLIES];
	struct notifier_block disable_nb[WM8996_NUM_SUPPLIES];
	int bg_ena;

	struct wm8996_pdata pdata;

	int rx_rate[WM8996_AIFS];
	int bclk_rate[WM8996_AIFS];

	/* Platform dependant ReTune mobile configuration */
	int num_retune_mobile_texts;
	const char **retune_mobile_texts;
	int retune_mobile_cfg[2];
	struct soc_enum retune_mobile_enum;

	struct snd_soc_jack *jack;
	bool detecting;
	bool jack_mic;
	int jack_flips;
	wm8996_polarity_fn polarity_cb;

#ifdef CONFIG_GPIOLIB
	struct gpio_chip gpio_chip;
#endif
};

/* We can't use the same notifier block for more than one supply and
 * there's no way I can see to get from a callback to the caller
 * except container_of().
 */
#define WM8996_REGULATOR_EVENT(n) \
static int wm8996_regulator_event_##n(struct notifier_block *nb, \
				    unsigned long event, void *data)	\
{ \
	struct wm8996_priv *wm8996 = container_of(nb, struct wm8996_priv, \
						  disable_nb[n]); \
	if (event & REGULATOR_EVENT_DISABLE) { \
		regcache_mark_dirty(wm8996->regmap);	\
	} \
	return 0; \
}

WM8996_REGULATOR_EVENT(0)
WM8996_REGULATOR_EVENT(1)
WM8996_REGULATOR_EVENT(2)

static struct reg_default wm8996_reg[] = {
	{ WM8996_POWER_MANAGEMENT_1, 0x0 },
	{ WM8996_POWER_MANAGEMENT_2, 0x0 },
	{ WM8996_POWER_MANAGEMENT_3, 0x0 },
	{ WM8996_POWER_MANAGEMENT_4, 0x0 },
	{ WM8996_POWER_MANAGEMENT_5, 0x0 },
	{ WM8996_POWER_MANAGEMENT_6, 0x0 },
	{ WM8996_POWER_MANAGEMENT_7, 0x10 },
	{ WM8996_POWER_MANAGEMENT_8, 0x0 },
	{ WM8996_LEFT_LINE_INPUT_VOLUME, 0x0 },
	{ WM8996_RIGHT_LINE_INPUT_VOLUME, 0x0 },
	{ WM8996_LINE_INPUT_CONTROL, 0x0 },
	{ WM8996_DAC1_HPOUT1_VOLUME, 0x88 },
	{ WM8996_DAC2_HPOUT2_VOLUME, 0x88 },
	{ WM8996_DAC1_LEFT_VOLUME, 0x2c0 },
	{ WM8996_DAC1_RIGHT_VOLUME, 0x2c0 },
	{ WM8996_DAC2_LEFT_VOLUME, 0x2c0 },
	{ WM8996_DAC2_RIGHT_VOLUME, 0x2c0 },
	{ WM8996_OUTPUT1_LEFT_VOLUME, 0x80 },
	{ WM8996_OUTPUT1_RIGHT_VOLUME, 0x80 },
	{ WM8996_OUTPUT2_LEFT_VOLUME, 0x80 },
	{ WM8996_OUTPUT2_RIGHT_VOLUME, 0x80 },
	{ WM8996_MICBIAS_1, 0x39 },
	{ WM8996_MICBIAS_2, 0x39 },
	{ WM8996_LDO_1, 0x3 },
	{ WM8996_LDO_2, 0x13 },
	{ WM8996_ACCESSORY_DETECT_MODE_1, 0x4 },
	{ WM8996_ACCESSORY_DETECT_MODE_2, 0x0 },
	{ WM8996_HEADPHONE_DETECT_1, 0x20 },
	{ WM8996_HEADPHONE_DETECT_2, 0x0 },
	{ WM8996_MIC_DETECT_1, 0x7600 },
	{ WM8996_MIC_DETECT_2, 0xbf },
	{ WM8996_CHARGE_PUMP_1, 0x1f25 },
	{ WM8996_CHARGE_PUMP_2, 0xab19 },
	{ WM8996_DC_SERVO_1, 0x0 },
	{ WM8996_DC_SERVO_3, 0x0 },
	{ WM8996_DC_SERVO_5, 0x2a2a },
	{ WM8996_DC_SERVO_6, 0x0 },
	{ WM8996_DC_SERVO_7, 0x0 },
	{ WM8996_ANALOGUE_HP_1, 0x0 },
	{ WM8996_ANALOGUE_HP_2, 0x0 },
	{ WM8996_CONTROL_INTERFACE_1, 0x8004 },
	{ WM8996_WRITE_SEQUENCER_CTRL_1, 0x0 },
	{ WM8996_WRITE_SEQUENCER_CTRL_2, 0x0 },
	{ WM8996_AIF_CLOCKING_1, 0x0 },
	{ WM8996_AIF_CLOCKING_2, 0x0 },
	{ WM8996_CLOCKING_1, 0x10 },
	{ WM8996_CLOCKING_2, 0x0 },
	{ WM8996_AIF_RATE, 0x83 },
	{ WM8996_FLL_CONTROL_1, 0x0 },
	{ WM8996_FLL_CONTROL_2, 0x0 },
	{ WM8996_FLL_CONTROL_3, 0x0 },
	{ WM8996_FLL_CONTROL_4, 0x5dc0 },
	{ WM8996_FLL_CONTROL_5, 0xc84 },
	{ WM8996_FLL_EFS_1, 0x0 },
	{ WM8996_FLL_EFS_2, 0x2 },
	{ WM8996_AIF1_CONTROL, 0x0 },
	{ WM8996_AIF1_BCLK, 0x0 },
	{ WM8996_AIF1_TX_LRCLK_1, 0x80 },
	{ WM8996_AIF1_TX_LRCLK_2, 0x8 },
	{ WM8996_AIF1_RX_LRCLK_1, 0x80 },
	{ WM8996_AIF1_RX_LRCLK_2, 0x0 },
	{ WM8996_AIF1TX_DATA_CONFIGURATION_1, 0x1818 },
	{ WM8996_AIF1TX_DATA_CONFIGURATION_2, 0 },
	{ WM8996_AIF1RX_DATA_CONFIGURATION, 0x1818 },
	{ WM8996_AIF1TX_CHANNEL_0_CONFIGURATION, 0x0 },
	{ WM8996_AIF1TX_CHANNEL_1_CONFIGURATION, 0x0 },
	{ WM8996_AIF1TX_CHANNEL_2_CONFIGURATION, 0x0 },
	{ WM8996_AIF1TX_CHANNEL_3_CONFIGURATION, 0x0 },
	{ WM8996_AIF1TX_CHANNEL_4_CONFIGURATION, 0x0 },
	{ WM8996_AIF1TX_CHANNEL_5_CONFIGURATION, 0x0 },
	{ WM8996_AIF1RX_CHANNEL_0_CONFIGURATION, 0x0 },
	{ WM8996_AIF1RX_CHANNEL_1_CONFIGURATION, 0x0 },
	{ WM8996_AIF1RX_CHANNEL_2_CONFIGURATION, 0x0 },
	{ WM8996_AIF1RX_CHANNEL_3_CONFIGURATION, 0x0 },
	{ WM8996_AIF1RX_CHANNEL_4_CONFIGURATION, 0x0 },
	{ WM8996_AIF1RX_CHANNEL_5_CONFIGURATION, 0x0 },
	{ WM8996_AIF1RX_MONO_CONFIGURATION, 0x0 },
	{ WM8996_AIF1TX_TEST, 0x7 },
	{ WM8996_AIF2_CONTROL, 0x0 },
	{ WM8996_AIF2_BCLK, 0x0 },
	{ WM8996_AIF2_TX_LRCLK_1, 0x80 },
	{ WM8996_AIF2_TX_LRCLK_2, 0x8 },
	{ WM8996_AIF2_RX_LRCLK_1, 0x80 },
	{ WM8996_AIF2_RX_LRCLK_2, 0x0 },
	{ WM8996_AIF2TX_DATA_CONFIGURATION_1, 0x1818 },
	{ WM8996_AIF2RX_DATA_CONFIGURATION, 0x1818 },
	{ WM8996_AIF2RX_DATA_CONFIGURATION, 0x0 },
	{ WM8996_AIF2TX_CHANNEL_0_CONFIGURATION, 0x0 },
	{ WM8996_AIF2TX_CHANNEL_1_CONFIGURATION, 0x0 },
	{ WM8996_AIF2RX_CHANNEL_0_CONFIGURATION, 0x0 },
	{ WM8996_AIF2RX_CHANNEL_1_CONFIGURATION, 0x0 },
	{ WM8996_AIF2RX_MONO_CONFIGURATION, 0x0 },
	{ WM8996_AIF2TX_TEST, 0x1 },
	{ WM8996_DSP1_TX_LEFT_VOLUME, 0xc0 },
	{ WM8996_DSP1_TX_RIGHT_VOLUME, 0xc0 },
	{ WM8996_DSP1_RX_LEFT_VOLUME, 0xc0 },
	{ WM8996_DSP1_RX_RIGHT_VOLUME, 0xc0 },
	{ WM8996_DSP1_TX_FILTERS, 0x2000 },
	{ WM8996_DSP1_RX_FILTERS_1, 0x200 },
	{ WM8996_DSP1_RX_FILTERS_2, 0x10 },
	{ WM8996_DSP1_DRC_1, 0x98 },
	{ WM8996_DSP1_DRC_2, 0x845 },
	{ WM8996_DSP1_RX_EQ_GAINS_1, 0x6318 },
	{ WM8996_DSP1_RX_EQ_GAINS_2, 0x6300 },
	{ WM8996_DSP1_RX_EQ_BAND_1_A, 0xfca },
	{ WM8996_DSP1_RX_EQ_BAND_1_B, 0x400 },
	{ WM8996_DSP1_RX_EQ_BAND_1_PG, 0xd8 },
	{ WM8996_DSP1_RX_EQ_BAND_2_A, 0x1eb5 },
	{ WM8996_DSP1_RX_EQ_BAND_2_B, 0xf145 },
	{ WM8996_DSP1_RX_EQ_BAND_2_C, 0xb75 },
	{ WM8996_DSP1_RX_EQ_BAND_2_PG, 0x1c5 },
	{ WM8996_DSP1_RX_EQ_BAND_3_A, 0x1c58 },
	{ WM8996_DSP1_RX_EQ_BAND_3_B, 0xf373 },
	{ WM8996_DSP1_RX_EQ_BAND_3_C, 0xa54 },
	{ WM8996_DSP1_RX_EQ_BAND_3_PG, 0x558 },
	{ WM8996_DSP1_RX_EQ_BAND_4_A, 0x168e },
	{ WM8996_DSP1_RX_EQ_BAND_4_B, 0xf829 },
	{ WM8996_DSP1_RX_EQ_BAND_4_C, 0x7ad },
	{ WM8996_DSP1_RX_EQ_BAND_4_PG, 0x1103 },
	{ WM8996_DSP1_RX_EQ_BAND_5_A, 0x564 },
	{ WM8996_DSP1_RX_EQ_BAND_5_B, 0x559 },
	{ WM8996_DSP1_RX_EQ_BAND_5_PG, 0x4000 },
	{ WM8996_DSP2_TX_LEFT_VOLUME, 0xc0 },
	{ WM8996_DSP2_TX_RIGHT_VOLUME, 0xc0 },
	{ WM8996_DSP2_RX_LEFT_VOLUME, 0xc0 },
	{ WM8996_DSP2_RX_RIGHT_VOLUME, 0xc0 },
	{ WM8996_DSP2_TX_FILTERS, 0x2000 },
	{ WM8996_DSP2_RX_FILTERS_1, 0x200 },
	{ WM8996_DSP2_RX_FILTERS_2, 0x10 },
	{ WM8996_DSP2_DRC_1, 0x98 },
	{ WM8996_DSP2_DRC_2, 0x845 },
	{ WM8996_DSP2_RX_EQ_GAINS_1, 0x6318 },
	{ WM8996_DSP2_RX_EQ_GAINS_2, 0x6300 },
	{ WM8996_DSP2_RX_EQ_BAND_1_A, 0xfca },
	{ WM8996_DSP2_RX_EQ_BAND_1_B, 0x400 },
	{ WM8996_DSP2_RX_EQ_BAND_1_PG, 0xd8 },
	{ WM8996_DSP2_RX_EQ_BAND_2_A, 0x1eb5 },
	{ WM8996_DSP2_RX_EQ_BAND_2_B, 0xf145 },
	{ WM8996_DSP2_RX_EQ_BAND_2_C, 0xb75 },
	{ WM8996_DSP2_RX_EQ_BAND_2_PG, 0x1c5 },
	{ WM8996_DSP2_RX_EQ_BAND_3_A, 0x1c58 },
	{ WM8996_DSP2_RX_EQ_BAND_3_B, 0xf373 },
	{ WM8996_DSP2_RX_EQ_BAND_3_C, 0xa54 },
	{ WM8996_DSP2_RX_EQ_BAND_3_PG, 0x558 },
	{ WM8996_DSP2_RX_EQ_BAND_4_A, 0x168e },
	{ WM8996_DSP2_RX_EQ_BAND_4_B, 0xf829 },
	{ WM8996_DSP2_RX_EQ_BAND_4_C, 0x7ad },
	{ WM8996_DSP2_RX_EQ_BAND_4_PG, 0x1103 },
	{ WM8996_DSP2_RX_EQ_BAND_5_A, 0x564 },
	{ WM8996_DSP2_RX_EQ_BAND_5_B, 0x559 },
	{ WM8996_DSP2_RX_EQ_BAND_5_PG, 0x4000 },
	{ WM8996_DAC1_MIXER_VOLUMES, 0x0 },
	{ WM8996_DAC1_LEFT_MIXER_ROUTING, 0x0 },
	{ WM8996_DAC1_RIGHT_MIXER_ROUTING, 0x0 },
	{ WM8996_DAC2_MIXER_VOLUMES, 0x0 },
	{ WM8996_DAC2_LEFT_MIXER_ROUTING, 0x0 },
	{ WM8996_DAC2_RIGHT_MIXER_ROUTING, 0x0 },
	{ WM8996_DSP1_TX_LEFT_MIXER_ROUTING, 0x0 },
	{ WM8996_DSP1_TX_RIGHT_MIXER_ROUTING, 0x0 },
	{ WM8996_DSP2_TX_LEFT_MIXER_ROUTING, 0x0 },
	{ WM8996_DSP2_TX_RIGHT_MIXER_ROUTING, 0x0 },
	{ WM8996_DSP_TX_MIXER_SELECT, 0x0 },
	{ WM8996_DAC_SOFTMUTE, 0x0 },
	{ WM8996_OVERSAMPLING, 0xd },
	{ WM8996_SIDETONE, 0x1040 },
	{ WM8996_GPIO_1, 0xa101 },
	{ WM8996_GPIO_2, 0xa101 },
	{ WM8996_GPIO_3, 0xa101 },
	{ WM8996_GPIO_4, 0xa101 },
	{ WM8996_GPIO_5, 0xa101 },
	{ WM8996_PULL_CONTROL_1, 0x0 },
	{ WM8996_PULL_CONTROL_2, 0x140 },
	{ WM8996_INTERRUPT_STATUS_1_MASK, 0x1f },
	{ WM8996_INTERRUPT_STATUS_2_MASK, 0x1ecf },
	{ WM8996_LEFT_PDM_SPEAKER, 0x0 },
	{ WM8996_RIGHT_PDM_SPEAKER, 0x1 },
	{ WM8996_PDM_SPEAKER_MUTE_SEQUENCE, 0x69 },
	{ WM8996_PDM_SPEAKER_VOLUME, 0x66 },
};

static const DECLARE_TLV_DB_SCALE(inpga_tlv, 0, 100, 0);
static const DECLARE_TLV_DB_SCALE(sidetone_tlv, -3600, 150, 0);
static const DECLARE_TLV_DB_SCALE(digital_tlv, -7200, 75, 1);
static const DECLARE_TLV_DB_SCALE(out_digital_tlv, -1200, 150, 0);
static const DECLARE_TLV_DB_SCALE(out_tlv, -900, 75, 0);
static const DECLARE_TLV_DB_SCALE(spk_tlv, -900, 150, 0);
static const DECLARE_TLV_DB_SCALE(eq_tlv, -1200, 100, 0);
static const DECLARE_TLV_DB_SCALE(threedstereo_tlv, -1600, 183, 1);

static const char *sidetone_hpf_text[] = {
	"2.9kHz", "1.5kHz", "735Hz", "403Hz", "196Hz", "98Hz", "49Hz"
};

static SOC_ENUM_SINGLE_DECL(sidetone_hpf,
			    WM8996_SIDETONE, 7, sidetone_hpf_text);

static const char *hpf_mode_text[] = {
	"HiFi", "Custom", "Voice"
};

static SOC_ENUM_SINGLE_DECL(dsp1tx_hpf_mode,
			    WM8996_DSP1_TX_FILTERS, 3, hpf_mode_text);

static SOC_ENUM_SINGLE_DECL(dsp2tx_hpf_mode,
			    WM8996_DSP2_TX_FILTERS, 3, hpf_mode_text);

static const char *hpf_cutoff_text[] = {
	"50Hz", "75Hz", "100Hz", "150Hz", "200Hz", "300Hz", "400Hz"
};

static SOC_ENUM_SINGLE_DECL(dsp1tx_hpf_cutoff,
			    WM8996_DSP1_TX_FILTERS, 0, hpf_cutoff_text);

static SOC_ENUM_SINGLE_DECL(dsp2tx_hpf_cutoff,
			    WM8996_DSP2_TX_FILTERS, 0, hpf_cutoff_text);

static void wm8996_set_retune_mobile(struct snd_soc_codec *codec, int block)
{
	struct wm8996_priv *wm8996 = snd_soc_codec_get_drvdata(codec);
	struct wm8996_pdata *pdata = &wm8996->pdata;
	int base, best, best_val, save, i, cfg, iface;

	if (!wm8996->num_retune_mobile_texts)
		return;

	switch (block) {
	case 0:
		base = WM8996_DSP1_RX_EQ_GAINS_1;
		if (snd_soc_read(codec, WM8996_POWER_MANAGEMENT_8) &
		    WM8996_DSP1RX_SRC)
			iface = 1;
		else
			iface = 0;
		break;
	case 1:
		base = WM8996_DSP1_RX_EQ_GAINS_2;
		if (snd_soc_read(codec, WM8996_POWER_MANAGEMENT_8) &
		    WM8996_DSP2RX_SRC)
			iface = 1;
		else
			iface = 0;
		break;
	default:
		return;
	}

	/* Find the version of the currently selected configuration
	 * with the nearest sample rate. */
	cfg = wm8996->retune_mobile_cfg[block];
	best = 0;
	best_val = INT_MAX;
	for (i = 0; i < pdata->num_retune_mobile_cfgs; i++) {
		if (strcmp(pdata->retune_mobile_cfgs[i].name,
			   wm8996->retune_mobile_texts[cfg]) == 0 &&
		    abs(pdata->retune_mobile_cfgs[i].rate
			- wm8996->rx_rate[iface]) < best_val) {
			best = i;
			best_val = abs(pdata->retune_mobile_cfgs[i].rate
				       - wm8996->rx_rate[iface]);
		}
	}

	dev_dbg(codec->dev, "ReTune Mobile %d %s/%dHz for %dHz sample rate\n",
		block,
		pdata->retune_mobile_cfgs[best].name,
		pdata->retune_mobile_cfgs[best].rate,
		wm8996->rx_rate[iface]);

	/* The EQ will be disabled while reconfiguring it, remember the
	 * current configuration. 
	 */
	save = snd_soc_read(codec, base);
	save &= WM8996_DSP1RX_EQ_ENA;

	for (i = 0; i < ARRAY_SIZE(pdata->retune_mobile_cfgs[best].regs); i++)
		snd_soc_update_bits(codec, base + i, 0xffff,
				    pdata->retune_mobile_cfgs[best].regs[i]);

	snd_soc_update_bits(codec, base, WM8996_DSP1RX_EQ_ENA, save);
}

/* Icky as hell but saves code duplication */
static int wm8996_get_retune_mobile_block(const char *name)
{
	if (strcmp(name, "DSP1 EQ Mode") == 0)
		return 0;
	if (strcmp(name, "DSP2 EQ Mode") == 0)
		return 1;
	return -EINVAL;
}

static int wm8996_put_retune_mobile_enum(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct wm8996_priv *wm8996 = snd_soc_codec_get_drvdata(codec);
	struct wm8996_pdata *pdata = &wm8996->pdata;
	int block = wm8996_get_retune_mobile_block(kcontrol->id.name);
	int value = ucontrol->value.integer.value[0];

	if (block < 0)
		return block;

	if (value >= pdata->num_retune_mobile_cfgs)
		return -EINVAL;

	wm8996->retune_mobile_cfg[block] = value;

	wm8996_set_retune_mobile(codec, block);

	return 0;
}

static int wm8996_get_retune_mobile_enum(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct wm8996_priv *wm8996 = snd_soc_codec_get_drvdata(codec);
	int block = wm8996_get_retune_mobile_block(kcontrol->id.name);

	if (block < 0)
		return block;
	ucontrol->value.enumerated.item[0] = wm8996->retune_mobile_cfg[block];

	return 0;
}

static const struct snd_kcontrol_new wm8996_snd_controls[] = {
SOC_DOUBLE_R_TLV("Capture Volume", WM8996_LEFT_LINE_INPUT_VOLUME,
		 WM8996_RIGHT_LINE_INPUT_VOLUME, 0, 31, 0, inpga_tlv),
SOC_DOUBLE_R("Capture ZC Switch", WM8996_LEFT_LINE_INPUT_VOLUME,
	     WM8996_RIGHT_LINE_INPUT_VOLUME, 5, 1, 0),

SOC_DOUBLE_TLV("DAC1 Sidetone Volume", WM8996_DAC1_MIXER_VOLUMES,
	       0, 5, 24, 0, sidetone_tlv),
SOC_DOUBLE_TLV("DAC2 Sidetone Volume", WM8996_DAC2_MIXER_VOLUMES,
	       0, 5, 24, 0, sidetone_tlv),
SOC_SINGLE("Sidetone LPF Switch", WM8996_SIDETONE, 12, 1, 0),
SOC_ENUM("Sidetone HPF Cut-off", sidetone_hpf),
SOC_SINGLE("Sidetone HPF Switch", WM8996_SIDETONE, 6, 1, 0),

SOC_DOUBLE_R_TLV("DSP1 Capture Volume", WM8996_DSP1_TX_LEFT_VOLUME,
		 WM8996_DSP1_TX_RIGHT_VOLUME, 1, 96, 0, digital_tlv),
SOC_DOUBLE_R_TLV("DSP2 Capture Volume", WM8996_DSP2_TX_LEFT_VOLUME,
		 WM8996_DSP2_TX_RIGHT_VOLUME, 1, 96, 0, digital_tlv),

SOC_SINGLE("DSP1 Capture Notch Filter Switch", WM8996_DSP1_TX_FILTERS,
	   13, 1, 0),
SOC_DOUBLE("DSP1 Capture HPF Switch", WM8996_DSP1_TX_FILTERS, 12, 11, 1, 0),
SOC_ENUM("DSP1 Capture HPF Mode", dsp1tx_hpf_mode),
SOC_ENUM("DSP1 Capture HPF Cutoff", dsp1tx_hpf_cutoff),

SOC_SINGLE("DSP2 Capture Notch Filter Switch", WM8996_DSP2_TX_FILTERS,
	   13, 1, 0),
SOC_DOUBLE("DSP2 Capture HPF Switch", WM8996_DSP2_TX_FILTERS, 12, 11, 1, 0),
SOC_ENUM("DSP2 Capture HPF Mode", dsp2tx_hpf_mode),
SOC_ENUM("DSP2 Capture HPF Cutoff", dsp2tx_hpf_cutoff),

SOC_DOUBLE_R_TLV("DSP1 Playback Volume", WM8996_DSP1_RX_LEFT_VOLUME,
		 WM8996_DSP1_RX_RIGHT_VOLUME, 1, 112, 0, digital_tlv),
SOC_SINGLE("DSP1 Playback Switch", WM8996_DSP1_RX_FILTERS_1, 9, 1, 1),

SOC_DOUBLE_R_TLV("DSP2 Playback Volume", WM8996_DSP2_RX_LEFT_VOLUME,
		 WM8996_DSP2_RX_RIGHT_VOLUME, 1, 112, 0, digital_tlv),
SOC_SINGLE("DSP2 Playback Switch", WM8996_DSP2_RX_FILTERS_1, 9, 1, 1),

SOC_DOUBLE_R_TLV("DAC1 Volume", WM8996_DAC1_LEFT_VOLUME,
		 WM8996_DAC1_RIGHT_VOLUME, 1, 112, 0, digital_tlv),
SOC_DOUBLE_R("DAC1 Switch", WM8996_DAC1_LEFT_VOLUME,
	     WM8996_DAC1_RIGHT_VOLUME, 9, 1, 1),

SOC_DOUBLE_R_TLV("DAC2 Volume", WM8996_DAC2_LEFT_VOLUME,
		 WM8996_DAC2_RIGHT_VOLUME, 1, 112, 0, digital_tlv),
SOC_DOUBLE_R("DAC2 Switch", WM8996_DAC2_LEFT_VOLUME,
	     WM8996_DAC2_RIGHT_VOLUME, 9, 1, 1),

SOC_SINGLE("Speaker High Performance Switch", WM8996_OVERSAMPLING, 3, 1, 0),
SOC_SINGLE("DMIC High Performance Switch", WM8996_OVERSAMPLING, 2, 1, 0),
SOC_SINGLE("ADC High Performance Switch", WM8996_OVERSAMPLING, 1, 1, 0),
SOC_SINGLE("DAC High Performance Switch", WM8996_OVERSAMPLING, 0, 1, 0),

SOC_SINGLE("DAC Soft Mute Switch", WM8996_DAC_SOFTMUTE, 1, 1, 0),
SOC_SINGLE("DAC Slow Soft Mute Switch", WM8996_DAC_SOFTMUTE, 0, 1, 0),

SOC_SINGLE("DSP1 3D Stereo Switch", WM8996_DSP1_RX_FILTERS_2, 8, 1, 0),
SOC_SINGLE("DSP2 3D Stereo Switch", WM8996_DSP2_RX_FILTERS_2, 8, 1, 0),

SOC_SINGLE_TLV("DSP1 3D Stereo Volume", WM8996_DSP1_RX_FILTERS_2, 10, 15,
		0, threedstereo_tlv),
SOC_SINGLE_TLV("DSP2 3D Stereo Volume", WM8996_DSP2_RX_FILTERS_2, 10, 15,
		0, threedstereo_tlv),

SOC_DOUBLE_TLV("Digital Output 1 Volume", WM8996_DAC1_HPOUT1_VOLUME, 0, 4,
	       8, 0, out_digital_tlv),
SOC_DOUBLE_TLV("Digital Output 2 Volume", WM8996_DAC2_HPOUT2_VOLUME, 0, 4,
	       8, 0, out_digital_tlv),

SOC_DOUBLE_R_TLV("Output 1 Volume", WM8996_OUTPUT1_LEFT_VOLUME,
		 WM8996_OUTPUT1_RIGHT_VOLUME, 0, 12, 0, out_tlv),
SOC_DOUBLE_R("Output 1 ZC Switch",  WM8996_OUTPUT1_LEFT_VOLUME,
	     WM8996_OUTPUT1_RIGHT_VOLUME, 7, 1, 0),

SOC_DOUBLE_R_TLV("Output 2 Volume", WM8996_OUTPUT2_LEFT_VOLUME,
		 WM8996_OUTPUT2_RIGHT_VOLUME, 0, 12, 0, out_tlv),
SOC_DOUBLE_R("Output 2 ZC Switch",  WM8996_OUTPUT2_LEFT_VOLUME,
	     WM8996_OUTPUT2_RIGHT_VOLUME, 7, 1, 0),

SOC_DOUBLE_TLV("Speaker Volume", WM8996_PDM_SPEAKER_VOLUME, 0, 4, 8, 0,
	       spk_tlv),
SOC_DOUBLE_R("Speaker Switch", WM8996_LEFT_PDM_SPEAKER,
	     WM8996_RIGHT_PDM_SPEAKER, 3, 1, 1),
SOC_DOUBLE_R("Speaker ZC Switch", WM8996_LEFT_PDM_SPEAKER,
	     WM8996_RIGHT_PDM_SPEAKER, 2, 1, 0),

SOC_SINGLE("DSP1 EQ Switch", WM8996_DSP1_RX_EQ_GAINS_1, 0, 1, 0),
SOC_SINGLE("DSP2 EQ Switch", WM8996_DSP2_RX_EQ_GAINS_1, 0, 1, 0),

SOC_SINGLE("DSP1 DRC TXL Switch", WM8996_DSP1_DRC_1, 0, 1, 0),
SOC_SINGLE("DSP1 DRC TXR Switch", WM8996_DSP1_DRC_1, 1, 1, 0),
SOC_SINGLE("DSP1 DRC RX Switch", WM8996_DSP1_DRC_1, 2, 1, 0),
SND_SOC_BYTES_MASK("DSP1 DRC", WM8996_DSP1_DRC_1, 5,
		   WM8996_DSP1RX_DRC_ENA | WM8996_DSP1TXL_DRC_ENA |
		   WM8996_DSP1TXR_DRC_ENA),

SOC_SINGLE("DSP2 DRC TXL Switch", WM8996_DSP2_DRC_1, 0, 1, 0),
SOC_SINGLE("DSP2 DRC TXR Switch", WM8996_DSP2_DRC_1, 1, 1, 0),
SOC_SINGLE("DSP2 DRC RX Switch", WM8996_DSP2_DRC_1, 2, 1, 0),
SND_SOC_BYTES_MASK("DSP2 DRC", WM8996_DSP2_DRC_1, 5,
		   WM8996_DSP2RX_DRC_ENA | WM8996_DSP2TXL_DRC_ENA |
		   WM8996_DSP2TXR_DRC_ENA),
};

static const struct snd_kcontrol_new wm8996_eq_controls[] = {
SOC_SINGLE_TLV("DSP1 EQ B1 Volume", WM8996_DSP1_RX_EQ_GAINS_1, 11, 31, 0,
	       eq_tlv),
SOC_SINGLE_TLV("DSP1 EQ B2 Volume", WM8996_DSP1_RX_EQ_GAINS_1, 6, 31, 0,
	       eq_tlv),
SOC_SINGLE_TLV("DSP1 EQ B3 Volume", WM8996_DSP1_RX_EQ_GAINS_1, 1, 31, 0,
	       eq_tlv),
SOC_SINGLE_TLV("DSP1 EQ B4 Volume", WM8996_DSP1_RX_EQ_GAINS_2, 11, 31, 0,
	       eq_tlv),
SOC_SINGLE_TLV("DSP1 EQ B5 Volume", WM8996_DSP1_RX_EQ_GAINS_2, 6, 31, 0,
	       eq_tlv),

SOC_SINGLE_TLV("DSP2 EQ B1 Volume", WM8996_DSP2_RX_EQ_GAINS_1, 11, 31, 0,
	       eq_tlv),
SOC_SINGLE_TLV("DSP2 EQ B2 Volume", WM8996_DSP2_RX_EQ_GAINS_1, 6, 31, 0,
	       eq_tlv),
SOC_SINGLE_TLV("DSP2 EQ B3 Volume", WM8996_DSP2_RX_EQ_GAINS_1, 1, 31, 0,
	       eq_tlv),
SOC_SINGLE_TLV("DSP2 EQ B4 Volume", WM8996_DSP2_RX_EQ_GAINS_2, 11, 31, 0,
	       eq_tlv),
SOC_SINGLE_TLV("DSP2 EQ B5 Volume", WM8996_DSP2_RX_EQ_GAINS_2, 6, 31, 0,
	       eq_tlv),
};

static void wm8996_bg_enable(struct snd_soc_codec *codec)
{
	struct wm8996_priv *wm8996 = snd_soc_codec_get_drvdata(codec);

	wm8996->bg_ena++;
	if (wm8996->bg_ena == 1) {
		snd_soc_update_bits(codec, WM8996_POWER_MANAGEMENT_1,
				    WM8996_BG_ENA, WM8996_BG_ENA);
		msleep(2);
	}
}

static void wm8996_bg_disable(struct snd_soc_codec *codec)
{
	struct wm8996_priv *wm8996 = snd_soc_codec_get_drvdata(codec);

	wm8996->bg_ena--;
	if (!wm8996->bg_ena)
		snd_soc_update_bits(codec, WM8996_POWER_MANAGEMENT_1,
				    WM8996_BG_ENA, 0);
}

static int bg_event(struct snd_soc_dapm_widget *w,
		    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	int ret = 0;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		wm8996_bg_enable(codec);
		break;
	case SND_SOC_DAPM_POST_PMD:
		wm8996_bg_disable(codec);
		break;
	default:
		WARN(1, "Invalid event %d\n", event);
		ret = -EINVAL;
	}

	return ret;
}

static int cp_event(struct snd_soc_dapm_widget *w,
		    struct snd_kcontrol *kcontrol, int event)
{
	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		msleep(5);
		break;
	default:
		WARN(1, "Invalid event %d\n", event);
	}

	return 0;
}

static int rmv_short_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct wm8996_priv *wm8996 = snd_soc_codec_get_drvdata(codec);

	/* Record which outputs we enabled */
	switch (event) {
	case SND_SOC_DAPM_PRE_PMD:
		wm8996->hpout_pending &= ~w->shift;
		break;
	case SND_SOC_DAPM_PRE_PMU:
		wm8996->hpout_pending |= w->shift;
		break;
	default:
		WARN(1, "Invalid event %d\n", event);
		return -EINVAL;
	}

	return 0;
}

static void wait_for_dc_servo(struct snd_soc_codec *codec, u16 mask)
{
	struct i2c_client *i2c = to_i2c_client(codec->dev);
	struct wm8996_priv *wm8996 = snd_soc_codec_get_drvdata(codec);
	int ret;
	unsigned long timeout = 200;

	snd_soc_write(codec, WM8996_DC_SERVO_2, mask);

	/* Use the interrupt if possible */
	do {
		if (i2c->irq) {
			timeout = wait_for_completion_timeout(&wm8996->dcs_done,
							      msecs_to_jiffies(200));
			if (timeout == 0)
				dev_err(codec->dev, "DC servo timed out\n");

		} else {
			msleep(1);
			timeout--;
		}

		ret = snd_soc_read(codec, WM8996_DC_SERVO_2);
		dev_dbg(codec->dev, "DC servo state: %x\n", ret);
	} while (timeout && ret & mask);

	if (timeout == 0)
		dev_err(codec->dev, "DC servo timed out for %x\n", mask);
	else
		dev_dbg(codec->dev, "DC servo complete for %x\n", mask);
}

static void wm8996_seq_notifier(struct snd_soc_dapm_context *dapm,
				enum snd_soc_dapm_type event, int subseq)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(dapm);
	struct wm8996_priv *wm8996 = snd_soc_codec_get_drvdata(codec);
	u16 val, mask;

	/* Complete any pending DC servo starts */
	if (wm8996->dcs_pending) {
		dev_dbg(codec->dev, "Starting DC servo for %x\n",
			wm8996->dcs_pending);

		/* Trigger a startup sequence */
		wait_for_dc_servo(codec, wm8996->dcs_pending
				         << WM8996_DCS_TRIG_STARTUP_0_SHIFT);

		wm8996->dcs_pending = 0;
	}

	if (wm8996->hpout_pending != wm8996->hpout_ena) {
		dev_dbg(codec->dev, "Applying RMV_SHORTs %x->%x\n",
			wm8996->hpout_ena, wm8996->hpout_pending);

		val = 0;
		mask = 0;
		if (wm8996->hpout_pending & HPOUT1L) {
			val |= WM8996_HPOUT1L_RMV_SHORT | WM8996_HPOUT1L_OUTP;
			mask |= WM8996_HPOUT1L_RMV_SHORT | WM8996_HPOUT1L_OUTP;
		} else {
			mask |= WM8996_HPOUT1L_RMV_SHORT |
				WM8996_HPOUT1L_OUTP |
				WM8996_HPOUT1L_DLY;
		}

		if (wm8996->hpout_pending & HPOUT1R) {
			val |= WM8996_HPOUT1R_RMV_SHORT | WM8996_HPOUT1R_OUTP;
			mask |= WM8996_HPOUT1R_RMV_SHORT | WM8996_HPOUT1R_OUTP;
		} else {
			mask |= WM8996_HPOUT1R_RMV_SHORT |
				WM8996_HPOUT1R_OUTP |
				WM8996_HPOUT1R_DLY;
		}

		snd_soc_update_bits(codec, WM8996_ANALOGUE_HP_1, mask, val);

		val = 0;
		mask = 0;
		if (wm8996->hpout_pending & HPOUT2L) {
			val |= WM8996_HPOUT2L_RMV_SHORT | WM8996_HPOUT2L_OUTP;
			mask |= WM8996_HPOUT2L_RMV_SHORT | WM8996_HPOUT2L_OUTP;
		} else {
			mask |= WM8996_HPOUT2L_RMV_SHORT |
				WM8996_HPOUT2L_OUTP |
				WM8996_HPOUT2L_DLY;
		}

		if (wm8996->hpout_pending & HPOUT2R) {
			val |= WM8996_HPOUT2R_RMV_SHORT | WM8996_HPOUT2R_OUTP;
			mask |= WM8996_HPOUT2R_RMV_SHORT | WM8996_HPOUT2R_OUTP;
		} else {
			mask |= WM8996_HPOUT2R_RMV_SHORT |
				WM8996_HPOUT2R_OUTP |
				WM8996_HPOUT2R_DLY;
		}

		snd_soc_update_bits(codec, WM8996_ANALOGUE_HP_2, mask, val);

		wm8996->hpout_ena = wm8996->hpout_pending;
	}
}

static int dcs_start(struct snd_soc_dapm_widget *w,
		     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct wm8996_priv *wm8996 = snd_soc_codec_get_drvdata(codec);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		wm8996->dcs_pending |= 1 << w->shift;
		break;
	default:
		WARN(1, "Invalid event %d\n", event);
		return -EINVAL;
	}

	return 0;
}

static const char *sidetone_text[] = {
	"IN1", "IN2",
};

static SOC_ENUM_SINGLE_DECL(left_sidetone_enum,
			    WM8996_SIDETONE, 0, sidetone_text);

static const struct snd_kcontrol_new left_sidetone =
	SOC_DAPM_ENUM("Left Sidetone", left_sidetone_enum);

static SOC_ENUM_SINGLE_DECL(right_sidetone_enum,
			    WM8996_SIDETONE, 1, sidetone_text);

static const struct snd_kcontrol_new right_sidetone =
	SOC_DAPM_ENUM("Right Sidetone", right_sidetone_enum);

static const char *spk_text[] = {
	"DAC1L", "DAC1R", "DAC2L", "DAC2R"
};

static SOC_ENUM_SINGLE_DECL(spkl_enum,
			    WM8996_LEFT_PDM_SPEAKER, 0, spk_text);

static const struct snd_kcontrol_new spkl_mux =
	SOC_DAPM_ENUM("SPKL", spkl_enum);

static SOC_ENUM_SINGLE_DECL(spkr_enum,
			    WM8996_RIGHT_PDM_SPEAKER, 0, spk_text);

static const struct snd_kcontrol_new spkr_mux =
	SOC_DAPM_ENUM("SPKR", spkr_enum);

static const char *dsp1rx_text[] = {
	"AIF1", "AIF2"
};

static SOC_ENUM_SINGLE_DECL(dsp1rx_enum,
			    WM8996_POWER_MANAGEMENT_8, 0, dsp1rx_text);

static const struct snd_kcontrol_new dsp1rx =
	SOC_DAPM_ENUM("DSP1RX", dsp1rx_enum);

static const char *dsp2rx_text[] = {
	 "AIF2", "AIF1"
};

static SOC_ENUM_SINGLE_DECL(dsp2rx_enum,
			    WM8996_POWER_MANAGEMENT_8, 4, dsp2rx_text);

static const struct snd_kcontrol_new dsp2rx =
	SOC_DAPM_ENUM("DSP2RX", dsp2rx_enum);

static const char *aif2tx_text[] = {
	"DSP2", "DSP1", "AIF1"
};

static SOC_ENUM_SINGLE_DECL(aif2tx_enum,
			    WM8996_POWER_MANAGEMENT_8, 6, aif2tx_text);

static const struct snd_kcontrol_new aif2tx =
	SOC_DAPM_ENUM("AIF2TX", aif2tx_enum);

static const char *inmux_text[] = {
	"ADC", "DMIC1", "DMIC2"
};

static SOC_ENUM_SINGLE_DECL(in1_enum,
			    WM8996_POWER_MANAGEMENT_7, 0, inmux_text);

static const struct snd_kcontrol_new in1_mux =
	SOC_DAPM_ENUM("IN1 Mux", in1_enum);

static SOC_ENUM_SINGLE_DECL(in2_enum,
			    WM8996_POWER_MANAGEMENT_7, 4, inmux_text);

static const struct snd_kcontrol_new in2_mux =
	SOC_DAPM_ENUM("IN2 Mux", in2_enum);

static const struct snd_kcontrol_new dac2r_mix[] = {
SOC_DAPM_SINGLE("Right Sidetone Switch", WM8996_DAC2_RIGHT_MIXER_ROUTING,
		5, 1, 0),
SOC_DAPM_SINGLE("Left Sidetone Switch", WM8996_DAC2_RIGHT_MIXER_ROUTING,
		4, 1, 0),
SOC_DAPM_SINGLE("DSP2 Switch", WM8996_DAC2_RIGHT_MIXER_ROUTING, 1, 1, 0),
SOC_DAPM_SINGLE("DSP1 Switch", WM8996_DAC2_RIGHT_MIXER_ROUTING, 0, 1, 0),
};

static const struct snd_kcontrol_new dac2l_mix[] = {
SOC_DAPM_SINGLE("Right Sidetone Switch", WM8996_DAC2_LEFT_MIXER_ROUTING,
		5, 1, 0),
SOC_DAPM_SINGLE("Left Sidetone Switch", WM8996_DAC2_LEFT_MIXER_ROUTING,
		4, 1, 0),
SOC_DAPM_SINGLE("DSP2 Switch", WM8996_DAC2_LEFT_MIXER_ROUTING, 1, 1, 0),
SOC_DAPM_SINGLE("DSP1 Switch", WM8996_DAC2_LEFT_MIXER_ROUTING, 0, 1, 0),
};

static const struct snd_kcontrol_new dac1r_mix[] = {
SOC_DAPM_SINGLE("Right Sidetone Switch", WM8996_DAC1_RIGHT_MIXER_ROUTING,
		5, 1, 0),
SOC_DAPM_SINGLE("Left Sidetone Switch", WM8996_DAC1_RIGHT_MIXER_ROUTING,
		4, 1, 0),
SOC_DAPM_SINGLE("DSP2 Switch", WM8996_DAC1_RIGHT_MIXER_ROUTING, 1, 1, 0),
SOC_DAPM_SINGLE("DSP1 Switch", WM8996_DAC1_RIGHT_MIXER_ROUTING, 0, 1, 0),
};

static const struct snd_kcontrol_new dac1l_mix[] = {
SOC_DAPM_SINGLE("Right Sidetone Switch", WM8996_DAC1_LEFT_MIXER_ROUTING,
		5, 1, 0),
SOC_DAPM_SINGLE("Left Sidetone Switch", WM8996_DAC1_LEFT_MIXER_ROUTING,
		4, 1, 0),
SOC_DAPM_SINGLE("DSP2 Switch", WM8996_DAC1_LEFT_MIXER_ROUTING, 1, 1, 0),
SOC_DAPM_SINGLE("DSP1 Switch", WM8996_DAC1_LEFT_MIXER_ROUTING, 0, 1, 0),
};

static const struct snd_kcontrol_new dsp1txl[] = {
SOC_DAPM_SINGLE("IN1 Switch", WM8996_DSP1_TX_LEFT_MIXER_ROUTING,
		1, 1, 0),
SOC_DAPM_SINGLE("DAC Switch", WM8996_DSP1_TX_LEFT_MIXER_ROUTING,
		0, 1, 0),
};

static const struct snd_kcontrol_new dsp1txr[] = {
SOC_DAPM_SINGLE("IN1 Switch", WM8996_DSP1_TX_RIGHT_MIXER_ROUTING,
		1, 1, 0),
SOC_DAPM_SINGLE("DAC Switch", WM8996_DSP1_TX_RIGHT_MIXER_ROUTING,
		0, 1, 0),
};

static const struct snd_kcontrol_new dsp2txl[] = {
SOC_DAPM_SINGLE("IN1 Switch", WM8996_DSP2_TX_LEFT_MIXER_ROUTING,
		1, 1, 0),
SOC_DAPM_SINGLE("DAC Switch", WM8996_DSP2_TX_LEFT_MIXER_ROUTING,
		0, 1, 0),
};

static const struct snd_kcontrol_new dsp2txr[] = {
SOC_DAPM_SINGLE("IN1 Switch", WM8996_DSP2_TX_RIGHT_MIXER_ROUTING,
		1, 1, 0),
SOC_DAPM_SINGLE("DAC Switch", WM8996_DSP2_TX_RIGHT_MIXER_ROUTING,
		0, 1, 0),
};


static const struct snd_soc_dapm_widget wm8996_dapm_widgets[] = {
SND_SOC_DAPM_INPUT("IN1LN"),
SND_SOC_DAPM_INPUT("IN1LP"),
SND_SOC_DAPM_INPUT("IN1RN"),
SND_SOC_DAPM_INPUT("IN1RP"),

SND_SOC_DAPM_INPUT("IN2LN"),
SND_SOC_DAPM_INPUT("IN2LP"),
SND_SOC_DAPM_INPUT("IN2RN"),
SND_SOC_DAPM_INPUT("IN2RP"),

SND_SOC_DAPM_INPUT("DMIC1DAT"),
SND_SOC_DAPM_INPUT("DMIC2DAT"),

SND_SOC_DAPM_REGULATOR_SUPPLY("CPVDD", 20, 0),
SND_SOC_DAPM_SUPPLY_S("SYSCLK", 1, WM8996_AIF_CLOCKING_1, 0, 0, NULL, 0),
SND_SOC_DAPM_SUPPLY_S("SYSDSPCLK", 2, WM8996_CLOCKING_1, 1, 0, NULL, 0),
SND_SOC_DAPM_SUPPLY_S("AIFCLK", 2, WM8996_CLOCKING_1, 2, 0, NULL, 0),
SND_SOC_DAPM_SUPPLY_S("Charge Pump", 2, WM8996_CHARGE_PUMP_1, 15, 0, cp_event,
		      SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_SUPPLY("Bandgap", SND_SOC_NOPM, 0, 0, bg_event,
		    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
SND_SOC_DAPM_SUPPLY("LDO2", WM8996_POWER_MANAGEMENT_2, 1, 0, NULL, 0),
SND_SOC_DAPM_SUPPLY("MICB1 Audio", WM8996_MICBIAS_1, 4, 1, NULL, 0),
SND_SOC_DAPM_SUPPLY("MICB2 Audio", WM8996_MICBIAS_2, 4, 1, NULL, 0),
SND_SOC_DAPM_MICBIAS("MICB2", WM8996_POWER_MANAGEMENT_1, 9, 0),
SND_SOC_DAPM_MICBIAS("MICB1", WM8996_POWER_MANAGEMENT_1, 8, 0),

SND_SOC_DAPM_PGA("IN1L PGA", WM8996_POWER_MANAGEMENT_2, 5, 0, NULL, 0),
SND_SOC_DAPM_PGA("IN1R PGA", WM8996_POWER_MANAGEMENT_2, 4, 0, NULL, 0),

SND_SOC_DAPM_MUX("IN1L Mux", WM8996_POWER_MANAGEMENT_7, 2, 0, &in1_mux),
SND_SOC_DAPM_MUX("IN1R Mux", WM8996_POWER_MANAGEMENT_7, 3, 0, &in1_mux),
SND_SOC_DAPM_MUX("IN2L Mux", WM8996_POWER_MANAGEMENT_7, 6, 0, &in2_mux),
SND_SOC_DAPM_MUX("IN2R Mux", WM8996_POWER_MANAGEMENT_7, 7, 0, &in2_mux),

SND_SOC_DAPM_SUPPLY("DMIC2", WM8996_POWER_MANAGEMENT_7, 9, 0, NULL, 0),
SND_SOC_DAPM_SUPPLY("DMIC1", WM8996_POWER_MANAGEMENT_7, 8, 0, NULL, 0),

SND_SOC_DAPM_ADC("DMIC2L", NULL, WM8996_POWER_MANAGEMENT_3, 5, 0),
SND_SOC_DAPM_ADC("DMIC2R", NULL, WM8996_POWER_MANAGEMENT_3, 4, 0),
SND_SOC_DAPM_ADC("DMIC1L", NULL, WM8996_POWER_MANAGEMENT_3, 3, 0),
SND_SOC_DAPM_ADC("DMIC1R", NULL, WM8996_POWER_MANAGEMENT_3, 2, 0),

SND_SOC_DAPM_ADC("ADCL", NULL, WM8996_POWER_MANAGEMENT_3, 1, 0),
SND_SOC_DAPM_ADC("ADCR", NULL, WM8996_POWER_MANAGEMENT_3, 0, 0),

SND_SOC_DAPM_MUX("Left Sidetone", SND_SOC_NOPM, 0, 0, &left_sidetone),
SND_SOC_DAPM_MUX("Right Sidetone", SND_SOC_NOPM, 0, 0, &right_sidetone),

SND_SOC_DAPM_AIF_IN("DSP2RXL", NULL, 0, WM8996_POWER_MANAGEMENT_3, 11, 0),
SND_SOC_DAPM_AIF_IN("DSP2RXR", NULL, 1, WM8996_POWER_MANAGEMENT_3, 10, 0),
SND_SOC_DAPM_AIF_IN("DSP1RXL", NULL, 0, WM8996_POWER_MANAGEMENT_3, 9, 0),
SND_SOC_DAPM_AIF_IN("DSP1RXR", NULL, 1, WM8996_POWER_MANAGEMENT_3, 8, 0),

SND_SOC_DAPM_MIXER("DSP2TXL", WM8996_POWER_MANAGEMENT_5, 11, 0,
		   dsp2txl, ARRAY_SIZE(dsp2txl)),
SND_SOC_DAPM_MIXER("DSP2TXR", WM8996_POWER_MANAGEMENT_5, 10, 0,
		   dsp2txr, ARRAY_SIZE(dsp2txr)),
SND_SOC_DAPM_MIXER("DSP1TXL", WM8996_POWER_MANAGEMENT_5, 9, 0,
		   dsp1txl, ARRAY_SIZE(dsp1txl)),
SND_SOC_DAPM_MIXER("DSP1TXR", WM8996_POWER_MANAGEMENT_5, 8, 0,
		   dsp1txr, ARRAY_SIZE(dsp1txr)),

SND_SOC_DAPM_MIXER("DAC2L Mixer", SND_SOC_NOPM, 0, 0,
		   dac2l_mix, ARRAY_SIZE(dac2l_mix)),
SND_SOC_DAPM_MIXER("DAC2R Mixer", SND_SOC_NOPM, 0, 0,
		   dac2r_mix, ARRAY_SIZE(dac2r_mix)),
SND_SOC_DAPM_MIXER("DAC1L Mixer", SND_SOC_NOPM, 0, 0,
		   dac1l_mix, ARRAY_SIZE(dac1l_mix)),
SND_SOC_DAPM_MIXER("DAC1R Mixer", SND_SOC_NOPM, 0, 0,
		   dac1r_mix, ARRAY_SIZE(dac1r_mix)),

SND_SOC_DAPM_DAC("DAC2L", NULL, WM8996_POWER_MANAGEMENT_5, 3, 0),
SND_SOC_DAPM_DAC("DAC2R", NULL, WM8996_POWER_MANAGEMENT_5, 2, 0),
SND_SOC_DAPM_DAC("DAC1L", NULL, WM8996_POWER_MANAGEMENT_5, 1, 0),
SND_SOC_DAPM_DAC("DAC1R", NULL, WM8996_POWER_MANAGEMENT_5, 0, 0),

SND_SOC_DAPM_AIF_IN("AIF2RX1", NULL, 0, WM8996_POWER_MANAGEMENT_4, 9, 0),
SND_SOC_DAPM_AIF_IN("AIF2RX0", NULL, 1, WM8996_POWER_MANAGEMENT_4, 8, 0),

SND_SOC_DAPM_AIF_OUT("AIF2TX1", NULL, 0, WM8996_POWER_MANAGEMENT_6, 9, 0),
SND_SOC_DAPM_AIF_OUT("AIF2TX0", NULL, 1, WM8996_POWER_MANAGEMENT_6, 8, 0),

SND_SOC_DAPM_AIF_IN("AIF1RX5", NULL, 5, WM8996_POWER_MANAGEMENT_4, 5, 0),
SND_SOC_DAPM_AIF_IN("AIF1RX4", NULL, 4, WM8996_POWER_MANAGEMENT_4, 4, 0),
SND_SOC_DAPM_AIF_IN("AIF1RX3", NULL, 3, WM8996_POWER_MANAGEMENT_4, 3, 0),
SND_SOC_DAPM_AIF_IN("AIF1RX2", NULL, 2, WM8996_POWER_MANAGEMENT_4, 2, 0),
SND_SOC_DAPM_AIF_IN("AIF1RX1", NULL, 1, WM8996_POWER_MANAGEMENT_4, 1, 0),
SND_SOC_DAPM_AIF_IN("AIF1RX0", NULL, 0, WM8996_POWER_MANAGEMENT_4, 0, 0),

SND_SOC_DAPM_AIF_OUT("AIF1TX5", NULL, 5, WM8996_POWER_MANAGEMENT_6, 5, 0),
SND_SOC_DAPM_AIF_OUT("AIF1TX4", NULL, 4, WM8996_POWER_MANAGEMENT_6, 4, 0),
SND_SOC_DAPM_AIF_OUT("AIF1TX3", NULL, 3, WM8996_POWER_MANAGEMENT_6, 3, 0),
SND_SOC_DAPM_AIF_OUT("AIF1TX2", NULL, 2, WM8996_POWER_MANAGEMENT_6, 2, 0),
SND_SOC_DAPM_AIF_OUT("AIF1TX1", NULL, 1, WM8996_POWER_MANAGEMENT_6, 1, 0),
SND_SOC_DAPM_AIF_OUT("AIF1TX0", NULL, 0, WM8996_POWER_MANAGEMENT_6, 0, 0),

/* We route as stereo pairs so define some dummy widgets to squash
 * things down for now.  RXA = 0,1, RXB = 2,3 and so on */
SND_SOC_DAPM_PGA("AIF1RXA", SND_SOC_NOPM, 0, 0, NULL, 0),
SND_SOC_DAPM_PGA("AIF1RXB", SND_SOC_NOPM, 0, 0, NULL, 0),
SND_SOC_DAPM_PGA("AIF1RXC", SND_SOC_NOPM, 0, 0, NULL, 0),
SND_SOC_DAPM_PGA("AIF2RX", SND_SOC_NOPM, 0, 0, NULL, 0),
SND_SOC_DAPM_PGA("DSP2TX", SND_SOC_NOPM, 0, 0, NULL, 0),

SND_SOC_DAPM_MUX("DSP1RX", SND_SOC_NOPM, 0, 0, &dsp1rx),
SND_SOC_DAPM_MUX("DSP2RX", SND_SOC_NOPM, 0, 0, &dsp2rx),
SND_SOC_DAPM_MUX("AIF2TX", SND_SOC_NOPM, 0, 0, &aif2tx),

SND_SOC_DAPM_MUX("SPKL", SND_SOC_NOPM, 0, 0, &spkl_mux),
SND_SOC_DAPM_MUX("SPKR", SND_SOC_NOPM, 0, 0, &spkr_mux),
SND_SOC_DAPM_PGA("SPKL PGA", WM8996_LEFT_PDM_SPEAKER, 4, 0, NULL, 0),
SND_SOC_DAPM_PGA("SPKR PGA", WM8996_RIGHT_PDM_SPEAKER, 4, 0, NULL, 0),

SND_SOC_DAPM_PGA_S("HPOUT2L PGA", 0, WM8996_POWER_MANAGEMENT_1, 7, 0, NULL, 0),
SND_SOC_DAPM_PGA_S("HPOUT2L_DLY", 1, WM8996_ANALOGUE_HP_2, 5, 0, NULL, 0),
SND_SOC_DAPM_PGA_S("HPOUT2L_DCS", 2, WM8996_DC_SERVO_1, 2, 0, dcs_start,
		   SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_S("HPOUT2L_RMV_SHORT", 3, SND_SOC_NOPM, HPOUT2L, 0,
		   rmv_short_event,
		   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),

SND_SOC_DAPM_PGA_S("HPOUT2R PGA", 0, WM8996_POWER_MANAGEMENT_1, 6, 0,NULL, 0),
SND_SOC_DAPM_PGA_S("HPOUT2R_DLY", 1, WM8996_ANALOGUE_HP_2, 1, 0, NULL, 0),
SND_SOC_DAPM_PGA_S("HPOUT2R_DCS", 2, WM8996_DC_SERVO_1, 3, 0, dcs_start,
		   SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_S("HPOUT2R_RMV_SHORT", 3, SND_SOC_NOPM, HPOUT2R, 0,
		   rmv_short_event,
		   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),

SND_SOC_DAPM_PGA_S("HPOUT1L PGA", 0, WM8996_POWER_MANAGEMENT_1, 5, 0, NULL, 0),
SND_SOC_DAPM_PGA_S("HPOUT1L_DLY", 1, WM8996_ANALOGUE_HP_1, 5, 0, NULL, 0),
SND_SOC_DAPM_PGA_S("HPOUT1L_DCS", 2, WM8996_DC_SERVO_1, 0, 0, dcs_start,
		   SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_S("HPOUT1L_RMV_SHORT", 3, SND_SOC_NOPM, HPOUT1L, 0,
		   rmv_short_event,
		   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),

SND_SOC_DAPM_PGA_S("HPOUT1R PGA", 0, WM8996_POWER_MANAGEMENT_1, 4, 0, NULL, 0),
SND_SOC_DAPM_PGA_S("HPOUT1R_DLY", 1, WM8996_ANALOGUE_HP_1, 1, 0, NULL, 0),
SND_SOC_DAPM_PGA_S("HPOUT1R_DCS", 2, WM8996_DC_SERVO_1, 1, 0, dcs_start,
		   SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_S("HPOUT1R_RMV_SHORT", 3, SND_SOC_NOPM, HPOUT1R, 0,
		   rmv_short_event,
		   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),

SND_SOC_DAPM_OUTPUT("HPOUT1L"),
SND_SOC_DAPM_OUTPUT("HPOUT1R"),
SND_SOC_DAPM_OUTPUT("HPOUT2L"),
SND_SOC_DAPM_OUTPUT("HPOUT2R"),
SND_SOC_DAPM_OUTPUT("SPKDAT"),
};

static const struct snd_soc_dapm_route wm8996_dapm_routes[] = {
	{ "AIFCLK", NULL, "SYSCLK" },
	{ "SYSDSPCLK", NULL, "SYSCLK" },
	{ "Charge Pump", NULL, "SYSCLK" },
	{ "Charge Pump", NULL, "CPVDD" },

	{ "MICB1", NULL, "LDO2" },
	{ "MICB1", NULL, "MICB1 Audio" },
	{ "MICB1", NULL, "Bandgap" },
	{ "MICB2", NULL, "LDO2" },
	{ "MICB2", NULL, "MICB2 Audio" },
	{ "MICB2", NULL, "Bandgap" },

	{ "AIF1RX0", NULL, "AIF1 Playback" },
	{ "AIF1RX1", NULL, "AIF1 Playback" },
	{ "AIF1RX2", NULL, "AIF1 Playback" },
	{ "AIF1RX3", NULL, "AIF1 Playback" },
	{ "AIF1RX4", NULL, "AIF1 Playback" },
	{ "AIF1RX5", NULL, "AIF1 Playback" },

	{ "AIF2RX0", NULL, "AIF2 Playback" },
	{ "AIF2RX1", NULL, "AIF2 Playback" },

	{ "AIF1 Capture", NULL, "AIF1TX0" },
	{ "AIF1 Capture", NULL, "AIF1TX1" },
	{ "AIF1 Capture", NULL, "AIF1TX2" },
	{ "AIF1 Capture", NULL, "AIF1TX3" },
	{ "AIF1 Capture", NULL, "AIF1TX4" },
	{ "AIF1 Capture", NULL, "AIF1TX5" },

	{ "AIF2 Capture", NULL, "AIF2TX0" },
	{ "AIF2 Capture", NULL, "AIF2TX1" },

	{ "IN1L PGA", NULL, "IN2LN" },
	{ "IN1L PGA", NULL, "IN2LP" },
	{ "IN1L PGA", NULL, "IN1LN" },
	{ "IN1L PGA", NULL, "IN1LP" },
	{ "IN1L PGA", NULL, "Bandgap" },

	{ "IN1R PGA", NULL, "IN2RN" },
	{ "IN1R PGA", NULL, "IN2RP" },
	{ "IN1R PGA", NULL, "IN1RN" },
	{ "IN1R PGA", NULL, "IN1RP" },
	{ "IN1R PGA", NULL, "Bandgap" },

	{ "ADCL", NULL, "IN1L PGA" },

	{ "ADCR", NULL, "IN1R PGA" },

	{ "DMIC1L", NULL, "DMIC1DAT" },
	{ "DMIC1R", NULL, "DMIC1DAT" },
	{ "DMIC2L", NULL, "DMIC2DAT" },
	{ "DMIC2R", NULL, "DMIC2DAT" },

	{ "DMIC2L", NULL, "DMIC2" },
	{ "DMIC2R", NULL, "DMIC2" },
	{ "DMIC1L", NULL, "DMIC1" },
	{ "DMIC1R", NULL, "DMIC1" },

	{ "IN1L Mux", "ADC", "ADCL" },
	{ "IN1L Mux", "DMIC1", "DMIC1L" },
	{ "IN1L Mux", "DMIC2", "DMIC2L" },

	{ "IN1R Mux", "ADC", "ADCR" },
	{ "IN1R Mux", "DMIC1", "DMIC1R" },
	{ "IN1R Mux", "DMIC2", "DMIC2R" },

	{ "IN2L Mux", "ADC", "ADCL" },
	{ "IN2L Mux", "DMIC1", "DMIC1L" },
	{ "IN2L Mux", "DMIC2", "DMIC2L" },

	{ "IN2R Mux", "ADC", "ADCR" },
	{ "IN2R Mux", "DMIC1", "DMIC1R" },
	{ "IN2R Mux", "DMIC2", "DMIC2R" },

	{ "Left Sidetone", "IN1", "IN1L Mux" },
	{ "Left Sidetone", "IN2", "IN2L Mux" },

	{ "Right Sidetone", "IN1", "IN1R Mux" },
	{ "Right Sidetone", "IN2", "IN2R Mux" },

	{ "DSP1TXL", "IN1 Switch", "IN1L Mux" },
	{ "DSP1TXR", "IN1 Switch", "IN1R Mux" },

	{ "DSP2TXL", "IN1 Switch", "IN2L Mux" },
	{ "DSP2TXR", "IN1 Switch", "IN2R Mux" },

	{ "AIF1TX0", NULL, "DSP1TXL" },
	{ "AIF1TX1", NULL, "DSP1TXR" },
	{ "AIF1TX2", NULL, "DSP2TXL" },
	{ "AIF1TX3", NULL, "DSP2TXR" },
	{ "AIF1TX4", NULL, "AIF2RX0" },
	{ "AIF1TX5", NULL, "AIF2RX1" },

	{ "AIF1RX0", NULL, "AIFCLK" },
	{ "AIF1RX1", NULL, "AIFCLK" },
	{ "AIF1RX2", NULL, "AIFCLK" },
	{ "AIF1RX3", NULL, "AIFCLK" },
	{ "AIF1RX4", NULL, "AIFCLK" },
	{ "AIF1RX5", NULL, "AIFCLK" },

	{ "AIF2RX0", NULL, "AIFCLK" },
	{ "AIF2RX1", NULL, "AIFCLK" },

	{ "AIF1TX0", NULL, "AIFCLK" },
	{ "AIF1TX1", NULL, "AIFCLK" },
	{ "AIF1TX2", NULL, "AIFCLK" },
	{ "AIF1TX3", NULL, "AIFCLK" },
	{ "AIF1TX4", NULL, "AIFCLK" },
	{ "AIF1TX5", NULL, "AIFCLK" },

	{ "AIF2TX0", NULL, "AIFCLK" },
	{ "AIF2TX1", NULL, "AIFCLK" },

	{ "DSP1RXL", NULL, "SYSDSPCLK" },
	{ "DSP1RXR", NULL, "SYSDSPCLK" },
	{ "DSP2RXL", NULL, "SYSDSPCLK" },
	{ "DSP2RXR", NULL, "SYSDSPCLK" },
	{ "DSP1TXL", NULL, "SYSDSPCLK" },
	{ "DSP1TXR", NULL, "SYSDSPCLK" },
	{ "DSP2TXL", NULL, "SYSDSPCLK" },
	{ "DSP2TXR", NULL, "SYSDSPCLK" },

	{ "AIF1RXA", NULL, "AIF1RX0" },
	{ "AIF1RXA", NULL, "AIF1RX1" },
	{ "AIF1RXB", NULL, "AIF1RX2" },
	{ "AIF1RXB", NULL, "AIF1RX3" },
	{ "AIF1RXC", NULL, "AIF1RX4" },
	{ "AIF1RXC", NULL, "AIF1RX5" },

	{ "AIF2RX", NULL, "AIF2RX0" },
	{ "AIF2RX", NULL, "AIF2RX1" },

	{ "AIF2TX", "DSP2", "DSP2TX" },
	{ "AIF2TX", "DSP1", "DSP1RX" },
	{ "AIF2TX", "AIF1", "AIF1RXC" },

	{ "DSP1RXL", NULL, "DSP1RX" },
	{ "DSP1RXR", NULL, "DSP1RX" },
	{ "DSP2RXL", NULL, "DSP2RX" },
	{ "DSP2RXR", NULL, "DSP2RX" },

	{ "DSP2TX", NULL, "DSP2TXL" },
	{ "DSP2TX", NULL, "DSP2TXR" },

	{ "DSP1RX", "AIF1", "AIF1RXA" },
	{ "DSP1RX", "AIF2", "AIF2RX" },

	{ "DSP2RX", "AIF1", "AIF1RXB" },
	{ "DSP2RX", "AIF2", "AIF2RX" },

	{ "DAC2L Mixer", "DSP2 Switch", "DSP2RXL" },
	{ "DAC2L Mixer", "DSP1 Switch", "DSP1RXL" },
	{ "DAC2L Mixer", "Right Sidetone Switch", "Right Sidetone" },
	{ "DAC2L Mixer", "Left Sidetone Switch", "Left Sidetone" },

	{ "DAC2R Mixer", "DSP2 Switch", "DSP2RXR" },
	{ "DAC2R Mixer", "DSP1 Switch", "DSP1RXR" },
	{ "DAC2R Mixer", "Right Sidetone Switch", "Right Sidetone" },
	{ "DAC2R Mixer", "Left Sidetone Switch", "Left Sidetone" },

	{ "DAC1L Mixer", "DSP2 Switch", "DSP2RXL" },
	{ "DAC1L Mixer", "DSP1 Switch", "DSP1RXL" },
	{ "DAC1L Mixer", "Right Sidetone Switch", "Right Sidetone" },
	{ "DAC1L Mixer", "Left Sidetone Switch", "Left Sidetone" },

	{ "DAC1R Mixer", "DSP2 Switch", "DSP2RXR" },
	{ "DAC1R Mixer", "DSP1 Switch", "DSP1RXR" },
	{ "DAC1R Mixer", "Right Sidetone Switch", "Right Sidetone" },
	{ "DAC1R Mixer", "Left Sidetone Switch", "Left Sidetone" },

	{ "DAC1L", NULL, "DAC1L Mixer" },
	{ "DAC1R", NULL, "DAC1R Mixer" },
	{ "DAC2L", NULL, "DAC2L Mixer" },
	{ "DAC2R", NULL, "DAC2R Mixer" },

	{ "HPOUT2L PGA", NULL, "Charge Pump" },
	{ "HPOUT2L PGA", NULL, "Bandgap" },
	{ "HPOUT2L PGA", NULL, "DAC2L" },
	{ "HPOUT2L_DLY", NULL, "HPOUT2L PGA" },
	{ "HPOUT2L_DCS", NULL, "HPOUT2L_DLY" },
	{ "HPOUT2L_RMV_SHORT", NULL, "HPOUT2L_DCS" },

	{ "HPOUT2R PGA", NULL, "Charge Pump" },
	{ "HPOUT2R PGA", NULL, "Bandgap" },
	{ "HPOUT2R PGA", NULL, "DAC2R" },
	{ "HPOUT2R_DLY", NULL, "HPOUT2R PGA" },
	{ "HPOUT2R_DCS", NULL, "HPOUT2R_DLY" },
	{ "HPOUT2R_RMV_SHORT", NULL, "HPOUT2R_DCS" },

	{ "HPOUT1L PGA", NULL, "Charge Pump" },
	{ "HPOUT1L PGA", NULL, "Bandgap" },
	{ "HPOUT1L PGA", NULL, "DAC1L" },
	{ "HPOUT1L_DLY", NULL, "HPOUT1L PGA" },
	{ "HPOUT1L_DCS", NULL, "HPOUT1L_DLY" },
	{ "HPOUT1L_RMV_SHORT", NULL, "HPOUT1L_DCS" },

	{ "HPOUT1R PGA", NULL, "Charge Pump" },
	{ "HPOUT1R PGA", NULL, "Bandgap" },
	{ "HPOUT1R PGA", NULL, "DAC1R" },
	{ "HPOUT1R_DLY", NULL, "HPOUT1R PGA" },
	{ "HPOUT1R_DCS", NULL, "HPOUT1R_DLY" },
	{ "HPOUT1R_RMV_SHORT", NULL, "HPOUT1R_DCS" },

	{ "HPOUT2L", NULL, "HPOUT2L_RMV_SHORT" },
	{ "HPOUT2R", NULL, "HPOUT2R_RMV_SHORT" },
	{ "HPOUT1L", NULL, "HPOUT1L_RMV_SHORT" },
	{ "HPOUT1R", NULL, "HPOUT1R_RMV_SHORT" },

	{ "SPKL", "DAC1L", "DAC1L" },
	{ "SPKL", "DAC1R", "DAC1R" },
	{ "SPKL", "DAC2L", "DAC2L" },
	{ "SPKL", "DAC2R", "DAC2R" },

	{ "SPKR", "DAC1L", "DAC1L" },
	{ "SPKR", "DAC1R", "DAC1R" },
	{ "SPKR", "DAC2L", "DAC2L" },
	{ "SPKR", "DAC2R", "DAC2R" },

	{ "SPKL PGA", NULL, "SPKL" },
	{ "SPKR PGA", NULL, "SPKR" },

	{ "SPKDAT", NULL, "SPKL PGA" },
	{ "SPKDAT", NULL, "SPKR PGA" },
};

static bool wm8996_readable_register(struct device *dev, unsigned int reg)
{
	/* Due to the sparseness of the register map the compiler
	 * output from an explicit switch statement ends up being much
	 * more efficient than a table.
	 */
	switch (reg) {
	case WM8996_SOFTWARE_RESET:
	case WM8996_POWER_MANAGEMENT_1:
	case WM8996_POWER_MANAGEMENT_2:
	case WM8996_POWER_MANAGEMENT_3:
	case WM8996_POWER_MANAGEMENT_4:
	case WM8996_POWER_MANAGEMENT_5:
	case WM8996_POWER_MANAGEMENT_6:
	case WM8996_POWER_MANAGEMENT_7:
	case WM8996_POWER_MANAGEMENT_8:
	case WM8996_LEFT_LINE_INPUT_VOLUME:
	case WM8996_RIGHT_LINE_INPUT_VOLUME:
	case WM8996_LINE_INPUT_CONTROL:
	case WM8996_DAC1_HPOUT1_VOLUME:
	case WM8996_DAC2_HPOUT2_VOLUME:
	case WM8996_DAC1_LEFT_VOLUME:
	case WM8996_DAC1_RIGHT_VOLUME:
	case WM8996_DAC2_LEFT_VOLUME:
	case WM8996_DAC2_RIGHT_VOLUME:
	case WM8996_OUTPUT1_LEFT_VOLUME:
	case WM8996_OUTPUT1_RIGHT_VOLUME:
	case WM8996_OUTPUT2_LEFT_VOLUME:
	case WM8996_OUTPUT2_RIGHT_VOLUME:
	case WM8996_MICBIAS_1:
	case WM8996_MICBIAS_2:
	case WM8996_LDO_1:
	case WM8996_LDO_2:
	case WM8996_ACCESSORY_DETECT_MODE_1:
	case WM8996_ACCESSORY_DETECT_MODE_2:
	case WM8996_HEADPHONE_DETECT_1:
	case WM8996_HEADPHONE_DETECT_2:
	case WM8996_MIC_DETECT_1:
	case WM8996_MIC_DETECT_2:
	case WM8996_MIC_DETECT_3:
	case WM8996_CHARGE_PUMP_1:
	case WM8996_CHARGE_PUMP_2:
	case WM8996_DC_SERVO_1:
	case WM8996_DC_SERVO_2:
	case WM8996_DC_SERVO_3:
	case WM8996_DC_SERVO_5:
	case WM8996_DC_SERVO_6:
	case WM8996_DC_SERVO_7:
	case WM8996_DC_SERVO_READBACK_0:
	case WM8996_ANALOGUE_HP_1:
	case WM8996_ANALOGUE_HP_2:
	case WM8996_CHIP_REVISION:
	case WM8996_CONTROL_INTERFACE_1:
	case WM8996_WRITE_SEQUENCER_CTRL_1:
	case WM8996_WRITE_SEQUENCER_CTRL_2:
	case WM8996_AIF_CLOCKING_1:
	case WM8996_AIF_CLOCKING_2:
	case WM8996_CLOCKING_1:
	case WM8996_CLOCKING_2:
	case WM8996_AIF_RATE:
	case WM8996_FLL_CONTROL_1:
	case WM8996_FLL_CONTROL_2:
	case WM8996_FLL_CONTROL_3:
	case WM8996_FLL_CONTROL_4:
	case WM8996_FLL_CONTROL_5:
	case WM8996_FLL_CONTROL_6:
	case WM8996_FLL_EFS_1:
	case WM8996_FLL_EFS_2:
	case WM8996_AIF1_CONTROL:
	case WM8996_AIF1_BCLK:
	case WM8996_AIF1_TX_LRCLK_1:
	case WM8996_AIF1_TX_LRCLK_2:
	case WM8996_AIF1_RX_LRCLK_1:
	case WM8996_AIF1_RX_LRCLK_2:
	case WM8996_AIF1TX_DATA_CONFIGURATION_1:
	case WM8996_AIF1TX_DATA_CONFIGURATION_2:
	case WM8996_AIF1RX_DATA_CONFIGURATION:
	case WM8996_AIF1TX_CHANNEL_0_CONFIGURATION:
	case WM8996_AIF1TX_CHANNEL_1_CONFIGURATION:
	case WM8996_AIF1TX_CHANNEL_2_CONFIGURATION:
	case WM8996_AIF1TX_CHANNEL_3_CONFIGURATION:
	case WM8996_AIF1TX_CHANNEL_4_CONFIGURATION:
	case WM8996_AIF1TX_CHANNEL_5_CONFIGURATION:
	case WM8996_AIF1RX_CHANNEL_0_CONFIGURATION:
	case WM8996_AIF1RX_CHANNEL_1_CONFIGURATION:
	case WM8996_AIF1RX_CHANNEL_2_CONFIGURATION:
	case WM8996_AIF1RX_CHANNEL_3_CONFIGURATION:
	case WM8996_AIF1RX_CHANNEL_4_CONFIGURATION:
	case WM8996_AIF1RX_CHANNEL_5_CONFIGURATION:
	case WM8996_AIF1RX_MONO_CONFIGURATION:
	case WM8996_AIF1TX_TEST:
	case WM8996_AIF2_CONTROL:
	case WM8996_AIF2_BCLK:
	case WM8996_AIF2_TX_LRCLK_1:
	case WM8996_AIF2_TX_LRCLK_2:
	case WM8996_AIF2_RX_LRCLK_1:
	case WM8996_AIF2_RX_LRCLK_2:
	case WM8996_AIF2TX_DATA_CONFIGURATION_1:
	case WM8996_AIF2TX_DATA_CONFIGURATION_2:
	case WM8996_AIF2RX_DATA_CONFIGURATION:
	case WM8996_AIF2TX_CHANNEL_0_CONFIGURATION:
	case WM8996_AIF2TX_CHANNEL_1_CONFIGURATION:
	case WM8996_AIF2RX_CHANNEL_0_CONFIGURATION:
	case WM8996_AIF2RX_CHANNEL_1_CONFIGURATION:
	case WM8996_AIF2RX_MONO_CONFIGURATION:
	case WM8996_AIF2TX_TEST:
	case WM8996_DSP1_TX_LEFT_VOLUME:
	case WM8996_DSP1_TX_RIGHT_VOLUME:
	case WM8996_DSP1_RX_LEFT_VOLUME:
	case WM8996_DSP1_RX_RIGHT_VOLUME:
	case WM8996_DSP1_TX_FILTERS:
	case WM8996_DSP1_RX_FILTERS_1:
	case WM8996_DSP1_RX_FILTERS_2:
	case WM8996_DSP1_DRC_1:
	case WM8996_DSP1_DRC_2:
	case WM8996_DSP1_DRC_3:
	case WM8996_DSP1_DRC_4:
	case WM8996_DSP1_DRC_5:
	case WM8996_DSP1_RX_EQ_GAINS_1:
	case WM8996_DSP1_RX_EQ_GAINS_2:
	case WM8996_DSP1_RX_EQ_BAND_1_A:
	case WM8996_DSP1_RX_EQ_BAND_1_B:
	case WM8996_DSP1_RX_EQ_BAND_1_PG:
	case WM8996_DSP1_RX_EQ_BAND_2_A:
	case WM8996_DSP1_RX_EQ_BAND_2_B:
	case WM8996_DSP1_RX_EQ_BAND_2_C:
	case WM8996_DSP1_RX_EQ_BAND_2_PG:
	case WM8996_DSP1_RX_EQ_BAND_3_A:
	case WM8996_DSP1_RX_EQ_BAND_3_B:
	case WM8996_DSP1_RX_EQ_BAND_3_C:
	case WM8996_DSP1_RX_EQ_BAND_3_PG:
	case WM8996_DSP1_RX_EQ_BAND_4_A:
	case WM8996_DSP1_RX_EQ_BAND_4_B:
	case WM8996_DSP1_RX_EQ_BAND_4_C:
	case WM8996_DSP1_RX_EQ_BAND_4_PG:
	case WM8996_DSP1_RX_EQ_BAND_5_A:
	case WM8996_DSP1_RX_EQ_BAND_5_B:
	case WM8996_DSP1_RX_EQ_BAND_5_PG:
	case WM8996_DSP2_TX_LEFT_VOLUME:
	case WM8996_DSP2_TX_RIGHT_VOLUME:
	case WM8996_DSP2_RX_LEFT_VOLUME:
	case WM8996_DSP2_RX_RIGHT_VOLUME:
	case WM8996_DSP2_TX_FILTERS:
	case WM8996_DSP2_RX_FILTERS_1:
	case WM8996_DSP2_RX_FILTERS_2:
	case WM8996_DSP2_DRC_1:
	case WM8996_DSP2_DRC_2:
	case WM8996_DSP2_DRC_3:
	case WM8996_DSP2_DRC_4:
	case WM8996_DSP2_DRC_5:
	case WM8996_DSP2_RX_EQ_GAINS_1:
	case WM8996_DSP2_RX_EQ_GAINS_2:
	case WM8996_DSP2_RX_EQ_BAND_1_A:
	case WM8996_DSP2_RX_EQ_BAND_1_B:
	case WM8996_DSP2_RX_EQ_BAND_1_PG:
	case WM8996_DSP2_RX_EQ_BAND_2_A:
	case WM8996_DSP2_RX_EQ_BAND_2_B:
	case WM8996_DSP2_RX_EQ_BAND_2_C:
	case WM8996_DSP2_RX_EQ_BAND_2_PG:
	case WM8996_DSP2_RX_EQ_BAND_3_A:
	case WM8996_DSP2_RX_EQ_BAND_3_B:
	case WM8996_DSP2_RX_EQ_BAND_3_C:
	case WM8996_DSP2_RX_EQ_BAND_3_PG:
	case WM8996_DSP2_RX_EQ_BAND_4_A:
	case WM8996_DSP2_RX_EQ_BAND_4_B:
	case WM8996_DSP2_RX_EQ_BAND_4_C:
	case WM8996_DSP2_RX_EQ_BAND_4_PG:
	case WM8996_DSP2_RX_EQ_BAND_5_A:
	case WM8996_DSP2_RX_EQ_BAND_5_B:
	case WM8996_DSP2_RX_EQ_BAND_5_PG:
	case WM8996_DAC1_MIXER_VOLUMES:
	case WM8996_DAC1_LEFT_MIXER_ROUTING:
	case WM8996_DAC1_RIGHT_MIXER_ROUTING:
	case WM8996_DAC2_MIXER_VOLUMES:
	case WM8996_DAC2_LEFT_MIXER_ROUTING:
	case WM8996_DAC2_RIGHT_MIXER_ROUTING:
	case WM8996_DSP1_TX_LEFT_MIXER_ROUTING:
	case WM8996_DSP1_TX_RIGHT_MIXER_ROUTING:
	case WM8996_DSP2_TX_LEFT_MIXER_ROUTING:
	case WM8996_DSP2_TX_RIGHT_MIXER_ROUTING:
	case WM8996_DSP_TX_MIXER_SELECT:
	case WM8996_DAC_SOFTMUTE:
	case WM8996_OVERSAMPLING:
	case WM8996_SIDETONE:
	case WM8996_GPIO_1:
	case WM8996_GPIO_2:
	case WM8996_GPIO_3:
	case WM8996_GPIO_4:
	case WM8996_GPIO_5:
	case WM8996_PULL_CONTROL_1:
	case WM8996_PULL_CONTROL_2:
	case WM8996_INTERRUPT_STATUS_1:
	case WM8996_INTERRUPT_STATUS_2:
	case WM8996_INTERRUPT_RAW_STATUS_2:
	case WM8996_INTERRUPT_STATUS_1_MASK:
	case WM8996_INTERRUPT_STATUS_2_MASK:
	case WM8996_INTERRUPT_CONTROL:
	case WM8996_LEFT_PDM_SPEAKER:
	case WM8996_RIGHT_PDM_SPEAKER:
	case WM8996_PDM_SPEAKER_MUTE_SEQUENCE:
	case WM8996_PDM_SPEAKER_VOLUME:
		return 1;
	default:
		return 0;
	}
}

static bool wm8996_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WM8996_SOFTWARE_RESET:
	case WM8996_CHIP_REVISION:
	case WM8996_LDO_1:
	case WM8996_LDO_2:
	case WM8996_INTERRUPT_STATUS_1:
	case WM8996_INTERRUPT_STATUS_2:
	case WM8996_INTERRUPT_RAW_STATUS_2:
	case WM8996_DC_SERVO_READBACK_0:
	case WM8996_DC_SERVO_2:
	case WM8996_DC_SERVO_6:
	case WM8996_DC_SERVO_7:
	case WM8996_FLL_CONTROL_6:
	case WM8996_MIC_DETECT_3:
	case WM8996_HEADPHONE_DETECT_1:
	case WM8996_HEADPHONE_DETECT_2:
		return 1;
	default:
		return 0;
	}
}

static const int bclk_divs[] = {
	1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64, 96
};

static void wm8996_update_bclk(struct snd_soc_codec *codec)
{
	struct wm8996_priv *wm8996 = snd_soc_codec_get_drvdata(codec);
	int aif, best, cur_val, bclk_rate, bclk_reg, i;

	/* Don't bother if we're in a low frequency idle mode that
	 * can't support audio.
	 */
	if (wm8996->sysclk < 64000)
		return;

	for (aif = 0; aif < WM8996_AIFS; aif++) {
		switch (aif) {
		case 0:
			bclk_reg = WM8996_AIF1_BCLK;
			break;
		case 1:
			bclk_reg = WM8996_AIF2_BCLK;
			break;
		}

		bclk_rate = wm8996->bclk_rate[aif];

		/* Pick a divisor for BCLK as close as we can get to ideal */
		best = 0;
		for (i = 0; i < ARRAY_SIZE(bclk_divs); i++) {
			cur_val = (wm8996->sysclk / bclk_divs[i]) - bclk_rate;
			if (cur_val < 0) /* BCLK table is sorted */
				break;
			best = i;
		}
		bclk_rate = wm8996->sysclk / bclk_divs[best];
		dev_dbg(codec->dev, "Using BCLK_DIV %d for actual BCLK %dHz\n",
			bclk_divs[best], bclk_rate);

		snd_soc_update_bits(codec, bclk_reg,
				    WM8996_AIF1_BCLK_DIV_MASK, best);
	}
}

static int wm8996_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	struct wm8996_priv *wm8996 = snd_soc_codec_get_drvdata(codec);
	int ret;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		/* Put the MICBIASes into regulating mode */
		snd_soc_update_bits(codec, WM8996_MICBIAS_1,
				    WM8996_MICB1_MODE, 0);
		snd_soc_update_bits(codec, WM8996_MICBIAS_2,
				    WM8996_MICB2_MODE, 0);
		break;

	case SND_SOC_BIAS_STANDBY:
		if (codec->dapm.bias_level == SND_SOC_BIAS_OFF) {
			ret = regulator_bulk_enable(ARRAY_SIZE(wm8996->supplies),
						    wm8996->supplies);
			if (ret != 0) {
				dev_err(codec->dev,
					"Failed to enable supplies: %d\n",
					ret);
				return ret;
			}

			if (wm8996->pdata.ldo_ena >= 0) {
				gpio_set_value_cansleep(wm8996->pdata.ldo_ena,
							1);
				msleep(5);
			}

			regcache_cache_only(wm8996->regmap, false);
			regcache_sync(wm8996->regmap);
		}

		/* Bypass the MICBIASes for lowest power */
		snd_soc_update_bits(codec, WM8996_MICBIAS_1,
				    WM8996_MICB1_MODE, WM8996_MICB1_MODE);
		snd_soc_update_bits(codec, WM8996_MICBIAS_2,
				    WM8996_MICB2_MODE, WM8996_MICB2_MODE);
		break;

	case SND_SOC_BIAS_OFF:
		regcache_cache_only(wm8996->regmap, true);
		if (wm8996->pdata.ldo_ena >= 0) {
			gpio_set_value_cansleep(wm8996->pdata.ldo_ena, 0);
			regcache_cache_only(wm8996->regmap, true);
		}
		regulator_bulk_disable(ARRAY_SIZE(wm8996->supplies),
				       wm8996->supplies);
		break;
	}

	codec->dapm.bias_level = level;

	return 0;
}

static int wm8996_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	int aifctrl = 0;
	int bclk = 0;
	int lrclk_tx = 0;
	int lrclk_rx = 0;
	int aifctrl_reg, bclk_reg, lrclk_tx_reg, lrclk_rx_reg;

	switch (dai->id) {
	case 0:
		aifctrl_reg = WM8996_AIF1_CONTROL;
		bclk_reg = WM8996_AIF1_BCLK;
		lrclk_tx_reg = WM8996_AIF1_TX_LRCLK_2;
		lrclk_rx_reg = WM8996_AIF1_RX_LRCLK_2;
		break;
	case 1:
		aifctrl_reg = WM8996_AIF2_CONTROL;
		bclk_reg = WM8996_AIF2_BCLK;
		lrclk_tx_reg = WM8996_AIF2_TX_LRCLK_2;
		lrclk_rx_reg = WM8996_AIF2_RX_LRCLK_2;
		break;
	default:
		WARN(1, "Invalid dai id %d\n", dai->id);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		bclk |= WM8996_AIF1_BCLK_INV;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		lrclk_tx |= WM8996_AIF1TX_LRCLK_INV;
		lrclk_rx |= WM8996_AIF1RX_LRCLK_INV;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		bclk |= WM8996_AIF1_BCLK_INV;
		lrclk_tx |= WM8996_AIF1TX_LRCLK_INV;
		lrclk_rx |= WM8996_AIF1RX_LRCLK_INV;
		break;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	case SND_SOC_DAIFMT_CBS_CFM:
		lrclk_tx |= WM8996_AIF1TX_LRCLK_MSTR;
		lrclk_rx |= WM8996_AIF1RX_LRCLK_MSTR;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
		bclk |= WM8996_AIF1_BCLK_MSTR;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		bclk |= WM8996_AIF1_BCLK_MSTR;
		lrclk_tx |= WM8996_AIF1TX_LRCLK_MSTR;
		lrclk_rx |= WM8996_AIF1RX_LRCLK_MSTR;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		break;
	case SND_SOC_DAIFMT_DSP_B:
		aifctrl |= 1;
		break;
	case SND_SOC_DAIFMT_I2S:
		aifctrl |= 2;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		aifctrl |= 3;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_update_bits(codec, aifctrl_reg, WM8996_AIF1_FMT_MASK, aifctrl);
	snd_soc_update_bits(codec, bclk_reg,
			    WM8996_AIF1_BCLK_INV | WM8996_AIF1_BCLK_MSTR,
			    bclk);
	snd_soc_update_bits(codec, lrclk_tx_reg,
			    WM8996_AIF1TX_LRCLK_INV |
			    WM8996_AIF1TX_LRCLK_MSTR,
			    lrclk_tx);
	snd_soc_update_bits(codec, lrclk_rx_reg,
			    WM8996_AIF1RX_LRCLK_INV |
			    WM8996_AIF1RX_LRCLK_MSTR,
			    lrclk_rx);

	return 0;
}

static const int dsp_divs[] = {
	48000, 32000, 16000, 8000
};

static int wm8996_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm8996_priv *wm8996 = snd_soc_codec_get_drvdata(codec);
	int bits, i, bclk_rate, best;
	int aifdata = 0;
	int lrclk = 0;
	int dsp = 0;
	int aifdata_reg, lrclk_reg, dsp_shift;

	switch (dai->id) {
	case 0:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK ||
		    (snd_soc_read(codec, WM8996_GPIO_1)) & WM8996_GP1_FN_MASK) {
			aifdata_reg = WM8996_AIF1RX_DATA_CONFIGURATION;
			lrclk_reg = WM8996_AIF1_RX_LRCLK_1;
		} else {
			aifdata_reg = WM8996_AIF1TX_DATA_CONFIGURATION_1;
			lrclk_reg = WM8996_AIF1_TX_LRCLK_1;
		}
		dsp_shift = 0;
		break;
	case 1:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK ||
		    (snd_soc_read(codec, WM8996_GPIO_2)) & WM8996_GP2_FN_MASK) {
			aifdata_reg = WM8996_AIF2RX_DATA_CONFIGURATION;
			lrclk_reg = WM8996_AIF2_RX_LRCLK_1;
		} else {
			aifdata_reg = WM8996_AIF2TX_DATA_CONFIGURATION_1;
			lrclk_reg = WM8996_AIF2_TX_LRCLK_1;
		}
		dsp_shift = WM8996_DSP2_DIV_SHIFT;
		break;
	default:
		WARN(1, "Invalid dai id %d\n", dai->id);
		return -EINVAL;
	}

	bclk_rate = snd_soc_params_to_bclk(params);
	if (bclk_rate < 0) {
		dev_err(codec->dev, "Unsupported BCLK rate: %d\n", bclk_rate);
		return bclk_rate;
	}

	wm8996->bclk_rate[dai->id] = bclk_rate;
	wm8996->rx_rate[dai->id] = params_rate(params);

	/* Needs looking at for TDM */
	bits = snd_pcm_format_width(params_format(params));
	if (bits < 0)
		return bits;
	aifdata |= (bits << WM8996_AIF1TX_WL_SHIFT) | bits;

	best = 0;
	for (i = 0; i < ARRAY_SIZE(dsp_divs); i++) {
		if (abs(dsp_divs[i] - params_rate(params)) <
		    abs(dsp_divs[best] - params_rate(params)))
			best = i;
	}
	dsp |= i << dsp_shift;

	wm8996_update_bclk(codec);

	lrclk = bclk_rate / params_rate(params);
	dev_dbg(dai->dev, "Using LRCLK rate %d for actual LRCLK %dHz\n",
		lrclk, bclk_rate / lrclk);

	snd_soc_update_bits(codec, aifdata_reg,
			    WM8996_AIF1TX_WL_MASK |
			    WM8996_AIF1TX_SLOT_LEN_MASK,
			    aifdata);
	snd_soc_update_bits(codec, lrclk_reg, WM8996_AIF1RX_RATE_MASK,
			    lrclk);
	snd_soc_update_bits(codec, WM8996_AIF_CLOCKING_2,
			    WM8996_DSP1_DIV_MASK << dsp_shift, dsp);

	return 0;
}

static int wm8996_set_sysclk(struct snd_soc_dai *dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm8996_priv *wm8996 = snd_soc_codec_get_drvdata(codec);
	int lfclk = 0;
	int ratediv = 0;
	int sync = WM8996_REG_SYNC;
	int src;
	int old;

	if (freq == wm8996->sysclk && clk_id == wm8996->sysclk_src)
		return 0;

	/* Disable SYSCLK while we reconfigure */
	old = snd_soc_read(codec, WM8996_AIF_CLOCKING_1) & WM8996_SYSCLK_ENA;
	snd_soc_update_bits(codec, WM8996_AIF_CLOCKING_1,
			    WM8996_SYSCLK_ENA, 0);

	switch (clk_id) {
	case WM8996_SYSCLK_MCLK1:
		wm8996->sysclk = freq;
		src = 0;
		break;
	case WM8996_SYSCLK_MCLK2:
		wm8996->sysclk = freq;
		src = 1;
		break;
	case WM8996_SYSCLK_FLL:
		wm8996->sysclk = freq;
		src = 2;
		break;
	default:
		dev_err(codec->dev, "Unsupported clock source %d\n", clk_id);
		return -EINVAL;
	}

	switch (wm8996->sysclk) {
	case 5644800:
	case 6144000:
		snd_soc_update_bits(codec, WM8996_AIF_RATE,
				    WM8996_SYSCLK_RATE, 0);
		break;
	case 22579200:
	case 24576000:
		ratediv = WM8996_SYSCLK_DIV;
		wm8996->sysclk /= 2;
	case 11289600:
	case 12288000:
		snd_soc_update_bits(codec, WM8996_AIF_RATE,
				    WM8996_SYSCLK_RATE, WM8996_SYSCLK_RATE);
		break;
	case 32000:
	case 32768:
		lfclk = WM8996_LFCLK_ENA;
		sync = 0;
		break;
	default:
		dev_warn(codec->dev, "Unsupported clock rate %dHz\n",
			 wm8996->sysclk);
		return -EINVAL;
	}

	wm8996_update_bclk(codec);

	snd_soc_update_bits(codec, WM8996_AIF_CLOCKING_1,
			    WM8996_SYSCLK_SRC_MASK | WM8996_SYSCLK_DIV_MASK,
			    src << WM8996_SYSCLK_SRC_SHIFT | ratediv);
	snd_soc_update_bits(codec, WM8996_CLOCKING_1, WM8996_LFCLK_ENA, lfclk);
	snd_soc_update_bits(codec, WM8996_CONTROL_INTERFACE_1,
			    WM8996_REG_SYNC, sync);
	snd_soc_update_bits(codec, WM8996_AIF_CLOCKING_1,
			    WM8996_SYSCLK_ENA, old);

	wm8996->sysclk_src = clk_id;

	return 0;
}

struct _fll_div {
	u16 fll_fratio;
	u16 fll_outdiv;
	u16 fll_refclk_div;
	u16 fll_loop_gain;
	u16 fll_ref_freq;
	u16 n;
	u16 theta;
	u16 lambda;
};

static struct {
	unsigned int min;
	unsigned int max;
	u16 fll_fratio;
	int ratio;
} fll_fratios[] = {
	{       0,    64000, 4, 16 },
	{   64000,   128000, 3,  8 },
	{  128000,   256000, 2,  4 },
	{  256000,  1000000, 1,  2 },
	{ 1000000, 13500000, 0,  1 },
};

static int fll_factors(struct _fll_div *fll_div, unsigned int Fref,
		       unsigned int Fout)
{
	unsigned int target;
	unsigned int div;
	unsigned int fratio, gcd_fll;
	int i;

	/* Fref must be <=13.5MHz */
	div = 1;
	fll_div->fll_refclk_div = 0;
	while ((Fref / div) > 13500000) {
		div *= 2;
		fll_div->fll_refclk_div++;

		if (div > 8) {
			pr_err("Can't scale %dMHz input down to <=13.5MHz\n",
			       Fref);
			return -EINVAL;
		}
	}

	pr_debug("FLL Fref=%u Fout=%u\n", Fref, Fout);

	/* Apply the division for our remaining calculations */
	Fref /= div;

	if (Fref >= 3000000)
		fll_div->fll_loop_gain = 5;
	else
		fll_div->fll_loop_gain = 0;

	if (Fref >= 48000)
		fll_div->fll_ref_freq = 0;
	else
		fll_div->fll_ref_freq = 1;

	/* Fvco should be 90-100MHz; don't check the upper bound */
	div = 2;
	while (Fout * div < 90000000) {
		div++;
		if (div > 64) {
			pr_err("Unable to find FLL_OUTDIV for Fout=%uHz\n",
			       Fout);
			return -EINVAL;
		}
	}
	target = Fout * div;
	fll_div->fll_outdiv = div - 1;

	pr_debug("FLL Fvco=%dHz\n", target);

	/* Find an appropraite FLL_FRATIO and factor it out of the target */
	for (i = 0; i < ARRAY_SIZE(fll_fratios); i++) {
		if (fll_fratios[i].min <= Fref && Fref <= fll_fratios[i].max) {
			fll_div->fll_fratio = fll_fratios[i].fll_fratio;
			fratio = fll_fratios[i].ratio;
			break;
		}
	}
	if (i == ARRAY_SIZE(fll_fratios)) {
		pr_err("Unable to find FLL_FRATIO for Fref=%uHz\n", Fref);
		return -EINVAL;
	}

	fll_div->n = target / (fratio * Fref);

	if (target % Fref == 0) {
		fll_div->theta = 0;
		fll_div->lambda = 0;
	} else {
		gcd_fll = gcd(target, fratio * Fref);

		fll_div->theta = (target - (fll_div->n * fratio * Fref))
			/ gcd_fll;
		fll_div->lambda = (fratio * Fref) / gcd_fll;
	}

	pr_debug("FLL N=%x THETA=%x LAMBDA=%x\n",
		 fll_div->n, fll_div->theta, fll_div->lambda);
	pr_debug("FLL_FRATIO=%x FLL_OUTDIV=%x FLL_REFCLK_DIV=%x\n",
		 fll_div->fll_fratio, fll_div->fll_outdiv,
		 fll_div->fll_refclk_div);

	return 0;
}

static int wm8996_set_fll(struct snd_soc_codec *codec, int fll_id, int source,
			  unsigned int Fref, unsigned int Fout)
{
	struct wm8996_priv *wm8996 = snd_soc_codec_get_drvdata(codec);
	struct i2c_client *i2c = to_i2c_client(codec->dev);
	struct _fll_div fll_div;
	unsigned long timeout;
	int ret, reg, retry;

	/* Any change? */
	if (source == wm8996->fll_src && Fref == wm8996->fll_fref &&
	    Fout == wm8996->fll_fout)
		return 0;

	if (Fout == 0) {
		dev_dbg(codec->dev, "FLL disabled\n");

		wm8996->fll_fref = 0;
		wm8996->fll_fout = 0;

		snd_soc_update_bits(codec, WM8996_FLL_CONTROL_1,
				    WM8996_FLL_ENA, 0);

		wm8996_bg_disable(codec);

		return 0;
	}

	ret = fll_factors(&fll_div, Fref, Fout);
	if (ret != 0)
		return ret;

	switch (source) {
	case WM8996_FLL_MCLK1:
		reg = 0;
		break;
	case WM8996_FLL_MCLK2:
		reg = 1;
		break;
	case WM8996_FLL_DACLRCLK1:
		reg = 2;
		break;
	case WM8996_FLL_BCLK1:
		reg = 3;
		break;
	default:
		dev_err(codec->dev, "Unknown FLL source %d\n", ret);
		return -EINVAL;
	}

	reg |= fll_div.fll_refclk_div << WM8996_FLL_REFCLK_DIV_SHIFT;
	reg |= fll_div.fll_ref_freq << WM8996_FLL_REF_FREQ_SHIFT;

	snd_soc_update_bits(codec, WM8996_FLL_CONTROL_5,
			    WM8996_FLL_REFCLK_DIV_MASK | WM8996_FLL_REF_FREQ |
			    WM8996_FLL_REFCLK_SRC_MASK, reg);

	reg = 0;
	if (fll_div.theta || fll_div.lambda)
		reg |= WM8996_FLL_EFS_ENA | (3 << WM8996_FLL_LFSR_SEL_SHIFT);
	else
		reg |= 1 << WM8996_FLL_LFSR_SEL_SHIFT;
	snd_soc_write(codec, WM8996_FLL_EFS_2, reg);

	snd_soc_update_bits(codec, WM8996_FLL_CONTROL_2,
			    WM8996_FLL_OUTDIV_MASK |
			    WM8996_FLL_FRATIO_MASK,
			    (fll_div.fll_outdiv << WM8996_FLL_OUTDIV_SHIFT) |
			    (fll_div.fll_fratio));

	snd_soc_write(codec, WM8996_FLL_CONTROL_3, fll_div.theta);

	snd_soc_update_bits(codec, WM8996_FLL_CONTROL_4,
			    WM8996_FLL_N_MASK | WM8996_FLL_LOOP_GAIN_MASK,
			    (fll_div.n << WM8996_FLL_N_SHIFT) |
			    fll_div.fll_loop_gain);

	snd_soc_write(codec, WM8996_FLL_EFS_1, fll_div.lambda);

	/* Enable the bandgap if it's not already enabled */
	ret = snd_soc_read(codec, WM8996_FLL_CONTROL_1);
	if (!(ret & WM8996_FLL_ENA))
		wm8996_bg_enable(codec);

	/* Clear any pending completions (eg, from failed startups) */
	try_wait_for_completion(&wm8996->fll_lock);

	snd_soc_update_bits(codec, WM8996_FLL_CONTROL_1,
			    WM8996_FLL_ENA, WM8996_FLL_ENA);

	/* The FLL supports live reconfiguration - kick that in case we were
	 * already enabled.
	 */
	snd_soc_write(codec, WM8996_FLL_CONTROL_6, WM8996_FLL_SWITCH_CLK);

	/* Wait for the FLL to lock, using the interrupt if possible */
	if (Fref > 1000000)
		timeout = usecs_to_jiffies(300);
	else
		timeout = msecs_to_jiffies(2);

	/* Allow substantially longer if we've actually got the IRQ, poll
	 * at a slightly higher rate if we don't.
	 */
	if (i2c->irq)
		timeout *= 10;
	else
		timeout /= 2;

	for (retry = 0; retry < 10; retry++) {
		ret = wait_for_completion_timeout(&wm8996->fll_lock,
						  timeout);
		if (ret != 0) {
			WARN_ON(!i2c->irq);
			break;
		}

		ret = snd_soc_read(codec, WM8996_INTERRUPT_RAW_STATUS_2);
		if (ret & WM8996_FLL_LOCK_STS)
			break;
	}
	if (retry == 10) {
		dev_err(codec->dev, "Timed out waiting for FLL\n");
		ret = -ETIMEDOUT;
	}

	dev_dbg(codec->dev, "FLL configured for %dHz->%dHz\n", Fref, Fout);

	wm8996->fll_fref = Fref;
	wm8996->fll_fout = Fout;
	wm8996->fll_src = source;

	return ret;
}

#ifdef CONFIG_GPIOLIB
static inline struct wm8996_priv *gpio_to_wm8996(struct gpio_chip *chip)
{
	return container_of(chip, struct wm8996_priv, gpio_chip);
}

static void wm8996_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct wm8996_priv *wm8996 = gpio_to_wm8996(chip);

	regmap_update_bits(wm8996->regmap, WM8996_GPIO_1 + offset,
			   WM8996_GP1_LVL, !!value << WM8996_GP1_LVL_SHIFT);
}

static int wm8996_gpio_direction_out(struct gpio_chip *chip,
				     unsigned offset, int value)
{
	struct wm8996_priv *wm8996 = gpio_to_wm8996(chip);
	int val;

	val = (1 << WM8996_GP1_FN_SHIFT) | (!!value << WM8996_GP1_LVL_SHIFT);

	return regmap_update_bits(wm8996->regmap, WM8996_GPIO_1 + offset,
				  WM8996_GP1_FN_MASK | WM8996_GP1_DIR |
				  WM8996_GP1_LVL, val);
}

static int wm8996_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct wm8996_priv *wm8996 = gpio_to_wm8996(chip);
	unsigned int reg;
	int ret;

	ret = regmap_read(wm8996->regmap, WM8996_GPIO_1 + offset, &reg);
	if (ret < 0)
		return ret;

	return (reg & WM8996_GP1_LVL) != 0;
}

static int wm8996_gpio_direction_in(struct gpio_chip *chip, unsigned offset)
{
	struct wm8996_priv *wm8996 = gpio_to_wm8996(chip);

	return regmap_update_bits(wm8996->regmap, WM8996_GPIO_1 + offset,
				  WM8996_GP1_FN_MASK | WM8996_GP1_DIR,
				  (1 << WM8996_GP1_FN_SHIFT) |
				  (1 << WM8996_GP1_DIR_SHIFT));
}

static struct gpio_chip wm8996_template_chip = {
	.label			= "wm8996",
	.owner			= THIS_MODULE,
	.direction_output	= wm8996_gpio_direction_out,
	.set			= wm8996_gpio_set,
	.direction_input	= wm8996_gpio_direction_in,
	.get			= wm8996_gpio_get,
	.can_sleep		= 1,
};

static void wm8996_init_gpio(struct wm8996_priv *wm8996)
{
	int ret;

	wm8996->gpio_chip = wm8996_template_chip;
	wm8996->gpio_chip.ngpio = 5;
	wm8996->gpio_chip.dev = wm8996->dev;

	if (wm8996->pdata.gpio_base)
		wm8996->gpio_chip.base = wm8996->pdata.gpio_base;
	else
		wm8996->gpio_chip.base = -1;

	ret = gpiochip_add(&wm8996->gpio_chip);
	if (ret != 0)
		dev_err(wm8996->dev, "Failed to add GPIOs: %d\n", ret);
}

static void wm8996_free_gpio(struct wm8996_priv *wm8996)
{
	gpiochip_remove(&wm8996->gpio_chip);
}
#else
static void wm8996_init_gpio(struct wm8996_priv *wm8996)
{
}

static void wm8996_free_gpio(struct wm8996_priv *wm8996)
{
}
#endif

/**
 * wm8996_detect - Enable default WM8996 jack detection
 *
 * The WM8996 has advanced accessory detection support for headsets.
 * This function provides a default implementation which integrates
 * the majority of this functionality with minimal user configuration.
 *
 * This will detect headset, headphone and short circuit button and
 * will also detect inverted microphone ground connections and update
 * the polarity of the connections.
 */
int wm8996_detect(struct snd_soc_codec *codec, struct snd_soc_jack *jack,
		  wm8996_polarity_fn polarity_cb)
{
	struct wm8996_priv *wm8996 = snd_soc_codec_get_drvdata(codec);
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	wm8996->jack = jack;
	wm8996->detecting = true;
	wm8996->polarity_cb = polarity_cb;
	wm8996->jack_flips = 0;

	if (wm8996->polarity_cb)
		wm8996->polarity_cb(codec, 0);

	/* Clear discarge to avoid noise during detection */
	snd_soc_update_bits(codec, WM8996_MICBIAS_1,
			    WM8996_MICB1_DISCH, 0);
	snd_soc_update_bits(codec, WM8996_MICBIAS_2,
			    WM8996_MICB2_DISCH, 0);

	/* LDO2 powers the microphones, SYSCLK clocks detection */
	snd_soc_dapm_mutex_lock(dapm);

	snd_soc_dapm_force_enable_pin_unlocked(dapm, "LDO2");
	snd_soc_dapm_force_enable_pin_unlocked(dapm, "SYSCLK");

	snd_soc_dapm_mutex_unlock(dapm);

	/* We start off just enabling microphone detection - even a
	 * plain headphone will trigger detection.
	 */
	snd_soc_update_bits(codec, WM8996_MIC_DETECT_1,
			    WM8996_MICD_ENA, WM8996_MICD_ENA);

	/* Slowest detection rate, gives debounce for initial detection */
	snd_soc_update_bits(codec, WM8996_MIC_DETECT_1,
			    WM8996_MICD_RATE_MASK,
			    WM8996_MICD_RATE_MASK);

	/* Enable interrupts and we're off */
	snd_soc_update_bits(codec, WM8996_INTERRUPT_STATUS_2_MASK,
			    WM8996_IM_MICD_EINT | WM8996_HP_DONE_EINT, 0);

	return 0;
}
EXPORT_SYMBOL_GPL(wm8996_detect);

static void wm8996_hpdet_irq(struct snd_soc_codec *codec)
{
	struct wm8996_priv *wm8996 = snd_soc_codec_get_drvdata(codec);
	int val, reg, report;

	/* Assume headphone in error conditions; we need to report
	 * something or we stall our state machine.
	 */
	report = SND_JACK_HEADPHONE;

	reg = snd_soc_read(codec, WM8996_HEADPHONE_DETECT_2);
	if (reg < 0) {
		dev_err(codec->dev, "Failed to read HPDET status\n");
		goto out;
	}

	if (!(reg & WM8996_HP_DONE)) {
		dev_err(codec->dev, "Got HPDET IRQ but HPDET is busy\n");
		goto out;
	}

	val = reg & WM8996_HP_LVL_MASK;

	dev_dbg(codec->dev, "HPDET measured %d ohms\n", val);

	/* If we've got high enough impedence then report as line,
	 * otherwise assume headphone.
	 */
	if (val >= 126)
		report = SND_JACK_LINEOUT;
	else
		report = SND_JACK_HEADPHONE;

out:
	if (wm8996->jack_mic)
		report |= SND_JACK_MICROPHONE;

	snd_soc_jack_report(wm8996->jack, report,
			    SND_JACK_LINEOUT | SND_JACK_HEADSET);

	wm8996->detecting = false;

	/* If the output isn't running re-clamp it */
	if (!(snd_soc_read(codec, WM8996_POWER_MANAGEMENT_1) &
	      (WM8996_HPOUT1L_ENA | WM8996_HPOUT1R_RMV_SHORT)))
		snd_soc_update_bits(codec, WM8996_ANALOGUE_HP_1,
				    WM8996_HPOUT1L_RMV_SHORT |
				    WM8996_HPOUT1R_RMV_SHORT, 0);

	/* Go back to looking at the microphone */
	snd_soc_update_bits(codec, WM8996_ACCESSORY_DETECT_MODE_1,
			    WM8996_JD_MODE_MASK, 0);
	snd_soc_update_bits(codec, WM8996_MIC_DETECT_1, WM8996_MICD_ENA,
			    WM8996_MICD_ENA);

	snd_soc_dapm_disable_pin(&codec->dapm, "Bandgap");
	snd_soc_dapm_sync(&codec->dapm);
}

static void wm8996_hpdet_start(struct snd_soc_codec *codec)
{
	/* Unclamp the output, we can't measure while we're shorting it */
	snd_soc_update_bits(codec, WM8996_ANALOGUE_HP_1,
			    WM8996_HPOUT1L_RMV_SHORT |
			    WM8996_HPOUT1R_RMV_SHORT,
			    WM8996_HPOUT1L_RMV_SHORT |
			    WM8996_HPOUT1R_RMV_SHORT);

	/* We need bandgap for HPDET */
	snd_soc_dapm_force_enable_pin(&codec->dapm, "Bandgap");
	snd_soc_dapm_sync(&codec->dapm);

	/* Go into headphone detect left mode */
	snd_soc_update_bits(codec, WM8996_MIC_DETECT_1, WM8996_MICD_ENA, 0);
	snd_soc_update_bits(codec, WM8996_ACCESSORY_DETECT_MODE_1,
			    WM8996_JD_MODE_MASK, 1);

	/* Trigger a measurement */
	snd_soc_update_bits(codec, WM8996_HEADPHONE_DETECT_1,
			    WM8996_HP_POLL, WM8996_HP_POLL);
}

static void wm8996_report_headphone(struct snd_soc_codec *codec)
{
	dev_dbg(codec->dev, "Headphone detected\n");
	wm8996_hpdet_start(codec);

	/* Increase the detection rate a bit for responsiveness. */
	snd_soc_update_bits(codec, WM8996_MIC_DETECT_1,
			    WM8996_MICD_RATE_MASK |
			    WM8996_MICD_BIAS_STARTTIME_MASK,
			    7 << WM8996_MICD_RATE_SHIFT |
			    7 << WM8996_MICD_BIAS_STARTTIME_SHIFT);
}

static void wm8996_micd(struct snd_soc_codec *codec)
{
	struct wm8996_priv *wm8996 = snd_soc_codec_get_drvdata(codec);
	int val, reg;

	val = snd_soc_read(codec, WM8996_MIC_DETECT_3);

	dev_dbg(codec->dev, "Microphone event: %x\n", val);

	if (!(val & WM8996_MICD_VALID)) {
		dev_warn(codec->dev, "Microphone detection state invalid\n");
		return;
	}

	/* No accessory, reset everything and report removal */
	if (!(val & WM8996_MICD_STS)) {
		dev_dbg(codec->dev, "Jack removal detected\n");
		wm8996->jack_mic = false;
		wm8996->detecting = true;
		wm8996->jack_flips = 0;
		snd_soc_jack_report(wm8996->jack, 0,
				    SND_JACK_LINEOUT | SND_JACK_HEADSET |
				    SND_JACK_BTN_0);

		snd_soc_update_bits(codec, WM8996_MIC_DETECT_1,
				    WM8996_MICD_RATE_MASK |
				    WM8996_MICD_BIAS_STARTTIME_MASK,
				    WM8996_MICD_RATE_MASK |
				    9 << WM8996_MICD_BIAS_STARTTIME_SHIFT);
		return;
	}

	/* If the measurement is very high we've got a microphone,
	 * either we just detected one or if we already reported then
	 * we've got a button release event.
	 */
	if (val & 0x400) {
		if (wm8996->detecting) {
			dev_dbg(codec->dev, "Microphone detected\n");
			wm8996->jack_mic = true;
			wm8996_hpdet_start(codec);

			/* Increase poll rate to give better responsiveness
			 * for buttons */
			snd_soc_update_bits(codec, WM8996_MIC_DETECT_1,
					    WM8996_MICD_RATE_MASK |
					    WM8996_MICD_BIAS_STARTTIME_MASK,
					    5 << WM8996_MICD_RATE_SHIFT |
					    7 << WM8996_MICD_BIAS_STARTTIME_SHIFT);
		} else {
			dev_dbg(codec->dev, "Mic button up\n");
			snd_soc_jack_report(wm8996->jack, 0, SND_JACK_BTN_0);
		}

		return;
	}

	/* If we detected a lower impedence during initial startup
	 * then we probably have the wrong polarity, flip it.  Don't
	 * do this for the lowest impedences to speed up detection of
	 * plain headphones.  If both polarities report a low
	 * impedence then give up and report headphones.
	 */
	if (wm8996->detecting && (val & 0x3f0)) {
		wm8996->jack_flips++;

		if (wm8996->jack_flips > 1) {
			wm8996_report_headphone(codec);
			return;
		}

		reg = snd_soc_read(codec, WM8996_ACCESSORY_DETECT_MODE_2);
		reg ^= WM8996_HPOUT1FB_SRC | WM8996_MICD_SRC |
			WM8996_MICD_BIAS_SRC;
		snd_soc_update_bits(codec, WM8996_ACCESSORY_DETECT_MODE_2,
				    WM8996_HPOUT1FB_SRC | WM8996_MICD_SRC |
				    WM8996_MICD_BIAS_SRC, reg);

		if (wm8996->polarity_cb)
			wm8996->polarity_cb(codec,
					    (reg & WM8996_MICD_SRC) != 0);

		dev_dbg(codec->dev, "Set microphone polarity to %d\n",
			(reg & WM8996_MICD_SRC) != 0);

		return;
	}

	/* Don't distinguish between buttons, just report any low
	 * impedence as BTN_0.
	 */
	if (val & 0x3fc) {
		if (wm8996->jack_mic) {
			dev_dbg(codec->dev, "Mic button detected\n");
			snd_soc_jack_report(wm8996->jack, SND_JACK_BTN_0,
					    SND_JACK_BTN_0);
		} else if (wm8996->detecting) {
			wm8996_report_headphone(codec);
		}
	}
}

static irqreturn_t wm8996_irq(int irq, void *data)
{
	struct snd_soc_codec *codec = data;
	struct wm8996_priv *wm8996 = snd_soc_codec_get_drvdata(codec);
	int irq_val;

	irq_val = snd_soc_read(codec, WM8996_INTERRUPT_STATUS_2);
	if (irq_val < 0) {
		dev_err(codec->dev, "Failed to read IRQ status: %d\n",
			irq_val);
		return IRQ_NONE;
	}
	irq_val &= ~snd_soc_read(codec, WM8996_INTERRUPT_STATUS_2_MASK);

	if (!irq_val)
		return IRQ_NONE;

	snd_soc_write(codec, WM8996_INTERRUPT_STATUS_2, irq_val);

	if (irq_val & (WM8996_DCS_DONE_01_EINT | WM8996_DCS_DONE_23_EINT)) {
		dev_dbg(codec->dev, "DC servo IRQ\n");
		complete(&wm8996->dcs_done);
	}

	if (irq_val & WM8996_FIFOS_ERR_EINT)
		dev_err(codec->dev, "Digital core FIFO error\n");

	if (irq_val & WM8996_FLL_LOCK_EINT) {
		dev_dbg(codec->dev, "FLL locked\n");
		complete(&wm8996->fll_lock);
	}

	if (irq_val & WM8996_MICD_EINT)
		wm8996_micd(codec);

	if (irq_val & WM8996_HP_DONE_EINT)
		wm8996_hpdet_irq(codec);

	return IRQ_HANDLED;
}

static irqreturn_t wm8996_edge_irq(int irq, void *data)
{
	irqreturn_t ret = IRQ_NONE;
	irqreturn_t val;

	do {
		val = wm8996_irq(irq, data);
		if (val != IRQ_NONE)
			ret = val;
	} while (val != IRQ_NONE);

	return ret;
}

static void wm8996_retune_mobile_pdata(struct snd_soc_codec *codec)
{
	struct wm8996_priv *wm8996 = snd_soc_codec_get_drvdata(codec);
	struct wm8996_pdata *pdata = &wm8996->pdata;

	struct snd_kcontrol_new controls[] = {
		SOC_ENUM_EXT("DSP1 EQ Mode",
			     wm8996->retune_mobile_enum,
			     wm8996_get_retune_mobile_enum,
			     wm8996_put_retune_mobile_enum),
		SOC_ENUM_EXT("DSP2 EQ Mode",
			     wm8996->retune_mobile_enum,
			     wm8996_get_retune_mobile_enum,
			     wm8996_put_retune_mobile_enum),
	};
	int ret, i, j;
	const char **t;

	/* We need an array of texts for the enum API but the number
	 * of texts is likely to be less than the number of
	 * configurations due to the sample rate dependency of the
	 * configurations. */
	wm8996->num_retune_mobile_texts = 0;
	wm8996->retune_mobile_texts = NULL;
	for (i = 0; i < pdata->num_retune_mobile_cfgs; i++) {
		for (j = 0; j < wm8996->num_retune_mobile_texts; j++) {
			if (strcmp(pdata->retune_mobile_cfgs[i].name,
				   wm8996->retune_mobile_texts[j]) == 0)
				break;
		}

		if (j != wm8996->num_retune_mobile_texts)
			continue;

		/* Expand the array... */
		t = krealloc(wm8996->retune_mobile_texts,
			     sizeof(char *) * 
			     (wm8996->num_retune_mobile_texts + 1),
			     GFP_KERNEL);
		if (t == NULL)
			continue;

		/* ...store the new entry... */
		t[wm8996->num_retune_mobile_texts] = 
			pdata->retune_mobile_cfgs[i].name;

		/* ...and remember the new version. */
		wm8996->num_retune_mobile_texts++;
		wm8996->retune_mobile_texts = t;
	}

	dev_dbg(codec->dev, "Allocated %d unique ReTune Mobile names\n",
		wm8996->num_retune_mobile_texts);

	wm8996->retune_mobile_enum.items = wm8996->num_retune_mobile_texts;
	wm8996->retune_mobile_enum.texts = wm8996->retune_mobile_texts;

	ret = snd_soc_add_codec_controls(codec, controls, ARRAY_SIZE(controls));
	if (ret != 0)
		dev_err(codec->dev,
			"Failed to add ReTune Mobile controls: %d\n", ret);
}

static const struct regmap_config wm8996_regmap = {
	.reg_bits = 16,
	.val_bits = 16,

	.max_register = WM8996_MAX_REGISTER,
	.reg_defaults = wm8996_reg,
	.num_reg_defaults = ARRAY_SIZE(wm8996_reg),
	.volatile_reg = wm8996_volatile_register,
	.readable_reg = wm8996_readable_register,
	.cache_type = REGCACHE_RBTREE,
};

static int wm8996_probe(struct snd_soc_codec *codec)
{
	int ret;
	struct wm8996_priv *wm8996 = snd_soc_codec_get_drvdata(codec);
	struct i2c_client *i2c = to_i2c_client(codec->dev);
	int irq_flags;

	wm8996->codec = codec;

	init_completion(&wm8996->dcs_done);
	init_completion(&wm8996->fll_lock);

	if (wm8996->pdata.num_retune_mobile_cfgs)
		wm8996_retune_mobile_pdata(codec);
	else
		snd_soc_add_codec_controls(codec, wm8996_eq_controls,
				     ARRAY_SIZE(wm8996_eq_controls));

	if (i2c->irq) {
		if (wm8996->pdata.irq_flags)
			irq_flags = wm8996->pdata.irq_flags;
		else
			irq_flags = IRQF_TRIGGER_LOW;

		irq_flags |= IRQF_ONESHOT;

		if (irq_flags & (IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING))
			ret = request_threaded_irq(i2c->irq, NULL,
						   wm8996_edge_irq,
						   irq_flags, "wm8996", codec);
		else
			ret = request_threaded_irq(i2c->irq, NULL, wm8996_irq,
						   irq_flags, "wm8996", codec);

		if (ret == 0) {
			/* Unmask the interrupt */
			snd_soc_update_bits(codec, WM8996_INTERRUPT_CONTROL,
					    WM8996_IM_IRQ, 0);

			/* Enable error reporting and DC servo status */
			snd_soc_update_bits(codec,
					    WM8996_INTERRUPT_STATUS_2_MASK,
					    WM8996_IM_DCS_DONE_23_EINT |
					    WM8996_IM_DCS_DONE_01_EINT |
					    WM8996_IM_FLL_LOCK_EINT |
					    WM8996_IM_FIFOS_ERR_EINT,
					    0);
		} else {
			dev_err(codec->dev, "Failed to request IRQ: %d\n",
				ret);
			return ret;
		}
	}

	return 0;
}

static int wm8996_remove(struct snd_soc_codec *codec)
{
	struct i2c_client *i2c = to_i2c_client(codec->dev);

	snd_soc_update_bits(codec, WM8996_INTERRUPT_CONTROL,
			    WM8996_IM_IRQ, WM8996_IM_IRQ);

	if (i2c->irq)
		free_irq(i2c->irq, codec);

	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_wm8996 = {
	.probe =	wm8996_probe,
	.remove =	wm8996_remove,
	.set_bias_level = wm8996_set_bias_level,
	.idle_bias_off	= true,
	.seq_notifier = wm8996_seq_notifier,
	.controls = wm8996_snd_controls,
	.num_controls = ARRAY_SIZE(wm8996_snd_controls),
	.dapm_widgets = wm8996_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(wm8996_dapm_widgets),
	.dapm_routes = wm8996_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(wm8996_dapm_routes),
	.set_pll = wm8996_set_fll,
};

#define WM8996_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |\
		      SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |\
		      SNDRV_PCM_RATE_48000)
#define WM8996_FORMATS (SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_LE |\
			SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S24_LE |\
			SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops wm8996_dai_ops = {
	.set_fmt = wm8996_set_fmt,
	.hw_params = wm8996_hw_params,
	.set_sysclk = wm8996_set_sysclk,
};

static struct snd_soc_dai_driver wm8996_dai[] = {
	{
		.name = "wm8996-aif1",
		.playback = {
			.stream_name = "AIF1 Playback",
			.channels_min = 1,
			.channels_max = 6,
			.rates = WM8996_RATES,
			.formats = WM8996_FORMATS,
			.sig_bits = 24,
		},
		.capture = {
			 .stream_name = "AIF1 Capture",
			 .channels_min = 1,
			 .channels_max = 6,
			 .rates = WM8996_RATES,
			 .formats = WM8996_FORMATS,
			 .sig_bits = 24,
		 },
		.ops = &wm8996_dai_ops,
	},
	{
		.name = "wm8996-aif2",
		.playback = {
			.stream_name = "AIF2 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = WM8996_RATES,
			.formats = WM8996_FORMATS,
			.sig_bits = 24,
		},
		.capture = {
			 .stream_name = "AIF2 Capture",
			 .channels_min = 1,
			 .channels_max = 2,
			 .rates = WM8996_RATES,
			 .formats = WM8996_FORMATS,
			.sig_bits = 24,
		 },
		.ops = &wm8996_dai_ops,
	},
};

static int wm8996_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct wm8996_priv *wm8996;
	int ret, i;
	unsigned int reg;

	wm8996 = devm_kzalloc(&i2c->dev, sizeof(struct wm8996_priv),
			      GFP_KERNEL);
	if (wm8996 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, wm8996);
	wm8996->dev = &i2c->dev;

	if (dev_get_platdata(&i2c->dev))
		memcpy(&wm8996->pdata, dev_get_platdata(&i2c->dev),
		       sizeof(wm8996->pdata));

	if (wm8996->pdata.ldo_ena > 0) {
		ret = gpio_request_one(wm8996->pdata.ldo_ena,
				       GPIOF_OUT_INIT_LOW, "WM8996 ENA");
		if (ret < 0) {
			dev_err(&i2c->dev, "Failed to request GPIO %d: %d\n",
				wm8996->pdata.ldo_ena, ret);
			goto err;
		}
	}

	for (i = 0; i < ARRAY_SIZE(wm8996->supplies); i++)
		wm8996->supplies[i].supply = wm8996_supply_names[i];

	ret = devm_regulator_bulk_get(&i2c->dev, ARRAY_SIZE(wm8996->supplies),
				      wm8996->supplies);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to request supplies: %d\n", ret);
		goto err_gpio;
	}

	wm8996->disable_nb[0].notifier_call = wm8996_regulator_event_0;
	wm8996->disable_nb[1].notifier_call = wm8996_regulator_event_1;
	wm8996->disable_nb[2].notifier_call = wm8996_regulator_event_2;

	/* This should really be moved into the regulator core */
	for (i = 0; i < ARRAY_SIZE(wm8996->supplies); i++) {
		ret = regulator_register_notifier(wm8996->supplies[i].consumer,
						  &wm8996->disable_nb[i]);
		if (ret != 0) {
			dev_err(&i2c->dev,
				"Failed to register regulator notifier: %d\n",
				ret);
		}
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(wm8996->supplies),
				    wm8996->supplies);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to enable supplies: %d\n", ret);
		goto err_gpio;
	}

	if (wm8996->pdata.ldo_ena > 0) {
		gpio_set_value_cansleep(wm8996->pdata.ldo_ena, 1);
		msleep(5);
	}

	wm8996->regmap = devm_regmap_init_i2c(i2c, &wm8996_regmap);
	if (IS_ERR(wm8996->regmap)) {
		ret = PTR_ERR(wm8996->regmap);
		dev_err(&i2c->dev, "regmap_init() failed: %d\n", ret);
		goto err_enable;
	}

	ret = regmap_read(wm8996->regmap, WM8996_SOFTWARE_RESET, &reg);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to read ID register: %d\n", ret);
		goto err_regmap;
	}
	if (reg != 0x8915) {
		dev_err(&i2c->dev, "Device is not a WM8996, ID %x\n", reg);
		ret = -EINVAL;
		goto err_regmap;
	}

	ret = regmap_read(wm8996->regmap, WM8996_CHIP_REVISION, &reg);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to read device revision: %d\n",
			ret);
		goto err_regmap;
	}

	dev_info(&i2c->dev, "revision %c\n",
		 (reg & WM8996_CHIP_REV_MASK) + 'A');

	if (wm8996->pdata.ldo_ena > 0) {
		gpio_set_value_cansleep(wm8996->pdata.ldo_ena, 0);
		regcache_cache_only(wm8996->regmap, true);
	} else {
		ret = regmap_write(wm8996->regmap, WM8996_SOFTWARE_RESET,
				   0x8915);
		if (ret != 0) {
			dev_err(&i2c->dev, "Failed to issue reset: %d\n", ret);
			goto err_regmap;
		}
	}

	regulator_bulk_disable(ARRAY_SIZE(wm8996->supplies), wm8996->supplies);

	/* Apply platform data settings */
	regmap_update_bits(wm8996->regmap, WM8996_LINE_INPUT_CONTROL,
			   WM8996_INL_MODE_MASK | WM8996_INR_MODE_MASK,
			   wm8996->pdata.inl_mode << WM8996_INL_MODE_SHIFT |
			   wm8996->pdata.inr_mode);

	for (i = 0; i < ARRAY_SIZE(wm8996->pdata.gpio_default); i++) {
		if (!wm8996->pdata.gpio_default[i])
			continue;

		regmap_write(wm8996->regmap, WM8996_GPIO_1 + i,
			     wm8996->pdata.gpio_default[i] & 0xffff);
	}

	if (wm8996->pdata.spkmute_seq)
		regmap_update_bits(wm8996->regmap,
				   WM8996_PDM_SPEAKER_MUTE_SEQUENCE,
				   WM8996_SPK_MUTE_ENDIAN |
				   WM8996_SPK_MUTE_SEQ1_MASK,
				   wm8996->pdata.spkmute_seq);

	regmap_update_bits(wm8996->regmap, WM8996_ACCESSORY_DETECT_MODE_2,
			   WM8996_MICD_BIAS_SRC | WM8996_HPOUT1FB_SRC |
			   WM8996_MICD_SRC, wm8996->pdata.micdet_def);

	/* Latch volume update bits */
	regmap_update_bits(wm8996->regmap, WM8996_LEFT_LINE_INPUT_VOLUME,
			   WM8996_IN1_VU, WM8996_IN1_VU);
	regmap_update_bits(wm8996->regmap, WM8996_RIGHT_LINE_INPUT_VOLUME,
			   WM8996_IN1_VU, WM8996_IN1_VU);

	regmap_update_bits(wm8996->regmap, WM8996_DAC1_LEFT_VOLUME,
			   WM8996_DAC1_VU, WM8996_DAC1_VU);
	regmap_update_bits(wm8996->regmap, WM8996_DAC1_RIGHT_VOLUME,
			   WM8996_DAC1_VU, WM8996_DAC1_VU);
	regmap_update_bits(wm8996->regmap, WM8996_DAC2_LEFT_VOLUME,
			   WM8996_DAC2_VU, WM8996_DAC2_VU);
	regmap_update_bits(wm8996->regmap, WM8996_DAC2_RIGHT_VOLUME,
			   WM8996_DAC2_VU, WM8996_DAC2_VU);

	regmap_update_bits(wm8996->regmap, WM8996_OUTPUT1_LEFT_VOLUME,
			   WM8996_DAC1_VU, WM8996_DAC1_VU);
	regmap_update_bits(wm8996->regmap, WM8996_OUTPUT1_RIGHT_VOLUME,
			   WM8996_DAC1_VU, WM8996_DAC1_VU);
	regmap_update_bits(wm8996->regmap, WM8996_OUTPUT2_LEFT_VOLUME,
			   WM8996_DAC2_VU, WM8996_DAC2_VU);
	regmap_update_bits(wm8996->regmap, WM8996_OUTPUT2_RIGHT_VOLUME,
			   WM8996_DAC2_VU, WM8996_DAC2_VU);

	regmap_update_bits(wm8996->regmap, WM8996_DSP1_TX_LEFT_VOLUME,
			   WM8996_DSP1TX_VU, WM8996_DSP1TX_VU);
	regmap_update_bits(wm8996->regmap, WM8996_DSP1_TX_RIGHT_VOLUME,
			   WM8996_DSP1TX_VU, WM8996_DSP1TX_VU);
	regmap_update_bits(wm8996->regmap, WM8996_DSP2_TX_LEFT_VOLUME,
			   WM8996_DSP2TX_VU, WM8996_DSP2TX_VU);
	regmap_update_bits(wm8996->regmap, WM8996_DSP2_TX_RIGHT_VOLUME,
			   WM8996_DSP2TX_VU, WM8996_DSP2TX_VU);

	regmap_update_bits(wm8996->regmap, WM8996_DSP1_RX_LEFT_VOLUME,
			   WM8996_DSP1RX_VU, WM8996_DSP1RX_VU);
	regmap_update_bits(wm8996->regmap, WM8996_DSP1_RX_RIGHT_VOLUME,
			   WM8996_DSP1RX_VU, WM8996_DSP1RX_VU);
	regmap_update_bits(wm8996->regmap, WM8996_DSP2_RX_LEFT_VOLUME,
			   WM8996_DSP2RX_VU, WM8996_DSP2RX_VU);
	regmap_update_bits(wm8996->regmap, WM8996_DSP2_RX_RIGHT_VOLUME,
			   WM8996_DSP2RX_VU, WM8996_DSP2RX_VU);

	/* No support currently for the underclocked TDM modes and
	 * pick a default TDM layout with each channel pair working with
	 * slots 0 and 1. */
	regmap_update_bits(wm8996->regmap,
			   WM8996_AIF1RX_CHANNEL_0_CONFIGURATION,
			   WM8996_AIF1RX_CHAN0_SLOTS_MASK |
			   WM8996_AIF1RX_CHAN0_START_SLOT_MASK,
			   1 << WM8996_AIF1RX_CHAN0_SLOTS_SHIFT | 0);
	regmap_update_bits(wm8996->regmap,
			   WM8996_AIF1RX_CHANNEL_1_CONFIGURATION,
			   WM8996_AIF1RX_CHAN1_SLOTS_MASK |
			   WM8996_AIF1RX_CHAN1_START_SLOT_MASK,
			   1 << WM8996_AIF1RX_CHAN1_SLOTS_SHIFT | 1);
	regmap_update_bits(wm8996->regmap,
			   WM8996_AIF1RX_CHANNEL_2_CONFIGURATION,
			   WM8996_AIF1RX_CHAN2_SLOTS_MASK |
			   WM8996_AIF1RX_CHAN2_START_SLOT_MASK,
			   1 << WM8996_AIF1RX_CHAN2_SLOTS_SHIFT | 0);
	regmap_update_bits(wm8996->regmap,
			   WM8996_AIF1RX_CHANNEL_3_CONFIGURATION,
			   WM8996_AIF1RX_CHAN3_SLOTS_MASK |
			   WM8996_AIF1RX_CHAN0_START_SLOT_MASK,
			   1 << WM8996_AIF1RX_CHAN3_SLOTS_SHIFT | 1);
	regmap_update_bits(wm8996->regmap,
			   WM8996_AIF1RX_CHANNEL_4_CONFIGURATION,
			   WM8996_AIF1RX_CHAN4_SLOTS_MASK |
			   WM8996_AIF1RX_CHAN0_START_SLOT_MASK,
			   1 << WM8996_AIF1RX_CHAN4_SLOTS_SHIFT | 0);
	regmap_update_bits(wm8996->regmap,
			   WM8996_AIF1RX_CHANNEL_5_CONFIGURATION,
			   WM8996_AIF1RX_CHAN5_SLOTS_MASK |
			   WM8996_AIF1RX_CHAN0_START_SLOT_MASK,
			   1 << WM8996_AIF1RX_CHAN5_SLOTS_SHIFT | 1);

	regmap_update_bits(wm8996->regmap,
			   WM8996_AIF2RX_CHANNEL_0_CONFIGURATION,
			   WM8996_AIF2RX_CHAN0_SLOTS_MASK |
			   WM8996_AIF2RX_CHAN0_START_SLOT_MASK,
			   1 << WM8996_AIF2RX_CHAN0_SLOTS_SHIFT | 0);
	regmap_update_bits(wm8996->regmap,
			   WM8996_AIF2RX_CHANNEL_1_CONFIGURATION,
			   WM8996_AIF2RX_CHAN1_SLOTS_MASK |
			   WM8996_AIF2RX_CHAN1_START_SLOT_MASK,
			   1 << WM8996_AIF2RX_CHAN1_SLOTS_SHIFT | 1);

	regmap_update_bits(wm8996->regmap,
			   WM8996_AIF1TX_CHANNEL_0_CONFIGURATION,
			   WM8996_AIF1TX_CHAN0_SLOTS_MASK |
			   WM8996_AIF1TX_CHAN0_START_SLOT_MASK,
			   1 << WM8996_AIF1TX_CHAN0_SLOTS_SHIFT | 0);
	regmap_update_bits(wm8996->regmap,
			   WM8996_AIF1TX_CHANNEL_1_CONFIGURATION,
			   WM8996_AIF1TX_CHAN1_SLOTS_MASK |
			   WM8996_AIF1TX_CHAN0_START_SLOT_MASK,
			   1 << WM8996_AIF1TX_CHAN1_SLOTS_SHIFT | 1);
	regmap_update_bits(wm8996->regmap,
			   WM8996_AIF1TX_CHANNEL_2_CONFIGURATION,
			   WM8996_AIF1TX_CHAN2_SLOTS_MASK |
			   WM8996_AIF1TX_CHAN0_START_SLOT_MASK,
			   1 << WM8996_AIF1TX_CHAN2_SLOTS_SHIFT | 0);
	regmap_update_bits(wm8996->regmap,
			   WM8996_AIF1TX_CHANNEL_3_CONFIGURATION,
			   WM8996_AIF1TX_CHAN3_SLOTS_MASK |
			   WM8996_AIF1TX_CHAN0_START_SLOT_MASK,
			   1 << WM8996_AIF1TX_CHAN3_SLOTS_SHIFT | 1);
	regmap_update_bits(wm8996->regmap,
			   WM8996_AIF1TX_CHANNEL_4_CONFIGURATION,
			   WM8996_AIF1TX_CHAN4_SLOTS_MASK |
			   WM8996_AIF1TX_CHAN0_START_SLOT_MASK,
			   1 << WM8996_AIF1TX_CHAN4_SLOTS_SHIFT | 0);
	regmap_update_bits(wm8996->regmap,
			   WM8996_AIF1TX_CHANNEL_5_CONFIGURATION,
			   WM8996_AIF1TX_CHAN5_SLOTS_MASK |
			   WM8996_AIF1TX_CHAN0_START_SLOT_MASK,
			   1 << WM8996_AIF1TX_CHAN5_SLOTS_SHIFT | 1);

	regmap_update_bits(wm8996->regmap,
			   WM8996_AIF2TX_CHANNEL_0_CONFIGURATION,
			   WM8996_AIF2TX_CHAN0_SLOTS_MASK |
			   WM8996_AIF2TX_CHAN0_START_SLOT_MASK,
			   1 << WM8996_AIF2TX_CHAN0_SLOTS_SHIFT | 0);
	regmap_update_bits(wm8996->regmap,
			   WM8996_AIF1TX_CHANNEL_1_CONFIGURATION,
			   WM8996_AIF2TX_CHAN1_SLOTS_MASK |
			   WM8996_AIF2TX_CHAN1_START_SLOT_MASK,
			   1 << WM8996_AIF1TX_CHAN1_SLOTS_SHIFT | 1);

	/* If the TX LRCLK pins are not in LRCLK mode configure the
	 * AIFs to source their clocks from the RX LRCLKs.
	 */
	ret = regmap_read(wm8996->regmap, WM8996_GPIO_1, &reg);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to read GPIO1: %d\n", ret);
		goto err_regmap;
	}

	if (reg & WM8996_GP1_FN_MASK)
		regmap_update_bits(wm8996->regmap, WM8996_AIF1_TX_LRCLK_2,
				   WM8996_AIF1TX_LRCLK_MODE,
				   WM8996_AIF1TX_LRCLK_MODE);

	ret = regmap_read(wm8996->regmap, WM8996_GPIO_2, &reg);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to read GPIO2: %d\n", ret);
		goto err_regmap;
	}

	if (reg & WM8996_GP2_FN_MASK)
		regmap_update_bits(wm8996->regmap, WM8996_AIF2_TX_LRCLK_2,
				   WM8996_AIF2TX_LRCLK_MODE,
				   WM8996_AIF2TX_LRCLK_MODE);

	wm8996_init_gpio(wm8996);

	ret = snd_soc_register_codec(&i2c->dev,
				     &soc_codec_dev_wm8996, wm8996_dai,
				     ARRAY_SIZE(wm8996_dai));
	if (ret < 0)
		goto err_gpiolib;

	return ret;

err_gpiolib:
	wm8996_free_gpio(wm8996);
err_regmap:
err_enable:
	if (wm8996->pdata.ldo_ena > 0)
		gpio_set_value_cansleep(wm8996->pdata.ldo_ena, 0);
	regulator_bulk_disable(ARRAY_SIZE(wm8996->supplies), wm8996->supplies);
err_gpio:
	if (wm8996->pdata.ldo_ena > 0)
		gpio_free(wm8996->pdata.ldo_ena);
err:

	return ret;
}

static int wm8996_i2c_remove(struct i2c_client *client)
{
	struct wm8996_priv *wm8996 = i2c_get_clientdata(client);
	int i;

	snd_soc_unregister_codec(&client->dev);
	wm8996_free_gpio(wm8996);
	if (wm8996->pdata.ldo_ena > 0) {
		gpio_set_value_cansleep(wm8996->pdata.ldo_ena, 0);
		gpio_free(wm8996->pdata.ldo_ena);
	}
	for (i = 0; i < ARRAY_SIZE(wm8996->supplies); i++)
		regulator_unregister_notifier(wm8996->supplies[i].consumer,
					      &wm8996->disable_nb[i]);

	return 0;
}

static const struct i2c_device_id wm8996_i2c_id[] = {
	{ "wm8996", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8996_i2c_id);

static struct i2c_driver wm8996_i2c_driver = {
	.driver = {
		.name = "wm8996",
		.owner = THIS_MODULE,
	},
	.probe =    wm8996_i2c_probe,
	.remove =   wm8996_i2c_remove,
	.id_table = wm8996_i2c_id,
};

module_i2c_driver(wm8996_i2c_driver);

MODULE_DESCRIPTION("ASoC WM8996 driver");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
