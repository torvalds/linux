// SPDX-License-Identifier: GPL-2.0+
//
// soc-dapm.c  --  ALSA SoC Dynamic Audio Power Management
//
// Copyright 2005 Wolfson Microelectronics PLC.
// Author: Liam Girdwood <lrg@slimlogic.co.uk>
//
//  Features:
//    o Changes power status of internal codec blocks depending on the
//      dynamic configuration of codec internal audio paths and active
//      DACs/ADCs.
//    o Platform power domain - can support external components i.e. amps and
//      mic/headphone insertion events.
//    o Automatic Mic Bias support
//    o Jack insertion power event initiation - e.g. hp insertion will enable
//      sinks, dacs, etc
//    o Delayed power down of audio subsystem to reduce pops between a quick
//      device reopen.

#include <linux/module.h>
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
#include <linux/pinctrl/consumer.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>

#include <trace/events/asoc.h>

#define DAPM_UPDATE_STAT(widget, val) widget->dapm->card->dapm_stats.val++;

#define SND_SOC_DAPM_DIR_REVERSE(x) ((x == SND_SOC_DAPM_DIR_IN) ? \
	SND_SOC_DAPM_DIR_OUT : SND_SOC_DAPM_DIR_IN)

#define snd_soc_dapm_for_each_direction(dir) \
	for ((dir) = SND_SOC_DAPM_DIR_IN; (dir) <= SND_SOC_DAPM_DIR_OUT; \
		(dir)++)

static int snd_soc_dapm_add_path(struct snd_soc_dapm_context *dapm,
	struct snd_soc_dapm_widget *wsource, struct snd_soc_dapm_widget *wsink,
	const char *control,
	int (*connected)(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink));

struct snd_soc_dapm_widget *
snd_soc_dapm_new_control(struct snd_soc_dapm_context *dapm,
			 const struct snd_soc_dapm_widget *widget);

struct snd_soc_dapm_widget *
snd_soc_dapm_new_control_unlocked(struct snd_soc_dapm_context *dapm,
			 const struct snd_soc_dapm_widget *widget);

static unsigned int soc_dapm_read(struct snd_soc_dapm_context *dapm, int reg);

/* dapm power sequences - make this per codec in the future */
static int dapm_up_seq[] = {
	[snd_soc_dapm_pre] = 1,
	[snd_soc_dapm_regulator_supply] = 2,
	[snd_soc_dapm_pinctrl] = 2,
	[snd_soc_dapm_clock_supply] = 2,
	[snd_soc_dapm_supply] = 3,
	[snd_soc_dapm_dai_link] = 3,
	[snd_soc_dapm_micbias] = 4,
	[snd_soc_dapm_vmid] = 4,
	[snd_soc_dapm_dai_in] = 5,
	[snd_soc_dapm_dai_out] = 5,
	[snd_soc_dapm_aif_in] = 5,
	[snd_soc_dapm_aif_out] = 5,
	[snd_soc_dapm_mic] = 6,
	[snd_soc_dapm_siggen] = 6,
	[snd_soc_dapm_input] = 6,
	[snd_soc_dapm_output] = 6,
	[snd_soc_dapm_mux] = 7,
	[snd_soc_dapm_demux] = 7,
	[snd_soc_dapm_dac] = 8,
	[snd_soc_dapm_switch] = 9,
	[snd_soc_dapm_mixer] = 9,
	[snd_soc_dapm_mixer_named_ctl] = 9,
	[snd_soc_dapm_pga] = 10,
	[snd_soc_dapm_buffer] = 10,
	[snd_soc_dapm_scheduler] = 10,
	[snd_soc_dapm_effect] = 10,
	[snd_soc_dapm_src] = 10,
	[snd_soc_dapm_asrc] = 10,
	[snd_soc_dapm_encoder] = 10,
	[snd_soc_dapm_decoder] = 10,
	[snd_soc_dapm_adc] = 11,
	[snd_soc_dapm_out_drv] = 12,
	[snd_soc_dapm_hp] = 12,
	[snd_soc_dapm_line] = 12,
	[snd_soc_dapm_sink] = 12,
	[snd_soc_dapm_spk] = 13,
	[snd_soc_dapm_kcontrol] = 14,
	[snd_soc_dapm_post] = 15,
};

static int dapm_down_seq[] = {
	[snd_soc_dapm_pre] = 1,
	[snd_soc_dapm_kcontrol] = 2,
	[snd_soc_dapm_adc] = 3,
	[snd_soc_dapm_spk] = 4,
	[snd_soc_dapm_hp] = 5,
	[snd_soc_dapm_line] = 5,
	[snd_soc_dapm_out_drv] = 5,
	[snd_soc_dapm_sink] = 6,
	[snd_soc_dapm_pga] = 6,
	[snd_soc_dapm_buffer] = 6,
	[snd_soc_dapm_scheduler] = 6,
	[snd_soc_dapm_effect] = 6,
	[snd_soc_dapm_src] = 6,
	[snd_soc_dapm_asrc] = 6,
	[snd_soc_dapm_encoder] = 6,
	[snd_soc_dapm_decoder] = 6,
	[snd_soc_dapm_switch] = 7,
	[snd_soc_dapm_mixer_named_ctl] = 7,
	[snd_soc_dapm_mixer] = 7,
	[snd_soc_dapm_dac] = 8,
	[snd_soc_dapm_mic] = 9,
	[snd_soc_dapm_siggen] = 9,
	[snd_soc_dapm_input] = 9,
	[snd_soc_dapm_output] = 9,
	[snd_soc_dapm_micbias] = 10,
	[snd_soc_dapm_vmid] = 10,
	[snd_soc_dapm_mux] = 11,
	[snd_soc_dapm_demux] = 11,
	[snd_soc_dapm_aif_in] = 12,
	[snd_soc_dapm_aif_out] = 12,
	[snd_soc_dapm_dai_in] = 12,
	[snd_soc_dapm_dai_out] = 12,
	[snd_soc_dapm_dai_link] = 13,
	[snd_soc_dapm_supply] = 14,
	[snd_soc_dapm_clock_supply] = 15,
	[snd_soc_dapm_pinctrl] = 15,
	[snd_soc_dapm_regulator_supply] = 15,
	[snd_soc_dapm_post] = 16,
};

static void dapm_assert_locked(struct snd_soc_dapm_context *dapm)
{
	if (snd_soc_card_is_instantiated(dapm->card))
		snd_soc_dapm_mutex_assert_held(dapm);
}

static void pop_wait(u32 pop_time)
{
	if (pop_time)
		schedule_timeout_uninterruptible(msecs_to_jiffies(pop_time));
}

__printf(3, 4)
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

/*
 * Common implementation for dapm_widget_invalidate_input_paths() and
 * dapm_widget_invalidate_output_paths(). The function is inlined since the
 * combined size of the two specialized functions is only marginally larger then
 * the size of the generic function and at the same time the fast path of the
 * specialized functions is significantly smaller than the generic function.
 */
static __always_inline void dapm_widget_invalidate_paths(
	struct snd_soc_dapm_widget *w, enum snd_soc_dapm_direction dir)
{
	enum snd_soc_dapm_direction rdir = SND_SOC_DAPM_DIR_REVERSE(dir);
	struct snd_soc_dapm_widget *node;
	struct snd_soc_dapm_path *p;
	LIST_HEAD(list);

	dapm_assert_locked(w->dapm);

	if (w->endpoints[dir] == -1)
		return;

	list_add_tail(&w->work_list, &list);
	w->endpoints[dir] = -1;

	list_for_each_entry(w, &list, work_list) {
		snd_soc_dapm_widget_for_each_path(w, dir, p) {
			if (p->is_supply || p->weak || !p->connect)
				continue;
			node = p->node[rdir];
			if (node->endpoints[dir] != -1) {
				node->endpoints[dir] = -1;
				list_add_tail(&node->work_list, &list);
			}
		}
	}
}

/*
 * dapm_widget_invalidate_input_paths() - Invalidate the cached number of
 *  input paths
 * @w: The widget for which to invalidate the cached number of input paths
 *
 * Resets the cached number of inputs for the specified widget and all widgets
 * that can be reached via outcoming paths from the widget.
 *
 * This function must be called if the number of output paths for a widget might
 * have changed. E.g. if the source state of a widget changes or a path is added
 * or activated with the widget as the sink.
 */
static void dapm_widget_invalidate_input_paths(struct snd_soc_dapm_widget *w)
{
	dapm_widget_invalidate_paths(w, SND_SOC_DAPM_DIR_IN);
}

/*
 * dapm_widget_invalidate_output_paths() - Invalidate the cached number of
 *  output paths
 * @w: The widget for which to invalidate the cached number of output paths
 *
 * Resets the cached number of outputs for the specified widget and all widgets
 * that can be reached via incoming paths from the widget.
 *
 * This function must be called if the number of output paths for a widget might
 * have changed. E.g. if the sink state of a widget changes or a path is added
 * or activated with the widget as the source.
 */
static void dapm_widget_invalidate_output_paths(struct snd_soc_dapm_widget *w)
{
	dapm_widget_invalidate_paths(w, SND_SOC_DAPM_DIR_OUT);
}

/*
 * dapm_path_invalidate() - Invalidates the cached number of inputs and outputs
 *  for the widgets connected to a path
 * @p: The path to invalidate
 *
 * Resets the cached number of inputs for the sink of the path and the cached
 * number of outputs for the source of the path.
 *
 * This function must be called when a path is added, removed or the connected
 * state changes.
 */
static void dapm_path_invalidate(struct snd_soc_dapm_path *p)
{
	/*
	 * Weak paths or supply paths do not influence the number of input or
	 * output paths of their neighbors.
	 */
	if (p->weak || p->is_supply)
		return;

	/*
	 * The number of connected endpoints is the sum of the number of
	 * connected endpoints of all neighbors. If a node with 0 connected
	 * endpoints is either connected or disconnected that sum won't change,
	 * so there is no need to re-check the path.
	 */
	if (p->source->endpoints[SND_SOC_DAPM_DIR_IN] != 0)
		dapm_widget_invalidate_input_paths(p->sink);
	if (p->sink->endpoints[SND_SOC_DAPM_DIR_OUT] != 0)
		dapm_widget_invalidate_output_paths(p->source);
}

void dapm_mark_endpoints_dirty(struct snd_soc_card *card)
{
	struct snd_soc_dapm_widget *w;

	snd_soc_dapm_mutex_lock_root(card);

	for_each_card_widgets(card, w) {
		if (w->is_ep) {
			dapm_mark_dirty(w, "Rechecking endpoints");
			if (w->is_ep & SND_SOC_DAPM_EP_SINK)
				dapm_widget_invalidate_output_paths(w);
			if (w->is_ep & SND_SOC_DAPM_EP_SOURCE)
				dapm_widget_invalidate_input_paths(w);
		}
	}

	snd_soc_dapm_mutex_unlock(card);
}
EXPORT_SYMBOL_GPL(dapm_mark_endpoints_dirty);

/* create a new dapm widget */
static inline struct snd_soc_dapm_widget *dapm_cnew_widget(
	const struct snd_soc_dapm_widget *_widget)
{
	struct snd_soc_dapm_widget *w;

	w = kmemdup(_widget, sizeof(*_widget), GFP_KERNEL);
	if (!w)
		return NULL;

	/*
	 * w->name is duplicated in caller, but w->sname isn't.
	 * Duplicate it here if defined
	 */
	if (_widget->sname) {
		w->sname = kstrdup_const(_widget->sname, GFP_KERNEL);
		if (!w->sname) {
			kfree(w);
			return NULL;
		}
	}
	return w;
}

struct dapm_kcontrol_data {
	unsigned int value;
	struct snd_soc_dapm_widget *widget;
	struct list_head paths;
	struct snd_soc_dapm_widget_list *wlist;
};

static int dapm_kcontrol_data_alloc(struct snd_soc_dapm_widget *widget,
	struct snd_kcontrol *kcontrol, const char *ctrl_name)
{
	struct dapm_kcontrol_data *data;
	struct soc_mixer_control *mc;
	struct soc_enum *e;
	const char *name;
	int ret;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	INIT_LIST_HEAD(&data->paths);

	switch (widget->id) {
	case snd_soc_dapm_switch:
	case snd_soc_dapm_mixer:
	case snd_soc_dapm_mixer_named_ctl:
		mc = (struct soc_mixer_control *)kcontrol->private_value;

		if (mc->autodisable) {
			struct snd_soc_dapm_widget template;

			if (snd_soc_volsw_is_stereo(mc))
				dev_warn(widget->dapm->dev,
					 "ASoC: Unsupported stereo autodisable control '%s'\n",
					 ctrl_name);

			name = kasprintf(GFP_KERNEL, "%s %s", ctrl_name,
					 "Autodisable");
			if (!name) {
				ret = -ENOMEM;
				goto err_data;
			}

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
			template.name = name;

			data->value = template.on_val;

			data->widget =
				snd_soc_dapm_new_control_unlocked(widget->dapm,
				&template);
			kfree(name);
			if (IS_ERR(data->widget)) {
				ret = PTR_ERR(data->widget);
				goto err_data;
			}
		}
		break;
	case snd_soc_dapm_demux:
	case snd_soc_dapm_mux:
		e = (struct soc_enum *)kcontrol->private_value;

		if (e->autodisable) {
			struct snd_soc_dapm_widget template;

			name = kasprintf(GFP_KERNEL, "%s %s", ctrl_name,
					 "Autodisable");
			if (!name) {
				ret = -ENOMEM;
				goto err_data;
			}

			memset(&template, 0, sizeof(template));
			template.reg = e->reg;
			template.mask = e->mask;
			template.shift = e->shift_l;
			template.off_val = snd_soc_enum_item_to_val(e, 0);
			template.on_val = template.off_val;
			template.id = snd_soc_dapm_kcontrol;
			template.name = name;

			data->value = template.on_val;

			data->widget = snd_soc_dapm_new_control_unlocked(
						widget->dapm, &template);
			kfree(name);
			if (IS_ERR(data->widget)) {
				ret = PTR_ERR(data->widget);
				goto err_data;
			}

			snd_soc_dapm_add_path(widget->dapm, data->widget,
					      widget, NULL, NULL);
		} else if (e->reg != SND_SOC_NOPM) {
			data->value = soc_dapm_read(widget->dapm, e->reg) &
				      (e->mask << e->shift_l);
		}
		break;
	default:
		break;
	}

	kcontrol->private_data = data;

	return 0;

err_data:
	kfree(data);
	return ret;
}

static void dapm_kcontrol_free(struct snd_kcontrol *kctl)
{
	struct dapm_kcontrol_data *data = snd_kcontrol_chip(kctl);

	list_del(&data->paths);
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
			     struct_size(new_wlist, widgets, n),
			     GFP_KERNEL);
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

unsigned int dapm_kcontrol_get_value(const struct snd_kcontrol *kcontrol)
{
	struct dapm_kcontrol_data *data = snd_kcontrol_chip(kcontrol);

	return data->value;
}
EXPORT_SYMBOL_GPL(dapm_kcontrol_get_value);

static bool dapm_kcontrol_set_value(const struct snd_kcontrol *kcontrol,
	unsigned int value)
{
	struct dapm_kcontrol_data *data = snd_kcontrol_chip(kcontrol);

	if (data->value == value)
		return false;

	if (data->widget) {
		switch (dapm_kcontrol_get_wlist(kcontrol)->widgets[0]->id) {
		case snd_soc_dapm_switch:
		case snd_soc_dapm_mixer:
		case snd_soc_dapm_mixer_named_ctl:
			data->widget->on_val = value & data->widget->mask;
			break;
		case snd_soc_dapm_demux:
		case snd_soc_dapm_mux:
			data->widget->on_val = value >> data->widget->shift;
			break;
		default:
			data->widget->on_val = value;
			break;
		}
	}

	data->value = value;

	return true;
}

/**
 * snd_soc_dapm_kcontrol_widget() - Returns the widget associated to a
 *   kcontrol
 * @kcontrol: The kcontrol
 */
struct snd_soc_dapm_widget *snd_soc_dapm_kcontrol_widget(
				struct snd_kcontrol *kcontrol)
{
	return dapm_kcontrol_get_wlist(kcontrol)->widgets[0];
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_kcontrol_widget);

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

static void dapm_reset(struct snd_soc_card *card)
{
	struct snd_soc_dapm_widget *w;

	snd_soc_dapm_mutex_assert_held(card);

	memset(&card->dapm_stats, 0, sizeof(card->dapm_stats));

	for_each_card_widgets(card, w) {
		w->new_power = w->power;
		w->power_checked = false;
	}
}

static const char *soc_dapm_prefix(struct snd_soc_dapm_context *dapm)
{
	if (!dapm->component)
		return NULL;
	return dapm->component->name_prefix;
}

static unsigned int soc_dapm_read(struct snd_soc_dapm_context *dapm, int reg)
{
	if (!dapm->component)
		return -EIO;
	return  snd_soc_component_read(dapm->component, reg);
}

static int soc_dapm_update_bits(struct snd_soc_dapm_context *dapm,
	int reg, unsigned int mask, unsigned int value)
{
	if (!dapm->component)
		return -EIO;
	return snd_soc_component_update_bits(dapm->component, reg,
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

static struct snd_soc_dapm_widget *
dapm_wcache_lookup(struct snd_soc_dapm_widget *w, const char *name)
{
	if (w) {
		struct list_head *wlist = &w->dapm->card->widgets;
		const int depth = 2;
		int i = 0;

		list_for_each_entry_from(w, wlist, list) {
			if (!strcmp(name, w->name))
				return w;

			if (++i == depth)
				break;
		}
	}

	return NULL;
}

/**
 * snd_soc_dapm_force_bias_level() - Sets the DAPM bias level
 * @dapm: The DAPM context for which to set the level
 * @level: The level to set
 *
 * Forces the DAPM bias level to a specific state. It will call the bias level
 * callback of DAPM context with the specified level. This will even happen if
 * the context is already at the same level. Furthermore it will not go through
 * the normal bias level sequencing, meaning any intermediate states between the
 * current and the target state will not be entered.
 *
 * Note that the change in bias level is only temporary and the next time
 * snd_soc_dapm_sync() is called the state will be set to the level as
 * determined by the DAPM core. The function is mainly intended to be used to
 * used during probe or resume from suspend to power up the device so
 * initialization can be done, before the DAPM core takes over.
 */
int snd_soc_dapm_force_bias_level(struct snd_soc_dapm_context *dapm,
	enum snd_soc_bias_level level)
{
	int ret = 0;

	if (dapm->component)
		ret = snd_soc_component_set_bias_level(dapm->component, level);

	if (ret == 0)
		dapm->bias_level = level;

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_force_bias_level);

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

	ret = snd_soc_card_set_bias_level(card, dapm, level);
	if (ret != 0)
		goto out;

	if (!card || dapm != &card->dapm)
		ret = snd_soc_dapm_force_bias_level(dapm, level);

	if (ret != 0)
		goto out;

	ret = snd_soc_card_set_bias_level_post(card, dapm, level);
out:
	trace_snd_soc_bias_level_done(card, level);

	return ret;
}

/* connect mux widget to its interconnecting audio paths */
static int dapm_connect_mux(struct snd_soc_dapm_context *dapm,
	struct snd_soc_dapm_path *path, const char *control_name,
	struct snd_soc_dapm_widget *w)
{
	const struct snd_kcontrol_new *kcontrol = &w->kcontrol_news[0];
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int item;
	int i;

	if (e->reg != SND_SOC_NOPM) {
		unsigned int val;
		val = soc_dapm_read(dapm, e->reg);
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

	i = match_string(e->texts, e->items, control_name);
	if (i < 0)
		return -ENODEV;

	path->name = e->texts[i];
	path->connect = (i == item);
	return 0;

}

/* set up initial codec paths */
static void dapm_set_mixer_path_status(struct snd_soc_dapm_path *p, int i,
				       int nth_path)
{
	struct soc_mixer_control *mc = (struct soc_mixer_control *)
		p->sink->kcontrol_news[i].private_value;
	unsigned int reg = mc->reg;
	unsigned int invert = mc->invert;

	if (reg != SND_SOC_NOPM) {
		unsigned int shift = mc->shift;
		unsigned int max = mc->max;
		unsigned int mask = (1 << fls(max)) - 1;
		unsigned int val = soc_dapm_read(p->sink->dapm, reg);

		/*
		 * The nth_path argument allows this function to know
		 * which path of a kcontrol it is setting the initial
		 * status for. Ideally this would support any number
		 * of paths and channels. But since kcontrols only come
		 * in mono and stereo variants, we are limited to 2
		 * channels.
		 *
		 * The following code assumes for stereo controls the
		 * first path is the left channel, and all remaining
		 * paths are the right channel.
		 */
		if (snd_soc_volsw_is_stereo(mc) && nth_path > 0) {
			if (reg != mc->rreg)
				val = soc_dapm_read(p->sink->dapm, mc->rreg);
			val = (val >> mc->rshift) & mask;
		} else {
			val = (val >> shift) & mask;
		}
		if (invert)
			val = max - val;
		p->connect = !!val;
	} else {
		/* since a virtual mixer has no backing registers to
		 * decide which path to connect, it will try to match
		 * with initial state.  This is to ensure
		 * that the default mixer choice will be
		 * correctly powered up during initialization.
		 */
		p->connect = invert;
	}
}

/* connect mixer widget to its interconnecting audio paths */
static int dapm_connect_mixer(struct snd_soc_dapm_context *dapm,
	struct snd_soc_dapm_path *path, const char *control_name)
{
	int i, nth_path = 0;

	/* search for mixer kcontrol */
	for (i = 0; i < path->sink->num_kcontrols; i++) {
		if (!strcmp(control_name, path->sink->kcontrol_news[i].name)) {
			path->name = path->sink->kcontrol_news[i].name;
			dapm_set_mixer_path_status(path, i, nth_path++);
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

	for_each_card_widgets(dapm->card, w) {
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
static int dapm_create_or_share_kcontrol(struct snd_soc_dapm_widget *w,
	int kci)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_card *card = dapm->card->snd_card;
	const char *prefix;
	size_t prefix_len;
	int shared;
	struct snd_kcontrol *kcontrol;
	bool wname_in_long_name, kcname_in_long_name;
	char *long_name = NULL;
	const char *name;
	int ret = 0;

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
			case snd_soc_dapm_pga:
			case snd_soc_dapm_effect:
			case snd_soc_dapm_out_drv:
				wname_in_long_name = true;
				kcname_in_long_name = true;
				break;
			case snd_soc_dapm_mixer_named_ctl:
				wname_in_long_name = false;
				kcname_in_long_name = true;
				break;
			case snd_soc_dapm_demux:
			case snd_soc_dapm_mux:
				wname_in_long_name = true;
				kcname_in_long_name = false;
				break;
			default:
				return -EINVAL;
			}
		}
		if (w->no_wname_in_kcontrol_name)
			wname_in_long_name = false;

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
		if (!kcontrol) {
			ret = -ENOMEM;
			goto exit_free;
		}

		kcontrol->private_free = dapm_kcontrol_free;

		ret = dapm_kcontrol_data_alloc(w, kcontrol, name);
		if (ret) {
			snd_ctl_free_one(kcontrol);
			goto exit_free;
		}

		ret = snd_ctl_add(card, kcontrol);
		if (ret < 0) {
			dev_err(dapm->dev,
				"ASoC: failed to add widget %s dapm kcontrol %s: %d\n",
				w->name, name, ret);
			goto exit_free;
		}
	}

	ret = dapm_kcontrol_add_widget(kcontrol, w);
	if (ret == 0)
		w->kcontrols[kci] = kcontrol;

exit_free:
	kfree(long_name);

	return ret;
}

/* create new dapm mixer control */
static int dapm_new_mixer(struct snd_soc_dapm_widget *w)
{
	int i, ret;
	struct snd_soc_dapm_path *path;
	struct dapm_kcontrol_data *data;

	/* add kcontrol */
	for (i = 0; i < w->num_kcontrols; i++) {
		/* match name */
		snd_soc_dapm_widget_for_each_source_path(w, path) {
			/* mixer/mux paths name must match control name */
			if (path->name != (char *)w->kcontrol_news[i].name)
				continue;

			if (!w->kcontrols[i]) {
				ret = dapm_create_or_share_kcontrol(w, i);
				if (ret < 0)
					return ret;
			}

			dapm_kcontrol_add_path(w->kcontrols[i], path);

			data = snd_kcontrol_chip(w->kcontrols[i]);
			if (data->widget)
				snd_soc_dapm_add_path(data->widget->dapm,
						      data->widget,
						      path->source,
						      NULL, NULL);
		}
	}

	return 0;
}

/* create new dapm mux control */
static int dapm_new_mux(struct snd_soc_dapm_widget *w)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	enum snd_soc_dapm_direction dir;
	struct snd_soc_dapm_path *path;
	const char *type;
	int ret;

	switch (w->id) {
	case snd_soc_dapm_mux:
		dir = SND_SOC_DAPM_DIR_OUT;
		type = "mux";
		break;
	case snd_soc_dapm_demux:
		dir = SND_SOC_DAPM_DIR_IN;
		type = "demux";
		break;
	default:
		return -EINVAL;
	}

	if (w->num_kcontrols != 1) {
		dev_err(dapm->dev,
			"ASoC: %s %s has incorrect number of controls\n", type,
			w->name);
		return -EINVAL;
	}

	if (list_empty(&w->edges[dir])) {
		dev_err(dapm->dev, "ASoC: %s %s has no paths\n", type, w->name);
		return -EINVAL;
	}

	ret = dapm_create_or_share_kcontrol(w, 0);
	if (ret < 0)
		return ret;

	snd_soc_dapm_widget_for_each_path(w, dir, path) {
		if (path->name)
			dapm_kcontrol_add_path(w->kcontrols[0], path);
	}

	return 0;
}

/* create new dapm volume control */
static int dapm_new_pga(struct snd_soc_dapm_widget *w)
{
	int i;

	for (i = 0; i < w->num_kcontrols; i++) {
		int ret = dapm_create_or_share_kcontrol(w, i);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/* create new dapm dai link control */
static int dapm_new_dai_link(struct snd_soc_dapm_widget *w)
{
	int i;
	struct snd_soc_pcm_runtime *rtd = w->priv;

	/* create control for links with > 1 config */
	if (rtd->dai_link->num_c2c_params <= 1)
		return 0;

	/* add kcontrol */
	for (i = 0; i < w->num_kcontrols; i++) {
		struct snd_soc_dapm_context *dapm = w->dapm;
		struct snd_card *card = dapm->card->snd_card;
		struct snd_kcontrol *kcontrol = snd_soc_cnew(&w->kcontrol_news[i],
							     w, w->name, NULL);
		int ret = snd_ctl_add(card, kcontrol);

		if (ret < 0) {
			dev_err(dapm->dev,
				"ASoC: failed to add widget %s dapm kcontrol %s: %d\n",
				w->name, w->kcontrol_news[i].name, ret);
			return ret;
		}
		kcontrol->private_data = w;
		w->kcontrols[i] = kcontrol;
	}

	return 0;
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

static void dapm_widget_list_free(struct snd_soc_dapm_widget_list **list)
{
	kfree(*list);
}

static int dapm_widget_list_create(struct snd_soc_dapm_widget_list **list,
	struct list_head *widgets)
{
	struct snd_soc_dapm_widget *w;
	struct list_head *it;
	unsigned int size = 0;
	unsigned int i = 0;

	list_for_each(it, widgets)
		size++;

	*list = kzalloc(struct_size(*list, widgets, size), GFP_KERNEL);
	if (*list == NULL)
		return -ENOMEM;

	list_for_each_entry(w, widgets, work_list)
		(*list)->widgets[i++] = w;

	(*list)->num_widgets = i;

	return 0;
}

/*
 * Recursively reset the cached number of inputs or outputs for the specified
 * widget and all widgets that can be reached via incoming or outcoming paths
 * from the widget.
 */
static void invalidate_paths_ep(struct snd_soc_dapm_widget *widget,
	enum snd_soc_dapm_direction dir)
{
	enum snd_soc_dapm_direction rdir = SND_SOC_DAPM_DIR_REVERSE(dir);
	struct snd_soc_dapm_path *path;

	widget->endpoints[dir] = -1;

	snd_soc_dapm_widget_for_each_path(widget, rdir, path) {
		if (path->weak || path->is_supply)
			continue;

		if (path->walking)
			return;

		if (path->connect) {
			path->walking = 1;
			invalidate_paths_ep(path->node[dir], dir);
			path->walking = 0;
		}
	}
}

/*
 * Common implementation for is_connected_output_ep() and
 * is_connected_input_ep(). The function is inlined since the combined size of
 * the two specialized functions is only marginally larger then the size of the
 * generic function and at the same time the fast path of the specialized
 * functions is significantly smaller than the generic function.
 */
static __always_inline int is_connected_ep(struct snd_soc_dapm_widget *widget,
	struct list_head *list, enum snd_soc_dapm_direction dir,
	int (*fn)(struct snd_soc_dapm_widget *, struct list_head *,
		  bool (*custom_stop_condition)(struct snd_soc_dapm_widget *,
						enum snd_soc_dapm_direction)),
	bool (*custom_stop_condition)(struct snd_soc_dapm_widget *,
				      enum snd_soc_dapm_direction))
{
	enum snd_soc_dapm_direction rdir = SND_SOC_DAPM_DIR_REVERSE(dir);
	struct snd_soc_dapm_path *path;
	int con = 0;

	if (widget->endpoints[dir] >= 0)
		return widget->endpoints[dir];

	DAPM_UPDATE_STAT(widget, path_checks);

	/* do we need to add this widget to the list ? */
	if (list)
		list_add_tail(&widget->work_list, list);

	if (custom_stop_condition && custom_stop_condition(widget, dir)) {
		list = NULL;
		custom_stop_condition = NULL;
	}

	if ((widget->is_ep & SND_SOC_DAPM_DIR_TO_EP(dir)) && widget->connected) {
		widget->endpoints[dir] = snd_soc_dapm_suspend_check(widget);
		return widget->endpoints[dir];
	}

	snd_soc_dapm_widget_for_each_path(widget, rdir, path) {
		DAPM_UPDATE_STAT(widget, neighbour_checks);

		if (path->weak || path->is_supply)
			continue;

		if (path->walking)
			return 1;

		trace_snd_soc_dapm_path(widget, dir, path);

		if (path->connect) {
			path->walking = 1;
			con += fn(path->node[dir], list, custom_stop_condition);
			path->walking = 0;
		}
	}

	widget->endpoints[dir] = con;

	return con;
}

/*
 * Recursively check for a completed path to an active or physically connected
 * output widget. Returns number of complete paths.
 *
 * Optionally, can be supplied with a function acting as a stopping condition.
 * This function takes the dapm widget currently being examined and the walk
 * direction as an arguments, it should return true if widgets from that point
 * in the graph onwards should not be added to the widget list.
 */
static int is_connected_output_ep(struct snd_soc_dapm_widget *widget,
	struct list_head *list,
	bool (*custom_stop_condition)(struct snd_soc_dapm_widget *i,
				      enum snd_soc_dapm_direction))
{
	return is_connected_ep(widget, list, SND_SOC_DAPM_DIR_OUT,
			is_connected_output_ep, custom_stop_condition);
}

/*
 * Recursively check for a completed path to an active or physically connected
 * input widget. Returns number of complete paths.
 *
 * Optionally, can be supplied with a function acting as a stopping condition.
 * This function takes the dapm widget currently being examined and the walk
 * direction as an arguments, it should return true if the walk should be
 * stopped and false otherwise.
 */
static int is_connected_input_ep(struct snd_soc_dapm_widget *widget,
	struct list_head *list,
	bool (*custom_stop_condition)(struct snd_soc_dapm_widget *i,
				      enum snd_soc_dapm_direction))
{
	return is_connected_ep(widget, list, SND_SOC_DAPM_DIR_IN,
			is_connected_input_ep, custom_stop_condition);
}

/**
 * snd_soc_dapm_dai_get_connected_widgets - query audio path and it's widgets.
 * @dai: the soc DAI.
 * @stream: stream direction.
 * @list: list of active widgets for this stream.
 * @custom_stop_condition: (optional) a function meant to stop the widget graph
 *                         walk based on custom logic.
 *
 * Queries DAPM graph as to whether a valid audio stream path exists for
 * the initial stream specified by name. This takes into account
 * current mixer and mux kcontrol settings. Creates list of valid widgets.
 *
 * Optionally, can be supplied with a function acting as a stopping condition.
 * This function takes the dapm widget currently being examined and the walk
 * direction as an arguments, it should return true if the walk should be
 * stopped and false otherwise.
 *
 * Returns the number of valid paths or negative error.
 */
int snd_soc_dapm_dai_get_connected_widgets(struct snd_soc_dai *dai, int stream,
	struct snd_soc_dapm_widget_list **list,
	bool (*custom_stop_condition)(struct snd_soc_dapm_widget *,
				      enum snd_soc_dapm_direction))
{
	struct snd_soc_card *card = dai->component->card;
	struct snd_soc_dapm_widget *w = snd_soc_dai_get_widget(dai, stream);
	LIST_HEAD(widgets);
	int paths;
	int ret;

	snd_soc_dapm_mutex_lock(card);

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		invalidate_paths_ep(w, SND_SOC_DAPM_DIR_OUT);
		paths = is_connected_output_ep(w, &widgets,
				custom_stop_condition);
	} else {
		invalidate_paths_ep(w, SND_SOC_DAPM_DIR_IN);
		paths = is_connected_input_ep(w, &widgets,
				custom_stop_condition);
	}

	/* Drop starting point */
	list_del(widgets.next);

	ret = dapm_widget_list_create(list, &widgets);
	if (ret)
		paths = ret;

	trace_snd_soc_dapm_connected(paths, stream);
	snd_soc_dapm_mutex_unlock(card);

	return paths;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_dai_get_connected_widgets);

void snd_soc_dapm_dai_free_widgets(struct snd_soc_dapm_widget_list **list)
{
	dapm_widget_list_free(list);
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_dai_free_widgets);

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
 * Handler for pinctrl widget.
 */
int dapm_pinctrl_event(struct snd_soc_dapm_widget *w,
		       struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_dapm_pinctrl_priv *priv = w->priv;
	struct pinctrl *p = w->pinctrl;
	struct pinctrl_state *s;

	if (!p || !priv)
		return -EIO;

	if (SND_SOC_DAPM_EVENT_ON(event))
		s = pinctrl_lookup_state(p, priv->active_state);
	else
		s = pinctrl_lookup_state(p, priv->sleep_state);

	if (IS_ERR(s))
		return PTR_ERR(s);

	return pinctrl_select_state(p, s);
}
EXPORT_SYMBOL_GPL(dapm_pinctrl_event);

/*
 * Handler for clock supply widget.
 */
int dapm_clock_event(struct snd_soc_dapm_widget *w,
		   struct snd_kcontrol *kcontrol, int event)
{
	if (!w->clk)
		return -EIO;

	soc_dapm_async_complete(w->dapm);

	if (SND_SOC_DAPM_EVENT_ON(event)) {
		return clk_prepare_enable(w->clk);
	} else {
		clk_disable_unprepare(w->clk);
		return 0;
	}

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

/* Generic check to see if a widget should be powered. */
static int dapm_generic_check_power(struct snd_soc_dapm_widget *w)
{
	int in, out;

	DAPM_UPDATE_STAT(w, power_checks);

	in = is_connected_input_ep(w, NULL, NULL);
	out = is_connected_output_ep(w, NULL, NULL);
	return out != 0 && in != 0;
}

/* Check to see if a power supply is needed */
static int dapm_supply_check_power(struct snd_soc_dapm_widget *w)
{
	struct snd_soc_dapm_path *path;

	DAPM_UPDATE_STAT(w, power_checks);

	/* Check if one of our outputs is connected */
	snd_soc_dapm_widget_for_each_sink_path(w, path) {
		DAPM_UPDATE_STAT(w, neighbour_checks);

		if (path->weak)
			continue;

		if (path->connected &&
		    !path->connected(path->source, path->sink))
			continue;

		if (dapm_widget_power_check(path->sink))
			return 1;
	}

	return 0;
}

static int dapm_always_on_check_power(struct snd_soc_dapm_widget *w)
{
	return w->connected;
}

static int dapm_seq_compare(struct snd_soc_dapm_widget *a,
			    struct snd_soc_dapm_widget *b,
			    bool power_up)
{
	int *sort;

	BUILD_BUG_ON(ARRAY_SIZE(dapm_up_seq) != SND_SOC_DAPM_TYPE_COUNT);
	BUILD_BUG_ON(ARRAY_SIZE(dapm_down_seq) != SND_SOC_DAPM_TYPE_COUNT);

	if (power_up)
		sort = dapm_up_seq;
	else
		sort = dapm_down_seq;

	WARN_ONCE(sort[a->id] == 0, "offset a->id %d not initialized\n", a->id);
	WARN_ONCE(sort[b->id] == 0, "offset b->id %d not initialized\n", b->id);

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
	int power;

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
		int ret;

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
	int i;
	int *sort;

	if (power_up)
		sort = dapm_up_seq;
	else
		sort = dapm_down_seq;

	list_for_each_entry_safe(w, n, list, power_list) {
		int ret = 0;

		/* Do we need to apply any queued changes? */
		if (sort[w->id] != cur_sort || w->reg != cur_reg ||
		    w->dapm != cur_dapm || w->subseq != cur_subseq) {
			if (!list_empty(&pending))
				dapm_seq_run_coalesced(card, &pending);

			if (cur_dapm && cur_dapm->component) {
				for (i = 0; i < ARRAY_SIZE(dapm_up_seq); i++)
					if (sort[i] == cur_sort)
						snd_soc_component_seq_notifier(
							cur_dapm->component,
							i, cur_subseq);
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
				continue;

			if (event == SND_SOC_DAPM_STREAM_START)
				ret = w->event(w,
					       NULL, SND_SOC_DAPM_PRE_PMU);
			else if (event == SND_SOC_DAPM_STREAM_STOP)
				ret = w->event(w,
					       NULL, SND_SOC_DAPM_PRE_PMD);
			break;

		case snd_soc_dapm_post:
			if (!w->event)
				continue;

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

	if (cur_dapm && cur_dapm->component) {
		for (i = 0; i < ARRAY_SIZE(dapm_up_seq); i++)
			if (sort[i] == cur_sort)
				snd_soc_component_seq_notifier(
					cur_dapm->component,
					i, cur_subseq);
	}

	for_each_card_dapms(card, d)
		soc_dapm_async_complete(d);
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

	for_each_dapm_widgets(wlist, wi, w) {
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

	if (update->has_second_set) {
		ret = soc_dapm_update_bits(w->dapm, update->reg2,
					   update->mask2, update->val2);
		if (ret < 0)
			dev_err(w->dapm->dev,
				"ASoC: %s DAPM update failed: %d\n",
				w->name, ret);
	}

	for_each_dapm_widgets(wlist, wi, w) {
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

	/* If we're off and we're not supposed to go into STANDBY */
	if (d->bias_level == SND_SOC_BIAS_OFF &&
	    d->target_bias_level != SND_SOC_BIAS_OFF) {
		if (d->dev && cookie)
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

		if (d->dev && cookie)
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

static void dapm_power_one_widget(struct snd_soc_dapm_widget *w,
				  struct list_head *up_list,
				  struct list_head *down_list)
{
	struct snd_soc_dapm_path *path;
	int power;

	switch (w->id) {
	case snd_soc_dapm_pre:
		power = 0;
		goto end;
	case snd_soc_dapm_post:
		power = 1;
		goto end;
	default:
		break;
	}

	power = dapm_widget_power_check(w);

	if (w->power == power)
		return;

	trace_snd_soc_dapm_widget_power(w, power);

	/*
	 * If we changed our power state perhaps our neigbours
	 * changed also.
	 */
	snd_soc_dapm_widget_for_each_source_path(w, path)
		dapm_widget_set_peer_power(path->source, power, path->connect);

	/*
	 * Supplies can't affect their outputs, only their inputs
	 */
	if (!w->is_supply)
		snd_soc_dapm_widget_for_each_sink_path(w, path)
			dapm_widget_set_peer_power(path->sink, power, path->connect);

end:
	if (power)
		dapm_seq_insert(w, up_list, true);
	else
		dapm_seq_insert(w, down_list, false);
}

static bool dapm_idle_bias_off(struct snd_soc_dapm_context *dapm)
{
	if (dapm->idle_bias_off)
		return true;

	switch (snd_power_get_state(dapm->card->snd_card)) {
	case SNDRV_CTL_POWER_D3hot:
	case SNDRV_CTL_POWER_D3cold:
		return dapm->suspend_bias_off;
	default:
		break;
	}

	return false;
}

/*
 * Scan each dapm widget for complete audio path.
 * A complete path is a route that has valid endpoints i.e.:-
 *
 *  o DAC to output pin.
 *  o Input pin to ADC.
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
	int ret;

	snd_soc_dapm_mutex_assert_held(card);

	trace_snd_soc_dapm_start(card);

	for_each_card_dapms(card, d) {
		if (dapm_idle_bias_off(d))
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

	for_each_card_widgets(card, w) {
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
			case snd_soc_dapm_pinctrl:
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
	for_each_card_dapms(card, d)
		if (d->target_bias_level > bias)
			bias = d->target_bias_level;
	for_each_card_dapms(card, d)
		if (!dapm_idle_bias_off(d))
			d->target_bias_level = bias;

	trace_snd_soc_dapm_walk_done(card);

	/* Run card bias changes at first */
	dapm_pre_sequence_async(&card->dapm, 0);
	/* Run other bias changes in parallel */
	for_each_card_dapms(card, d) {
		if (d != &card->dapm && d->bias_level != d->target_bias_level)
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
	for_each_card_dapms(card, d) {
		if (d != &card->dapm && d->bias_level != d->target_bias_level)
			async_schedule_domain(dapm_post_sequence_async, d,
						&async_domain);
	}
	async_synchronize_full_domain(&async_domain);
	/* Run card bias changes at last */
	dapm_post_sequence_async(&card->dapm, 0);

	/* do we need to notify any clients that DAPM event is complete */
	for_each_card_dapms(card, d) {
		if (!d->component)
			continue;

		ret = snd_soc_component_stream_event(d->component, event);
		if (ret < 0)
			return ret;
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
	enum snd_soc_dapm_direction dir, rdir;
	char *buf;
	int in, out;
	ssize_t ret;
	struct snd_soc_dapm_path *p = NULL;

	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	snd_soc_dapm_mutex_lock_root(w->dapm);

	/* Supply widgets are not handled by is_connected_{input,output}_ep() */
	if (w->is_supply) {
		in = 0;
		out = 0;
	} else {
		in = is_connected_input_ep(w, NULL, NULL);
		out = is_connected_output_ep(w, NULL, NULL);
	}

	ret = scnprintf(buf, PAGE_SIZE, "%s: %s%s  in %d out %d",
		       w->name, w->power ? "On" : "Off",
		       w->force ? " (forced)" : "", in, out);

	if (w->reg >= 0)
		ret += scnprintf(buf + ret, PAGE_SIZE - ret,
				" - R%d(0x%x) mask 0x%x",
				w->reg, w->reg, w->mask << w->shift);

	ret += scnprintf(buf + ret, PAGE_SIZE - ret, "\n");

	if (w->sname)
		ret += scnprintf(buf + ret, PAGE_SIZE - ret, " stream %s %s\n",
				w->sname,
				w->active ? "active" : "inactive");

	snd_soc_dapm_for_each_direction(dir) {
		rdir = SND_SOC_DAPM_DIR_REVERSE(dir);
		snd_soc_dapm_widget_for_each_path(w, dir, p) {
			if (p->connected && !p->connected(p->source, p->sink))
				continue;

			if (!p->connect)
				continue;

			ret += scnprintf(buf + ret, PAGE_SIZE - ret,
					" %s  \"%s\" \"%s\"\n",
					(rdir == SND_SOC_DAPM_DIR_IN) ? "in" : "out",
					p->name ? p->name : "static",
					p->node[rdir]->name);
		}
	}

	snd_soc_dapm_mutex_unlock(w->dapm);

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
	if (!parent || IS_ERR(parent))
		return;

	dapm->debugfs_dapm = debugfs_create_dir("dapm", parent);

	debugfs_create_file("bias_level", 0444, dapm->debugfs_dapm, dapm,
			    &dapm_bias_fops);
}

static void dapm_debugfs_add_widget(struct snd_soc_dapm_widget *w)
{
	struct snd_soc_dapm_context *dapm = w->dapm;

	if (!dapm->debugfs_dapm || !w->name)
		return;

	debugfs_create_file(w->name, 0444, dapm->debugfs_dapm, w,
			    &dapm_widget_power_fops);
}

static void dapm_debugfs_free_widget(struct snd_soc_dapm_widget *w)
{
	struct snd_soc_dapm_context *dapm = w->dapm;

	if (!dapm->debugfs_dapm || !w->name)
		return;

	debugfs_lookup_and_remove(w->name, dapm->debugfs_dapm);
}

static void dapm_debugfs_cleanup(struct snd_soc_dapm_context *dapm)
{
	debugfs_remove_recursive(dapm->debugfs_dapm);
	dapm->debugfs_dapm = NULL;
}

#else
void snd_soc_dapm_debugfs_init(struct snd_soc_dapm_context *dapm,
	struct dentry *parent)
{
}

static inline void dapm_debugfs_add_widget(struct snd_soc_dapm_widget *w)
{
}

static inline void dapm_debugfs_free_widget(struct snd_soc_dapm_widget *w)
{
}

static inline void dapm_debugfs_cleanup(struct snd_soc_dapm_context *dapm)
{
}

#endif

/*
 * soc_dapm_connect_path() - Connects or disconnects a path
 * @path: The path to update
 * @connect: The new connect state of the path. True if the path is connected,
 *  false if it is disconnected.
 * @reason: The reason why the path changed (for debugging only)
 */
static void soc_dapm_connect_path(struct snd_soc_dapm_path *path,
	bool connect, const char *reason)
{
	if (path->connect == connect)
		return;

	path->connect = connect;
	dapm_mark_dirty(path->source, reason);
	dapm_mark_dirty(path->sink, reason);
	dapm_path_invalidate(path);
}

/* test and update the power status of a mux widget */
static int soc_dapm_mux_update_power(struct snd_soc_card *card,
				 struct snd_kcontrol *kcontrol, int mux, struct soc_enum *e)
{
	struct snd_soc_dapm_path *path;
	int found = 0;
	bool connect;

	snd_soc_dapm_mutex_assert_held(card);

	/* find dapm widget path assoc with kcontrol */
	dapm_kcontrol_for_each_path(path, kcontrol) {
		found = 1;
		/* we now need to match the string in the enum to the path */
		if (e && !(strcmp(path->name, e->texts[mux])))
			connect = true;
		else
			connect = false;

		soc_dapm_connect_path(path, connect, "mux update");
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

	snd_soc_dapm_mutex_lock(card);
	card->update = update;
	ret = soc_dapm_mux_update_power(card, kcontrol, mux, e);
	card->update = NULL;
	snd_soc_dapm_mutex_unlock(card);
	if (ret > 0)
		snd_soc_dpcm_runtime_update(card);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_mux_update_power);

/* test and update the power status of a mixer or switch widget */
static int soc_dapm_mixer_update_power(struct snd_soc_card *card,
				       struct snd_kcontrol *kcontrol,
				       int connect, int rconnect)
{
	struct snd_soc_dapm_path *path;
	int found = 0;

	snd_soc_dapm_mutex_assert_held(card);

	/* find dapm widget path assoc with kcontrol */
	dapm_kcontrol_for_each_path(path, kcontrol) {
		/*
		 * Ideally this function should support any number of
		 * paths and channels. But since kcontrols only come
		 * in mono and stereo variants, we are limited to 2
		 * channels.
		 *
		 * The following code assumes for stereo controls the
		 * first path (when 'found == 0') is the left channel,
		 * and all remaining paths (when 'found == 1') are the
		 * right channel.
		 *
		 * A stereo control is signified by a valid 'rconnect'
		 * value, either 0 for unconnected, or >= 0 for connected.
		 * This is chosen instead of using snd_soc_volsw_is_stereo,
		 * so that the behavior of snd_soc_dapm_mixer_update_power
		 * doesn't change even when the kcontrol passed in is
		 * stereo.
		 *
		 * It passes 'connect' as the path connect status for
		 * the left channel, and 'rconnect' for the right
		 * channel.
		 */
		if (found && rconnect >= 0)
			soc_dapm_connect_path(path, rconnect, "mixer update");
		else
			soc_dapm_connect_path(path, connect, "mixer update");
		found = 1;
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

	snd_soc_dapm_mutex_lock(card);
	card->update = update;
	ret = soc_dapm_mixer_update_power(card, kcontrol, connect, -1);
	card->update = NULL;
	snd_soc_dapm_mutex_unlock(card);
	if (ret > 0)
		snd_soc_dpcm_runtime_update(card);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_mixer_update_power);

static ssize_t dapm_widget_show_component(struct snd_soc_component *cmpnt,
					  char *buf, int count)
{
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(cmpnt);
	struct snd_soc_dapm_widget *w;
	char *state = "not set";

	/* card won't be set for the dummy component, as a spot fix
	 * we're checking for that case specifically here but in future
	 * we will ensure that the dummy component looks like others.
	 */
	if (!cmpnt->card)
		return 0;

	for_each_card_widgets(cmpnt->card, w) {
		if (w->dapm != dapm)
			continue;

		/* only display widgets that burn power */
		switch (w->id) {
		case snd_soc_dapm_hp:
		case snd_soc_dapm_mic:
		case snd_soc_dapm_spk:
		case snd_soc_dapm_line:
		case snd_soc_dapm_micbias:
		case snd_soc_dapm_dac:
		case snd_soc_dapm_adc:
		case snd_soc_dapm_pga:
		case snd_soc_dapm_effect:
		case snd_soc_dapm_out_drv:
		case snd_soc_dapm_mixer:
		case snd_soc_dapm_mixer_named_ctl:
		case snd_soc_dapm_supply:
		case snd_soc_dapm_regulator_supply:
		case snd_soc_dapm_pinctrl:
		case snd_soc_dapm_clock_supply:
			if (w->name)
				count += sysfs_emit_at(buf, count, "%s: %s\n",
					w->name, w->power ? "On":"Off");
		break;
		default:
		break;
		}
	}

	switch (snd_soc_dapm_get_bias_level(dapm)) {
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
	count += sysfs_emit_at(buf, count, "PM State: %s\n", state);

	return count;
}

/* show dapm widget status in sys fs */
static ssize_t dapm_widget_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct snd_soc_pcm_runtime *rtd = dev_get_drvdata(dev);
	struct snd_soc_dai *codec_dai;
	int i, count = 0;

	snd_soc_dapm_mutex_lock_root(rtd->card);

	for_each_rtd_codec_dais(rtd, i, codec_dai) {
		struct snd_soc_component *cmpnt = codec_dai->component;

		count = dapm_widget_show_component(cmpnt, buf, count);
	}

	snd_soc_dapm_mutex_unlock(rtd->card);

	return count;
}

static DEVICE_ATTR_RO(dapm_widget);

struct attribute *soc_dapm_dev_attrs[] = {
	&dev_attr_dapm_widget.attr,
	NULL
};

static void dapm_free_path(struct snd_soc_dapm_path *path)
{
	list_del(&path->list_node[SND_SOC_DAPM_DIR_IN]);
	list_del(&path->list_node[SND_SOC_DAPM_DIR_OUT]);
	list_del(&path->list_kcontrol);
	list_del(&path->list);
	kfree(path);
}

/**
 * snd_soc_dapm_free_widget - Free specified widget
 * @w: widget to free
 *
 * Removes widget from all paths and frees memory occupied by it.
 */
void snd_soc_dapm_free_widget(struct snd_soc_dapm_widget *w)
{
	struct snd_soc_dapm_path *p, *next_p;
	enum snd_soc_dapm_direction dir;

	if (!w)
		return;

	list_del(&w->list);
	list_del(&w->dirty);
	/*
	 * remove source and sink paths associated to this widget.
	 * While removing the path, remove reference to it from both
	 * source and sink widgets so that path is removed only once.
	 */
	snd_soc_dapm_for_each_direction(dir) {
		snd_soc_dapm_widget_for_each_path_safe(w, dir, p, next_p)
			dapm_free_path(p);
	}

	dapm_debugfs_free_widget(w);

	kfree(w->kcontrols);
	kfree_const(w->name);
	kfree_const(w->sname);
	kfree(w);
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_free_widget);

/* free all dapm widgets and resources */
static void dapm_free_widgets(struct snd_soc_dapm_context *dapm)
{
	struct snd_soc_dapm_widget *w, *next_w;

	for_each_card_widgets_safe(dapm->card, w, next_w) {
		if (w->dapm != dapm)
			continue;
		snd_soc_dapm_free_widget(w);
	}

	dapm->wcache_sink	= NULL;
	dapm->wcache_source	= NULL;
}

static struct snd_soc_dapm_widget *dapm_find_widget(
			struct snd_soc_dapm_context *dapm, const char *pin,
			bool search_other_contexts)
{
	struct snd_soc_dapm_widget *w;
	struct snd_soc_dapm_widget *fallback = NULL;
	char prefixed_pin[80];
	const char *pin_name;
	const char *prefix = soc_dapm_prefix(dapm);

	if (prefix) {
		snprintf(prefixed_pin, sizeof(prefixed_pin), "%s %s",
			 prefix, pin);
		pin_name = prefixed_pin;
	} else {
		pin_name = pin;
	}

	for_each_card_widgets(dapm->card, w) {
		if (!strcmp(w->name, pin_name)) {
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

/*
 * set the DAPM pin status:
 * returns 1 when the value has been updated, 0 when unchanged, or a negative
 * error code; called from kcontrol put callback
 */
static int __snd_soc_dapm_set_pin(struct snd_soc_dapm_context *dapm,
				  const char *pin, int status)
{
	struct snd_soc_dapm_widget *w = dapm_find_widget(dapm, pin, true);
	int ret = 0;

	dapm_assert_locked(dapm);

	if (!w) {
		dev_err(dapm->dev, "ASoC: DAPM unknown pin %s\n", pin);
		return -EINVAL;
	}

	if (w->connected != status) {
		dapm_mark_dirty(w, "pin configuration");
		dapm_widget_invalidate_input_paths(w);
		dapm_widget_invalidate_output_paths(w);
		ret = 1;
	}

	w->connected = status;
	if (status == 0)
		w->force = 0;

	return ret;
}

/*
 * similar as __snd_soc_dapm_set_pin(), but returns 0 when successful;
 * called from several API functions below
 */
static int snd_soc_dapm_set_pin(struct snd_soc_dapm_context *dapm,
				const char *pin, int status)
{
	int ret = __snd_soc_dapm_set_pin(dapm, pin, status);

	return ret < 0 ? ret : 0;
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
	if (!snd_soc_card_is_instantiated(dapm->card))
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

	snd_soc_dapm_mutex_lock(dapm);
	ret = snd_soc_dapm_sync_unlocked(dapm);
	snd_soc_dapm_mutex_unlock(dapm);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_sync);

static int dapm_update_dai_chan(struct snd_soc_dapm_path *p,
				struct snd_soc_dapm_widget *w,
				int channels)
{
	switch (w->id) {
	case snd_soc_dapm_aif_out:
	case snd_soc_dapm_aif_in:
		break;
	default:
		return 0;
	}

	dev_dbg(w->dapm->dev, "%s DAI route %s -> %s\n",
		w->channel < channels ? "Connecting" : "Disconnecting",
		p->source->name, p->sink->name);

	if (w->channel < channels)
		soc_dapm_connect_path(p, true, "dai update");
	else
		soc_dapm_connect_path(p, false, "dai update");

	return 0;
}

static int dapm_update_dai_unlocked(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params,
				    struct snd_soc_dai *dai)
{
	int dir = substream->stream;
	int channels = params_channels(params);
	struct snd_soc_dapm_path *p;
	struct snd_soc_dapm_widget *w;
	int ret;

	w = snd_soc_dai_get_widget(dai, dir);

	if (!w)
		return 0;

	dev_dbg(dai->dev, "Update DAI routes for %s %s\n", dai->name,
		dir == SNDRV_PCM_STREAM_PLAYBACK ? "playback" : "capture");

	snd_soc_dapm_widget_for_each_sink_path(w, p) {
		ret = dapm_update_dai_chan(p, p->sink, channels);
		if (ret < 0)
			return ret;
	}

	snd_soc_dapm_widget_for_each_source_path(w, p) {
		ret = dapm_update_dai_chan(p, p->source, channels);
		if (ret < 0)
			return ret;
	}

	return 0;
}

int snd_soc_dapm_update_dai(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	int ret;

	snd_soc_dapm_mutex_lock(rtd->card);
	ret = dapm_update_dai_unlocked(substream, params, dai);
	snd_soc_dapm_mutex_unlock(rtd->card);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_update_dai);

/*
 * dapm_update_widget_flags() - Re-compute widget sink and source flags
 * @w: The widget for which to update the flags
 *
 * Some widgets have a dynamic category which depends on which neighbors they
 * are connected to. This function update the category for these widgets.
 *
 * This function must be called whenever a path is added or removed to a widget.
 */
static void dapm_update_widget_flags(struct snd_soc_dapm_widget *w)
{
	enum snd_soc_dapm_direction dir;
	struct snd_soc_dapm_path *p;
	unsigned int ep;

	switch (w->id) {
	case snd_soc_dapm_input:
		/* On a fully routed card an input is never a source */
		if (w->dapm->card->fully_routed)
			return;
		ep = SND_SOC_DAPM_EP_SOURCE;
		snd_soc_dapm_widget_for_each_source_path(w, p) {
			if (p->source->id == snd_soc_dapm_micbias ||
				p->source->id == snd_soc_dapm_mic ||
				p->source->id == snd_soc_dapm_line ||
				p->source->id == snd_soc_dapm_output) {
					ep = 0;
					break;
			}
		}
		break;
	case snd_soc_dapm_output:
		/* On a fully routed card a output is never a sink */
		if (w->dapm->card->fully_routed)
			return;
		ep = SND_SOC_DAPM_EP_SINK;
		snd_soc_dapm_widget_for_each_sink_path(w, p) {
			if (p->sink->id == snd_soc_dapm_spk ||
				p->sink->id == snd_soc_dapm_hp ||
				p->sink->id == snd_soc_dapm_line ||
				p->sink->id == snd_soc_dapm_input) {
					ep = 0;
					break;
			}
		}
		break;
	case snd_soc_dapm_line:
		ep = 0;
		snd_soc_dapm_for_each_direction(dir) {
			if (!list_empty(&w->edges[dir]))
				ep |= SND_SOC_DAPM_DIR_TO_EP(dir);
		}
		break;
	default:
		return;
	}

	w->is_ep = ep;
}

static int snd_soc_dapm_check_dynamic_path(struct snd_soc_dapm_context *dapm,
	struct snd_soc_dapm_widget *source, struct snd_soc_dapm_widget *sink,
	const char *control)
{
	bool dynamic_source = false;
	bool dynamic_sink = false;

	if (!control)
		return 0;

	switch (source->id) {
	case snd_soc_dapm_demux:
		dynamic_source = true;
		break;
	default:
		break;
	}

	switch (sink->id) {
	case snd_soc_dapm_mux:
	case snd_soc_dapm_switch:
	case snd_soc_dapm_mixer:
	case snd_soc_dapm_mixer_named_ctl:
		dynamic_sink = true;
		break;
	default:
		break;
	}

	if (dynamic_source && dynamic_sink) {
		dev_err(dapm->dev,
			"Direct connection between demux and mixer/mux not supported for path %s -> [%s] -> %s\n",
			source->name, control, sink->name);
		return -EINVAL;
	} else if (!dynamic_source && !dynamic_sink) {
		dev_err(dapm->dev,
			"Control not supported for path %s -> [%s] -> %s\n",
			source->name, control, sink->name);
		return -EINVAL;
	}

	return 0;
}

static int snd_soc_dapm_add_path(struct snd_soc_dapm_context *dapm,
	struct snd_soc_dapm_widget *wsource, struct snd_soc_dapm_widget *wsink,
	const char *control,
	int (*connected)(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink))
{
	enum snd_soc_dapm_direction dir;
	struct snd_soc_dapm_path *path;
	int ret;

	if (wsink->is_supply && !wsource->is_supply) {
		dev_err(dapm->dev,
			"Connecting non-supply widget to supply widget is not supported (%s -> %s)\n",
			wsource->name, wsink->name);
		return -EINVAL;
	}

	if (connected && !wsource->is_supply) {
		dev_err(dapm->dev,
			"connected() callback only supported for supply widgets (%s -> %s)\n",
			wsource->name, wsink->name);
		return -EINVAL;
	}

	if (wsource->is_supply && control) {
		dev_err(dapm->dev,
			"Conditional paths are not supported for supply widgets (%s -> [%s] -> %s)\n",
			wsource->name, control, wsink->name);
		return -EINVAL;
	}

	ret = snd_soc_dapm_check_dynamic_path(dapm, wsource, wsink, control);
	if (ret)
		return ret;

	path = kzalloc(sizeof(struct snd_soc_dapm_path), GFP_KERNEL);
	if (!path)
		return -ENOMEM;

	path->node[SND_SOC_DAPM_DIR_IN] = wsource;
	path->node[SND_SOC_DAPM_DIR_OUT] = wsink;

	path->connected = connected;
	INIT_LIST_HEAD(&path->list);
	INIT_LIST_HEAD(&path->list_kcontrol);

	if (wsource->is_supply || wsink->is_supply)
		path->is_supply = 1;

	/* connect static paths */
	if (control == NULL) {
		path->connect = 1;
	} else {
		switch (wsource->id) {
		case snd_soc_dapm_demux:
			ret = dapm_connect_mux(dapm, path, control, wsource);
			if (ret)
				goto err;
			break;
		default:
			break;
		}

		switch (wsink->id) {
		case snd_soc_dapm_mux:
			ret = dapm_connect_mux(dapm, path, control, wsink);
			if (ret != 0)
				goto err;
			break;
		case snd_soc_dapm_switch:
		case snd_soc_dapm_mixer:
		case snd_soc_dapm_mixer_named_ctl:
			ret = dapm_connect_mixer(dapm, path, control);
			if (ret != 0)
				goto err;
			break;
		default:
			break;
		}
	}

	list_add(&path->list, &dapm->card->paths);

	snd_soc_dapm_for_each_direction(dir)
		list_add(&path->list_node[dir], &path->node[dir]->edges[dir]);

	snd_soc_dapm_for_each_direction(dir) {
		dapm_update_widget_flags(path->node[dir]);
		dapm_mark_dirty(path->node[dir], "Route added");
	}

	if (snd_soc_card_is_instantiated(dapm->card) && path->connect)
		dapm_path_invalidate(path);

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
	unsigned int sink_ref = 0;
	unsigned int source_ref = 0;
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

	wsource	= dapm_wcache_lookup(dapm->wcache_source, source);
	wsink	= dapm_wcache_lookup(dapm->wcache_sink,   sink);

	if (wsink && wsource)
		goto skip_search;

	/*
	 * find src and dest widgets over all widgets but favor a widget from
	 * current DAPM context
	 */
	for_each_card_widgets(dapm->card, w) {
		if (!wsink && !(strcmp(w->name, sink))) {
			wtsink = w;
			if (w->dapm == dapm) {
				wsink = w;
				if (wsource)
					break;
			}
			sink_ref++;
			if (sink_ref > 1)
				dev_warn(dapm->dev,
					"ASoC: sink widget %s overwritten\n",
					w->name);
			continue;
		}
		if (!wsource && !(strcmp(w->name, source))) {
			wtsource = w;
			if (w->dapm == dapm) {
				wsource = w;
				if (wsink)
					break;
			}
			source_ref++;
			if (source_ref > 1)
				dev_warn(dapm->dev,
					"ASoC: source widget %s overwritten\n",
					w->name);
		}
	}
	/* use widget from another DAPM context if not found from this */
	if (!wsink)
		wsink = wtsink;
	if (!wsource)
		wsource = wtsource;

	ret = -ENODEV;
	if (!wsource)
		goto err;
	if (!wsink)
		goto err;

skip_search:
	/* update cache */
	dapm->wcache_sink	= wsink;
	dapm->wcache_source	= wsource;

	ret = snd_soc_dapm_add_path(dapm, wsource, wsink, route->control,
		route->connected);
err:
	if (ret)
		dev_err(dapm->dev, "ASoC: Failed to add route %s%s -%s%s%s> %s%s\n",
			source, !wsource ? "(*)" : "",
			!route->control ? "" : "> [",
			!route->control ? "" : route->control,
			!route->control ? "" : "] -",
			sink,  !wsink ? "(*)" : "");
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
		struct snd_soc_dapm_widget *wsource = path->source;
		struct snd_soc_dapm_widget *wsink = path->sink;

		dapm_mark_dirty(wsource, "Route removed");
		dapm_mark_dirty(wsink, "Route removed");
		if (path->connect)
			dapm_path_invalidate(path);

		dapm_free_path(path);

		/* Update any path related flags */
		dapm_update_widget_flags(wsource);
		dapm_update_widget_flags(wsink);
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
	int i, ret = 0;

	snd_soc_dapm_mutex_lock(dapm);
	for (i = 0; i < num; i++) {
		int r = snd_soc_dapm_add_route(dapm, route);
		if (r < 0)
			ret = r;
		route++;
	}
	snd_soc_dapm_mutex_unlock(dapm);

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
	int i;

	snd_soc_dapm_mutex_lock(dapm);
	for (i = 0; i < num; i++) {
		snd_soc_dapm_del_route(dapm, route);
		route++;
	}
	snd_soc_dapm_mutex_unlock(dapm);

	return 0;
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

	snd_soc_dapm_widget_for_each_sink_path(source, path) {
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
	int i;
	int ret = 0;

	snd_soc_dapm_mutex_lock_root(dapm);
	for (i = 0; i < num; i++) {
		int err = snd_soc_dapm_weak_route(dapm, route);
		if (err)
			ret = err;
		route++;
	}
	snd_soc_dapm_mutex_unlock(dapm);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_weak_routes);

/**
 * snd_soc_dapm_new_widgets - add new dapm widgets
 * @card: card to be checked for new dapm widgets
 *
 * Checks the codec for any new dapm widgets and creates them if found.
 *
 * Returns 0 for success.
 */
int snd_soc_dapm_new_widgets(struct snd_soc_card *card)
{
	struct snd_soc_dapm_widget *w;
	unsigned int val;

	snd_soc_dapm_mutex_lock_root(card);

	for_each_card_widgets(card, w)
	{
		if (w->new)
			continue;

		if (w->num_kcontrols) {
			w->kcontrols = kcalloc(w->num_kcontrols,
						sizeof(struct snd_kcontrol *),
						GFP_KERNEL);
			if (!w->kcontrols) {
				snd_soc_dapm_mutex_unlock(card);
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
		case snd_soc_dapm_demux:
			dapm_new_mux(w);
			break;
		case snd_soc_dapm_pga:
		case snd_soc_dapm_effect:
		case snd_soc_dapm_out_drv:
			dapm_new_pga(w);
			break;
		case snd_soc_dapm_dai_link:
			dapm_new_dai_link(w);
			break;
		default:
			break;
		}

		/* Read the initial power state from the device */
		if (w->reg >= 0) {
			val = soc_dapm_read(w->dapm, w->reg);
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
	snd_soc_dapm_mutex_unlock(card);
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
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int reg = mc->reg;
	unsigned int shift = mc->shift;
	int max = mc->max;
	unsigned int width = fls(max);
	unsigned int mask = (1 << fls(max)) - 1;
	unsigned int invert = mc->invert;
	unsigned int reg_val, val, rval = 0;

	snd_soc_dapm_mutex_lock(dapm);
	if (dapm_kcontrol_is_powered(kcontrol) && reg != SND_SOC_NOPM) {
		reg_val = soc_dapm_read(dapm, reg);
		val = (reg_val >> shift) & mask;

		if (reg != mc->rreg)
			reg_val = soc_dapm_read(dapm, mc->rreg);

		if (snd_soc_volsw_is_stereo(mc))
			rval = (reg_val >> mc->rshift) & mask;
	} else {
		reg_val = dapm_kcontrol_get_value(kcontrol);
		val = reg_val & mask;

		if (snd_soc_volsw_is_stereo(mc))
			rval = (reg_val >> width) & mask;
	}
	snd_soc_dapm_mutex_unlock(dapm);

	if (invert)
		ucontrol->value.integer.value[0] = max - val;
	else
		ucontrol->value.integer.value[0] = val;

	if (snd_soc_volsw_is_stereo(mc)) {
		if (invert)
			ucontrol->value.integer.value[1] = max - rval;
		else
			ucontrol->value.integer.value[1] = rval;
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
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct snd_soc_card *card = dapm->card;
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int reg = mc->reg;
	unsigned int shift = mc->shift;
	int max = mc->max;
	unsigned int width = fls(max);
	unsigned int mask = (1 << width) - 1;
	unsigned int invert = mc->invert;
	unsigned int val, rval = 0;
	int connect, rconnect = -1, change, reg_change = 0;
	struct snd_soc_dapm_update update = {};
	int ret = 0;

	val = (ucontrol->value.integer.value[0] & mask);
	connect = !!val;

	if (invert)
		val = max - val;

	if (snd_soc_volsw_is_stereo(mc)) {
		rval = (ucontrol->value.integer.value[1] & mask);
		rconnect = !!rval;
		if (invert)
			rval = max - rval;
	}

	snd_soc_dapm_mutex_lock(card);

	/* This assumes field width < (bits in unsigned int / 2) */
	if (width > sizeof(unsigned int) * 8 / 2)
		dev_warn(dapm->dev,
			 "ASoC: control %s field width limit exceeded\n",
			 kcontrol->id.name);
	change = dapm_kcontrol_set_value(kcontrol, val | (rval << width));

	if (reg != SND_SOC_NOPM) {
		val = val << shift;
		rval = rval << mc->rshift;

		reg_change = soc_dapm_test_bits(dapm, reg, mask << shift, val);

		if (snd_soc_volsw_is_stereo(mc))
			reg_change |= soc_dapm_test_bits(dapm, mc->rreg,
							 mask << mc->rshift,
							 rval);
	}

	if (change || reg_change) {
		if (reg_change) {
			if (snd_soc_volsw_is_stereo(mc)) {
				update.has_second_set = true;
				update.reg2 = mc->rreg;
				update.mask2 = mask << mc->rshift;
				update.val2 = rval;
			}
			update.kcontrol = kcontrol;
			update.reg = reg;
			update.mask = mask << shift;
			update.val = val;
			card->update = &update;
		}

		ret = soc_dapm_mixer_update_power(card, kcontrol, connect,
						  rconnect);

		card->update = NULL;
	}

	snd_soc_dapm_mutex_unlock(card);

	if (ret > 0)
		snd_soc_dpcm_runtime_update(card);

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

	snd_soc_dapm_mutex_lock(dapm);
	if (e->reg != SND_SOC_NOPM && dapm_kcontrol_is_powered(kcontrol)) {
		reg_val = soc_dapm_read(dapm, e->reg);
	} else {
		reg_val = dapm_kcontrol_get_value(kcontrol);
	}
	snd_soc_dapm_mutex_unlock(dapm);

	val = (reg_val >> e->shift_l) & e->mask;
	ucontrol->value.enumerated.item[0] = snd_soc_enum_val_to_item(e, val);
	if (e->shift_l != e->shift_r) {
		val = (reg_val >> e->shift_r) & e->mask;
		val = snd_soc_enum_val_to_item(e, val);
		ucontrol->value.enumerated.item[1] = val;
	}

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
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct snd_soc_card *card = dapm->card;
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int *item = ucontrol->value.enumerated.item;
	unsigned int val, change, reg_change = 0;
	unsigned int mask;
	struct snd_soc_dapm_update update = {};
	int ret = 0;

	if (item[0] >= e->items)
		return -EINVAL;

	val = snd_soc_enum_item_to_val(e, item[0]) << e->shift_l;
	mask = e->mask << e->shift_l;
	if (e->shift_l != e->shift_r) {
		if (item[1] > e->items)
			return -EINVAL;
		val |= snd_soc_enum_item_to_val(e, item[1]) << e->shift_r;
		mask |= e->mask << e->shift_r;
	}

	snd_soc_dapm_mutex_lock(card);

	change = dapm_kcontrol_set_value(kcontrol, val);

	if (e->reg != SND_SOC_NOPM)
		reg_change = soc_dapm_test_bits(dapm, e->reg, mask, val);

	if (change || reg_change) {
		if (reg_change) {
			update.kcontrol = kcontrol;
			update.reg = e->reg;
			update.mask = mask;
			update.val = val;
			card->update = &update;
		}

		ret = soc_dapm_mux_update_power(card, kcontrol, item[0], e);

		card->update = NULL;
	}

	snd_soc_dapm_mutex_unlock(card);

	if (ret > 0)
		snd_soc_dpcm_runtime_update(card);

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

	snd_soc_dapm_mutex_lock(card);

	ucontrol->value.integer.value[0] =
		snd_soc_dapm_get_pin_status(&card->dapm, pin);

	snd_soc_dapm_mutex_unlock(card);

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
	int ret;

	snd_soc_dapm_mutex_lock(card);
	ret = __snd_soc_dapm_set_pin(&card->dapm, pin,
				     !!ucontrol->value.integer.value[0]);
	snd_soc_dapm_mutex_unlock(card);

	snd_soc_dapm_sync(&card->dapm);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_put_pin_switch);

struct snd_soc_dapm_widget *
snd_soc_dapm_new_control_unlocked(struct snd_soc_dapm_context *dapm,
			 const struct snd_soc_dapm_widget *widget)
{
	enum snd_soc_dapm_direction dir;
	struct snd_soc_dapm_widget *w;
	const char *prefix;
	int ret = -ENOMEM;

	if ((w = dapm_cnew_widget(widget)) == NULL)
		goto cnew_failed;

	prefix = soc_dapm_prefix(dapm);
	if (prefix)
		w->name = kasprintf(GFP_KERNEL, "%s %s", prefix, widget->name);
	else
		w->name = kstrdup_const(widget->name, GFP_KERNEL);
	if (!w->name)
		goto name_failed;

	switch (w->id) {
	case snd_soc_dapm_regulator_supply:
		w->regulator = devm_regulator_get(dapm->dev, widget->name);
		if (IS_ERR(w->regulator)) {
			ret = PTR_ERR(w->regulator);
			goto request_failed;
		}

		if (w->on_val & SND_SOC_DAPM_REGULATOR_BYPASS) {
			ret = regulator_allow_bypass(w->regulator, true);
			if (ret != 0)
				dev_warn(dapm->dev,
					 "ASoC: Failed to bypass %s: %d\n",
					 w->name, ret);
		}
		break;
	case snd_soc_dapm_pinctrl:
		w->pinctrl = devm_pinctrl_get(dapm->dev);
		if (IS_ERR(w->pinctrl)) {
			ret = PTR_ERR(w->pinctrl);
			goto request_failed;
		}

		/* set to sleep_state when initializing */
		dapm_pinctrl_event(w, NULL, SND_SOC_DAPM_POST_PMD);
		break;
	case snd_soc_dapm_clock_supply:
		w->clk = devm_clk_get(dapm->dev, w->name);
		if (IS_ERR(w->clk)) {
			ret = PTR_ERR(w->clk);
			goto request_failed;
		}
		break;
	default:
		break;
	}

	switch (w->id) {
	case snd_soc_dapm_mic:
		w->is_ep = SND_SOC_DAPM_EP_SOURCE;
		w->power_check = dapm_generic_check_power;
		break;
	case snd_soc_dapm_input:
		if (!dapm->card->fully_routed)
			w->is_ep = SND_SOC_DAPM_EP_SOURCE;
		w->power_check = dapm_generic_check_power;
		break;
	case snd_soc_dapm_spk:
	case snd_soc_dapm_hp:
		w->is_ep = SND_SOC_DAPM_EP_SINK;
		w->power_check = dapm_generic_check_power;
		break;
	case snd_soc_dapm_output:
		if (!dapm->card->fully_routed)
			w->is_ep = SND_SOC_DAPM_EP_SINK;
		w->power_check = dapm_generic_check_power;
		break;
	case snd_soc_dapm_vmid:
	case snd_soc_dapm_siggen:
		w->is_ep = SND_SOC_DAPM_EP_SOURCE;
		w->power_check = dapm_always_on_check_power;
		break;
	case snd_soc_dapm_sink:
		w->is_ep = SND_SOC_DAPM_EP_SINK;
		w->power_check = dapm_always_on_check_power;
		break;

	case snd_soc_dapm_mux:
	case snd_soc_dapm_demux:
	case snd_soc_dapm_switch:
	case snd_soc_dapm_mixer:
	case snd_soc_dapm_mixer_named_ctl:
	case snd_soc_dapm_adc:
	case snd_soc_dapm_aif_out:
	case snd_soc_dapm_dac:
	case snd_soc_dapm_aif_in:
	case snd_soc_dapm_pga:
	case snd_soc_dapm_buffer:
	case snd_soc_dapm_scheduler:
	case snd_soc_dapm_effect:
	case snd_soc_dapm_src:
	case snd_soc_dapm_asrc:
	case snd_soc_dapm_encoder:
	case snd_soc_dapm_decoder:
	case snd_soc_dapm_out_drv:
	case snd_soc_dapm_micbias:
	case snd_soc_dapm_line:
	case snd_soc_dapm_dai_link:
	case snd_soc_dapm_dai_out:
	case snd_soc_dapm_dai_in:
		w->power_check = dapm_generic_check_power;
		break;
	case snd_soc_dapm_supply:
	case snd_soc_dapm_regulator_supply:
	case snd_soc_dapm_pinctrl:
	case snd_soc_dapm_clock_supply:
	case snd_soc_dapm_kcontrol:
		w->is_supply = 1;
		w->power_check = dapm_supply_check_power;
		break;
	default:
		w->power_check = dapm_always_on_check_power;
		break;
	}

	w->dapm = dapm;
	INIT_LIST_HEAD(&w->list);
	INIT_LIST_HEAD(&w->dirty);
	/* see for_each_card_widgets */
	list_add_tail(&w->list, &dapm->card->widgets);

	snd_soc_dapm_for_each_direction(dir) {
		INIT_LIST_HEAD(&w->edges[dir]);
		w->endpoints[dir] = -1;
	}

	/* machine layer sets up unconnected pins and insertions */
	w->connected = 1;
	return w;

request_failed:
	dev_err_probe(dapm->dev, ret, "ASoC: Failed to request %s\n",
		      w->name);
	kfree_const(w->name);
name_failed:
	kfree_const(w->sname);
	kfree(w);
cnew_failed:
	return ERR_PTR(ret);
}

/**
 * snd_soc_dapm_new_control - create new dapm control
 * @dapm: DAPM context
 * @widget: widget template
 *
 * Creates new DAPM control based upon a template.
 *
 * Returns a widget pointer on success or an error pointer on failure
 */
struct snd_soc_dapm_widget *
snd_soc_dapm_new_control(struct snd_soc_dapm_context *dapm,
			 const struct snd_soc_dapm_widget *widget)
{
	struct snd_soc_dapm_widget *w;

	snd_soc_dapm_mutex_lock(dapm);
	w = snd_soc_dapm_new_control_unlocked(dapm, widget);
	snd_soc_dapm_mutex_unlock(dapm);

	return w;
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
	int i;
	int ret = 0;

	snd_soc_dapm_mutex_lock_root(dapm);
	for (i = 0; i < num; i++) {
		struct snd_soc_dapm_widget *w = snd_soc_dapm_new_control_unlocked(dapm, widget);
		if (IS_ERR(w)) {
			ret = PTR_ERR(w);
			break;
		}
		widget++;
	}
	snd_soc_dapm_mutex_unlock(dapm);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_new_controls);

static int
snd_soc_dai_link_event_pre_pmu(struct snd_soc_dapm_widget *w,
			       struct snd_pcm_substream *substream)
{
	struct snd_soc_dapm_path *path;
	struct snd_soc_dai *source, *sink;
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_pcm_hw_params *params = NULL;
	const struct snd_soc_pcm_stream *config = NULL;
	struct snd_pcm_runtime *runtime = NULL;
	unsigned int fmt;
	int ret = 0;

	/*
	 * NOTE
	 *
	 * snd_pcm_hw_params is quite large (608 bytes on arm64) and is
	 * starting to get a bit excessive for allocation on the stack,
	 * especially when you're building with some of the KASAN type
	 * stuff that increases stack usage.
	 * So, we use kzalloc()/kfree() for params in this function.
	 */
	params = kzalloc(sizeof(*params), GFP_KERNEL);
	if (!params)
		return -ENOMEM;

	runtime = kzalloc(sizeof(*runtime), GFP_KERNEL);
	if (!runtime) {
		ret = -ENOMEM;
		goto out;
	}

	substream->runtime = runtime;

	substream->stream = SNDRV_PCM_STREAM_CAPTURE;
	snd_soc_dapm_widget_for_each_source_path(w, path) {
		source = path->source->priv;

		ret = snd_soc_dai_startup(source, substream);
		if (ret < 0)
			goto out;

		snd_soc_dai_activate(source, substream->stream);
	}

	substream->stream = SNDRV_PCM_STREAM_PLAYBACK;
	snd_soc_dapm_widget_for_each_sink_path(w, path) {
		sink = path->sink->priv;

		ret = snd_soc_dai_startup(sink, substream);
		if (ret < 0)
			goto out;

		snd_soc_dai_activate(sink, substream->stream);
	}

	substream->hw_opened = 1;

	/*
	 * Note: getting the config after .startup() gives a chance to
	 * either party on the link to alter the configuration if
	 * necessary
	 */
	config = rtd->dai_link->c2c_params + rtd->c2c_params_select;
	if (!config) {
		dev_err(w->dapm->dev, "ASoC: link config missing\n");
		ret = -EINVAL;
		goto out;
	}

	/* Be a little careful as we don't want to overflow the mask array */
	if (!config->formats) {
		dev_warn(w->dapm->dev, "ASoC: Invalid format was specified\n");

		ret = -EINVAL;
		goto out;
	}

	fmt = ffs(config->formats) - 1;

	snd_mask_set(hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT), fmt);
	hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE)->min =
		config->rate_min;
	hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE)->max =
		config->rate_max;
	hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS)->min
		= config->channels_min;
	hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS)->max
		= config->channels_max;

	substream->stream = SNDRV_PCM_STREAM_CAPTURE;
	snd_soc_dapm_widget_for_each_source_path(w, path) {
		source = path->source->priv;

		ret = snd_soc_dai_hw_params(source, substream, params);
		if (ret < 0)
			goto out;

		dapm_update_dai_unlocked(substream, params, source);
	}

	substream->stream = SNDRV_PCM_STREAM_PLAYBACK;
	snd_soc_dapm_widget_for_each_sink_path(w, path) {
		sink = path->sink->priv;

		ret = snd_soc_dai_hw_params(sink, substream, params);
		if (ret < 0)
			goto out;

		dapm_update_dai_unlocked(substream, params, sink);
	}

	runtime->format = params_format(params);
	runtime->subformat = params_subformat(params);
	runtime->channels = params_channels(params);
	runtime->rate = params_rate(params);

out:
	/* see above NOTE */
	kfree(params);

	return ret;
}

static int snd_soc_dai_link_event(struct snd_soc_dapm_widget *w,
				  struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_dapm_path *path;
	struct snd_soc_dai *source, *sink;
	struct snd_pcm_substream *substream = w->priv;
	int ret = 0, saved_stream = substream->stream;

	if (WARN_ON(list_empty(&w->edges[SND_SOC_DAPM_DIR_OUT]) ||
		    list_empty(&w->edges[SND_SOC_DAPM_DIR_IN])))
		return -EINVAL;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		ret = snd_soc_dai_link_event_pre_pmu(w, substream);
		if (ret < 0)
			goto out;

		break;

	case SND_SOC_DAPM_POST_PMU:
		snd_soc_dapm_widget_for_each_sink_path(w, path) {
			sink = path->sink->priv;

			snd_soc_dai_digital_mute(sink, 0, SNDRV_PCM_STREAM_PLAYBACK);
			ret = 0;
		}
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_dapm_widget_for_each_sink_path(w, path) {
			sink = path->sink->priv;

			snd_soc_dai_digital_mute(sink, 1, SNDRV_PCM_STREAM_PLAYBACK);
			ret = 0;
		}

		substream->stream = SNDRV_PCM_STREAM_CAPTURE;
		snd_soc_dapm_widget_for_each_source_path(w, path) {
			source = path->source->priv;
			snd_soc_dai_hw_free(source, substream, 0);
		}

		substream->stream = SNDRV_PCM_STREAM_PLAYBACK;
		snd_soc_dapm_widget_for_each_sink_path(w, path) {
			sink = path->sink->priv;
			snd_soc_dai_hw_free(sink, substream, 0);
		}

		substream->stream = SNDRV_PCM_STREAM_CAPTURE;
		snd_soc_dapm_widget_for_each_source_path(w, path) {
			source = path->source->priv;
			snd_soc_dai_deactivate(source, substream->stream);
			snd_soc_dai_shutdown(source, substream, 0);
		}

		substream->stream = SNDRV_PCM_STREAM_PLAYBACK;
		snd_soc_dapm_widget_for_each_sink_path(w, path) {
			sink = path->sink->priv;
			snd_soc_dai_deactivate(sink, substream->stream);
			snd_soc_dai_shutdown(sink, substream, 0);
		}
		break;

	case SND_SOC_DAPM_POST_PMD:
		kfree(substream->runtime);
		break;

	default:
		WARN(1, "Unknown event %d\n", event);
		ret = -EINVAL;
	}

out:
	/* Restore the substream direction */
	substream->stream = saved_stream;
	return ret;
}

static int snd_soc_dapm_dai_link_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *w = snd_kcontrol_chip(kcontrol);
	struct snd_soc_pcm_runtime *rtd = w->priv;

	ucontrol->value.enumerated.item[0] = rtd->c2c_params_select;

	return 0;
}

static int snd_soc_dapm_dai_link_put(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *w = snd_kcontrol_chip(kcontrol);
	struct snd_soc_pcm_runtime *rtd = w->priv;

	/* Can't change the config when widget is already powered */
	if (w->power)
		return -EBUSY;

	if (ucontrol->value.enumerated.item[0] == rtd->c2c_params_select)
		return 0;

	if (ucontrol->value.enumerated.item[0] >= rtd->dai_link->num_c2c_params)
		return -EINVAL;

	rtd->c2c_params_select = ucontrol->value.enumerated.item[0];

	return 1;
}

static void
snd_soc_dapm_free_kcontrol(struct snd_soc_card *card,
			unsigned long *private_value,
			int num_c2c_params,
			const char **w_param_text)
{
	int count;

	devm_kfree(card->dev, (void *)*private_value);

	if (!w_param_text)
		return;

	for (count = 0 ; count < num_c2c_params; count++)
		devm_kfree(card->dev, (void *)w_param_text[count]);
	devm_kfree(card->dev, w_param_text);
}

static struct snd_kcontrol_new *
snd_soc_dapm_alloc_kcontrol(struct snd_soc_card *card,
			char *link_name,
			const struct snd_soc_pcm_stream *c2c_params,
			int num_c2c_params, const char **w_param_text,
			unsigned long *private_value)
{
	struct soc_enum w_param_enum[] = {
		SOC_ENUM_SINGLE(0, 0, 0, NULL),
	};
	struct snd_kcontrol_new kcontrol_dai_link[] = {
		SOC_ENUM_EXT(NULL, w_param_enum[0],
			     snd_soc_dapm_dai_link_get,
			     snd_soc_dapm_dai_link_put),
	};
	struct snd_kcontrol_new *kcontrol_news;
	const struct snd_soc_pcm_stream *config = c2c_params;
	int count;

	for (count = 0 ; count < num_c2c_params; count++) {
		if (!config->stream_name) {
			dev_warn(card->dapm.dev,
				"ASoC: anonymous config %d for dai link %s\n",
				count, link_name);
			w_param_text[count] =
				devm_kasprintf(card->dev, GFP_KERNEL,
					       "Anonymous Configuration %d",
					       count);
		} else {
			w_param_text[count] = devm_kmemdup(card->dev,
						config->stream_name,
						strlen(config->stream_name) + 1,
						GFP_KERNEL);
		}
		if (!w_param_text[count])
			goto outfree_w_param;
		config++;
	}

	w_param_enum[0].items = num_c2c_params;
	w_param_enum[0].texts = w_param_text;

	*private_value =
		(unsigned long) devm_kmemdup(card->dev,
			(void *)(kcontrol_dai_link[0].private_value),
			sizeof(struct soc_enum), GFP_KERNEL);
	if (!*private_value) {
		dev_err(card->dev, "ASoC: Failed to create control for %s widget\n",
			link_name);
		goto outfree_w_param;
	}
	kcontrol_dai_link[0].private_value = *private_value;
	/* duplicate kcontrol_dai_link on heap so that memory persists */
	kcontrol_news = devm_kmemdup(card->dev, &kcontrol_dai_link[0],
					sizeof(struct snd_kcontrol_new),
					GFP_KERNEL);
	if (!kcontrol_news) {
		dev_err(card->dev, "ASoC: Failed to create control for %s widget\n",
			link_name);
		goto outfree_w_param;
	}
	return kcontrol_news;

outfree_w_param:
	snd_soc_dapm_free_kcontrol(card, private_value, num_c2c_params, w_param_text);
	return NULL;
}

static struct snd_soc_dapm_widget *
snd_soc_dapm_new_dai(struct snd_soc_card *card,
		     struct snd_pcm_substream *substream,
		     char *id)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dapm_widget template;
	struct snd_soc_dapm_widget *w;
	const struct snd_kcontrol_new *kcontrol_news;
	int num_kcontrols;
	const char **w_param_text;
	unsigned long private_value = 0;
	char *link_name;
	int ret = -ENOMEM;

	link_name = devm_kasprintf(card->dev, GFP_KERNEL, "%s-%s",
				   rtd->dai_link->name, id);
	if (!link_name)
		goto name_fail;

	/* allocate memory for control, only in case of multiple configs */
	w_param_text	= NULL;
	kcontrol_news	= NULL;
	num_kcontrols	= 0;
	if (rtd->dai_link->num_c2c_params > 1) {
		w_param_text = devm_kcalloc(card->dev,
					    rtd->dai_link->num_c2c_params,
					    sizeof(char *), GFP_KERNEL);
		if (!w_param_text)
			goto param_fail;

		num_kcontrols = 1;
		kcontrol_news = snd_soc_dapm_alloc_kcontrol(card, link_name,
							    rtd->dai_link->c2c_params,
							    rtd->dai_link->num_c2c_params,
							    w_param_text, &private_value);
		if (!kcontrol_news)
			goto param_fail;
	}

	memset(&template, 0, sizeof(template));
	template.reg		= SND_SOC_NOPM;
	template.id		= snd_soc_dapm_dai_link;
	template.name		= link_name;
	template.event		= snd_soc_dai_link_event;
	template.event_flags	= SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMU |
				  SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMD;
	template.kcontrol_news	= kcontrol_news;
	template.num_kcontrols	= num_kcontrols;

	dev_dbg(card->dev, "ASoC: adding %s widget\n", link_name);

	w = snd_soc_dapm_new_control_unlocked(&card->dapm, &template);
	if (IS_ERR(w)) {
		ret = PTR_ERR(w);
		goto outfree_kcontrol_news;
	}

	w->priv = substream;

	return w;

outfree_kcontrol_news:
	devm_kfree(card->dev, (void *)template.kcontrol_news);
	snd_soc_dapm_free_kcontrol(card, &private_value,
				   rtd->dai_link->num_c2c_params, w_param_text);
param_fail:
	devm_kfree(card->dev, link_name);
name_fail:
	dev_err(rtd->dev, "ASoC: Failed to create %s-%s widget: %d\n",
		rtd->dai_link->name, id, ret);
	return ERR_PTR(ret);
}

/**
 * snd_soc_dapm_new_dai_widgets - Create new DAPM widgets
 * @dapm: DAPM context
 * @dai: parent DAI
 *
 * Returns 0 on success, error code otherwise.
 */
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

		w = snd_soc_dapm_new_control_unlocked(dapm, &template);
		if (IS_ERR(w))
			return PTR_ERR(w);

		w->priv = dai;
		snd_soc_dai_set_widget_playback(dai, w);
	}

	if (dai->driver->capture.stream_name) {
		template.id = snd_soc_dapm_dai_out;
		template.name = dai->driver->capture.stream_name;
		template.sname = dai->driver->capture.stream_name;

		dev_dbg(dai->dev, "ASoC: adding %s widget\n",
			template.name);

		w = snd_soc_dapm_new_control_unlocked(dapm, &template);
		if (IS_ERR(w))
			return PTR_ERR(w);

		w->priv = dai;
		snd_soc_dai_set_widget_capture(dai, w);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_new_dai_widgets);

int snd_soc_dapm_link_dai_widgets(struct snd_soc_card *card)
{
	struct snd_soc_dapm_widget *dai_w, *w;
	struct snd_soc_dapm_widget *src, *sink;
	struct snd_soc_dai *dai;

	/* For each DAI widget... */
	for_each_card_widgets(card, dai_w) {
		switch (dai_w->id) {
		case snd_soc_dapm_dai_in:
		case snd_soc_dapm_dai_out:
			break;
		default:
			continue;
		}

		/* let users know there is no DAI to link */
		if (!dai_w->priv) {
			dev_dbg(card->dev, "dai widget %s has no DAI\n",
				dai_w->name);
			continue;
		}

		dai = dai_w->priv;

		/* ...find all widgets with the same stream and link them */
		for_each_card_widgets(card, w) {
			if (w->dapm != dai_w->dapm)
				continue;

			switch (w->id) {
			case snd_soc_dapm_dai_in:
			case snd_soc_dapm_dai_out:
				continue;
			default:
				break;
			}

			if (!w->sname || !strstr(w->sname, dai_w->sname))
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

static void dapm_connect_dai_routes(struct snd_soc_dapm_context *dapm,
				    struct snd_soc_dai *src_dai,
				    struct snd_soc_dapm_widget *src,
				    struct snd_soc_dapm_widget *dai,
				    struct snd_soc_dai *sink_dai,
				    struct snd_soc_dapm_widget *sink)
{
	dev_dbg(dapm->dev, "connected DAI link %s:%s -> %s:%s\n",
		src_dai->component->name, src->name,
		sink_dai->component->name, sink->name);

	if (dai) {
		snd_soc_dapm_add_path(dapm, src, dai, NULL, NULL);
		src = dai;
	}

	snd_soc_dapm_add_path(dapm, src, sink, NULL, NULL);
}

static void dapm_connect_dai_pair(struct snd_soc_card *card,
				  struct snd_soc_pcm_runtime *rtd,
				  struct snd_soc_dai *codec_dai,
				  struct snd_soc_dai *cpu_dai)
{
	struct snd_soc_dai_link *dai_link = rtd->dai_link;
	struct snd_soc_dapm_widget *codec, *cpu;
	struct snd_soc_dai *src_dai[]		= { cpu_dai,	codec_dai };
	struct snd_soc_dai *sink_dai[]		= { codec_dai,	cpu_dai };
	struct snd_soc_dapm_widget **src[]	= { &cpu,	&codec };
	struct snd_soc_dapm_widget **sink[]	= { &codec,	&cpu };
	char *widget_name[]			= { "playback",	"capture" };
	int stream;

	for_each_pcm_streams(stream) {
		int stream_cpu, stream_codec;

		stream_cpu	= snd_soc_get_stream_cpu(dai_link, stream);
		stream_codec	= stream;

		/* connect BE DAI playback if widgets are valid */
		cpu	= snd_soc_dai_get_widget(cpu_dai,	stream_cpu);
		codec	= snd_soc_dai_get_widget(codec_dai,	stream_codec);

		if (!cpu || !codec)
			continue;

		/* special handling for [Codec2Codec] */
		if (dai_link->c2c_params && !rtd->c2c_widget[stream]) {
			struct snd_pcm_substream *substream = rtd->pcm->streams[stream].substream;
			struct snd_soc_dapm_widget *dai = snd_soc_dapm_new_dai(card, substream,
									       widget_name[stream]);

			if (IS_ERR(dai))
				continue;

			rtd->c2c_widget[stream] = dai;
		}

		dapm_connect_dai_routes(&card->dapm, src_dai[stream], *src[stream],
					rtd->c2c_widget[stream],
					sink_dai[stream], *sink[stream]);
	}
}

static void soc_dapm_dai_stream_event(struct snd_soc_dai *dai, int stream,
	int event)
{
	struct snd_soc_dapm_widget *w;

	w = snd_soc_dai_get_widget(dai, stream);

	if (w) {
		unsigned int ep;

		dapm_mark_dirty(w, "stream event");

		if (w->id == snd_soc_dapm_dai_in) {
			ep = SND_SOC_DAPM_EP_SOURCE;
			dapm_widget_invalidate_input_paths(w);
		} else {
			ep = SND_SOC_DAPM_EP_SINK;
			dapm_widget_invalidate_output_paths(w);
		}

		switch (event) {
		case SND_SOC_DAPM_STREAM_START:
			w->active = 1;
			w->is_ep = ep;
			break;
		case SND_SOC_DAPM_STREAM_STOP:
			w->active = 0;
			w->is_ep = 0;
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
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_dai *codec_dai;
	int i;

	/* for each BE DAI link... */
	for_each_card_rtds(card, rtd)  {
		/*
		 * dynamic FE links have no fixed DAI mapping.
		 * CODEC<->CODEC links have no direct connection.
		 */
		if (rtd->dai_link->dynamic)
			continue;

		if (rtd->dai_link->num_cpus == 1) {
			for_each_rtd_codec_dais(rtd, i, codec_dai)
				dapm_connect_dai_pair(card, rtd, codec_dai,
						      asoc_rtd_to_cpu(rtd, 0));
		} else if (rtd->dai_link->num_codecs == rtd->dai_link->num_cpus) {
			for_each_rtd_codec_dais(rtd, i, codec_dai)
				dapm_connect_dai_pair(card, rtd, codec_dai,
						      asoc_rtd_to_cpu(rtd, i));
		} else if (rtd->dai_link->num_codecs > rtd->dai_link->num_cpus) {
			int cpu_id;

			if (!rtd->dai_link->codec_ch_maps) {
				dev_err(card->dev, "%s: no codec channel mapping table provided\n",
					__func__);
				continue;
			}

			for_each_rtd_codec_dais(rtd, i, codec_dai) {
				cpu_id = rtd->dai_link->codec_ch_maps[i].connected_cpu_id;
				if (cpu_id >= rtd->dai_link->num_cpus) {
					dev_err(card->dev,
						"%s: dai_link %s cpu_id %d too large, num_cpus is %d\n",
						__func__, rtd->dai_link->name, cpu_id,
						rtd->dai_link->num_cpus);
					continue;
				}
				dapm_connect_dai_pair(card, rtd, codec_dai,
						      asoc_rtd_to_cpu(rtd, cpu_id));
			}
		} else {
			dev_err(card->dev,
				"%s: codec number %d < cpu number %d is not supported\n",
				__func__, rtd->dai_link->num_codecs, rtd->dai_link->num_cpus);
		}
	}
}

static void soc_dapm_stream_event(struct snd_soc_pcm_runtime *rtd, int stream,
	int event)
{
	struct snd_soc_dai *dai;
	int i;

	for_each_rtd_dais(rtd, i, dai)
		soc_dapm_dai_stream_event(dai, stream, event);

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

	snd_soc_dapm_mutex_lock(card);
	soc_dapm_stream_event(rtd, stream, event);
	snd_soc_dapm_mutex_unlock(card);
}

void snd_soc_dapm_stream_stop(struct snd_soc_pcm_runtime *rtd, int stream)
{
	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (snd_soc_runtime_ignore_pmdown_time(rtd)) {
			/* powered down playback stream now */
			snd_soc_dapm_stream_event(rtd,
						  SNDRV_PCM_STREAM_PLAYBACK,
						  SND_SOC_DAPM_STREAM_STOP);
		} else {
			/* start delayed pop wq here for playback streams */
			rtd->pop_wait = 1;
			queue_delayed_work(system_power_efficient_wq,
					   &rtd->delayed_work,
					   msecs_to_jiffies(rtd->pmdown_time));
		}
	} else {
		/* capture streams can be powered down now */
		snd_soc_dapm_stream_event(rtd, SNDRV_PCM_STREAM_CAPTURE,
					  SND_SOC_DAPM_STREAM_STOP);
	}
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_stream_stop);

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

	snd_soc_dapm_mutex_lock(dapm);

	ret = snd_soc_dapm_set_pin(dapm, pin, 1);

	snd_soc_dapm_mutex_unlock(dapm);

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
	if (!w->connected) {
		/*
		 * w->force does not affect the number of input or output paths,
		 * so we only have to recheck if w->connected is changed
		 */
		dapm_widget_invalidate_input_paths(w);
		dapm_widget_invalidate_output_paths(w);
		w->connected = 1;
	}
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

	snd_soc_dapm_mutex_lock(dapm);

	ret = snd_soc_dapm_force_enable_pin_unlocked(dapm, pin);

	snd_soc_dapm_mutex_unlock(dapm);

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

	snd_soc_dapm_mutex_lock(dapm);

	ret = snd_soc_dapm_set_pin(dapm, pin, 0);

	snd_soc_dapm_mutex_unlock(dapm);

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

	snd_soc_dapm_mutex_lock(dapm);

	ret = snd_soc_dapm_set_pin(dapm, pin, 0);

	snd_soc_dapm_mutex_unlock(dapm);

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

/**
 * snd_soc_dapm_free - free dapm resources
 * @dapm: DAPM context
 *
 * Free all dapm widgets and resources.
 */
void snd_soc_dapm_free(struct snd_soc_dapm_context *dapm)
{
	dapm_debugfs_cleanup(dapm);
	dapm_free_widgets(dapm);
	list_del(&dapm->list);
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_free);

void snd_soc_dapm_init(struct snd_soc_dapm_context *dapm,
		       struct snd_soc_card *card,
		       struct snd_soc_component *component)
{
	dapm->card		= card;
	dapm->component		= component;
	dapm->bias_level	= SND_SOC_BIAS_OFF;

	if (component) {
		dapm->dev		= component->dev;
		dapm->idle_bias_off	= !component->driver->idle_bias_on;
		dapm->suspend_bias_off	= component->driver->suspend_bias_off;
	} else {
		dapm->dev		= card->dev;
	}

	INIT_LIST_HEAD(&dapm->list);
	/* see for_each_card_dapms */
	list_add(&dapm->list, &card->dapm_list);
}
EXPORT_SYMBOL_GPL(snd_soc_dapm_init);

static void soc_dapm_shutdown_dapm(struct snd_soc_dapm_context *dapm)
{
	struct snd_soc_card *card = dapm->card;
	struct snd_soc_dapm_widget *w;
	LIST_HEAD(down_list);
	int powerdown = 0;

	snd_soc_dapm_mutex_lock_root(card);

	for_each_card_widgets(dapm->card, w) {
		if (w->dapm != dapm)
			continue;
		if (w->power) {
			dapm_seq_insert(w, &down_list, false);
			w->new_power = 0;
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

	snd_soc_dapm_mutex_unlock(card);
}

/*
 * snd_soc_dapm_shutdown - callback for system shutdown
 */
void snd_soc_dapm_shutdown(struct snd_soc_card *card)
{
	struct snd_soc_dapm_context *dapm;

	for_each_card_dapms(card, dapm) {
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
