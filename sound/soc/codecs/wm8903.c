// SPDX-License-Identifier: GPL-2.0-only
/*
 * wm8903.c  --  WM8903 ALSA SoC Audio driver
 *
 * Copyright 2008-12 Wolfson Microelectronics
 * Copyright 2011-2012 NVIDIA, Inc.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * TODO:
 *  - TDM mode configuration.
 *  - Digital microphone support.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/gpio/driver.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/mutex.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/wm8903.h>
#include <trace/events/asoc.h>

#include "wm8903.h"

/* Register defaults at reset */
static const struct reg_default wm8903_reg_defaults[] = {
	{ 4,  0x0018 },     /* R4   - Bias Control 0 */
	{ 5,  0x0000 },     /* R5   - VMID Control 0 */
	{ 6,  0x0000 },     /* R6   - Mic Bias Control 0 */
	{ 8,  0x0001 },     /* R8   - Analogue DAC 0 */
	{ 10, 0x0001 },     /* R10  - Analogue ADC 0 */
	{ 12, 0x0000 },     /* R12  - Power Management 0 */
	{ 13, 0x0000 },     /* R13  - Power Management 1 */
	{ 14, 0x0000 },     /* R14  - Power Management 2 */
	{ 15, 0x0000 },     /* R15  - Power Management 3 */
	{ 16, 0x0000 },     /* R16  - Power Management 4 */
	{ 17, 0x0000 },     /* R17  - Power Management 5 */
	{ 18, 0x0000 },     /* R18  - Power Management 6 */
	{ 20, 0x0400 },     /* R20  - Clock Rates 0 */
	{ 21, 0x0D07 },     /* R21  - Clock Rates 1 */
	{ 22, 0x0000 },     /* R22  - Clock Rates 2 */
	{ 24, 0x0050 },     /* R24  - Audio Interface 0 */
	{ 25, 0x0242 },     /* R25  - Audio Interface 1 */
	{ 26, 0x0008 },     /* R26  - Audio Interface 2 */
	{ 27, 0x0022 },     /* R27  - Audio Interface 3 */
	{ 30, 0x00C0 },     /* R30  - DAC Digital Volume Left */
	{ 31, 0x00C0 },     /* R31  - DAC Digital Volume Right */
	{ 32, 0x0000 },     /* R32  - DAC Digital 0 */
	{ 33, 0x0000 },     /* R33  - DAC Digital 1 */
	{ 36, 0x00C0 },     /* R36  - ADC Digital Volume Left */
	{ 37, 0x00C0 },     /* R37  - ADC Digital Volume Right */
	{ 38, 0x0000 },     /* R38  - ADC Digital 0 */
	{ 39, 0x0073 },     /* R39  - Digital Microphone 0 */
	{ 40, 0x09BF },     /* R40  - DRC 0 */
	{ 41, 0x3241 },     /* R41  - DRC 1 */
	{ 42, 0x0020 },     /* R42  - DRC 2 */
	{ 43, 0x0000 },     /* R43  - DRC 3 */
	{ 44, 0x0085 },     /* R44  - Analogue Left Input 0 */
	{ 45, 0x0085 },     /* R45  - Analogue Right Input 0 */
	{ 46, 0x0044 },     /* R46  - Analogue Left Input 1 */
	{ 47, 0x0044 },     /* R47  - Analogue Right Input 1 */
	{ 50, 0x0008 },     /* R50  - Analogue Left Mix 0 */
	{ 51, 0x0004 },     /* R51  - Analogue Right Mix 0 */
	{ 52, 0x0000 },     /* R52  - Analogue Spk Mix Left 0 */
	{ 53, 0x0000 },     /* R53  - Analogue Spk Mix Left 1 */
	{ 54, 0x0000 },     /* R54  - Analogue Spk Mix Right 0 */
	{ 55, 0x0000 },     /* R55  - Analogue Spk Mix Right 1 */
	{ 57, 0x002D },     /* R57  - Analogue OUT1 Left */
	{ 58, 0x002D },     /* R58  - Analogue OUT1 Right */
	{ 59, 0x0039 },     /* R59  - Analogue OUT2 Left */
	{ 60, 0x0039 },     /* R60  - Analogue OUT2 Right */
	{ 62, 0x0139 },     /* R62  - Analogue OUT3 Left */
	{ 63, 0x0139 },     /* R63  - Analogue OUT3 Right */
	{ 64, 0x0000 },     /* R65  - Analogue SPK Output Control 0 */
	{ 67, 0x0010 },     /* R67  - DC Servo 0 */
	{ 69, 0x00A4 },     /* R69  - DC Servo 2 */
	{ 90, 0x0000 },     /* R90  - Analogue HP 0 */
	{ 94, 0x0000 },     /* R94  - Analogue Lineout 0 */
	{ 98, 0x0000 },     /* R98  - Charge Pump 0 */
	{ 104, 0x0000 },    /* R104 - Class W 0 */
	{ 108, 0x0000 },    /* R108 - Write Sequencer 0 */
	{ 109, 0x0000 },    /* R109 - Write Sequencer 1 */
	{ 110, 0x0000 },    /* R110 - Write Sequencer 2 */
	{ 111, 0x0000 },    /* R111 - Write Sequencer 3 */
	{ 112, 0x0000 },    /* R112 - Write Sequencer 4 */
	{ 114, 0x0000 },    /* R114 - Control Interface */
	{ 116, 0x00A8 },    /* R116 - GPIO Control 1 */
	{ 117, 0x00A8 },    /* R117 - GPIO Control 2 */
	{ 118, 0x00A8 },    /* R118 - GPIO Control 3 */
	{ 119, 0x0220 },    /* R119 - GPIO Control 4 */
	{ 120, 0x01A0 },    /* R120 - GPIO Control 5 */
	{ 122, 0xFFFF },    /* R122 - Interrupt Status 1 Mask */
	{ 123, 0x0000 },    /* R123 - Interrupt Polarity 1 */
	{ 126, 0x0000 },    /* R126 - Interrupt Control */
	{ 129, 0x0000 },    /* R129 - Control Interface Test 1 */
	{ 149, 0x6810 },    /* R149 - Charge Pump Test 1 */
	{ 164, 0x0028 },    /* R164 - Clock Rate Test 4 */
	{ 172, 0x0000 },    /* R172 - Analogue Output Bias 0 */
};

#define WM8903_NUM_SUPPLIES 4
static const char *wm8903_supply_names[WM8903_NUM_SUPPLIES] = {
	"AVDD",
	"CPVDD",
	"DBVDD",
	"DCVDD",
};

struct wm8903_priv {
	struct wm8903_platform_data *pdata;
	struct device *dev;
	struct regmap *regmap;
	struct regulator_bulk_data supplies[WM8903_NUM_SUPPLIES];

	int sysclk;
	int irq;

	struct mutex lock;
	int fs;
	int deemph;

	int dcs_pending;
	int dcs_cache[4];

	/* Reference count */
	int class_w_users;

	struct snd_soc_jack *mic_jack;
	int mic_det;
	int mic_short;
	int mic_last_report;
	int mic_delay;

#ifdef CONFIG_GPIOLIB
	struct gpio_chip gpio_chip;
#endif
};

static bool wm8903_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WM8903_SW_RESET_AND_ID:
	case WM8903_REVISION_NUMBER:
	case WM8903_BIAS_CONTROL_0:
	case WM8903_VMID_CONTROL_0:
	case WM8903_MIC_BIAS_CONTROL_0:
	case WM8903_ANALOGUE_DAC_0:
	case WM8903_ANALOGUE_ADC_0:
	case WM8903_POWER_MANAGEMENT_0:
	case WM8903_POWER_MANAGEMENT_1:
	case WM8903_POWER_MANAGEMENT_2:
	case WM8903_POWER_MANAGEMENT_3:
	case WM8903_POWER_MANAGEMENT_4:
	case WM8903_POWER_MANAGEMENT_5:
	case WM8903_POWER_MANAGEMENT_6:
	case WM8903_CLOCK_RATES_0:
	case WM8903_CLOCK_RATES_1:
	case WM8903_CLOCK_RATES_2:
	case WM8903_AUDIO_INTERFACE_0:
	case WM8903_AUDIO_INTERFACE_1:
	case WM8903_AUDIO_INTERFACE_2:
	case WM8903_AUDIO_INTERFACE_3:
	case WM8903_DAC_DIGITAL_VOLUME_LEFT:
	case WM8903_DAC_DIGITAL_VOLUME_RIGHT:
	case WM8903_DAC_DIGITAL_0:
	case WM8903_DAC_DIGITAL_1:
	case WM8903_ADC_DIGITAL_VOLUME_LEFT:
	case WM8903_ADC_DIGITAL_VOLUME_RIGHT:
	case WM8903_ADC_DIGITAL_0:
	case WM8903_DIGITAL_MICROPHONE_0:
	case WM8903_DRC_0:
	case WM8903_DRC_1:
	case WM8903_DRC_2:
	case WM8903_DRC_3:
	case WM8903_ANALOGUE_LEFT_INPUT_0:
	case WM8903_ANALOGUE_RIGHT_INPUT_0:
	case WM8903_ANALOGUE_LEFT_INPUT_1:
	case WM8903_ANALOGUE_RIGHT_INPUT_1:
	case WM8903_ANALOGUE_LEFT_MIX_0:
	case WM8903_ANALOGUE_RIGHT_MIX_0:
	case WM8903_ANALOGUE_SPK_MIX_LEFT_0:
	case WM8903_ANALOGUE_SPK_MIX_LEFT_1:
	case WM8903_ANALOGUE_SPK_MIX_RIGHT_0:
	case WM8903_ANALOGUE_SPK_MIX_RIGHT_1:
	case WM8903_ANALOGUE_OUT1_LEFT:
	case WM8903_ANALOGUE_OUT1_RIGHT:
	case WM8903_ANALOGUE_OUT2_LEFT:
	case WM8903_ANALOGUE_OUT2_RIGHT:
	case WM8903_ANALOGUE_OUT3_LEFT:
	case WM8903_ANALOGUE_OUT3_RIGHT:
	case WM8903_ANALOGUE_SPK_OUTPUT_CONTROL_0:
	case WM8903_DC_SERVO_0:
	case WM8903_DC_SERVO_2:
	case WM8903_DC_SERVO_READBACK_1:
	case WM8903_DC_SERVO_READBACK_2:
	case WM8903_DC_SERVO_READBACK_3:
	case WM8903_DC_SERVO_READBACK_4:
	case WM8903_ANALOGUE_HP_0:
	case WM8903_ANALOGUE_LINEOUT_0:
	case WM8903_CHARGE_PUMP_0:
	case WM8903_CLASS_W_0:
	case WM8903_WRITE_SEQUENCER_0:
	case WM8903_WRITE_SEQUENCER_1:
	case WM8903_WRITE_SEQUENCER_2:
	case WM8903_WRITE_SEQUENCER_3:
	case WM8903_WRITE_SEQUENCER_4:
	case WM8903_CONTROL_INTERFACE:
	case WM8903_GPIO_CONTROL_1:
	case WM8903_GPIO_CONTROL_2:
	case WM8903_GPIO_CONTROL_3:
	case WM8903_GPIO_CONTROL_4:
	case WM8903_GPIO_CONTROL_5:
	case WM8903_INTERRUPT_STATUS_1:
	case WM8903_INTERRUPT_STATUS_1_MASK:
	case WM8903_INTERRUPT_POLARITY_1:
	case WM8903_INTERRUPT_CONTROL:
	case WM8903_CLOCK_RATE_TEST_4:
	case WM8903_ANALOGUE_OUTPUT_BIAS_0:
		return true;
	default:
		return false;
	}
}

static bool wm8903_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WM8903_SW_RESET_AND_ID:
	case WM8903_REVISION_NUMBER:
	case WM8903_INTERRUPT_STATUS_1:
	case WM8903_WRITE_SEQUENCER_4:
	case WM8903_DC_SERVO_READBACK_1:
	case WM8903_DC_SERVO_READBACK_2:
	case WM8903_DC_SERVO_READBACK_3:
	case WM8903_DC_SERVO_READBACK_4:
		return true;

	default:
		return false;
	}
}

static int wm8903_cp_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol, int event)
{
	WARN_ON(event != SND_SOC_DAPM_POST_PMU);
	mdelay(4);

	return 0;
}

static int wm8903_dcs_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct wm8903_priv *wm8903 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		wm8903->dcs_pending |= 1 << w->shift;
		break;
	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_component_update_bits(component, WM8903_DC_SERVO_0,
				    1 << w->shift, 0);
		break;
	}

	return 0;
}

#define WM8903_DCS_MODE_WRITE_STOP 0
#define WM8903_DCS_MODE_START_STOP 2

static void wm8903_seq_notifier(struct snd_soc_component *component,
				enum snd_soc_dapm_type event, int subseq)
{
	struct wm8903_priv *wm8903 = snd_soc_component_get_drvdata(component);
	int dcs_mode = WM8903_DCS_MODE_WRITE_STOP;
	int i, val;

	/* Complete any pending DC servo starts */
	if (wm8903->dcs_pending) {
		dev_dbg(component->dev, "Starting DC servo for %x\n",
			wm8903->dcs_pending);

		/* If we've no cached values then we need to do startup */
		for (i = 0; i < ARRAY_SIZE(wm8903->dcs_cache); i++) {
			if (!(wm8903->dcs_pending & (1 << i)))
				continue;

			if (wm8903->dcs_cache[i]) {
				dev_dbg(component->dev,
					"Restore DC servo %d value %x\n",
					3 - i, wm8903->dcs_cache[i]);

				snd_soc_component_write(component, WM8903_DC_SERVO_4 + i,
					      wm8903->dcs_cache[i] & 0xff);
			} else {
				dev_dbg(component->dev,
					"Calibrate DC servo %d\n", 3 - i);
				dcs_mode = WM8903_DCS_MODE_START_STOP;
			}
		}

		/* Don't trust the cache for analogue */
		if (wm8903->class_w_users)
			dcs_mode = WM8903_DCS_MODE_START_STOP;

		snd_soc_component_update_bits(component, WM8903_DC_SERVO_2,
				    WM8903_DCS_MODE_MASK, dcs_mode);

		snd_soc_component_update_bits(component, WM8903_DC_SERVO_0,
				    WM8903_DCS_ENA_MASK, wm8903->dcs_pending);

		switch (dcs_mode) {
		case WM8903_DCS_MODE_WRITE_STOP:
			break;

		case WM8903_DCS_MODE_START_STOP:
			msleep(270);

			/* Cache the measured offsets for digital */
			if (wm8903->class_w_users)
				break;

			for (i = 0; i < ARRAY_SIZE(wm8903->dcs_cache); i++) {
				if (!(wm8903->dcs_pending & (1 << i)))
					continue;

				val = snd_soc_component_read(component,
						   WM8903_DC_SERVO_READBACK_1 + i);
				dev_dbg(component->dev, "DC servo %d: %x\n",
					3 - i, val);
				wm8903->dcs_cache[i] = val;
			}
			break;

		default:
			pr_warn("DCS mode %d delay not set\n", dcs_mode);
			break;
		}

		wm8903->dcs_pending = 0;
	}
}

/*
 * When used with DAC outputs only the WM8903 charge pump supports
 * operation in class W mode, providing very low power consumption
 * when used with digital sources.  Enable and disable this mode
 * automatically depending on the mixer configuration.
 *
 * All the relevant controls are simple switches.
 */
static int wm8903_class_w_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_dapm_kcontrol_component(kcontrol);
	struct wm8903_priv *wm8903 = snd_soc_component_get_drvdata(component);
	u16 reg;
	int ret;

	reg = snd_soc_component_read(component, WM8903_CLASS_W_0);

	/* Turn it off if we're about to enable bypass */
	if (ucontrol->value.integer.value[0]) {
		if (wm8903->class_w_users == 0) {
			dev_dbg(component->dev, "Disabling Class W\n");
			snd_soc_component_write(component, WM8903_CLASS_W_0, reg &
				     ~(WM8903_CP_DYN_FREQ | WM8903_CP_DYN_V));
		}
		wm8903->class_w_users++;
	}

	/* Implement the change */
	ret = snd_soc_dapm_put_volsw(kcontrol, ucontrol);

	/* If we've just disabled the last bypass path turn Class W on */
	if (!ucontrol->value.integer.value[0]) {
		if (wm8903->class_w_users == 1) {
			dev_dbg(component->dev, "Enabling Class W\n");
			snd_soc_component_write(component, WM8903_CLASS_W_0, reg |
				     WM8903_CP_DYN_FREQ | WM8903_CP_DYN_V);
		}
		wm8903->class_w_users--;
	}

	dev_dbg(component->dev, "Bypass use count now %d\n",
		wm8903->class_w_users);

	return ret;
}

#define SOC_DAPM_SINGLE_W(xname, reg, shift, max, invert) \
	SOC_SINGLE_EXT(xname, reg, shift, max, invert, \
		snd_soc_dapm_get_volsw, wm8903_class_w_put)


static int wm8903_deemph[] = { 0, 32000, 44100, 48000 };

static int wm8903_set_deemph(struct snd_soc_component *component)
{
	struct wm8903_priv *wm8903 = snd_soc_component_get_drvdata(component);
	int val, i, best;

	/* If we're using deemphasis select the nearest available sample
	 * rate.
	 */
	if (wm8903->deemph) {
		best = 1;
		for (i = 2; i < ARRAY_SIZE(wm8903_deemph); i++) {
			if (abs(wm8903_deemph[i] - wm8903->fs) <
			    abs(wm8903_deemph[best] - wm8903->fs))
				best = i;
		}

		val = best << WM8903_DEEMPH_SHIFT;
	} else {
		best = 0;
		val = 0;
	}

	dev_dbg(component->dev, "Set deemphasis %d (%dHz)\n",
		best, wm8903_deemph[best]);

	return snd_soc_component_update_bits(component, WM8903_DAC_DIGITAL_1,
				   WM8903_DEEMPH_MASK, val);
}

static int wm8903_get_deemph(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wm8903_priv *wm8903 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = wm8903->deemph;

	return 0;
}

static int wm8903_put_deemph(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct wm8903_priv *wm8903 = snd_soc_component_get_drvdata(component);
	unsigned int deemph = ucontrol->value.integer.value[0];
	int ret = 0;

	if (deemph > 1)
		return -EINVAL;

	mutex_lock(&wm8903->lock);
	if (wm8903->deemph != deemph) {
		wm8903->deemph = deemph;

		wm8903_set_deemph(component);

		ret = 1;
	}
	mutex_unlock(&wm8903->lock);

	return ret;
}

/* ALSA can only do steps of .01dB */
static const DECLARE_TLV_DB_SCALE(digital_tlv, -7200, 75, 1);

static const DECLARE_TLV_DB_SCALE(dac_boost_tlv, 0, 600, 0);

static const DECLARE_TLV_DB_SCALE(digital_sidetone_tlv, -3600, 300, 0);
static const DECLARE_TLV_DB_SCALE(out_tlv, -5700, 100, 0);

static const DECLARE_TLV_DB_SCALE(drc_tlv_thresh, 0, 75, 0);
static const DECLARE_TLV_DB_SCALE(drc_tlv_amp, -2250, 75, 0);
static const DECLARE_TLV_DB_SCALE(drc_tlv_min, 0, 600, 0);
static const DECLARE_TLV_DB_SCALE(drc_tlv_max, 1200, 600, 0);
static const DECLARE_TLV_DB_SCALE(drc_tlv_startup, -300, 50, 0);

static const char *hpf_mode_text[] = {
	"Hi-fi", "Voice 1", "Voice 2", "Voice 3"
};

static SOC_ENUM_SINGLE_DECL(hpf_mode,
			    WM8903_ADC_DIGITAL_0, 5, hpf_mode_text);

static const char *osr_text[] = {
	"Low power", "High performance"
};

static SOC_ENUM_SINGLE_DECL(adc_osr,
			    WM8903_ANALOGUE_ADC_0, 0, osr_text);

static SOC_ENUM_SINGLE_DECL(dac_osr,
			    WM8903_DAC_DIGITAL_1, 0, osr_text);

static const char *drc_slope_text[] = {
	"1", "1/2", "1/4", "1/8", "1/16", "0"
};

static SOC_ENUM_SINGLE_DECL(drc_slope_r0,
			    WM8903_DRC_2, 3, drc_slope_text);

static SOC_ENUM_SINGLE_DECL(drc_slope_r1,
			    WM8903_DRC_2, 0, drc_slope_text);

static const char *drc_attack_text[] = {
	"instantaneous",
	"363us", "762us", "1.45ms", "2.9ms", "5.8ms", "11.6ms", "23.2ms",
	"46.4ms", "92.8ms", "185.6ms"
};

static SOC_ENUM_SINGLE_DECL(drc_attack,
			    WM8903_DRC_1, 12, drc_attack_text);

static const char *drc_decay_text[] = {
	"186ms", "372ms", "743ms", "1.49s", "2.97s", "5.94s", "11.89s",
	"23.87s", "47.56s"
};

static SOC_ENUM_SINGLE_DECL(drc_decay,
			    WM8903_DRC_1, 8, drc_decay_text);

static const char *drc_ff_delay_text[] = {
	"5 samples", "9 samples"
};

static SOC_ENUM_SINGLE_DECL(drc_ff_delay,
			    WM8903_DRC_0, 5, drc_ff_delay_text);

static const char *drc_qr_decay_text[] = {
	"0.725ms", "1.45ms", "5.8ms"
};

static SOC_ENUM_SINGLE_DECL(drc_qr_decay,
			    WM8903_DRC_1, 4, drc_qr_decay_text);

static const char *drc_smoothing_text[] = {
	"Low", "Medium", "High"
};

static SOC_ENUM_SINGLE_DECL(drc_smoothing,
			    WM8903_DRC_0, 11, drc_smoothing_text);

static const char *soft_mute_text[] = {
	"Fast (fs/2)", "Slow (fs/32)"
};

static SOC_ENUM_SINGLE_DECL(soft_mute,
			    WM8903_DAC_DIGITAL_1, 10, soft_mute_text);

static const char *mute_mode_text[] = {
	"Hard", "Soft"
};

static SOC_ENUM_SINGLE_DECL(mute_mode,
			    WM8903_DAC_DIGITAL_1, 9, mute_mode_text);

static const char *companding_text[] = {
	"ulaw", "alaw"
};

static SOC_ENUM_SINGLE_DECL(dac_companding,
			    WM8903_AUDIO_INTERFACE_0, 0, companding_text);

static SOC_ENUM_SINGLE_DECL(adc_companding,
			    WM8903_AUDIO_INTERFACE_0, 2, companding_text);

static const char *input_mode_text[] = {
	"Single-Ended", "Differential Line", "Differential Mic"
};

static SOC_ENUM_SINGLE_DECL(linput_mode_enum,
			    WM8903_ANALOGUE_LEFT_INPUT_1, 0, input_mode_text);

static SOC_ENUM_SINGLE_DECL(rinput_mode_enum,
			    WM8903_ANALOGUE_RIGHT_INPUT_1, 0, input_mode_text);

static const char *linput_mux_text[] = {
	"IN1L", "IN2L", "IN3L"
};

static SOC_ENUM_SINGLE_DECL(linput_enum,
			    WM8903_ANALOGUE_LEFT_INPUT_1, 2, linput_mux_text);

static SOC_ENUM_SINGLE_DECL(linput_inv_enum,
			    WM8903_ANALOGUE_LEFT_INPUT_1, 4, linput_mux_text);

static const char *rinput_mux_text[] = {
	"IN1R", "IN2R", "IN3R"
};

static SOC_ENUM_SINGLE_DECL(rinput_enum,
			    WM8903_ANALOGUE_RIGHT_INPUT_1, 2, rinput_mux_text);

static SOC_ENUM_SINGLE_DECL(rinput_inv_enum,
			    WM8903_ANALOGUE_RIGHT_INPUT_1, 4, rinput_mux_text);


static const char *sidetone_text[] = {
	"None", "Left", "Right"
};

static SOC_ENUM_SINGLE_DECL(lsidetone_enum,
			    WM8903_DAC_DIGITAL_0, 2, sidetone_text);

static SOC_ENUM_SINGLE_DECL(rsidetone_enum,
			    WM8903_DAC_DIGITAL_0, 0, sidetone_text);

static const char *adcinput_text[] = {
	"ADC", "DMIC"
};

static SOC_ENUM_SINGLE_DECL(adcinput_enum,
			    WM8903_CLOCK_RATE_TEST_4, 9, adcinput_text);

static const char *aif_text[] = {
	"Left", "Right"
};

static SOC_ENUM_SINGLE_DECL(lcapture_enum,
			    WM8903_AUDIO_INTERFACE_0, 7, aif_text);

static SOC_ENUM_SINGLE_DECL(rcapture_enum,
			    WM8903_AUDIO_INTERFACE_0, 6, aif_text);

static SOC_ENUM_SINGLE_DECL(lplay_enum,
			    WM8903_AUDIO_INTERFACE_0, 5, aif_text);

static SOC_ENUM_SINGLE_DECL(rplay_enum,
			    WM8903_AUDIO_INTERFACE_0, 4, aif_text);

static const struct snd_kcontrol_new wm8903_snd_controls[] = {

/* Input PGAs - No TLV since the scale depends on PGA mode */
SOC_SINGLE("Left Input PGA Switch", WM8903_ANALOGUE_LEFT_INPUT_0,
	   7, 1, 1),
SOC_SINGLE("Left Input PGA Volume", WM8903_ANALOGUE_LEFT_INPUT_0,
	   0, 31, 0),
SOC_SINGLE("Left Input PGA Common Mode Switch", WM8903_ANALOGUE_LEFT_INPUT_1,
	   6, 1, 0),

SOC_SINGLE("Right Input PGA Switch", WM8903_ANALOGUE_RIGHT_INPUT_0,
	   7, 1, 1),
SOC_SINGLE("Right Input PGA Volume", WM8903_ANALOGUE_RIGHT_INPUT_0,
	   0, 31, 0),
SOC_SINGLE("Right Input PGA Common Mode Switch", WM8903_ANALOGUE_RIGHT_INPUT_1,
	   6, 1, 0),

/* ADCs */
SOC_ENUM("ADC OSR", adc_osr),
SOC_SINGLE("HPF Switch", WM8903_ADC_DIGITAL_0, 4, 1, 0),
SOC_ENUM("HPF Mode", hpf_mode),
SOC_SINGLE("DRC Switch", WM8903_DRC_0, 15, 1, 0),
SOC_ENUM("DRC Compressor Slope R0", drc_slope_r0),
SOC_ENUM("DRC Compressor Slope R1", drc_slope_r1),
SOC_SINGLE_TLV("DRC Compressor Threshold Volume", WM8903_DRC_3, 5, 124, 1,
	       drc_tlv_thresh),
SOC_SINGLE_TLV("DRC Volume", WM8903_DRC_3, 0, 30, 1, drc_tlv_amp),
SOC_SINGLE_TLV("DRC Minimum Gain Volume", WM8903_DRC_1, 2, 3, 1, drc_tlv_min),
SOC_SINGLE_TLV("DRC Maximum Gain Volume", WM8903_DRC_1, 0, 3, 0, drc_tlv_max),
SOC_ENUM("DRC Attack Rate", drc_attack),
SOC_ENUM("DRC Decay Rate", drc_decay),
SOC_ENUM("DRC FF Delay", drc_ff_delay),
SOC_SINGLE("DRC Anticlip Switch", WM8903_DRC_0, 1, 1, 0),
SOC_SINGLE("DRC QR Switch", WM8903_DRC_0, 2, 1, 0),
SOC_SINGLE_TLV("DRC QR Threshold Volume", WM8903_DRC_0, 6, 3, 0, drc_tlv_max),
SOC_ENUM("DRC QR Decay Rate", drc_qr_decay),
SOC_SINGLE("DRC Smoothing Switch", WM8903_DRC_0, 3, 1, 0),
SOC_SINGLE("DRC Smoothing Hysteresis Switch", WM8903_DRC_0, 0, 1, 0),
SOC_ENUM("DRC Smoothing Threshold", drc_smoothing),
SOC_SINGLE_TLV("DRC Startup Volume", WM8903_DRC_0, 6, 18, 0, drc_tlv_startup),

SOC_DOUBLE_R_TLV("Digital Capture Volume", WM8903_ADC_DIGITAL_VOLUME_LEFT,
		 WM8903_ADC_DIGITAL_VOLUME_RIGHT, 1, 120, 0, digital_tlv),
SOC_ENUM("ADC Companding Mode", adc_companding),
SOC_SINGLE("ADC Companding Switch", WM8903_AUDIO_INTERFACE_0, 3, 1, 0),

SOC_DOUBLE_TLV("Digital Sidetone Volume", WM8903_DAC_DIGITAL_0, 4, 8,
	       12, 0, digital_sidetone_tlv),

/* DAC */
SOC_ENUM("DAC OSR", dac_osr),
SOC_DOUBLE_R_TLV("Digital Playback Volume", WM8903_DAC_DIGITAL_VOLUME_LEFT,
		 WM8903_DAC_DIGITAL_VOLUME_RIGHT, 1, 120, 0, digital_tlv),
SOC_ENUM("DAC Soft Mute Rate", soft_mute),
SOC_ENUM("DAC Mute Mode", mute_mode),
SOC_SINGLE("DAC Mono Switch", WM8903_DAC_DIGITAL_1, 12, 1, 0),
SOC_ENUM("DAC Companding Mode", dac_companding),
SOC_SINGLE("DAC Companding Switch", WM8903_AUDIO_INTERFACE_0, 1, 1, 0),
SOC_SINGLE_TLV("DAC Boost Volume", WM8903_AUDIO_INTERFACE_0, 9, 3, 0,
	       dac_boost_tlv),
SOC_SINGLE_BOOL_EXT("Playback Deemphasis Switch", 0,
		    wm8903_get_deemph, wm8903_put_deemph),

/* Headphones */
SOC_DOUBLE_R("Headphone Switch",
	     WM8903_ANALOGUE_OUT1_LEFT, WM8903_ANALOGUE_OUT1_RIGHT,
	     8, 1, 1),
SOC_DOUBLE_R("Headphone ZC Switch",
	     WM8903_ANALOGUE_OUT1_LEFT, WM8903_ANALOGUE_OUT1_RIGHT,
	     6, 1, 0),
SOC_DOUBLE_R_TLV("Headphone Volume",
		 WM8903_ANALOGUE_OUT1_LEFT, WM8903_ANALOGUE_OUT1_RIGHT,
		 0, 63, 0, out_tlv),

/* Line out */
SOC_DOUBLE_R("Line Out Switch",
	     WM8903_ANALOGUE_OUT2_LEFT, WM8903_ANALOGUE_OUT2_RIGHT,
	     8, 1, 1),
SOC_DOUBLE_R("Line Out ZC Switch",
	     WM8903_ANALOGUE_OUT2_LEFT, WM8903_ANALOGUE_OUT2_RIGHT,
	     6, 1, 0),
SOC_DOUBLE_R_TLV("Line Out Volume",
		 WM8903_ANALOGUE_OUT2_LEFT, WM8903_ANALOGUE_OUT2_RIGHT,
		 0, 63, 0, out_tlv),

/* Speaker */
SOC_DOUBLE_R("Speaker Switch",
	     WM8903_ANALOGUE_OUT3_LEFT, WM8903_ANALOGUE_OUT3_RIGHT, 8, 1, 1),
SOC_DOUBLE_R("Speaker ZC Switch",
	     WM8903_ANALOGUE_OUT3_LEFT, WM8903_ANALOGUE_OUT3_RIGHT, 6, 1, 0),
SOC_DOUBLE_R_TLV("Speaker Volume",
		 WM8903_ANALOGUE_OUT3_LEFT, WM8903_ANALOGUE_OUT3_RIGHT,
		 0, 63, 0, out_tlv),
};

static const struct snd_kcontrol_new linput_mode_mux =
	SOC_DAPM_ENUM("Left Input Mode Mux", linput_mode_enum);

static const struct snd_kcontrol_new rinput_mode_mux =
	SOC_DAPM_ENUM("Right Input Mode Mux", rinput_mode_enum);

static const struct snd_kcontrol_new linput_mux =
	SOC_DAPM_ENUM("Left Input Mux", linput_enum);

static const struct snd_kcontrol_new linput_inv_mux =
	SOC_DAPM_ENUM("Left Inverting Input Mux", linput_inv_enum);

static const struct snd_kcontrol_new rinput_mux =
	SOC_DAPM_ENUM("Right Input Mux", rinput_enum);

static const struct snd_kcontrol_new rinput_inv_mux =
	SOC_DAPM_ENUM("Right Inverting Input Mux", rinput_inv_enum);

static const struct snd_kcontrol_new lsidetone_mux =
	SOC_DAPM_ENUM("DACL Sidetone Mux", lsidetone_enum);

static const struct snd_kcontrol_new rsidetone_mux =
	SOC_DAPM_ENUM("DACR Sidetone Mux", rsidetone_enum);

static const struct snd_kcontrol_new adcinput_mux =
	SOC_DAPM_ENUM("ADC Input", adcinput_enum);

static const struct snd_kcontrol_new lcapture_mux =
	SOC_DAPM_ENUM("Left Capture Mux", lcapture_enum);

static const struct snd_kcontrol_new rcapture_mux =
	SOC_DAPM_ENUM("Right Capture Mux", rcapture_enum);

static const struct snd_kcontrol_new lplay_mux =
	SOC_DAPM_ENUM("Left Playback Mux", lplay_enum);

static const struct snd_kcontrol_new rplay_mux =
	SOC_DAPM_ENUM("Right Playback Mux", rplay_enum);

static const struct snd_kcontrol_new left_output_mixer[] = {
SOC_DAPM_SINGLE("DACL Switch", WM8903_ANALOGUE_LEFT_MIX_0, 3, 1, 0),
SOC_DAPM_SINGLE("DACR Switch", WM8903_ANALOGUE_LEFT_MIX_0, 2, 1, 0),
SOC_DAPM_SINGLE_W("Left Bypass Switch", WM8903_ANALOGUE_LEFT_MIX_0, 1, 1, 0),
SOC_DAPM_SINGLE_W("Right Bypass Switch", WM8903_ANALOGUE_LEFT_MIX_0, 0, 1, 0),
};

static const struct snd_kcontrol_new right_output_mixer[] = {
SOC_DAPM_SINGLE("DACL Switch", WM8903_ANALOGUE_RIGHT_MIX_0, 3, 1, 0),
SOC_DAPM_SINGLE("DACR Switch", WM8903_ANALOGUE_RIGHT_MIX_0, 2, 1, 0),
SOC_DAPM_SINGLE_W("Left Bypass Switch", WM8903_ANALOGUE_RIGHT_MIX_0, 1, 1, 0),
SOC_DAPM_SINGLE_W("Right Bypass Switch", WM8903_ANALOGUE_RIGHT_MIX_0, 0, 1, 0),
};

static const struct snd_kcontrol_new left_speaker_mixer[] = {
SOC_DAPM_SINGLE("DACL Switch", WM8903_ANALOGUE_SPK_MIX_LEFT_0, 3, 1, 0),
SOC_DAPM_SINGLE("DACR Switch", WM8903_ANALOGUE_SPK_MIX_LEFT_0, 2, 1, 0),
SOC_DAPM_SINGLE("Left Bypass Switch", WM8903_ANALOGUE_SPK_MIX_LEFT_0, 1, 1, 0),
SOC_DAPM_SINGLE("Right Bypass Switch", WM8903_ANALOGUE_SPK_MIX_LEFT_0,
		0, 1, 0),
};

static const struct snd_kcontrol_new right_speaker_mixer[] = {
SOC_DAPM_SINGLE("DACL Switch", WM8903_ANALOGUE_SPK_MIX_RIGHT_0, 3, 1, 0),
SOC_DAPM_SINGLE("DACR Switch", WM8903_ANALOGUE_SPK_MIX_RIGHT_0, 2, 1, 0),
SOC_DAPM_SINGLE("Left Bypass Switch", WM8903_ANALOGUE_SPK_MIX_RIGHT_0,
		1, 1, 0),
SOC_DAPM_SINGLE("Right Bypass Switch", WM8903_ANALOGUE_SPK_MIX_RIGHT_0,
		0, 1, 0),
};

static const struct snd_soc_dapm_widget wm8903_dapm_widgets[] = {
SND_SOC_DAPM_INPUT("IN1L"),
SND_SOC_DAPM_INPUT("IN1R"),
SND_SOC_DAPM_INPUT("IN2L"),
SND_SOC_DAPM_INPUT("IN2R"),
SND_SOC_DAPM_INPUT("IN3L"),
SND_SOC_DAPM_INPUT("IN3R"),
SND_SOC_DAPM_INPUT("DMICDAT"),

SND_SOC_DAPM_OUTPUT("HPOUTL"),
SND_SOC_DAPM_OUTPUT("HPOUTR"),
SND_SOC_DAPM_OUTPUT("LINEOUTL"),
SND_SOC_DAPM_OUTPUT("LINEOUTR"),
SND_SOC_DAPM_OUTPUT("LOP"),
SND_SOC_DAPM_OUTPUT("LON"),
SND_SOC_DAPM_OUTPUT("ROP"),
SND_SOC_DAPM_OUTPUT("RON"),

SND_SOC_DAPM_SUPPLY("MICBIAS", WM8903_MIC_BIAS_CONTROL_0, 0, 0, NULL, 0),

SND_SOC_DAPM_MUX("Left Input Mux", SND_SOC_NOPM, 0, 0, &linput_mux),
SND_SOC_DAPM_MUX("Left Input Inverting Mux", SND_SOC_NOPM, 0, 0,
		 &linput_inv_mux),
SND_SOC_DAPM_MUX("Left Input Mode Mux", SND_SOC_NOPM, 0, 0, &linput_mode_mux),

SND_SOC_DAPM_MUX("Right Input Mux", SND_SOC_NOPM, 0, 0, &rinput_mux),
SND_SOC_DAPM_MUX("Right Input Inverting Mux", SND_SOC_NOPM, 0, 0,
		 &rinput_inv_mux),
SND_SOC_DAPM_MUX("Right Input Mode Mux", SND_SOC_NOPM, 0, 0, &rinput_mode_mux),

SND_SOC_DAPM_PGA("Left Input PGA", WM8903_POWER_MANAGEMENT_0, 1, 0, NULL, 0),
SND_SOC_DAPM_PGA("Right Input PGA", WM8903_POWER_MANAGEMENT_0, 0, 0, NULL, 0),

SND_SOC_DAPM_MUX("Left ADC Input", SND_SOC_NOPM, 0, 0, &adcinput_mux),
SND_SOC_DAPM_MUX("Right ADC Input", SND_SOC_NOPM, 0, 0, &adcinput_mux),

SND_SOC_DAPM_ADC("ADCL", NULL, WM8903_POWER_MANAGEMENT_6, 1, 0),
SND_SOC_DAPM_ADC("ADCR", NULL, WM8903_POWER_MANAGEMENT_6, 0, 0),

SND_SOC_DAPM_MUX("Left Capture Mux", SND_SOC_NOPM, 0, 0, &lcapture_mux),
SND_SOC_DAPM_MUX("Right Capture Mux", SND_SOC_NOPM, 0, 0, &rcapture_mux),

SND_SOC_DAPM_AIF_OUT("AIFTXL", "Left HiFi Capture", 0, SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_AIF_OUT("AIFTXR", "Right HiFi Capture", 0, SND_SOC_NOPM, 0, 0),

SND_SOC_DAPM_MUX("DACL Sidetone", SND_SOC_NOPM, 0, 0, &lsidetone_mux),
SND_SOC_DAPM_MUX("DACR Sidetone", SND_SOC_NOPM, 0, 0, &rsidetone_mux),

SND_SOC_DAPM_AIF_IN("AIFRXL", "Left Playback", 0, SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_AIF_IN("AIFRXR", "Right Playback", 0, SND_SOC_NOPM, 0, 0),

SND_SOC_DAPM_MUX("Left Playback Mux", SND_SOC_NOPM, 0, 0, &lplay_mux),
SND_SOC_DAPM_MUX("Right Playback Mux", SND_SOC_NOPM, 0, 0, &rplay_mux),

SND_SOC_DAPM_DAC("DACL", NULL, WM8903_POWER_MANAGEMENT_6, 3, 0),
SND_SOC_DAPM_DAC("DACR", NULL, WM8903_POWER_MANAGEMENT_6, 2, 0),

SND_SOC_DAPM_MIXER("Left Output Mixer", WM8903_POWER_MANAGEMENT_1, 1, 0,
		   left_output_mixer, ARRAY_SIZE(left_output_mixer)),
SND_SOC_DAPM_MIXER("Right Output Mixer", WM8903_POWER_MANAGEMENT_1, 0, 0,
		   right_output_mixer, ARRAY_SIZE(right_output_mixer)),

SND_SOC_DAPM_MIXER("Left Speaker Mixer", WM8903_POWER_MANAGEMENT_4, 1, 0,
		   left_speaker_mixer, ARRAY_SIZE(left_speaker_mixer)),
SND_SOC_DAPM_MIXER("Right Speaker Mixer", WM8903_POWER_MANAGEMENT_4, 0, 0,
		   right_speaker_mixer, ARRAY_SIZE(right_speaker_mixer)),

SND_SOC_DAPM_PGA_S("Left Headphone Output PGA", 0, WM8903_POWER_MANAGEMENT_2,
		   1, 0, NULL, 0),
SND_SOC_DAPM_PGA_S("Right Headphone Output PGA", 0, WM8903_POWER_MANAGEMENT_2,
		   0, 0, NULL, 0),

SND_SOC_DAPM_PGA_S("Left Line Output PGA", 0, WM8903_POWER_MANAGEMENT_3, 1, 0,
		   NULL, 0),
SND_SOC_DAPM_PGA_S("Right Line Output PGA", 0, WM8903_POWER_MANAGEMENT_3, 0, 0,
		   NULL, 0),

SND_SOC_DAPM_PGA_S("HPL_RMV_SHORT", 4, WM8903_ANALOGUE_HP_0, 7, 0, NULL, 0),
SND_SOC_DAPM_PGA_S("HPL_ENA_OUTP", 3, WM8903_ANALOGUE_HP_0, 6, 0, NULL, 0),
SND_SOC_DAPM_PGA_S("HPL_ENA_DLY", 2, WM8903_ANALOGUE_HP_0, 5, 0, NULL, 0),
SND_SOC_DAPM_PGA_S("HPL_ENA", 1, WM8903_ANALOGUE_HP_0, 4, 0, NULL, 0),
SND_SOC_DAPM_PGA_S("HPR_RMV_SHORT", 4, WM8903_ANALOGUE_HP_0, 3, 0, NULL, 0),
SND_SOC_DAPM_PGA_S("HPR_ENA_OUTP", 3, WM8903_ANALOGUE_HP_0, 2, 0, NULL, 0),
SND_SOC_DAPM_PGA_S("HPR_ENA_DLY", 2, WM8903_ANALOGUE_HP_0, 1, 0, NULL, 0),
SND_SOC_DAPM_PGA_S("HPR_ENA", 1, WM8903_ANALOGUE_HP_0, 0, 0, NULL, 0),

SND_SOC_DAPM_PGA_S("LINEOUTL_RMV_SHORT", 4, WM8903_ANALOGUE_LINEOUT_0, 7, 0,
		   NULL, 0),
SND_SOC_DAPM_PGA_S("LINEOUTL_ENA_OUTP", 3, WM8903_ANALOGUE_LINEOUT_0, 6, 0,
		   NULL, 0),
SND_SOC_DAPM_PGA_S("LINEOUTL_ENA_DLY", 2, WM8903_ANALOGUE_LINEOUT_0, 5, 0,
		   NULL, 0),
SND_SOC_DAPM_PGA_S("LINEOUTL_ENA", 1, WM8903_ANALOGUE_LINEOUT_0, 4, 0,
		   NULL, 0),
SND_SOC_DAPM_PGA_S("LINEOUTR_RMV_SHORT", 4, WM8903_ANALOGUE_LINEOUT_0, 3, 0,
		   NULL, 0),
SND_SOC_DAPM_PGA_S("LINEOUTR_ENA_OUTP", 3, WM8903_ANALOGUE_LINEOUT_0, 2, 0,
		   NULL, 0),
SND_SOC_DAPM_PGA_S("LINEOUTR_ENA_DLY", 2, WM8903_ANALOGUE_LINEOUT_0, 1, 0,
		   NULL, 0),
SND_SOC_DAPM_PGA_S("LINEOUTR_ENA", 1, WM8903_ANALOGUE_LINEOUT_0, 0, 0,
		   NULL, 0),

SND_SOC_DAPM_SUPPLY("DCS Master", WM8903_DC_SERVO_0, 4, 0, NULL, 0),
SND_SOC_DAPM_PGA_S("HPL_DCS", 3, SND_SOC_NOPM, 3, 0, wm8903_dcs_event,
		   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
SND_SOC_DAPM_PGA_S("HPR_DCS", 3, SND_SOC_NOPM, 2, 0, wm8903_dcs_event,
		   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
SND_SOC_DAPM_PGA_S("LINEOUTL_DCS", 3, SND_SOC_NOPM, 1, 0, wm8903_dcs_event,
		   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
SND_SOC_DAPM_PGA_S("LINEOUTR_DCS", 3, SND_SOC_NOPM, 0, 0, wm8903_dcs_event,
		   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

SND_SOC_DAPM_PGA("Left Speaker PGA", WM8903_POWER_MANAGEMENT_5, 1, 0,
		 NULL, 0),
SND_SOC_DAPM_PGA("Right Speaker PGA", WM8903_POWER_MANAGEMENT_5, 0, 0,
		 NULL, 0),

SND_SOC_DAPM_SUPPLY("Charge Pump", WM8903_CHARGE_PUMP_0, 0, 0,
		    wm8903_cp_event, SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_SUPPLY("CLK_DSP", WM8903_CLOCK_RATES_2, 1, 0, NULL, 0),
SND_SOC_DAPM_SUPPLY("CLK_SYS", WM8903_CLOCK_RATES_2, 2, 0, NULL, 0),
};

static const struct snd_soc_dapm_route wm8903_intercon[] = {

	{ "CLK_DSP", NULL, "CLK_SYS" },
	{ "MICBIAS", NULL, "CLK_SYS" },
	{ "HPL_DCS", NULL, "CLK_SYS" },
	{ "HPR_DCS", NULL, "CLK_SYS" },
	{ "LINEOUTL_DCS", NULL, "CLK_SYS" },
	{ "LINEOUTR_DCS", NULL, "CLK_SYS" },

	{ "Left Input Mux", "IN1L", "IN1L" },
	{ "Left Input Mux", "IN2L", "IN2L" },
	{ "Left Input Mux", "IN3L", "IN3L" },

	{ "Left Input Inverting Mux", "IN1L", "IN1L" },
	{ "Left Input Inverting Mux", "IN2L", "IN2L" },
	{ "Left Input Inverting Mux", "IN3L", "IN3L" },

	{ "Right Input Mux", "IN1R", "IN1R" },
	{ "Right Input Mux", "IN2R", "IN2R" },
	{ "Right Input Mux", "IN3R", "IN3R" },

	{ "Right Input Inverting Mux", "IN1R", "IN1R" },
	{ "Right Input Inverting Mux", "IN2R", "IN2R" },
	{ "Right Input Inverting Mux", "IN3R", "IN3R" },

	{ "Left Input Mode Mux", "Single-Ended", "Left Input Inverting Mux" },
	{ "Left Input Mode Mux", "Differential Line",
	  "Left Input Mux" },
	{ "Left Input Mode Mux", "Differential Line",
	  "Left Input Inverting Mux" },
	{ "Left Input Mode Mux", "Differential Mic",
	  "Left Input Mux" },
	{ "Left Input Mode Mux", "Differential Mic",
	  "Left Input Inverting Mux" },

	{ "Right Input Mode Mux", "Single-Ended",
	  "Right Input Inverting Mux" },
	{ "Right Input Mode Mux", "Differential Line",
	  "Right Input Mux" },
	{ "Right Input Mode Mux", "Differential Line",
	  "Right Input Inverting Mux" },
	{ "Right Input Mode Mux", "Differential Mic",
	  "Right Input Mux" },
	{ "Right Input Mode Mux", "Differential Mic",
	  "Right Input Inverting Mux" },

	{ "Left Input PGA", NULL, "Left Input Mode Mux" },
	{ "Right Input PGA", NULL, "Right Input Mode Mux" },

	{ "Left ADC Input", "ADC", "Left Input PGA" },
	{ "Left ADC Input", "DMIC", "DMICDAT" },
	{ "Right ADC Input", "ADC", "Right Input PGA" },
	{ "Right ADC Input", "DMIC", "DMICDAT" },

	{ "Left Capture Mux", "Left", "ADCL" },
	{ "Left Capture Mux", "Right", "ADCR" },

	{ "Right Capture Mux", "Left", "ADCL" },
	{ "Right Capture Mux", "Right", "ADCR" },

	{ "AIFTXL", NULL, "Left Capture Mux" },
	{ "AIFTXR", NULL, "Right Capture Mux" },

	{ "ADCL", NULL, "Left ADC Input" },
	{ "ADCL", NULL, "CLK_DSP" },
	{ "ADCR", NULL, "Right ADC Input" },
	{ "ADCR", NULL, "CLK_DSP" },

	{ "Left Playback Mux", "Left", "AIFRXL" },
	{ "Left Playback Mux", "Right", "AIFRXR" },

	{ "Right Playback Mux", "Left", "AIFRXL" },
	{ "Right Playback Mux", "Right", "AIFRXR" },

	{ "DACL Sidetone", "Left", "ADCL" },
	{ "DACL Sidetone", "Right", "ADCR" },
	{ "DACR Sidetone", "Left", "ADCL" },
	{ "DACR Sidetone", "Right", "ADCR" },

	{ "DACL", NULL, "Left Playback Mux" },
	{ "DACL", NULL, "DACL Sidetone" },
	{ "DACL", NULL, "CLK_DSP" },

	{ "DACR", NULL, "Right Playback Mux" },
	{ "DACR", NULL, "DACR Sidetone" },
	{ "DACR", NULL, "CLK_DSP" },

	{ "Left Output Mixer", "Left Bypass Switch", "Left Input PGA" },
	{ "Left Output Mixer", "Right Bypass Switch", "Right Input PGA" },
	{ "Left Output Mixer", "DACL Switch", "DACL" },
	{ "Left Output Mixer", "DACR Switch", "DACR" },

	{ "Right Output Mixer", "Left Bypass Switch", "Left Input PGA" },
	{ "Right Output Mixer", "Right Bypass Switch", "Right Input PGA" },
	{ "Right Output Mixer", "DACL Switch", "DACL" },
	{ "Right Output Mixer", "DACR Switch", "DACR" },

	{ "Left Speaker Mixer", "Left Bypass Switch", "Left Input PGA" },
	{ "Left Speaker Mixer", "Right Bypass Switch", "Right Input PGA" },
	{ "Left Speaker Mixer", "DACL Switch", "DACL" },
	{ "Left Speaker Mixer", "DACR Switch", "DACR" },

	{ "Right Speaker Mixer", "Left Bypass Switch", "Left Input PGA" },
	{ "Right Speaker Mixer", "Right Bypass Switch", "Right Input PGA" },
	{ "Right Speaker Mixer", "DACL Switch", "DACL" },
	{ "Right Speaker Mixer", "DACR Switch", "DACR" },

	{ "Left Line Output PGA", NULL, "Left Output Mixer" },
	{ "Right Line Output PGA", NULL, "Right Output Mixer" },

	{ "Left Headphone Output PGA", NULL, "Left Output Mixer" },
	{ "Right Headphone Output PGA", NULL, "Right Output Mixer" },

	{ "Left Speaker PGA", NULL, "Left Speaker Mixer" },
	{ "Right Speaker PGA", NULL, "Right Speaker Mixer" },

	{ "HPL_ENA", NULL, "Left Headphone Output PGA" },
	{ "HPR_ENA", NULL, "Right Headphone Output PGA" },
	{ "HPL_ENA_DLY", NULL, "HPL_ENA" },
	{ "HPR_ENA_DLY", NULL, "HPR_ENA" },
	{ "LINEOUTL_ENA", NULL, "Left Line Output PGA" },
	{ "LINEOUTR_ENA", NULL, "Right Line Output PGA" },
	{ "LINEOUTL_ENA_DLY", NULL, "LINEOUTL_ENA" },
	{ "LINEOUTR_ENA_DLY", NULL, "LINEOUTR_ENA" },

	{ "HPL_DCS", NULL, "DCS Master" },
	{ "HPR_DCS", NULL, "DCS Master" },
	{ "LINEOUTL_DCS", NULL, "DCS Master" },
	{ "LINEOUTR_DCS", NULL, "DCS Master" },

	{ "HPL_DCS", NULL, "HPL_ENA_DLY" },
	{ "HPR_DCS", NULL, "HPR_ENA_DLY" },
	{ "LINEOUTL_DCS", NULL, "LINEOUTL_ENA_DLY" },
	{ "LINEOUTR_DCS", NULL, "LINEOUTR_ENA_DLY" },

	{ "HPL_ENA_OUTP", NULL, "HPL_DCS" },
	{ "HPR_ENA_OUTP", NULL, "HPR_DCS" },
	{ "LINEOUTL_ENA_OUTP", NULL, "LINEOUTL_DCS" },
	{ "LINEOUTR_ENA_OUTP", NULL, "LINEOUTR_DCS" },

	{ "HPL_RMV_SHORT", NULL, "HPL_ENA_OUTP" },
	{ "HPR_RMV_SHORT", NULL, "HPR_ENA_OUTP" },
	{ "LINEOUTL_RMV_SHORT", NULL, "LINEOUTL_ENA_OUTP" },
	{ "LINEOUTR_RMV_SHORT", NULL, "LINEOUTR_ENA_OUTP" },

	{ "HPOUTL", NULL, "HPL_RMV_SHORT" },
	{ "HPOUTR", NULL, "HPR_RMV_SHORT" },
	{ "LINEOUTL", NULL, "LINEOUTL_RMV_SHORT" },
	{ "LINEOUTR", NULL, "LINEOUTR_RMV_SHORT" },

	{ "LOP", NULL, "Left Speaker PGA" },
	{ "LON", NULL, "Left Speaker PGA" },

	{ "ROP", NULL, "Right Speaker PGA" },
	{ "RON", NULL, "Right Speaker PGA" },

	{ "Charge Pump", NULL, "CLK_DSP" },

	{ "Left Headphone Output PGA", NULL, "Charge Pump" },
	{ "Right Headphone Output PGA", NULL, "Charge Pump" },
	{ "Left Line Output PGA", NULL, "Charge Pump" },
	{ "Right Line Output PGA", NULL, "Charge Pump" },
};

static int wm8903_set_bias_level(struct snd_soc_component *component,
				 enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		snd_soc_component_update_bits(component, WM8903_VMID_CONTROL_0,
				    WM8903_VMID_RES_MASK,
				    WM8903_VMID_RES_50K);
		break;

	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF) {
			snd_soc_component_update_bits(component, WM8903_BIAS_CONTROL_0,
					    WM8903_POBCTRL | WM8903_ISEL_MASK |
					    WM8903_STARTUP_BIAS_ENA |
					    WM8903_BIAS_ENA,
					    WM8903_POBCTRL |
					    (2 << WM8903_ISEL_SHIFT) |
					    WM8903_STARTUP_BIAS_ENA);

			snd_soc_component_update_bits(component,
					    WM8903_ANALOGUE_SPK_OUTPUT_CONTROL_0,
					    WM8903_SPK_DISCHARGE,
					    WM8903_SPK_DISCHARGE);

			msleep(33);

			snd_soc_component_update_bits(component, WM8903_POWER_MANAGEMENT_5,
					    WM8903_SPKL_ENA | WM8903_SPKR_ENA,
					    WM8903_SPKL_ENA | WM8903_SPKR_ENA);

			snd_soc_component_update_bits(component,
					    WM8903_ANALOGUE_SPK_OUTPUT_CONTROL_0,
					    WM8903_SPK_DISCHARGE, 0);

			snd_soc_component_update_bits(component, WM8903_VMID_CONTROL_0,
					    WM8903_VMID_TIE_ENA |
					    WM8903_BUFIO_ENA |
					    WM8903_VMID_IO_ENA |
					    WM8903_VMID_SOFT_MASK |
					    WM8903_VMID_RES_MASK |
					    WM8903_VMID_BUF_ENA,
					    WM8903_VMID_TIE_ENA |
					    WM8903_BUFIO_ENA |
					    WM8903_VMID_IO_ENA |
					    (2 << WM8903_VMID_SOFT_SHIFT) |
					    WM8903_VMID_RES_250K |
					    WM8903_VMID_BUF_ENA);

			msleep(129);

			snd_soc_component_update_bits(component, WM8903_POWER_MANAGEMENT_5,
					    WM8903_SPKL_ENA | WM8903_SPKR_ENA,
					    0);

			snd_soc_component_update_bits(component, WM8903_VMID_CONTROL_0,
					    WM8903_VMID_SOFT_MASK, 0);

			snd_soc_component_update_bits(component, WM8903_VMID_CONTROL_0,
					    WM8903_VMID_RES_MASK,
					    WM8903_VMID_RES_50K);

			snd_soc_component_update_bits(component, WM8903_BIAS_CONTROL_0,
					    WM8903_BIAS_ENA | WM8903_POBCTRL,
					    WM8903_BIAS_ENA);

			/* By default no bypass paths are enabled so
			 * enable Class W support.
			 */
			dev_dbg(component->dev, "Enabling Class W\n");
			snd_soc_component_update_bits(component, WM8903_CLASS_W_0,
					    WM8903_CP_DYN_FREQ |
					    WM8903_CP_DYN_V,
					    WM8903_CP_DYN_FREQ |
					    WM8903_CP_DYN_V);
		}

		snd_soc_component_update_bits(component, WM8903_VMID_CONTROL_0,
				    WM8903_VMID_RES_MASK,
				    WM8903_VMID_RES_250K);
		break;

	case SND_SOC_BIAS_OFF:
		snd_soc_component_update_bits(component, WM8903_BIAS_CONTROL_0,
				    WM8903_BIAS_ENA, 0);

		snd_soc_component_update_bits(component, WM8903_VMID_CONTROL_0,
				    WM8903_VMID_SOFT_MASK,
				    2 << WM8903_VMID_SOFT_SHIFT);

		snd_soc_component_update_bits(component, WM8903_VMID_CONTROL_0,
				    WM8903_VMID_BUF_ENA, 0);

		msleep(290);

		snd_soc_component_update_bits(component, WM8903_VMID_CONTROL_0,
				    WM8903_VMID_TIE_ENA | WM8903_BUFIO_ENA |
				    WM8903_VMID_IO_ENA | WM8903_VMID_RES_MASK |
				    WM8903_VMID_SOFT_MASK |
				    WM8903_VMID_BUF_ENA, 0);

		snd_soc_component_update_bits(component, WM8903_BIAS_CONTROL_0,
				    WM8903_STARTUP_BIAS_ENA, 0);
		break;
	}

	return 0;
}

static int wm8903_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				 int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = codec_dai->component;
	struct wm8903_priv *wm8903 = snd_soc_component_get_drvdata(component);

	wm8903->sysclk = freq;

	return 0;
}

static int wm8903_set_dai_fmt(struct snd_soc_dai *codec_dai,
			      unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	u16 aif1 = snd_soc_component_read(component, WM8903_AUDIO_INTERFACE_1);

	aif1 &= ~(WM8903_LRCLK_DIR | WM8903_BCLK_DIR | WM8903_AIF_FMT_MASK |
		  WM8903_AIF_LRCLK_INV | WM8903_AIF_BCLK_INV);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	case SND_SOC_DAIFMT_CBS_CFM:
		aif1 |= WM8903_LRCLK_DIR;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		aif1 |= WM8903_LRCLK_DIR | WM8903_BCLK_DIR;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
		aif1 |= WM8903_BCLK_DIR;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		aif1 |= 0x3;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		aif1 |= 0x3 | WM8903_AIF_LRCLK_INV;
		break;
	case SND_SOC_DAIFMT_I2S:
		aif1 |= 0x2;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		aif1 |= 0x1;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		break;
	default:
		return -EINVAL;
	}

	/* Clock inversion */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		/* frame inversion not valid for DSP modes */
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			break;
		case SND_SOC_DAIFMT_IB_NF:
			aif1 |= WM8903_AIF_BCLK_INV;
			break;
		default:
			return -EINVAL;
		}
		break;
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_RIGHT_J:
	case SND_SOC_DAIFMT_LEFT_J:
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			break;
		case SND_SOC_DAIFMT_IB_IF:
			aif1 |= WM8903_AIF_BCLK_INV | WM8903_AIF_LRCLK_INV;
			break;
		case SND_SOC_DAIFMT_IB_NF:
			aif1 |= WM8903_AIF_BCLK_INV;
			break;
		case SND_SOC_DAIFMT_NB_IF:
			aif1 |= WM8903_AIF_LRCLK_INV;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_write(component, WM8903_AUDIO_INTERFACE_1, aif1);

	return 0;
}

static int wm8903_mute(struct snd_soc_dai *codec_dai, int mute, int direction)
{
	struct snd_soc_component *component = codec_dai->component;
	u16 reg;

	reg = snd_soc_component_read(component, WM8903_DAC_DIGITAL_1);

	if (mute)
		reg |= WM8903_DAC_MUTE;
	else
		reg &= ~WM8903_DAC_MUTE;

	snd_soc_component_write(component, WM8903_DAC_DIGITAL_1, reg);

	return 0;
}

/* Lookup table for CLK_SYS/fs ratio.  256fs or more is recommended
 * for optimal performance so we list the lower rates first and match
 * on the last match we find. */
static struct {
	int div;
	int rate;
	int mode;
	int mclk_div;
} clk_sys_ratios[] = {
	{   64, 0x0, 0x0, 1 },
	{   68, 0x0, 0x1, 1 },
	{  125, 0x0, 0x2, 1 },
	{  128, 0x1, 0x0, 1 },
	{  136, 0x1, 0x1, 1 },
	{  192, 0x2, 0x0, 1 },
	{  204, 0x2, 0x1, 1 },

	{   64, 0x0, 0x0, 2 },
	{   68, 0x0, 0x1, 2 },
	{  125, 0x0, 0x2, 2 },
	{  128, 0x1, 0x0, 2 },
	{  136, 0x1, 0x1, 2 },
	{  192, 0x2, 0x0, 2 },
	{  204, 0x2, 0x1, 2 },

	{  250, 0x2, 0x2, 1 },
	{  256, 0x3, 0x0, 1 },
	{  272, 0x3, 0x1, 1 },
	{  384, 0x4, 0x0, 1 },
	{  408, 0x4, 0x1, 1 },
	{  375, 0x4, 0x2, 1 },
	{  512, 0x5, 0x0, 1 },
	{  544, 0x5, 0x1, 1 },
	{  500, 0x5, 0x2, 1 },
	{  768, 0x6, 0x0, 1 },
	{  816, 0x6, 0x1, 1 },
	{  750, 0x6, 0x2, 1 },
	{ 1024, 0x7, 0x0, 1 },
	{ 1088, 0x7, 0x1, 1 },
	{ 1000, 0x7, 0x2, 1 },
	{ 1408, 0x8, 0x0, 1 },
	{ 1496, 0x8, 0x1, 1 },
	{ 1536, 0x9, 0x0, 1 },
	{ 1632, 0x9, 0x1, 1 },
	{ 1500, 0x9, 0x2, 1 },

	{  250, 0x2, 0x2, 2 },
	{  256, 0x3, 0x0, 2 },
	{  272, 0x3, 0x1, 2 },
	{  384, 0x4, 0x0, 2 },
	{  408, 0x4, 0x1, 2 },
	{  375, 0x4, 0x2, 2 },
	{  512, 0x5, 0x0, 2 },
	{  544, 0x5, 0x1, 2 },
	{  500, 0x5, 0x2, 2 },
	{  768, 0x6, 0x0, 2 },
	{  816, 0x6, 0x1, 2 },
	{  750, 0x6, 0x2, 2 },
	{ 1024, 0x7, 0x0, 2 },
	{ 1088, 0x7, 0x1, 2 },
	{ 1000, 0x7, 0x2, 2 },
	{ 1408, 0x8, 0x0, 2 },
	{ 1496, 0x8, 0x1, 2 },
	{ 1536, 0x9, 0x0, 2 },
	{ 1632, 0x9, 0x1, 2 },
	{ 1500, 0x9, 0x2, 2 },
};

/* CLK_SYS/BCLK ratios - multiplied by 10 due to .5s */
static struct {
	int ratio;
	int div;
} bclk_divs[] = {
	{  10,  0 },
	{  20,  2 },
	{  30,  3 },
	{  40,  4 },
	{  50,  5 },
	{  60,  7 },
	{  80,  8 },
	{ 100,  9 },
	{ 120, 11 },
	{ 160, 12 },
	{ 200, 13 },
	{ 220, 14 },
	{ 240, 15 },
	{ 300, 17 },
	{ 320, 18 },
	{ 440, 19 },
	{ 480, 20 },
};

/* Sample rates for DSP */
static struct {
	int rate;
	int value;
} sample_rates[] = {
	{  8000,  0 },
	{ 11025,  1 },
	{ 12000,  2 },
	{ 16000,  3 },
	{ 22050,  4 },
	{ 24000,  5 },
	{ 32000,  6 },
	{ 44100,  7 },
	{ 48000,  8 },
	{ 88200,  9 },
	{ 96000, 10 },
	{ 0,      0 },
};

static int wm8903_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct wm8903_priv *wm8903 = snd_soc_component_get_drvdata(component);
	int fs = params_rate(params);
	int bclk;
	int bclk_div;
	int i;
	int dsp_config;
	int clk_config;
	int best_val;
	int cur_val;
	int clk_sys;

	u16 aif1 = snd_soc_component_read(component, WM8903_AUDIO_INTERFACE_1);
	u16 aif2 = snd_soc_component_read(component, WM8903_AUDIO_INTERFACE_2);
	u16 aif3 = snd_soc_component_read(component, WM8903_AUDIO_INTERFACE_3);
	u16 clock0 = snd_soc_component_read(component, WM8903_CLOCK_RATES_0);
	u16 clock1 = snd_soc_component_read(component, WM8903_CLOCK_RATES_1);
	u16 dac_digital1 = snd_soc_component_read(component, WM8903_DAC_DIGITAL_1);

	/* Enable sloping stopband filter for low sample rates */
	if (fs <= 24000)
		dac_digital1 |= WM8903_DAC_SB_FILT;
	else
		dac_digital1 &= ~WM8903_DAC_SB_FILT;

	/* Configure sample rate logic for DSP - choose nearest rate */
	dsp_config = 0;
	best_val = abs(sample_rates[dsp_config].rate - fs);
	for (i = 1; i < ARRAY_SIZE(sample_rates); i++) {
		cur_val = abs(sample_rates[i].rate - fs);
		if (cur_val <= best_val) {
			dsp_config = i;
			best_val = cur_val;
		}
	}

	dev_dbg(component->dev, "DSP fs = %dHz\n", sample_rates[dsp_config].rate);
	clock1 &= ~WM8903_SAMPLE_RATE_MASK;
	clock1 |= sample_rates[dsp_config].value;

	aif1 &= ~WM8903_AIF_WL_MASK;
	bclk = 2 * fs;
	switch (params_width(params)) {
	case 16:
		bclk *= 16;
		break;
	case 20:
		bclk *= 20;
		aif1 |= 0x4;
		break;
	case 24:
		bclk *= 24;
		aif1 |= 0x8;
		break;
	case 32:
		bclk *= 32;
		aif1 |= 0xc;
		break;
	default:
		return -EINVAL;
	}

	dev_dbg(component->dev, "MCLK = %dHz, target sample rate = %dHz\n",
		wm8903->sysclk, fs);

	/* We may not have an MCLK which allows us to generate exactly
	 * the clock we want, particularly with USB derived inputs, so
	 * approximate.
	 */
	clk_config = 0;
	best_val = abs((wm8903->sysclk /
			(clk_sys_ratios[0].mclk_div *
			 clk_sys_ratios[0].div)) - fs);
	for (i = 1; i < ARRAY_SIZE(clk_sys_ratios); i++) {
		cur_val = abs((wm8903->sysclk /
			       (clk_sys_ratios[i].mclk_div *
				clk_sys_ratios[i].div)) - fs);

		if (cur_val <= best_val) {
			clk_config = i;
			best_val = cur_val;
		}
	}

	if (clk_sys_ratios[clk_config].mclk_div == 2) {
		clock0 |= WM8903_MCLKDIV2;
		clk_sys = wm8903->sysclk / 2;
	} else {
		clock0 &= ~WM8903_MCLKDIV2;
		clk_sys = wm8903->sysclk;
	}

	clock1 &= ~(WM8903_CLK_SYS_RATE_MASK |
		    WM8903_CLK_SYS_MODE_MASK);
	clock1 |= clk_sys_ratios[clk_config].rate << WM8903_CLK_SYS_RATE_SHIFT;
	clock1 |= clk_sys_ratios[clk_config].mode << WM8903_CLK_SYS_MODE_SHIFT;

	dev_dbg(component->dev, "CLK_SYS_RATE=%x, CLK_SYS_MODE=%x div=%d\n",
		clk_sys_ratios[clk_config].rate,
		clk_sys_ratios[clk_config].mode,
		clk_sys_ratios[clk_config].div);

	dev_dbg(component->dev, "Actual CLK_SYS = %dHz\n", clk_sys);

	/* We may not get quite the right frequency if using
	 * approximate clocks so look for the closest match that is
	 * higher than the target (we need to ensure that there enough
	 * BCLKs to clock out the samples).
	 */
	bclk_div = 0;
	best_val = ((clk_sys * 10) / bclk_divs[0].ratio) - bclk;
	i = 1;
	while (i < ARRAY_SIZE(bclk_divs)) {
		cur_val = ((clk_sys * 10) / bclk_divs[i].ratio) - bclk;
		if (cur_val < 0) /* BCLK table is sorted */
			break;
		bclk_div = i;
		best_val = cur_val;
		i++;
	}

	aif2 &= ~WM8903_BCLK_DIV_MASK;
	aif3 &= ~WM8903_LRCLK_RATE_MASK;

	dev_dbg(component->dev, "BCLK ratio %d for %dHz - actual BCLK = %dHz\n",
		bclk_divs[bclk_div].ratio / 10, bclk,
		(clk_sys * 10) / bclk_divs[bclk_div].ratio);

	aif2 |= bclk_divs[bclk_div].div;
	aif3 |= bclk / fs;

	wm8903->fs = params_rate(params);
	wm8903_set_deemph(component);

	snd_soc_component_write(component, WM8903_CLOCK_RATES_0, clock0);
	snd_soc_component_write(component, WM8903_CLOCK_RATES_1, clock1);
	snd_soc_component_write(component, WM8903_AUDIO_INTERFACE_1, aif1);
	snd_soc_component_write(component, WM8903_AUDIO_INTERFACE_2, aif2);
	snd_soc_component_write(component, WM8903_AUDIO_INTERFACE_3, aif3);
	snd_soc_component_write(component, WM8903_DAC_DIGITAL_1, dac_digital1);

	return 0;
}

/**
 * wm8903_mic_detect - Enable microphone detection via the WM8903 IRQ
 *
 * @component:  WM8903 component
 * @jack:   jack to report detection events on
 * @det:    value to report for presence detection
 * @shrt:   value to report for short detection
 *
 * Enable microphone detection via IRQ on the WM8903.  If GPIOs are
 * being used to bring out signals to the processor then only platform
 * data configuration is needed for WM8903 and processor GPIOs should
 * be configured using snd_soc_jack_add_gpios() instead.
 *
 * The current threasholds for detection should be configured using
 * micdet_cfg in the platform data.  Using this function will force on
 * the microphone bias for the device.
 */
int wm8903_mic_detect(struct snd_soc_component *component, struct snd_soc_jack *jack,
		      int det, int shrt)
{
	struct wm8903_priv *wm8903 = snd_soc_component_get_drvdata(component);
	int irq_mask = WM8903_MICDET_EINT | WM8903_MICSHRT_EINT;

	dev_dbg(component->dev, "Enabling microphone detection: %x %x\n",
		det, shrt);

	/* Store the configuration */
	wm8903->mic_jack = jack;
	wm8903->mic_det = det;
	wm8903->mic_short = shrt;

	/* Enable interrupts we've got a report configured for */
	if (det)
		irq_mask &= ~WM8903_MICDET_EINT;
	if (shrt)
		irq_mask &= ~WM8903_MICSHRT_EINT;

	snd_soc_component_update_bits(component, WM8903_INTERRUPT_STATUS_1_MASK,
			    WM8903_MICDET_EINT | WM8903_MICSHRT_EINT,
			    irq_mask);

	if (det || shrt) {
		/* Enable mic detection, this may not have been set through
		 * platform data (eg, if the defaults are OK). */
		snd_soc_component_update_bits(component, WM8903_WRITE_SEQUENCER_0,
				    WM8903_WSEQ_ENA, WM8903_WSEQ_ENA);
		snd_soc_component_update_bits(component, WM8903_MIC_BIAS_CONTROL_0,
				    WM8903_MICDET_ENA, WM8903_MICDET_ENA);
	} else {
		snd_soc_component_update_bits(component, WM8903_MIC_BIAS_CONTROL_0,
				    WM8903_MICDET_ENA, 0);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(wm8903_mic_detect);

static irqreturn_t wm8903_irq(int irq, void *data)
{
	struct wm8903_priv *wm8903 = data;
	int mic_report, ret;
	unsigned int int_val, mask, int_pol;

	ret = regmap_read(wm8903->regmap, WM8903_INTERRUPT_STATUS_1_MASK,
			  &mask);
	if (ret != 0) {
		dev_err(wm8903->dev, "Failed to read IRQ mask: %d\n", ret);
		return IRQ_NONE;
	}

	ret = regmap_read(wm8903->regmap, WM8903_INTERRUPT_STATUS_1, &int_val);
	if (ret != 0) {
		dev_err(wm8903->dev, "Failed to read IRQ status: %d\n", ret);
		return IRQ_NONE;
	}

	int_val &= ~mask;

	if (int_val & WM8903_WSEQ_BUSY_EINT) {
		dev_warn(wm8903->dev, "Write sequencer done\n");
	}

	/*
	 * The rest is microphone jack detection.  We need to manually
	 * invert the polarity of the interrupt after each event - to
	 * simplify the code keep track of the last state we reported
	 * and just invert the relevant bits in both the report and
	 * the polarity register.
	 */
	mic_report = wm8903->mic_last_report;
	ret = regmap_read(wm8903->regmap, WM8903_INTERRUPT_POLARITY_1,
			  &int_pol);
	if (ret != 0) {
		dev_err(wm8903->dev, "Failed to read interrupt polarity: %d\n",
			ret);
		return IRQ_HANDLED;
	}

#ifndef CONFIG_SND_SOC_WM8903_MODULE
	if (int_val & (WM8903_MICSHRT_EINT | WM8903_MICDET_EINT))
		trace_snd_soc_jack_irq(dev_name(wm8903->dev));
#endif

	if (int_val & WM8903_MICSHRT_EINT) {
		dev_dbg(wm8903->dev, "Microphone short (pol=%x)\n", int_pol);

		mic_report ^= wm8903->mic_short;
		int_pol ^= WM8903_MICSHRT_INV;
	}

	if (int_val & WM8903_MICDET_EINT) {
		dev_dbg(wm8903->dev, "Microphone detect (pol=%x)\n", int_pol);

		mic_report ^= wm8903->mic_det;
		int_pol ^= WM8903_MICDET_INV;

		msleep(wm8903->mic_delay);
	}

	regmap_update_bits(wm8903->regmap, WM8903_INTERRUPT_POLARITY_1,
			   WM8903_MICSHRT_INV | WM8903_MICDET_INV, int_pol);

	snd_soc_jack_report(wm8903->mic_jack, mic_report,
			    wm8903->mic_short | wm8903->mic_det);

	wm8903->mic_last_report = mic_report;

	return IRQ_HANDLED;
}

#define WM8903_PLAYBACK_RATES (SNDRV_PCM_RATE_8000 |\
			       SNDRV_PCM_RATE_11025 |	\
			       SNDRV_PCM_RATE_16000 |	\
			       SNDRV_PCM_RATE_22050 |	\
			       SNDRV_PCM_RATE_32000 |	\
			       SNDRV_PCM_RATE_44100 |	\
			       SNDRV_PCM_RATE_48000 |	\
			       SNDRV_PCM_RATE_88200 |	\
			       SNDRV_PCM_RATE_96000)

#define WM8903_CAPTURE_RATES (SNDRV_PCM_RATE_8000 |\
			      SNDRV_PCM_RATE_11025 |	\
			      SNDRV_PCM_RATE_16000 |	\
			      SNDRV_PCM_RATE_22050 |	\
			      SNDRV_PCM_RATE_32000 |	\
			      SNDRV_PCM_RATE_44100 |	\
			      SNDRV_PCM_RATE_48000)

#define WM8903_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE)

static const struct snd_soc_dai_ops wm8903_dai_ops = {
	.hw_params	= wm8903_hw_params,
	.mute_stream	= wm8903_mute,
	.set_fmt	= wm8903_set_dai_fmt,
	.set_sysclk	= wm8903_set_dai_sysclk,
	.no_capture_mute = 1,
};

static struct snd_soc_dai_driver wm8903_dai = {
	.name = "wm8903-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = WM8903_PLAYBACK_RATES,
		.formats = WM8903_FORMATS,
	},
	.capture = {
		 .stream_name = "Capture",
		 .channels_min = 2,
		 .channels_max = 2,
		 .rates = WM8903_CAPTURE_RATES,
		 .formats = WM8903_FORMATS,
	 },
	.ops = &wm8903_dai_ops,
	.symmetric_rate = 1,
};

static int wm8903_resume(struct snd_soc_component *component)
{
	struct wm8903_priv *wm8903 = snd_soc_component_get_drvdata(component);

	regcache_sync(wm8903->regmap);

	return 0;
}

#ifdef CONFIG_GPIOLIB
static int wm8903_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	if (offset >= WM8903_NUM_GPIO)
		return -EINVAL;

	return 0;
}

static int wm8903_gpio_direction_in(struct gpio_chip *chip, unsigned offset)
{
	struct wm8903_priv *wm8903 = gpiochip_get_data(chip);
	unsigned int mask, val;
	int ret;

	mask = WM8903_GP1_FN_MASK | WM8903_GP1_DIR_MASK;
	val = (WM8903_GPn_FN_GPIO_INPUT << WM8903_GP1_FN_SHIFT) |
		WM8903_GP1_DIR;

	ret = regmap_update_bits(wm8903->regmap,
				 WM8903_GPIO_CONTROL_1 + offset, mask, val);
	if (ret < 0)
		return ret;

	return 0;
}

static int wm8903_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct wm8903_priv *wm8903 = gpiochip_get_data(chip);
	unsigned int reg;

	regmap_read(wm8903->regmap, WM8903_GPIO_CONTROL_1 + offset, &reg);

	return !!((reg & WM8903_GP1_LVL_MASK) >> WM8903_GP1_LVL_SHIFT);
}

static int wm8903_gpio_direction_out(struct gpio_chip *chip,
				     unsigned offset, int value)
{
	struct wm8903_priv *wm8903 = gpiochip_get_data(chip);
	unsigned int mask, val;
	int ret;

	mask = WM8903_GP1_FN_MASK | WM8903_GP1_DIR_MASK | WM8903_GP1_LVL_MASK;
	val = (WM8903_GPn_FN_GPIO_OUTPUT << WM8903_GP1_FN_SHIFT) |
		(value << WM8903_GP2_LVL_SHIFT);

	ret = regmap_update_bits(wm8903->regmap,
				 WM8903_GPIO_CONTROL_1 + offset, mask, val);
	if (ret < 0)
		return ret;

	return 0;
}

static void wm8903_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct wm8903_priv *wm8903 = gpiochip_get_data(chip);

	regmap_update_bits(wm8903->regmap, WM8903_GPIO_CONTROL_1 + offset,
			   WM8903_GP1_LVL_MASK,
			   !!value << WM8903_GP1_LVL_SHIFT);
}

static const struct gpio_chip wm8903_template_chip = {
	.label			= "wm8903",
	.owner			= THIS_MODULE,
	.request		= wm8903_gpio_request,
	.direction_input	= wm8903_gpio_direction_in,
	.get			= wm8903_gpio_get,
	.direction_output	= wm8903_gpio_direction_out,
	.set			= wm8903_gpio_set,
	.can_sleep		= 1,
};

static void wm8903_init_gpio(struct wm8903_priv *wm8903)
{
	struct wm8903_platform_data *pdata = wm8903->pdata;
	int ret;

	wm8903->gpio_chip = wm8903_template_chip;
	wm8903->gpio_chip.ngpio = WM8903_NUM_GPIO;
	wm8903->gpio_chip.parent = wm8903->dev;

	if (pdata->gpio_base)
		wm8903->gpio_chip.base = pdata->gpio_base;
	else
		wm8903->gpio_chip.base = -1;

	ret = gpiochip_add_data(&wm8903->gpio_chip, wm8903);
	if (ret != 0)
		dev_err(wm8903->dev, "Failed to add GPIOs: %d\n", ret);
}

static void wm8903_free_gpio(struct wm8903_priv *wm8903)
{
	gpiochip_remove(&wm8903->gpio_chip);
}
#else
static void wm8903_init_gpio(struct wm8903_priv *wm8903)
{
}

static void wm8903_free_gpio(struct wm8903_priv *wm8903)
{
}
#endif

static const struct snd_soc_component_driver soc_component_dev_wm8903 = {
	.resume			= wm8903_resume,
	.set_bias_level		= wm8903_set_bias_level,
	.seq_notifier		= wm8903_seq_notifier,
	.controls		= wm8903_snd_controls,
	.num_controls		= ARRAY_SIZE(wm8903_snd_controls),
	.dapm_widgets		= wm8903_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(wm8903_dapm_widgets),
	.dapm_routes		= wm8903_intercon,
	.num_dapm_routes	= ARRAY_SIZE(wm8903_intercon),
	.suspend_bias_off	= 1,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct regmap_config wm8903_regmap = {
	.reg_bits = 8,
	.val_bits = 16,

	.max_register = WM8903_MAX_REGISTER,
	.volatile_reg = wm8903_volatile_register,
	.readable_reg = wm8903_readable_register,

	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = wm8903_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(wm8903_reg_defaults),
};

static int wm8903_set_pdata_irq_trigger(struct i2c_client *i2c,
					struct wm8903_platform_data *pdata)
{
	struct irq_data *irq_data = irq_get_irq_data(i2c->irq);
	if (!irq_data) {
		dev_err(&i2c->dev, "Invalid IRQ: %d\n",
			i2c->irq);
		return -EINVAL;
	}

	switch (irqd_get_trigger_type(irq_data)) {
	case IRQ_TYPE_NONE:
	default:
		/*
		* We assume the controller imposes no restrictions,
		* so we are able to select active-high
		*/
		fallthrough;
	case IRQ_TYPE_LEVEL_HIGH:
		pdata->irq_active_low = false;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		pdata->irq_active_low = true;
		break;
	}

	return 0;
}

static int wm8903_set_pdata_from_of(struct i2c_client *i2c,
				    struct wm8903_platform_data *pdata)
{
	const struct device_node *np = i2c->dev.of_node;
	u32 val32;
	int i;

	if (of_property_read_u32(np, "micdet-cfg", &val32) >= 0)
		pdata->micdet_cfg = val32;

	if (of_property_read_u32(np, "micdet-delay", &val32) >= 0)
		pdata->micdet_delay = val32;

	if (of_property_read_u32_array(np, "gpio-cfg", pdata->gpio_cfg,
				       ARRAY_SIZE(pdata->gpio_cfg)) >= 0) {
		/*
		 * In device tree: 0 means "write 0",
		 * 0xffffffff means "don't touch".
		 *
		 * In platform data: 0 means "don't touch",
		 * 0x8000 means "write 0".
		 *
		 * Note: WM8903_GPIO_CONFIG_ZERO == 0x8000.
		 *
		 *  Convert from DT to pdata representation here,
		 * so no other code needs to change.
		 */
		for (i = 0; i < ARRAY_SIZE(pdata->gpio_cfg); i++) {
			if (pdata->gpio_cfg[i] == 0) {
				pdata->gpio_cfg[i] = WM8903_GPIO_CONFIG_ZERO;
			} else if (pdata->gpio_cfg[i] == 0xffffffff) {
				pdata->gpio_cfg[i] = 0;
			} else if (pdata->gpio_cfg[i] > 0x7fff) {
				dev_err(&i2c->dev, "Invalid gpio-cfg[%d] %x\n",
					i, pdata->gpio_cfg[i]);
				return -EINVAL;
			}
		}
	}

	return 0;
}

static int wm8903_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct wm8903_platform_data *pdata = dev_get_platdata(&i2c->dev);
	struct wm8903_priv *wm8903;
	int trigger;
	bool mic_gpio = false;
	unsigned int val, irq_pol;
	int ret, i;

	wm8903 = devm_kzalloc(&i2c->dev, sizeof(*wm8903), GFP_KERNEL);
	if (wm8903 == NULL)
		return -ENOMEM;

	mutex_init(&wm8903->lock);
	wm8903->dev = &i2c->dev;

	wm8903->regmap = devm_regmap_init_i2c(i2c, &wm8903_regmap);
	if (IS_ERR(wm8903->regmap)) {
		ret = PTR_ERR(wm8903->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	i2c_set_clientdata(i2c, wm8903);

	/* If no platform data was supplied, create storage for defaults */
	if (pdata) {
		wm8903->pdata = pdata;
	} else {
		wm8903->pdata = devm_kzalloc(&i2c->dev, sizeof(*wm8903->pdata),
					     GFP_KERNEL);
		if (!wm8903->pdata)
			return -ENOMEM;

		if (i2c->irq) {
			ret = wm8903_set_pdata_irq_trigger(i2c, wm8903->pdata);
			if (ret != 0)
				return ret;
		}

		if (i2c->dev.of_node) {
			ret = wm8903_set_pdata_from_of(i2c, wm8903->pdata);
			if (ret != 0)
				return ret;
		}
	}

	pdata = wm8903->pdata;

	for (i = 0; i < ARRAY_SIZE(wm8903->supplies); i++)
		wm8903->supplies[i].supply = wm8903_supply_names[i];

	ret = devm_regulator_bulk_get(&i2c->dev, ARRAY_SIZE(wm8903->supplies),
				      wm8903->supplies);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to request supplies: %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(wm8903->supplies),
				    wm8903->supplies);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to enable supplies: %d\n", ret);
		return ret;
	}

	ret = regmap_read(wm8903->regmap, WM8903_SW_RESET_AND_ID, &val);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to read chip ID: %d\n", ret);
		goto err;
	}
	if (val != 0x8903) {
		dev_err(&i2c->dev, "Device with ID %x is not a WM8903\n", val);
		ret = -ENODEV;
		goto err;
	}

	ret = regmap_read(wm8903->regmap, WM8903_REVISION_NUMBER, &val);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to read chip revision: %d\n", ret);
		goto err;
	}
	dev_info(&i2c->dev, "WM8903 revision %c\n",
		 (val & WM8903_CHIP_REV_MASK) + 'A');

	/* Reset the device */
	regmap_write(wm8903->regmap, WM8903_SW_RESET_AND_ID, 0x8903);

	wm8903_init_gpio(wm8903);

	/* Set up GPIO pin state, detect if any are MIC detect outputs */
	for (i = 0; i < ARRAY_SIZE(pdata->gpio_cfg); i++) {
		if ((!pdata->gpio_cfg[i]) ||
		    (pdata->gpio_cfg[i] > WM8903_GPIO_CONFIG_ZERO))
			continue;

		regmap_write(wm8903->regmap, WM8903_GPIO_CONTROL_1 + i,
				pdata->gpio_cfg[i] & 0x7fff);

		val = (pdata->gpio_cfg[i] & WM8903_GP1_FN_MASK)
			>> WM8903_GP1_FN_SHIFT;

		switch (val) {
		case WM8903_GPn_FN_MICBIAS_CURRENT_DETECT:
		case WM8903_GPn_FN_MICBIAS_SHORT_DETECT:
			mic_gpio = true;
			break;
		default:
			break;
		}
	}

	/* Set up microphone detection */
	regmap_write(wm8903->regmap, WM8903_MIC_BIAS_CONTROL_0,
		     pdata->micdet_cfg);

	/* Microphone detection needs the WSEQ clock */
	if (pdata->micdet_cfg)
		regmap_update_bits(wm8903->regmap, WM8903_WRITE_SEQUENCER_0,
				   WM8903_WSEQ_ENA, WM8903_WSEQ_ENA);

	/* If microphone detection is enabled by pdata but
	 * detected via IRQ then interrupts can be lost before
	 * the machine driver has set up microphone detection
	 * IRQs as the IRQs are clear on read.  The detection
	 * will be enabled when the machine driver configures.
	 */
	WARN_ON(!mic_gpio && (pdata->micdet_cfg & WM8903_MICDET_ENA));

	wm8903->mic_delay = pdata->micdet_delay;

	if (i2c->irq) {
		if (pdata->irq_active_low) {
			trigger = IRQF_TRIGGER_LOW;
			irq_pol = WM8903_IRQ_POL;
		} else {
			trigger = IRQF_TRIGGER_HIGH;
			irq_pol = 0;
		}

		regmap_update_bits(wm8903->regmap, WM8903_INTERRUPT_CONTROL,
				   WM8903_IRQ_POL, irq_pol);

		ret = request_threaded_irq(i2c->irq, NULL, wm8903_irq,
					   trigger | IRQF_ONESHOT,
					   "wm8903", wm8903);
		if (ret != 0) {
			dev_err(wm8903->dev, "Failed to request IRQ: %d\n",
				ret);
			return ret;
		}

		/* Enable write sequencer interrupts */
		regmap_update_bits(wm8903->regmap,
				   WM8903_INTERRUPT_STATUS_1_MASK,
				   WM8903_IM_WSEQ_BUSY_EINT, 0);
	}

	/* Latch volume update bits */
	regmap_update_bits(wm8903->regmap, WM8903_ADC_DIGITAL_VOLUME_LEFT,
			   WM8903_ADCVU, WM8903_ADCVU);
	regmap_update_bits(wm8903->regmap, WM8903_ADC_DIGITAL_VOLUME_RIGHT,
			   WM8903_ADCVU, WM8903_ADCVU);

	regmap_update_bits(wm8903->regmap, WM8903_DAC_DIGITAL_VOLUME_LEFT,
			   WM8903_DACVU, WM8903_DACVU);
	regmap_update_bits(wm8903->regmap, WM8903_DAC_DIGITAL_VOLUME_RIGHT,
			   WM8903_DACVU, WM8903_DACVU);

	regmap_update_bits(wm8903->regmap, WM8903_ANALOGUE_OUT1_LEFT,
			   WM8903_HPOUTVU, WM8903_HPOUTVU);
	regmap_update_bits(wm8903->regmap, WM8903_ANALOGUE_OUT1_RIGHT,
			   WM8903_HPOUTVU, WM8903_HPOUTVU);

	regmap_update_bits(wm8903->regmap, WM8903_ANALOGUE_OUT2_LEFT,
			   WM8903_LINEOUTVU, WM8903_LINEOUTVU);
	regmap_update_bits(wm8903->regmap, WM8903_ANALOGUE_OUT2_RIGHT,
			   WM8903_LINEOUTVU, WM8903_LINEOUTVU);

	regmap_update_bits(wm8903->regmap, WM8903_ANALOGUE_OUT3_LEFT,
			   WM8903_SPKVU, WM8903_SPKVU);
	regmap_update_bits(wm8903->regmap, WM8903_ANALOGUE_OUT3_RIGHT,
			   WM8903_SPKVU, WM8903_SPKVU);

	/* Enable DAC soft mute by default */
	regmap_update_bits(wm8903->regmap, WM8903_DAC_DIGITAL_1,
			   WM8903_DAC_MUTEMODE | WM8903_DAC_MUTE,
			   WM8903_DAC_MUTEMODE | WM8903_DAC_MUTE);

	ret = devm_snd_soc_register_component(&i2c->dev,
			&soc_component_dev_wm8903, &wm8903_dai, 1);
	if (ret != 0)
		goto err;

	return 0;
err:
	regulator_bulk_disable(ARRAY_SIZE(wm8903->supplies),
			       wm8903->supplies);
	return ret;
}

static int wm8903_i2c_remove(struct i2c_client *client)
{
	struct wm8903_priv *wm8903 = i2c_get_clientdata(client);

	regulator_bulk_disable(ARRAY_SIZE(wm8903->supplies),
			       wm8903->supplies);
	if (client->irq)
		free_irq(client->irq, wm8903);
	wm8903_free_gpio(wm8903);

	return 0;
}

static const struct of_device_id wm8903_of_match[] = {
	{ .compatible = "wlf,wm8903", },
	{},
};
MODULE_DEVICE_TABLE(of, wm8903_of_match);

static const struct i2c_device_id wm8903_i2c_id[] = {
	{ "wm8903", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8903_i2c_id);

static struct i2c_driver wm8903_i2c_driver = {
	.driver = {
		.name = "wm8903",
		.of_match_table = wm8903_of_match,
	},
	.probe =    wm8903_i2c_probe,
	.remove =   wm8903_i2c_remove,
	.id_table = wm8903_i2c_id,
};

module_i2c_driver(wm8903_i2c_driver);

MODULE_DESCRIPTION("ASoC WM8903 driver");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.cm>");
MODULE_LICENSE("GPL");
