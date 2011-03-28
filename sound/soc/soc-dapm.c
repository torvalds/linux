/*
 * soc-dapm.c  --  ALSA SoC Dynamic Audio Power Management
 *
 * Copyright 2005 Wolfson Microelectronics PLC.
 * Author: Liam Girdwood <lrg@slimlogic.co.uk>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  Features:
 *    o Changes power status of internal codec blocks depending on the
 *      dynamic configuration of codec internal audio paths and active
 *      DACs/ADCs.
 *    o Platform power domain - can support external components i.e. amps and
 *      mic/meadphone insertion events.
 *    o Automatic Mic Bias support
 *    o Jack insertion power event initiation - e.g. hp insertion will enable
 *      sinks, dacs, etc
 *    o Delayed powerdown of audio susbsystem to reduce pops between a quick
 *      device reopen.
 *
 *  Todo:
 *    o DAPM power change sequencing - allow for configurable per
 *      codec sequences.
 *    o Support for analogue bias optimisation.
 *    o Support for reduced codec oversampling rates.
 *    o Support for reduced codec bias currents.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/bitops.h>
#include <linux/platform_device.h>
#include <linux/jiffies.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>

#include <trace/events/asoc.h>

/* dapm power sequences - make this per codec in the future */
static int dapm_up_seq[] = {
	[snd_soc_dapm_pre] = 0,
	[snd_soc_dapm_supply] = 1,
	[snd_soc_dapm_micbias] = 2,
	[snd_soc_dapm_aif_in] = 3,
	[snd_soc_dapm_aif_out] = 3,
	[snd_soc_dapm_mic] = 4,
	[snd_soc_dapm_mux] = 5,
	[snd_soc_dapm_virt_mux] = 5,
	[snd_soc_dapm_value_mux] = 5,
	[snd_soc_dapm_dac] = 6,
	[snd_soc_dapm_mixer] = 7,
	[snd_soc_dapm_mixer_named_ctl] = 7,
	[snd_soc_dapm_pga] = 8,
	[snd_soc_dapm_adc] = 9,
	[snd_soc_dapm_out_drv] = 10,
	[snd_soc_dapm_hp] = 10,
	[snd_soc_dapm_spk] = 10,
	[snd_soc_dapm_post] = 11,
};

static int dapm_down_seq[] = {
	[snd_soc_dapm_pre] = 0,
	[snd_soc_dapm_adc] = 1,
	[snd_soc_dapm_hp] = 2,
	[snd_soc_dapm_spk] = 2,
	[snd_soc_dapm_out_drv] = 2,
	[snd_soc_dapm_pga] = 4,
	[snd_soc_dapm_mixer_named_ctl] = 5,
	[snd_soc_dapm_mixer] = 5,
	[snd_soc_dapm_dac] = 6,
	[snd_soc_dapm_mic] = 7,
	[snd_soc_dapm_micbias] = 8,
	[snd_soc_dapm_mux] = 9,
	[snd_soc_dapm_virt_mux] = 9,
	[snd_soc_dapm_value_mux] = 9,
	[snd_soc_dapm_aif_in] = 10,
	[snd_soc_dapm_aif_out] = 10,
	[snd_soc_dapm_supply] = 11,
	[snd_soc_dapm_post] = 12,
};

static void pop_wait(u32 pop_time)
{
	if (pop_time)
		schedule_timeout_uninterruptible(msecs_to_jiffies(pop_time));
}

static void pop_dbg(struct device *dev, u32 pop_time, const char *fmt, ...)
{
	va_list args;
	char *buf;

	if (!pop_time)
		return;

	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (buf == NULL)
		return;

	va_start(args, fmt);
	vsnprintf(buf, PAGE_SIZE, fmt, args);
	dev_info(dev, "%s", buf);
	va_end(args);

	kfree(buf);
}

/* create a new dapm widget */
static inline struct snd_soc_dapm_widget *dapm_cnew_widget(
	const struct snd_soc_dapm_widget *_widget)
{
	return kmemdup(_widget, sizeof(*_widget), GFP_KERNEL);
}

/**
 * snd_soc_dapm_set_bias_level - set the bias level for the system
 * @card: audio device
 * @level: level to configure
 *
 * Configure the bias (power) levels for the SoC audio device.
 *
 * Returns 0 for success else error.
 */
static int snd_soc_dapm_set_bias_level(struct snd_soc_card *card,
				       struct snd_soc_dapm_context *dapm,
				       enum snd_soc_bias_level level)
{
	int ret = 0;

	switch (level) {
	case SND_SOC_BIAS_ON:
		dev_dbg(dapm->dev, "Setting full bias\n");
		break;
	case SND_SOC_BIAS_PREPARE:
		dev_dbg(dapm->dev, "Setting bias prepare\n");
		break;
	case SND_SOC_BIAS_STANDBY:
		dev_dbg(dapm->dev, "Setting standby bias\n");
		break;
	case SND_SOC_BIAS_OFF:
		dev_dbg(dapm->dev, "Setting bias off\n");
		break;
	default:
		dev_err(dapm->dev, "Setting invalid bias %d\n", level);
		return -EINVAL;
	}

	trace_snd_soc_bias_level_start(card, level);

	if (card && card->set_bias_level)
		ret = card->set_bias_level(card, level);
	if (ret == 0) {
		if (dapm->codec && dapm->codec->driver->set_bias_level)
			ret = dapm->codec->driver->set_bias_level(dapm->codec, level);
		else
			dapm->bias_level = level;
	}
	if (ret == 0) {
		if (card && card->set_bias_level_post)
			ret = card->set_bias_level_post(card, level);
	}

	trace_snd_soc_bias_level_done(card, level);

	return ret;
}

/* set up initial codec paths */
static void dapm_set_path_status(struct snd_soc_dapm_widget *w,
	struct snd_soc_dapm_path *p, int i)
{
	switch (w->id) {
	case snd_soc_dapm_switch:
	case snd_soc_dapm_mixer:
	case snd_soc_dapm_mixer_named_ctl: {
		int val;
		struct soc_mixer_control *mc = (struct soc_mixer_control *)
			w->kcontrols[i].private_value;
		unsigned int reg = mc->reg;
		unsigned int shift = mc->shift;
		int max = mc->max;
		unsigned int mask = (1 << fls(max)) - 1;
		unsigned int invert = mc->invert;

		val = snd_soc_read(w->codec, reg);
		val = (val >> shift) & mask;

		if ((invert && !val) || (!invert && val))
			p->connect = 1;
		else
			p->connect = 0;
	}
	break;
	case snd_soc_dapm_mux: {
		struct soc_enum *e = (struct soc_enum *)w->kcontrols[i].private_value;
		int val, item, bitmask;

		for (bitmask = 1; bitmask < e->max; bitmask <<= 1)
		;
		val = snd_soc_read(w->codec, e->reg);
		item = (val >> e->shift_l) & (bitmask - 1);

		p->connect = 0;
		for (i = 0; i < e->max; i++) {
			if (!(strcmp(p->name, e->texts[i])) && item == i)
				p->connect = 1;
		}
	}
	break;
	case snd_soc_dapm_virt_mux: {
		struct soc_enum *e = (struct soc_enum *)w->kcontrols[i].private_value;

		p->connect = 0;
		/* since a virtual mux has no backing registers to
		 * decide which path to connect, it will try to match
		 * with the first enumeration.  This is to ensure
		 * that the default mux choice (the first) will be
		 * correctly powered up during initialization.
		 */
		if (!strcmp(p->name, e->texts[0]))
			p->connect = 1;
	}
	break;
	case snd_soc_dapm_value_mux: {
		struct soc_enum *e = (struct soc_enum *)
			w->kcontrols[i].private_value;
		int val, item;

		val = snd_soc_read(w->codec, e->reg);
		val = (val >> e->shift_l) & e->mask;
		for (item = 0; item < e->max; item++) {
			if (val == e->values[item])
				break;
		}

		p->connect = 0;
		for (i = 0; i < e->max; i++) {
			if (!(strcmp(p->name, e->texts[i])) && item == i)
				p->connect = 1;
		}
	}
	break;
	/* does not effect routing - always connected */
	case snd_soc_dapm_pga:
	case snd_soc_dapm_out_drv:
	case snd_soc_dapm_output:
	case snd_soc_dapm_adc:
	case snd_soc_dapm_input:
	case snd_soc_dapm_dac:
	case snd_soc_dapm_micbias:
	case snd_soc_dapm_vmid:
	case snd_soc_dapm_supply:
	case snd_soc_dapm_aif_in:
	case snd_soc_dapm_aif_out:
		p->connect = 1;
	break;
	/* does effect routing - dynamically connected */
	case snd_soc_dapm_hp:
	case snd_soc_dapm_mic:
	case snd_soc_dapm_spk:
	case snd_soc_dapm_line:
	case snd_soc_dapm_pre:
	case snd_soc_dapm_post:
		p->connect = 0;
	break;
	}
}

/* connect mux widget to its interconnecting audio paths */
static int dapm_connect_mux(struct snd_soc_dapm_context *dapm,
	struct snd_soc_dapm_widget *src, struct snd_soc_dapm_widget *dest,
	struct snd_soc_dapm_path *path, const char *control_name,
	const struct snd_kcontrol_new *kcontrol)
{
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	int i;

	for (i = 0; i < e->max; i++) {
		if (!(strcmp(control_name, e->texts[i]))) {
			list_add(&path->list, &dapm->card->paths);
			list_add(&path->list_sink, &dest->sources);
			list_add(&path->list_source, &src->sinks);
			path->name = (char*)e->texts[i];
			dapm_set_path_status(dest, path, 0);
			return 0;
		}
	}

	return -ENODEV;
}

/* connect mixer widget to its interconnecting audio paths */
static int dapm_connect_mixer(struct snd_soc_dapm_context *dapm,
	struct snd_soc_dapm_widget *src, struct snd_soc_dapm_widget *dest,
	struct snd_soc_dapm_path *path, const char *control_name)
{
	int i;

	/* search for mixer kcontrol */
	for (i = 0; i < dest->num_kcontrols; i++) {
		if (!strcmp(control_name, dest->kcontrols[i].name)) {
			list_add(&path->list, &dapm->card->paths);
			list_add(&path->list_sink, &dest->sources);
			list_add(&path->list_source, &src->sinks);
			path->name = dest->kcontrols[i].name;
			dapm_set_path_status(dest, path, i);
			return 0;
		}
	}
	return -ENODEV;
}

/* update dapm codec register bits */
static int dapm_update_bits(struct snd_soc_dapm_widget *widget)
{
	int change, power;
	unsigned int old, new;
	struct snd_soc_codec *codec = widget->codec;
	struct snd_soc_dapm_context *dapm = widget->dapm;
	struct snd_soc_card *card = dapm->card;

	/* check for valid widgets */
	if (widget->reg < 0 || widget->id == snd_soc_dapm_input ||
		widget->id == snd_soc_dapm_output ||
		widget->id == snd_soc_dapm_hp ||
		widget->id == snd_soc_dapm_mic ||
		widget->id == snd_soc_dapm_line ||
		widget->id == snd_soc_dapm_spk)
		return 0;

	power = widget->power;
	if (widget->invert)
		power = (power ? 0:1);

	old = snd_soc_read(codec, widget->reg);
	new = (old & ~(0x1 << widget->shift)) | (power << widget->shift);

	change = old != new;
	if (change) {
		pop_dbg(dapm->dev, card->pop_time,
			"pop test %s : %s in %d ms\n",
			widget->name, widget->power ? "on" : "off",
			card->pop_time);
		pop_wait(card->pop_time);
		snd_soc_write(codec, widget->reg, new);
	}
	dev_dbg(dapm->dev, "reg %x old %x new %x change %d\n", widget->reg,
		old, new, change);
	return change;
}

/* create new dapm mixer control */
static int dapm_new_mixer(struct snd_soc_dapm_context *dapm,
	struct snd_soc_dapm_widget *w)
{
	int i, ret = 0;
	size_t name_len;
	struct snd_soc_dapm_path *path;
	struct snd_card *card = dapm->codec->card->snd_card;

	/* add kcontrol */
	for (i = 0; i < w->num_kcontrols; i++) {

		/* match name */
		list_for_each_entry(path, &w->sources, list_sink) {

			/* mixer/mux paths name must match control name */
			if (path->name != (char*)w->kcontrols[i].name)
				continue;

			/* add dapm control with long name.
			 * for dapm_mixer this is the concatenation of the
			 * mixer and kcontrol name.
			 * for dapm_mixer_named_ctl this is simply the
			 * kcontrol name.
			 */
			name_len = strlen(w->kcontrols[i].name) + 1;
			if (w->id != snd_soc_dapm_mixer_named_ctl)
				name_len += 1 + strlen(w->name);

			path->long_name = kmalloc(name_len, GFP_KERNEL);

			if (path->long_name == NULL)
				return -ENOMEM;

			switch (w->id) {
			default:
				snprintf(path->long_name, name_len, "%s %s",
					 w->name, w->kcontrols[i].name);
				break;
			case snd_soc_dapm_mixer_named_ctl:
				snprintf(path->long_name, name_len, "%s",
					 w->kcontrols[i].name);
				break;
			}

			path->long_name[name_len - 1] = '\0';

			path->kcontrol = snd_soc_cnew(&w->kcontrols[i], w,
				path->long_name);
			ret = snd_ctl_add(card, path->kcontrol);
			if (ret < 0) {
				dev_err(dapm->dev,
					"asoc: failed to add dapm kcontrol %s: %d\n",
					path->long_name, ret);
				kfree(path->long_name);
				path->long_name = NULL;
				return ret;
			}
		}
	}
	return ret;
}

/* create new dapm mux control */
static int dapm_new_mux(struct snd_soc_dapm_context *dapm,
	struct snd_soc_dapm_widget *w)
{
	struct snd_soc_dapm_path *path = NULL;
	struct snd_kcontrol *kcontrol;
	struct snd_card *card = dapm->codec->card->snd_card;
	int ret = 0;

	if (!w->num_kcontrols) {
		dev_err(dapm->dev, "asoc: mux %s has no controls\n", w->name);
		return -EINVAL;
	}

	kcontrol = snd_soc_cnew(&w->kcontrols[0], w, w->name);
	ret = snd_ctl_add(card, kcontrol);

	if (ret < 0)
		goto err;

	list_for_each_entry(path, &w->sources, list_sink)
		path->kcontrol = kcontrol;

	return ret;

err:
	dev_err(dapm->dev, "asoc: failed to add kcontrol %s\n", w->name);
	return ret;
}

/* create new dapm volume control */
static int dapm_new_pga(struct snd_soc_dapm_context *dapm,
	struct snd_soc_dapm_widget *w)
{
	if (w->num_kcontrols)
		dev_err(w->dapm->dev,
			"asoc: PGA controls not supported: '%s'\n", w->name);

	return 0;
}

/* reset 'walked' bit for each dapm path */
static inline void dapm_clear_walk(struct snd_soc_dapm_context *dapm)
{
	struct snd_soc_dapm_path *p;

	list_for_each_entry(p, &dapm->card->paths, list)
		p->walked = 0;
}

/* We implement power down on suspend by checking the power state of
 * the ALSA card - when we are suspending the ALSA state for the card
 * is set to D3.
 */
static int snd_soc_dapm_suspend_check(struct snd_soc_dapm_widget *widget)
{
	int level = snd_power_get_state(widget->dapm->codec->card->snd_card);

	switch (level) {
	case SNDRV_CTL_POWER_D3hot:
	case SNDRV_CTL_POWER_D3cold:
		if (widget->ignore_suspend)
			dev_dbg(widget->dapm->dev, "%s ignoring suspend\n",
				widget->name);
		return widget->ignore_suspend;
	default:
		return 1;
	}
}

/*
 * Recursively check for a completed path to an active or physically connected
 * output widget. Returns number of complete paths.
 */
static int is_connected_output_ep(struct snd_soc_dapm_widget *widget)
{
	struct snd_soc_dapm_path *path;
	int con = 0;

	if (widget->id == snd_soc_dapm_supply)
		return 0;

	switch (widget->id) {
	case snd_soc_dapm_adc:
	case snd_soc_dapm_aif_out:
		if (widget->active)
			return snd_soc_dapm_suspend_check(widget);
	default:
		break;
	}

	if (widget->connected) {
		/* connected pin ? */
		if (widget->id == snd_soc_dapm_output && !widget->ext)
			return snd_soc_dapm_suspend_check(widget);

		/* connected jack or spk ? */
		if (widget->id == snd_soc_dapm_hp || widget->id == snd_soc_dapm_spk ||
		    (widget->id == snd_soc_dapm_line && !list_empty(&widget->sources)))
			return snd_soc_dapm_suspend_check(widget);
	}

	list_for_each_entry(path, &widget->sinks, list_source) {
		if (path->walked)
			continue;

		if (path->sink && path->connect) {
			path->walked = 1;
			con += is_connected_output_ep(path->sink);
		}
	}

	return con;
}

/*
 * Recursively check for a completed path to an active or physically connected
 * input widget. Returns number of complete paths.
 */
static int is_connected_input_ep(struct snd_soc_dapm_widget *widget)
{
	struct snd_soc_dapm_path *path;
	int con = 0;

	if (widget->id == snd_soc_dapm_supply)
		return 0;

	/* active stream ? */
	switch (widget->id) {
	case snd_soc_dapm_dac:
	case snd_soc_dapm_aif_in:
		if (widget->active)
			return snd_soc_dapm_suspend_check(widget);
	default:
		break;
	}

	if (widget->connected) {
		/* connected pin ? */
		if (widget->id == snd_soc_dapm_input && !widget->ext)
			return snd_soc_dapm_suspend_check(widget);

		/* connected VMID/Bias for lower pops */
		if (widget->id == snd_soc_dapm_vmid)
			return snd_soc_dapm_suspend_check(widget);

		/* connected jack ? */
		if (widget->id == snd_soc_dapm_mic ||
		    (widget->id == snd_soc_dapm_line && !list_empty(&widget->sinks)))
			return snd_soc_dapm_suspend_check(widget);
	}

	list_for_each_entry(path, &widget->sources, list_sink) {
		if (path->walked)
			continue;

		if (path->source && path->connect) {
			path->walked = 1;
			con += is_connected_input_ep(path->source);
		}
	}

	return con;
}

/*
 * Handler for generic register modifier widget.
 */
int dapm_reg_event(struct snd_soc_dapm_widget *w,
		   struct snd_kcontrol *kcontrol, int event)
{
	unsigned int val;

	if (SND_SOC_DAPM_EVENT_ON(event))
		val = w->on_val;
	else
		val = w->off_val;

	snd_soc_update_bits(w->codec, -(w->reg + 1),
			    w->mask << w->shift, val << w->shift);

	return 0;
}
EXPORT_SYMBOL_GPL(dapm_reg_event);

/* Standard power change method, used to apply power changes to most
 * widgets.
 */
static int dapm_generic_apply_power(struct snd_soc_dapm_widget *w)
{
	int ret;

	/* call any power change event handlers */
	if (w->event)
		dev_dbg(w->dapm->dev, "power %s event for %s flags %x\n",
			 w->power ? "on" : "off",
			 w->name, w->event_flags);

	/* power up pre event */
	if (w->power && w->event &&
	    (w->event_flags & SND_SOC_DAPM_PRE_PMU)) {
		ret = w->event(w, NULL, SND_SOC_DAPM_PRE_PMU);
		if (ret < 0)
			return ret;
	}

	/* power down pre event */
	if (!w->power && w->event &&
	    (w->event_flags & SND_SOC_DAPM_PRE_PMD)) {
		ret = w->event(w, NULL, SND_SOC_DAPM_PRE_PMD);
		if (ret < 0)
			return ret;
	}

	dapm_update_bits(w);

	/* power up post event */
	if (w->power && w->event &&
	    (w->event_flags & SND_SOC_DAPM_POST_PMU)) {
		ret = w->event(w,
			       NULL, SND_SOC_DAPM_POST_PMU);
		if (ret < 0)
			return ret;
	}

	/* power down post event */
	if (!w->power && w->event &&
	    (w->event_flags & SND_SOC_DAPM_POST_PMD)) {
		ret = w->event(w, NULL, SND_SOC_DAPM_POST_PMD);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/* Generic check to see if a widget should be powered.
 */
static int dapm_generic_check_power(struct snd_soc_dapm_widget *w)
{
	int in, out;

	in = is_connected_input_ep(w);
	dapm_clear_walk(w->dapm);
	out = is_connected_output_ep(w);
	dapm_clear_walk(w->dapm);
	return out != 0 && in != 0;
}

/* Check to see if an ADC has power */
static int dapm_adc_check_power(struct snd_soc_dapm_widget *w)
{
	int in;

	if (w->active) {
		in = is_connected_input_ep(w);
		dapm_clear_walk(w->dapm);
		return in != 0;
	} else {
		return dapm_generic_check_power(w);
	}
}

/* Check to see if a DAC has power */
static int dapm_dac_check_power(struct snd_soc_dapm_widget *w)
{
	int out;

	if (w->active) {
		out = is_connected_output_ep(w);
		dapm_clear_walk(w->dapm);
		return out != 0;
	} else {
		return dapm_generic_check_power(w);
	}
}

/* Check to see if a power supply is needed */
static int dapm_supply_check_power(struct snd_soc_dapm_widget *w)
{
	struct snd_soc_dapm_path *path;
	int power = 0;

	/* Check if one of our outputs is connected */
	list_for_each_entry(path, &w->sinks, list_source) {
		if (path->connected &&
		    !path->connected(path->source, path->sink))
			continue;

		if (!path->sink)
			continue;

		if (path->sink->force) {
			power = 1;
			break;
		}

		if (path->sink->power_check &&
		    path->sink->power_check(path->sink)) {
			power = 1;
			break;
		}
	}

	dapm_clear_walk(w->dapm);

	return power;
}

static int dapm_seq_compare(struct snd_soc_dapm_widget *a,
			    struct snd_soc_dapm_widget *b,
			    int sort[])
{
	if (sort[a->id] != sort[b->id])
		return sort[a->id] - sort[b->id];
	if (a->reg != b->reg)
		return a->reg - b->reg;
	if (a->dapm != b->dapm)
		return (unsigned long)a->dapm - (unsigned long)b->dapm;

	return 0;
}

/* Insert a widget in order into a DAPM power sequence. */
static void dapm_seq_insert(struct snd_soc_dapm_widget *new_widget,
			    struct list_head *list,
			    int sort[])
{
	struct snd_soc_dapm_widget *w;

	list_for_each_entry(w, list, power_list)
		if (dapm_seq_compare(new_widget, w, sort) < 0) {
			list_add_tail(&new_widget->power_list, &w->power_list);
			return;
		}

	list_add_tail(&new_widget->power_list, list);
}

static void dapm_seq_check_event(struct snd_soc_dapm_context *dapm,
				 struct snd_soc_dapm_widget *w, int event)
{
	struct snd_soc_card *card = dapm->card;
	const char *ev_name;
	int power, ret;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		ev_name = "PRE_PMU";
		power = 1;
		break;
	case SND_SOC_DAPM_POST_PMU:
		ev_name = "POST_PMU";
		power = 1;
		break;
	case SND_SOC_DAPM_PRE_PMD:
		ev_name = "PRE_PMD";
		power = 0;
		break;
	case SND_SOC_DAPM_POST_PMD:
		ev_name = "POST_PMD";
		power = 0;
		break;
	default:
		BUG();
		return;
	}

	if (w->power != power)
		return;

	if (w->event && (w->event_flags & event)) {
		pop_dbg(dapm->dev, card->pop_time, "pop test : %s %s\n",
			w->name, ev_name);
		trace_snd_soc_dapm_widget_event_start(w, event);
		ret = w->event(w, NULL, event);
		trace_snd_soc_dapm_widget_event_done(w, event);
		if (ret < 0)
			pr_err("%s: %s event failed: %d\n",
			       ev_name, w->name, ret);
	}
}

/* Apply the coalesced changes from a DAPM sequence */
static void dapm_seq_run_coalesced(struct snd_soc_dapm_context *dapm,
				   struct list_head *pending)
{
	struct snd_soc_card *card = dapm->card;
	struct snd_soc_dapm_widget *w;
	int reg, power;
	unsigned int value = 0;
	unsigned int mask = 0;
	unsigned int cur_mask;

	reg = list_first_entry(pending, struct snd_soc_dapm_widget,
			       power_list)->reg;

	list_for_each_entry(w, pending, power_list) {
		cur_mask = 1 << w->shift;
		BUG_ON(reg != w->reg);

		if (w->invert)
			power = !w->power;
		else
			power = w->power;

		mask |= cur_mask;
		if (power)
			value |= cur_mask;

		pop_dbg(dapm->dev, card->pop_time,
			"pop test : Queue %s: reg=0x%x, 0x%x/0x%x\n",
			w->name, reg, value, mask);

		/* Check for events */
		dapm_seq_check_event(dapm, w, SND_SOC_DAPM_PRE_PMU);
		dapm_seq_check_event(dapm, w, SND_SOC_DAPM_PRE_PMD);
	}

	if (reg >= 0) {
		pop_dbg(dapm->dev, card->pop_time,
			"pop test : Applying 0x%x/0x%x to %x in %dms\n",
			value, mask, reg, card->pop_time);
		pop_wait(card->pop_time);
		snd_soc_update_bits(dapm->codec, reg, mask, value);
	}

	list_for_each_entry(w, pending, power_list) {
		dapm_seq_check_event(dapm, w, SND_SOC_DAPM_POST_PMU);
		dapm_seq_check_event(dapm, w, SND_SOC_DAPM_POST_PMD);
	}
}

/* Apply a DAPM power sequence.
 *
 * We walk over a pre-sorted list of widgets to apply power to.  In
 * order to minimise the number of writes to the device required
 * multiple widgets will be updated in a single write where possible.
 * Currently anything that requires more than a single write is not
 * handled.
 */
static void dapm_seq_run(struct snd_soc_dapm_context *dapm,
			 struct list_head *list, int event, int sort[])
{
	struct snd_soc_dapm_widget *w, *n;
	LIST_HEAD(pending);
	int cur_sort = -1;
	int cur_reg = SND_SOC_NOPM;
	struct snd_soc_dapm_context *cur_dapm = NULL;
	int ret;

	list_for_each_entry_safe(w, n, list, power_list) {
		ret = 0;

		/* Do we need to apply any queued changes? */
		if (sort[w->id] != cur_sort || w->reg != cur_reg ||
		    w->dapm != cur_dapm) {
			if (!list_empty(&pending))
				dapm_seq_run_coalesced(cur_dapm, &pending);

			INIT_LIST_HEAD(&pending);
			cur_sort = -1;
			cur_reg = SND_SOC_NOPM;
			cur_dapm = NULL;
		}

		switch (w->id) {
		case snd_soc_dapm_pre:
			if (!w->event)
				list_for_each_entry_safe_continue(w, n, list,
								  power_list);

			if (event == SND_SOC_DAPM_STREAM_START)
				ret = w->event(w,
					       NULL, SND_SOC_DAPM_PRE_PMU);
			else if (event == SND_SOC_DAPM_STREAM_STOP)
				ret = w->event(w,
					       NULL, SND_SOC_DAPM_PRE_PMD);
			break;

		case snd_soc_dapm_post:
			if (!w->event)
				list_for_each_entry_safe_continue(w, n, list,
								  power_list);

			if (event == SND_SOC_DAPM_STREAM_START)
				ret = w->event(w,
					       NULL, SND_SOC_DAPM_POST_PMU);
			else if (event == SND_SOC_DAPM_STREAM_STOP)
				ret = w->event(w,
					       NULL, SND_SOC_DAPM_POST_PMD);
			break;

		case snd_soc_dapm_input:
		case snd_soc_dapm_output:
		case snd_soc_dapm_hp:
		case snd_soc_dapm_mic:
		case snd_soc_dapm_line:
		case snd_soc_dapm_spk:
			/* No register support currently */
			ret = dapm_generic_apply_power(w);
			break;

		default:
			/* Queue it up for application */
			cur_sort = sort[w->id];
			cur_reg = w->reg;
			cur_dapm = w->dapm;
			list_move(&w->power_list, &pending);
			break;
		}

		if (ret < 0)
			dev_err(w->dapm->dev,
				"Failed to apply widget power: %d\n", ret);
	}

	if (!list_empty(&pending))
		dapm_seq_run_coalesced(cur_dapm, &pending);
}

static void dapm_widget_update(struct snd_soc_dapm_context *dapm)
{
	struct snd_soc_dapm_update *update = dapm->update;
	struct snd_soc_dapm_widget *w;
	int ret;

	if (!update)
		return;

	w = update->widget;

	if (w->event &&
	    (w->event_flags & SND_SOC_DAPM_PRE_REG)) {
		ret = w->event(w, update->kcontrol, SND_SOC_DAPM_PRE_REG);
		if (ret != 0)
			pr_err("%s DAPM pre-event failed: %d\n",
			       w->name, ret);
	}

	ret = snd_soc_update_bits(w->codec, update->reg, update->mask,
				  update->val);
	if (ret < 0)
		pr_err("%s DAPM update failed: %d\n", w->name, ret);

	if (w->event &&
	    (w->event_flags & SND_SOC_DAPM_POST_REG)) {
		ret = w->event(w, update->kcontrol, SND_SOC_DAPM_POST_REG);
		if (ret != 0)
			pr_err("%s DAPM post-event failed: %d\n",
			       w->name, ret);
	}
}



/*
 * Scan each dapm widget for complete audio path.
 * A complete path is a route that has valid endpoints i.e.:-
 *
 *  o DAC to output pin.
 *  o Input Pin to ADC.
 *  o Input pin to Output pin (bypass, sidetone)
 *  o DAC to ADC (loopback).
 */
static int dapm_power_widgets(struct snd_soc_dapm_context *dapm, int event)
{
	struct snd_soc_card *card = dapm->codec->card;
	struct snd_soc_dapm_widget *w;
	struct snd_soc_dapm_context *d;
	LIST_HEAD(up_list);
	LIST_HEAD(down_list);
	int ret = 0;
	int power;

	trace_snd_soc_dapm_start(card);

	list_for_each_entry(d, &card->dapm_list, list)
		if (d->n_widgets)
			d->dev_power = 0;

	/* Check which widgets we need to power and store them in
	 * lists indicating if they should be powered up or down.
	 */
	list_for_each_entry(w, &card->widgets, list) {
		switch (w->id) {
		case snd_soc_dapm_pre:
			dapm_seq_insert(w, &down_list, dapm_down_seq);
			break;
		case snd_soc_dapm_post:
			dapm_seq_insert(w, &up_list, dapm_up_seq);
			break;

		default:
			if (!w->power_check)
				continue;

			if (!w->force)
				power = w->power_check(w);
			else
				power = 1;
			if (power)
				w->dapm->dev_power = 1;

			if (w->power == power)
				continue;

			trace_snd_soc_dapm_widget_power(w, power);

			if (power)
				dapm_seq_insert(w, &up_list, dapm_up_seq);
			else
				dapm_seq_insert(w, &down_list, dapm_down_seq);

			w->power = power;
			break;
		}
	}

	/* If there are no DAPM widgets then try to figure out power from the
	 * event type.
	 */
	if (!dapm->n_widgets) {
		switch (event) {
		case SND_SOC_DAPM_STREAM_START:
		case SND_SOC_DAPM_STREAM_RESUME:
			dapm->dev_power = 1;
			break;
		case SND_SOC_DAPM_STREAM_STOP:
			dapm->dev_power = !!dapm->codec->active;
			break;
		case SND_SOC_DAPM_STREAM_SUSPEND:
			dapm->dev_power = 0;
			break;
		case SND_SOC_DAPM_STREAM_NOP:
			switch (dapm->bias_level) {
				case SND_SOC_BIAS_STANDBY:
				case SND_SOC_BIAS_OFF:
					dapm->dev_power = 0;
					break;
				default:
					dapm->dev_power = 1;
					break;
			}
			break;
		default:
			break;
		}
	}

	list_for_each_entry(d, &dapm->card->dapm_list, list) {
		if (d->dev_power && d->bias_level == SND_SOC_BIAS_OFF) {
			ret = snd_soc_dapm_set_bias_level(card, d,
							  SND_SOC_BIAS_STANDBY);
			if (ret != 0)
				dev_err(d->dev,
					"Failed to turn on bias: %d\n", ret);
		}

		/* If we're changing to all on or all off then prepare */
		if ((d->dev_power && d->bias_level == SND_SOC_BIAS_STANDBY) ||
		    (!d->dev_power && d->bias_level == SND_SOC_BIAS_ON)) {
			ret = snd_soc_dapm_set_bias_level(card, d,
							  SND_SOC_BIAS_PREPARE);
			if (ret != 0)
				dev_err(d->dev,
					"Failed to prepare bias: %d\n", ret);
		}
	}

	/* Power down widgets first; try to avoid amplifying pops. */
	dapm_seq_run(dapm, &down_list, event, dapm_down_seq);

	dapm_widget_update(dapm);

	/* Now power up. */
	dapm_seq_run(dapm, &up_list, event, dapm_up_seq);

	list_for_each_entry(d, &dapm->card->dapm_list, list) {
		/* If we just powered the last thing off drop to standby bias */
		if (d->bias_level == SND_SOC_BIAS_PREPARE && !d->dev_power) {
			ret = snd_soc_dapm_set_bias_level(card, d,
							  SND_SOC_BIAS_STANDBY);
			if (ret != 0)
				dev_err(d->dev,
					"Failed to apply standby bias: %d\n",
					ret);
		}

		/* If we're in standby and can support bias off then do that */
		if (d->bias_level == SND_SOC_BIAS_STANDBY &&
		    d->idle_bias_off) {
			ret = snd_soc_dapm_set_bias_level(card, d,
							  SND_SOC_BIAS_OFF);
			if (ret != 0)
				dev_err(d->dev,
					"Failed to turn off bias: %d\n", ret);
		}

		/* If we just powered up then move to active bias */
		if (d->bias_level == SND_SOC_BIAS_PREPARE && d->dev_power) {
			ret = snd_soc_dapm_set_bias_level(card, d,
							  SND_SOC_BIAS_ON);
			if (ret != 0)
				dev_err(d->dev,
					"Failed to apply active bias: %d\n",
					ret);
		}
	}

	pop_dbg(dapm->dev, card->pop_time,
		"DAPM sequencing finished, waiting %dms\n", card->pop_time);
	pop_wait(card->pop_time);

	trace_snd_soc_dapm_done(card);

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static int dapm_widget_power_open_file(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t dapm_widget_power_read_file(struct file *file,
					   char __user *user_buf,
					   size_t count, loff_t *ppos)
{
	struct snd_soc_dapm_widget *w = file->private_data;
	char *buf;
	int in, out;
	ssize_t ret;
	struct snd_soc_dapm_path *p = NULL;

	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	in = is_connected_input_ep(w);
	dapm_clear_walk(w->dapm);
	out = is_connected_output_ep(w);
	dapm_clear_walk(w->dapm);

	ret = snprintf(buf, PAGE_SIZE, "%s: %s  in %d out %d",
		       w->name, w->power ? "On" : "Off", in, out);

	if (w->reg >= 0)
		ret += snprintf(buf + ret, PAGE_SIZE - ret,
				" - R%d(0x%x) bit %d",
				w->reg, w->reg, w->shift);

	ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");

	if (w->sname)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, " stream %s %s\n",
				w->sname,
				w->active ? "active" : "inactive");

	list_for_each_entry(p, &w->sources, list_sink) {
		if (p->connected && !p->connected(w, p->sink))
			continue;

		if (p->connect)
			ret += snprintf(buf + ret, PAGE_SIZE - ret,
					" in  %s %s\n",
					p->name ? p->name : "static",
					p->source->name);
	}
	list_for_each_entry(p, &w->sinks, list_source) {
		if (p->connected && !p->connected(w, p->sink))
			continue;

		if (p->connect)
			ret += snprintf(buf + ret, PAGE_SIZE - ret,
					" out %s %s\n",
					p->name ? p->name : "static",
					p->sink->name);
	}

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

	kfree(buf);
	return ret;
}

static const struct file_operations dapm_widget_power_fops = {
	.open = dapm_widget_power_open_file,
	.read = dapm_widget_power_read_file,
	.llseek = default_llseek,
};

void snd_soc_dapm_debugfs_init(struct snd_soc_dapm_context *dapm)
{
	struct snd_soc_dapm_widget *w;
	struct dentry *d;

	if (!dapm->debugfs_dapm)
		return;

	list_for_each_entry(w, &dapm->card->widgets, list) {
		if (!w->name || w->dapm != dapm)
			continue;

		d = debugfs_create_file(w->name, 0444,
					dapm->debugfs_dapm, w,
					&dapm_widget_power_fops);
		if (!d)
			dev_warn(w->dapm->dev,
				"ASoC: Failed to create %s debugfs file\n",
				w->name);
	}
}
#else
void snd_soc_dapm_debugfs_init(struct snd_soc_dapm_context *dapm)
{
}
#endif

/* test and update the power status of a mux widget */
static int dapm_mux_update_power(struct snd_soc_dapm_widget *widget,
				 struct snd_kcontrol *kcontrol, int change,
				 int mux, struct soc_enum *e)
{
	struct snd_soc_dapm_path *path;
	int found = 0;

	if (widget->id != snd_soc_dapm_mux &&
	    widget->id != snd_soc_dapm_virt_mux &&
	    widget->id != snd_soc_dapm_value_mux)
		return -ENODEV;

	if (!change)
		return 0;

	/* find dapm widget path assoc with kcontrol */
	list_for_each_entry(path, &widget->dapm->card->paths, list) {
		if (path->kcontrol != kcontrol)
			continue;

		if (!path->name || !e->texts[mux])
			continue;

		found = 1;
		/* we now need to match the string in the enum to the path */
		if (!(strcmp(path->name, e->texts[mux])))
			path->connect = 1; /* new connection */
		else
			path->connect = 0; /* old connection must be powered down */
	}

	if (found)
		dapm_power_widgets(widget->dapm, SND_SOC_DAPM_STREAM_NOP);

	return 0;
}

/* test and update the power status of a mixer or switch widget */
static int dapm_mixer_update_power(struct snd_soc_dapm_widget *widget,
				   struct snd_kcontrol *kcontrol, int connect)
{
	struct snd_soc_dapm_path *path;
	int found = 0;

	if (widget->id != snd_soc_dapm_mixer &&
	    widget->id != snd_soc_dapm_mixer_named_ctl &&
	    widget->id != snd_soc_dapm_switch)
		return -ENODEV;

	/* find dapm widget path assoc with kcontrol */
	list_for_each_entry(path, &widget->dapm->card->paths, list) {
		if (path->kcontrol != kcontrol)
			continue;

		/* found, now check type */
		found = 1;
		path->connect = connect;
		break;
	}

	if (found)
		dapm_power_widgets(widget->dapm, SND_SOC_DAPM_STREAM_NOP);

	return 0;
}

/* show dapm widget status in sys fs */
static ssize_t dapm_widget_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct snd_soc_pcm_runtime *rtd =
			container_of(dev, struct snd_soc_pcm_runtime, dev);
	struct snd_soc_codec *codec =rtd->codec;
	struct snd_soc_dapm_widget *w;
	int count = 0;
	char *state = "not set";

	list_for_each_entry(w, &codec->card->widgets, list) {
		if (w->dapm != &codec->dapm)
			continue;

		/* only display widgets that burnm power */
		switch (w->id) {
		case snd_soc_dapm_hp:
		case snd_soc_dapm_mic:
		case snd_soc_dapm_spk:
		case snd_soc_dapm_line:
		case snd_soc_dapm_micbias:
		case snd_soc_dapm_dac:
		case snd_soc_dapm_adc:
		case snd_soc_dapm_pga:
		case snd_soc_dapm_out_drv:
		case snd_soc_dapm_mixer:
		case snd_soc_dapm_mixer_named_ctl:
		case snd_soc_dapm_supply:
			if (w->name)
				count += sprintf(buf + count, "%s: %s\n",
					w->name, w->power ? "On":"Off");
		break;
		default:
		break;
		}
	}

	switch (codec->dapm.bias_level) {
	case SND_SOC_BIAS_ON:
		state = "On";
		break;
	case SND_SOC_BIAS_PREPARE:
		state = "Prepare";
		break;
	case SND_SOC_BIAS_STANDBY:
		state = "Standby";
		break;
	case SND_SOC_BIAS_OFF:
		state = "Off";
		break;
	}
	count += sprintf(buf + count, "PM State: %s\n", state);

	return count;
}

static DEVICE_ATTR(dapm_widget, 0444, dapm_widget_show, NULL);

int snd_soc_dapm_sys_add(struct device *dev)
{
	return device_create_file(dev, &dev_attr_dapm_widget);
}

static void snd_soc_dapm_sys_remove(struct device *dev)
{
	device_remove_file(dev, &dev_attr_dapm_widget);
}

/* free all dapm widgets and resources */
static void dapm_free_widgets(struct snd_soc_dapm_context *dapm)
{
	struct snd_soc_dapm_widget *w, *next_w;
	struct snd_soc_dapm_path *p, *next_p;

	list_for_each_entry_safe(w, next_w, &dapm->card->widgets, list) {
		if (w->dapm != dapm)
			continue;
		list_del(&w->list);
		/*
		 * remove source and sink paths associated to this widget.
		 * While removing the path, remove reference to it from both
		 * source and sink widgets so that path is removed only once.
		 */
		list_for_each_entry_safe(p, next_p, &w->sources, list_sink) {
			list_del(&p->list_sink);
			list_del(&p->list_source);
			list_del(&p->list);
			kfree(p->long_name);
			kfree(p);
		}
		list_for_each_entry_safe(p, next_p, &w->sinks, list_source) {
			list_del(&p->list_sink);
			list_del(&p->list_source);
			list_del(&p->list);
			kfree(p->long_name);
			kfree(p);
		}
		kfree(w->name);
		kfree(w);
	}
}

static int snd_soc_dapm_set_pin(struct snd_soc_dapm_context *dapm,
				const char *pin, int status)
{
	struct snd_soc_dapm_widget *w;

	list_for_each_entry(w, &dapm->card->widgets, list) {
		if (w->dapm != dapm)
			continue;
		if (!strcmp(w->name, pin)) {
			dev_dbg(w->dapm->dev, "dapm: pin %s = %d\n",
				pin, status);
			w->connected = status;
			/* Allow disabling of forced pins */
			if (status == 0)
				w->force = 0;
			return 0;
		}
	}

	dev_err(dapm->dev, "dapm: unknown pin %s\n", pin);
	return -EINVAL;
}

/**
 * snd_soc_dapm_sync - scan and power dapm paths
 * @dapm: DAPM context
 *
 * Walks all dapm audio paths and powers widgets according to their
 * stream or path usage.
 *
 * Returns 0 for success.
 */
int snd_soc_dapm_sync(struct snd_soc_dapm_context *dapm)
{
	return dapm_power_widgets(dapm, SND_SOC_DAPM_STREAM_NOP);
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_sync);

static int snd_soc_dapm_add_route(struct snd_soc_dapm_context *dapm,
				  const struct snd_soc_dapm_route *route)
{
	struct snd_soc_dapm_path *path;
	struct snd_soc_dapm_widget *wsource = NULL, *wsink = NULL, *w;
	struct snd_soc_dapm_widget *wtsource = NULL, *wtsink = NULL;
	const char *sink;
	const char *control = route->control;
	const char *source;
	char prefixed_sink[80];
	char prefixed_source[80];
	int ret = 0;

	if (dapm->codec->name_prefix) {
		snprintf(prefixed_sink, sizeof(prefixed_sink), "%s %s",
			 dapm->codec->name_prefix, route->sink);
		sink = prefixed_sink;
		snprintf(prefixed_source, sizeof(prefixed_source), "%s %s",
			 dapm->codec->name_prefix, route->source);
		source = prefixed_source;
	} else {
		sink = route->sink;
		source = route->source;
	}

	/*
	 * find src and dest widgets over all widgets but favor a widget from
	 * current DAPM context
	 */
	list_for_each_entry(w, &dapm->card->widgets, list) {
		if (!wsink && !(strcmp(w->name, sink))) {
			wtsink = w;
			if (w->dapm == dapm)
				wsink = w;
			continue;
		}
		if (!wsource && !(strcmp(w->name, source))) {
			wtsource = w;
			if (w->dapm == dapm)
				wsource = w;
		}
	}
	/* use widget from another DAPM context if not found from this */
	if (!wsink)
		wsink = wtsink;
	if (!wsource)
		wsource = wtsource;

	if (wsource == NULL || wsink == NULL)
		return -ENODEV;

	path = kzalloc(sizeof(struct snd_soc_dapm_path), GFP_KERNEL);
	if (!path)
		return -ENOMEM;

	path->source = wsource;
	path->sink = wsink;
	path->connected = route->connected;
	INIT_LIST_HEAD(&path->list);
	INIT_LIST_HEAD(&path->list_source);
	INIT_LIST_HEAD(&path->list_sink);

	/* check for external widgets */
	if (wsink->id == snd_soc_dapm_input) {
		if (wsource->id == snd_soc_dapm_micbias ||
			wsource->id == snd_soc_dapm_mic ||
			wsource->id == snd_soc_dapm_line ||
			wsource->id == snd_soc_dapm_output)
			wsink->ext = 1;
	}
	if (wsource->id == snd_soc_dapm_output) {
		if (wsink->id == snd_soc_dapm_spk ||
			wsink->id == snd_soc_dapm_hp ||
			wsink->id == snd_soc_dapm_line ||
			wsink->id == snd_soc_dapm_input)
			wsource->ext = 1;
	}

	/* connect static paths */
	if (control == NULL) {
		list_add(&path->list, &dapm->card->paths);
		list_add(&path->list_sink, &wsink->sources);
		list_add(&path->list_source, &wsource->sinks);
		path->connect = 1;
		return 0;
	}

	/* connect dynamic paths */
	switch(wsink->id) {
	case snd_soc_dapm_adc:
	case snd_soc_dapm_dac:
	case snd_soc_dapm_pga:
	case snd_soc_dapm_out_drv:
	case snd_soc_dapm_input:
	case snd_soc_dapm_output:
	case snd_soc_dapm_micbias:
	case snd_soc_dapm_vmid:
	case snd_soc_dapm_pre:
	case snd_soc_dapm_post:
	case snd_soc_dapm_supply:
	case snd_soc_dapm_aif_in:
	case snd_soc_dapm_aif_out:
		list_add(&path->list, &dapm->card->paths);
		list_add(&path->list_sink, &wsink->sources);
		list_add(&path->list_source, &wsource->sinks);
		path->connect = 1;
		return 0;
	case snd_soc_dapm_mux:
	case snd_soc_dapm_virt_mux:
	case snd_soc_dapm_value_mux:
		ret = dapm_connect_mux(dapm, wsource, wsink, path, control,
			&wsink->kcontrols[0]);
		if (ret != 0)
			goto err;
		break;
	case snd_soc_dapm_switch:
	case snd_soc_dapm_mixer:
	case snd_soc_dapm_mixer_named_ctl:
		ret = dapm_connect_mixer(dapm, wsource, wsink, path, control);
		if (ret != 0)
			goto err;
		break;
	case snd_soc_dapm_hp:
	case snd_soc_dapm_mic:
	case snd_soc_dapm_line:
	case snd_soc_dapm_spk:
		list_add(&path->list, &dapm->card->paths);
		list_add(&path->list_sink, &wsink->sources);
		list_add(&path->list_source, &wsource->sinks);
		path->connect = 0;
		return 0;
	}
	return 0;

err:
	dev_warn(dapm->dev, "asoc: no dapm match for %s --> %s --> %s\n",
		 source, control, sink);
	kfree(path);
	return ret;
}

/**
 * snd_soc_dapm_add_routes - Add routes between DAPM widgets
 * @dapm: DAPM context
 * @route: audio routes
 * @num: number of routes
 *
 * Connects 2 dapm widgets together via a named audio path. The sink is
 * the widget receiving the audio signal, whilst the source is the sender
 * of the audio signal.
 *
 * Returns 0 for success else error. On error all resources can be freed
 * with a call to snd_soc_card_free().
 */
int snd_soc_dapm_add_routes(struct snd_soc_dapm_context *dapm,
			    const struct snd_soc_dapm_route *route, int num)
{
	int i, ret;

	for (i = 0; i < num; i++) {
		ret = snd_soc_dapm_add_route(dapm, route);
		if (ret < 0) {
			dev_err(dapm->dev, "Failed to add route %s->%s\n",
				route->source, route->sink);
			return ret;
		}
		route++;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_add_routes);

/**
 * snd_soc_dapm_new_widgets - add new dapm widgets
 * @dapm: DAPM context
 *
 * Checks the codec for any new dapm widgets and creates them if found.
 *
 * Returns 0 for success.
 */
int snd_soc_dapm_new_widgets(struct snd_soc_dapm_context *dapm)
{
	struct snd_soc_dapm_widget *w;
	unsigned int val;

	list_for_each_entry(w, &dapm->card->widgets, list)
	{
		if (w->new)
			continue;

		switch(w->id) {
		case snd_soc_dapm_switch:
		case snd_soc_dapm_mixer:
		case snd_soc_dapm_mixer_named_ctl:
			w->power_check = dapm_generic_check_power;
			dapm_new_mixer(dapm, w);
			break;
		case snd_soc_dapm_mux:
		case snd_soc_dapm_virt_mux:
		case snd_soc_dapm_value_mux:
			w->power_check = dapm_generic_check_power;
			dapm_new_mux(dapm, w);
			break;
		case snd_soc_dapm_adc:
		case snd_soc_dapm_aif_out:
			w->power_check = dapm_adc_check_power;
			break;
		case snd_soc_dapm_dac:
		case snd_soc_dapm_aif_in:
			w->power_check = dapm_dac_check_power;
			break;
		case snd_soc_dapm_pga:
		case snd_soc_dapm_out_drv:
			w->power_check = dapm_generic_check_power;
			dapm_new_pga(dapm, w);
			break;
		case snd_soc_dapm_input:
		case snd_soc_dapm_output:
		case snd_soc_dapm_micbias:
		case snd_soc_dapm_spk:
		case snd_soc_dapm_hp:
		case snd_soc_dapm_mic:
		case snd_soc_dapm_line:
			w->power_check = dapm_generic_check_power;
			break;
		case snd_soc_dapm_supply:
			w->power_check = dapm_supply_check_power;
		case snd_soc_dapm_vmid:
		case snd_soc_dapm_pre:
		case snd_soc_dapm_post:
			break;
		}

		/* Read the initial power state from the device */
		if (w->reg >= 0) {
			val = snd_soc_read(w->codec, w->reg);
			val &= 1 << w->shift;
			if (w->invert)
				val = !val;

			if (val)
				w->power = 1;
		}

		w->new = 1;
	}

	dapm_power_widgets(dapm, SND_SOC_DAPM_STREAM_NOP);
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_new_widgets);

/**
 * snd_soc_dapm_get_volsw - dapm mixer get callback
 * @kcontrol: mixer control
 * @ucontrol: control element information
 *
 * Callback to get the value of a dapm mixer control.
 *
 * Returns 0 for success.
 */
int snd_soc_dapm_get_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget = snd_kcontrol_chip(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int reg = mc->reg;
	unsigned int shift = mc->shift;
	unsigned int rshift = mc->rshift;
	int max = mc->max;
	unsigned int invert = mc->invert;
	unsigned int mask = (1 << fls(max)) - 1;

	ucontrol->value.integer.value[0] =
		(snd_soc_read(widget->codec, reg) >> shift) & mask;
	if (shift != rshift)
		ucontrol->value.integer.value[1] =
			(snd_soc_read(widget->codec, reg) >> rshift) & mask;
	if (invert) {
		ucontrol->value.integer.value[0] =
			max - ucontrol->value.integer.value[0];
		if (shift != rshift)
			ucontrol->value.integer.value[1] =
				max - ucontrol->value.integer.value[1];
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_get_volsw);

/**
 * snd_soc_dapm_put_volsw - dapm mixer set callback
 * @kcontrol: mixer control
 * @ucontrol: control element information
 *
 * Callback to set the value of a dapm mixer control.
 *
 * Returns 0 for success.
 */
int snd_soc_dapm_put_volsw(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget = snd_kcontrol_chip(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int reg = mc->reg;
	unsigned int shift = mc->shift;
	int max = mc->max;
	unsigned int mask = (1 << fls(max)) - 1;
	unsigned int invert = mc->invert;
	unsigned int val;
	int connect, change;
	struct snd_soc_dapm_update update;

	val = (ucontrol->value.integer.value[0] & mask);

	if (invert)
		val = max - val;
	mask = mask << shift;
	val = val << shift;

	mutex_lock(&widget->codec->mutex);
	widget->value = val;

	change = snd_soc_test_bits(widget->codec, reg, mask, val);
	if (change) {
		if (val)
			/* new connection */
			connect = invert ? 0:1;
		else
			/* old connection must be powered down */
			connect = invert ? 1:0;

		update.kcontrol = kcontrol;
		update.widget = widget;
		update.reg = reg;
		update.mask = mask;
		update.val = val;
		widget->dapm->update = &update;

		dapm_mixer_update_power(widget, kcontrol, connect);

		widget->dapm->update = NULL;
	}

	mutex_unlock(&widget->codec->mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_put_volsw);

/**
 * snd_soc_dapm_get_enum_double - dapm enumerated double mixer get callback
 * @kcontrol: mixer control
 * @ucontrol: control element information
 *
 * Callback to get the value of a dapm enumerated double mixer control.
 *
 * Returns 0 for success.
 */
int snd_soc_dapm_get_enum_double(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget = snd_kcontrol_chip(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int val, bitmask;

	for (bitmask = 1; bitmask < e->max; bitmask <<= 1)
		;
	val = snd_soc_read(widget->codec, e->reg);
	ucontrol->value.enumerated.item[0] = (val >> e->shift_l) & (bitmask - 1);
	if (e->shift_l != e->shift_r)
		ucontrol->value.enumerated.item[1] =
			(val >> e->shift_r) & (bitmask - 1);

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_get_enum_double);

/**
 * snd_soc_dapm_put_enum_double - dapm enumerated double mixer set callback
 * @kcontrol: mixer control
 * @ucontrol: control element information
 *
 * Callback to set the value of a dapm enumerated double mixer control.
 *
 * Returns 0 for success.
 */
int snd_soc_dapm_put_enum_double(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget = snd_kcontrol_chip(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int val, mux, change;
	unsigned int mask, bitmask;
	struct snd_soc_dapm_update update;

	for (bitmask = 1; bitmask < e->max; bitmask <<= 1)
		;
	if (ucontrol->value.enumerated.item[0] > e->max - 1)
		return -EINVAL;
	mux = ucontrol->value.enumerated.item[0];
	val = mux << e->shift_l;
	mask = (bitmask - 1) << e->shift_l;
	if (e->shift_l != e->shift_r) {
		if (ucontrol->value.enumerated.item[1] > e->max - 1)
			return -EINVAL;
		val |= ucontrol->value.enumerated.item[1] << e->shift_r;
		mask |= (bitmask - 1) << e->shift_r;
	}

	mutex_lock(&widget->codec->mutex);
	widget->value = val;
	change = snd_soc_test_bits(widget->codec, e->reg, mask, val);

	update.kcontrol = kcontrol;
	update.widget = widget;
	update.reg = e->reg;
	update.mask = mask;
	update.val = val;
	widget->dapm->update = &update;

	dapm_mux_update_power(widget, kcontrol, change, mux, e);

	widget->dapm->update = NULL;

	mutex_unlock(&widget->codec->mutex);
	return change;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_put_enum_double);

/**
 * snd_soc_dapm_get_enum_virt - Get virtual DAPM mux
 * @kcontrol: mixer control
 * @ucontrol: control element information
 *
 * Returns 0 for success.
 */
int snd_soc_dapm_get_enum_virt(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = widget->value;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_get_enum_virt);

/**
 * snd_soc_dapm_put_enum_virt - Set virtual DAPM mux
 * @kcontrol: mixer control
 * @ucontrol: control element information
 *
 * Returns 0 for success.
 */
int snd_soc_dapm_put_enum_virt(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget = snd_kcontrol_chip(kcontrol);
	struct soc_enum *e =
		(struct soc_enum *)kcontrol->private_value;
	int change;
	int ret = 0;

	if (ucontrol->value.enumerated.item[0] >= e->max)
		return -EINVAL;

	mutex_lock(&widget->codec->mutex);

	change = widget->value != ucontrol->value.enumerated.item[0];
	widget->value = ucontrol->value.enumerated.item[0];
	dapm_mux_update_power(widget, kcontrol, change, widget->value, e);

	mutex_unlock(&widget->codec->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_put_enum_virt);

/**
 * snd_soc_dapm_get_value_enum_double - dapm semi enumerated double mixer get
 *					callback
 * @kcontrol: mixer control
 * @ucontrol: control element information
 *
 * Callback to get the value of a dapm semi enumerated double mixer control.
 *
 * Semi enumerated mixer: the enumerated items are referred as values. Can be
 * used for handling bitfield coded enumeration for example.
 *
 * Returns 0 for success.
 */
int snd_soc_dapm_get_value_enum_double(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget = snd_kcontrol_chip(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int reg_val, val, mux;

	reg_val = snd_soc_read(widget->codec, e->reg);
	val = (reg_val >> e->shift_l) & e->mask;
	for (mux = 0; mux < e->max; mux++) {
		if (val == e->values[mux])
			break;
	}
	ucontrol->value.enumerated.item[0] = mux;
	if (e->shift_l != e->shift_r) {
		val = (reg_val >> e->shift_r) & e->mask;
		for (mux = 0; mux < e->max; mux++) {
			if (val == e->values[mux])
				break;
		}
		ucontrol->value.enumerated.item[1] = mux;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_get_value_enum_double);

/**
 * snd_soc_dapm_put_value_enum_double - dapm semi enumerated double mixer set
 *					callback
 * @kcontrol: mixer control
 * @ucontrol: control element information
 *
 * Callback to set the value of a dapm semi enumerated double mixer control.
 *
 * Semi enumerated mixer: the enumerated items are referred as values. Can be
 * used for handling bitfield coded enumeration for example.
 *
 * Returns 0 for success.
 */
int snd_soc_dapm_put_value_enum_double(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget = snd_kcontrol_chip(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int val, mux, change;
	unsigned int mask;
	struct snd_soc_dapm_update update;

	if (ucontrol->value.enumerated.item[0] > e->max - 1)
		return -EINVAL;
	mux = ucontrol->value.enumerated.item[0];
	val = e->values[ucontrol->value.enumerated.item[0]] << e->shift_l;
	mask = e->mask << e->shift_l;
	if (e->shift_l != e->shift_r) {
		if (ucontrol->value.enumerated.item[1] > e->max - 1)
			return -EINVAL;
		val |= e->values[ucontrol->value.enumerated.item[1]] << e->shift_r;
		mask |= e->mask << e->shift_r;
	}

	mutex_lock(&widget->codec->mutex);
	widget->value = val;
	change = snd_soc_test_bits(widget->codec, e->reg, mask, val);

	update.kcontrol = kcontrol;
	update.widget = widget;
	update.reg = e->reg;
	update.mask = mask;
	update.val = val;
	widget->dapm->update = &update;

	dapm_mux_update_power(widget, kcontrol, change, mux, e);

	widget->dapm->update = NULL;

	mutex_unlock(&widget->codec->mutex);
	return change;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_put_value_enum_double);

/**
 * snd_soc_dapm_info_pin_switch - Info for a pin switch
 *
 * @kcontrol: mixer control
 * @uinfo: control element information
 *
 * Callback to provide information about a pin switch control.
 */
int snd_soc_dapm_info_pin_switch(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_info_pin_switch);

/**
 * snd_soc_dapm_get_pin_switch - Get information for a pin switch
 *
 * @kcontrol: mixer control
 * @ucontrol: Value
 */
int snd_soc_dapm_get_pin_switch(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	const char *pin = (const char *)kcontrol->private_value;

	mutex_lock(&codec->mutex);

	ucontrol->value.integer.value[0] =
		snd_soc_dapm_get_pin_status(&codec->dapm, pin);

	mutex_unlock(&codec->mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_get_pin_switch);

/**
 * snd_soc_dapm_put_pin_switch - Set information for a pin switch
 *
 * @kcontrol: mixer control
 * @ucontrol: Value
 */
int snd_soc_dapm_put_pin_switch(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	const char *pin = (const char *)kcontrol->private_value;

	mutex_lock(&codec->mutex);

	if (ucontrol->value.integer.value[0])
		snd_soc_dapm_enable_pin(&codec->dapm, pin);
	else
		snd_soc_dapm_disable_pin(&codec->dapm, pin);

	snd_soc_dapm_sync(&codec->dapm);

	mutex_unlock(&codec->mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_put_pin_switch);

/**
 * snd_soc_dapm_new_control - create new dapm control
 * @dapm: DAPM context
 * @widget: widget template
 *
 * Creates a new dapm control based upon the template.
 *
 * Returns 0 for success else error.
 */
int snd_soc_dapm_new_control(struct snd_soc_dapm_context *dapm,
	const struct snd_soc_dapm_widget *widget)
{
	struct snd_soc_dapm_widget *w;
	size_t name_len;

	if ((w = dapm_cnew_widget(widget)) == NULL)
		return -ENOMEM;

	name_len = strlen(widget->name) + 1;
	if (dapm->codec->name_prefix)
		name_len += 1 + strlen(dapm->codec->name_prefix);
	w->name = kmalloc(name_len, GFP_KERNEL);
	if (w->name == NULL) {
		kfree(w);
		return -ENOMEM;
	}
	if (dapm->codec->name_prefix)
		snprintf(w->name, name_len, "%s %s",
			dapm->codec->name_prefix, widget->name);
	else
		snprintf(w->name, name_len, "%s", widget->name);

	dapm->n_widgets++;
	w->dapm = dapm;
	w->codec = dapm->codec;
	INIT_LIST_HEAD(&w->sources);
	INIT_LIST_HEAD(&w->sinks);
	INIT_LIST_HEAD(&w->list);
	list_add(&w->list, &dapm->card->widgets);

	/* machine layer set ups unconnected pins and insertions */
	w->connected = 1;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_new_control);

/**
 * snd_soc_dapm_new_controls - create new dapm controls
 * @dapm: DAPM context
 * @widget: widget array
 * @num: number of widgets
 *
 * Creates new DAPM controls based upon the templates.
 *
 * Returns 0 for success else error.
 */
int snd_soc_dapm_new_controls(struct snd_soc_dapm_context *dapm,
	const struct snd_soc_dapm_widget *widget,
	int num)
{
	int i, ret;

	for (i = 0; i < num; i++) {
		ret = snd_soc_dapm_new_control(dapm, widget);
		if (ret < 0) {
			dev_err(dapm->dev,
				"ASoC: Failed to create DAPM control %s: %d\n",
				widget->name, ret);
			return ret;
		}
		widget++;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_new_controls);

static void soc_dapm_stream_event(struct snd_soc_dapm_context *dapm,
	const char *stream, int event)
{
	struct snd_soc_dapm_widget *w;

	list_for_each_entry(w, &dapm->card->widgets, list)
	{
		if (!w->sname || w->dapm != dapm)
			continue;
		dev_dbg(w->dapm->dev, "widget %s\n %s stream %s event %d\n",
			w->name, w->sname, stream, event);
		if (strstr(w->sname, stream)) {
			switch(event) {
			case SND_SOC_DAPM_STREAM_START:
				w->active = 1;
				break;
			case SND_SOC_DAPM_STREAM_STOP:
				w->active = 0;
				break;
			case SND_SOC_DAPM_STREAM_SUSPEND:
			case SND_SOC_DAPM_STREAM_RESUME:
			case SND_SOC_DAPM_STREAM_PAUSE_PUSH:
			case SND_SOC_DAPM_STREAM_PAUSE_RELEASE:
				break;
			}
		}
	}

	dapm_power_widgets(dapm, event);
}

/**
 * snd_soc_dapm_stream_event - send a stream event to the dapm core
 * @rtd: PCM runtime data
 * @stream: stream name
 * @event: stream event
 *
 * Sends a stream event to the dapm core. The core then makes any
 * necessary widget power changes.
 *
 * Returns 0 for success else error.
 */
int snd_soc_dapm_stream_event(struct snd_soc_pcm_runtime *rtd,
	const char *stream, int event)
{
	struct snd_soc_codec *codec = rtd->codec;

	if (stream == NULL)
		return 0;

	mutex_lock(&codec->mutex);
	soc_dapm_stream_event(&codec->dapm, stream, event);
	mutex_unlock(&codec->mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_stream_event);

/**
 * snd_soc_dapm_enable_pin - enable pin.
 * @dapm: DAPM context
 * @pin: pin name
 *
 * Enables input/output pin and its parents or children widgets iff there is
 * a valid audio route and active audio stream.
 * NOTE: snd_soc_dapm_sync() needs to be called after this for DAPM to
 * do any widget power switching.
 */
int snd_soc_dapm_enable_pin(struct snd_soc_dapm_context *dapm, const char *pin)
{
	return snd_soc_dapm_set_pin(dapm, pin, 1);
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_enable_pin);

/**
 * snd_soc_dapm_force_enable_pin - force a pin to be enabled
 * @dapm: DAPM context
 * @pin: pin name
 *
 * Enables input/output pin regardless of any other state.  This is
 * intended for use with microphone bias supplies used in microphone
 * jack detection.
 *
 * NOTE: snd_soc_dapm_sync() needs to be called after this for DAPM to
 * do any widget power switching.
 */
int snd_soc_dapm_force_enable_pin(struct snd_soc_dapm_context *dapm,
				  const char *pin)
{
	struct snd_soc_dapm_widget *w;

	list_for_each_entry(w, &dapm->card->widgets, list) {
		if (w->dapm != dapm)
			continue;
		if (!strcmp(w->name, pin)) {
			dev_dbg(w->dapm->dev,
				"dapm: force enable pin %s\n", pin);
			w->connected = 1;
			w->force = 1;
			return 0;
		}
	}

	dev_err(dapm->dev, "dapm: unknown pin %s\n", pin);
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_force_enable_pin);

/**
 * snd_soc_dapm_disable_pin - disable pin.
 * @dapm: DAPM context
 * @pin: pin name
 *
 * Disables input/output pin and its parents or children widgets.
 * NOTE: snd_soc_dapm_sync() needs to be called after this for DAPM to
 * do any widget power switching.
 */
int snd_soc_dapm_disable_pin(struct snd_soc_dapm_context *dapm,
			     const char *pin)
{
	return snd_soc_dapm_set_pin(dapm, pin, 0);
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_disable_pin);

/**
 * snd_soc_dapm_nc_pin - permanently disable pin.
 * @dapm: DAPM context
 * @pin: pin name
 *
 * Marks the specified pin as being not connected, disabling it along
 * any parent or child widgets.  At present this is identical to
 * snd_soc_dapm_disable_pin() but in future it will be extended to do
 * additional things such as disabling controls which only affect
 * paths through the pin.
 *
 * NOTE: snd_soc_dapm_sync() needs to be called after this for DAPM to
 * do any widget power switching.
 */
int snd_soc_dapm_nc_pin(struct snd_soc_dapm_context *dapm, const char *pin)
{
	return snd_soc_dapm_set_pin(dapm, pin, 0);
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_nc_pin);

/**
 * snd_soc_dapm_get_pin_status - get audio pin status
 * @dapm: DAPM context
 * @pin: audio signal pin endpoint (or start point)
 *
 * Get audio pin status - connected or disconnected.
 *
 * Returns 1 for connected otherwise 0.
 */
int snd_soc_dapm_get_pin_status(struct snd_soc_dapm_context *dapm,
				const char *pin)
{
	struct snd_soc_dapm_widget *w;

	list_for_each_entry(w, &dapm->card->widgets, list) {
		if (w->dapm != dapm)
			continue;
		if (!strcmp(w->name, pin))
			return w->connected;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_get_pin_status);

/**
 * snd_soc_dapm_ignore_suspend - ignore suspend status for DAPM endpoint
 * @dapm: DAPM context
 * @pin: audio signal pin endpoint (or start point)
 *
 * Mark the given endpoint or pin as ignoring suspend.  When the
 * system is disabled a path between two endpoints flagged as ignoring
 * suspend will not be disabled.  The path must already be enabled via
 * normal means at suspend time, it will not be turned on if it was not
 * already enabled.
 */
int snd_soc_dapm_ignore_suspend(struct snd_soc_dapm_context *dapm,
				const char *pin)
{
	struct snd_soc_dapm_widget *w;

	list_for_each_entry(w, &dapm->card->widgets, list) {
		if (w->dapm != dapm)
			continue;
		if (!strcmp(w->name, pin)) {
			w->ignore_suspend = 1;
			return 0;
		}
	}

	dev_err(dapm->dev, "dapm: unknown pin %s\n", pin);
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_ignore_suspend);

/**
 * snd_soc_dapm_free - free dapm resources
 * @card: SoC device
 *
 * Free all dapm widgets and resources.
 */
void snd_soc_dapm_free(struct snd_soc_dapm_context *dapm)
{
	snd_soc_dapm_sys_remove(dapm->dev);
	dapm_free_widgets(dapm);
	list_del(&dapm->list);
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_free);

static void soc_dapm_shutdown_codec(struct snd_soc_dapm_context *dapm)
{
	struct snd_soc_dapm_widget *w;
	LIST_HEAD(down_list);
	int powerdown = 0;

	list_for_each_entry(w, &dapm->card->widgets, list) {
		if (w->dapm != dapm)
			continue;
		if (w->power) {
			dapm_seq_insert(w, &down_list, dapm_down_seq);
			w->power = 0;
			powerdown = 1;
		}
	}

	/* If there were no widgets to power down we're already in
	 * standby.
	 */
	if (powerdown) {
		snd_soc_dapm_set_bias_level(NULL, dapm, SND_SOC_BIAS_PREPARE);
		dapm_seq_run(dapm, &down_list, 0, dapm_down_seq);
		snd_soc_dapm_set_bias_level(NULL, dapm, SND_SOC_BIAS_STANDBY);
	}
}

/*
 * snd_soc_dapm_shutdown - callback for system shutdown
 */
void snd_soc_dapm_shutdown(struct snd_soc_card *card)
{
	struct snd_soc_codec *codec;

	list_for_each_entry(codec, &card->codec_dev_list, list) {
		soc_dapm_shutdown_codec(&codec->dapm);
		snd_soc_dapm_set_bias_level(card, &codec->dapm, SND_SOC_BIAS_OFF);
	}
}

/* Module information */
MODULE_AUTHOR("Liam Girdwood, lrg@slimlogic.co.uk");
MODULE_DESCRIPTION("Dynamic Audio Power Management core for ALSA SoC");
MODULE_LICENSE("GPL");
