/*
 *  t0_wm1811.c
 *
 *  Copyright (c) 2011 Samsung Electronics Co. Ltd
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/input.h>
#include <linux/wakelock.h>
#include <linux/suspend.h>
#include <linux/irq.h>
#include <linux/interrupt.h>

#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/jack.h>

#include <mach/regs-clock.h>
#include <mach/pmu.h>
#include <mach/midas-sound.h>

#include <linux/mfd/wm8994/core.h>
#include <linux/mfd/wm8994/registers.h>
#include <linux/mfd/wm8994/pdata.h>
#include <linux/mfd/wm8994/gpio.h>
#include <linux/exynos_audio.h>

#if defined(CONFIG_SND_USE_MUIC_SWITCH)
#include <linux/mfd/max77693-private.h>
#endif


#include "i2s.h"
#include "s3c-i2s-v2.h"
#include "../codecs/wm8994.h"


#define MIDAS_DEFAULT_MCLK1	24000000
#define MIDAS_DEFAULT_MCLK2	32768
#define MIDAS_DEFAULT_SYNC_CLK	11289600

#define WM1811_JACKDET_MODE_NONE  0x0000
#define WM1811_JACKDET_MODE_JACK  0x0100
#define WM1811_JACKDET_MODE_MIC   0x0080
#define WM1811_JACKDET_MODE_AUDIO 0x0180

#define WM1811_JACKDET_BTN0	0x04
#define WM1811_JACKDET_BTN1	0x10
#define WM1811_JACKDET_BTN2	0x08

#define WM1811_MIC_IRQ_NUM	(IRQ_BOARD_CODEC_START + WM8994_IRQ_MIC1_DET)
#define WM1811_JACKDET_IRQ_NUM	(IRQ_BOARD_CODEC_START + WM8994_IRQ_GPIO(6))

#define MIC_DISABLE	0
#define MIC_ENABLE	1
#define MIC_FORCE_DISABLE	2
#define MIC_FORCE_ENABLE	3

static struct wm8958_micd_rate t0_det_rates[] = {
	{ MIDAS_DEFAULT_MCLK2,     true,  0,  0 },
	{ MIDAS_DEFAULT_MCLK2,    false,  0,  0 },
	{ MIDAS_DEFAULT_SYNC_CLK,  true,  7,  7 },
	{ MIDAS_DEFAULT_SYNC_CLK, false,  7,  7 },
};

static struct wm8958_micd_rate t0_jackdet_rates[] = {
	{ MIDAS_DEFAULT_MCLK2,     true,  0,  0 },
	{ MIDAS_DEFAULT_MCLK2,    false,  0,  0 },
	{ MIDAS_DEFAULT_SYNC_CLK,  true, 12, 12 },
	{ MIDAS_DEFAULT_SYNC_CLK, false,  7,  8 },
};

static int aif2_mode;
const char *aif2_mode_text[] = {
	"Slave", "Master"
};

const char *switch_mode_text[] = {
	"Off", "On"
};

const char *mic_bias_mode_text[] = {
	"Disable", "Force Disable", "Enable", "Force Enable"
};

static int input_clamp;
static int lineout_mode;
static int aif2_digital_mute;
static int main_mic_bias_mode;
static int sub_mic_bias_mode;

#ifndef CONFIG_SEC_DEV_JACK
/* To support PBA function test */
static struct class *jack_class;
static struct device *jack_dev;
#endif

struct wm1811_machine_priv {
	struct snd_soc_jack jack;
	struct snd_soc_codec *codec;
	struct wake_lock jackdet_wake_lock;
	void (*lineout_switch_f) (int on);
	void (*set_main_mic_f) (int on);
	void (*set_sub_mic_f) (int on);
	int (*get_g_det_value_f) (void);
	int (*get_g_det_irq_num_f) (void);
};

static const struct soc_enum switch_mode_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(switch_mode_text), switch_mode_text),
};

static const struct soc_enum aif2_mode_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(aif2_mode_text), aif2_mode_text),
};

static const struct soc_enum mic_bias_mode_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(mic_bias_mode_text), mic_bias_mode_text),
};

static const struct soc_enum sub_bias_mode_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(mic_bias_mode_text), mic_bias_mode_text),
};

static int get_lineout_mode(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = lineout_mode;
	return 0;
}

static int set_lineout_mode(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wm1811_machine_priv *wm1811
		= snd_soc_card_get_drvdata(codec->card);

	lineout_mode = ucontrol->value.integer.value[0];

	if (lineout_mode) {
		wm8994_vmid_mode(codec, WM8994_VMID_FORCE);
		if (wm1811->lineout_switch_f)
			wm1811->lineout_switch_f(1);
	} else {
		if (wm1811->lineout_switch_f)
			wm1811->lineout_switch_f(0);
		wm8994_vmid_mode(codec, WM8994_VMID_NORMAL);
	}

	dev_info(codec->dev, "set lineout mode : %s\n",
		switch_mode_text[lineout_mode]);
	return 0;

}

static int get_aif2_mode(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = aif2_mode;
	return 0;
}

static int set_aif2_mode(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	if (aif2_mode == ucontrol->value.integer.value[0])
		return 0;

	aif2_mode = ucontrol->value.integer.value[0];

	pr_info("set aif2 mode : %s\n", aif2_mode_text[aif2_mode]);

	return 0;
}

static int get_input_clamp(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = input_clamp;
	return 0;
}

static int set_input_clamp(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);

	input_clamp = ucontrol->value.integer.value[0];

	if (input_clamp) {
		snd_soc_update_bits(codec, WM8994_INPUT_MIXER_1,
				WM8994_INPUTS_CLAMP, WM8994_INPUTS_CLAMP);
		msleep(100);
	} else {
		snd_soc_update_bits(codec, WM8994_INPUT_MIXER_1,
				WM8994_INPUTS_CLAMP, 0);
	}
	pr_info("set fm input_clamp : %s\n", switch_mode_text[input_clamp]);

	return 0;
}

static int get_aif2_mute_status(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = aif2_digital_mute;
	return 0;
}

static int set_aif2_mute_status(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	int reg;

	aif2_digital_mute = ucontrol->value.integer.value[0];

	if (snd_soc_read(codec, WM8994_POWER_MANAGEMENT_6)
		& WM8994_AIF2_DACDAT_SRC)
		aif2_digital_mute = 0;

	if (aif2_digital_mute)
		reg = WM8994_AIF1DAC1_MUTE;
	else
		reg = 0;

	snd_soc_update_bits(codec, WM8994_AIF2_DAC_FILTERS_1,
				WM8994_AIF1DAC1_MUTE, reg);

	pr_info("set aif2_digital_mute : %s\n",
			switch_mode_text[aif2_digital_mute]);

	return 0;
}

static int get_sub_mic_bias_mode(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = sub_mic_bias_mode;
	return 0;
}

static int set_sub_mic_bias_mode(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wm1811_machine_priv *wm1811
		= snd_soc_card_get_drvdata(codec->card);
	int status = 0;

	status = ucontrol->value.integer.value[0];

	switch (status) {
	case MIC_FORCE_ENABLE:
		sub_mic_bias_mode = status;
		snd_soc_update_bits(codec, WM8994_POWER_MANAGEMENT_1,
				WM8994_MICB1_ENA, WM8994_MICB1_ENA);

		if (wm1811->set_sub_mic_f)
			wm1811->set_sub_mic_f(1);
		break;
	case MIC_ENABLE:
		snd_soc_update_bits(codec, WM8994_POWER_MANAGEMENT_1,
				WM8994_MICB1_ENA, WM8994_MICB1_ENA);
		if (wm1811->set_sub_mic_f)
			wm1811->set_sub_mic_f(1);
		if (sub_mic_bias_mode != MIC_FORCE_ENABLE)
			msleep(100);
		break;
	case MIC_FORCE_DISABLE:
		sub_mic_bias_mode = status;
		snd_soc_update_bits(codec, WM8994_POWER_MANAGEMENT_1,
				WM8994_MICB1_ENA, 0);

		if (wm1811->set_sub_mic_f)
			wm1811->set_sub_mic_f(0);
		break;
	case MIC_DISABLE:
		if (sub_mic_bias_mode != MIC_FORCE_ENABLE) {
			snd_soc_update_bits(codec, WM8994_POWER_MANAGEMENT_1,
					WM8994_MICB1_ENA, 0);
			if (wm1811->set_sub_mic_f)
				wm1811->set_sub_mic_f(0);
		} else
			dev_info(codec->dev,
				"SKIP submic disable=%d\n", status);
		break;
	default:
		break;
	}

	dev_info(codec->dev, "sub_mic_bias_mod=%d: status=%d\n",
				sub_mic_bias_mode, status);

	return 0;

}

static int get_main_mic_bias_mode(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = main_mic_bias_mode;
	return 0;
}

static int set_main_mic_bias_mode(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct wm1811_machine_priv *wm1811
		= snd_soc_card_get_drvdata(codec->card);
	int status = 0;

	status = ucontrol->value.integer.value[0];

	switch (status) {
	case MIC_FORCE_ENABLE:
		main_mic_bias_mode = status;

		if (wm1811->set_main_mic_f)
			wm1811->set_main_mic_f(1);
		break;
	case MIC_ENABLE:
		if (wm1811->set_main_mic_f)
			wm1811->set_main_mic_f(1);
		if (main_mic_bias_mode != MIC_FORCE_ENABLE)
			msleep(100);
		break;
	case MIC_FORCE_DISABLE:
		main_mic_bias_mode = status;

		if (wm1811->set_main_mic_f)
			wm1811->set_main_mic_f(0);
		break;
	case MIC_DISABLE:
		if (main_mic_bias_mode != MIC_FORCE_ENABLE) {
			if (wm1811->set_main_mic_f)
				wm1811->set_main_mic_f(0);
		} else
			dev_info(codec->dev,
				"SKIP mainmic disable=%d\n", status);
		break;
	default:
		break;
	}

	dev_info(codec->dev, "main_mic_bias_mod=%d: status=%d\n",
				main_mic_bias_mode, status);

	return 0;

}

static int set_ext_micbias(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	struct wm1811_machine_priv *wm1811
		= snd_soc_card_get_drvdata(codec->card);

	dev_info(codec->dev, "%s event is %02X", w->name, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (wm1811->set_main_mic_f)
			wm1811->set_main_mic_f(1);
		break;
	case SND_SOC_DAPM_POST_PMD:
		if (wm1811->set_main_mic_f)
			wm1811->set_main_mic_f(0);
		break;
	}

	return 0;
}

static int set_ext_submicbias(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	struct wm1811_machine_priv *wm1811
		= snd_soc_card_get_drvdata(codec->card);

	dev_info(codec->dev, "%s event is %02X", w->name, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (wm1811->set_sub_mic_f)
			wm1811->set_sub_mic_f(1);
		break;
	case SND_SOC_DAPM_POST_PMD:
		if (wm1811->set_sub_mic_f)
			wm1811->set_sub_mic_f(0);
		break;
	}

	return 0;
}

static int set_muic_switch(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;

	dev_info(codec->dev, "%s event is %02X", w->name, event);

#if defined(CONFIG_SND_USE_MUIC_SWITCH)
	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		msleep(150);
		max77693_muic_set_audio_switch(1);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		max77693_muic_set_audio_switch(0);
		break;
	}
#endif

	return 0;
}

static void t0_micd_set_rate(struct snd_soc_codec *codec)
{
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);
	int best, i, sysclk, val;
	bool idle;
	const struct wm8958_micd_rate *rates = NULL;
	int num_rates = 0;

	idle = !wm8994->jack_mic;

	sysclk = snd_soc_read(codec, WM8994_CLOCKING_1);
	if (sysclk & WM8994_SYSCLK_SRC)
		sysclk = wm8994->aifclk[1];
	else
		sysclk = wm8994->aifclk[0];

	if (wm8994->jackdet) {
		rates = t0_jackdet_rates;
		num_rates = ARRAY_SIZE(t0_jackdet_rates);
		wm8994->pdata->micd_rates = t0_jackdet_rates;
		wm8994->pdata->num_micd_rates = num_rates;
	} else {
		rates = t0_det_rates;
		num_rates = ARRAY_SIZE(t0_det_rates);
		wm8994->pdata->micd_rates = t0_det_rates;
		wm8994->pdata->num_micd_rates = num_rates;
	}

	best = 0;
	for (i = 0; i < num_rates; i++) {
		if (rates[i].idle != idle)
			continue;
		if (abs(rates[i].sysclk - sysclk) <
		    abs(rates[best].sysclk - sysclk))
			best = i;
		else if (rates[best].idle != idle)
			best = i;
	}

	val = rates[best].start << WM8958_MICD_BIAS_STARTTIME_SHIFT
		| rates[best].rate << WM8958_MICD_RATE_SHIFT;

	snd_soc_update_bits(codec, WM8958_MIC_DETECT_1,
			    WM8958_MICD_BIAS_STARTTIME_MASK |
			    WM8958_MICD_RATE_MASK, val);
}

static void t0_micdet(u16 status, void *data)
{
	struct wm1811_machine_priv *wm1811 = data;
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(wm1811->codec);
	int report;
	int reg;
	bool present;

	pr_info("%s: detected jack or button\n", __func__);

	/*
	 * If the jack is inserted abnormally,
	 * The variable puts back to its previous status.
	 */
	if ((wm1811->get_g_det_value_f) && wm1811->get_g_det_value_f()) {
		dev_info(wm1811->codec->dev, "The jack is inserted abnormally\n");
		wm8994->mic_detecting = false;
	}

	wake_lock_timeout(&wm1811->jackdet_wake_lock, 5 * HZ);

	/* Either nothing present or just starting detection */
	if (!(status & WM8958_MICD_STS)) {
		if (!wm8994->jackdet) {
			/* If nothing present then clear our statuses */
			dev_dbg(wm1811->codec->dev, "Detected open circuit\n");
			wm8994->jack_mic = false;
			wm8994->mic_detecting = true;

			t0_micd_set_rate(wm1811->codec);

			snd_soc_jack_report(wm8994->micdet[0].jack, 0,
					    wm8994->btn_mask |
					     SND_JACK_HEADSET);
		}
		/*ToDo*/
		/*return;*/
	}

	/* If the measurement is showing a high impedence we've got a
	 * microphone.
	 */
	if (wm8994->mic_detecting && (status & 0x400)) {
		dev_info(wm1811->codec->dev, "Detected microphone\n");

		wm8994->mic_detecting = false;
		wm8994->jack_mic = true;

		t0_micd_set_rate(wm1811->codec);

		snd_soc_jack_report(wm8994->micdet[0].jack, SND_JACK_HEADSET,
				    SND_JACK_HEADSET);
	}

	if (wm8994->mic_detecting && status & 0x4) {
		dev_info(wm1811->codec->dev, "Detected headphone\n");
		wm8994->mic_detecting = false;

		t0_micd_set_rate(wm1811->codec);

		snd_soc_jack_report(wm8994->micdet[0].jack, SND_JACK_HEADPHONE,
				    SND_JACK_HEADSET);

		/* If we have jackdet that will detect removal */
		if (wm8994->jackdet) {
			mutex_lock(&wm8994->accdet_lock);

			snd_soc_update_bits(wm1811->codec, WM8958_MIC_DETECT_1,
					    WM8958_MICD_ENA, 0);

			if (wm8994->active_refcount) {
				snd_soc_update_bits(wm1811->codec,
					WM8994_ANTIPOP_2,
					WM1811_JACKDET_MODE_MASK,
					WM1811_JACKDET_MODE_AUDIO);
			}

			mutex_unlock(&wm8994->accdet_lock);

			if (wm8994->pdata->jd_ext_cap) {
				mutex_lock(&wm1811->codec->mutex);
				snd_soc_dapm_disable_pin(&wm1811->codec->dapm,
							 "MICBIAS2");
				snd_soc_dapm_sync(&wm1811->codec->dapm);
				mutex_unlock(&wm1811->codec->mutex);
			}
		}
	}

	/* Report short circuit as a button */
	if (wm8994->jack_mic) {
		report = 0;
		if (status & WM1811_JACKDET_BTN0)
			report |= SND_JACK_BTN_0;

		if (status & WM1811_JACKDET_BTN1)
			report |= SND_JACK_BTN_1;

		if (status & WM1811_JACKDET_BTN2)
			report |= SND_JACK_BTN_2;

		reg = snd_soc_read(wm1811->codec, WM1811_JACKDET_CTRL);
		if (reg < 0) {
			pr_err("%s: Failed to read jack status: %d\n",
				__func__, reg);
			return;
		}

		pr_err("%s: JACKDET %x\n", __func__, reg);

		present = reg & WM1811_JACKDET_LVL;

		if (!present) {
			pr_err("%s: button is ignored!!!\n", __func__);
			return;
		}

		dev_info(wm1811->codec->dev, "Detected Button: %08x (%08X)\n",
			report, status);

		snd_soc_jack_report(wm8994->micdet[0].jack, report,
				    wm8994->btn_mask);
	}
}

static int t0_wm1811_aif1_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	unsigned int pll_out;
	int ret;

	dev_info(codec_dai->dev, "%s ++\n", __func__);
	/* AIF1CLK should be >=3MHz for optimal performance */
	if (params_rate(params) == 8000 || params_rate(params) == 11025)
		pll_out = params_rate(params) * 512;
	else
		pll_out = params_rate(params) * 256;

	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S
					| SND_SOC_DAIFMT_NB_NF
					| SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0)
		return ret;

	/* Set the cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S
					| SND_SOC_DAIFMT_NB_NF
					| SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0)
		return ret;

	/* Switch the FLL */
	ret = snd_soc_dai_set_pll(codec_dai, WM8994_FLL1,
				  WM8994_FLL_SRC_MCLK1, MIDAS_DEFAULT_MCLK1,
				  pll_out);
	if (ret < 0)
		dev_err(codec_dai->dev, "Unable to start FLL1: %d\n", ret);

	ret = snd_soc_dai_set_sysclk(codec_dai, WM8994_SYSCLK_FLL1,
				     pll_out, SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(codec_dai->dev, "Unable to switch to FLL1: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(cpu_dai, SAMSUNG_I2S_OPCLK,
				     0, MOD_OPCLK_PCLK);
	if (ret < 0)
		return ret;

	dev_info(codec_dai->dev, "%s --\n", __func__);

	return 0;
}

/*
 * T0 WM1811 DAI operations.
 */
static struct snd_soc_ops t0_wm1811_aif1_ops = {
	.hw_params = t0_wm1811_aif1_hw_params,
};

static int t0_wm1811_aif2_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret;
	int prate;
	int bclk;

	dev_info(codec_dai->dev, "%s ++\n", __func__);
	prate = params_rate(params);
	switch (params_rate(params)) {
	case 8000:
	case 16000:
	       break;
	default:
		dev_warn(codec_dai->dev, "Unsupported LRCLK %d, falling back to 8000Hz\n",
				(int)params_rate(params));
		prate = 8000;
	}

	/* Set the codec DAI configuration, aif2_mode:0 is slave */
	if (aif2_mode == 0)
		ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S
					| SND_SOC_DAIFMT_NB_NF
					| SND_SOC_DAIFMT_CBS_CFS);
	else
		ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S
					| SND_SOC_DAIFMT_NB_NF
					| SND_SOC_DAIFMT_CBM_CFM);

	if (ret < 0)
		return ret;

	switch (prate) {
	case 8000:
		bclk = 256000;
		break;
	case 16000:
		bclk = 512000;
		break;
	default:
		return -EINVAL;
	}

	if (aif2_mode == 0) {
		ret = snd_soc_dai_set_pll(codec_dai, WM8994_FLL2,
					WM8994_FLL_SRC_BCLK,
					bclk, prate * 256);
	} else {
		ret = snd_soc_dai_set_pll(codec_dai, WM8994_FLL2,
					  WM8994_FLL_SRC_MCLK1,
					  MIDAS_DEFAULT_MCLK1, prate * 256);
	}

	if (ret < 0)
		dev_err(codec_dai->dev, "Unable to configure FLL2: %d\n", ret);

	ret = snd_soc_dai_set_sysclk(codec_dai, WM8994_SYSCLK_FLL2,
				     prate * 256, SND_SOC_CLOCK_IN);
	if (ret < 0)
		dev_err(codec_dai->dev, "Unable to switch to FLL2: %d\n", ret);

	if (!(snd_soc_read(codec, WM8994_INTERRUPT_RAW_STATUS_2)
		& WM8994_FLL2_LOCK_STS)) {
		dev_info(codec_dai->dev, "%s: use mclk1 for FLL2\n", __func__);
		ret = snd_soc_dai_set_pll(codec_dai, WM8994_FLL2,
			WM8994_FLL_SRC_MCLK1,
			MIDAS_DEFAULT_MCLK1, prate * 256);
	}

	dev_info(codec_dai->dev, "%s --\n", __func__);
	return 0;
}

static struct snd_soc_ops t0_wm1811_aif2_ops = {
	.hw_params = t0_wm1811_aif2_hw_params,
};

static int t0_wm1811_aif3_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	pr_err("%s: enter\n", __func__);
	return 0;
}

static bool playback_stream_status;
static bool capture_stream_status;

static int t0_wm1811_aif3_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);
	struct wm8994_pdata *pdata = wm8994->pdata;
	int base = WM8994_GPIO_8 - WM8994_GPIO_1;
	int i;

	pr_err("%s: enter\n", __func__);

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		capture_stream_status = 1;
	else
		playback_stream_status = 1;

	for (i = 0; i < 4; i++) {
		if (pdata->gpio_defaults[base + i]) {
			snd_soc_update_bits(wm8994->codec, WM8994_GPIO_8 + i,
						0xffff,
						pdata->gpio_defaults[base + i]);
		}
	}
	return 0;
}

static void t0_wm1811_aif3_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);
	int i;

	pr_err("%s: enter, stream=%d\n", __func__, substream->stream);

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		capture_stream_status = 0;
	else
		playback_stream_status = 0;

	if (playback_stream_status || capture_stream_status)
		return;

	pr_info("%s: set input gpios for AIF3\n", __func__);

	for (i = 0; i < 4; i++) {
		snd_soc_update_bits(wm8994->codec, WM8994_GPIO_8 + i,
						0xffff,
						0xA101);
	}
	return;
}

static struct snd_soc_ops t0_wm1811_aif3_ops = {
	.startup = t0_wm1811_aif3_startup,
	.shutdown = t0_wm1811_aif3_shutdown,
	.hw_params = t0_wm1811_aif3_hw_params,
};

static const struct snd_kcontrol_new t0_controls[] = {
	SOC_DAPM_PIN_SWITCH("HP"),
	SOC_DAPM_PIN_SWITCH("SPK"),
	SOC_DAPM_PIN_SWITCH("RCV"),
	SOC_DAPM_PIN_SWITCH("FM In"),
	SOC_DAPM_PIN_SWITCH("LINE"),
	SOC_DAPM_PIN_SWITCH("HDMI"),
	SOC_DAPM_PIN_SWITCH("Main Mic"),
	SOC_DAPM_PIN_SWITCH("Sub Mic"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),

	SOC_ENUM_EXT("AIF2 Mode", aif2_mode_enum[0],
		get_aif2_mode, set_aif2_mode),

	SOC_ENUM_EXT("Input Clamp", switch_mode_enum[0],
		get_input_clamp, set_input_clamp),

	SOC_ENUM_EXT("LineoutSwitch Mode", switch_mode_enum[0],
		get_lineout_mode, set_lineout_mode),

	SOC_ENUM_EXT("MainMicBias Mode", mic_bias_mode_enum[0],
		get_main_mic_bias_mode, set_main_mic_bias_mode),

	SOC_ENUM_EXT("SubMicBias Mode", mic_bias_mode_enum[0],
		get_sub_mic_bias_mode, set_sub_mic_bias_mode),

	SOC_ENUM_EXT("AIF2 digital mute", switch_mode_enum[0],
		get_aif2_mute_status, set_aif2_mute_status),
};

const struct snd_soc_dapm_widget t0_dapm_widgets[] = {
	SND_SOC_DAPM_HP("HP", NULL),
	SND_SOC_DAPM_SPK("SPK", NULL),
	SND_SOC_DAPM_SPK("RCV", NULL),
	SND_SOC_DAPM_LINE("LINE", set_muic_switch),
	SND_SOC_DAPM_LINE("HDMI", NULL),

	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Main Mic", NULL),
	SND_SOC_DAPM_MIC("Sub Mic", NULL),
	SND_SOC_DAPM_LINE("FM In", NULL),

	SND_SOC_DAPM_INPUT("S5P RP"),
};

const struct snd_soc_dapm_route t0_dapm_routes[] = {
	{ "HP", NULL, "HPOUT1L" },
	{ "HP", NULL, "HPOUT1R" },

	{ "SPK", NULL, "SPKOUTLN" },
	{ "SPK", NULL, "SPKOUTLP" },

	{ "RCV", NULL, "HPOUT2N" },
	{ "RCV", NULL, "HPOUT2P" },

	{ "LINE", NULL, "LINEOUT2N" },
	{ "LINE", NULL, "LINEOUT2P" },

	{ "HDMI", NULL, "LINEOUT1N" },
	{ "HDMI", NULL, "LINEOUT1P" },

	{ "IN2LP:VXRN", NULL, "Main Mic" },
	{ "IN2LN", NULL, "Main Mic" },

	{ "IN1RP", NULL, "Sub Mic" },
	{ "IN1RN", NULL, "Sub Mic" },

	{ "IN1LP", NULL, "MICBIAS2" },
	{ "MICBIAS2", NULL, "Headset Mic" },
	{ "IN1LN", NULL, "MICBIAS2" },
	{ "MICBIAS2", NULL, "Headset Mic" },

	{ "AIF1DAC1L", NULL, "S5P RP" },
	{ "AIF1DAC1R", NULL, "S5P RP" },

	{ "IN2RN", NULL, "FM In" },
	{ "IN2RP:VXRP", NULL, "FM In" },
};

static struct snd_soc_dai_driver t0_ext_dai[] = {
	{
		.name = "t0.cp",
		.playback = {
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 16000,
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.capture = {
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 16000,
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
	},
	{
		.name = "t0.bt",
		.playback = {
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 16000,
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.capture = {
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 16000,
			.rates = SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
	},
};

#ifndef CONFIG_SEC_DEV_JACK
static ssize_t earjack_state_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct snd_soc_codec *codec = dev_get_drvdata(dev);
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);

	int report = 0;

	if ((wm8994->micdet[0].jack->status & SND_JACK_HEADPHONE) ||
		(wm8994->micdet[0].jack->status & SND_JACK_HEADSET)) {
		report = 1;
	}

	return sprintf(buf, "%d\n", report);
}

static ssize_t earjack_state_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	pr_info("%s : operate nothing\n", __func__);

	return size;
}

static ssize_t earjack_key_state_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct snd_soc_codec *codec = dev_get_drvdata(dev);
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);

	int report = 0;

	if (wm8994->micdet[0].jack->status & SND_JACK_BTN_0)
		report = 1;

	return sprintf(buf, "%d\n", report);
}

static ssize_t earjack_key_state_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	pr_info("%s : operate nothing\n", __func__);

	return size;
}

static ssize_t earjack_select_jack_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	pr_info("%s : operate nothing\n", __func__);

	return 0;
}

static ssize_t earjack_select_jack_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct snd_soc_codec *codec = dev_get_drvdata(dev);
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);

	wm8994->mic_detecting = false;
	wm8994->jack_mic = true;

	t0_micd_set_rate(codec);

	if ((!size) || (buf[0] != '1')) {
		snd_soc_jack_report(wm8994->micdet[0].jack,
				    0, SND_JACK_HEADSET);
		dev_info(codec->dev, "Forced remove microphone\n");
	} else {

		snd_soc_jack_report(wm8994->micdet[0].jack,
				    SND_JACK_HEADSET, SND_JACK_HEADSET);
		dev_info(codec->dev, "Forced detect microphone\n");
	}

	return size;
}

static ssize_t reselect_jack_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	pr_info("%s : operate nothing\n", __func__);
	return 0;
}

static ssize_t reselect_jack_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	pr_info("%s : operate nothing\n", __func__);
	return size;
}

static DEVICE_ATTR(reselect_jack, S_IRUGO | S_IWUSR | S_IWGRP,
		reselect_jack_show, reselect_jack_store);

static DEVICE_ATTR(select_jack, S_IRUGO | S_IWUSR | S_IWGRP,
		   earjack_select_jack_show, earjack_select_jack_store);

static DEVICE_ATTR(key_state, S_IRUGO | S_IWUSR | S_IWGRP,
		   earjack_key_state_show, earjack_key_state_store);

static DEVICE_ATTR(state, S_IRUGO | S_IWUSR | S_IWGRP,
		   earjack_state_show, earjack_state_store);
#endif

static int t0_wm1811_init_paiftx(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct wm1811_machine_priv *wm1811
		= snd_soc_card_get_drvdata(codec->card);
	struct snd_soc_dai *aif1_dai = rtd->codec_dai;
	struct wm8994 *control = codec->control_data;
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);
	const struct exynos_sound_platform_data *sound_pdata;
	int ret;

	midas_snd_set_mclk(true, false);

	rtd->codec_dai->driver->playback.channels_max =
				rtd->cpu_dai->driver->playback.channels_max;

	ret = snd_soc_add_controls(codec, t0_controls,
					ARRAY_SIZE(t0_controls));

	ret = snd_soc_dapm_new_controls(&codec->dapm, t0_dapm_widgets,
					   ARRAY_SIZE(t0_dapm_widgets));
	if (ret != 0)
		dev_err(codec->dev, "Failed to add DAPM widgets: %d\n", ret);

	ret = snd_soc_dapm_add_routes(&codec->dapm, t0_dapm_routes,
					   ARRAY_SIZE(t0_dapm_routes));
	if (ret != 0)
		dev_err(codec->dev, "Failed to add DAPM routes: %d\n", ret);

	ret = snd_soc_dai_set_sysclk(aif1_dai, WM8994_SYSCLK_MCLK2,
				     MIDAS_DEFAULT_MCLK2, SND_SOC_CLOCK_IN);
	if (ret < 0)
		dev_err(codec->dev, "Failed to boot clocking\n");

	/* Force AIF1CLK on as it will be master for jack detection */
	if (wm8994->revision > 1) {
		ret = snd_soc_dapm_force_enable_pin(&codec->dapm, "AIF1CLK");
		if (ret < 0)
			dev_err(codec->dev, "Failed to enable AIF1CLK: %d\n",
					ret);
	}

	ret = snd_soc_dapm_disable_pin(&codec->dapm, "S5P RP");
	if (ret < 0)
		dev_err(codec->dev, "Failed to disable S5P RP: %d\n", ret);

	snd_soc_dapm_ignore_suspend(&codec->dapm, "RCV");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "SPK");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "HP");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "Headset Mic");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "Sub Mic");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "Main Mic");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "AIF1DACDAT");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "AIF2DACDAT");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "AIF3DACDAT");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "AIF1ADCDAT");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "AIF2ADCDAT");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "AIF3ADCDAT");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "FM In");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "LINE");
	snd_soc_dapm_ignore_suspend(&codec->dapm, "HDMI");

	wm1811->codec = codec;

	t0_micd_set_rate(codec);

#ifdef CONFIG_SEC_DEV_JACK
	/* By default use idle_bias_off, will override for WM8994 */
	codec->dapm.idle_bias_off = 0;
#else /* CONFIG_SEC_DEV_JACK */
	wm1811->jack.status = 0;

	ret = snd_soc_jack_new(codec, "T0 Jack",
				SND_JACK_HEADSET | SND_JACK_BTN_0 |
				SND_JACK_BTN_1 | SND_JACK_BTN_2,
				&wm1811->jack);

	if (ret < 0)
		dev_err(codec->dev, "Failed to create jack: %d\n", ret);

	ret = snd_jack_set_key(wm1811->jack.jack, SND_JACK_BTN_0, KEY_MEDIA);

	if (ret < 0)
		dev_err(codec->dev, "Failed to set KEY_MEDIA: %d\n", ret);

	ret = snd_jack_set_key(wm1811->jack.jack, SND_JACK_BTN_1,
							KEY_VOLUMEDOWN);
	if (ret < 0)
		dev_err(codec->dev, "Failed to set KEY_VOLUMEUP: %d\n", ret);

	ret = snd_jack_set_key(wm1811->jack.jack, SND_JACK_BTN_2,
							KEY_VOLUMEUP);

	if (ret < 0)
		dev_err(codec->dev, "Failed to set KEY_VOLUMEDOWN: %d\n", ret);

	if (wm8994->revision > 1) {
		dev_info(codec->dev, "wm1811: Rev %c support mic detection\n",
			'A' + wm8994->revision);
		ret = wm8958_mic_detect(codec, &wm1811->jack, t0_micdet,
			wm1811);

		if (ret < 0)
			dev_err(codec->dev, "Failed start detection: %d\n",
				ret);
	} else {
		dev_info(codec->dev, "wm1811: Rev %c doesn't support mic detection\n",
			'A' + wm8994->revision);
		codec->dapm.idle_bias_off = 0;
	}
	/* To wakeup for earjack event in suspend mode */
	enable_irq_wake(control->irq);

	wake_lock_init(&wm1811->jackdet_wake_lock,
					WAKE_LOCK_SUSPEND, "T0_jackdet");

	/* To support PBA function test */
	jack_class = class_create(THIS_MODULE, "audio");

	if (IS_ERR(jack_class))
		pr_err("Failed to create class\n");

	jack_dev = device_create(jack_class, NULL, 0, codec, "earjack");

	if (device_create_file(jack_dev, &dev_attr_select_jack) < 0)
		pr_err("Failed to create device file (%s)!\n",
			dev_attr_select_jack.attr.name);

	if (device_create_file(jack_dev, &dev_attr_key_state) < 0)
		pr_err("Failed to create device file (%s)!\n",
			dev_attr_key_state.attr.name);

	if (device_create_file(jack_dev, &dev_attr_state) < 0)
		pr_err("Failed to create device file (%s)!\n",
			dev_attr_state.attr.name);

	if (device_create_file(jack_dev, &dev_attr_reselect_jack) < 0)
		pr_err("Failed to create device file (%s)!\n",
			dev_attr_reselect_jack.attr.name);

#endif /* CONFIG_SEC_DEV_JACK */
	sound_pdata = exynos_sound_get_platform_data();

	if (sound_pdata) {
		wm8994->hubs.dcs_codes_l = sound_pdata->dcs_offset_l;
		wm8994->hubs.dcs_codes_r = sound_pdata->dcs_offset_r;
	}

	return snd_soc_dapm_sync(&codec->dapm);
}

static struct snd_soc_dai_link t0_dai[] = {
	{ /* Sec_Fifo DAI i/f */
		.name = "Sec_FIFO TX",
		.stream_name = "Sec_Dai",
		.cpu_dai_name = "samsung-i2s.4",
		.codec_dai_name = "wm8994-aif1",
#ifndef CONFIG_SND_SOC_SAMSUNG_USE_DMA_WRAPPER
		.platform_name = "samsung-audio-idma",
#else
		.platform_name = "samsung-audio",
#endif
		.codec_name = "wm8994-codec",
		.init = t0_wm1811_init_paiftx,
		.ops = &t0_wm1811_aif1_ops,
	},
	{
		.name = "T0_WM1811 Voice",
		.stream_name = "Voice Tx/Rx",
		.cpu_dai_name = "t0.cp",
		.codec_dai_name = "wm8994-aif2",
		.platform_name = "snd-soc-dummy",
		.codec_name = "wm8994-codec",
		.ops = &t0_wm1811_aif2_ops,
		.ignore_suspend = 1,
	},
	{
		.name = "T0_WM1811 BT",
		.stream_name = "BT Tx/Rx",
		.cpu_dai_name = "t0.bt",
		.codec_dai_name = "wm8994-aif3",
		.platform_name = "snd-soc-dummy",
		.codec_name = "wm8994-codec",
		.ops = &t0_wm1811_aif3_ops,
		.ignore_suspend = 1,
	},
	{ /* Primary DAI i/f */
		.name = "WM8994 AIF1",
		.stream_name = "Pri_Dai",
		.cpu_dai_name = "samsung-i2s.0",
		.codec_dai_name = "wm8994-aif1",
		.platform_name = "samsung-audio",
		.codec_name = "wm8994-codec",
		.ops = &t0_wm1811_aif1_ops,
	},
};

static int t0_card_suspend_pre(struct snd_soc_card *card)
{
	struct snd_soc_codec *codec = card->rtd->codec;
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);

#ifdef CONFIG_SND_USE_LINEOUT_SWITCH
	struct wm1811_machine_priv *wm1811
		= snd_soc_card_get_drvdata(codec->card);
	if (lineout_mode == 1 &&
		wm8994->vmid_mode == WM8994_VMID_FORCE) {
		dev_info(codec->dev,
			"%s: entering force vmid mode\n", __func__);
		if (wm1811->lineout_switch_f)
			wm1811->lineout_switch_f(0);
		wm8994_vmid_mode(codec, WM8994_VMID_NORMAL);
	}
#endif
#ifdef CONFIG_SEC_DEV_JACK
	snd_soc_dapm_disable_pin(&codec->dapm, "AIF1CLK");
#endif

	return 0;
}

static int t0_card_suspend_post(struct snd_soc_card *card)
{
	struct snd_soc_codec *codec = card->rtd->codec;
	struct snd_soc_dai *aif1_dai = card->rtd[0].codec_dai;
	struct snd_soc_dai *aif2_dai = card->rtd[1].codec_dai;
	int ret;

	if (!codec->active) {
		ret = snd_soc_dai_set_sysclk(aif2_dai,
					     WM8994_SYSCLK_MCLK2,
					     MIDAS_DEFAULT_MCLK2,
					     SND_SOC_CLOCK_IN);

		if (ret < 0)
			dev_err(codec->dev, "Unable to switch to MCLK2: %d\n",
				ret);

		ret = snd_soc_dai_set_pll(aif2_dai, WM8994_FLL2, 0, 0, 0);

		if (ret < 0)
			dev_err(codec->dev, "Unable to stop FLL2\n");

		ret = snd_soc_dai_set_sysclk(aif1_dai,
					     WM8994_SYSCLK_MCLK2,
					     MIDAS_DEFAULT_MCLK2,
					     SND_SOC_CLOCK_IN);
		if (ret < 0)
			dev_err(codec->dev, "Unable to switch to MCLK2\n");

		ret = snd_soc_dai_set_pll(aif1_dai, WM8994_FLL1, 0, 0, 0);

		if (ret < 0)
			dev_err(codec->dev, "Unable to stop FLL1\n");

		midas_snd_set_mclk(false, true);
	}

#ifdef CONFIG_ARCH_EXYNOS5
	exynos5_sys_powerdown_xxti_control(midas_snd_get_mclk() ? 1 : 0);
#else /* for CONFIG_ARCH_EXYNOS5 */
	exynos4_sys_powerdown_xusbxti_control(midas_snd_get_mclk() ? 1 : 0);
#endif

	return 0;
}

static int t0_card_resume_pre(struct snd_soc_card *card)
{
	struct snd_soc_codec *codec = card->rtd->codec;
	struct snd_soc_dai *aif1_dai = card->rtd[0].codec_dai;
	int ret;

	midas_snd_set_mclk(true, false);

	/* Switch the FLL */
	ret = snd_soc_dai_set_pll(aif1_dai, WM8994_FLL1,
				  WM8994_FLL_SRC_MCLK1,
				  MIDAS_DEFAULT_MCLK1,
				  MIDAS_DEFAULT_SYNC_CLK);

	if (ret < 0)
		dev_err(aif1_dai->dev, "Unable to start FLL1: %d\n", ret);

	/* Then switch AIF1CLK to it */
	ret = snd_soc_dai_set_sysclk(aif1_dai,
				     WM8994_SYSCLK_FLL1,
				     MIDAS_DEFAULT_SYNC_CLK,
				     SND_SOC_CLOCK_IN);

	if (ret < 0)
		dev_err(aif1_dai->dev, "Unable to switch to FLL1: %d\n", ret);

	return 0;
}

static int t0_card_resume_post(struct snd_soc_card *card)
{
	struct snd_soc_codec *codec = card->rtd->codec;
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);
	int reg = 0;

#ifdef CONFIG_SND_USE_LINEOUT_SWITCH
	struct wm1811_machine_priv *wm1811
		= snd_soc_card_get_drvdata(codec->card);

	if (lineout_mode == 1 &&
		wm8994->vmid_mode == WM8994_VMID_NORMAL) {
		dev_info(codec->dev,
			"%s: entering normal vmid mode\n", __func__);
		wm8994_vmid_mode(codec, WM8994_VMID_FORCE);
		if (wm1811->lineout_switch_f)
			wm1811->lineout_switch_f(1);
	}
#endif
	/*  workaround for jack detection
	 * sometimes WM8994_GPIO_1 type changed wrong function type
	 * so if type mismatched, update to IRQ type
	 */
	reg = snd_soc_read(codec, WM8994_GPIO_1);

	if ((reg & WM8994_GPN_FN_MASK) != WM8994_GP_FN_IRQ) {
		dev_err(codec->dev, "%s: GPIO1 type 0x%x\n", __func__, reg);
		snd_soc_write(codec, WM8994_GPIO_1, WM8994_GP_FN_IRQ);
	}

#ifdef CONFIG_SEC_DEV_JACK
	snd_soc_dapm_force_enable_pin(&codec->dapm, "AIF1CLK");
#endif

	return 0;
}

static struct snd_soc_card t0_card = {
	.name = "T0_WM1811",
	.dai_link = t0_dai,

	/* If you want to use sec_fifo device,
	 * changes the num_link = 2 or ARRAY_SIZE(t0_dai). */
	.num_links = ARRAY_SIZE(t0_dai),

	.suspend_pre = t0_card_suspend_pre,
	.suspend_post = t0_card_suspend_post,
	.resume_pre = t0_card_resume_pre,
	.resume_post = t0_card_resume_post
};

static struct platform_device *t0_snd_device;

static void t0_jackdet_set_mode(struct snd_soc_codec *codec, u16 mode)
{
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(codec);

	if (!wm8994->jackdet || !wm8994->jack_cb)
		return;

	if (wm8994->active_refcount)
		mode = WM1811_JACKDET_MODE_AUDIO;

	if (mode == wm8994->jackdet_mode)
		return;

	wm8994->jackdet_mode = mode;

	/* Always use audio mode to detect while the system is active */
	if (mode != WM1811_JACKDET_MODE_NONE)
		mode = WM1811_JACKDET_MODE_AUDIO;

	snd_soc_update_bits(codec, WM8994_ANTIPOP_2,
			    WM1811_JACKDET_MODE_MASK, mode);
}

static irqreturn_t t0_g_det_thread(int irq, void *data)
{
	struct wm1811_machine_priv *wm1811 = data;
	struct wm8994_priv *wm8994 = snd_soc_codec_get_drvdata(wm1811->codec);
	struct snd_soc_codec *codec = wm8994->codec;


	if (wm1811->get_g_det_value_f()) {

		pr_info("%s: G_DET_N GPIO is High!!!!", __func__);

		mutex_lock(&wm8994->accdet_lock);

		snd_soc_update_bits(codec, WM8958_MICBIAS2,
				    WM8958_MICB2_DISCH, WM8958_MICB2_DISCH);

		/* Enable debounce while removed */
		snd_soc_update_bits(codec, WM1811_JACKDET_CTRL,
				    WM1811_JACKDET_DB, WM1811_JACKDET_DB);

		wm8994->mic_detecting = false;
		wm8994->jack_mic = false;

		snd_soc_update_bits(codec, WM8958_MIC_DETECT_1,
					    WM8958_MICD_ENA, 0);

		t0_jackdet_set_mode(codec, WM1811_JACKDET_MODE_JACK);

		mutex_unlock(&wm8994->accdet_lock);

		mutex_lock(&codec->mutex);

		snd_soc_dapm_disable_pin(&codec->dapm, "MICBIAS2");
		snd_soc_dapm_sync(&codec->dapm);

		mutex_unlock(&codec->mutex);

		snd_soc_jack_report(wm8994->micdet[0].jack, 0,
				    SND_JACK_MECHANICAL | SND_JACK_HEADSET |
				    wm8994->btn_mask);
	} else {
		pr_info("%s: G_DET_N GPIO is Low!!!!", __func__);

		handle_nested_irq(WM1811_JACKDET_IRQ_NUM);
		msleep(100);
		handle_nested_irq(WM1811_MIC_IRQ_NUM);
	}

	return IRQ_HANDLED;

}

static int __init t0_audio_init(void)
{
	struct wm1811_machine_priv *wm1811;
	const struct exynos_sound_platform_data *sound_pdata;
	int ret;

	wm1811 = kzalloc(sizeof *wm1811, GFP_KERNEL);
	if (!wm1811) {
		pr_err("Failed to allocate memory\n");
		ret = -ENOMEM;
		goto err_kzalloc;
	}
	snd_soc_card_set_drvdata(&t0_card, wm1811);

	t0_snd_device = platform_device_alloc("soc-audio", -1);
	if (!t0_snd_device) {
		ret = -ENOMEM;
		goto err_device_alloc;
	}

	ret = snd_soc_register_dais(&t0_snd_device->dev, t0_ext_dai,
						ARRAY_SIZE(t0_ext_dai));
	if (ret != 0)
		pr_err("Failed to register external DAIs: %d\n", ret);

	platform_set_drvdata(t0_snd_device, &t0_card);

	ret = platform_device_add(t0_snd_device);
	if (ret)
		platform_device_put(t0_snd_device);

	sound_pdata = exynos_sound_get_platform_data();
	if (!sound_pdata) {
		pr_info("%s: don't use sound pdata\n", __func__);
		goto err_out_free;
	}

	if (sound_pdata->set_lineout_switch)
		wm1811->lineout_switch_f = sound_pdata->set_lineout_switch;

	if (sound_pdata->set_ext_main_mic)
		wm1811->set_main_mic_f = sound_pdata->set_ext_main_mic;

	if (sound_pdata->set_ext_sub_mic)
		wm1811->set_sub_mic_f = sound_pdata->set_ext_sub_mic;

	if (sound_pdata->get_ground_det_value)
		wm1811->get_g_det_value_f = sound_pdata->get_ground_det_value;

	if (sound_pdata->get_ground_det_irq_num) {
		wm1811->get_g_det_irq_num_f =
				sound_pdata->get_ground_det_irq_num;
		ret = request_threaded_irq(wm1811->get_g_det_irq_num_f(), NULL,
					t0_g_det_thread, IRQF_TRIGGER_RISING |
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					"g_det irq", wm1811);
		if (ret != 0)
			pr_err("%s: Failed to register IRQ\n", __func__);

		/* to handle insert/removal when we're sleeping in a call */
		ret = enable_irq_wake(wm1811->get_g_det_irq_num_f());
		if (ret)
			pr_err("%s : Failed to enable_irq_wake\n", __func__);
	}

	return ret;

err_out_free:
	platform_device_put(t0_snd_device);
err_device_alloc:
	kfree(wm1811);
err_kzalloc:
	return ret;
}
module_init(t0_audio_init);

static void __exit t0_audio_exit(void)
{
	struct snd_soc_card *card = &t0_card;
	struct wm1811_machine_priv *wm1811 = snd_soc_card_get_drvdata(card);
	platform_device_unregister(t0_snd_device);
	kfree(wm1811);
}
module_exit(t0_audio_exit);

MODULE_AUTHOR("Uk Kim <w0806.kim@samsung.com>");
MODULE_DESCRIPTION("ALSA SoC T0 WM1811");
MODULE_LICENSE("GPL");
