// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2015-2021, The Linux Foundation. All rights reserved.

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/pm_runtime.h>
#include <linux/printk.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include "wcd-mbhc-v2.h"

#define HS_DETECT_PLUG_TIME_MS		(3 * 1000)
#define MBHC_BUTTON_PRESS_THRESHOLD_MIN	250
#define GND_MIC_SWAP_THRESHOLD		4
#define WCD_FAKE_REMOVAL_MIN_PERIOD_MS	100
#define HPHL_CROSS_CONN_THRESHOLD	100
#define HS_VREF_MIN_VAL			1400
#define FAKE_REM_RETRY_ATTEMPTS		3
#define WCD_MBHC_ADC_HS_THRESHOLD_MV	1700
#define WCD_MBHC_ADC_HPH_THRESHOLD_MV	75
#define WCD_MBHC_ADC_MICBIAS_MV		1800
#define WCD_MBHC_FAKE_INS_RETRY		4

#define WCD_MBHC_JACK_MASK (SND_JACK_HEADSET | SND_JACK_LINEOUT | \
			   SND_JACK_MECHANICAL)

#define WCD_MBHC_JACK_BUTTON_MASK (SND_JACK_BTN_0 | SND_JACK_BTN_1 | \
				  SND_JACK_BTN_2 | SND_JACK_BTN_3 | \
				  SND_JACK_BTN_4 | SND_JACK_BTN_5)

enum wcd_mbhc_adc_mux_ctl {
	MUX_CTL_AUTO = 0,
	MUX_CTL_IN2P,
	MUX_CTL_IN3P,
	MUX_CTL_IN4P,
	MUX_CTL_HPH_L,
	MUX_CTL_HPH_R,
	MUX_CTL_NONE,
};

struct wcd_mbhc {
	struct device *dev;
	struct snd_soc_component *component;
	struct snd_soc_jack *jack;
	struct wcd_mbhc_config *cfg;
	const struct wcd_mbhc_cb *mbhc_cb;
	const struct wcd_mbhc_intr *intr_ids;
	struct wcd_mbhc_field *fields;
	/* Delayed work to report long button press */
	struct delayed_work mbhc_btn_dwork;
	/* Work to correct accessory type */
	struct work_struct correct_plug_swch;
	struct mutex lock;
	int buttons_pressed;
	u32 hph_status; /* track headhpone status */
	u8 current_plug;
	bool is_btn_press;
	bool in_swch_irq_handler;
	bool hs_detect_work_stop;
	bool is_hs_recording;
	bool extn_cable_hph_rem;
	bool force_linein;
	bool impedance_detect;
	unsigned long event_state;
	unsigned long jiffies_atreport;
	/* impedance of hphl and hphr */
	uint32_t zl, zr;
	/* Holds type of Headset - Mono/Stereo */
	enum wcd_mbhc_hph_type hph_type;
	/* Holds mbhc detection method - ADC/Legacy */
	int mbhc_detection_logic;
};

static inline int wcd_mbhc_write_field(const struct wcd_mbhc *mbhc,
				       int field, int val)
{
	if (!mbhc->fields[field].reg)
		return 0;

	return snd_soc_component_write_field(mbhc->component,
					     mbhc->fields[field].reg,
					     mbhc->fields[field].mask, val);
}

static inline int wcd_mbhc_read_field(const struct wcd_mbhc *mbhc, int field)
{
	if (!mbhc->fields[field].reg)
		return 0;

	return snd_soc_component_read_field(mbhc->component,
					    mbhc->fields[field].reg,
					    mbhc->fields[field].mask);
}

static void wcd_program_hs_vref(struct wcd_mbhc *mbhc)
{
	u32 reg_val = ((mbhc->cfg->v_hs_max - HS_VREF_MIN_VAL) / 100);

	wcd_mbhc_write_field(mbhc, WCD_MBHC_HS_VREF, reg_val);
}

static void wcd_program_btn_threshold(const struct wcd_mbhc *mbhc, bool micbias)
{
	struct snd_soc_component *component = mbhc->component;

	mbhc->mbhc_cb->set_btn_thr(component, mbhc->cfg->btn_low,
				   mbhc->cfg->btn_high,
				   mbhc->cfg->num_btn, micbias);
}

static void wcd_mbhc_curr_micbias_control(const struct wcd_mbhc *mbhc,
					  const enum wcd_mbhc_cs_mb_en_flag cs_mb_en)
{

	/*
	 * Some codecs handle micbias/pullup enablement in codec
	 * drivers itself and micbias is not needed for regular
	 * plug type detection. So if micbias_control callback function
	 * is defined, just return.
	 */
	if (mbhc->mbhc_cb->mbhc_micbias_control)
		return;

	switch (cs_mb_en) {
	case WCD_MBHC_EN_CS:
		wcd_mbhc_write_field(mbhc, WCD_MBHC_MICB_CTRL, 0);
		wcd_mbhc_write_field(mbhc, WCD_MBHC_BTN_ISRC_CTL, 3);
		/* Program Button threshold registers as per CS */
		wcd_program_btn_threshold(mbhc, false);
		break;
	case WCD_MBHC_EN_MB:
		wcd_mbhc_write_field(mbhc, WCD_MBHC_BTN_ISRC_CTL, 0);
		wcd_mbhc_write_field(mbhc, WCD_MBHC_FSM_EN, 1);
		/* Disable PULL_UP_EN & enable MICBIAS */
		wcd_mbhc_write_field(mbhc, WCD_MBHC_MICB_CTRL, 2);
		/* Program Button threshold registers as per MICBIAS */
		wcd_program_btn_threshold(mbhc, true);
		break;
	case WCD_MBHC_EN_PULLUP:
		wcd_mbhc_write_field(mbhc, WCD_MBHC_BTN_ISRC_CTL, 3);
		wcd_mbhc_write_field(mbhc, WCD_MBHC_FSM_EN, 1);
		wcd_mbhc_write_field(mbhc, WCD_MBHC_MICB_CTRL, 1);
		/* Program Button threshold registers as per MICBIAS */
		wcd_program_btn_threshold(mbhc, true);
		break;
	case WCD_MBHC_EN_NONE:
		wcd_mbhc_write_field(mbhc, WCD_MBHC_BTN_ISRC_CTL, 0);
		wcd_mbhc_write_field(mbhc, WCD_MBHC_FSM_EN, 1);
		wcd_mbhc_write_field(mbhc, WCD_MBHC_MICB_CTRL, 0);
		break;
	default:
		dev_err(mbhc->dev, "%s: Invalid parameter", __func__);
		break;
	}
}

int wcd_mbhc_event_notify(struct wcd_mbhc *mbhc, unsigned long event)
{

	struct snd_soc_component *component;
	bool micbias2 = false;

	if (!mbhc)
		return 0;

	component = mbhc->component;

	if (mbhc->mbhc_cb->micbias_enable_status)
		micbias2 = mbhc->mbhc_cb->micbias_enable_status(component, MIC_BIAS_2);

	switch (event) {
	/* MICBIAS usage change */
	case WCD_EVENT_POST_DAPM_MICBIAS_2_ON:
		mbhc->is_hs_recording = true;
		break;
	case WCD_EVENT_POST_MICBIAS_2_ON:
		/* Disable current source if micbias2 enabled */
		if (mbhc->mbhc_cb->mbhc_micbias_control) {
			if (wcd_mbhc_read_field(mbhc, WCD_MBHC_FSM_EN))
				wcd_mbhc_write_field(mbhc, WCD_MBHC_BTN_ISRC_CTL, 0);
		} else {
			mbhc->is_hs_recording = true;
			wcd_mbhc_curr_micbias_control(mbhc, WCD_MBHC_EN_MB);
		}
		break;
	case WCD_EVENT_PRE_MICBIAS_2_OFF:
		/*
		 * Before MICBIAS_2 is turned off, if FSM is enabled,
		 * make sure current source is enabled so as to detect
		 * button press/release events
		 */
		if (mbhc->mbhc_cb->mbhc_micbias_control/* && !mbhc->micbias_enable*/) {
			if (wcd_mbhc_read_field(mbhc, WCD_MBHC_FSM_EN))
				wcd_mbhc_write_field(mbhc, WCD_MBHC_BTN_ISRC_CTL, 3);
		}
		break;
	/* MICBIAS usage change */
	case WCD_EVENT_POST_DAPM_MICBIAS_2_OFF:
		mbhc->is_hs_recording = false;
		break;
	case WCD_EVENT_POST_MICBIAS_2_OFF:
		if (!mbhc->mbhc_cb->mbhc_micbias_control)
			mbhc->is_hs_recording = false;

		/* Enable PULL UP if PA's are enabled */
		if ((test_bit(WCD_MBHC_EVENT_PA_HPHL, &mbhc->event_state)) ||
		    (test_bit(WCD_MBHC_EVENT_PA_HPHR, &mbhc->event_state)))
			/* enable pullup and cs, disable mb */
			wcd_mbhc_curr_micbias_control(mbhc, WCD_MBHC_EN_PULLUP);
		else
			/* enable current source and disable mb, pullup*/
			wcd_mbhc_curr_micbias_control(mbhc, WCD_MBHC_EN_CS);

		break;
	case WCD_EVENT_POST_HPHL_PA_OFF:
		clear_bit(WCD_MBHC_EVENT_PA_HPHL, &mbhc->event_state);

		/* check if micbias is enabled */
		if (micbias2)
			/* Disable cs, pullup & enable micbias */
			wcd_mbhc_curr_micbias_control(mbhc, WCD_MBHC_EN_MB);
		else
			/* Disable micbias, pullup & enable cs */
			wcd_mbhc_curr_micbias_control(mbhc, WCD_MBHC_EN_CS);
		break;
	case WCD_EVENT_POST_HPHR_PA_OFF:
		clear_bit(WCD_MBHC_EVENT_PA_HPHR, &mbhc->event_state);
		/* check if micbias is enabled */
		if (micbias2)
			/* Disable cs, pullup & enable micbias */
			wcd_mbhc_curr_micbias_control(mbhc, WCD_MBHC_EN_MB);
		else
			/* Disable micbias, pullup & enable cs */
			wcd_mbhc_curr_micbias_control(mbhc, WCD_MBHC_EN_CS);
		break;
	case WCD_EVENT_PRE_HPHL_PA_ON:
		set_bit(WCD_MBHC_EVENT_PA_HPHL, &mbhc->event_state);
		/* check if micbias is enabled */
		if (micbias2)
			/* Disable cs, pullup & enable micbias */
			wcd_mbhc_curr_micbias_control(mbhc, WCD_MBHC_EN_MB);
		else
			/* Disable micbias, enable pullup & cs */
			wcd_mbhc_curr_micbias_control(mbhc, WCD_MBHC_EN_PULLUP);
		break;
	case WCD_EVENT_PRE_HPHR_PA_ON:
		set_bit(WCD_MBHC_EVENT_PA_HPHR, &mbhc->event_state);
		/* check if micbias is enabled */
		if (micbias2)
			/* Disable cs, pullup & enable micbias */
			wcd_mbhc_curr_micbias_control(mbhc, WCD_MBHC_EN_MB);
		else
			/* Disable micbias, enable pullup & cs */
			wcd_mbhc_curr_micbias_control(mbhc, WCD_MBHC_EN_PULLUP);
		break;
	default:
		break;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(wcd_mbhc_event_notify);

static int wcd_cancel_btn_work(struct wcd_mbhc *mbhc)
{
	return cancel_delayed_work_sync(&mbhc->mbhc_btn_dwork);
}

static void wcd_micbias_disable(struct wcd_mbhc *mbhc)
{
	struct snd_soc_component *component = mbhc->component;

	if (mbhc->mbhc_cb->mbhc_micbias_control)
		mbhc->mbhc_cb->mbhc_micbias_control(component, MIC_BIAS_2, MICB_DISABLE);

	if (mbhc->mbhc_cb->mbhc_micb_ctrl_thr_mic)
		mbhc->mbhc_cb->mbhc_micb_ctrl_thr_mic(component, MIC_BIAS_2, false);

	if (mbhc->mbhc_cb->set_micbias_value) {
		mbhc->mbhc_cb->set_micbias_value(component);
		wcd_mbhc_write_field(mbhc, WCD_MBHC_MICB_CTRL, 0);
	}
}

static void wcd_mbhc_report_plug_removal(struct wcd_mbhc *mbhc,
					 enum snd_jack_types jack_type)
{
	mbhc->hph_status &= ~jack_type;
	/*
	 * cancel possibly scheduled btn work and
	 * report release if we reported button press
	 */
	if (!wcd_cancel_btn_work(mbhc) && mbhc->buttons_pressed) {
		snd_soc_jack_report(mbhc->jack, 0, mbhc->buttons_pressed);
		mbhc->buttons_pressed &= ~WCD_MBHC_JACK_BUTTON_MASK;
	}

	wcd_micbias_disable(mbhc);
	mbhc->hph_type = WCD_MBHC_HPH_NONE;
	mbhc->zl = mbhc->zr = 0;
	snd_soc_jack_report(mbhc->jack, mbhc->hph_status, WCD_MBHC_JACK_MASK);
	mbhc->current_plug = MBHC_PLUG_TYPE_NONE;
	mbhc->force_linein = false;
}

static void wcd_mbhc_compute_impedance(struct wcd_mbhc *mbhc)
{

	if (!mbhc->impedance_detect)
		return;

	if (mbhc->cfg->linein_th != 0) {
		u8 fsm_en = wcd_mbhc_read_field(mbhc, WCD_MBHC_FSM_EN);
		/* Set MUX_CTL to AUTO for Z-det */

		wcd_mbhc_write_field(mbhc, WCD_MBHC_FSM_EN, 0);
		wcd_mbhc_write_field(mbhc, WCD_MBHC_MUX_CTL, MUX_CTL_AUTO);
		wcd_mbhc_write_field(mbhc, WCD_MBHC_FSM_EN, 1);
		mbhc->mbhc_cb->compute_impedance(mbhc->component, &mbhc->zl, &mbhc->zr);
		wcd_mbhc_write_field(mbhc, WCD_MBHC_FSM_EN, fsm_en);
	}
}

static void wcd_mbhc_report_plug_insertion(struct wcd_mbhc *mbhc,
					   enum snd_jack_types jack_type)
{
	bool is_pa_on;
	/*
	 * Report removal of current jack type.
	 * Headphone to headset shouldn't report headphone
	 * removal.
	 */
	if (mbhc->current_plug == MBHC_PLUG_TYPE_HEADSET &&
	    jack_type == SND_JACK_HEADPHONE)
		mbhc->hph_status &= ~SND_JACK_HEADSET;

	/* Report insertion */
	switch (jack_type) {
	case SND_JACK_HEADPHONE:
		mbhc->current_plug = MBHC_PLUG_TYPE_HEADPHONE;
		break;
	case SND_JACK_HEADSET:
		mbhc->current_plug = MBHC_PLUG_TYPE_HEADSET;
		mbhc->jiffies_atreport = jiffies;
		break;
	case SND_JACK_LINEOUT:
		mbhc->current_plug = MBHC_PLUG_TYPE_HIGH_HPH;
		break;
	default:
		break;
	}


	is_pa_on = wcd_mbhc_read_field(mbhc, WCD_MBHC_HPH_PA_EN);

	if (!is_pa_on) {
		wcd_mbhc_compute_impedance(mbhc);
		if ((mbhc->zl > mbhc->cfg->linein_th) &&
		    (mbhc->zr > mbhc->cfg->linein_th) &&
		    (jack_type == SND_JACK_HEADPHONE)) {
			jack_type = SND_JACK_LINEOUT;
			mbhc->force_linein = true;
			mbhc->current_plug = MBHC_PLUG_TYPE_HIGH_HPH;
			if (mbhc->hph_status) {
				mbhc->hph_status &= ~(SND_JACK_HEADSET |
						      SND_JACK_LINEOUT);
				snd_soc_jack_report(mbhc->jack,	mbhc->hph_status,
						    WCD_MBHC_JACK_MASK);
			}
		}
	}

	/* Do not calculate impedance again for lineout
	 * as during playback pa is on and impedance values
	 * will not be correct resulting in lineout detected
	 * as headphone.
	 */
	if (is_pa_on && mbhc->force_linein) {
		jack_type = SND_JACK_LINEOUT;
		mbhc->current_plug = MBHC_PLUG_TYPE_HIGH_HPH;
		if (mbhc->hph_status) {
			mbhc->hph_status &= ~(SND_JACK_HEADSET |
					      SND_JACK_LINEOUT);
			snd_soc_jack_report(mbhc->jack,	mbhc->hph_status,
					    WCD_MBHC_JACK_MASK);
		}
	}

	mbhc->hph_status |= jack_type;

	if (jack_type == SND_JACK_HEADPHONE && mbhc->mbhc_cb->mbhc_micb_ramp_control)
		mbhc->mbhc_cb->mbhc_micb_ramp_control(mbhc->component, false);

	snd_soc_jack_report(mbhc->jack, (mbhc->hph_status | SND_JACK_MECHANICAL),
			    WCD_MBHC_JACK_MASK);
}

static void wcd_mbhc_report_plug(struct wcd_mbhc *mbhc, int insertion,
				 enum snd_jack_types jack_type)
{

	WARN_ON(!mutex_is_locked(&mbhc->lock));

	if (!insertion) /* Report removal */
		wcd_mbhc_report_plug_removal(mbhc, jack_type);
	else
		wcd_mbhc_report_plug_insertion(mbhc, jack_type);

}

static void wcd_cancel_hs_detect_plug(struct wcd_mbhc *mbhc,
				      struct work_struct *work)
{
	mbhc->hs_detect_work_stop = true;
	mutex_unlock(&mbhc->lock);
	cancel_work_sync(work);
	mutex_lock(&mbhc->lock);
}

static void wcd_mbhc_cancel_pending_work(struct wcd_mbhc *mbhc)
{
	/* cancel pending button press */
	wcd_cancel_btn_work(mbhc);
	/* cancel correct work function */
	wcd_cancel_hs_detect_plug(mbhc,	&mbhc->correct_plug_swch);
}

static void wcd_mbhc_elec_hs_report_unplug(struct wcd_mbhc *mbhc)
{
	wcd_mbhc_cancel_pending_work(mbhc);
	/* Report extension cable */
	wcd_mbhc_report_plug(mbhc, 1, SND_JACK_LINEOUT);
	/*
	 * Disable HPHL trigger and MIC Schmitt triggers.
	 * Setup for insertion detection.
	 */
	disable_irq_nosync(mbhc->intr_ids->mbhc_hs_rem_intr);
	wcd_mbhc_curr_micbias_control(mbhc, WCD_MBHC_EN_NONE);
	/* Disable HW FSM */
	wcd_mbhc_write_field(mbhc, WCD_MBHC_FSM_EN, 0);
	wcd_mbhc_write_field(mbhc, WCD_MBHC_ELECT_SCHMT_ISRC, 3);

	/* Set the detection type appropriately */
	wcd_mbhc_write_field(mbhc, WCD_MBHC_ELECT_DETECTION_TYPE, 1);
	enable_irq(mbhc->intr_ids->mbhc_hs_ins_intr);
}

static void wcd_mbhc_find_plug_and_report(struct wcd_mbhc *mbhc,
				   enum wcd_mbhc_plug_type plug_type)
{
	if (mbhc->current_plug == plug_type)
		return;

	mutex_lock(&mbhc->lock);

	switch (plug_type) {
	case MBHC_PLUG_TYPE_HEADPHONE:
		wcd_mbhc_report_plug(mbhc, 1, SND_JACK_HEADPHONE);
		break;
	case MBHC_PLUG_TYPE_HEADSET:
		wcd_mbhc_report_plug(mbhc, 1, SND_JACK_HEADSET);
		break;
	case MBHC_PLUG_TYPE_HIGH_HPH:
		wcd_mbhc_report_plug(mbhc, 1, SND_JACK_LINEOUT);
		break;
	case MBHC_PLUG_TYPE_GND_MIC_SWAP:
		if (mbhc->current_plug == MBHC_PLUG_TYPE_HEADPHONE)
			wcd_mbhc_report_plug(mbhc, 0, SND_JACK_HEADPHONE);
		if (mbhc->current_plug == MBHC_PLUG_TYPE_HEADSET)
			wcd_mbhc_report_plug(mbhc, 0, SND_JACK_HEADSET);
		break;
	default:
		WARN(1, "Unexpected current plug_type %d, plug_type %d\n",
		     mbhc->current_plug, plug_type);
		break;
	}
	mutex_unlock(&mbhc->lock);
}

static void wcd_schedule_hs_detect_plug(struct wcd_mbhc *mbhc,
					    struct work_struct *work)
{
	WARN_ON(!mutex_is_locked(&mbhc->lock));
	mbhc->hs_detect_work_stop = false;
	schedule_work(work);
}

static void wcd_mbhc_adc_detect_plug_type(struct wcd_mbhc *mbhc)
{
	struct snd_soc_component *component = mbhc->component;

	WARN_ON(!mutex_is_locked(&mbhc->lock));

	if (mbhc->mbhc_cb->hph_pull_down_ctrl)
		mbhc->mbhc_cb->hph_pull_down_ctrl(component, false);

	wcd_mbhc_write_field(mbhc, WCD_MBHC_DETECTION_DONE, 0);

	if (mbhc->mbhc_cb->mbhc_micbias_control) {
		mbhc->mbhc_cb->mbhc_micbias_control(component, MIC_BIAS_2,
						    MICB_ENABLE);
		wcd_schedule_hs_detect_plug(mbhc, &mbhc->correct_plug_swch);
	}
}

static irqreturn_t wcd_mbhc_mech_plug_detect_irq(int irq, void *data)
{
	struct snd_soc_component *component;
	enum snd_jack_types jack_type;
	struct wcd_mbhc *mbhc = data;
	bool detection_type;

	component = mbhc->component;
	mutex_lock(&mbhc->lock);

	mbhc->in_swch_irq_handler = true;

	wcd_mbhc_cancel_pending_work(mbhc);

	detection_type = wcd_mbhc_read_field(mbhc, WCD_MBHC_MECH_DETECTION_TYPE);

	/* Set the detection type appropriately */
	wcd_mbhc_write_field(mbhc, WCD_MBHC_MECH_DETECTION_TYPE, !detection_type);

	/* Enable micbias ramp */
	if (mbhc->mbhc_cb->mbhc_micb_ramp_control)
		mbhc->mbhc_cb->mbhc_micb_ramp_control(component, true);

	if (detection_type) {
		if (mbhc->current_plug != MBHC_PLUG_TYPE_NONE)
			goto exit;
		/* Make sure MASTER_BIAS_CTL is enabled */
		mbhc->mbhc_cb->mbhc_bias(component, true);
		mbhc->is_btn_press = false;
		wcd_mbhc_adc_detect_plug_type(mbhc);
	} else {
		/* Disable HW FSM */
		wcd_mbhc_write_field(mbhc, WCD_MBHC_FSM_EN, 0);
		wcd_mbhc_write_field(mbhc, WCD_MBHC_BTN_ISRC_CTL, 0);
		mbhc->extn_cable_hph_rem = false;

		if (mbhc->current_plug == MBHC_PLUG_TYPE_NONE)
			goto exit;

		mbhc->is_btn_press = false;
		switch (mbhc->current_plug) {
		case MBHC_PLUG_TYPE_HEADPHONE:
			jack_type = SND_JACK_HEADPHONE;
			break;
		case MBHC_PLUG_TYPE_HEADSET:
			jack_type = SND_JACK_HEADSET;
			break;
		case MBHC_PLUG_TYPE_HIGH_HPH:
			if (mbhc->mbhc_detection_logic == WCD_DETECTION_ADC)
				wcd_mbhc_write_field(mbhc, WCD_MBHC_ELECT_ISRC_EN, 0);
			jack_type = SND_JACK_LINEOUT;
			break;
		case MBHC_PLUG_TYPE_GND_MIC_SWAP:
			dev_err(mbhc->dev, "Ground and Mic Swapped on plug\n");
			goto exit;
		default:
			dev_err(mbhc->dev, "Invalid current plug: %d\n",
				mbhc->current_plug);
			goto exit;
		}
		disable_irq_nosync(mbhc->intr_ids->mbhc_hs_rem_intr);
		disable_irq_nosync(mbhc->intr_ids->mbhc_hs_ins_intr);
		wcd_mbhc_write_field(mbhc, WCD_MBHC_ELECT_DETECTION_TYPE, 1);
		wcd_mbhc_write_field(mbhc, WCD_MBHC_ELECT_SCHMT_ISRC, 0);
		wcd_mbhc_report_plug(mbhc, 0, jack_type);
	}

exit:
	mbhc->in_swch_irq_handler = false;
	mutex_unlock(&mbhc->lock);
	return IRQ_HANDLED;
}

static int wcd_mbhc_get_button_mask(struct wcd_mbhc *mbhc)
{
	int mask = 0;
	int btn;

	btn = wcd_mbhc_read_field(mbhc, WCD_MBHC_BTN_RESULT);

	switch (btn) {
	case 0:
		mask = SND_JACK_BTN_0;
		break;
	case 1:
		mask = SND_JACK_BTN_1;
		break;
	case 2:
		mask = SND_JACK_BTN_2;
		break;
	case 3:
		mask = SND_JACK_BTN_3;
		break;
	case 4:
		mask = SND_JACK_BTN_4;
		break;
	case 5:
		mask = SND_JACK_BTN_5;
		break;
	default:
		break;
	}

	return mask;
}

static void wcd_btn_long_press_fn(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct wcd_mbhc *mbhc = container_of(dwork, struct wcd_mbhc, mbhc_btn_dwork);

	if (mbhc->current_plug == MBHC_PLUG_TYPE_HEADSET)
		snd_soc_jack_report(mbhc->jack, mbhc->buttons_pressed,
				    mbhc->buttons_pressed);
}

static irqreturn_t wcd_mbhc_btn_press_handler(int irq, void *data)
{
	struct wcd_mbhc *mbhc = data;
	int mask;
	unsigned long msec_val;

	mutex_lock(&mbhc->lock);
	wcd_cancel_btn_work(mbhc);
	mbhc->is_btn_press = true;
	msec_val = jiffies_to_msecs(jiffies - mbhc->jiffies_atreport);

	/* Too short, ignore button press */
	if (msec_val < MBHC_BUTTON_PRESS_THRESHOLD_MIN)
		goto done;

	/* If switch interrupt already kicked in, ignore button press */
	if (mbhc->in_swch_irq_handler)
		goto done;

	/* Plug isn't headset, ignore button press */
	if (mbhc->current_plug != MBHC_PLUG_TYPE_HEADSET)
		goto done;

	mask = wcd_mbhc_get_button_mask(mbhc);
	mbhc->buttons_pressed |= mask;
	if (schedule_delayed_work(&mbhc->mbhc_btn_dwork, msecs_to_jiffies(400)) == 0)
		WARN(1, "Button pressed twice without release event\n");
done:
	mutex_unlock(&mbhc->lock);
	return IRQ_HANDLED;
}

static irqreturn_t wcd_mbhc_btn_release_handler(int irq, void *data)
{
	struct wcd_mbhc *mbhc = data;
	int ret;

	mutex_lock(&mbhc->lock);
	if (mbhc->is_btn_press)
		mbhc->is_btn_press = false;
	else /* fake btn press */
		goto exit;

	if (!(mbhc->buttons_pressed & WCD_MBHC_JACK_BUTTON_MASK))
		goto exit;

	ret = wcd_cancel_btn_work(mbhc);
	if (ret == 0) { /* Reporting long button release event */
		snd_soc_jack_report(mbhc->jack,	0, mbhc->buttons_pressed);
	} else {
		if (!mbhc->in_swch_irq_handler) {
			/* Reporting btn press n Release */
			snd_soc_jack_report(mbhc->jack, mbhc->buttons_pressed,
					    mbhc->buttons_pressed);
			snd_soc_jack_report(mbhc->jack,	0, mbhc->buttons_pressed);
		}
	}
	mbhc->buttons_pressed &= ~WCD_MBHC_JACK_BUTTON_MASK;
exit:
	mutex_unlock(&mbhc->lock);

	return IRQ_HANDLED;
}

static irqreturn_t wcd_mbhc_hph_ocp_irq(struct wcd_mbhc *mbhc, bool hphr)
{

	/* TODO Find a better way to report this to Userspace */
	dev_err(mbhc->dev, "MBHC Over Current on %s detected\n",
		hphr ? "HPHR" : "HPHL");

	wcd_mbhc_write_field(mbhc, WCD_MBHC_OCP_FSM_EN, 0);
	wcd_mbhc_write_field(mbhc, WCD_MBHC_OCP_FSM_EN, 1);

	return IRQ_HANDLED;
}

static irqreturn_t wcd_mbhc_hphl_ocp_irq(int irq, void *data)
{
	return wcd_mbhc_hph_ocp_irq(data, false);
}

static irqreturn_t wcd_mbhc_hphr_ocp_irq(int irq, void *data)
{
	return wcd_mbhc_hph_ocp_irq(data, true);
}

static int wcd_mbhc_initialise(struct wcd_mbhc *mbhc)
{
	struct snd_soc_component *component = mbhc->component;
	int ret;

	ret = pm_runtime_get_sync(component->dev);
	if (ret < 0 && ret != -EACCES) {
		dev_err_ratelimited(component->dev,
				    "pm_runtime_get_sync failed in %s, ret %d\n",
				    __func__, ret);
		pm_runtime_put_noidle(component->dev);
		return ret;
	}

	mutex_lock(&mbhc->lock);

	/* enable HS detection */
	if (mbhc->mbhc_cb->hph_pull_up_control_v2)
		mbhc->mbhc_cb->hph_pull_up_control_v2(component,
						      HS_PULLUP_I_DEFAULT);
	else if (mbhc->mbhc_cb->hph_pull_up_control)
		mbhc->mbhc_cb->hph_pull_up_control(component, I_DEFAULT);
	else
		wcd_mbhc_write_field(mbhc, WCD_MBHC_HS_L_DET_PULL_UP_CTRL, 3);

	wcd_mbhc_write_field(mbhc, WCD_MBHC_HPHL_PLUG_TYPE, mbhc->cfg->hphl_swh);
	wcd_mbhc_write_field(mbhc, WCD_MBHC_GND_PLUG_TYPE, mbhc->cfg->gnd_swh);
	wcd_mbhc_write_field(mbhc, WCD_MBHC_SW_HPH_LP_100K_TO_GND, 1);
	if (mbhc->cfg->gnd_det_en && mbhc->mbhc_cb->mbhc_gnd_det_ctrl)
		mbhc->mbhc_cb->mbhc_gnd_det_ctrl(component, true);
	wcd_mbhc_write_field(mbhc, WCD_MBHC_HS_L_DET_PULL_UP_COMP_CTRL, 1);

	wcd_mbhc_write_field(mbhc, WCD_MBHC_L_DET_EN, 1);

	/* Insertion debounce set to 96ms */
	wcd_mbhc_write_field(mbhc, WCD_MBHC_INSREM_DBNC, 6);

	/* Button Debounce set to 16ms */
	wcd_mbhc_write_field(mbhc, WCD_MBHC_BTN_DBNC, 2);

	/* enable bias */
	mbhc->mbhc_cb->mbhc_bias(component, true);
	/* enable MBHC clock */
	if (mbhc->mbhc_cb->clk_setup)
		mbhc->mbhc_cb->clk_setup(component, true);

	/* program HS_VREF value */
	wcd_program_hs_vref(mbhc);

	wcd_program_btn_threshold(mbhc, false);

	mutex_unlock(&mbhc->lock);

	pm_runtime_mark_last_busy(component->dev);
	pm_runtime_put_autosuspend(component->dev);

	return 0;
}

static int wcd_mbhc_get_micbias(struct wcd_mbhc *mbhc)
{
	int micbias = 0;

	if (mbhc->mbhc_cb->get_micbias_val) {
		mbhc->mbhc_cb->get_micbias_val(mbhc->component, &micbias);
	} else {
		u8 vout_ctl = 0;
		/* Read MBHC Micbias (Mic Bias2) voltage */
		vout_ctl = wcd_mbhc_read_field(mbhc, WCD_MBHC_MICB2_VOUT);
		/* Formula for getting micbias from vout
		 * micbias = 1.0V + VOUT_CTL * 50mV
		 */
		micbias = 1000 + (vout_ctl * 50);
	}
	return micbias;
}

static int wcd_get_voltage_from_adc(u8 val, int micbias)
{
	/* Formula for calculating voltage from ADC
	 * Voltage = ADC_RESULT*12.5mV*V_MICBIAS/1.8
	 */
	return ((val * 125 * micbias)/(WCD_MBHC_ADC_MICBIAS_MV * 10));
}

static int wcd_measure_adc_continuous(struct wcd_mbhc *mbhc)
{
	u8 adc_result;
	int output_mv;
	int retry = 3;
	u8 adc_en;

	/* Pre-requisites for ADC continuous measurement */
	/* Read legacy electircal detection and disable */
	wcd_mbhc_write_field(mbhc, WCD_MBHC_ELECT_SCHMT_ISRC, 0x00);
	/* Set ADC to continuous measurement */
	wcd_mbhc_write_field(mbhc, WCD_MBHC_ADC_MODE, 1);
	/* Read ADC Enable bit to restore after adc measurement */
	adc_en = wcd_mbhc_read_field(mbhc, WCD_MBHC_ADC_EN);
	/* Disable ADC_ENABLE bit */
	wcd_mbhc_write_field(mbhc, WCD_MBHC_ADC_EN, 0);
	/* Disable MBHC FSM */
	wcd_mbhc_write_field(mbhc, WCD_MBHC_FSM_EN, 0);
	/* Set the MUX selection to IN2P */
	wcd_mbhc_write_field(mbhc, WCD_MBHC_MUX_CTL, MUX_CTL_IN2P);
	/* Enable MBHC FSM */
	wcd_mbhc_write_field(mbhc, WCD_MBHC_FSM_EN, 1);
	/* Enable ADC_ENABLE bit */
	wcd_mbhc_write_field(mbhc, WCD_MBHC_ADC_EN, 1);

	while (retry--) {
		/* wait for 3 msec before reading ADC result */
		usleep_range(3000, 3100);
		adc_result = wcd_mbhc_read_field(mbhc, WCD_MBHC_ADC_RESULT);
	}

	/* Restore ADC Enable */
	wcd_mbhc_write_field(mbhc, WCD_MBHC_ADC_EN, adc_en);
	/* Get voltage from ADC result */
	output_mv = wcd_get_voltage_from_adc(adc_result, wcd_mbhc_get_micbias(mbhc));

	return output_mv;
}

static int wcd_measure_adc_once(struct wcd_mbhc *mbhc, int mux_ctl)
{
	struct device *dev = mbhc->dev;
	u8 adc_timeout = 0;
	u8 adc_complete = 0;
	u8 adc_result;
	int retry = 6;
	int ret;
	int output_mv = 0;
	u8 adc_en;

	wcd_mbhc_write_field(mbhc, WCD_MBHC_ADC_MODE, 0);
	/* Read ADC Enable bit to restore after adc measurement */
	adc_en = wcd_mbhc_read_field(mbhc, WCD_MBHC_ADC_EN);
	/* Trigger ADC one time measurement */
	wcd_mbhc_write_field(mbhc, WCD_MBHC_ADC_EN, 0);
	wcd_mbhc_write_field(mbhc, WCD_MBHC_FSM_EN, 0);
	/* Set the appropriate MUX selection */
	wcd_mbhc_write_field(mbhc, WCD_MBHC_MUX_CTL, mux_ctl);
	wcd_mbhc_write_field(mbhc, WCD_MBHC_FSM_EN, 1);
	wcd_mbhc_write_field(mbhc, WCD_MBHC_ADC_EN, 1);

	while (retry--) {
		/* wait for 600usec to get adc results */
		usleep_range(600, 610);

		/* check for ADC Timeout */
		adc_timeout = wcd_mbhc_read_field(mbhc, WCD_MBHC_ADC_TIMEOUT);
		if (adc_timeout)
			continue;

		/* Read ADC complete bit */
		adc_complete = wcd_mbhc_read_field(mbhc, WCD_MBHC_ADC_COMPLETE);
		if (!adc_complete)
			continue;

		/* Read ADC result */
		adc_result = wcd_mbhc_read_field(mbhc, WCD_MBHC_ADC_RESULT);

		/* Get voltage from ADC result */
		output_mv = wcd_get_voltage_from_adc(adc_result,
						wcd_mbhc_get_micbias(mbhc));
		break;
	}

	/* Restore ADC Enable */
	wcd_mbhc_write_field(mbhc, WCD_MBHC_ADC_EN, adc_en);

	if (retry <= 0) {
		dev_err(dev, "%s: adc complete: %d, adc timeout: %d\n",
			__func__, adc_complete, adc_timeout);
		ret = -EINVAL;
	} else {
		ret = output_mv;
	}

	return ret;
}

/* To determine if cross connection occurred */
static int wcd_check_cross_conn(struct wcd_mbhc *mbhc)
{
	u8 adc_mode, elect_ctl, adc_en, fsm_en;
	int hphl_adc_res, hphr_adc_res;
	bool is_cross_conn = false;

	/* If PA is enabled, dont check for cross-connection */
	if (wcd_mbhc_read_field(mbhc, WCD_MBHC_HPH_PA_EN))
		return -EINVAL;

	/* Read legacy electircal detection and disable */
	elect_ctl = wcd_mbhc_read_field(mbhc, WCD_MBHC_ELECT_SCHMT_ISRC);
	wcd_mbhc_write_field(mbhc, WCD_MBHC_ELECT_SCHMT_ISRC, 0);

	/* Read and set ADC to single measurement */
	adc_mode = wcd_mbhc_read_field(mbhc, WCD_MBHC_ADC_MODE);
	/* Read ADC Enable bit to restore after adc measurement */
	adc_en = wcd_mbhc_read_field(mbhc, WCD_MBHC_ADC_EN);
	/* Read FSM status */
	fsm_en = wcd_mbhc_read_field(mbhc, WCD_MBHC_FSM_EN);

	/* Get adc result for HPH L */
	hphl_adc_res = wcd_measure_adc_once(mbhc, MUX_CTL_HPH_L);
	if (hphl_adc_res < 0)
		return hphl_adc_res;

	/* Get adc result for HPH R in mV */
	hphr_adc_res = wcd_measure_adc_once(mbhc, MUX_CTL_HPH_R);
	if (hphr_adc_res < 0)
		return hphr_adc_res;

	if (hphl_adc_res > HPHL_CROSS_CONN_THRESHOLD ||
	    hphr_adc_res > HPHL_CROSS_CONN_THRESHOLD)
		is_cross_conn = true;

	wcd_mbhc_write_field(mbhc, WCD_MBHC_FSM_EN, 0);
	/* Set the MUX selection to Auto */
	wcd_mbhc_write_field(mbhc, WCD_MBHC_MUX_CTL, MUX_CTL_AUTO);
	wcd_mbhc_write_field(mbhc, WCD_MBHC_FSM_EN, 1);
	/* Restore ADC Enable */
	wcd_mbhc_write_field(mbhc, WCD_MBHC_ADC_EN, adc_en);
	/* Restore ADC mode */
	wcd_mbhc_write_field(mbhc, WCD_MBHC_ADC_MODE, adc_mode);
	/* Restore FSM state */
	wcd_mbhc_write_field(mbhc, WCD_MBHC_FSM_EN, fsm_en);
	/* Restore electrical detection */
	wcd_mbhc_write_field(mbhc, WCD_MBHC_ELECT_SCHMT_ISRC, elect_ctl);

	return is_cross_conn;
}

static int wcd_mbhc_adc_get_hs_thres(struct wcd_mbhc *mbhc)
{
	int hs_threshold, micbias_mv;

	micbias_mv = wcd_mbhc_get_micbias(mbhc);
	if (mbhc->cfg->hs_thr) {
		if (mbhc->cfg->micb_mv == micbias_mv)
			hs_threshold = mbhc->cfg->hs_thr;
		else
			hs_threshold = (mbhc->cfg->hs_thr *
				micbias_mv) / mbhc->cfg->micb_mv;
	} else {
		hs_threshold = ((WCD_MBHC_ADC_HS_THRESHOLD_MV *
			micbias_mv) / WCD_MBHC_ADC_MICBIAS_MV);
	}
	return hs_threshold;
}

static int wcd_mbhc_adc_get_hph_thres(struct wcd_mbhc *mbhc)
{
	int hph_threshold, micbias_mv;

	micbias_mv = wcd_mbhc_get_micbias(mbhc);
	if (mbhc->cfg->hph_thr) {
		if (mbhc->cfg->micb_mv == micbias_mv)
			hph_threshold = mbhc->cfg->hph_thr;
		else
			hph_threshold = (mbhc->cfg->hph_thr *
				micbias_mv) / mbhc->cfg->micb_mv;
	} else {
		hph_threshold = ((WCD_MBHC_ADC_HPH_THRESHOLD_MV *
			micbias_mv) / WCD_MBHC_ADC_MICBIAS_MV);
	}
	return hph_threshold;
}

static void wcd_mbhc_adc_update_fsm_source(struct wcd_mbhc *mbhc,
					   enum wcd_mbhc_plug_type plug_type)
{
	bool micbias2 = false;

	switch (plug_type) {
	case MBHC_PLUG_TYPE_HEADPHONE:
		wcd_mbhc_write_field(mbhc, WCD_MBHC_BTN_ISRC_CTL, 3);
		break;
	case MBHC_PLUG_TYPE_HEADSET:
		if (mbhc->mbhc_cb->micbias_enable_status)
			micbias2 = mbhc->mbhc_cb->micbias_enable_status(mbhc->component,
									MIC_BIAS_2);

		if (!mbhc->is_hs_recording && !micbias2)
			wcd_mbhc_write_field(mbhc, WCD_MBHC_BTN_ISRC_CTL, 3);
		break;
	default:
		wcd_mbhc_write_field(mbhc, WCD_MBHC_BTN_ISRC_CTL, 0);
		break;

	}
}

static void wcd_mbhc_bcs_enable(struct wcd_mbhc *mbhc, int plug_type, bool enable)
{
	switch (plug_type) {
	case MBHC_PLUG_TYPE_HEADSET:
	case MBHC_PLUG_TYPE_HEADPHONE:
		if (mbhc->mbhc_cb->bcs_enable)
			mbhc->mbhc_cb->bcs_enable(mbhc->component, enable);
		break;
	default:
		break;
	}
}

static int wcd_mbhc_get_plug_from_adc(struct wcd_mbhc *mbhc, int adc_result)

{
	enum wcd_mbhc_plug_type plug_type;
	u32 hph_thr, hs_thr;

	hs_thr = wcd_mbhc_adc_get_hs_thres(mbhc);
	hph_thr = wcd_mbhc_adc_get_hph_thres(mbhc);

	if (adc_result < hph_thr)
		plug_type = MBHC_PLUG_TYPE_HEADPHONE;
	else if (adc_result > hs_thr)
		plug_type = MBHC_PLUG_TYPE_HIGH_HPH;
	else
		plug_type = MBHC_PLUG_TYPE_HEADSET;

	return plug_type;
}

static int wcd_mbhc_get_spl_hs_thres(struct wcd_mbhc *mbhc)
{
	int hs_threshold, micbias_mv;

	micbias_mv = wcd_mbhc_get_micbias(mbhc);
	if (mbhc->cfg->hs_thr && mbhc->cfg->micb_mv != WCD_MBHC_ADC_MICBIAS_MV) {
		if (mbhc->cfg->micb_mv == micbias_mv)
			hs_threshold = mbhc->cfg->hs_thr;
		else
			hs_threshold = (mbhc->cfg->hs_thr * micbias_mv) / mbhc->cfg->micb_mv;
	} else {
		hs_threshold = ((WCD_MBHC_ADC_HS_THRESHOLD_MV * micbias_mv) /
							WCD_MBHC_ADC_MICBIAS_MV);
	}
	return hs_threshold;
}

static bool wcd_mbhc_check_for_spl_headset(struct wcd_mbhc *mbhc)
{
	bool is_spl_hs = false;
	int output_mv, hs_threshold, hph_threshold;

	if (!mbhc->mbhc_cb->mbhc_micb_ctrl_thr_mic)
		return false;

	/* Bump up MIC_BIAS2 to 2.7V */
	mbhc->mbhc_cb->mbhc_micb_ctrl_thr_mic(mbhc->component, MIC_BIAS_2, true);
	usleep_range(10000, 10100);

	output_mv = wcd_measure_adc_once(mbhc, MUX_CTL_IN2P);
	hs_threshold = wcd_mbhc_get_spl_hs_thres(mbhc);
	hph_threshold = wcd_mbhc_adc_get_hph_thres(mbhc);

	if (!(output_mv > hs_threshold || output_mv < hph_threshold))
		is_spl_hs = true;

	/* Back MIC_BIAS2 to 1.8v if the type is not special headset */
	if (!is_spl_hs) {
		mbhc->mbhc_cb->mbhc_micb_ctrl_thr_mic(mbhc->component, MIC_BIAS_2, false);
		/* Add 10ms delay for micbias to settle */
		usleep_range(10000, 10100);
	}

	return is_spl_hs;
}

static void wcd_correct_swch_plug(struct work_struct *work)
{
	struct wcd_mbhc *mbhc;
	struct snd_soc_component *component;
	enum wcd_mbhc_plug_type plug_type = MBHC_PLUG_TYPE_INVALID;
	unsigned long timeout;
	int pt_gnd_mic_swap_cnt = 0;
	int output_mv, cross_conn, hs_threshold, try = 0, micbias_mv;
	bool is_spl_hs = false;
	bool is_pa_on;
	int ret;

	mbhc = container_of(work, struct wcd_mbhc, correct_plug_swch);
	component = mbhc->component;

	ret = pm_runtime_get_sync(component->dev);
	if (ret < 0 && ret != -EACCES) {
		dev_err_ratelimited(component->dev,
				    "pm_runtime_get_sync failed in %s, ret %d\n",
				    __func__, ret);
		pm_runtime_put_noidle(component->dev);
		return;
	}
	micbias_mv = wcd_mbhc_get_micbias(mbhc);
	hs_threshold = wcd_mbhc_adc_get_hs_thres(mbhc);

	/* Mask ADC COMPLETE interrupt */
	disable_irq_nosync(mbhc->intr_ids->mbhc_hs_ins_intr);

	/* Check for cross connection */
	do {
		cross_conn = wcd_check_cross_conn(mbhc);
		try++;
	} while (try < GND_MIC_SWAP_THRESHOLD);

	if (cross_conn > 0) {
		plug_type = MBHC_PLUG_TYPE_GND_MIC_SWAP;
		dev_err(mbhc->dev, "cross connection found, Plug type %d\n",
			plug_type);
		goto correct_plug_type;
	}

	/* Find plug type */
	output_mv = wcd_measure_adc_continuous(mbhc);
	plug_type = wcd_mbhc_get_plug_from_adc(mbhc, output_mv);

	/*
	 * Report plug type if it is either headset or headphone
	 * else start the 3 sec loop
	 */
	switch (plug_type) {
	case MBHC_PLUG_TYPE_HEADPHONE:
		wcd_mbhc_find_plug_and_report(mbhc, plug_type);
		break;
	case MBHC_PLUG_TYPE_HEADSET:
		wcd_mbhc_find_plug_and_report(mbhc, plug_type);
		wcd_mbhc_write_field(mbhc, WCD_MBHC_ADC_MODE, 0);
		wcd_mbhc_write_field(mbhc, WCD_MBHC_ADC_EN, 0);
		wcd_mbhc_write_field(mbhc, WCD_MBHC_DETECTION_DONE, 1);
		break;
	default:
		break;
	}

correct_plug_type:

	/* Disable BCS slow insertion detection */
	wcd_mbhc_bcs_enable(mbhc, plug_type, false);

	timeout = jiffies + msecs_to_jiffies(HS_DETECT_PLUG_TIME_MS);

	while (!time_after(jiffies, timeout)) {
		if (mbhc->hs_detect_work_stop) {
			wcd_micbias_disable(mbhc);
			goto exit;
		}

		msleep(180);
		/*
		 * Use ADC single mode to minimize the chance of missing out
		 * btn press/release for HEADSET type during correct work.
		 */
		output_mv = wcd_measure_adc_once(mbhc, MUX_CTL_IN2P);
		plug_type = wcd_mbhc_get_plug_from_adc(mbhc, output_mv);
		is_pa_on = wcd_mbhc_read_field(mbhc, WCD_MBHC_HPH_PA_EN);

		if (output_mv > hs_threshold && !is_spl_hs) {
			is_spl_hs = wcd_mbhc_check_for_spl_headset(mbhc);
			output_mv = wcd_measure_adc_once(mbhc, MUX_CTL_IN2P);

			if (is_spl_hs) {
				hs_threshold *= wcd_mbhc_get_micbias(mbhc);
				hs_threshold /= micbias_mv;
			}
		}

		if ((output_mv <= hs_threshold) && !is_pa_on) {
			/* Check for cross connection*/
			cross_conn = wcd_check_cross_conn(mbhc);
			if (cross_conn > 0) { /* cross-connection */
				pt_gnd_mic_swap_cnt++;
				if (pt_gnd_mic_swap_cnt < GND_MIC_SWAP_THRESHOLD)
					continue;
				else
					plug_type = MBHC_PLUG_TYPE_GND_MIC_SWAP;
			} else if (!cross_conn) { /* no cross connection */
				pt_gnd_mic_swap_cnt = 0;
				plug_type = wcd_mbhc_get_plug_from_adc(mbhc, output_mv);
				continue;
			} else /* Error if (cross_conn < 0) */
				continue;

			if (pt_gnd_mic_swap_cnt == GND_MIC_SWAP_THRESHOLD) {
				/* US_EU gpio present, flip switch */
				if (mbhc->cfg->swap_gnd_mic) {
					if (mbhc->cfg->swap_gnd_mic(component, true))
						continue;
				}
			}
		}

		/* cable is extension cable */
		if (output_mv > hs_threshold || mbhc->force_linein)
			plug_type = MBHC_PLUG_TYPE_HIGH_HPH;
	}

	wcd_mbhc_bcs_enable(mbhc, plug_type, true);

	if (plug_type == MBHC_PLUG_TYPE_HIGH_HPH) {
		if (is_spl_hs)
			plug_type = MBHC_PLUG_TYPE_HEADSET;
		else
			wcd_mbhc_write_field(mbhc, WCD_MBHC_ELECT_ISRC_EN, 1);
	}

	wcd_mbhc_write_field(mbhc, WCD_MBHC_ADC_MODE, 0);
	wcd_mbhc_write_field(mbhc, WCD_MBHC_ADC_EN, 0);
	wcd_mbhc_find_plug_and_report(mbhc, plug_type);

	/*
	 * Set DETECTION_DONE bit for HEADSET
	 * so that btn press/release interrupt can be generated.
	 * For other plug type, clear the bit.
	 */
	if (plug_type == MBHC_PLUG_TYPE_HEADSET)
		wcd_mbhc_write_field(mbhc, WCD_MBHC_DETECTION_DONE, 1);
	else
		wcd_mbhc_write_field(mbhc, WCD_MBHC_DETECTION_DONE, 0);

	if (mbhc->mbhc_cb->mbhc_micbias_control)
		wcd_mbhc_adc_update_fsm_source(mbhc, plug_type);

exit:
	if (mbhc->mbhc_cb->mbhc_micbias_control/* &&  !mbhc->micbias_enable*/)
		mbhc->mbhc_cb->mbhc_micbias_control(component, MIC_BIAS_2, MICB_DISABLE);

	/*
	 * If plug type is corrected from special headset to headphone,
	 * clear the micbias enable flag, set micbias back to 1.8V and
	 * disable micbias.
	 */
	if (plug_type == MBHC_PLUG_TYPE_HEADPHONE) {
		wcd_micbias_disable(mbhc);
		/*
		 * Enable ADC COMPLETE interrupt for HEADPHONE.
		 * Btn release may happen after the correct work, ADC COMPLETE
		 * interrupt needs to be captured to correct plug type.
		 */
		enable_irq(mbhc->intr_ids->mbhc_hs_ins_intr);
	}

	if (mbhc->mbhc_cb->hph_pull_down_ctrl)
		mbhc->mbhc_cb->hph_pull_down_ctrl(component, true);

	pm_runtime_mark_last_busy(component->dev);
	pm_runtime_put_autosuspend(component->dev);
}

static irqreturn_t wcd_mbhc_adc_hs_rem_irq(int irq, void *data)
{
	struct wcd_mbhc *mbhc = data;
	unsigned long timeout;
	int adc_threshold, output_mv, retry = 0;

	mutex_lock(&mbhc->lock);
	timeout = jiffies + msecs_to_jiffies(WCD_FAKE_REMOVAL_MIN_PERIOD_MS);
	adc_threshold = wcd_mbhc_adc_get_hs_thres(mbhc);

	do {
		retry++;
		/*
		 * read output_mv every 10ms to look for
		 * any change in IN2_P
		 */
		usleep_range(10000, 10100);
		output_mv = wcd_measure_adc_once(mbhc, MUX_CTL_IN2P);

		/* Check for fake removal */
		if ((output_mv <= adc_threshold) && retry > FAKE_REM_RETRY_ATTEMPTS)
			goto exit;
	} while (!time_after(jiffies, timeout));

	/*
	 * ADC COMPLETE and ELEC_REM interrupts are both enabled for
	 * HEADPHONE, need to reject the ADC COMPLETE interrupt which
	 * follows ELEC_REM one when HEADPHONE is removed.
	 */
	if (mbhc->current_plug == MBHC_PLUG_TYPE_HEADPHONE)
		mbhc->extn_cable_hph_rem = true;

	wcd_mbhc_write_field(mbhc, WCD_MBHC_DETECTION_DONE, 0);
	wcd_mbhc_write_field(mbhc, WCD_MBHC_ADC_MODE, 0);
	wcd_mbhc_write_field(mbhc, WCD_MBHC_ADC_EN, 0);
	wcd_mbhc_elec_hs_report_unplug(mbhc);
	wcd_mbhc_write_field(mbhc, WCD_MBHC_BTN_ISRC_CTL, 0);

exit:
	mutex_unlock(&mbhc->lock);
	return IRQ_HANDLED;
}

static irqreturn_t wcd_mbhc_adc_hs_ins_irq(int irq, void *data)
{
	struct wcd_mbhc *mbhc = data;
	u8 clamp_state;
	u8 clamp_retry = WCD_MBHC_FAKE_INS_RETRY;

	/*
	 * ADC COMPLETE and ELEC_REM interrupts are both enabled for HEADPHONE,
	 * need to reject the ADC COMPLETE interrupt which follows ELEC_REM one
	 * when HEADPHONE is removed.
	 */
	if (mbhc->extn_cable_hph_rem == true) {
		mbhc->extn_cable_hph_rem = false;
		return IRQ_HANDLED;
	}

	do {
		clamp_state = wcd_mbhc_read_field(mbhc, WCD_MBHC_IN2P_CLAMP_STATE);
		if (clamp_state)
			return IRQ_HANDLED;
		/*
		 * check clamp for 120ms but at 30ms chunks to leave
		 * room for other interrupts to be processed
		 */
		usleep_range(30000, 30100);
	} while (--clamp_retry);

	/*
	 * If current plug is headphone then there is no chance to
	 * get ADC complete interrupt, so connected cable should be
	 * headset not headphone.
	 */
	if (mbhc->current_plug == MBHC_PLUG_TYPE_HEADPHONE) {
		disable_irq_nosync(mbhc->intr_ids->mbhc_hs_ins_intr);
		wcd_mbhc_write_field(mbhc, WCD_MBHC_DETECTION_DONE, 1);
		wcd_mbhc_find_plug_and_report(mbhc, MBHC_PLUG_TYPE_HEADSET);
		return IRQ_HANDLED;
	}

	return IRQ_HANDLED;
}

int wcd_mbhc_get_impedance(struct wcd_mbhc *mbhc, uint32_t *zl,	uint32_t *zr)
{
	*zl = mbhc->zl;
	*zr = mbhc->zr;

	if (*zl && *zr)
		return 0;
	else
		return -EINVAL;
}
EXPORT_SYMBOL(wcd_mbhc_get_impedance);

void wcd_mbhc_set_hph_type(struct wcd_mbhc *mbhc, int hph_type)
{
	mbhc->hph_type = hph_type;
}
EXPORT_SYMBOL(wcd_mbhc_set_hph_type);

int wcd_mbhc_get_hph_type(struct wcd_mbhc *mbhc)
{
	return mbhc->hph_type;
}
EXPORT_SYMBOL(wcd_mbhc_get_hph_type);

int wcd_mbhc_start(struct wcd_mbhc *mbhc, struct wcd_mbhc_config *cfg,
		   struct snd_soc_jack *jack)
{
	if (!mbhc || !cfg || !jack)
		return -EINVAL;

	mbhc->cfg = cfg;
	mbhc->jack = jack;

	return wcd_mbhc_initialise(mbhc);
}
EXPORT_SYMBOL(wcd_mbhc_start);

void wcd_mbhc_stop(struct wcd_mbhc *mbhc)
{
	mbhc->current_plug = MBHC_PLUG_TYPE_NONE;
	mbhc->hph_status = 0;
	disable_irq_nosync(mbhc->intr_ids->hph_left_ocp);
	disable_irq_nosync(mbhc->intr_ids->hph_right_ocp);
}
EXPORT_SYMBOL(wcd_mbhc_stop);

int wcd_dt_parse_mbhc_data(struct device *dev, struct wcd_mbhc_config *cfg)
{
	struct device_node *np = dev->of_node;
	int ret, i, microvolt;

	if (of_property_read_bool(np, "qcom,hphl-jack-type-normally-closed"))
		cfg->hphl_swh = false;
	else
		cfg->hphl_swh = true;

	if (of_property_read_bool(np, "qcom,ground-jack-type-normally-closed"))
		cfg->gnd_swh = false;
	else
		cfg->gnd_swh = true;

	ret = of_property_read_u32(np, "qcom,mbhc-headset-vthreshold-microvolt",
				   &microvolt);
	if (ret)
		dev_dbg(dev, "missing qcom,mbhc-hs-mic-max-vthreshold--microvolt in dt node\n");
	else
		cfg->hs_thr = microvolt/1000;

	ret = of_property_read_u32(np, "qcom,mbhc-headphone-vthreshold-microvolt",
				   &microvolt);
	if (ret)
		dev_dbg(dev, "missing qcom,mbhc-hs-mic-min-vthreshold-microvolt	entry\n");
	else
		cfg->hph_thr = microvolt/1000;

	ret = of_property_read_u32_array(np,
					 "qcom,mbhc-buttons-vthreshold-microvolt",
					 &cfg->btn_high[0],
					 WCD_MBHC_DEF_BUTTONS);
	if (ret)
		dev_err(dev, "missing qcom,mbhc-buttons-vthreshold-microvolt entry\n");

	for (i = 0; i < WCD_MBHC_DEF_BUTTONS; i++) {
		if (ret) /* default voltage */
			cfg->btn_high[i] = 500000;
		else
			/* Micro to Milli Volts */
			cfg->btn_high[i] = cfg->btn_high[i]/1000;
	}

	return 0;
}
EXPORT_SYMBOL(wcd_dt_parse_mbhc_data);

struct wcd_mbhc *wcd_mbhc_init(struct snd_soc_component *component,
			       const struct wcd_mbhc_cb *mbhc_cb,
			       const struct wcd_mbhc_intr *intr_ids,
			       struct wcd_mbhc_field *fields,
			       bool impedance_det_en)
{
	struct device *dev = component->dev;
	struct wcd_mbhc *mbhc;
	int ret;

	if (!intr_ids || !fields || !mbhc_cb || !mbhc_cb->mbhc_bias || !mbhc_cb->set_btn_thr) {
		dev_err(dev, "%s: Insufficient mbhc configuration\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	mbhc = devm_kzalloc(dev, sizeof(*mbhc), GFP_KERNEL);
	if (!mbhc)
		return ERR_PTR(-ENOMEM);

	mbhc->component = component;
	mbhc->dev = dev;
	mbhc->intr_ids = intr_ids;
	mbhc->mbhc_cb = mbhc_cb;
	mbhc->fields = fields;
	mbhc->mbhc_detection_logic = WCD_DETECTION_ADC;

	if (mbhc_cb->compute_impedance)
		mbhc->impedance_detect = impedance_det_en;

	INIT_DELAYED_WORK(&mbhc->mbhc_btn_dwork, wcd_btn_long_press_fn);

	mutex_init(&mbhc->lock);

	INIT_WORK(&mbhc->correct_plug_swch, wcd_correct_swch_plug);

	ret = devm_request_threaded_irq(dev, mbhc->intr_ids->mbhc_sw_intr, NULL,
					wcd_mbhc_mech_plug_detect_irq,
					IRQF_ONESHOT | IRQF_TRIGGER_RISING,
					"mbhc sw intr", mbhc);
	if (ret)
		goto err;

	ret = devm_request_threaded_irq(dev, mbhc->intr_ids->mbhc_btn_press_intr, NULL,
					wcd_mbhc_btn_press_handler,
					IRQF_ONESHOT | IRQF_TRIGGER_RISING,
					"Button Press detect", mbhc);
	if (ret)
		goto err;

	ret = devm_request_threaded_irq(dev, mbhc->intr_ids->mbhc_btn_release_intr, NULL,
					wcd_mbhc_btn_release_handler,
					IRQF_ONESHOT | IRQF_TRIGGER_RISING,
					"Button Release detect", mbhc);
	if (ret)
		goto err;

	ret = devm_request_threaded_irq(dev, mbhc->intr_ids->mbhc_hs_ins_intr, NULL,
					wcd_mbhc_adc_hs_ins_irq,
					IRQF_ONESHOT | IRQF_TRIGGER_RISING,
					"Elect Insert", mbhc);
	if (ret)
		goto err;

	disable_irq_nosync(mbhc->intr_ids->mbhc_hs_ins_intr);

	ret = devm_request_threaded_irq(dev, mbhc->intr_ids->mbhc_hs_rem_intr, NULL,
					wcd_mbhc_adc_hs_rem_irq,
					IRQF_ONESHOT | IRQF_TRIGGER_RISING,
					"Elect Remove", mbhc);
	if (ret)
		goto err;

	disable_irq_nosync(mbhc->intr_ids->mbhc_hs_rem_intr);

	ret = devm_request_threaded_irq(dev, mbhc->intr_ids->hph_left_ocp, NULL,
					wcd_mbhc_hphl_ocp_irq,
					IRQF_ONESHOT | IRQF_TRIGGER_RISING,
					"HPH_L OCP detect", mbhc);
	if (ret)
		goto err;

	ret = devm_request_threaded_irq(dev, mbhc->intr_ids->hph_right_ocp, NULL,
					wcd_mbhc_hphr_ocp_irq,
					IRQF_ONESHOT | IRQF_TRIGGER_RISING,
					"HPH_R OCP detect", mbhc);
	if (ret)
		goto err;

	return mbhc;
err:
	dev_err(dev, "Failed to request mbhc interrupts %d\n", ret);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL(wcd_mbhc_init);

void wcd_mbhc_deinit(struct wcd_mbhc *mbhc)
{
	mutex_lock(&mbhc->lock);
	wcd_cancel_hs_detect_plug(mbhc,	&mbhc->correct_plug_swch);
	mutex_unlock(&mbhc->lock);
}
EXPORT_SYMBOL(wcd_mbhc_deinit);

static int __init mbhc_init(void)
{
	return 0;
}

static void __exit mbhc_exit(void)
{
}

module_init(mbhc_init);
module_exit(mbhc_exit);

MODULE_DESCRIPTION("wcd MBHC v2 module");
MODULE_LICENSE("GPL");
