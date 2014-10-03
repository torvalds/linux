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
 *      mic/headphone insertion events.
 *    o Automatic Mic Bias support
 *    o Jack insertion power event initiation - e.g. hp insertion will enable
 *      sinks, dacs, etc
 *    o Delayed power down of audio subsystem to reduce pops between a quick
 *      device reopen.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/async.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/bitops.h>
#include <linux/platform_device.h>
#include <linux/jiffies.h>
#include <linux/debugfs.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>

#include <trace/events/asoc.h>

#define DAPM_UPDATE_STAT(widget, val) widget->dapm->card->dapm_stats.val++;

static int snd_soc_dapm_add_path(struct snd_soc_dapm_context *dapm,
	struct snd_soc_dapm_widget *wsource, struct snd_soc_dapm_widget *wsink,
	const char *control,
	int (*connected)(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink));
static struct snd_soc_dapm_widget *
snd_soc_dapm_new_control(struct snd_soc_dapm_context *dapm,
			 const struct snd_soc_dapm_widget *widget);

/* dapm power sequences - make this per codec in the future */
static int dapm_up_seq[] = {
	[snd_soc_dapm_pre] = 0,
	[snd_soc_dapm_regulator_supply] = 1,
	[snd_soc_dapm_clock_supply] = 1,
	[snd_soc_dapm_supply] = 2,
	[snd_soc_dapm_micbias] = 3,
	[snd_soc_dapm_dai_link] = 2,
	[snd_soc_dapm_dai_in] = 4,
	[snd_soc_dapm_dai_out] = 4,
	[snd_soc_dapm_aif_in] = 4,
	[snd_soc_dapm_aif_out] = 4,
	[snd_soc_dapm_mic] = 5,
	[snd_soc_dapm_mux] = 6,
	[snd_soc_dapm_dac] = 7,
	[snd_soc_dapm_switch] = 8,
	[snd_soc_dapm_mixer] = 8,
	[snd_soc_dapm_mixer_named_ctl] = 8,
	[snd_soc_dapm_pga] = 9,
	[snd_soc_dapm_adc] = 10,
	[snd_soc_dapm_out_drv] = 11,
	[snd_soc_dapm_hp] = 11,
	[snd_soc_dapm_spk] = 11,
	[snd_soc_dapm_line] = 11,
	[snd_soc_dapm_kcontrol] = 12,
	[snd_soc_dapm_post] = 13,
};

static int dapm_down_seq[] = {
	[snd_soc_dapm_pre] = 0,
	[snd_soc_dapm_kcontrol] = 1,
	[snd_soc_dapm_adc] = 2,
	[snd_soc_dapm_hp] = 3,
	[snd_soc_dapm_spk] = 3,
	[snd_soc_dapm_line] = 3,
	[snd_soc_dapm_out_drv] = 3,
	[snd_soc_dapm_pga] = 4,
	[snd_soc_dapm_switch] = 5,
	[snd_soc_dapm_mixer_named_ctl] = 5,
	[snd_soc_dapm_mixer] = 5,
	[snd_soc_dapm_dac] = 6,
	[snd_soc_dapm_mic] = 7,
	[snd_soc_dapm_micbias] = 8,
	[snd_soc_dapm_mux] = 9,
	[snd_soc_dapm_aif_in] = 10,
	[snd_soc_dapm_aif_out] = 10,
	[snd_soc_dapm_dai_in] = 10,
	[snd_soc_dapm_dai_out] = 10,
	[snd_soc_dapm_dai_link] = 11,
	[snd_soc_dapm_supply] = 12,
	[snd_soc_dapm_clock_supply] = 13,
	[snd_soc_dapm_regulator_supply] = 13,
	[snd_soc_dapm_post] = 14,
};

static void dapm_assert_locked(struct snd_soc_dapm_context *dapm)
{
	if (dapm->card && dapm->card->instantiated)
		lockdep_assert_held(&dapm->card->dapm_mutex);
}

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

static bool dapm_dirty_widget(struct snd_soc_dapm_widget *w)
{
	return !list_empty(&w->dirty);
}

static void dapm_mark_dirty(struct snd_soc_dapm_widget *w, const char *reason)
{
	dapm_assert_locked(w->dapm);

	if (!dapm_dirty_widget(w)) {
		dev_vdbg(w->dapm->dev, "Marking %s dirty due to %s\n",
			 w->name, reason);
		list_add_tail(&w->dirty, &w->dapm->card->dapm_dirty);
	}
}

void dapm_mark_io_dirty(struct snd_soc_dapm_context *dapm)
{
	struct snd_soc_card *card = dapm->card;
	struct snd_soc_dapm_widget *w;

	mutex_lock(&card->dapm_mutex);

	list_for_each_entry(w, &card->widgets, list) {
		switch (w->id) {
		case snd_soc_dapm_input:
		case snd_soc_dapm_output:
			dapm_mark_dirty(w, "Rechecking inputs and outputs");
			break;
		default:
			break;
		}
	}

	mutex_unlock(&card->dapm_mutex);
}
EXPORT_SYMBOL_GPL(dapm_mark_io_dirty);

/* create a new dapm widget */
static inline struct snd_soc_dapm_widget *dapm_cnew_widget(
	const struct snd_soc_dapm_widget *_widget)
{
	return kmemdup(_widget, sizeof(*_widget), GFP_KERNEL);
}

struct dapm_kcontrol_data {
	unsigned int value;
	struct snd_soc_dapm_widget *widget;
	struct list_head paths;
	struct snd_soc_dapm_widget_list *wlist;
};

static int dapm_kcontrol_data_alloc(struct snd_soc_dapm_widget *widget,
	struct snd_kcontrol *kcontrol)
{
	struct dapm_kcontrol_data *data;
	struct soc_mixer_control *mc;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data) {
		dev_err(widget->dapm->dev,
				"ASoC: can't allocate kcontrol data for %s\n",
				widget->name);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&data->paths);

	switch (widget->id) {
	case snd_soc_dapm_switch:
	case snd_soc_dapm_mixer:
	case snd_soc_dapm_mixer_named_ctl:
		mc = (struct soc_mixer_control *)kcontrol->private_value;

		if (mc->autodisable) {
			struct snd_soc_dapm_widget template;

			memset(&template, 0, sizeof(template));
			template.reg = mc->reg;
			template.mask = (1 << fls(mc->max)) - 1;
			template.shift = mc->shift;
			if (mc->invert)
				template.off_val = mc->max;
			else
				template.off_val = 0;
			template.on_val = template.off_val;
			template.id = snd_soc_dapm_kcontrol;
			template.name = kcontrol->id.name;

			data->value = template.on_val;

			data->widget = snd_soc_dapm_new_control(widget->dapm,
				&template);
			if (!data->widget) {
				kfree(data);
				return -ENOMEM;
			}
		}
		break;
	default:
		break;
	}

	kcontrol->private_data = data;

	return 0;
}

static void dapm_kcontrol_free(struct snd_kcontrol *kctl)
{
	struct dapm_kcontrol_data *data = snd_kcontrol_chip(kctl);
	kfree(data->wlist);
	kfree(data);
}

static struct snd_soc_dapm_widget_list *dapm_kcontrol_get_wlist(
	const struct snd_kcontrol *kcontrol)
{
	struct dapm_kcontrol_data *data = snd_kcontrol_chip(kcontrol);

	return data->wlist;
}

static int dapm_kcontrol_add_widget(struct snd_kcontrol *kcontrol,
	struct snd_soc_dapm_widget *widget)
{
	struct dapm_kcontrol_data *data = snd_kcontrol_chip(kcontrol);
	struct snd_soc_dapm_widget_list *new_wlist;
	unsigned int n;

	if (data->wlist)
		n = data->wlist->num_widgets + 1;
	else
		n = 1;

	new_wlist = krealloc(data->wlist,
			sizeof(*new_wlist) + sizeof(widget) * n, GFP_KERNEL);
	if (!new_wlist)
		return -ENOMEM;

	new_wlist->widgets[n - 1] = widget;
	new_wlist->num_widgets = n;

	data->wlist = new_wlist;

	return 0;
}

static void dapm_kcontrol_add_path(const struct snd_kcontrol *kcontrol,
	struct snd_soc_dapm_path *path)
{
	struct dapm_kcontrol_data *data = snd_kcontrol_chip(kcontrol);

	list_add_tail(&path->list_kcontrol, &data->paths);

	if (data->widget) {
		snd_soc_dapm_add_path(data->widget->dapm, data->widget,
		    path->source, NULL, NULL);
	}
}

static bool dapm_kcontrol_is_powered(const struct snd_kcontrol *kcontrol)
{
	struct dapm_kcontrol_data *data = snd_kcontrol_chip(kcontrol);

	if (!data->widget)
		return true;

	return data->widget->power;
}

static struct list_head *dapm_kcontrol_get_path_list(
	const struct snd_kcontrol *kcontrol)
{
	struct dapm_kcontrol_data *data = snd_kcontrol_chip(kcontrol);

	return &data->paths;
}

#define dapm_kcontrol_for_each_path(path, kcontrol) \
	list_for_each_entry(path, dapm_kcontrol_get_path_list(kcontrol), \
		list_kcontrol)

static unsigned int dapm_kcontrol_get_value(const struct snd_kcontrol *kcontrol)
{
	struct dapm_kcontrol_data *data = snd_kcontrol_chip(kcontrol);

	return data->value;
}

static bool dapm_kcontrol_set_value(const struct snd_kcontrol *kcontrol,
	unsigned int value)
{
	struct dapm_kcontrol_data *data = snd_kcontrol_chip(kcontrol);

	if (data->value == value)
		return false;

	if (data->widget)
		data->widget->on_val = value;

	data->value = value;

	return true;
}

/**
 * snd_soc_dapm_kcontrol_dapm() - Returns the dapm context associated to a
 *  kcontrol
 * @kcontrol: The kcontrol
 *
 * Note: This function must only be used on kcontrols that are known to have
 * been registered for a CODEC. Otherwise the behaviour is undefined.
 */
struct snd_soc_dapm_context *snd_soc_dapm_kcontrol_dapm(
	struct snd_kcontrol *kcontrol)
{
	return dapm_kcontrol_get_wlist(kcontrol)->widgets[0]->dapm;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_kcontrol_dapm);

/**
 * snd_soc_dapm_kcontrol_codec() - Returns the codec associated to a kcontrol
 * @kcontrol: The kcontrol
 */
struct snd_soc_codec *snd_soc_dapm_kcontrol_codec(struct snd_kcontrol *kcontrol)
{
	return snd_soc_dapm_to_codec(snd_soc_dapm_kcontrol_dapm(kcontrol));
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_kcontrol_codec);

static void dapm_reset(struct snd_soc_card *card)
{
	struct snd_soc_dapm_widget *w;

	lockdep_assert_held(&card->dapm_mutex);

	memset(&card->dapm_stats, 0, sizeof(card->dapm_stats));

	list_for_each_entry(w, &card->widgets, list) {
		w->new_power = w->power;
		w->power_checked = false;
		w->inputs = -1;
		w->outputs = -1;
	}
}

static const char *soc_dapm_prefix(struct snd_soc_dapm_context *dapm)
{
	if (!dapm->component)
		return NULL;
	return dapm->component->name_prefix;
}

static int soc_dapm_read(struct snd_soc_dapm_context *dapm, int reg,
	unsigned int *value)
{
	if (!dapm->component)
		return -EIO;
	return snd_soc_component_read(dapm->component, reg, value);
}

static int soc_dapm_update_bits(struct snd_soc_dapm_context *dapm,
	int reg, unsigned int mask, unsigned int value)
{
	if (!dapm->component)
		return -EIO;
	return snd_soc_component_update_bits_async(dapm->component, reg,
		mask, value);
}

static int soc_dapm_test_bits(struct snd_soc_dapm_context *dapm,
	int reg, unsigned int mask, unsigned int value)
{
	if (!dapm->component)
		return -EIO;
	return snd_soc_component_test_bits(dapm->component, reg, mask, value);
}

static void soc_dapm_async_complete(struct snd_soc_dapm_context *dapm)
{
	if (dapm->component)
		snd_soc_component_async_complete(dapm->component);
}

/**
 * snd_soc_dapm_set_bias_level - set the bias level for the system
 * @dapm: DAPM context
 * @level: level to configure
 *
 * Configure the bias (power) levels for the SoC audio device.
 *
 * Returns 0 for success else error.
 */
static int snd_soc_dapm_set_bias_level(struct snd_soc_dapm_context *dapm,
				       enum snd_soc_bias_level level)
{
	struct snd_soc_card *card = dapm->card;
	int ret = 0;

	trace_snd_soc_bias_level_start(card, level);

	if (card && card->set_bias_level)
		ret = card->set_bias_level(card, dapm, level);
	if (ret != 0)
		goto out;

	if (dapm->set_bias_level)
		ret = dapm->set_bias_level(dapm, level);
	else if (!card || dapm != &card->dapm)
		dapm->bias_level = level;

	if (ret != 0)
		goto out;

	if (card && card->set_bias_level_post)
		ret = card->set_bias_level_post(card, dapm, level);
out:
	trace_snd_soc_bias_level_done(card, level);

	return ret;
}

/* connect mux widget to its interconnecting audio paths */
static int dapm_connect_mux(struct snd_soc_dapm_context *dapm,
	struct snd_soc_dapm_widget *src, struct snd_soc_dapm_widget *dest,
	struct snd_soc_dapm_path *path, const char *control_name,
	const struct snd_kcontrol_new *kcontrol)
{
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int val, item;
	int i;

	if (e->reg != SND_SOC_NOPM) {
		soc_dapm_read(dapm, e->reg, &val);
		val = (val >> e->shift_l) & e->mask;
		item = snd_soc_enum_val_to_item(e, val);
	} else {
		/* since a virtual mux has no backing registers to
		 * decide which path to connect, it will try to match
		 * with the first enumeration.  This is to ensure
		 * that the default mux choice (the first) will be
		 * correctly powered up during initialization.
		 */
		item = 0;
	}

	for (i = 0; i < e->items; i++) {
		if (!(strcmp(control_name, e->texts[i]))) {
			list_add(&path->list, &dapm->card->paths);
			list_add(&path->list_sink, &dest->sources);
			list_add(&path->list_source, &src->sinks);
			path->name = (char*)e->texts[i];
			if (i == item)
				path->connect = 1;
			else
				path->connect = 0;
			return 0;
		}
	}

	return -ENODEV;
}

/* set up initial codec paths */
static void dapm_set_mixer_path_status(struct snd_soc_dapm_widget *w,
	struct snd_soc_dapm_path *p, int i)
{
	struct soc_mixer_control *mc = (struct soc_mixer_control *)
		w->kcontrol_news[i].private_value;
	unsigned int reg = mc->reg;
	unsigned int shift = mc->shift;
	unsigned int max = mc->max;
	unsigned int mask = (1 << fls(max)) - 1;
	unsigned int invert = mc->invert;
	unsigned int val;

	if (reg != SND_SOC_NOPM) {
		soc_dapm_read(w->dapm, reg, &val);
		val = (val >> shift) & mask;
		if (invert)
			val = max - val;
		p->connect = !!val;
	} else {
		p->connect = 0;
	}
}

/* connect mixer widget to its interconnecting audio paths */
static int dapm_connect_mixer(struct snd_soc_dapm_context *dapm,
	struct snd_soc_dapm_widget *src, struct snd_soc_dapm_widget *dest,
	struct snd_soc_dapm_path *path, const char *control_name)
{
	int i;

	/* search for mixer kcontrol */
	for (i = 0; i < dest->num_kcontrols; i++) {
		if (!strcmp(control_name, dest->kcontrol_news[i].name)) {
			list_add(&path->list, &dapm->card->paths);
			list_add(&path->list_sink, &dest->sources);
			list_add(&path->list_source, &src->sinks);
			path->name = dest->kcontrol_news[i].name;
			dapm_set_mixer_path_status(dest, path, i);
			return 0;
		}
	}
	return -ENODEV;
}

static int dapm_is_shared_kcontrol(struct snd_soc_dapm_context *dapm,
	struct snd_soc_dapm_widget *kcontrolw,
	const struct snd_kcontrol_new *kcontrol_new,
	struct snd_kcontrol **kcontrol)
{
	struct snd_soc_dapm_widget *w;
	int i;

	*kcontrol = NULL;

	list_for_each_entry(w, &dapm->card->widgets, list) {
		if (w == kcontrolw || w->dapm != kcontrolw->dapm)
			continue;
		for (i = 0; i < w->num_kcontrols; i++) {
			if (&w->kcontrol_news[i] == kcontrol_new) {
				if (w->kcontrols)
					*kcontrol = w->kcontrols[i];
				return 1;
			}
		}
	}

	return 0;
}

/*
 * Determine if a kcontrol is shared. If it is, look it up. If it isn't,
 * create it. Either way, add the widget into the control's widget list
 */
static int dapm_create_or_share_mixmux_kcontrol(struct snd_soc_dapm_widget *w,
	int kci)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_card *card = dapm->card->snd_card;
	const char *prefix;
	size_t prefix_len;
	int shared;
	struct snd_kcontrol *kcontrol;
	bool wname_in_long_name, kcname_in_long_name;
	char *long_name;
	const char *name;
	int ret;

	prefix = soc_dapm_prefix(dapm);
	if (prefix)
		prefix_len = strlen(prefix) + 1;
	else
		prefix_len = 0;

	shared = dapm_is_shared_kcontrol(dapm, w, &w->kcontrol_news[kci],
					 &kcontrol);

	if (!kcontrol) {
		if (shared) {
			wname_in_long_name = false;
			kcname_in_long_name = true;
		} else {
			switch (w->id) {
			case snd_soc_dapm_switch:
			case snd_soc_dapm_mixer:
				wname_in_long_name = true;
				kcname_in_long_name = true;
				break;
			case snd_soc_dapm_mixer_named_ctl:
				wname_in_long_name = false;
				kcname_in_long_name = true;
				break;
			case snd_soc_dapm_mux:
				wname_in_long_name = true;
				kcname_in_long_name = false;
				break;
			default:
				return -EINVAL;
			}
		}

		if (wname_in_long_name && kcname_in_long_name) {
			/*
			 * The control will get a prefix from the control
			 * creation process but we're also using the same
			 * prefix for widgets so cut the prefix off the
			 * front of the widget name.
			 */
			long_name = kasprintf(GFP_KERNEL, "%s %s",
				 w->name + prefix_len,
				 w->kcontrol_news[kci].name);
			if (long_name == NULL)
				return -ENOMEM;

			name = long_name;
		} else if (wname_in_long_name) {
			long_name = NULL;
			name = w->name + prefix_len;
		} else {
			long_name = NULL;
			name = w->kcontrol_news[kci].name;
		}

		kcontrol = snd_soc_cnew(&w->kcontrol_news[kci], NULL, name,
					prefix);
		kfree(long_name);
		if (!kcontrol)
			return -ENOMEM;
		kcontrol->private_free = dapm_kcontrol_free;

		ret = dapm_kcontrol_data_alloc(w, kcontrol);
		if (ret) {
			snd_ctl_free_one(kcontrol);
			return ret;
		}

		ret = snd_ctl_add(card, kcontrol);
		if (ret < 0) {
			dev_err(dapm->dev,
				"ASoC: failed to add widget %s dapm kcontrol %s: %d\n",
				w->name, name, ret);
			return ret;
		}
	}

	ret = dapm_kcontrol_add_widget(kcontrol, w);
	if (ret)
		return ret;

	w->kcontrols[kci] = kcontrol;

	return 0;
}

/* create new dapm mixer control */
static int dapm_new_mixer(struct snd_soc_dapm_widget *w)
{
	int i, ret;
	struct snd_soc_dapm_path *path;

	/* add kcontrol */
	for (i = 0; i < w->num_kcontrols; i++) {
		/* match name */
		list_for_each_entry(path, &w->sources, list_sink) {
			/* mixer/mux paths name must match control name */
			if (path->name != (char *)w->kcontrol_news[i].name)
				continue;

			if (w->kcontrols[i]) {
				dapm_kcontrol_add_path(w->kcontrols[i], path);
				continue;
			}

			ret = dapm_create_or_share_mixmux_kcontrol(w, i);
			if (ret < 0)
				return ret;

			dapm_kcontrol_add_path(w->kcontrols[i], path);
		}
	}

	return 0;
}

/* create new dapm mux control */
static int dapm_new_mux(struct snd_soc_dapm_widget *w)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_soc_dapm_path *path;
	int ret;

	if (w->num_kcontrols != 1) {
		dev_err(dapm->dev,
			"ASoC: mux %s has incorrect number of controls\n",
			w->name);
		return -EINVAL;
	}

	if (list_empty(&w->sources)) {
		dev_err(dapm->dev, "ASoC: mux %s has no paths\n", w->name);
		return -EINVAL;
	}

	ret = dapm_create_or_share_mixmux_kcontrol(w, 0);
	if (ret < 0)
		return ret;

	list_for_each_entry(path, &w->sources, list_sink)
		dapm_kcontrol_add_path(w->kcontrols[0], path);

	return 0;
}

/* create new dapm volume control */
static int dapm_new_pga(struct snd_soc_dapm_widget *w)
{
	if (w->num_kcontrols)
		dev_err(w->dapm->dev,
			"ASoC: PGA controls not supported: '%s'\n", w->name);

	return 0;
}

/* reset 'walked' bit for each dapm path */
static void dapm_clear_walk_output(struct snd_soc_dapm_context *dapm,
				   struct list_head *sink)
{
	struct snd_soc_dapm_path *p;

	list_for_each_entry(p, sink, list_source) {
		if (p->walked) {
			p->walked = 0;
			dapm_clear_walk_output(dapm, &p->sink->sinks);
		}
	}
}

static void dapm_clear_walk_input(struct snd_soc_dapm_context *dapm,
				  struct list_head *source)
{
	struct snd_soc_dapm_path *p;

	list_for_each_entry(p, source, list_sink) {
		if (p->walked) {
			p->walked = 0;
			dapm_clear_walk_input(dapm, &p->source->sources);
		}
	}
}


/* We implement power down on suspend by checking the power state of
 * the ALSA card - when we are suspending the ALSA state for the card
 * is set to D3.
 */
static int snd_soc_dapm_suspend_check(struct snd_soc_dapm_widget *widget)
{
	int level = snd_power_get_state(widget->dapm->card->snd_card);

	switch (level) {
	case SNDRV_CTL_POWER_D3hot:
	case SNDRV_CTL_POWER_D3cold:
		if (widget->ignore_suspend)
			dev_dbg(widget->dapm->dev, "ASoC: %s ignoring suspend\n",
				widget->name);
		return widget->ignore_suspend;
	default:
		return 1;
	}
}

/* add widget to list if it's not already in the list */
static int dapm_list_add_widget(struct snd_soc_dapm_widget_list **list,
	struct snd_soc_dapm_widget *w)
{
	struct snd_soc_dapm_widget_list *wlist;
	int wlistsize, wlistentries, i;

	if (*list == NULL)
		return -EINVAL;

	wlist = *list;

	/* is this widget already in the list */
	for (i = 0; i < wlist->num_widgets; i++) {
		if (wlist->widgets[i] == w)
			return 0;
	}

	/* allocate some new space */
	wlistentries = wlist->num_widgets + 1;
	wlistsize = sizeof(struct snd_soc_dapm_widget_list) +
			wlistentries * sizeof(struct snd_soc_dapm_widget *);
	*list = krealloc(wlist, wlistsize, GFP_KERNEL);
	if (*list == NULL) {
		dev_err(w->dapm->dev, "ASoC: can't allocate widget list for %s\n",
			w->name);
		return -ENOMEM;
	}
	wlist = *list;

	/* insert the widget */
	dev_dbg(w->dapm->dev, "ASoC: added %s in widget list pos %d\n",
			w->name, wlist->num_widgets);

	wlist->widgets[wlist->num_widgets] = w;
	wlist->num_widgets++;
	return 1;
}

/*
 * Recursively check for a completed path to an active or physically connected
 * output widget. Returns number of complete paths.
 */
static int is_connected_output_ep(struct snd_soc_dapm_widget *widget,
	struct snd_soc_dapm_widget_list **list)
{
	struct snd_soc_dapm_path *path;
	int con = 0;

	if (widget->outputs >= 0)
		return widget->outputs;

	DAPM_UPDATE_STAT(widget, path_checks);

	switch (widget->id) {
	case snd_soc_dapm_supply:
	case snd_soc_dapm_regulator_supply:
	case snd_soc_dapm_clock_supply:
	case snd_soc_dapm_kcontrol:
		return 0;
	default:
		break;
	}

	switch (widget->id) {
	case snd_soc_dapm_adc:
	case snd_soc_dapm_aif_out:
	case snd_soc_dapm_dai_out:
		if (widget->active) {
			widget->outputs = snd_soc_dapm_suspend_check(widget);
			return widget->outputs;
		}
	default:
		break;
	}

	if (widget->connected) {
		/* connected pin ? */
		if (widget->id == snd_soc_dapm_output && !widget->ext) {
			widget->outputs = snd_soc_dapm_suspend_check(widget);
			return widget->outputs;
		}

		/* connected jack or spk ? */
		if (widget->id == snd_soc_dapm_hp ||
		    widget->id == snd_soc_dapm_spk ||
		    (widget->id == snd_soc_dapm_line &&
		     !list_empty(&widget->sources))) {
			widget->outputs = snd_soc_dapm_suspend_check(widget);
			return widget->outputs;
		}
	}

	list_for_each_entry(path, &widget->sinks, list_source) {
		DAPM_UPDATE_STAT(widget, neighbour_checks);

		if (path->weak)
			continue;

		if (path->walking)
			return 1;

		if (path->walked)
			continue;

		trace_snd_soc_dapm_output_path(widget, path);

		if (path->sink && path->connect) {
			path->walked = 1;
			path->walking = 1;

			/* do we need to add this widget to the list ? */
			if (list) {
				int err;
				err = dapm_list_add_widget(list, path->sink);
				if (err < 0) {
					dev_err(widget->dapm->dev,
						"ASoC: could not add widget %s\n",
						widget->name);
					path->walking = 0;
					return con;
				}
			}

			con += is_connected_output_ep(path->sink, list);

			path->walking = 0;
		}
	}

	widget->outputs = con;

	return con;
}

/*
 * Recursively check for a completed path to an active or physically connected
 * input widget. Returns number of complete paths.
 */
static int is_connected_input_ep(struct snd_soc_dapm_widget *widget,
	struct snd_soc_dapm_widget_list **list)
{
	struct snd_soc_dapm_path *path;
	int con = 0;

	if (widget->inputs >= 0)
		return widget->inputs;

	DAPM_UPDATE_STAT(widget, path_checks);

	switch (widget->id) {
	case snd_soc_dapm_supply:
	case snd_soc_dapm_regulator_supply:
	case snd_soc_dapm_clock_supply:
	case snd_soc_dapm_kcontrol:
		return 0;
	default:
		break;
	}

	/* active stream ? */
	switch (widget->id) {
	case snd_soc_dapm_dac:
	case snd_soc_dapm_aif_in:
	case snd_soc_dapm_dai_in:
		if (widget->active) {
			widget->inputs = snd_soc_dapm_suspend_check(widget);
			return widget->inputs;
		}
	default:
		break;
	}

	if (widget->connected) {
		/* connected pin ? */
		if (widget->id == snd_soc_dapm_input && !widget->ext) {
			widget->inputs = snd_soc_dapm_suspend_check(widget);
			return widget->inputs;
		}

		/* connected VMID/Bias for lower pops */
		if (widget->id == snd_soc_dapm_vmid) {
			widget->inputs = snd_soc_dapm_suspend_check(widget);
			return widget->inputs;
		}

		/* connected jack ? */
		if (widget->id == snd_soc_dapm_mic ||
		    (widget->id == snd_soc_dapm_line &&
		     !list_empty(&widget->sinks))) {
			widget->inputs = snd_soc_dapm_suspend_check(widget);
			return widget->inputs;
		}

		/* signal generator */
		if (widget->id == snd_soc_dapm_siggen) {
			widget->inputs = snd_soc_dapm_suspend_check(widget);
			return widget->inputs;
		}
	}

	list_for_each_entry(path, &widget->sources, list_sink) {
		DAPM_UPDATE_STAT(widget, neighbour_checks);

		if (path->weak)
			continue;

		if (path->walking)
			return 1;

		if (path->walked)
			continue;

		trace_snd_soc_dapm_input_path(widget, path);

		if (path->source && path->connect) {
			path->walked = 1;
			path->walking = 1;

			/* do we need to add this widget to the list ? */
			if (list) {
				int err;
				err = dapm_list_add_widget(list, path->source);
				if (err < 0) {
					dev_err(widget->dapm->dev,
						"ASoC: could not add widget %s\n",
						widget->name);
					path->walking = 0;
					return con;
				}
			}

			con += is_connected_input_ep(path->source, list);

			path->walking = 0;
		}
	}

	widget->inputs = con;

	return con;
}

/**
 * snd_soc_dapm_get_connected_widgets - query audio path and it's widgets.
 * @dai: the soc DAI.
 * @stream: stream direction.
 * @list: list of active widgets for this stream.
 *
 * Queries DAPM graph as to whether an valid audio stream path exists for
 * the initial stream specified by name. This takes into account
 * current mixer and mux kcontrol settings. Creates list of valid widgets.
 *
 * Returns the number of valid paths or negative error.
 */
int snd_soc_dapm_dai_get_connected_widgets(struct snd_soc_dai *dai, int stream,
	struct snd_soc_dapm_widget_list **list)
{
	struct snd_soc_card *card = dai->card;
	int paths;

	mutex_lock_nested(&card->dapm_mutex, SND_SOC_DAPM_CLASS_RUNTIME);
	dapm_reset(card);

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		paths = is_connected_output_ep(dai->playback_widget, list);
		dapm_clear_walk_output(&card->dapm,
				       &dai->playback_widget->sinks);
	} else {
		paths = is_connected_input_ep(dai->capture_widget, list);
		dapm_clear_walk_input(&card->dapm,
				      &dai->capture_widget->sources);
	}

	trace_snd_soc_dapm_connected(paths, stream);
	mutex_unlock(&card->dapm_mutex);

	return paths;
}

/*
 * Handler for regulator supply widget.
 */
int dapm_regulator_event(struct snd_soc_dapm_widget *w,
		   struct snd_kcontrol *kcontrol, int event)
{
	int ret;

	soc_dapm_async_complete(w->dapm);

	if (SND_SOC_DAPM_EVENT_ON(event)) {
		if (w->on_val & SND_SOC_DAPM_REGULATOR_BYPASS) {
			ret = regulator_allow_bypass(w->regulator, false);
			if (ret != 0)
				dev_warn(w->dapm->dev,
					 "ASoC: Failed to unbypass %s: %d\n",
					 w->name, ret);
		}

		return regulator_enable(w->regulator);
	} else {
		if (w->on_val & SND_SOC_DAPM_REGULATOR_BYPASS) {
			ret = regulator_allow_bypass(w->regulator, true);
			if (ret != 0)
				dev_warn(w->dapm->dev,
					 "ASoC: Failed to bypass %s: %d\n",
					 w->name, ret);
		}

		return regulator_disable_deferred(w->regulator, w->shift);
	}
}
EXPORT_SYMBOL_GPL(dapm_regulator_event);

/*
 * Handler for clock supply widget.
 */
int dapm_clock_event(struct snd_soc_dapm_widget *w,
		   struct snd_kcontrol *kcontrol, int event)
{
	if (!w->clk)
		return -EIO;

	soc_dapm_async_complete(w->dapm);

#ifdef CONFIG_HAVE_CLK
	if (SND_SOC_DAPM_EVENT_ON(event)) {
		return clk_prepare_enable(w->clk);
	} else {
		clk_disable_unprepare(w->clk);
		return 0;
	}
#endif
	return 0;
}
EXPORT_SYMBOL_GPL(dapm_clock_event);

static int dapm_widget_power_check(struct snd_soc_dapm_widget *w)
{
	if (w->power_checked)
		return w->new_power;

	if (w->force)
		w->new_power = 1;
	else
		w->new_power = w->power_check(w);

	w->power_checked = true;

	return w->new_power;
}

/* Generic check to see if a widget should be powered.
 */
static int dapm_generic_check_power(struct snd_soc_dapm_widget *w)
{
	int in, out;

	DAPM_UPDATE_STAT(w, power_checks);

	in = is_connected_input_ep(w, NULL);
	dapm_clear_walk_input(w->dapm, &w->sources);
	out = is_connected_output_ep(w, NULL);
	dapm_clear_walk_output(w->dapm, &w->sinks);
	return out != 0 && in != 0;
}

/* Check to see if an ADC has power */
static int dapm_adc_check_power(struct snd_soc_dapm_widget *w)
{
	int in;

	DAPM_UPDATE_STAT(w, power_checks);

	if (w->active) {
		in = is_connected_input_ep(w, NULL);
		dapm_clear_walk_input(w->dapm, &w->sources);
		return in != 0;
	} else {
		return dapm_generic_check_power(w);
	}
}

/* Check to see if a DAC has power */
static int dapm_dac_check_power(struct snd_soc_dapm_widget *w)
{
	int out;

	DAPM_UPDATE_STAT(w, power_checks);

	if (w->active) {
		out = is_connected_output_ep(w, NULL);
		dapm_clear_walk_output(w->dapm, &w->sinks);
		return out != 0;
	} else {
		return dapm_generic_check_power(w);
	}
}

/* Check to see if a power supply is needed */
static int dapm_supply_check_power(struct snd_soc_dapm_widget *w)
{
	struct snd_soc_dapm_path *path;

	DAPM_UPDATE_STAT(w, power_checks);

	/* Check if one of our outputs is connected */
	list_for_each_entry(path, &w->sinks, list_source) {
		DAPM_UPDATE_STAT(w, neighbour_checks);

		if (path->weak)
			continue;

		if (path->connected &&
		    !path->connected(path->source, path->sink))
			continue;

		if (!path->sink)
			continue;

		if (dapm_widget_power_check(path->sink))
			return 1;
	}

	return 0;
}

static int dapm_always_on_check_power(struct snd_soc_dapm_widget *w)
{
	return 1;
}

static int dapm_seq_compare(struct snd_soc_dapm_widget *a,
			    struct snd_soc_dapm_widget *b,
			    bool power_up)
{
	int *sort;

	if (power_up)
		sort = dapm_up_seq;
	else
		sort = dapm_down_seq;

	if (sort[a->id] != sort[b->id])
		return sort[a->id] - sort[b->id];
	if (a->subseq != b->subseq) {
		if (power_up)
			return a->subseq - b->subseq;
		else
			return b->subseq - a->subseq;
	}
	if (a->reg != b->reg)
		return a->reg - b->reg;
	if (a->dapm != b->dapm)
		return (unsigned long)a->dapm - (unsigned long)b->dapm;

	return 0;
}

/* Insert a widget in order into a DAPM power sequence. */
static void dapm_seq_insert(struct snd_soc_dapm_widget *new_widget,
			    struct list_head *list,
			    bool power_up)
{
	struct snd_soc_dapm_widget *w;

	list_for_each_entry(w, list, power_list)
		if (dapm_seq_compare(new_widget, w, power_up) < 0) {
			list_add_tail(&new_widget->power_list, &w->power_list);
			return;
		}

	list_add_tail(&new_widget->power_list, list);
}

static void dapm_seq_check_event(struct snd_soc_card *card,
				 struct snd_soc_dapm_widget *w, int event)
{
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
	case SND_SOC_DAPM_WILL_PMU:
		ev_name = "WILL_PMU";
		power = 1;
		break;
	case SND_SOC_DAPM_WILL_PMD:
		ev_name = "WILL_PMD";
		power = 0;
		break;
	default:
		WARN(1, "Unknown event %d\n", event);
		return;
	}

	if (w->new_power != power)
		return;

	if (w->event && (w->event_flags & event)) {
		pop_dbg(w->dapm->dev, card->pop_time, "pop test : %s %s\n",
			w->name, ev_name);
		soc_dapm_async_complete(w->dapm);
		trace_snd_soc_dapm_widget_event_start(w, event);
		ret = w->event(w, NULL, event);
		trace_snd_soc_dapm_widget_event_done(w, event);
		if (ret < 0)
			dev_err(w->dapm->dev, "ASoC: %s: %s event failed: %d\n",
			       ev_name, w->name, ret);
	}
}

/* Apply the coalesced changes from a DAPM sequence */
static void dapm_seq_run_coalesced(struct snd_soc_card *card,
				   struct list_head *pending)
{
	struct snd_soc_dapm_context *dapm;
	struct snd_soc_dapm_widget *w;
	int reg;
	unsigned int value = 0;
	unsigned int mask = 0;

	w = list_first_entry(pending, struct snd_soc_dapm_widget, power_list);
	reg = w->reg;
	dapm = w->dapm;

	list_for_each_entry(w, pending, power_list) {
		WARN_ON(reg != w->reg || dapm != w->dapm);
		w->power = w->new_power;

		mask |= w->mask << w->shift;
		if (w->power)
			value |= w->on_val << w->shift;
		else
			value |= w->off_val << w->shift;

		pop_dbg(dapm->dev, card->pop_time,
			"pop test : Queue %s: reg=0x%x, 0x%x/0x%x\n",
			w->name, reg, value, mask);

		/* Check for events */
		dapm_seq_check_event(card, w, SND_SOC_DAPM_PRE_PMU);
		dapm_seq_check_event(card, w, SND_SOC_DAPM_PRE_PMD);
	}

	if (reg >= 0) {
		/* Any widget will do, they should all be updating the
		 * same register.
		 */

		pop_dbg(dapm->dev, card->pop_time,
			"pop test : Applying 0x%x/0x%x to %x in %dms\n",
			value, mask, reg, card->pop_time);
		pop_wait(card->pop_time);
		soc_dapm_update_bits(dapm, reg, mask, value);
	}

	list_for_each_entry(w, pending, power_list) {
		dapm_seq_check_event(card, w, SND_SOC_DAPM_POST_PMU);
		dapm_seq_check_event(card, w, SND_SOC_DAPM_POST_PMD);
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
static void dapm_seq_run(struct snd_soc_card *card,
	struct list_head *list, int event, bool power_up)
{
	struct snd_soc_dapm_widget *w, *n;
	struct snd_soc_dapm_context *d;
	LIST_HEAD(pending);
	int cur_sort = -1;
	int cur_subseq = -1;
	int cur_reg = SND_SOC_NOPM;
	struct snd_soc_dapm_context *cur_dapm = NULL;
	int ret, i;
	int *sort;

	if (power_up)
		sort = dapm_up_seq;
	else
		sort = dapm_down_seq;

	list_for_each_entry_safe(w, n, list, power_list) {
		ret = 0;

		/* Do we need to apply any queued changes? */
		if (sort[w->id] != cur_sort || w->reg != cur_reg ||
		    w->dapm != cur_dapm || w->subseq != cur_subseq) {
			if (!list_empty(&pending))
				dapm_seq_run_coalesced(card, &pending);

			if (cur_dapm && cur_dapm->seq_notifier) {
				for (i = 0; i < ARRAY_SIZE(dapm_up_seq); i++)
					if (sort[i] == cur_sort)
						cur_dapm->seq_notifier(cur_dapm,
								       i,
								       cur_subseq);
			}

			if (cur_dapm && w->dapm != cur_dapm)
				soc_dapm_async_complete(cur_dapm);

			INIT_LIST_HEAD(&pending);
			cur_sort = -1;
			cur_subseq = INT_MIN;
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

		default:
			/* Queue it up for application */
			cur_sort = sort[w->id];
			cur_subseq = w->subseq;
			cur_reg = w->reg;
			cur_dapm = w->dapm;
			list_move(&w->power_list, &pending);
			break;
		}

		if (ret < 0)
			dev_err(w->dapm->dev,
				"ASoC: Failed to apply widget power: %d\n", ret);
	}

	if (!list_empty(&pending))
		dapm_seq_run_coalesced(card, &pending);

	if (cur_dapm && cur_dapm->seq_notifier) {
		for (i = 0; i < ARRAY_SIZE(dapm_up_seq); i++)
			if (sort[i] == cur_sort)
				cur_dapm->seq_notifier(cur_dapm,
						       i, cur_subseq);
	}

	list_for_each_entry(d, &card->dapm_list, list) {
		soc_dapm_async_complete(d);
	}
}

static void dapm_widget_update(struct snd_soc_card *card)
{
	struct snd_soc_dapm_update *update = card->update;
	struct snd_soc_dapm_widget_list *wlist;
	struct snd_soc_dapm_widget *w = NULL;
	unsigned int wi;
	int ret;

	if (!update || !dapm_kcontrol_is_powered(update->kcontrol))
		return;

	wlist = dapm_kcontrol_get_wlist(update->kcontrol);

	for (wi = 0; wi < wlist->num_widgets; wi++) {
		w = wlist->widgets[wi];

		if (w->event && (w->event_flags & SND_SOC_DAPM_PRE_REG)) {
			ret = w->event(w, update->kcontrol, SND_SOC_DAPM_PRE_REG);
			if (ret != 0)
				dev_err(w->dapm->dev, "ASoC: %s DAPM pre-event failed: %d\n",
					   w->name, ret);
		}
	}

	if (!w)
		return;

	ret = soc_dapm_update_bits(w->dapm, update->reg, update->mask,
		update->val);
	if (ret < 0)
		dev_err(w->dapm->dev, "ASoC: %s DAPM update failed: %d\n",
			w->name, ret);

	for (wi = 0; wi < wlist->num_widgets; wi++) {
		w = wlist->widgets[wi];

		if (w->event && (w->event_flags & SND_SOC_DAPM_POST_REG)) {
			ret = w->event(w, update->kcontrol, SND_SOC_DAPM_POST_REG);
			if (ret != 0)
				dev_err(w->dapm->dev, "ASoC: %s DAPM post-event failed: %d\n",
					   w->name, ret);
		}
	}
}

/* Async callback run prior to DAPM sequences - brings to _PREPARE if
 * they're changing state.
 */
static void dapm_pre_sequence_async(void *data, async_cookie_t cookie)
{
	struct snd_soc_dapm_context *d = data;
	int ret;

	/* If we're off and we're not supposed to be go into STANDBY */
	if (d->bias_level == SND_SOC_BIAS_OFF &&
	    d->target_bias_level != SND_SOC_BIAS_OFF) {
		if (d->dev)
			pm_runtime_get_sync(d->dev);

		ret = snd_soc_dapm_set_bias_level(d, SND_SOC_BIAS_STANDBY);
		if (ret != 0)
			dev_err(d->dev,
				"ASoC: Failed to turn on bias: %d\n", ret);
	}

	/* Prepare for a transition to ON or away from ON */
	if ((d->target_bias_level == SND_SOC_BIAS_ON &&
	     d->bias_level != SND_SOC_BIAS_ON) ||
	    (d->target_bias_level != SND_SOC_BIAS_ON &&
	     d->bias_level == SND_SOC_BIAS_ON)) {
		ret = snd_soc_dapm_set_bias_level(d, SND_SOC_BIAS_PREPARE);
		if (ret != 0)
			dev_err(d->dev,
				"ASoC: Failed to prepare bias: %d\n", ret);
	}
}

/* Async callback run prior to DAPM sequences - brings to their final
 * state.
 */
static void dapm_post_sequence_async(void *data, async_cookie_t cookie)
{
	struct snd_soc_dapm_context *d = data;
	int ret;

	/* If we just powered the last thing off drop to standby bias */
	if (d->bias_level == SND_SOC_BIAS_PREPARE &&
	    (d->target_bias_level == SND_SOC_BIAS_STANDBY ||
	     d->target_bias_level == SND_SOC_BIAS_OFF)) {
		ret = snd_soc_dapm_set_bias_level(d, SND_SOC_BIAS_STANDBY);
		if (ret != 0)
			dev_err(d->dev, "ASoC: Failed to apply standby bias: %d\n",
				ret);
	}

	/* If we're in standby and can support bias off then do that */
	if (d->bias_level == SND_SOC_BIAS_STANDBY &&
	    d->target_bias_level == SND_SOC_BIAS_OFF) {
		ret = snd_soc_dapm_set_bias_level(d, SND_SOC_BIAS_OFF);
		if (ret != 0)
			dev_err(d->dev, "ASoC: Failed to turn off bias: %d\n",
				ret);

		if (d->dev)
			pm_runtime_put(d->dev);
	}

	/* If we just powered up then move to active bias */
	if (d->bias_level == SND_SOC_BIAS_PREPARE &&
	    d->target_bias_level == SND_SOC_BIAS_ON) {
		ret = snd_soc_dapm_set_bias_level(d, SND_SOC_BIAS_ON);
		if (ret != 0)
			dev_err(d->dev, "ASoC: Failed to apply active bias: %d\n",
				ret);
	}
}

static void dapm_widget_set_peer_power(struct snd_soc_dapm_widget *peer,
				       bool power, bool connect)
{
	/* If a connection is being made or broken then that update
	 * will have marked the peer dirty, otherwise the widgets are
	 * not connected and this update has no impact. */
	if (!connect)
		return;

	/* If the peer is already in the state we're moving to then we
	 * won't have an impact on it. */
	if (power != peer->power)
		dapm_mark_dirty(peer, "peer state change");
}

static void dapm_widget_set_power(struct snd_soc_dapm_widget *w, bool power,
				  struct list_head *up_list,
				  struct list_head *down_list)
{
	struct snd_soc_dapm_path *path;

	if (w->power == power)
		return;

	trace_snd_soc_dapm_widget_power(w, power);

	/* If we changed our power state perhaps our neigbours changed
	 * also.
	 */
	list_for_each_entry(path, &w->sources, list_sink) {
		if (path->source) {
			dapm_widget_set_peer_power(path->source, power,
						   path->connect);
		}
	}
	switch (w->id) {
	case snd_soc_dapm_supply:
	case snd_soc_dapm_regulator_supply:
	case snd_soc_dapm_clock_supply:
	case snd_soc_dapm_kcontrol:
		/* Supplies can't affect their outputs, only their inputs */
		break;
	default:
		list_for_each_entry(path, &w->sinks, list_source) {
			if (path->sink) {
				dapm_widget_set_peer_power(path->sink, power,
							   path->connect);
			}
		}
		break;
	}

	if (power)
		dapm_seq_insert(w, up_list, true);
	else
		dapm_seq_insert(w, down_list, false);
}

static void dapm_power_one_widget(struct snd_soc_dapm_widget *w,
				  struct list_head *up_list,
				  struct list_head *down_list)
{
	int power;

	switch (w->id) {
	case snd_soc_dapm_pre:
		dapm_seq_insert(w, down_list, false);
		break;
	case snd_soc_dapm_post:
		dapm_seq_insert(w, up_list, true);
		break;

	default:
		power = dapm_widget_power_check(w);

		dapm_widget_set_power(w, power, up_list, down_list);
		break;
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
static int dapm_power_widgets(struct snd_soc_card *card, int event)
{
	struct snd_soc_dapm_widget *w;
	struct snd_soc_dapm_context *d;
	LIST_HEAD(up_list);
	LIST_HEAD(down_list);
	ASYNC_DOMAIN_EXCLUSIVE(async_domain);
	enum snd_soc_bias_level bias;

	lockdep_assert_held(&card->dapm_mutex);

	trace_snd_soc_dapm_start(card);

	list_for_each_entry(d, &card->dapm_list, list) {
		if (d->idle_bias_off)
			d->target_bias_level = SND_SOC_BIAS_OFF;
		else
			d->target_bias_level = SND_SOC_BIAS_STANDBY;
	}

	dapm_reset(card);

	/* Check which widgets we need to power and store them in
	 * lists indicating if they should be powered up or down.  We
	 * only check widgets that have been flagged as dirty but note
	 * that new widgets may be added to the dirty list while we
	 * iterate.
	 */
	list_for_each_entry(w, &card->dapm_dirty, dirty) {
		dapm_power_one_widget(w, &up_list, &down_list);
	}

	list_for_each_entry(w, &card->widgets, list) {
		switch (w->id) {
		case snd_soc_dapm_pre:
		case snd_soc_dapm_post:
			/* These widgets always need to be powered */
			break;
		default:
			list_del_init(&w->dirty);
			break;
		}

		if (w->new_power) {
			d = w->dapm;

			/* Supplies and micbiases only bring the
			 * context up to STANDBY as unless something
			 * else is active and passing audio they
			 * generally don't require full power.  Signal
			 * generators are virtual pins and have no
			 * power impact themselves.
			 */
			switch (w->id) {
			case snd_soc_dapm_siggen:
			case snd_soc_dapm_vmid:
				break;
			case snd_soc_dapm_supply:
			case snd_soc_dapm_regulator_supply:
			case snd_soc_dapm_clock_supply:
			case snd_soc_dapm_micbias:
				if (d->target_bias_level < SND_SOC_BIAS_STANDBY)
					d->target_bias_level = SND_SOC_BIAS_STANDBY;
				break;
			default:
				d->target_bias_level = SND_SOC_BIAS_ON;
				break;
			}
		}

	}

	/* Force all contexts in the card to the same bias state if
	 * they're not ground referenced.
	 */
	bias = SND_SOC_BIAS_OFF;
	list_for_each_entry(d, &card->dapm_list, list)
		if (d->target_bias_level > bias)
			bias = d->target_bias_level;
	list_for_each_entry(d, &card->dapm_list, list)
		if (!d->idle_bias_off)
			d->target_bias_level = bias;

	trace_snd_soc_dapm_walk_done(card);

	/* Run card bias changes at first */
	dapm_pre_sequence_async(&card->dapm, 0);
	/* Run other bias changes in parallel */
	list_for_each_entry(d, &card->dapm_list, list) {
		if (d != &card->dapm)
			async_schedule_domain(dapm_pre_sequence_async, d,
						&async_domain);
	}
	async_synchronize_full_domain(&async_domain);

	list_for_each_entry(w, &down_list, power_list) {
		dapm_seq_check_event(card, w, SND_SOC_DAPM_WILL_PMD);
	}

	list_for_each_entry(w, &up_list, power_list) {
		dapm_seq_check_event(card, w, SND_SOC_DAPM_WILL_PMU);
	}

	/* Power down widgets first; try to avoid amplifying pops. */
	dapm_seq_run(card, &down_list, event, false);

	dapm_widget_update(card);

	/* Now power up. */
	dapm_seq_run(card, &up_list, event, true);

	/* Run all the bias changes in parallel */
	list_for_each_entry(d, &card->dapm_list, list) {
		if (d != &card->dapm)
			async_schedule_domain(dapm_post_sequence_async, d,
						&async_domain);
	}
	async_synchronize_full_domain(&async_domain);
	/* Run card bias changes at last */
	dapm_post_sequence_async(&card->dapm, 0);

	/* do we need to notify any clients that DAPM event is complete */
	list_for_each_entry(d, &card->dapm_list, list) {
		if (d->stream_event)
			d->stream_event(d, event);
	}

	pop_dbg(card->dev, card->pop_time,
		"DAPM sequencing finished, waiting %dms\n", card->pop_time);
	pop_wait(card->pop_time);

	trace_snd_soc_dapm_done(card);

	return 0;
}

#ifdef CONFIG_DEBUG_FS
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

	in = is_connected_input_ep(w, NULL);
	dapm_clear_walk_input(w->dapm, &w->sources);
	out = is_connected_output_ep(w, NULL);
	dapm_clear_walk_output(w->dapm, &w->sinks);

	ret = snprintf(buf, PAGE_SIZE, "%s: %s%s  in %d out %d",
		       w->name, w->power ? "On" : "Off",
		       w->force ? " (forced)" : "", in, out);

	if (w->reg >= 0)
		ret += snprintf(buf + ret, PAGE_SIZE - ret,
				" - R%d(0x%x) mask 0x%x",
				w->reg, w->reg, w->mask << w->shift);

	ret += snprintf(buf + ret, PAGE_SIZE - ret, "\n");

	if (w->sname)
		ret += snprintf(buf + ret, PAGE_SIZE - ret, " stream %s %s\n",
				w->sname,
				w->active ? "active" : "inactive");

	list_for_each_entry(p, &w->sources, list_sink) {
		if (p->connected && !p->connected(w, p->source))
			continue;

		if (p->connect)
			ret += snprintf(buf + ret, PAGE_SIZE - ret,
					" in  \"%s\" \"%s\"\n",
					p->name ? p->name : "static",
					p->source->name);
	}
	list_for_each_entry(p, &w->sinks, list_source) {
		if (p->connected && !p->connected(w, p->sink))
			continue;

		if (p->connect)
			ret += snprintf(buf + ret, PAGE_SIZE - ret,
					" out \"%s\" \"%s\"\n",
					p->name ? p->name : "static",
					p->sink->name);
	}

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

	kfree(buf);
	return ret;
}

static const struct file_operations dapm_widget_power_fops = {
	.open = simple_open,
	.read = dapm_widget_power_read_file,
	.llseek = default_llseek,
};

static ssize_t dapm_bias_read_file(struct file *file, char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct snd_soc_dapm_context *dapm = file->private_data;
	char *level;

	switch (dapm->bias_level) {
	case SND_SOC_BIAS_ON:
		level = "On\n";
		break;
	case SND_SOC_BIAS_PREPARE:
		level = "Prepare\n";
		break;
	case SND_SOC_BIAS_STANDBY:
		level = "Standby\n";
		break;
	case SND_SOC_BIAS_OFF:
		level = "Off\n";
		break;
	default:
		WARN(1, "Unknown bias_level %d\n", dapm->bias_level);
		level = "Unknown\n";
		break;
	}

	return simple_read_from_buffer(user_buf, count, ppos, level,
				       strlen(level));
}

static const struct file_operations dapm_bias_fops = {
	.open = simple_open,
	.read = dapm_bias_read_file,
	.llseek = default_llseek,
};

void snd_soc_dapm_debugfs_init(struct snd_soc_dapm_context *dapm,
	struct dentry *parent)
{
	struct dentry *d;

	dapm->debugfs_dapm = debugfs_create_dir("dapm", parent);

	if (!dapm->debugfs_dapm) {
		dev_warn(dapm->dev,
		       "ASoC: Failed to create DAPM debugfs directory\n");
		return;
	}

	d = debugfs_create_file("bias_level", 0444,
				dapm->debugfs_dapm, dapm,
				&dapm_bias_fops);
	if (!d)
		dev_warn(dapm->dev,
			 "ASoC: Failed to create bias level debugfs file\n");
}

static void dapm_debugfs_add_widget(struct snd_soc_dapm_widget *w)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct dentry *d;

	if (!dapm->debugfs_dapm || !w->name)
		return;

	d = debugfs_create_file(w->name, 0444,
				dapm->debugfs_dapm, w,
				&dapm_widget_power_fops);
	if (!d)
		dev_warn(w->dapm->dev,
			"ASoC: Failed to create %s debugfs file\n",
			w->name);
}

static void dapm_debugfs_cleanup(struct snd_soc_dapm_context *dapm)
{
	debugfs_remove_recursive(dapm->debugfs_dapm);
}

#else
void snd_soc_dapm_debugfs_init(struct snd_soc_dapm_context *dapm,
	struct dentry *parent)
{
}

static inline void dapm_debugfs_add_widget(struct snd_soc_dapm_widget *w)
{
}

static inline void dapm_debugfs_cleanup(struct snd_soc_dapm_context *dapm)
{
}

#endif

/* test and update the power status of a mux widget */
static int soc_dapm_mux_update_power(struct snd_soc_card *card,
				 struct snd_kcontrol *kcontrol, int mux, struct soc_enum *e)
{
	struct snd_soc_dapm_path *path;
	int found = 0;

	lockdep_assert_held(&card->dapm_mutex);

	/* find dapm widget path assoc with kcontrol */
	dapm_kcontrol_for_each_path(path, kcontrol) {
		if (!path->name || !e->texts[mux])
			continue;

		found = 1;
		/* we now need to match the string in the enum to the path */
		if (!(strcmp(path->name, e->texts[mux]))) {
			path->connect = 1; /* new connection */
			dapm_mark_dirty(path->source, "mux connection");
		} else {
			if (path->connect)
				dapm_mark_dirty(path->source,
						"mux disconnection");
			path->connect = 0; /* old connection must be powered down */
		}
		dapm_mark_dirty(path->sink, "mux change");
	}

	if (found)
		dapm_power_widgets(card, SND_SOC_DAPM_STREAM_NOP);

	return found;
}

int snd_soc_dapm_mux_update_power(struct snd_soc_dapm_context *dapm,
	struct snd_kcontrol *kcontrol, int mux, struct soc_enum *e,
	struct snd_soc_dapm_update *update)
{
	struct snd_soc_card *card = dapm->card;
	int ret;

	mutex_lock_nested(&card->dapm_mutex, SND_SOC_DAPM_CLASS_RUNTIME);
	card->update = update;
	ret = soc_dapm_mux_update_power(card, kcontrol, mux, e);
	card->update = NULL;
	mutex_unlock(&card->dapm_mutex);
	if (ret > 0)
		soc_dpcm_runtime_update(card);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_mux_update_power);

/* test and update the power status of a mixer or switch widget */
static int soc_dapm_mixer_update_power(struct snd_soc_card *card,
				   struct snd_kcontrol *kcontrol, int connect)
{
	struct snd_soc_dapm_path *path;
	int found = 0;

	lockdep_assert_held(&card->dapm_mutex);

	/* find dapm widget path assoc with kcontrol */
	dapm_kcontrol_for_each_path(path, kcontrol) {
		found = 1;
		path->connect = connect;
		dapm_mark_dirty(path->source, "mixer connection");
		dapm_mark_dirty(path->sink, "mixer update");
	}

	if (found)
		dapm_power_widgets(card, SND_SOC_DAPM_STREAM_NOP);

	return found;
}

int snd_soc_dapm_mixer_update_power(struct snd_soc_dapm_context *dapm,
	struct snd_kcontrol *kcontrol, int connect,
	struct snd_soc_dapm_update *update)
{
	struct snd_soc_card *card = dapm->card;
	int ret;

	mutex_lock_nested(&card->dapm_mutex, SND_SOC_DAPM_CLASS_RUNTIME);
	card->update = update;
	ret = soc_dapm_mixer_update_power(card, kcontrol, connect);
	card->update = NULL;
	mutex_unlock(&card->dapm_mutex);
	if (ret > 0)
		soc_dpcm_runtime_update(card);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_mixer_update_power);

static ssize_t dapm_widget_show_codec(struct snd_soc_codec *codec, char *buf)
{
	struct snd_soc_dapm_widget *w;
	int count = 0;
	char *state = "not set";

	list_for_each_entry(w, &codec->component.card->widgets, list) {
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
		case snd_soc_dapm_regulator_supply:
		case snd_soc_dapm_clock_supply:
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

/* show dapm widget status in sys fs */
static ssize_t dapm_widget_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct snd_soc_pcm_runtime *rtd = dev_get_drvdata(dev);
	int i, count = 0;

	for (i = 0; i < rtd->num_codecs; i++) {
		struct snd_soc_codec *codec = rtd->codec_dais[i]->codec;
		count += dapm_widget_show_codec(codec, buf + count);
	}

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

static void dapm_free_path(struct snd_soc_dapm_path *path)
{
	list_del(&path->list_sink);
	list_del(&path->list_source);
	list_del(&path->list_kcontrol);
	list_del(&path->list);
	kfree(path);
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
		list_for_each_entry_safe(p, next_p, &w->sources, list_sink)
			dapm_free_path(p);

		list_for_each_entry_safe(p, next_p, &w->sinks, list_source)
			dapm_free_path(p);

		kfree(w->kcontrols);
		kfree(w->name);
		kfree(w);
	}
}

static struct snd_soc_dapm_widget *dapm_find_widget(
			struct snd_soc_dapm_context *dapm, const char *pin,
			bool search_other_contexts)
{
	struct snd_soc_dapm_widget *w;
	struct snd_soc_dapm_widget *fallback = NULL;

	list_for_each_entry(w, &dapm->card->widgets, list) {
		if (!strcmp(w->name, pin)) {
			if (w->dapm == dapm)
				return w;
			else
				fallback = w;
		}
	}

	if (search_other_contexts)
		return fallback;

	return NULL;
}

static int snd_soc_dapm_set_pin(struct snd_soc_dapm_context *dapm,
				const char *pin, int status)
{
	struct snd_soc_dapm_widget *w = dapm_find_widget(dapm, pin, true);

	dapm_assert_locked(dapm);

	if (!w) {
		dev_err(dapm->dev, "ASoC: DAPM unknown pin %s\n", pin);
		return -EINVAL;
	}

	if (w->connected != status)
		dapm_mark_dirty(w, "pin configuration");

	w->connected = status;
	if (status == 0)
		w->force = 0;

	return 0;
}

/**
 * snd_soc_dapm_sync_unlocked - scan and power dapm paths
 * @dapm: DAPM context
 *
 * Walks all dapm audio paths and powers widgets according to their
 * stream or path usage.
 *
 * Requires external locking.
 *
 * Returns 0 for success.
 */
int snd_soc_dapm_sync_unlocked(struct snd_soc_dapm_context *dapm)
{
	/*
	 * Suppress early reports (eg, jacks syncing their state) to avoid
	 * silly DAPM runs during card startup.
	 */
	if (!dapm->card || !dapm->card->instantiated)
		return 0;

	return dapm_power_widgets(dapm->card, SND_SOC_DAPM_STREAM_NOP);
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_sync_unlocked);

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
	int ret;

	mutex_lock_nested(&dapm->card->dapm_mutex, SND_SOC_DAPM_CLASS_RUNTIME);
	ret = snd_soc_dapm_sync_unlocked(dapm);
	mutex_unlock(&dapm->card->dapm_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_sync);

static int snd_soc_dapm_add_path(struct snd_soc_dapm_context *dapm,
	struct snd_soc_dapm_widget *wsource, struct snd_soc_dapm_widget *wsink,
	const char *control,
	int (*connected)(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink))
{
	struct snd_soc_dapm_path *path;
	int ret;

	path = kzalloc(sizeof(struct snd_soc_dapm_path), GFP_KERNEL);
	if (!path)
		return -ENOMEM;

	path->source = wsource;
	path->sink = wsink;
	path->connected = connected;
	INIT_LIST_HEAD(&path->list);
	INIT_LIST_HEAD(&path->list_kcontrol);
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

	dapm_mark_dirty(wsource, "Route added");
	dapm_mark_dirty(wsink, "Route added");

	/* connect static paths */
	if (control == NULL) {
		list_add(&path->list, &dapm->card->paths);
		list_add(&path->list_sink, &wsink->sources);
		list_add(&path->list_source, &wsource->sinks);
		path->connect = 1;
		return 0;
	}

	/* connect dynamic paths */
	switch (wsink->id) {
	case snd_soc_dapm_adc:
	case snd_soc_dapm_dac:
	case snd_soc_dapm_pga:
	case snd_soc_dapm_out_drv:
	case snd_soc_dapm_input:
	case snd_soc_dapm_output:
	case snd_soc_dapm_siggen:
	case snd_soc_dapm_micbias:
	case snd_soc_dapm_vmid:
	case snd_soc_dapm_pre:
	case snd_soc_dapm_post:
	case snd_soc_dapm_supply:
	case snd_soc_dapm_regulator_supply:
	case snd_soc_dapm_clock_supply:
	case snd_soc_dapm_aif_in:
	case snd_soc_dapm_aif_out:
	case snd_soc_dapm_dai_in:
	case snd_soc_dapm_dai_out:
	case snd_soc_dapm_dai_link:
	case snd_soc_dapm_kcontrol:
		list_add(&path->list, &dapm->card->paths);
		list_add(&path->list_sink, &wsink->sources);
		list_add(&path->list_source, &wsource->sinks);
		path->connect = 1;
		return 0;
	case snd_soc_dapm_mux:
		ret = dapm_connect_mux(dapm, wsource, wsink, path, control,
			&wsink->kcontrol_news[0]);
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
	kfree(path);
	return ret;
}

static int snd_soc_dapm_add_route(struct snd_soc_dapm_context *dapm,
				  const struct snd_soc_dapm_route *route)
{
	struct snd_soc_dapm_widget *wsource = NULL, *wsink = NULL, *w;
	struct snd_soc_dapm_widget *wtsource = NULL, *wtsink = NULL;
	const char *sink;
	const char *source;
	char prefixed_sink[80];
	char prefixed_source[80];
	const char *prefix;
	int ret;

	prefix = soc_dapm_prefix(dapm);
	if (prefix) {
		snprintf(prefixed_sink, sizeof(prefixed_sink), "%s %s",
			 prefix, route->sink);
		sink = prefixed_sink;
		snprintf(prefixed_source, sizeof(prefixed_source), "%s %s",
			 prefix, route->source);
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

	if (wsource == NULL) {
		dev_err(dapm->dev, "ASoC: no source widget found for %s\n",
			route->source);
		return -ENODEV;
	}
	if (wsink == NULL) {
		dev_err(dapm->dev, "ASoC: no sink widget found for %s\n",
			route->sink);
		return -ENODEV;
	}

	ret = snd_soc_dapm_add_path(dapm, wsource, wsink, route->control,
		route->connected);
	if (ret)
		goto err;

	return 0;
err:
	dev_warn(dapm->dev, "ASoC: no dapm match for %s --> %s --> %s\n",
		 source, route->control, sink);
	return ret;
}

static int snd_soc_dapm_del_route(struct snd_soc_dapm_context *dapm,
				  const struct snd_soc_dapm_route *route)
{
	struct snd_soc_dapm_path *path, *p;
	const char *sink;
	const char *source;
	char prefixed_sink[80];
	char prefixed_source[80];
	const char *prefix;

	if (route->control) {
		dev_err(dapm->dev,
			"ASoC: Removal of routes with controls not supported\n");
		return -EINVAL;
	}

	prefix = soc_dapm_prefix(dapm);
	if (prefix) {
		snprintf(prefixed_sink, sizeof(prefixed_sink), "%s %s",
			 prefix, route->sink);
		sink = prefixed_sink;
		snprintf(prefixed_source, sizeof(prefixed_source), "%s %s",
			 prefix, route->source);
		source = prefixed_source;
	} else {
		sink = route->sink;
		source = route->source;
	}

	path = NULL;
	list_for_each_entry(p, &dapm->card->paths, list) {
		if (strcmp(p->source->name, source) != 0)
			continue;
		if (strcmp(p->sink->name, sink) != 0)
			continue;
		path = p;
		break;
	}

	if (path) {
		dapm_mark_dirty(path->source, "Route removed");
		dapm_mark_dirty(path->sink, "Route removed");

		dapm_free_path(path);
	} else {
		dev_warn(dapm->dev, "ASoC: Route %s->%s does not exist\n",
			 source, sink);
	}

	return 0;
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
	int i, r, ret = 0;

	mutex_lock_nested(&dapm->card->dapm_mutex, SND_SOC_DAPM_CLASS_INIT);
	for (i = 0; i < num; i++) {
		r = snd_soc_dapm_add_route(dapm, route);
		if (r < 0) {
			dev_err(dapm->dev, "ASoC: Failed to add route %s -> %s -> %s\n",
				route->source,
				route->control ? route->control : "direct",
				route->sink);
			ret = r;
		}
		route++;
	}
	mutex_unlock(&dapm->card->dapm_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_add_routes);

/**
 * snd_soc_dapm_del_routes - Remove routes between DAPM widgets
 * @dapm: DAPM context
 * @route: audio routes
 * @num: number of routes
 *
 * Removes routes from the DAPM context.
 */
int snd_soc_dapm_del_routes(struct snd_soc_dapm_context *dapm,
			    const struct snd_soc_dapm_route *route, int num)
{
	int i, ret = 0;

	mutex_lock_nested(&dapm->card->dapm_mutex, SND_SOC_DAPM_CLASS_INIT);
	for (i = 0; i < num; i++) {
		snd_soc_dapm_del_route(dapm, route);
		route++;
	}
	mutex_unlock(&dapm->card->dapm_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_del_routes);

static int snd_soc_dapm_weak_route(struct snd_soc_dapm_context *dapm,
				   const struct snd_soc_dapm_route *route)
{
	struct snd_soc_dapm_widget *source = dapm_find_widget(dapm,
							      route->source,
							      true);
	struct snd_soc_dapm_widget *sink = dapm_find_widget(dapm,
							    route->sink,
							    true);
	struct snd_soc_dapm_path *path;
	int count = 0;

	if (!source) {
		dev_err(dapm->dev, "ASoC: Unable to find source %s for weak route\n",
			route->source);
		return -ENODEV;
	}

	if (!sink) {
		dev_err(dapm->dev, "ASoC: Unable to find sink %s for weak route\n",
			route->sink);
		return -ENODEV;
	}

	if (route->control || route->connected)
		dev_warn(dapm->dev, "ASoC: Ignoring control for weak route %s->%s\n",
			 route->source, route->sink);

	list_for_each_entry(path, &source->sinks, list_source) {
		if (path->sink == sink) {
			path->weak = 1;
			count++;
		}
	}

	if (count == 0)
		dev_err(dapm->dev, "ASoC: No path found for weak route %s->%s\n",
			route->source, route->sink);
	if (count > 1)
		dev_warn(dapm->dev, "ASoC: %d paths found for weak route %s->%s\n",
			 count, route->source, route->sink);

	return 0;
}

/**
 * snd_soc_dapm_weak_routes - Mark routes between DAPM widgets as weak
 * @dapm: DAPM context
 * @route: audio routes
 * @num: number of routes
 *
 * Mark existing routes matching those specified in the passed array
 * as being weak, meaning that they are ignored for the purpose of
 * power decisions.  The main intended use case is for sidetone paths
 * which couple audio between other independent paths if they are both
 * active in order to make the combination work better at the user
 * level but which aren't intended to be "used".
 *
 * Note that CODEC drivers should not use this as sidetone type paths
 * can frequently also be used as bypass paths.
 */
int snd_soc_dapm_weak_routes(struct snd_soc_dapm_context *dapm,
			     const struct snd_soc_dapm_route *route, int num)
{
	int i, err;
	int ret = 0;

	mutex_lock_nested(&dapm->card->dapm_mutex, SND_SOC_DAPM_CLASS_INIT);
	for (i = 0; i < num; i++) {
		err = snd_soc_dapm_weak_route(dapm, route);
		if (err)
			ret = err;
		route++;
	}
	mutex_unlock(&dapm->card->dapm_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_weak_routes);

/**
 * snd_soc_dapm_new_widgets - add new dapm widgets
 * @dapm: DAPM context
 *
 * Checks the codec for any new dapm widgets and creates them if found.
 *
 * Returns 0 for success.
 */
int snd_soc_dapm_new_widgets(struct snd_soc_card *card)
{
	struct snd_soc_dapm_widget *w;
	unsigned int val;

	mutex_lock_nested(&card->dapm_mutex, SND_SOC_DAPM_CLASS_INIT);

	list_for_each_entry(w, &card->widgets, list)
	{
		if (w->new)
			continue;

		if (w->num_kcontrols) {
			w->kcontrols = kzalloc(w->num_kcontrols *
						sizeof(struct snd_kcontrol *),
						GFP_KERNEL);
			if (!w->kcontrols) {
				mutex_unlock(&card->dapm_mutex);
				return -ENOMEM;
			}
		}

		switch(w->id) {
		case snd_soc_dapm_switch:
		case snd_soc_dapm_mixer:
		case snd_soc_dapm_mixer_named_ctl:
			dapm_new_mixer(w);
			break;
		case snd_soc_dapm_mux:
			dapm_new_mux(w);
			break;
		case snd_soc_dapm_pga:
		case snd_soc_dapm_out_drv:
			dapm_new_pga(w);
			break;
		default:
			break;
		}

		/* Read the initial power state from the device */
		if (w->reg >= 0) {
			soc_dapm_read(w->dapm, w->reg, &val);
			val = val >> w->shift;
			val &= w->mask;
			if (val == w->on_val)
				w->power = 1;
		}

		w->new = 1;

		dapm_mark_dirty(w, "new widget");
		dapm_debugfs_add_widget(w);
	}

	dapm_power_widgets(card, SND_SOC_DAPM_STREAM_NOP);
	mutex_unlock(&card->dapm_mutex);
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
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct snd_soc_card *card = dapm->card;
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int reg = mc->reg;
	unsigned int shift = mc->shift;
	int max = mc->max;
	unsigned int mask = (1 << fls(max)) - 1;
	unsigned int invert = mc->invert;
	unsigned int val;
	int ret = 0;

	if (snd_soc_volsw_is_stereo(mc))
		dev_warn(dapm->dev,
			 "ASoC: Control '%s' is stereo, which is not supported\n",
			 kcontrol->id.name);

	mutex_lock_nested(&card->dapm_mutex, SND_SOC_DAPM_CLASS_RUNTIME);
	if (dapm_kcontrol_is_powered(kcontrol) && reg != SND_SOC_NOPM) {
		ret = soc_dapm_read(dapm, reg, &val);
		val = (val >> shift) & mask;
	} else {
		val = dapm_kcontrol_get_value(kcontrol);
	}
	mutex_unlock(&card->dapm_mutex);

	if (invert)
		ucontrol->value.integer.value[0] = max - val;
	else
		ucontrol->value.integer.value[0] = val;

	return ret;
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
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct snd_soc_card *card = dapm->card;
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int reg = mc->reg;
	unsigned int shift = mc->shift;
	int max = mc->max;
	unsigned int mask = (1 << fls(max)) - 1;
	unsigned int invert = mc->invert;
	unsigned int val;
	int connect, change, reg_change = 0;
	struct snd_soc_dapm_update update;
	int ret = 0;

	if (snd_soc_volsw_is_stereo(mc))
		dev_warn(dapm->dev,
			 "ASoC: Control '%s' is stereo, which is not supported\n",
			 kcontrol->id.name);

	val = (ucontrol->value.integer.value[0] & mask);
	connect = !!val;

	if (invert)
		val = max - val;

	mutex_lock_nested(&card->dapm_mutex, SND_SOC_DAPM_CLASS_RUNTIME);

	change = dapm_kcontrol_set_value(kcontrol, val);

	if (reg != SND_SOC_NOPM) {
		mask = mask << shift;
		val = val << shift;

		reg_change = soc_dapm_test_bits(dapm, reg, mask, val);
	}

	if (change || reg_change) {
		if (reg_change) {
			update.kcontrol = kcontrol;
			update.reg = reg;
			update.mask = mask;
			update.val = val;
			card->update = &update;
		}
		change |= reg_change;

		ret = soc_dapm_mixer_update_power(card, kcontrol, connect);

		card->update = NULL;
	}

	mutex_unlock(&card->dapm_mutex);

	if (ret > 0)
		soc_dpcm_runtime_update(card);

	return change;
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
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int reg_val, val;
	int ret = 0;

	if (e->reg != SND_SOC_NOPM)
		ret = soc_dapm_read(dapm, e->reg, &reg_val);
	else
		reg_val = dapm_kcontrol_get_value(kcontrol);

	val = (reg_val >> e->shift_l) & e->mask;
	ucontrol->value.enumerated.item[0] = snd_soc_enum_val_to_item(e, val);
	if (e->shift_l != e->shift_r) {
		val = (reg_val >> e->shift_r) & e->mask;
		val = snd_soc_enum_val_to_item(e, val);
		ucontrol->value.enumerated.item[1] = val;
	}

	return ret;
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
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct snd_soc_card *card = dapm->card;
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int *item = ucontrol->value.enumerated.item;
	unsigned int val, change;
	unsigned int mask;
	struct snd_soc_dapm_update update;
	int ret = 0;

	if (item[0] >= e->items)
		return -EINVAL;

	val = snd_soc_enum_item_to_val(e, item[0]) << e->shift_l;
	mask = e->mask << e->shift_l;
	if (e->shift_l != e->shift_r) {
		if (item[1] > e->items)
			return -EINVAL;
		val |= snd_soc_enum_item_to_val(e, item[1]) << e->shift_l;
		mask |= e->mask << e->shift_r;
	}

	mutex_lock_nested(&card->dapm_mutex, SND_SOC_DAPM_CLASS_RUNTIME);

	if (e->reg != SND_SOC_NOPM)
		change = soc_dapm_test_bits(dapm, e->reg, mask, val);
	else
		change = dapm_kcontrol_set_value(kcontrol, val);

	if (change) {
		if (e->reg != SND_SOC_NOPM) {
			update.kcontrol = kcontrol;
			update.reg = e->reg;
			update.mask = mask;
			update.val = val;
			card->update = &update;
		}

		ret = soc_dapm_mux_update_power(card, kcontrol, item[0], e);

		card->update = NULL;
	}

	mutex_unlock(&card->dapm_mutex);

	if (ret > 0)
		soc_dpcm_runtime_update(card);

	return change;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_put_enum_double);

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
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);
	const char *pin = (const char *)kcontrol->private_value;

	mutex_lock_nested(&card->dapm_mutex, SND_SOC_DAPM_CLASS_RUNTIME);

	ucontrol->value.integer.value[0] =
		snd_soc_dapm_get_pin_status(&card->dapm, pin);

	mutex_unlock(&card->dapm_mutex);

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
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);
	const char *pin = (const char *)kcontrol->private_value;

	if (ucontrol->value.integer.value[0])
		snd_soc_dapm_enable_pin(&card->dapm, pin);
	else
		snd_soc_dapm_disable_pin(&card->dapm, pin);

	snd_soc_dapm_sync(&card->dapm);
	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_put_pin_switch);

static struct snd_soc_dapm_widget *
snd_soc_dapm_new_control(struct snd_soc_dapm_context *dapm,
			 const struct snd_soc_dapm_widget *widget)
{
	struct snd_soc_dapm_widget *w;
	const char *prefix;
	int ret;

	if ((w = dapm_cnew_widget(widget)) == NULL)
		return NULL;

	switch (w->id) {
	case snd_soc_dapm_regulator_supply:
		w->regulator = devm_regulator_get(dapm->dev, w->name);
		if (IS_ERR(w->regulator)) {
			ret = PTR_ERR(w->regulator);
			dev_err(dapm->dev, "ASoC: Failed to request %s: %d\n",
				w->name, ret);
			return NULL;
		}

		if (w->on_val & SND_SOC_DAPM_REGULATOR_BYPASS) {
			ret = regulator_allow_bypass(w->regulator, true);
			if (ret != 0)
				dev_warn(w->dapm->dev,
					 "ASoC: Failed to bypass %s: %d\n",
					 w->name, ret);
		}
		break;
	case snd_soc_dapm_clock_supply:
#ifdef CONFIG_CLKDEV_LOOKUP
		w->clk = devm_clk_get(dapm->dev, w->name);
		if (IS_ERR(w->clk)) {
			ret = PTR_ERR(w->clk);
			dev_err(dapm->dev, "ASoC: Failed to request %s: %d\n",
				w->name, ret);
			return NULL;
		}
#else
		return NULL;
#endif
		break;
	default:
		break;
	}

	prefix = soc_dapm_prefix(dapm);
	if (prefix)
		w->name = kasprintf(GFP_KERNEL, "%s %s", prefix, widget->name);
	else
		w->name = kasprintf(GFP_KERNEL, "%s", widget->name);

	if (w->name == NULL) {
		kfree(w);
		return NULL;
	}

	switch (w->id) {
	case snd_soc_dapm_switch:
	case snd_soc_dapm_mixer:
	case snd_soc_dapm_mixer_named_ctl:
		w->power_check = dapm_generic_check_power;
		break;
	case snd_soc_dapm_mux:
		w->power_check = dapm_generic_check_power;
		break;
	case snd_soc_dapm_dai_out:
		w->power_check = dapm_adc_check_power;
		break;
	case snd_soc_dapm_dai_in:
		w->power_check = dapm_dac_check_power;
		break;
	case snd_soc_dapm_adc:
	case snd_soc_dapm_aif_out:
	case snd_soc_dapm_dac:
	case snd_soc_dapm_aif_in:
	case snd_soc_dapm_pga:
	case snd_soc_dapm_out_drv:
	case snd_soc_dapm_input:
	case snd_soc_dapm_output:
	case snd_soc_dapm_micbias:
	case snd_soc_dapm_spk:
	case snd_soc_dapm_hp:
	case snd_soc_dapm_mic:
	case snd_soc_dapm_line:
	case snd_soc_dapm_dai_link:
		w->power_check = dapm_generic_check_power;
		break;
	case snd_soc_dapm_supply:
	case snd_soc_dapm_regulator_supply:
	case snd_soc_dapm_clock_supply:
	case snd_soc_dapm_kcontrol:
		w->power_check = dapm_supply_check_power;
		break;
	default:
		w->power_check = dapm_always_on_check_power;
		break;
	}

	w->dapm = dapm;
	if (dapm->component)
		w->codec = dapm->component->codec;
	INIT_LIST_HEAD(&w->sources);
	INIT_LIST_HEAD(&w->sinks);
	INIT_LIST_HEAD(&w->list);
	INIT_LIST_HEAD(&w->dirty);
	list_add(&w->list, &dapm->card->widgets);

	/* machine layer set ups unconnected pins and insertions */
	w->connected = 1;
	return w;
}

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
	struct snd_soc_dapm_widget *w;
	int i;
	int ret = 0;

	mutex_lock_nested(&dapm->card->dapm_mutex, SND_SOC_DAPM_CLASS_INIT);
	for (i = 0; i < num; i++) {
		w = snd_soc_dapm_new_control(dapm, widget);
		if (!w) {
			dev_err(dapm->dev,
				"ASoC: Failed to create DAPM control %s\n",
				widget->name);
			ret = -ENOMEM;
			break;
		}
		widget++;
	}
	mutex_unlock(&dapm->card->dapm_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_new_controls);

static int snd_soc_dai_link_event(struct snd_soc_dapm_widget *w,
				  struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_dapm_path *source_p, *sink_p;
	struct snd_soc_dai *source, *sink;
	const struct snd_soc_pcm_stream *config = w->params;
	struct snd_pcm_substream substream;
	struct snd_pcm_hw_params *params = NULL;
	u64 fmt;
	int ret;

	if (WARN_ON(!config) ||
	    WARN_ON(list_empty(&w->sources) || list_empty(&w->sinks)))
		return -EINVAL;

	/* We only support a single source and sink, pick the first */
	source_p = list_first_entry(&w->sources, struct snd_soc_dapm_path,
				    list_sink);
	sink_p = list_first_entry(&w->sinks, struct snd_soc_dapm_path,
				  list_source);

	if (WARN_ON(!source_p || !sink_p) ||
	    WARN_ON(!sink_p->source || !source_p->sink) ||
	    WARN_ON(!source_p->source || !sink_p->sink))
		return -EINVAL;

	source = source_p->source->priv;
	sink = sink_p->sink->priv;

	/* Be a little careful as we don't want to overflow the mask array */
	if (config->formats) {
		fmt = ffs(config->formats) - 1;
	} else {
		dev_warn(w->dapm->dev, "ASoC: Invalid format %llx specified\n",
			 config->formats);
		fmt = 0;
	}

	/* Currently very limited parameter selection */
	params = kzalloc(sizeof(*params), GFP_KERNEL);
	if (!params) {
		ret = -ENOMEM;
		goto out;
	}
	snd_mask_set(hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT), fmt);

	hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE)->min =
		config->rate_min;
	hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE)->max =
		config->rate_max;

	hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS)->min
		= config->channels_min;
	hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS)->max
		= config->channels_max;

	memset(&substream, 0, sizeof(substream));

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		substream.stream = SNDRV_PCM_STREAM_CAPTURE;
		ret = soc_dai_hw_params(&substream, params, source);
		if (ret < 0)
			goto out;

		substream.stream = SNDRV_PCM_STREAM_PLAYBACK;
		ret = soc_dai_hw_params(&substream, params, sink);
		if (ret < 0)
			goto out;
		break;

	case SND_SOC_DAPM_POST_PMU:
		ret = snd_soc_dai_digital_mute(sink, 0,
					       SNDRV_PCM_STREAM_PLAYBACK);
		if (ret != 0 && ret != -ENOTSUPP)
			dev_warn(sink->dev, "ASoC: Failed to unmute: %d\n", ret);
		ret = 0;
		break;

	case SND_SOC_DAPM_PRE_PMD:
		ret = snd_soc_dai_digital_mute(sink, 1,
					       SNDRV_PCM_STREAM_PLAYBACK);
		if (ret != 0 && ret != -ENOTSUPP)
			dev_warn(sink->dev, "ASoC: Failed to mute: %d\n", ret);
		ret = 0;
		break;

	default:
		WARN(1, "Unknown event %d\n", event);
		return -EINVAL;
	}

out:
	kfree(params);
	return ret;
}

int snd_soc_dapm_new_pcm(struct snd_soc_card *card,
			 const struct snd_soc_pcm_stream *params,
			 struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_dapm_widget template;
	struct snd_soc_dapm_widget *w;
	size_t len;
	char *link_name;
	int ret;

	len = strlen(source->name) + strlen(sink->name) + 2;
	link_name = devm_kzalloc(card->dev, len, GFP_KERNEL);
	if (!link_name)
		return -ENOMEM;
	snprintf(link_name, len, "%s-%s", source->name, sink->name);

	memset(&template, 0, sizeof(template));
	template.reg = SND_SOC_NOPM;
	template.id = snd_soc_dapm_dai_link;
	template.name = link_name;
	template.event = snd_soc_dai_link_event;
	template.event_flags = SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
		SND_SOC_DAPM_PRE_PMD;

	dev_dbg(card->dev, "ASoC: adding %s widget\n", link_name);

	w = snd_soc_dapm_new_control(&card->dapm, &template);
	if (!w) {
		dev_err(card->dev, "ASoC: Failed to create %s widget\n",
			link_name);
		return -ENOMEM;
	}

	w->params = params;

	ret = snd_soc_dapm_add_path(&card->dapm, source, w, NULL, NULL);
	if (ret)
		return ret;
	return snd_soc_dapm_add_path(&card->dapm, w, sink, NULL, NULL);
}

int snd_soc_dapm_new_dai_widgets(struct snd_soc_dapm_context *dapm,
				 struct snd_soc_dai *dai)
{
	struct snd_soc_dapm_widget template;
	struct snd_soc_dapm_widget *w;

	WARN_ON(dapm->dev != dai->dev);

	memset(&template, 0, sizeof(template));
	template.reg = SND_SOC_NOPM;

	if (dai->driver->playback.stream_name) {
		template.id = snd_soc_dapm_dai_in;
		template.name = dai->driver->playback.stream_name;
		template.sname = dai->driver->playback.stream_name;

		dev_dbg(dai->dev, "ASoC: adding %s widget\n",
			template.name);

		w = snd_soc_dapm_new_control(dapm, &template);
		if (!w) {
			dev_err(dapm->dev, "ASoC: Failed to create %s widget\n",
				dai->driver->playback.stream_name);
			return -ENOMEM;
		}

		w->priv = dai;
		dai->playback_widget = w;
	}

	if (dai->driver->capture.stream_name) {
		template.id = snd_soc_dapm_dai_out;
		template.name = dai->driver->capture.stream_name;
		template.sname = dai->driver->capture.stream_name;

		dev_dbg(dai->dev, "ASoC: adding %s widget\n",
			template.name);

		w = snd_soc_dapm_new_control(dapm, &template);
		if (!w) {
			dev_err(dapm->dev, "ASoC: Failed to create %s widget\n",
				dai->driver->capture.stream_name);
			return -ENOMEM;
		}

		w->priv = dai;
		dai->capture_widget = w;
	}

	return 0;
}

int snd_soc_dapm_link_dai_widgets(struct snd_soc_card *card)
{
	struct snd_soc_dapm_widget *dai_w, *w;
	struct snd_soc_dapm_widget *src, *sink;
	struct snd_soc_dai *dai;

	/* For each DAI widget... */
	list_for_each_entry(dai_w, &card->widgets, list) {
		switch (dai_w->id) {
		case snd_soc_dapm_dai_in:
		case snd_soc_dapm_dai_out:
			break;
		default:
			continue;
		}

		dai = dai_w->priv;

		/* ...find all widgets with the same stream and link them */
		list_for_each_entry(w, &card->widgets, list) {
			if (w->dapm != dai_w->dapm)
				continue;

			switch (w->id) {
			case snd_soc_dapm_dai_in:
			case snd_soc_dapm_dai_out:
				continue;
			default:
				break;
			}

			if (!w->sname || !strstr(w->sname, dai_w->name))
				continue;

			if (dai_w->id == snd_soc_dapm_dai_in) {
				src = dai_w;
				sink = w;
			} else {
				src = w;
				sink = dai_w;
			}
			dev_dbg(dai->dev, "%s -> %s\n", src->name, sink->name);
			snd_soc_dapm_add_path(w->dapm, src, sink, NULL, NULL);
		}
	}

	return 0;
}

static void dapm_connect_dai_link_widgets(struct snd_soc_card *card,
					  struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dapm_widget *sink, *source;
	int i;

	for (i = 0; i < rtd->num_codecs; i++) {
		struct snd_soc_dai *codec_dai = rtd->codec_dais[i];

		/* there is no point in connecting BE DAI links with dummies */
		if (snd_soc_dai_is_dummy(codec_dai) ||
			snd_soc_dai_is_dummy(cpu_dai))
			continue;

		/* connect BE DAI playback if widgets are valid */
		if (codec_dai->playback_widget && cpu_dai->playback_widget) {
			source = cpu_dai->playback_widget;
			sink = codec_dai->playback_widget;
			dev_dbg(rtd->dev, "connected DAI link %s:%s -> %s:%s\n",
				cpu_dai->component->name, source->name,
				codec_dai->component->name, sink->name);

			snd_soc_dapm_add_path(&card->dapm, source, sink,
				NULL, NULL);
		}

		/* connect BE DAI capture if widgets are valid */
		if (codec_dai->capture_widget && cpu_dai->capture_widget) {
			source = codec_dai->capture_widget;
			sink = cpu_dai->capture_widget;
			dev_dbg(rtd->dev, "connected DAI link %s:%s -> %s:%s\n",
				codec_dai->component->name, source->name,
				cpu_dai->component->name, sink->name);

			snd_soc_dapm_add_path(&card->dapm, source, sink,
				NULL, NULL);
		}
	}
}

static void soc_dapm_dai_stream_event(struct snd_soc_dai *dai, int stream,
	int event)
{
	struct snd_soc_dapm_widget *w;

	if (stream == SNDRV_PCM_STREAM_PLAYBACK)
		w = dai->playback_widget;
	else
		w = dai->capture_widget;

	if (w) {
		dapm_mark_dirty(w, "stream event");

		switch (event) {
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

void snd_soc_dapm_connect_dai_link_widgets(struct snd_soc_card *card)
{
	struct snd_soc_pcm_runtime *rtd = card->rtd;
	int i;

	/* for each BE DAI link... */
	for (i = 0; i < card->num_rtd; i++) {
		rtd = &card->rtd[i];

		/*
		 * dynamic FE links have no fixed DAI mapping.
		 * CODEC<->CODEC links have no direct connection.
		 */
		if (rtd->dai_link->dynamic || rtd->dai_link->params)
			continue;

		dapm_connect_dai_link_widgets(card, rtd);
	}
}

static void soc_dapm_stream_event(struct snd_soc_pcm_runtime *rtd, int stream,
	int event)
{
	int i;

	soc_dapm_dai_stream_event(rtd->cpu_dai, stream, event);
	for (i = 0; i < rtd->num_codecs; i++)
		soc_dapm_dai_stream_event(rtd->codec_dais[i], stream, event);

	dapm_power_widgets(rtd->card, event);
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
void snd_soc_dapm_stream_event(struct snd_soc_pcm_runtime *rtd, int stream,
			      int event)
{
	struct snd_soc_card *card = rtd->card;

	mutex_lock_nested(&card->dapm_mutex, SND_SOC_DAPM_CLASS_RUNTIME);
	soc_dapm_stream_event(rtd, stream, event);
	mutex_unlock(&card->dapm_mutex);
}

/**
 * snd_soc_dapm_enable_pin_unlocked - enable pin.
 * @dapm: DAPM context
 * @pin: pin name
 *
 * Enables input/output pin and its parents or children widgets iff there is
 * a valid audio route and active audio stream.
 *
 * Requires external locking.
 *
 * NOTE: snd_soc_dapm_sync() needs to be called after this for DAPM to
 * do any widget power switching.
 */
int snd_soc_dapm_enable_pin_unlocked(struct snd_soc_dapm_context *dapm,
				   const char *pin)
{
	return snd_soc_dapm_set_pin(dapm, pin, 1);
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_enable_pin_unlocked);

/**
 * snd_soc_dapm_enable_pin - enable pin.
 * @dapm: DAPM context
 * @pin: pin name
 *
 * Enables input/output pin and its parents or children widgets iff there is
 * a valid audio route and active audio stream.
 *
 * NOTE: snd_soc_dapm_sync() needs to be called after this for DAPM to
 * do any widget power switching.
 */
int snd_soc_dapm_enable_pin(struct snd_soc_dapm_context *dapm, const char *pin)
{
	int ret;

	mutex_lock_nested(&dapm->card->dapm_mutex, SND_SOC_DAPM_CLASS_RUNTIME);

	ret = snd_soc_dapm_set_pin(dapm, pin, 1);

	mutex_unlock(&dapm->card->dapm_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_enable_pin);

/**
 * snd_soc_dapm_force_enable_pin_unlocked - force a pin to be enabled
 * @dapm: DAPM context
 * @pin: pin name
 *
 * Enables input/output pin regardless of any other state.  This is
 * intended for use with microphone bias supplies used in microphone
 * jack detection.
 *
 * Requires external locking.
 *
 * NOTE: snd_soc_dapm_sync() needs to be called after this for DAPM to
 * do any widget power switching.
 */
int snd_soc_dapm_force_enable_pin_unlocked(struct snd_soc_dapm_context *dapm,
					 const char *pin)
{
	struct snd_soc_dapm_widget *w = dapm_find_widget(dapm, pin, true);

	if (!w) {
		dev_err(dapm->dev, "ASoC: unknown pin %s\n", pin);
		return -EINVAL;
	}

	dev_dbg(w->dapm->dev, "ASoC: force enable pin %s\n", pin);
	w->connected = 1;
	w->force = 1;
	dapm_mark_dirty(w, "force enable");

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_force_enable_pin_unlocked);

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
	int ret;

	mutex_lock_nested(&dapm->card->dapm_mutex, SND_SOC_DAPM_CLASS_RUNTIME);

	ret = snd_soc_dapm_force_enable_pin_unlocked(dapm, pin);

	mutex_unlock(&dapm->card->dapm_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_force_enable_pin);

/**
 * snd_soc_dapm_disable_pin_unlocked - disable pin.
 * @dapm: DAPM context
 * @pin: pin name
 *
 * Disables input/output pin and its parents or children widgets.
 *
 * Requires external locking.
 *
 * NOTE: snd_soc_dapm_sync() needs to be called after this for DAPM to
 * do any widget power switching.
 */
int snd_soc_dapm_disable_pin_unlocked(struct snd_soc_dapm_context *dapm,
				    const char *pin)
{
	return snd_soc_dapm_set_pin(dapm, pin, 0);
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_disable_pin_unlocked);

/**
 * snd_soc_dapm_disable_pin - disable pin.
 * @dapm: DAPM context
 * @pin: pin name
 *
 * Disables input/output pin and its parents or children widgets.
 *
 * NOTE: snd_soc_dapm_sync() needs to be called after this for DAPM to
 * do any widget power switching.
 */
int snd_soc_dapm_disable_pin(struct snd_soc_dapm_context *dapm,
			     const char *pin)
{
	int ret;

	mutex_lock_nested(&dapm->card->dapm_mutex, SND_SOC_DAPM_CLASS_RUNTIME);

	ret = snd_soc_dapm_set_pin(dapm, pin, 0);

	mutex_unlock(&dapm->card->dapm_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_disable_pin);

/**
 * snd_soc_dapm_nc_pin_unlocked - permanently disable pin.
 * @dapm: DAPM context
 * @pin: pin name
 *
 * Marks the specified pin as being not connected, disabling it along
 * any parent or child widgets.  At present this is identical to
 * snd_soc_dapm_disable_pin() but in future it will be extended to do
 * additional things such as disabling controls which only affect
 * paths through the pin.
 *
 * Requires external locking.
 *
 * NOTE: snd_soc_dapm_sync() needs to be called after this for DAPM to
 * do any widget power switching.
 */
int snd_soc_dapm_nc_pin_unlocked(struct snd_soc_dapm_context *dapm,
			       const char *pin)
{
	return snd_soc_dapm_set_pin(dapm, pin, 0);
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_nc_pin_unlocked);

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
	int ret;

	mutex_lock_nested(&dapm->card->dapm_mutex, SND_SOC_DAPM_CLASS_RUNTIME);

	ret = snd_soc_dapm_set_pin(dapm, pin, 0);

	mutex_unlock(&dapm->card->dapm_mutex);

	return ret;
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
	struct snd_soc_dapm_widget *w = dapm_find_widget(dapm, pin, true);

	if (w)
		return w->connected;

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
	struct snd_soc_dapm_widget *w = dapm_find_widget(dapm, pin, false);

	if (!w) {
		dev_err(dapm->dev, "ASoC: unknown pin %s\n", pin);
		return -EINVAL;
	}

	w->ignore_suspend = 1;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_ignore_suspend);

static bool snd_soc_dapm_widget_in_card_paths(struct snd_soc_card *card,
					      struct snd_soc_dapm_widget *w)
{
	struct snd_soc_dapm_path *p;

	list_for_each_entry(p, &card->paths, list) {
		if ((p->source == w) || (p->sink == w)) {
			dev_dbg(card->dev,
			    "... Path %s(id:%d dapm:%p) - %s(id:%d dapm:%p)\n",
			    p->source->name, p->source->id, p->source->dapm,
			    p->sink->name, p->sink->id, p->sink->dapm);

			/* Connected to something other than the codec */
			if (p->source->dapm != p->sink->dapm)
				return true;
			/*
			 * Loopback connection from codec external pin to
			 * codec external pin
			 */
			if (p->sink->id == snd_soc_dapm_input) {
				switch (p->source->id) {
				case snd_soc_dapm_output:
				case snd_soc_dapm_micbias:
					return true;
				default:
					break;
				}
			}
		}
	}

	return false;
}

/**
 * snd_soc_dapm_auto_nc_pins - call snd_soc_dapm_nc_pin for unused pins
 * @card: The card whose pins should be processed
 *
 * Automatically call snd_soc_dapm_nc_pin() for any external pins in the card
 * which are unused. Pins are used if they are connected externally to a
 * component, whether that be to some other device, or a loop-back connection to
 * the component itself.
 */
void snd_soc_dapm_auto_nc_pins(struct snd_soc_card *card)
{
	struct snd_soc_dapm_widget *w;

	dev_dbg(card->dev, "ASoC: Auto NC: DAPMs: card:%p\n", &card->dapm);

	list_for_each_entry(w, &card->widgets, list) {
		switch (w->id) {
		case snd_soc_dapm_input:
		case snd_soc_dapm_output:
		case snd_soc_dapm_micbias:
			dev_dbg(card->dev, "ASoC: Auto NC: Checking widget %s\n",
				w->name);
			if (!snd_soc_dapm_widget_in_card_paths(card, w)) {
				dev_dbg(card->dev,
					"... Not in map; disabling\n");
				snd_soc_dapm_nc_pin(w->dapm, w->name);
			}
			break;
		default:
			break;
		}
	}
}

/**
 * snd_soc_dapm_free - free dapm resources
 * @dapm: DAPM context
 *
 * Free all dapm widgets and resources.
 */
void snd_soc_dapm_free(struct snd_soc_dapm_context *dapm)
{
	snd_soc_dapm_sys_remove(dapm->dev);
	dapm_debugfs_cleanup(dapm);
	dapm_free_widgets(dapm);
	list_del(&dapm->list);
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_free);

static void soc_dapm_shutdown_dapm(struct snd_soc_dapm_context *dapm)
{
	struct snd_soc_card *card = dapm->card;
	struct snd_soc_dapm_widget *w;
	LIST_HEAD(down_list);
	int powerdown = 0;

	mutex_lock(&card->dapm_mutex);

	list_for_each_entry(w, &dapm->card->widgets, list) {
		if (w->dapm != dapm)
			continue;
		if (w->power) {
			dapm_seq_insert(w, &down_list, false);
			w->power = 0;
			powerdown = 1;
		}
	}

	/* If there were no widgets to power down we're already in
	 * standby.
	 */
	if (powerdown) {
		if (dapm->bias_level == SND_SOC_BIAS_ON)
			snd_soc_dapm_set_bias_level(dapm,
						    SND_SOC_BIAS_PREPARE);
		dapm_seq_run(card, &down_list, 0, false);
		if (dapm->bias_level == SND_SOC_BIAS_PREPARE)
			snd_soc_dapm_set_bias_level(dapm,
						    SND_SOC_BIAS_STANDBY);
	}

	mutex_unlock(&card->dapm_mutex);
}

/*
 * snd_soc_dapm_shutdown - callback for system shutdown
 */
void snd_soc_dapm_shutdown(struct snd_soc_card *card)
{
	struct snd_soc_dapm_context *dapm;

	list_for_each_entry(dapm, &card->dapm_list, list) {
		if (dapm != &card->dapm) {
			soc_dapm_shutdown_dapm(dapm);
			if (dapm->bias_level == SND_SOC_BIAS_STANDBY)
				snd_soc_dapm_set_bias_level(dapm,
							    SND_SOC_BIAS_OFF);
		}
	}

	soc_dapm_shutdown_dapm(&card->dapm);
	if (card->dapm.bias_level == SND_SOC_BIAS_STANDBY)
		snd_soc_dapm_set_bias_level(&card->dapm,
					    SND_SOC_BIAS_OFF);
}

/* Module information */
MODULE_AUTHOR("Liam Girdwood, lrg@slimlogic.co.uk");
MODULE_DESCRIPTION("Dynamic Audio Power Management core for ALSA SoC");
MODULE_LICENSE("GPL");
