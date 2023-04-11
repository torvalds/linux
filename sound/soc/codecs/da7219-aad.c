// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * da7219-aad.c - Dialog DA7219 ALSA SoC AAD Driver
 *
 * Copyright (c) 2015 Dialog Semiconductor Ltd.
 *
 * Author: Adam Thomson <Adam.Thomson.Opensource@diasemi.com>
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/property.h>
#include <linux/pm_wakeirq.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <sound/da7219.h>

#include "da7219.h"
#include "da7219-aad.h"


/*
 * Detection control
 */

void da7219_aad_jack_det(struct snd_soc_component *component, struct snd_soc_jack *jack)
{
	struct da7219_priv *da7219 = snd_soc_component_get_drvdata(component);

	da7219->aad->jack = jack;
	da7219->aad->jack_inserted = false;

	/* Send an initial empty report */
	snd_soc_jack_report(jack, 0, DA7219_AAD_REPORT_ALL_MASK);

	/* Enable/Disable jack detection */
	snd_soc_component_update_bits(component, DA7219_ACCDET_CONFIG_1,
			    DA7219_ACCDET_EN_MASK,
			    (jack ? DA7219_ACCDET_EN_MASK : 0));
}

/*
 * Button/HPTest work
 */

static void da7219_aad_btn_det_work(struct work_struct *work)
{
	struct da7219_aad_priv *da7219_aad =
		container_of(work, struct da7219_aad_priv, btn_det_work);
	struct snd_soc_component *component = da7219_aad->component;
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	struct da7219_priv *da7219 = snd_soc_component_get_drvdata(component);
	u8 statusa, micbias_ctrl;
	bool micbias_up = false;
	int retries = 0;

	/* Disable ground switch */
	snd_soc_component_update_bits(component, 0xFB, 0x01, 0x00);

	/* Drive headphones/lineout */
	snd_soc_component_update_bits(component, DA7219_HP_L_CTRL,
			    DA7219_HP_L_AMP_OE_MASK,
			    DA7219_HP_L_AMP_OE_MASK);
	snd_soc_component_update_bits(component, DA7219_HP_R_CTRL,
			    DA7219_HP_R_AMP_OE_MASK,
			    DA7219_HP_R_AMP_OE_MASK);

	/* Make sure mic bias is up */
	snd_soc_dapm_force_enable_pin(dapm, "Mic Bias");
	snd_soc_dapm_sync(dapm);

	do {
		statusa = snd_soc_component_read(component, DA7219_ACCDET_STATUS_A);
		if (statusa & DA7219_MICBIAS_UP_STS_MASK)
			micbias_up = true;
		else if (retries++ < DA7219_AAD_MICBIAS_CHK_RETRIES)
			msleep(DA7219_AAD_MICBIAS_CHK_DELAY);
	} while ((!micbias_up) && (retries < DA7219_AAD_MICBIAS_CHK_RETRIES));

	if (retries >= DA7219_AAD_MICBIAS_CHK_RETRIES)
		dev_warn(component->dev, "Mic bias status check timed out");

	da7219->micbias_on_event = true;

	/*
	 * Mic bias pulse required to enable mic, must be done before enabling
	 * button detection to prevent erroneous button readings.
	 */
	if (da7219_aad->micbias_pulse_lvl && da7219_aad->micbias_pulse_time) {
		/* Pulse higher level voltage */
		micbias_ctrl = snd_soc_component_read(component, DA7219_MICBIAS_CTRL);
		snd_soc_component_update_bits(component, DA7219_MICBIAS_CTRL,
				    DA7219_MICBIAS1_LEVEL_MASK,
				    da7219_aad->micbias_pulse_lvl);
		msleep(da7219_aad->micbias_pulse_time);
		snd_soc_component_write(component, DA7219_MICBIAS_CTRL, micbias_ctrl);

	}

	snd_soc_component_update_bits(component, DA7219_ACCDET_CONFIG_1,
			    DA7219_BUTTON_CONFIG_MASK,
			    da7219_aad->btn_cfg);
}

static void da7219_aad_hptest_work(struct work_struct *work)
{
	struct da7219_aad_priv *da7219_aad =
		container_of(work, struct da7219_aad_priv, hptest_work);
	struct snd_soc_component *component = da7219_aad->component;
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	struct da7219_priv *da7219 = snd_soc_component_get_drvdata(component);

	__le16 tonegen_freq_hptest;
	u8 pll_srm_sts, pll_ctrl, gain_ramp_ctrl, accdet_cfg8;
	int report = 0, ret;

	/* Lock DAPM, Kcontrols affected by this test and the PLL */
	snd_soc_dapm_mutex_lock(dapm);
	mutex_lock(&da7219->ctrl_lock);
	mutex_lock(&da7219->pll_lock);

	/* Ensure MCLK is available for HP test procedure */
	if (da7219->mclk) {
		ret = clk_prepare_enable(da7219->mclk);
		if (ret) {
			dev_err(component->dev, "Failed to enable mclk - %d\n", ret);
			mutex_unlock(&da7219->pll_lock);
			mutex_unlock(&da7219->ctrl_lock);
			snd_soc_dapm_mutex_unlock(dapm);
			return;
		}
	}

	/*
	 * If MCLK not present, then we're using the internal oscillator and
	 * require different frequency settings to achieve the same result.
	 *
	 * If MCLK is present, but PLL is not enabled then we enable it here to
	 * ensure a consistent detection procedure.
	 */
	pll_srm_sts = snd_soc_component_read(component, DA7219_PLL_SRM_STS);
	if (pll_srm_sts & DA7219_PLL_SRM_STS_MCLK) {
		tonegen_freq_hptest = cpu_to_le16(DA7219_AAD_HPTEST_RAMP_FREQ);

		pll_ctrl = snd_soc_component_read(component, DA7219_PLL_CTRL);
		if ((pll_ctrl & DA7219_PLL_MODE_MASK) == DA7219_PLL_MODE_BYPASS)
			da7219_set_pll(component, DA7219_SYSCLK_PLL,
				       DA7219_PLL_FREQ_OUT_98304);
	} else {
		tonegen_freq_hptest = cpu_to_le16(DA7219_AAD_HPTEST_RAMP_FREQ_INT_OSC);
	}

	/* Disable ground switch */
	snd_soc_component_update_bits(component, 0xFB, 0x01, 0x00);

	/* Ensure gain ramping at fastest rate */
	gain_ramp_ctrl = snd_soc_component_read(component, DA7219_GAIN_RAMP_CTRL);
	snd_soc_component_write(component, DA7219_GAIN_RAMP_CTRL, DA7219_GAIN_RAMP_RATE_X8);

	/* Bypass cache so it saves current settings */
	regcache_cache_bypass(da7219->regmap, true);

	/* Make sure Tone Generator is disabled */
	snd_soc_component_write(component, DA7219_TONE_GEN_CFG1, 0);

	/* Enable HPTest block, 1KOhms check */
	snd_soc_component_update_bits(component, DA7219_ACCDET_CONFIG_8,
			    DA7219_HPTEST_EN_MASK | DA7219_HPTEST_RES_SEL_MASK,
			    DA7219_HPTEST_EN_MASK |
			    DA7219_HPTEST_RES_SEL_1KOHMS);

	/* Set gains to 0db */
	snd_soc_component_write(component, DA7219_DAC_L_GAIN, DA7219_DAC_DIGITAL_GAIN_0DB);
	snd_soc_component_write(component, DA7219_DAC_R_GAIN, DA7219_DAC_DIGITAL_GAIN_0DB);
	snd_soc_component_write(component, DA7219_HP_L_GAIN, DA7219_HP_AMP_GAIN_0DB);
	snd_soc_component_write(component, DA7219_HP_R_GAIN, DA7219_HP_AMP_GAIN_0DB);

	/* Disable DAC filters, EQs and soft mute */
	snd_soc_component_update_bits(component, DA7219_DAC_FILTERS1, DA7219_HPF_MODE_MASK,
			    0);
	snd_soc_component_update_bits(component, DA7219_DAC_FILTERS4, DA7219_DAC_EQ_EN_MASK,
			    0);
	snd_soc_component_update_bits(component, DA7219_DAC_FILTERS5,
			    DA7219_DAC_SOFTMUTE_EN_MASK, 0);

	/* Enable HP left & right paths */
	snd_soc_component_update_bits(component, DA7219_CP_CTRL, DA7219_CP_EN_MASK,
			    DA7219_CP_EN_MASK);
	snd_soc_component_update_bits(component, DA7219_DIG_ROUTING_DAC,
			    DA7219_DAC_L_SRC_MASK | DA7219_DAC_R_SRC_MASK,
			    DA7219_DAC_L_SRC_TONEGEN |
			    DA7219_DAC_R_SRC_TONEGEN);
	snd_soc_component_update_bits(component, DA7219_DAC_L_CTRL,
			    DA7219_DAC_L_EN_MASK | DA7219_DAC_L_MUTE_EN_MASK,
			    DA7219_DAC_L_EN_MASK);
	snd_soc_component_update_bits(component, DA7219_DAC_R_CTRL,
			    DA7219_DAC_R_EN_MASK | DA7219_DAC_R_MUTE_EN_MASK,
			    DA7219_DAC_R_EN_MASK);
	snd_soc_component_update_bits(component, DA7219_MIXOUT_L_SELECT,
			    DA7219_MIXOUT_L_MIX_SELECT_MASK,
			    DA7219_MIXOUT_L_MIX_SELECT_MASK);
	snd_soc_component_update_bits(component, DA7219_MIXOUT_R_SELECT,
			    DA7219_MIXOUT_R_MIX_SELECT_MASK,
			    DA7219_MIXOUT_R_MIX_SELECT_MASK);
	snd_soc_component_update_bits(component, DA7219_DROUTING_ST_OUTFILT_1L,
			    DA7219_OUTFILT_ST_1L_SRC_MASK,
			    DA7219_DMIX_ST_SRC_OUTFILT1L);
	snd_soc_component_update_bits(component, DA7219_DROUTING_ST_OUTFILT_1R,
			    DA7219_OUTFILT_ST_1R_SRC_MASK,
			    DA7219_DMIX_ST_SRC_OUTFILT1R);
	snd_soc_component_update_bits(component, DA7219_MIXOUT_L_CTRL,
			    DA7219_MIXOUT_L_AMP_EN_MASK,
			    DA7219_MIXOUT_L_AMP_EN_MASK);
	snd_soc_component_update_bits(component, DA7219_MIXOUT_R_CTRL,
			    DA7219_MIXOUT_R_AMP_EN_MASK,
			    DA7219_MIXOUT_R_AMP_EN_MASK);
	snd_soc_component_update_bits(component, DA7219_HP_L_CTRL,
			    DA7219_HP_L_AMP_OE_MASK | DA7219_HP_L_AMP_EN_MASK,
			    DA7219_HP_L_AMP_OE_MASK | DA7219_HP_L_AMP_EN_MASK);
	snd_soc_component_update_bits(component, DA7219_HP_R_CTRL,
			    DA7219_HP_R_AMP_OE_MASK | DA7219_HP_R_AMP_EN_MASK,
			    DA7219_HP_R_AMP_OE_MASK | DA7219_HP_R_AMP_EN_MASK);
	msleep(DA7219_SETTLING_DELAY);
	snd_soc_component_update_bits(component, DA7219_HP_L_CTRL,
			    DA7219_HP_L_AMP_MUTE_EN_MASK |
			    DA7219_HP_L_AMP_MIN_GAIN_EN_MASK, 0);
	snd_soc_component_update_bits(component, DA7219_HP_R_CTRL,
			    DA7219_HP_R_AMP_MUTE_EN_MASK |
			    DA7219_HP_R_AMP_MIN_GAIN_EN_MASK, 0);

	/*
	 * If we're running from the internal oscillator then give audio paths
	 * time to settle before running test.
	 */
	if (!(pll_srm_sts & DA7219_PLL_SRM_STS_MCLK))
		msleep(DA7219_AAD_HPTEST_INT_OSC_PATH_DELAY);

	/* Configure & start Tone Generator */
	snd_soc_component_write(component, DA7219_TONE_GEN_ON_PER, DA7219_BEEP_ON_PER_MASK);
	regmap_raw_write(da7219->regmap, DA7219_TONE_GEN_FREQ1_L,
			 &tonegen_freq_hptest, sizeof(tonegen_freq_hptest));
	snd_soc_component_update_bits(component, DA7219_TONE_GEN_CFG2,
			    DA7219_SWG_SEL_MASK | DA7219_TONE_GEN_GAIN_MASK,
			    DA7219_SWG_SEL_SRAMP |
			    DA7219_TONE_GEN_GAIN_MINUS_15DB);
	snd_soc_component_write(component, DA7219_TONE_GEN_CFG1, DA7219_START_STOPN_MASK);

	msleep(DA7219_AAD_HPTEST_PERIOD);

	/* Grab comparator reading */
	accdet_cfg8 = snd_soc_component_read(component, DA7219_ACCDET_CONFIG_8);
	if (accdet_cfg8 & DA7219_HPTEST_COMP_MASK)
		report |= SND_JACK_HEADPHONE;
	else
		report |= SND_JACK_LINEOUT;

	/* Stop tone generator */
	snd_soc_component_write(component, DA7219_TONE_GEN_CFG1, 0);

	msleep(DA7219_AAD_HPTEST_PERIOD);

	/* Restore original settings from cache */
	regcache_mark_dirty(da7219->regmap);
	regcache_sync_region(da7219->regmap, DA7219_HP_L_CTRL,
			     DA7219_HP_R_CTRL);
	msleep(DA7219_SETTLING_DELAY);
	regcache_sync_region(da7219->regmap, DA7219_MIXOUT_L_CTRL,
			     DA7219_MIXOUT_R_CTRL);
	regcache_sync_region(da7219->regmap, DA7219_DROUTING_ST_OUTFILT_1L,
			     DA7219_DROUTING_ST_OUTFILT_1R);
	regcache_sync_region(da7219->regmap, DA7219_MIXOUT_L_SELECT,
			     DA7219_MIXOUT_R_SELECT);
	regcache_sync_region(da7219->regmap, DA7219_DAC_L_CTRL,
			     DA7219_DAC_R_CTRL);
	regcache_sync_region(da7219->regmap, DA7219_DIG_ROUTING_DAC,
			     DA7219_DIG_ROUTING_DAC);
	regcache_sync_region(da7219->regmap, DA7219_CP_CTRL, DA7219_CP_CTRL);
	regcache_sync_region(da7219->regmap, DA7219_DAC_FILTERS5,
			     DA7219_DAC_FILTERS5);
	regcache_sync_region(da7219->regmap, DA7219_DAC_FILTERS4,
			     DA7219_DAC_FILTERS1);
	regcache_sync_region(da7219->regmap, DA7219_HP_L_GAIN,
			     DA7219_HP_R_GAIN);
	regcache_sync_region(da7219->regmap, DA7219_DAC_L_GAIN,
			     DA7219_DAC_R_GAIN);
	regcache_sync_region(da7219->regmap, DA7219_TONE_GEN_ON_PER,
			     DA7219_TONE_GEN_ON_PER);
	regcache_sync_region(da7219->regmap, DA7219_TONE_GEN_FREQ1_L,
			     DA7219_TONE_GEN_FREQ1_U);
	regcache_sync_region(da7219->regmap, DA7219_TONE_GEN_CFG1,
			     DA7219_TONE_GEN_CFG2);

	regcache_cache_bypass(da7219->regmap, false);

	/* Disable HPTest block */
	snd_soc_component_update_bits(component, DA7219_ACCDET_CONFIG_8,
			    DA7219_HPTEST_EN_MASK, 0);

	/*
	 * If we're running from the internal oscillator then give audio paths
	 * time to settle before allowing headphones to be driven as required.
	 */
	if (!(pll_srm_sts & DA7219_PLL_SRM_STS_MCLK))
		msleep(DA7219_AAD_HPTEST_INT_OSC_PATH_DELAY);

	/* Restore gain ramping rate */
	snd_soc_component_write(component, DA7219_GAIN_RAMP_CTRL, gain_ramp_ctrl);

	/* Drive Headphones/lineout */
	snd_soc_component_update_bits(component, DA7219_HP_L_CTRL, DA7219_HP_L_AMP_OE_MASK,
			    DA7219_HP_L_AMP_OE_MASK);
	snd_soc_component_update_bits(component, DA7219_HP_R_CTRL, DA7219_HP_R_AMP_OE_MASK,
			    DA7219_HP_R_AMP_OE_MASK);

	/* Restore PLL to previous configuration, if re-configured */
	if ((pll_srm_sts & DA7219_PLL_SRM_STS_MCLK) &&
	    ((pll_ctrl & DA7219_PLL_MODE_MASK) == DA7219_PLL_MODE_BYPASS))
		da7219_set_pll(component, DA7219_SYSCLK_MCLK, 0);

	/* Remove MCLK, if previously enabled */
	if (da7219->mclk)
		clk_disable_unprepare(da7219->mclk);

	mutex_unlock(&da7219->pll_lock);
	mutex_unlock(&da7219->ctrl_lock);
	snd_soc_dapm_mutex_unlock(dapm);

	/*
	 * Only send report if jack hasn't been removed during process,
	 * otherwise it's invalid and we drop it.
	 */
	if (da7219_aad->jack_inserted)
		snd_soc_jack_report(da7219_aad->jack, report,
				    SND_JACK_HEADSET | SND_JACK_LINEOUT);
}

static void da7219_aad_jack_det_work(struct work_struct *work)
{
	struct da7219_aad_priv *da7219_aad =
		container_of(work, struct da7219_aad_priv, jack_det_work);
	struct snd_soc_component *component = da7219_aad->component;
	u8 srm_st;

	mutex_lock(&da7219_aad->jack_det_mutex);

	srm_st = snd_soc_component_read(component, DA7219_PLL_SRM_STS) & DA7219_PLL_SRM_STS_MCLK;
	msleep(da7219_aad->gnd_switch_delay * ((srm_st == 0x0) ? 2 : 1) - 4);
	/* Enable ground switch */
	snd_soc_component_update_bits(component, 0xFB, 0x01, 0x01);

	mutex_unlock(&da7219_aad->jack_det_mutex);
}


/*
 * IRQ
 */

static irqreturn_t da7219_aad_pre_irq_thread(int irq, void *data)
{

	struct da7219_aad_priv *da7219_aad = data;

	if (!da7219_aad->jack_inserted)
		schedule_work(&da7219_aad->jack_det_work);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t da7219_aad_irq_thread(int irq, void *data)
{
	struct da7219_aad_priv *da7219_aad = data;
	struct snd_soc_component *component = da7219_aad->component;
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	struct da7219_priv *da7219 = snd_soc_component_get_drvdata(component);
	u8 events[DA7219_AAD_IRQ_REG_MAX];
	u8 statusa;
	int i, report = 0, mask = 0;

	/* Read current IRQ events */
	regmap_bulk_read(da7219->regmap, DA7219_ACCDET_IRQ_EVENT_A,
			 events, DA7219_AAD_IRQ_REG_MAX);

	if (!events[DA7219_AAD_IRQ_REG_A] && !events[DA7219_AAD_IRQ_REG_B])
		return IRQ_NONE;

	/* Read status register for jack insertion & type status */
	statusa = snd_soc_component_read(component, DA7219_ACCDET_STATUS_A);

	/* Clear events */
	regmap_bulk_write(da7219->regmap, DA7219_ACCDET_IRQ_EVENT_A,
			  events, DA7219_AAD_IRQ_REG_MAX);

	dev_dbg(component->dev, "IRQ events = 0x%x|0x%x, status = 0x%x\n",
		events[DA7219_AAD_IRQ_REG_A], events[DA7219_AAD_IRQ_REG_B],
		statusa);

	if (!da7219_aad->jack_inserted)
		cancel_work_sync(&da7219_aad->jack_det_work);

	if (statusa & DA7219_JACK_INSERTION_STS_MASK) {
		/* Jack Insertion */
		if (events[DA7219_AAD_IRQ_REG_A] &
		    DA7219_E_JACK_INSERTED_MASK) {
			report |= SND_JACK_MECHANICAL;
			mask |= SND_JACK_MECHANICAL;
			da7219_aad->jack_inserted = true;
		}

		/* Jack type detection */
		if (events[DA7219_AAD_IRQ_REG_A] &
		    DA7219_E_JACK_DETECT_COMPLETE_MASK) {
			/*
			 * If 4-pole, then enable button detection, else perform
			 * HP impedance test to determine output type to report.
			 *
			 * We schedule work here as the tasks themselves can
			 * take time to complete, and in particular for hptest
			 * we want to be able to check if the jack was removed
			 * during the procedure as this will invalidate the
			 * result. By doing this as work, the IRQ thread can
			 * handle a removal, and we can check at the end of
			 * hptest if we have a valid result or not.
			 */
			if (statusa & DA7219_JACK_TYPE_STS_MASK) {
				report |= SND_JACK_HEADSET;
				mask |=	SND_JACK_HEADSET | SND_JACK_LINEOUT;
				schedule_work(&da7219_aad->btn_det_work);
			} else {
				schedule_work(&da7219_aad->hptest_work);
			}
		}

		/* Button support for 4-pole jack */
		if (statusa & DA7219_JACK_TYPE_STS_MASK) {
			for (i = 0; i < DA7219_AAD_MAX_BUTTONS; ++i) {
				/* Button Press */
				if (events[DA7219_AAD_IRQ_REG_B] &
				    (DA7219_E_BUTTON_A_PRESSED_MASK << i)) {
					report |= SND_JACK_BTN_0 >> i;
					mask |= SND_JACK_BTN_0 >> i;
				}
			}
			snd_soc_jack_report(da7219_aad->jack, report, mask);

			for (i = 0; i < DA7219_AAD_MAX_BUTTONS; ++i) {
				/* Button Release */
				if (events[DA7219_AAD_IRQ_REG_B] &
				    (DA7219_E_BUTTON_A_RELEASED_MASK >> i)) {
					report &= ~(SND_JACK_BTN_0 >> i);
					mask |= SND_JACK_BTN_0 >> i;
				}
			}
		}
	} else {
		/* Jack removal */
		if (events[DA7219_AAD_IRQ_REG_A] & DA7219_E_JACK_REMOVED_MASK) {
			report = 0;
			mask |= DA7219_AAD_REPORT_ALL_MASK;
			da7219_aad->jack_inserted = false;

			/* Cancel any pending work */
			cancel_work_sync(&da7219_aad->btn_det_work);
			cancel_work_sync(&da7219_aad->hptest_work);

			/* Un-drive headphones/lineout */
			snd_soc_component_update_bits(component, DA7219_HP_R_CTRL,
					    DA7219_HP_R_AMP_OE_MASK, 0);
			snd_soc_component_update_bits(component, DA7219_HP_L_CTRL,
					    DA7219_HP_L_AMP_OE_MASK, 0);

			/* Ensure button detection disabled */
			snd_soc_component_update_bits(component, DA7219_ACCDET_CONFIG_1,
					    DA7219_BUTTON_CONFIG_MASK, 0);

			da7219->micbias_on_event = false;

			/* Disable mic bias */
			snd_soc_dapm_disable_pin(dapm, "Mic Bias");
			snd_soc_dapm_sync(dapm);

			/* Disable ground switch */
			snd_soc_component_update_bits(component, 0xFB, 0x01, 0x00);
		}
	}

	snd_soc_jack_report(da7219_aad->jack, report, mask);

	return IRQ_HANDLED;
}

/*
 * DT/ACPI to pdata conversion
 */

static enum da7219_aad_micbias_pulse_lvl
	da7219_aad_fw_micbias_pulse_lvl(struct device *dev, u32 val)
{
	switch (val) {
	case 2800:
		return DA7219_AAD_MICBIAS_PULSE_LVL_2_8V;
	case 2900:
		return DA7219_AAD_MICBIAS_PULSE_LVL_2_9V;
	default:
		dev_warn(dev, "Invalid micbias pulse level");
		return DA7219_AAD_MICBIAS_PULSE_LVL_OFF;
	}
}

static enum da7219_aad_btn_cfg
	da7219_aad_fw_btn_cfg(struct device *dev, u32 val)
{
	switch (val) {
	case 2:
		return DA7219_AAD_BTN_CFG_2MS;
	case 5:
		return DA7219_AAD_BTN_CFG_5MS;
	case 10:
		return DA7219_AAD_BTN_CFG_10MS;
	case 50:
		return DA7219_AAD_BTN_CFG_50MS;
	case 100:
		return DA7219_AAD_BTN_CFG_100MS;
	case 200:
		return DA7219_AAD_BTN_CFG_200MS;
	case 500:
		return DA7219_AAD_BTN_CFG_500MS;
	default:
		dev_warn(dev, "Invalid button config");
		return DA7219_AAD_BTN_CFG_10MS;
	}
}

static enum da7219_aad_mic_det_thr
	da7219_aad_fw_mic_det_thr(struct device *dev, u32 val)
{
	switch (val) {
	case 200:
		return DA7219_AAD_MIC_DET_THR_200_OHMS;
	case 500:
		return DA7219_AAD_MIC_DET_THR_500_OHMS;
	case 750:
		return DA7219_AAD_MIC_DET_THR_750_OHMS;
	case 1000:
		return DA7219_AAD_MIC_DET_THR_1000_OHMS;
	default:
		dev_warn(dev, "Invalid mic detect threshold");
		return DA7219_AAD_MIC_DET_THR_500_OHMS;
	}
}

static enum da7219_aad_jack_ins_deb
	da7219_aad_fw_jack_ins_deb(struct device *dev, u32 val)
{
	switch (val) {
	case 5:
		return DA7219_AAD_JACK_INS_DEB_5MS;
	case 10:
		return DA7219_AAD_JACK_INS_DEB_10MS;
	case 20:
		return DA7219_AAD_JACK_INS_DEB_20MS;
	case 50:
		return DA7219_AAD_JACK_INS_DEB_50MS;
	case 100:
		return DA7219_AAD_JACK_INS_DEB_100MS;
	case 200:
		return DA7219_AAD_JACK_INS_DEB_200MS;
	case 500:
		return DA7219_AAD_JACK_INS_DEB_500MS;
	case 1000:
		return DA7219_AAD_JACK_INS_DEB_1S;
	default:
		dev_warn(dev, "Invalid jack insert debounce");
		return DA7219_AAD_JACK_INS_DEB_20MS;
	}
}

static enum da7219_aad_jack_det_rate
	da7219_aad_fw_jack_det_rate(struct device *dev, const char *str)
{
	if (!strcmp(str, "32ms_64ms")) {
		return DA7219_AAD_JACK_DET_RATE_32_64MS;
	} else if (!strcmp(str, "64ms_128ms")) {
		return DA7219_AAD_JACK_DET_RATE_64_128MS;
	} else if (!strcmp(str, "128ms_256ms")) {
		return DA7219_AAD_JACK_DET_RATE_128_256MS;
	} else if (!strcmp(str, "256ms_512ms")) {
		return DA7219_AAD_JACK_DET_RATE_256_512MS;
	} else {
		dev_warn(dev, "Invalid jack detect rate");
		return DA7219_AAD_JACK_DET_RATE_256_512MS;
	}
}

static enum da7219_aad_jack_rem_deb
	da7219_aad_fw_jack_rem_deb(struct device *dev, u32 val)
{
	switch (val) {
	case 1:
		return DA7219_AAD_JACK_REM_DEB_1MS;
	case 5:
		return DA7219_AAD_JACK_REM_DEB_5MS;
	case 10:
		return DA7219_AAD_JACK_REM_DEB_10MS;
	case 20:
		return DA7219_AAD_JACK_REM_DEB_20MS;
	default:
		dev_warn(dev, "Invalid jack removal debounce");
		return DA7219_AAD_JACK_REM_DEB_1MS;
	}
}

static enum da7219_aad_btn_avg
	da7219_aad_fw_btn_avg(struct device *dev, u32 val)
{
	switch (val) {
	case 1:
		return DA7219_AAD_BTN_AVG_1;
	case 2:
		return DA7219_AAD_BTN_AVG_2;
	case 4:
		return DA7219_AAD_BTN_AVG_4;
	case 8:
		return DA7219_AAD_BTN_AVG_8;
	default:
		dev_warn(dev, "Invalid button average value");
		return DA7219_AAD_BTN_AVG_2;
	}
}

static enum da7219_aad_adc_1bit_rpt
	da7219_aad_fw_adc_1bit_rpt(struct device *dev, u32 val)
{
	switch (val) {
	case 1:
		return DA7219_AAD_ADC_1BIT_RPT_1;
	case 2:
		return DA7219_AAD_ADC_1BIT_RPT_2;
	case 4:
		return DA7219_AAD_ADC_1BIT_RPT_4;
	case 8:
		return DA7219_AAD_ADC_1BIT_RPT_8;
	default:
		dev_warn(dev, "Invalid ADC 1-bit repeat value");
		return DA7219_AAD_ADC_1BIT_RPT_1;
	}
}

static struct da7219_aad_pdata *da7219_aad_fw_to_pdata(struct device *dev)
{
	struct i2c_client *i2c = to_i2c_client(dev);
	struct fwnode_handle *aad_np;
	struct da7219_aad_pdata *aad_pdata;
	const char *fw_str;
	u32 fw_val32;

	aad_np = device_get_named_child_node(dev, "da7219_aad");
	if (!aad_np)
		return NULL;

	aad_pdata = devm_kzalloc(dev, sizeof(*aad_pdata), GFP_KERNEL);
	if (!aad_pdata)
		return NULL;

	aad_pdata->irq = i2c->irq;

	if (fwnode_property_read_u32(aad_np, "dlg,micbias-pulse-lvl",
				     &fw_val32) >= 0)
		aad_pdata->micbias_pulse_lvl =
			da7219_aad_fw_micbias_pulse_lvl(dev, fw_val32);
	else
		aad_pdata->micbias_pulse_lvl = DA7219_AAD_MICBIAS_PULSE_LVL_OFF;

	if (fwnode_property_read_u32(aad_np, "dlg,micbias-pulse-time",
				     &fw_val32) >= 0)
		aad_pdata->micbias_pulse_time = fw_val32;

	if (fwnode_property_read_u32(aad_np, "dlg,btn-cfg", &fw_val32) >= 0)
		aad_pdata->btn_cfg = da7219_aad_fw_btn_cfg(dev, fw_val32);
	else
		aad_pdata->btn_cfg = DA7219_AAD_BTN_CFG_10MS;

	if (fwnode_property_read_u32(aad_np, "dlg,mic-det-thr", &fw_val32) >= 0)
		aad_pdata->mic_det_thr =
			da7219_aad_fw_mic_det_thr(dev, fw_val32);
	else
		aad_pdata->mic_det_thr = DA7219_AAD_MIC_DET_THR_500_OHMS;

	if (fwnode_property_read_u32(aad_np, "dlg,jack-ins-deb", &fw_val32) >= 0)
		aad_pdata->jack_ins_deb =
			da7219_aad_fw_jack_ins_deb(dev, fw_val32);
	else
		aad_pdata->jack_ins_deb = DA7219_AAD_JACK_INS_DEB_20MS;

	if (!fwnode_property_read_string(aad_np, "dlg,jack-det-rate", &fw_str))
		aad_pdata->jack_det_rate =
			da7219_aad_fw_jack_det_rate(dev, fw_str);
	else
		aad_pdata->jack_det_rate = DA7219_AAD_JACK_DET_RATE_256_512MS;

	if (fwnode_property_read_u32(aad_np, "dlg,jack-rem-deb", &fw_val32) >= 0)
		aad_pdata->jack_rem_deb =
			da7219_aad_fw_jack_rem_deb(dev, fw_val32);
	else
		aad_pdata->jack_rem_deb = DA7219_AAD_JACK_REM_DEB_1MS;

	if (fwnode_property_read_u32(aad_np, "dlg,a-d-btn-thr", &fw_val32) >= 0)
		aad_pdata->a_d_btn_thr = (u8) fw_val32;
	else
		aad_pdata->a_d_btn_thr = 0xA;

	if (fwnode_property_read_u32(aad_np, "dlg,d-b-btn-thr", &fw_val32) >= 0)
		aad_pdata->d_b_btn_thr = (u8) fw_val32;
	else
		aad_pdata->d_b_btn_thr = 0x16;

	if (fwnode_property_read_u32(aad_np, "dlg,b-c-btn-thr", &fw_val32) >= 0)
		aad_pdata->b_c_btn_thr = (u8) fw_val32;
	else
		aad_pdata->b_c_btn_thr = 0x21;

	if (fwnode_property_read_u32(aad_np, "dlg,c-mic-btn-thr", &fw_val32) >= 0)
		aad_pdata->c_mic_btn_thr = (u8) fw_val32;
	else
		aad_pdata->c_mic_btn_thr = 0x3E;

	if (fwnode_property_read_u32(aad_np, "dlg,btn-avg", &fw_val32) >= 0)
		aad_pdata->btn_avg = da7219_aad_fw_btn_avg(dev, fw_val32);
	else
		aad_pdata->btn_avg = DA7219_AAD_BTN_AVG_2;

	if (fwnode_property_read_u32(aad_np, "dlg,adc-1bit-rpt", &fw_val32) >= 0)
		aad_pdata->adc_1bit_rpt =
			da7219_aad_fw_adc_1bit_rpt(dev, fw_val32);
	else
		aad_pdata->adc_1bit_rpt = DA7219_AAD_ADC_1BIT_RPT_1;

	return aad_pdata;
}

static void da7219_aad_handle_pdata(struct snd_soc_component *component)
{
	struct da7219_priv *da7219 = snd_soc_component_get_drvdata(component);
	struct da7219_aad_priv *da7219_aad = da7219->aad;
	struct da7219_pdata *pdata = da7219->pdata;

	if ((pdata) && (pdata->aad_pdata)) {
		struct da7219_aad_pdata *aad_pdata = pdata->aad_pdata;
		u8 cfg, mask;

		da7219_aad->irq = aad_pdata->irq;

		switch (aad_pdata->micbias_pulse_lvl) {
		case DA7219_AAD_MICBIAS_PULSE_LVL_2_8V:
		case DA7219_AAD_MICBIAS_PULSE_LVL_2_9V:
			da7219_aad->micbias_pulse_lvl =
				(aad_pdata->micbias_pulse_lvl <<
				 DA7219_MICBIAS1_LEVEL_SHIFT);
			break;
		default:
			break;
		}

		da7219_aad->micbias_pulse_time = aad_pdata->micbias_pulse_time;

		switch (aad_pdata->btn_cfg) {
		case DA7219_AAD_BTN_CFG_2MS:
		case DA7219_AAD_BTN_CFG_5MS:
		case DA7219_AAD_BTN_CFG_10MS:
		case DA7219_AAD_BTN_CFG_50MS:
		case DA7219_AAD_BTN_CFG_100MS:
		case DA7219_AAD_BTN_CFG_200MS:
		case DA7219_AAD_BTN_CFG_500MS:
			da7219_aad->btn_cfg  = (aad_pdata->btn_cfg <<
						DA7219_BUTTON_CONFIG_SHIFT);
		}

		cfg = 0;
		mask = 0;
		switch (aad_pdata->mic_det_thr) {
		case DA7219_AAD_MIC_DET_THR_200_OHMS:
		case DA7219_AAD_MIC_DET_THR_500_OHMS:
		case DA7219_AAD_MIC_DET_THR_750_OHMS:
		case DA7219_AAD_MIC_DET_THR_1000_OHMS:
			cfg |= (aad_pdata->mic_det_thr <<
				DA7219_MIC_DET_THRESH_SHIFT);
			mask |= DA7219_MIC_DET_THRESH_MASK;
		}
		snd_soc_component_update_bits(component, DA7219_ACCDET_CONFIG_1, mask, cfg);

		cfg = 0;
		mask = 0;
		switch (aad_pdata->jack_ins_deb) {
		case DA7219_AAD_JACK_INS_DEB_5MS:
		case DA7219_AAD_JACK_INS_DEB_10MS:
		case DA7219_AAD_JACK_INS_DEB_20MS:
		case DA7219_AAD_JACK_INS_DEB_50MS:
		case DA7219_AAD_JACK_INS_DEB_100MS:
		case DA7219_AAD_JACK_INS_DEB_200MS:
		case DA7219_AAD_JACK_INS_DEB_500MS:
		case DA7219_AAD_JACK_INS_DEB_1S:
			cfg |= (aad_pdata->jack_ins_deb <<
				DA7219_JACKDET_DEBOUNCE_SHIFT);
			mask |= DA7219_JACKDET_DEBOUNCE_MASK;
		}
		switch (aad_pdata->jack_det_rate) {
		case DA7219_AAD_JACK_DET_RATE_32_64MS:
		case DA7219_AAD_JACK_DET_RATE_64_128MS:
		case DA7219_AAD_JACK_DET_RATE_128_256MS:
		case DA7219_AAD_JACK_DET_RATE_256_512MS:
			cfg |= (aad_pdata->jack_det_rate <<
				DA7219_JACK_DETECT_RATE_SHIFT);
			mask |= DA7219_JACK_DETECT_RATE_MASK;
		}
		switch (aad_pdata->jack_rem_deb) {
		case DA7219_AAD_JACK_REM_DEB_1MS:
		case DA7219_AAD_JACK_REM_DEB_5MS:
		case DA7219_AAD_JACK_REM_DEB_10MS:
		case DA7219_AAD_JACK_REM_DEB_20MS:
			cfg |= (aad_pdata->jack_rem_deb <<
				DA7219_JACKDET_REM_DEB_SHIFT);
			mask |= DA7219_JACKDET_REM_DEB_MASK;
		}
		snd_soc_component_update_bits(component, DA7219_ACCDET_CONFIG_2, mask, cfg);

		snd_soc_component_write(component, DA7219_ACCDET_CONFIG_3,
			      aad_pdata->a_d_btn_thr);
		snd_soc_component_write(component, DA7219_ACCDET_CONFIG_4,
			      aad_pdata->d_b_btn_thr);
		snd_soc_component_write(component, DA7219_ACCDET_CONFIG_5,
			      aad_pdata->b_c_btn_thr);
		snd_soc_component_write(component, DA7219_ACCDET_CONFIG_6,
			      aad_pdata->c_mic_btn_thr);

		cfg = 0;
		mask = 0;
		switch (aad_pdata->btn_avg) {
		case DA7219_AAD_BTN_AVG_1:
		case DA7219_AAD_BTN_AVG_2:
		case DA7219_AAD_BTN_AVG_4:
		case DA7219_AAD_BTN_AVG_8:
			cfg |= (aad_pdata->btn_avg <<
				DA7219_BUTTON_AVERAGE_SHIFT);
			mask |= DA7219_BUTTON_AVERAGE_MASK;
		}
		switch (aad_pdata->adc_1bit_rpt) {
		case DA7219_AAD_ADC_1BIT_RPT_1:
		case DA7219_AAD_ADC_1BIT_RPT_2:
		case DA7219_AAD_ADC_1BIT_RPT_4:
		case DA7219_AAD_ADC_1BIT_RPT_8:
			cfg |= (aad_pdata->adc_1bit_rpt <<
			       DA7219_ADC_1_BIT_REPEAT_SHIFT);
			mask |= DA7219_ADC_1_BIT_REPEAT_MASK;
		}
		snd_soc_component_update_bits(component, DA7219_ACCDET_CONFIG_7, mask, cfg);
	}
}

static void da7219_aad_handle_gnd_switch_time(struct snd_soc_component *component)
{
	struct da7219_priv *da7219 = snd_soc_component_get_drvdata(component);
	struct da7219_aad_priv *da7219_aad = da7219->aad;
	u8 jack_det;

	jack_det = snd_soc_component_read(component, DA7219_ACCDET_CONFIG_2)
		& DA7219_JACK_DETECT_RATE_MASK;
	switch (jack_det) {
	case 0x00:
		da7219_aad->gnd_switch_delay = 32;
		break;
	case 0x10:
		da7219_aad->gnd_switch_delay = 64;
		break;
	case 0x20:
		da7219_aad->gnd_switch_delay = 128;
		break;
	case 0x30:
		da7219_aad->gnd_switch_delay = 256;
		break;
	default:
		da7219_aad->gnd_switch_delay = 32;
		break;
	}
}

/*
 * Suspend/Resume
 */

void da7219_aad_suspend(struct snd_soc_component *component)
{
	struct da7219_priv *da7219 = snd_soc_component_get_drvdata(component);
	struct da7219_aad_priv *da7219_aad = da7219->aad;
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	u8 micbias_ctrl;

	if (da7219_aad->jack) {
		/* Disable jack detection during suspend */
		snd_soc_component_update_bits(component, DA7219_ACCDET_CONFIG_1,
				    DA7219_ACCDET_EN_MASK, 0);

		/*
		 * If we have a 4-pole jack inserted, then micbias will be
		 * enabled. We can disable micbias here, and keep a note to
		 * re-enable it on resume. If jack removal occurred during
		 * suspend then this will be dealt with through the IRQ handler.
		 */
		if (da7219_aad->jack_inserted) {
			micbias_ctrl = snd_soc_component_read(component, DA7219_MICBIAS_CTRL);
			if (micbias_ctrl & DA7219_MICBIAS1_EN_MASK) {
				snd_soc_dapm_disable_pin(dapm, "Mic Bias");
				snd_soc_dapm_sync(dapm);
				da7219_aad->micbias_resume_enable = true;
			}
		}
	}
}

void da7219_aad_resume(struct snd_soc_component *component)
{
	struct da7219_priv *da7219 = snd_soc_component_get_drvdata(component);
	struct da7219_aad_priv *da7219_aad = da7219->aad;
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);

	if (da7219_aad->jack) {
		/* Re-enable micbias if previously enabled for 4-pole jack */
		if (da7219_aad->jack_inserted &&
		    da7219_aad->micbias_resume_enable) {
			snd_soc_dapm_force_enable_pin(dapm, "Mic Bias");
			snd_soc_dapm_sync(dapm);
			da7219_aad->micbias_resume_enable = false;
		}

		/* Re-enable jack detection */
		snd_soc_component_update_bits(component, DA7219_ACCDET_CONFIG_1,
				    DA7219_ACCDET_EN_MASK,
				    DA7219_ACCDET_EN_MASK);
	}
}


/*
 * Init/Exit
 */

int da7219_aad_init(struct snd_soc_component *component)
{
	struct da7219_priv *da7219 = snd_soc_component_get_drvdata(component);
	struct da7219_aad_priv *da7219_aad = da7219->aad;
	u8 mask[DA7219_AAD_IRQ_REG_MAX];
	int ret;

	da7219_aad->component = component;

	/* Handle any DT/ACPI/platform data */
	da7219_aad_handle_pdata(component);

	/* Disable button detection */
	snd_soc_component_update_bits(component, DA7219_ACCDET_CONFIG_1,
			    DA7219_BUTTON_CONFIG_MASK, 0);

	INIT_WORK(&da7219_aad->btn_det_work, da7219_aad_btn_det_work);
	INIT_WORK(&da7219_aad->hptest_work, da7219_aad_hptest_work);
	INIT_WORK(&da7219_aad->jack_det_work, da7219_aad_jack_det_work);

	mutex_init(&da7219_aad->jack_det_mutex);

	ret = request_threaded_irq(da7219_aad->irq, da7219_aad_pre_irq_thread,
				   da7219_aad_irq_thread,
				   IRQF_TRIGGER_LOW | IRQF_ONESHOT,
				   "da7219-aad", da7219_aad);
	if (ret) {
		dev_err(component->dev, "Failed to request IRQ: %d\n", ret);
		return ret;
	}

	/* Unmask AAD IRQs */
	memset(mask, 0, DA7219_AAD_IRQ_REG_MAX);
	regmap_bulk_write(da7219->regmap, DA7219_ACCDET_IRQ_MASK_A,
			  &mask, DA7219_AAD_IRQ_REG_MAX);

	da7219_aad_handle_gnd_switch_time(component);

	return 0;
}

void da7219_aad_exit(struct snd_soc_component *component)
{
	struct da7219_priv *da7219 = snd_soc_component_get_drvdata(component);
	struct da7219_aad_priv *da7219_aad = da7219->aad;
	u8 mask[DA7219_AAD_IRQ_REG_MAX];

	/* Mask off AAD IRQs */
	memset(mask, DA7219_BYTE_MASK, DA7219_AAD_IRQ_REG_MAX);
	regmap_bulk_write(da7219->regmap, DA7219_ACCDET_IRQ_MASK_A,
			  mask, DA7219_AAD_IRQ_REG_MAX);

	free_irq(da7219_aad->irq, da7219_aad);

	cancel_work_sync(&da7219_aad->btn_det_work);
	cancel_work_sync(&da7219_aad->hptest_work);
}

/*
 * AAD related I2C probe handling
 */

int da7219_aad_probe(struct i2c_client *i2c)
{
	struct da7219_priv *da7219 = i2c_get_clientdata(i2c);
	struct device *dev = &i2c->dev;
	struct da7219_aad_priv *da7219_aad;

	da7219_aad = devm_kzalloc(dev, sizeof(*da7219_aad), GFP_KERNEL);
	if (!da7219_aad)
		return -ENOMEM;

	da7219->aad = da7219_aad;

	/* Retrieve any DT/ACPI/platform data */
	if (da7219->pdata && !da7219->pdata->aad_pdata)
		da7219->pdata->aad_pdata = da7219_aad_fw_to_pdata(dev);

	return 0;
}

MODULE_DESCRIPTION("ASoC DA7219 AAD Driver");
MODULE_AUTHOR("Adam Thomson <Adam.Thomson.Opensource@diasemi.com>");
MODULE_LICENSE("GPL");
