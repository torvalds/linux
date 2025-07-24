// SPDX-License-Identifier: GPL-2.0
//
// CS42L43 CODEC driver jack handling
//
// Copyright (C) 2022-2023 Cirrus Logic, Inc. and
//                         Cirrus Logic International Semiconductor Ltd.

#include <linux/build_bug.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/irq.h>
#include <linux/jiffies.h>
#include <linux/mfd/cs42l43.h>
#include <linux/mfd/cs42l43-regs.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/time.h>
#include <linux/workqueue.h>
#include <sound/control.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc-component.h>
#include <sound/soc-jack.h>
#include <sound/soc.h>

#include "cs42l43.h"

static const unsigned int cs42l43_accdet_us[] = {
	20, 100, 1000, 10000, 50000, 75000, 100000, 200000,
};

static const unsigned int cs42l43_accdet_db_ms[] = {
	0, 125, 250, 500, 750, 1000, 1250, 1500,
};

static const unsigned int cs42l43_accdet_ramp_ms[] = { 10, 40, 90, 170 };

static const unsigned int cs42l43_accdet_bias_sense[] = {
	14, 24, 43, 52, 61, 71, 90, 99, 0,
};

static int cs42l43_find_index(struct cs42l43_codec *priv, const char * const prop,
			      unsigned int defval, unsigned int *val,
			      const unsigned int *values, const int nvalues)
{
	struct cs42l43 *cs42l43 = priv->core;
	int i, ret;

	ret = device_property_read_u32(cs42l43->dev, prop, &defval);
	if (ret != -EINVAL && ret < 0) {
		dev_err(priv->dev, "Property %s malformed: %d\n", prop, ret);
		return ret;
	}

	if (val)
		*val = defval;

	for (i = 0; i < nvalues; i++)
		if (defval == values[i])
			return i;

	dev_err(priv->dev, "Invalid value for property %s: %d\n", prop, defval);
	return -EINVAL;
}

int cs42l43_set_jack(struct snd_soc_component *component,
		     struct snd_soc_jack *jack, void *d)
{
	struct cs42l43_codec *priv = snd_soc_component_get_drvdata(component);
	struct cs42l43 *cs42l43 = priv->core;
	/* This tip sense invert is always set, HW wants an inverted signal */
	unsigned int tip_deb = CS42L43_TIPSENSE_INV_MASK;
	unsigned int hs2 = 0x2 << CS42L43_HSDET_MODE_SHIFT;
	unsigned int autocontrol = 0, pdncntl = 0;
	int ret;

	dev_dbg(priv->dev, "Configure accessory detect\n");

	ret = pm_runtime_resume_and_get(priv->dev);
	if (ret) {
		dev_err(priv->dev, "Failed to resume for jack config: %d\n", ret);
		return ret;
	}

	mutex_lock(&priv->jack_lock);

	priv->jack_hp = jack;

	if (!jack)
		goto done;

	ret = device_property_count_u32(cs42l43->dev, "cirrus,buttons-ohms");
	if (ret != -EINVAL) {
		if (ret < 0) {
			dev_err(priv->dev, "Property cirrus,buttons-ohms malformed: %d\n",
				ret);
			goto error;
		}

		if (ret > CS42L43_N_BUTTONS) {
			ret = -EINVAL;
			dev_err(priv->dev, "Property cirrus,buttons-ohms too many entries\n");
			goto error;
		}

		ret = device_property_read_u32_array(cs42l43->dev, "cirrus,buttons-ohms",
						     priv->buttons, ret);
		if (ret < 0) {
			dev_err(priv->dev, "Property cirrus,button-ohms malformed: %d\n",
				ret);
			goto error;
		}
	} else {
		priv->buttons[0] = 70;
		priv->buttons[1] = 185;
		priv->buttons[2] = 355;
		priv->buttons[3] = 735;
	}

	ret = cs42l43_find_index(priv, "cirrus,detect-us", 50000, &priv->detect_us,
				 cs42l43_accdet_us, ARRAY_SIZE(cs42l43_accdet_us));
	if (ret < 0)
		goto error;

	hs2 |= ret << CS42L43_AUTO_HSDET_TIME_SHIFT;

	priv->bias_low = device_property_read_bool(cs42l43->dev, "cirrus,bias-low");

	ret = cs42l43_find_index(priv, "cirrus,bias-ramp-ms", 170,
				 &priv->bias_ramp_ms, cs42l43_accdet_ramp_ms,
				 ARRAY_SIZE(cs42l43_accdet_ramp_ms));
	if (ret < 0)
		goto error;

	hs2 |= ret << CS42L43_HSBIAS_RAMP_SHIFT;

	ret = cs42l43_find_index(priv, "cirrus,bias-sense-microamp", 14,
				 &priv->bias_sense_ua, cs42l43_accdet_bias_sense,
				 ARRAY_SIZE(cs42l43_accdet_bias_sense));
	if (ret < 0)
		goto error;

	if (priv->bias_sense_ua)
		autocontrol |= ret << CS42L43_HSBIAS_SENSE_TRIP_SHIFT;

	if (!device_property_read_bool(cs42l43->dev, "cirrus,button-automute"))
		autocontrol |= CS42L43_S0_AUTO_ADCMUTE_DISABLE_MASK;

	ret = device_property_read_u32(cs42l43->dev, "cirrus,tip-debounce-ms",
				       &priv->tip_debounce_ms);
	if (ret < 0 && ret != -EINVAL) {
		dev_err(priv->dev, "Property cirrus,tip-debounce-ms malformed: %d\n", ret);
		goto error;
	}

	/* This tip sense invert is set normally, as TIPSENSE_INV already inverted */
	if (device_property_read_bool(cs42l43->dev, "cirrus,tip-invert"))
		autocontrol |= 0x1 << CS42L43_JACKDET_INV_SHIFT;

	if (device_property_read_bool(cs42l43->dev, "cirrus,tip-disable-pullup"))
		autocontrol |= 0x1 << CS42L43_JACKDET_MODE_SHIFT;
	else
		autocontrol |= 0x3 << CS42L43_JACKDET_MODE_SHIFT;

	ret = cs42l43_find_index(priv, "cirrus,tip-fall-db-ms", 500,
				 &priv->tip_fall_db_ms, cs42l43_accdet_db_ms,
				 ARRAY_SIZE(cs42l43_accdet_db_ms));
	if (ret < 0)
		goto error;

	tip_deb |= ret << CS42L43_TIPSENSE_FALLING_DB_TIME_SHIFT;

	ret = cs42l43_find_index(priv, "cirrus,tip-rise-db-ms", 500,
				 &priv->tip_rise_db_ms, cs42l43_accdet_db_ms,
				 ARRAY_SIZE(cs42l43_accdet_db_ms));
	if (ret < 0)
		goto error;

	tip_deb |= ret << CS42L43_TIPSENSE_RISING_DB_TIME_SHIFT;

	if (device_property_read_bool(cs42l43->dev, "cirrus,use-ring-sense")) {
		unsigned int ring_deb = 0;

		priv->use_ring_sense = true;

		/* HW wants an inverted signal, so invert the invert */
		if (!device_property_read_bool(cs42l43->dev, "cirrus,ring-invert"))
			ring_deb |= CS42L43_RINGSENSE_INV_MASK;

		if (!device_property_read_bool(cs42l43->dev,
					       "cirrus,ring-disable-pullup"))
			ring_deb |= CS42L43_RINGSENSE_PULLUP_PDNB_MASK;

		ret = cs42l43_find_index(priv, "cirrus,ring-fall-db-ms", 500,
					 NULL, cs42l43_accdet_db_ms,
					 ARRAY_SIZE(cs42l43_accdet_db_ms));
		if (ret < 0)
			goto error;

		ring_deb |= ret << CS42L43_RINGSENSE_FALLING_DB_TIME_SHIFT;

		ret = cs42l43_find_index(priv, "cirrus,ring-rise-db-ms", 500,
					 NULL, cs42l43_accdet_db_ms,
					 ARRAY_SIZE(cs42l43_accdet_db_ms));
		if (ret < 0)
			goto error;

		ring_deb |= ret << CS42L43_RINGSENSE_RISING_DB_TIME_SHIFT;
		pdncntl |= CS42L43_RING_SENSE_EN_MASK;

		regmap_update_bits(cs42l43->regmap, CS42L43_RINGSENSE_DEB_CTRL,
				   CS42L43_RINGSENSE_INV_MASK |
				   CS42L43_RINGSENSE_PULLUP_PDNB_MASK |
				   CS42L43_RINGSENSE_FALLING_DB_TIME_MASK |
				   CS42L43_RINGSENSE_RISING_DB_TIME_MASK,
				   ring_deb);
	}

	regmap_update_bits(cs42l43->regmap, CS42L43_TIPSENSE_DEB_CTRL,
			   CS42L43_TIPSENSE_INV_MASK |
			   CS42L43_TIPSENSE_FALLING_DB_TIME_MASK |
			   CS42L43_TIPSENSE_RISING_DB_TIME_MASK, tip_deb);
	regmap_update_bits(cs42l43->regmap, CS42L43_HS2,
			   CS42L43_HSBIAS_RAMP_MASK | CS42L43_HSDET_MODE_MASK |
			   CS42L43_AUTO_HSDET_TIME_MASK, hs2);

done:
	ret = 0;

	regmap_update_bits(cs42l43->regmap, CS42L43_HS_BIAS_SENSE_AND_CLAMP_AUTOCONTROL,
			   CS42L43_JACKDET_MODE_MASK | CS42L43_S0_AUTO_ADCMUTE_DISABLE_MASK |
			   CS42L43_HSBIAS_SENSE_TRIP_MASK, autocontrol);
	regmap_update_bits(cs42l43->regmap, CS42L43_PDNCNTL,
			   CS42L43_RING_SENSE_EN_MASK, pdncntl);

	dev_dbg(priv->dev, "Successfully configured accessory detect\n");

error:
	mutex_unlock(&priv->jack_lock);

	pm_runtime_put_autosuspend(priv->dev);

	return ret;
}

static void cs42l43_start_hs_bias(struct cs42l43_codec *priv, bool type_detect)
{
	struct cs42l43 *cs42l43 = priv->core;
	unsigned int val = 0x3 << CS42L43_HSBIAS_MODE_SHIFT;

	dev_dbg(priv->dev, "Start headset bias\n");

	regmap_update_bits(cs42l43->regmap, CS42L43_HS2,
			   CS42L43_HS_CLAMP_DISABLE_MASK, CS42L43_HS_CLAMP_DISABLE_MASK);

	if (!type_detect) {
		if (priv->bias_low)
			val = 0x2 << CS42L43_HSBIAS_MODE_SHIFT;

		if (priv->bias_sense_ua)
			regmap_update_bits(cs42l43->regmap,
					   CS42L43_HS_BIAS_SENSE_AND_CLAMP_AUTOCONTROL,
					   CS42L43_HSBIAS_SENSE_EN_MASK |
					   CS42L43_AUTO_HSBIAS_CLAMP_EN_MASK,
					   CS42L43_HSBIAS_SENSE_EN_MASK |
					   CS42L43_AUTO_HSBIAS_CLAMP_EN_MASK);
	}

	regmap_update_bits(cs42l43->regmap, CS42L43_MIC_DETECT_CONTROL_1,
			   CS42L43_HSBIAS_MODE_MASK, val);

	msleep(priv->bias_ramp_ms);
}

static void cs42l43_stop_hs_bias(struct cs42l43_codec *priv)
{
	struct cs42l43 *cs42l43 = priv->core;

	dev_dbg(priv->dev, "Stop headset bias\n");

	regmap_update_bits(cs42l43->regmap, CS42L43_MIC_DETECT_CONTROL_1,
			   CS42L43_HSBIAS_MODE_MASK, 0x1 << CS42L43_HSBIAS_MODE_SHIFT);

	regmap_update_bits(cs42l43->regmap, CS42L43_HS2,
			   CS42L43_HS_CLAMP_DISABLE_MASK, 0);

	if (priv->bias_sense_ua) {
		regmap_update_bits(cs42l43->regmap,
				   CS42L43_HS_BIAS_SENSE_AND_CLAMP_AUTOCONTROL,
				   CS42L43_HSBIAS_SENSE_EN_MASK |
				   CS42L43_AUTO_HSBIAS_CLAMP_EN_MASK, 0);
	}
}

irqreturn_t cs42l43_bias_detect_clamp(int irq, void *data)
{
	struct cs42l43_codec *priv = data;

	queue_delayed_work(system_wq, &priv->bias_sense_timeout,
			   msecs_to_jiffies(1000));

	return IRQ_HANDLED;
}

#define CS42L43_JACK_PRESENT 0x3
#define CS42L43_JACK_ABSENT 0x0

#define CS42L43_JACK_OPTICAL (SND_JACK_MECHANICAL | SND_JACK_AVOUT)
#define CS42L43_JACK_HEADPHONE (SND_JACK_MECHANICAL | SND_JACK_HEADPHONE)
#define CS42L43_JACK_HEADSET (SND_JACK_MECHANICAL | SND_JACK_HEADSET)
#define CS42L43_JACK_LINEOUT (SND_JACK_MECHANICAL | SND_JACK_LINEOUT)
#define CS42L43_JACK_LINEIN (SND_JACK_MECHANICAL | SND_JACK_LINEIN)
#define CS42L43_JACK_EXTENSION (SND_JACK_MECHANICAL)
#define CS42L43_JACK_BUTTONS (SND_JACK_BTN_0 | SND_JACK_BTN_1 | SND_JACK_BTN_2 | \
			      SND_JACK_BTN_3 | SND_JACK_BTN_4 | SND_JACK_BTN_5)

static inline bool cs42l43_jack_present(struct cs42l43_codec *priv)
{
	struct cs42l43 *cs42l43 = priv->core;
	unsigned int sts = 0;

	regmap_read(cs42l43->regmap, CS42L43_TIP_RING_SENSE_INTERRUPT_STATUS, &sts);

	sts = (sts >> CS42L43_TIPSENSE_PLUG_DB_STS_SHIFT) & CS42L43_JACK_PRESENT;

	return sts == CS42L43_JACK_PRESENT;
}

static void cs42l43_start_button_detect(struct cs42l43_codec *priv)
{
	struct cs42l43 *cs42l43 = priv->core;
	unsigned int val = 0x3 << CS42L43_BUTTON_DETECT_MODE_SHIFT;

	dev_dbg(priv->dev, "Start button detect\n");

	priv->button_detect_running = true;

	if (priv->bias_low)
		val = 0x1 << CS42L43_BUTTON_DETECT_MODE_SHIFT;

	regmap_update_bits(cs42l43->regmap, CS42L43_MIC_DETECT_CONTROL_1,
			   CS42L43_BUTTON_DETECT_MODE_MASK |
			   CS42L43_MIC_LVL_DET_DISABLE_MASK, val);
}

static void cs42l43_stop_button_detect(struct cs42l43_codec *priv)
{
	struct cs42l43 *cs42l43 = priv->core;

	dev_dbg(priv->dev, "Stop button detect\n");

	regmap_update_bits(cs42l43->regmap, CS42L43_MIC_DETECT_CONTROL_1,
			   CS42L43_BUTTON_DETECT_MODE_MASK |
			   CS42L43_MIC_LVL_DET_DISABLE_MASK,
			   CS42L43_MIC_LVL_DET_DISABLE_MASK);

	priv->button_detect_running = false;
}

#define CS42L43_BUTTON_COMB_MAX 512
#define CS42L43_BUTTON_ROUT 2210

void cs42l43_button_press_work(struct work_struct *work)
{
	struct cs42l43_codec *priv = container_of(work, struct cs42l43_codec,
						  button_press_work.work);
	struct cs42l43 *cs42l43 = priv->core;
	unsigned int buttons = 0;
	unsigned int val = 0;
	int i, ret;

	ret = pm_runtime_resume_and_get(priv->dev);
	if (ret) {
		dev_err(priv->dev, "Failed to resume for button press: %d\n", ret);
		return;
	}

	mutex_lock(&priv->jack_lock);

	if (!priv->button_detect_running) {
		dev_dbg(priv->dev, "Spurious button press IRQ\n");
		goto error;
	}

	regmap_read(cs42l43->regmap, CS42L43_DETECT_STATUS_1, &val);

	/* Bail if jack removed, the button is irrelevant and likely invalid */
	if (!cs42l43_jack_present(priv)) {
		dev_dbg(priv->dev, "Button ignored due to removal\n");
		goto error;
	}

	if (val & CS42L43_HSBIAS_CLAMP_STS_MASK) {
		dev_dbg(priv->dev, "Button ignored due to bias sense\n");
		goto error;
	}

	val = (val & CS42L43_HSDET_DC_STS_MASK) >> CS42L43_HSDET_DC_STS_SHIFT;
	val = ((CS42L43_BUTTON_COMB_MAX << 20) / (val + 1)) - (1 << 20);
	if (val)
		val = (CS42L43_BUTTON_ROUT << 20) / val;
	else
		val = UINT_MAX;

	for (i = 0; i < CS42L43_N_BUTTONS; i++) {
		if (val < priv->buttons[i]) {
			buttons = SND_JACK_BTN_0 >> i;
			dev_dbg(priv->dev, "Detected button %d at %d Ohms\n", i, val);
			break;
		}
	}

	if (!buttons)
		dev_dbg(priv->dev, "Unrecognised button: %d Ohms\n", val);

	snd_soc_jack_report(priv->jack_hp, buttons, CS42L43_JACK_BUTTONS);

error:
	mutex_unlock(&priv->jack_lock);

	pm_runtime_put_autosuspend(priv->dev);
}

irqreturn_t cs42l43_button_press(int irq, void *data)
{
	struct cs42l43_codec *priv = data;

	// Wait for 2 full cycles of comb filter to ensure good reading
	queue_delayed_work(system_wq, &priv->button_press_work,
			   msecs_to_jiffies(20));

	return IRQ_HANDLED;
}

void cs42l43_button_release_work(struct work_struct *work)
{
	struct cs42l43_codec *priv = container_of(work, struct cs42l43_codec,
						  button_release_work);
	int ret;

	ret = pm_runtime_resume_and_get(priv->dev);
	if (ret) {
		dev_err(priv->dev, "Failed to resume for button release: %d\n", ret);
		return;
	}

	mutex_lock(&priv->jack_lock);

	if (priv->button_detect_running) {
		dev_dbg(priv->dev, "Button release IRQ\n");

		snd_soc_jack_report(priv->jack_hp, 0, CS42L43_JACK_BUTTONS);
	} else {
		dev_dbg(priv->dev, "Spurious button release IRQ\n");
	}

	mutex_unlock(&priv->jack_lock);

	pm_runtime_put_autosuspend(priv->dev);
}

irqreturn_t cs42l43_button_release(int irq, void *data)
{
	struct cs42l43_codec *priv = data;

	queue_work(system_wq, &priv->button_release_work);

	return IRQ_HANDLED;
}

void cs42l43_bias_sense_timeout(struct work_struct *work)
{
	struct cs42l43_codec *priv = container_of(work, struct cs42l43_codec,
						  bias_sense_timeout.work);
	struct cs42l43 *cs42l43 = priv->core;
	int ret;

	ret = pm_runtime_resume_and_get(priv->dev);
	if (ret) {
		dev_err(priv->dev, "Failed to resume for bias sense: %d\n", ret);
		return;
	}

	mutex_lock(&priv->jack_lock);

	if (cs42l43_jack_present(priv) && priv->button_detect_running) {
		dev_dbg(priv->dev, "Bias sense timeout out, restore bias\n");

		regmap_update_bits(cs42l43->regmap,
				   CS42L43_HS_BIAS_SENSE_AND_CLAMP_AUTOCONTROL,
				   CS42L43_AUTO_HSBIAS_CLAMP_EN_MASK, 0);
		regmap_update_bits(cs42l43->regmap,
				   CS42L43_HS_BIAS_SENSE_AND_CLAMP_AUTOCONTROL,
				   CS42L43_AUTO_HSBIAS_CLAMP_EN_MASK,
				   CS42L43_AUTO_HSBIAS_CLAMP_EN_MASK);
	}

	mutex_unlock(&priv->jack_lock);

	pm_runtime_put_autosuspend(priv->dev);
}

static void cs42l43_start_load_detect(struct cs42l43_codec *priv)
{
	struct cs42l43 *cs42l43 = priv->core;

	dev_dbg(priv->dev, "Start load detect\n");

	snd_soc_dapm_mutex_lock(snd_soc_component_get_dapm(priv->component));

	priv->load_detect_running = true;

	if (priv->hp_ena && !priv->hp_ilimited) {
		unsigned long time_left;

		reinit_completion(&priv->hp_shutdown);

		regmap_update_bits(cs42l43->regmap, CS42L43_BLOCK_EN8,
				   CS42L43_HP_EN_MASK, 0);

		time_left = wait_for_completion_timeout(&priv->hp_shutdown,
							msecs_to_jiffies(CS42L43_HP_TIMEOUT_MS));
		if (!time_left)
			dev_err(priv->dev, "Load detect HP power down timed out\n");
	}

	regmap_update_bits(cs42l43->regmap, CS42L43_BLOCK_EN3,
			   CS42L43_ADC1_EN_MASK | CS42L43_ADC2_EN_MASK, 0);
	regmap_update_bits(cs42l43->regmap, CS42L43_DACCNFG2, CS42L43_HP_HPF_EN_MASK, 0);
	regmap_update_bits(cs42l43->regmap, CS42L43_MIC_DETECT_CONTROL_1,
			   CS42L43_HSBIAS_MODE_MASK, 0);
	regmap_update_bits(cs42l43->regmap, CS42L43_CTRL,
			   CS42L43_ADPTPWR_MODE_MASK, 0x4 << CS42L43_ADPTPWR_MODE_SHIFT);
	regmap_update_bits(cs42l43->regmap, CS42L43_PGAVOL,
			   CS42L43_HP_DIG_VOL_RAMP_MASK | CS42L43_HP_ANA_VOL_RAMP_MASK, 0x6);
	regmap_update_bits(cs42l43->regmap, CS42L43_DACCNFG1,
			   CS42L43_HP_MSTR_VOL_CTRL_EN_MASK, 0);

	regmap_update_bits(cs42l43->regmap, CS42L43_HS2,
			   CS42L43_HS_CLAMP_DISABLE_MASK, CS42L43_HS_CLAMP_DISABLE_MASK);

	regmap_update_bits(cs42l43->regmap, CS42L43_LOADDETENA,
			   CS42L43_HPLOAD_DET_EN_MASK,
			   CS42L43_HPLOAD_DET_EN_MASK);

	snd_soc_dapm_mutex_unlock(snd_soc_component_get_dapm(priv->component));
}

static void cs42l43_stop_load_detect(struct cs42l43_codec *priv)
{
	struct cs42l43 *cs42l43 = priv->core;

	dev_dbg(priv->dev, "Stop load detect\n");

	snd_soc_dapm_mutex_lock(snd_soc_component_get_dapm(priv->component));

	regmap_update_bits(cs42l43->regmap, CS42L43_LOADDETENA,
			   CS42L43_HPLOAD_DET_EN_MASK, 0);
	regmap_update_bits(cs42l43->regmap, CS42L43_HS2,
			   CS42L43_HS_CLAMP_DISABLE_MASK, 0);
	regmap_update_bits(cs42l43->regmap, CS42L43_DACCNFG1,
			   CS42L43_HP_MSTR_VOL_CTRL_EN_MASK,
			   CS42L43_HP_MSTR_VOL_CTRL_EN_MASK);
	regmap_update_bits(cs42l43->regmap, CS42L43_PGAVOL,
			   CS42L43_HP_DIG_VOL_RAMP_MASK | CS42L43_HP_ANA_VOL_RAMP_MASK,
			   0x4 << CS42L43_HP_DIG_VOL_RAMP_SHIFT);
	regmap_update_bits(cs42l43->regmap, CS42L43_CTRL,
			   CS42L43_ADPTPWR_MODE_MASK, 0x7 << CS42L43_ADPTPWR_MODE_SHIFT);
	regmap_update_bits(cs42l43->regmap, CS42L43_MIC_DETECT_CONTROL_1,
			   CS42L43_HSBIAS_MODE_MASK, 0x1 << CS42L43_HSBIAS_MODE_SHIFT);
	regmap_update_bits(cs42l43->regmap, CS42L43_DACCNFG2,
			   CS42L43_HP_HPF_EN_MASK, CS42L43_HP_HPF_EN_MASK);

	regmap_update_bits(cs42l43->regmap, CS42L43_BLOCK_EN3,
			   CS42L43_ADC1_EN_MASK | CS42L43_ADC2_EN_MASK,
			   priv->adc_ena);

	if (priv->hp_ena && !priv->hp_ilimited) {
		unsigned long time_left;

		reinit_completion(&priv->hp_startup);

		regmap_update_bits(cs42l43->regmap, CS42L43_BLOCK_EN8,
				   CS42L43_HP_EN_MASK, priv->hp_ena);

		time_left = wait_for_completion_timeout(&priv->hp_startup,
							msecs_to_jiffies(CS42L43_HP_TIMEOUT_MS));
		if (!time_left)
			dev_err(priv->dev, "Load detect HP restore timed out\n");
	}

	priv->load_detect_running = false;

	snd_soc_dapm_mutex_unlock(snd_soc_component_get_dapm(priv->component));
}

static int cs42l43_run_load_detect(struct cs42l43_codec *priv, bool mic)
{
	struct cs42l43 *cs42l43 = priv->core;
	unsigned int val = 0;
	unsigned long time_left;

	reinit_completion(&priv->load_detect);

	cs42l43_start_load_detect(priv);
	time_left = wait_for_completion_timeout(&priv->load_detect,
						msecs_to_jiffies(CS42L43_LOAD_TIMEOUT_MS));
	cs42l43_stop_load_detect(priv);

	if (!time_left)
		return -ETIMEDOUT;

	regmap_read(cs42l43->regmap, CS42L43_LOADDETRESULTS, &val);

	dev_dbg(priv->dev, "Headphone load detect: 0x%x\n", val);

	/* Bail if jack removed, the load is irrelevant and likely invalid */
	if (!cs42l43_jack_present(priv))
		return -ENODEV;

	if (mic) {
		cs42l43_start_hs_bias(priv, false);
		cs42l43_start_button_detect(priv);

		return CS42L43_JACK_HEADSET;
	}

	switch (val & CS42L43_AMP3_RES_DET_MASK) {
	case 0x0: // low impedance
	case 0x1: // high impedance
		return CS42L43_JACK_HEADPHONE;
	case 0x2: // lineout
	case 0x3: // Open circuit
		return CS42L43_JACK_LINEOUT;
	default:
		return -EINVAL;
	}
}

static int cs42l43_run_type_detect(struct cs42l43_codec *priv)
{
	struct cs42l43 *cs42l43 = priv->core;
	int timeout_ms = ((2 * priv->detect_us) / USEC_PER_MSEC) + 200;
	unsigned int type = 0xff;
	unsigned long time_left;

	reinit_completion(&priv->type_detect);

	regmap_update_bits(cs42l43->regmap, CS42L43_STEREO_MIC_CLAMP_CTRL,
			   CS42L43_SMIC_HPAMP_CLAMP_DIS_FRC_VAL_MASK,
			   CS42L43_SMIC_HPAMP_CLAMP_DIS_FRC_VAL_MASK);

	cs42l43_start_hs_bias(priv, true);
	regmap_update_bits(cs42l43->regmap, CS42L43_HS2,
			   CS42L43_HSDET_MODE_MASK, 0x3 << CS42L43_HSDET_MODE_SHIFT);

	time_left = wait_for_completion_timeout(&priv->type_detect,
						msecs_to_jiffies(timeout_ms));

	regmap_update_bits(cs42l43->regmap, CS42L43_HS2,
			   CS42L43_HSDET_MODE_MASK, 0x2 << CS42L43_HSDET_MODE_SHIFT);
	cs42l43_stop_hs_bias(priv);

	regmap_update_bits(cs42l43->regmap, CS42L43_STEREO_MIC_CLAMP_CTRL,
			   CS42L43_SMIC_HPAMP_CLAMP_DIS_FRC_VAL_MASK, 0);

	if (!time_left)
		return -ETIMEDOUT;

	regmap_read(cs42l43->regmap, CS42L43_HS_STAT, &type);

	dev_dbg(priv->dev, "Type detect: 0x%x\n", type);

	/* Bail if jack removed, the type is irrelevant and likely invalid */
	if (!cs42l43_jack_present(priv))
		return -ENODEV;

	switch (type & CS42L43_HSDET_TYPE_STS_MASK) {
	case 0x0: // CTIA
	case 0x1: // OMTP
		return cs42l43_run_load_detect(priv, true);
	case 0x2: // 3-pole
		return cs42l43_run_load_detect(priv, false);
	case 0x3: // Open-circuit
		return CS42L43_JACK_EXTENSION;
	default:
		return -EINVAL;
	}
}

static void cs42l43_clear_jack(struct cs42l43_codec *priv)
{
	struct cs42l43 *cs42l43 = priv->core;

	cs42l43_stop_button_detect(priv);
	cs42l43_stop_hs_bias(priv);

	regmap_update_bits(cs42l43->regmap, CS42L43_ADC_B_CTRL1,
			   CS42L43_PGA_WIDESWING_MODE_EN_MASK, 0);
	regmap_update_bits(cs42l43->regmap, CS42L43_ADC_B_CTRL2,
			   CS42L43_PGA_WIDESWING_MODE_EN_MASK, 0);
	regmap_update_bits(cs42l43->regmap, CS42L43_STEREO_MIC_CTRL,
			   CS42L43_JACK_STEREO_CONFIG_MASK, 0);
	regmap_update_bits(cs42l43->regmap, CS42L43_STEREO_MIC_CLAMP_CTRL,
			   CS42L43_SMIC_HPAMP_CLAMP_DIS_FRC_MASK,
			   CS42L43_SMIC_HPAMP_CLAMP_DIS_FRC_MASK);
	regmap_update_bits(cs42l43->regmap, CS42L43_HS2,
			   CS42L43_HSDET_MODE_MASK | CS42L43_HSDET_MANUAL_MODE_MASK,
			   0x2 << CS42L43_HSDET_MODE_SHIFT);

	snd_soc_jack_report(priv->jack_hp, 0, 0xFFFF);
}

void cs42l43_tip_sense_work(struct work_struct *work)
{
	struct cs42l43_codec *priv = container_of(work, struct cs42l43_codec,
						  tip_sense_work.work);
	struct cs42l43 *cs42l43 = priv->core;
	unsigned int sts = 0;
	unsigned int tip, ring;
	int ret, report;

	ret = pm_runtime_resume_and_get(priv->dev);
	if (ret) {
		dev_err(priv->dev, "Failed to resume for tip work: %d\n", ret);
		return;
	}

	mutex_lock(&priv->jack_lock);

	regmap_read(cs42l43->regmap, CS42L43_TIP_RING_SENSE_INTERRUPT_STATUS, &sts);

	dev_dbg(priv->dev, "Tip sense: 0x%x\n", sts);

	tip = (sts >> CS42L43_TIPSENSE_PLUG_DB_STS_SHIFT) & CS42L43_JACK_PRESENT;
	ring = (sts >> CS42L43_RINGSENSE_PLUG_DB_STS_SHIFT) & CS42L43_JACK_PRESENT;

	if (tip == CS42L43_JACK_PRESENT) {
		if (cs42l43->sdw && !priv->jack_present) {
			priv->jack_present = true;
			pm_runtime_get(priv->dev);
		}

		if (priv->use_ring_sense && ring == CS42L43_JACK_ABSENT) {
			report = CS42L43_JACK_OPTICAL;
		} else {
			report = cs42l43_run_type_detect(priv);
			if (report < 0) {
				dev_err(priv->dev, "Jack detect failed: %d\n", report);
				goto error;
			}
		}

		snd_soc_jack_report(priv->jack_hp, report, report);
	} else {
		priv->jack_override = 0;

		cs42l43_clear_jack(priv);

		if (cs42l43->sdw && priv->jack_present) {
			pm_runtime_put(priv->dev);
			priv->jack_present = false;
		}
	}

error:
	mutex_unlock(&priv->jack_lock);

	priv->suspend_jack_debounce = false;

	pm_runtime_put_autosuspend(priv->dev);
}

irqreturn_t cs42l43_tip_sense(int irq, void *data)
{
	struct cs42l43_codec *priv = data;
	unsigned int db_delay = priv->tip_debounce_ms;

	cancel_delayed_work(&priv->bias_sense_timeout);
	cancel_delayed_work(&priv->tip_sense_work);
	cancel_delayed_work(&priv->button_press_work);
	cancel_work(&priv->button_release_work);

	// Ensure delay after suspend is long enough to avoid false detection
	if (priv->suspend_jack_debounce)
		db_delay += priv->tip_fall_db_ms + priv->tip_rise_db_ms;

	queue_delayed_work(system_long_wq, &priv->tip_sense_work,
			   msecs_to_jiffies(db_delay));

	return IRQ_HANDLED;
}

enum cs42l43_raw_jack {
	CS42L43_JACK_RAW_CTIA = 0,
	CS42L43_JACK_RAW_OMTP,
	CS42L43_JACK_RAW_HEADPHONE,
	CS42L43_JACK_RAW_LINE_OUT,
	CS42L43_JACK_RAW_LINE_IN,
	CS42L43_JACK_RAW_MICROPHONE,
	CS42L43_JACK_RAW_OPTICAL,
};

#define CS42L43_JACK_3_POLE_SWITCHES ((0x2 << CS42L43_HSDET_MANUAL_MODE_SHIFT) | \
				      CS42L43_AMP3_4_GNDREF_HS3_SEL_MASK | \
				      CS42L43_AMP3_4_GNDREF_HS4_SEL_MASK | \
				      CS42L43_HSBIAS_GNDREF_HS3_SEL_MASK | \
				      CS42L43_HSBIAS_GNDREF_HS4_SEL_MASK | \
				      CS42L43_HSGND_HS3_SEL_MASK | \
				      CS42L43_HSGND_HS4_SEL_MASK)

static const struct cs42l43_jack_override_mode {
	unsigned int hsdet_mode;
	unsigned int mic_ctrl;
	unsigned int clamp_ctrl;
	int report;
} cs42l43_jack_override_modes[] = {
	[CS42L43_JACK_RAW_CTIA] = {
		.hsdet_mode = CS42L43_AMP3_4_GNDREF_HS3_SEL_MASK |
			      CS42L43_HSBIAS_GNDREF_HS3_SEL_MASK |
			      CS42L43_HSBIAS_OUT_HS4_SEL_MASK |
			      CS42L43_HSGND_HS3_SEL_MASK,
		.clamp_ctrl = CS42L43_SMIC_HPAMP_CLAMP_DIS_FRC_MASK,
		.report = CS42L43_JACK_HEADSET,
	},
	[CS42L43_JACK_RAW_OMTP] = {
		.hsdet_mode = (0x1 << CS42L43_HSDET_MANUAL_MODE_SHIFT) |
			       CS42L43_AMP3_4_GNDREF_HS4_SEL_MASK |
			       CS42L43_HSBIAS_GNDREF_HS4_SEL_MASK |
			       CS42L43_HSBIAS_OUT_HS3_SEL_MASK |
			       CS42L43_HSGND_HS4_SEL_MASK,
		.clamp_ctrl = CS42L43_SMIC_HPAMP_CLAMP_DIS_FRC_MASK,
		.report = CS42L43_JACK_HEADSET,
	},
	[CS42L43_JACK_RAW_HEADPHONE] = {
		.hsdet_mode = CS42L43_JACK_3_POLE_SWITCHES,
		.clamp_ctrl = CS42L43_SMIC_HPAMP_CLAMP_DIS_FRC_MASK,
		.report = CS42L43_JACK_HEADPHONE,
	},
	[CS42L43_JACK_RAW_LINE_OUT] = {
		.hsdet_mode = CS42L43_JACK_3_POLE_SWITCHES,
		.clamp_ctrl = CS42L43_SMIC_HPAMP_CLAMP_DIS_FRC_MASK,
		.report = CS42L43_JACK_LINEOUT,
	},
	[CS42L43_JACK_RAW_LINE_IN] = {
		.hsdet_mode = CS42L43_JACK_3_POLE_SWITCHES,
		.mic_ctrl = 0x2 << CS42L43_JACK_STEREO_CONFIG_SHIFT,
		.report = CS42L43_JACK_LINEIN,
	},
	[CS42L43_JACK_RAW_MICROPHONE] = {
		.hsdet_mode = CS42L43_JACK_3_POLE_SWITCHES,
		.mic_ctrl = (0x3 << CS42L43_JACK_STEREO_CONFIG_SHIFT) |
			    CS42L43_HS1_BIAS_EN_MASK | CS42L43_HS2_BIAS_EN_MASK,
		.report = CS42L43_JACK_LINEIN,
	},
	[CS42L43_JACK_RAW_OPTICAL] = {
		.hsdet_mode = CS42L43_JACK_3_POLE_SWITCHES,
		.clamp_ctrl = CS42L43_SMIC_HPAMP_CLAMP_DIS_FRC_MASK,
		.report = CS42L43_JACK_OPTICAL,
	},
};

static const char * const cs42l43_jack_text[] = {
	"None", "CTIA", "OMTP", "Headphone", "Line-Out",
	"Line-In", "Microphone", "Optical",
};

static_assert(ARRAY_SIZE(cs42l43_jack_override_modes) ==
	      ARRAY_SIZE(cs42l43_jack_text) - 1);

SOC_ENUM_SINGLE_VIRT_DECL(cs42l43_jack_enum, cs42l43_jack_text);

int cs42l43_jack_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct cs42l43_codec *priv = snd_soc_component_get_drvdata(component);

	mutex_lock(&priv->jack_lock);
	ucontrol->value.integer.value[0] = priv->jack_override;
	mutex_unlock(&priv->jack_lock);

	return 0;
}

int cs42l43_jack_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct cs42l43_codec *priv = snd_soc_component_get_drvdata(component);
	struct cs42l43 *cs42l43 = priv->core;
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int override = ucontrol->value.integer.value[0];

	if (override >= e->items)
		return -EINVAL;

	mutex_lock(&priv->jack_lock);

	if (!cs42l43_jack_present(priv)) {
		mutex_unlock(&priv->jack_lock);
		return -EBUSY;
	}

	if (override == priv->jack_override) {
		mutex_unlock(&priv->jack_lock);
		return 0;
	}

	priv->jack_override = override;

	cs42l43_clear_jack(priv);

	if (!override) {
		queue_delayed_work(system_long_wq, &priv->tip_sense_work, 0);
	} else {
		override--;

		regmap_update_bits(cs42l43->regmap, CS42L43_HS2,
				   CS42L43_HSDET_MODE_MASK |
				   CS42L43_HSDET_MANUAL_MODE_MASK |
				   CS42L43_AMP3_4_GNDREF_HS3_SEL_MASK |
				   CS42L43_AMP3_4_GNDREF_HS4_SEL_MASK |
				   CS42L43_HSBIAS_GNDREF_HS3_SEL_MASK |
				   CS42L43_HSBIAS_GNDREF_HS4_SEL_MASK |
				   CS42L43_HSBIAS_OUT_HS3_SEL_MASK |
				   CS42L43_HSBIAS_OUT_HS4_SEL_MASK |
				   CS42L43_HSGND_HS3_SEL_MASK |
				   CS42L43_HSGND_HS4_SEL_MASK,
				   cs42l43_jack_override_modes[override].hsdet_mode);
		regmap_update_bits(cs42l43->regmap, CS42L43_STEREO_MIC_CTRL,
				   CS42L43_HS2_BIAS_EN_MASK | CS42L43_HS1_BIAS_EN_MASK |
				   CS42L43_JACK_STEREO_CONFIG_MASK,
				   cs42l43_jack_override_modes[override].mic_ctrl);
		regmap_update_bits(cs42l43->regmap, CS42L43_STEREO_MIC_CLAMP_CTRL,
				   CS42L43_SMIC_HPAMP_CLAMP_DIS_FRC_MASK,
				   cs42l43_jack_override_modes[override].clamp_ctrl);

		switch (override) {
		case CS42L43_JACK_RAW_CTIA:
		case CS42L43_JACK_RAW_OMTP:
			cs42l43_start_hs_bias(priv, false);
			cs42l43_start_button_detect(priv);
			break;
		case CS42L43_JACK_RAW_LINE_IN:
			regmap_update_bits(cs42l43->regmap, CS42L43_ADC_B_CTRL1,
					   CS42L43_PGA_WIDESWING_MODE_EN_MASK,
					   CS42L43_PGA_WIDESWING_MODE_EN_MASK);
			regmap_update_bits(cs42l43->regmap, CS42L43_ADC_B_CTRL2,
					   CS42L43_PGA_WIDESWING_MODE_EN_MASK,
					   CS42L43_PGA_WIDESWING_MODE_EN_MASK);
			break;
		case CS42L43_JACK_RAW_MICROPHONE:
			cs42l43_start_hs_bias(priv, false);
			break;
		default:
			break;
		}

		snd_soc_jack_report(priv->jack_hp,
				    cs42l43_jack_override_modes[override].report,
				    cs42l43_jack_override_modes[override].report);
	}

	mutex_unlock(&priv->jack_lock);

	return 1;
}
