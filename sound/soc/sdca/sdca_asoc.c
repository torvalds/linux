// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2025 Cirrus Logic, Inc. and
//                    Cirrus Logic International Semiconductor Ltd.

/*
 * The MIPI SDCA specification is available for public downloads at
 * https://www.mipi.org/mipi-sdca-v1-0-download
 */

#include <linux/bits.h>
#include <linux/bitmap.h>
#include <linux/build_bug.h>
#include <linux/delay.h>
#include <linux/dev_printk.h>
#include <linux/device.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/regmap.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/string_helpers.h>
#include <linux/types.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/sdca.h>
#include <sound/sdca_asoc.h>
#include <sound/sdca_function.h>
#include <sound/soc.h>
#include <sound/soc-component.h>
#include <sound/soc-dai.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>

static bool exported_control(struct sdca_entity *entity, struct sdca_control *control)
{
	switch (SDCA_CTL_TYPE(entity->type, control->sel)) {
	case SDCA_CTL_TYPE_S(GE, DETECTED_MODE):
		return true;
	default:
		break;
	}

	return control->layers & (SDCA_ACCESS_LAYER_USER |
				  SDCA_ACCESS_LAYER_APPLICATION);
}

static bool readonly_control(struct sdca_control *control)
{
	return control->has_fixed || control->mode == SDCA_ACCESS_MODE_RO;
}

/**
 * sdca_asoc_count_component - count the various component parts
 * @dev: Pointer to the device against which allocations will be done.
 * @function: Pointer to the Function information.
 * @num_widgets: Output integer pointer, will be filled with the
 * required number of DAPM widgets for the Function.
 * @num_routes: Output integer pointer, will be filled with the
 * required number of DAPM routes for the Function.
 * @num_controls: Output integer pointer, will be filled with the
 * required number of ALSA controls for the Function.
 * @num_dais: Output integer pointer, will be filled with the
 * required number of ASoC DAIs for the Function.
 *
 * This function counts various things within the SDCA Function such
 * that the calling driver can allocate appropriate space before
 * calling the appropriate population functions.
 *
 * Return: Returns zero on success, and a negative error code on failure.
 */
int sdca_asoc_count_component(struct device *dev, struct sdca_function_data *function,
			      int *num_widgets, int *num_routes, int *num_controls,
			      int *num_dais)
{
	int i, j;

	*num_widgets = function->num_entities - 1;
	*num_routes = 0;
	*num_controls = 0;
	*num_dais = 0;

	for (i = 0; i < function->num_entities - 1; i++) {
		struct sdca_entity *entity = &function->entities[i];

		/* Add supply/DAI widget connections */
		switch (entity->type) {
		case SDCA_ENTITY_TYPE_IT:
		case SDCA_ENTITY_TYPE_OT:
			*num_routes += !!entity->iot.clock;
			*num_routes += !!entity->iot.is_dataport;
			*num_controls += !entity->iot.is_dataport;
			*num_dais += !!entity->iot.is_dataport;
			break;
		case SDCA_ENTITY_TYPE_PDE:
			*num_routes += entity->pde.num_managed;
			break;
		default:
			break;
		}

		if (entity->group)
			(*num_routes)++;

		/* Add primary entity connections from DisCo */
		*num_routes += entity->num_sources;

		for (j = 0; j < entity->num_controls; j++) {
			if (exported_control(entity, &entity->controls[j]))
				(*num_controls)++;
		}
	}

	return 0;
}
EXPORT_SYMBOL_NS(sdca_asoc_count_component, "SND_SOC_SDCA");

static const char *get_terminal_name(enum sdca_terminal_type type)
{
	switch (type) {
	case SDCA_TERM_TYPE_LINEIN_STEREO:
		return SDCA_TERM_TYPE_LINEIN_STEREO_NAME;
	case SDCA_TERM_TYPE_LINEIN_FRONT_LR:
		return SDCA_TERM_TYPE_LINEIN_FRONT_LR_NAME;
	case SDCA_TERM_TYPE_LINEIN_CENTER_LFE:
		return SDCA_TERM_TYPE_LINEIN_CENTER_LFE_NAME;
	case SDCA_TERM_TYPE_LINEIN_SURROUND_LR:
		return SDCA_TERM_TYPE_LINEIN_SURROUND_LR_NAME;
	case SDCA_TERM_TYPE_LINEIN_REAR_LR:
		return SDCA_TERM_TYPE_LINEIN_REAR_LR_NAME;
	case SDCA_TERM_TYPE_LINEOUT_STEREO:
		return SDCA_TERM_TYPE_LINEOUT_STEREO_NAME;
	case SDCA_TERM_TYPE_LINEOUT_FRONT_LR:
		return SDCA_TERM_TYPE_LINEOUT_FRONT_LR_NAME;
	case SDCA_TERM_TYPE_LINEOUT_CENTER_LFE:
		return SDCA_TERM_TYPE_LINEOUT_CENTER_LFE_NAME;
	case SDCA_TERM_TYPE_LINEOUT_SURROUND_LR:
		return SDCA_TERM_TYPE_LINEOUT_SURROUND_LR_NAME;
	case SDCA_TERM_TYPE_LINEOUT_REAR_LR:
		return SDCA_TERM_TYPE_LINEOUT_REAR_LR_NAME;
	case SDCA_TERM_TYPE_MIC_JACK:
		return SDCA_TERM_TYPE_MIC_JACK_NAME;
	case SDCA_TERM_TYPE_STEREO_JACK:
		return SDCA_TERM_TYPE_STEREO_JACK_NAME;
	case SDCA_TERM_TYPE_FRONT_LR_JACK:
		return SDCA_TERM_TYPE_FRONT_LR_JACK_NAME;
	case SDCA_TERM_TYPE_CENTER_LFE_JACK:
		return SDCA_TERM_TYPE_CENTER_LFE_JACK_NAME;
	case SDCA_TERM_TYPE_SURROUND_LR_JACK:
		return SDCA_TERM_TYPE_SURROUND_LR_JACK_NAME;
	case SDCA_TERM_TYPE_REAR_LR_JACK:
		return SDCA_TERM_TYPE_REAR_LR_JACK_NAME;
	case SDCA_TERM_TYPE_HEADPHONE_JACK:
		return SDCA_TERM_TYPE_HEADPHONE_JACK_NAME;
	case SDCA_TERM_TYPE_HEADSET_JACK:
		return SDCA_TERM_TYPE_HEADSET_JACK_NAME;
	default:
		return NULL;
	}
}

static int entity_early_parse_ge(struct device *dev,
				 struct sdca_function_data *function,
				 struct sdca_entity *entity)
{
	struct sdca_control_range *range;
	struct sdca_control *control;
	struct snd_kcontrol_new *kctl;
	struct soc_enum *soc_enum;
	const char *control_name;
	unsigned int *values;
	const char **texts;
	int i;

	control = sdca_selector_find_control(dev, entity, SDCA_CTL_GE_SELECTED_MODE);
	if (!control)
		return -EINVAL;

	if (control->layers != SDCA_ACCESS_LAYER_CLASS)
		dev_warn(dev, "%s: unexpected access layer: %x\n",
			 entity->label, control->layers);

	range = sdca_control_find_range(dev, entity, control, SDCA_SELECTED_MODE_NCOLS, 0);
	if (!range)
		return -EINVAL;

	control_name = devm_kasprintf(dev, GFP_KERNEL, "%s %s",
				      entity->label, control->label);
	if (!control_name)
		return -ENOMEM;

	kctl = devm_kzalloc(dev, sizeof(*kctl), GFP_KERNEL);
	if (!kctl)
		return -ENOMEM;

	soc_enum = devm_kzalloc(dev, sizeof(*soc_enum), GFP_KERNEL);
	if (!soc_enum)
		return -ENOMEM;

	texts = devm_kcalloc(dev, range->rows + 3, sizeof(*texts), GFP_KERNEL);
	if (!texts)
		return -ENOMEM;

	values = devm_kcalloc(dev, range->rows + 3, sizeof(*values), GFP_KERNEL);
	if (!values)
		return -ENOMEM;

	texts[0] = "Jack Unplugged";
	texts[1] = "Jack Unknown";
	texts[2] = "Detection in Progress";
	values[0] = SDCA_DETECTED_MODE_JACK_UNPLUGGED;
	values[1] = SDCA_DETECTED_MODE_JACK_UNKNOWN;
	values[2] = SDCA_DETECTED_MODE_DETECTION_IN_PROGRESS;
	for (i = 0; i < range->rows; i++) {
		enum sdca_terminal_type type;

		type = sdca_range(range, SDCA_SELECTED_MODE_TERM_TYPE, i);

		values[i + 3] = sdca_range(range, SDCA_SELECTED_MODE_INDEX, i);
		texts[i + 3] = get_terminal_name(type);
		if (!texts[i + 3]) {
			dev_err(dev, "%s: unrecognised terminal type: %#x\n",
				entity->label, type);
			return -EINVAL;
		}
	}

	soc_enum->reg = SDW_SDCA_CTL(function->desc->adr, entity->id, control->sel, 0);
	soc_enum->items = range->rows + 3;
	soc_enum->mask = roundup_pow_of_two(soc_enum->items) - 1;
	soc_enum->texts = texts;
	soc_enum->values = values;

	kctl->iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	kctl->name = control_name;
	kctl->info = snd_soc_info_enum_double;
	kctl->get = snd_soc_dapm_get_enum_double;
	kctl->put = snd_soc_dapm_put_enum_double;
	kctl->private_value = (unsigned long)soc_enum;

	entity->ge.kctl = kctl;

	return 0;
}

static void add_route(struct snd_soc_dapm_route **route, const char *sink,
		      const char *control, const char *source)
{
	(*route)->sink = sink;
	(*route)->control = control;
	(*route)->source = source;
	(*route)++;
}

static int entity_parse_simple(struct device *dev,
			       struct sdca_function_data *function,
			       struct sdca_entity *entity,
			       struct snd_soc_dapm_widget **widget,
			       struct snd_soc_dapm_route **route,
			       enum snd_soc_dapm_type id)
{
	int i;

	(*widget)->id = id;
	(*widget)++;

	for (i = 0; i < entity->num_sources; i++)
		add_route(route, entity->label, NULL, entity->sources[i]->label);

	return 0;
}

static int entity_parse_it(struct device *dev,
			   struct sdca_function_data *function,
			   struct sdca_entity *entity,
			   struct snd_soc_dapm_widget **widget,
			   struct snd_soc_dapm_route **route)
{
	int i;

	if (entity->iot.is_dataport) {
		const char *aif_name = devm_kasprintf(dev, GFP_KERNEL, "%s %s",
						      entity->label, "Playback");
		if (!aif_name)
			return -ENOMEM;

		(*widget)->id = snd_soc_dapm_aif_in;

		add_route(route, entity->label, NULL, aif_name);
	} else {
		(*widget)->id = snd_soc_dapm_mic;
	}

	if (entity->iot.clock)
		add_route(route, entity->label, NULL, entity->iot.clock->label);

	for (i = 0; i < entity->num_sources; i++)
		add_route(route, entity->label, NULL, entity->sources[i]->label);

	(*widget)++;

	return 0;
}

static int entity_parse_ot(struct device *dev,
			   struct sdca_function_data *function,
			   struct sdca_entity *entity,
			   struct snd_soc_dapm_widget **widget,
			   struct snd_soc_dapm_route **route)
{
	int i;

	if (entity->iot.is_dataport) {
		const char *aif_name = devm_kasprintf(dev, GFP_KERNEL, "%s %s",
						      entity->label, "Capture");
		if (!aif_name)
			return -ENOMEM;

		(*widget)->id = snd_soc_dapm_aif_out;

		add_route(route, aif_name, NULL, entity->label);
	} else {
		(*widget)->id = snd_soc_dapm_spk;
	}

	if (entity->iot.clock)
		add_route(route, entity->label, NULL, entity->iot.clock->label);

	for (i = 0; i < entity->num_sources; i++)
		add_route(route, entity->label, NULL, entity->sources[i]->label);

	(*widget)++;

	return 0;
}

static int entity_pde_event(struct snd_soc_dapm_widget *widget,
			    struct snd_kcontrol *kctl, int event)
{
	struct snd_soc_component *component = widget->dapm->component;
	struct sdca_entity *entity = widget->priv;
	static const int polls = 100;
	unsigned int reg, val;
	int from, to, i;
	int poll_us;
	int ret;

	if (!component)
		return -EIO;

	switch (event) {
	case SND_SOC_DAPM_POST_PMD:
		from = widget->on_val;
		to = widget->off_val;
		break;
	case SND_SOC_DAPM_POST_PMU:
		from = widget->off_val;
		to = widget->on_val;
		break;
	default:
		return 0;
	}

	for (i = 0; i < entity->pde.num_max_delay; i++) {
		struct sdca_pde_delay *delay = &entity->pde.max_delay[i];

		if (delay->from_ps == from && delay->to_ps == to) {
			poll_us = delay->us / polls;
			break;
		}
	}

	reg = SDW_SDCA_CTL(SDW_SDCA_CTL_FUNC(widget->reg),
			   SDW_SDCA_CTL_ENT(widget->reg),
			   SDCA_CTL_PDE_ACTUAL_PS, 0);

	for (i = 0; i < polls; i++) {
		if (i)
			fsleep(poll_us);

		ret = regmap_read(component->regmap, reg, &val);
		if (ret)
			return ret;
		else if (val == to)
			return 0;
	}

	dev_err(component->dev, "%s: power transition failed: %x\n",
		entity->label, val);
	return -ETIMEDOUT;
}

static int entity_parse_pde(struct device *dev,
			    struct sdca_function_data *function,
			    struct sdca_entity *entity,
			    struct snd_soc_dapm_widget **widget,
			    struct snd_soc_dapm_route **route)
{
	unsigned int target = (1 << SDCA_PDE_PS0) | (1 << SDCA_PDE_PS3);
	struct sdca_control_range *range;
	struct sdca_control *control;
	unsigned int mask = 0;
	int i;

	control = sdca_selector_find_control(dev, entity, SDCA_CTL_PDE_REQUESTED_PS);
	if (!control)
		return -EINVAL;

	/* Power should only be controlled by the driver */
	if (control->layers != SDCA_ACCESS_LAYER_CLASS)
		dev_warn(dev, "%s: unexpected access layer: %x\n",
			 entity->label, control->layers);

	range = sdca_control_find_range(dev, entity, control, SDCA_REQUESTED_PS_NCOLS, 0);
	if (!range)
		return -EINVAL;

	for (i = 0; i < range->rows; i++)
		mask |= 1 << sdca_range(range, SDCA_REQUESTED_PS_STATE, i);

	if ((mask & target) != target) {
		dev_err(dev, "%s: power control missing states\n", entity->label);
		return -EINVAL;
	}

	(*widget)->id = snd_soc_dapm_supply;
	(*widget)->reg = SDW_SDCA_CTL(function->desc->adr, entity->id, control->sel, 0);
	(*widget)->mask = GENMASK(control->nbits - 1, 0);
	(*widget)->on_val = SDCA_PDE_PS0;
	(*widget)->off_val = SDCA_PDE_PS3;
	(*widget)->event_flags = SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD;
	(*widget)->event = entity_pde_event;
	(*widget)->priv = entity;
	(*widget)++;

	for (i = 0; i < entity->pde.num_managed; i++)
		add_route(route, entity->pde.managed[i]->label, NULL, entity->label);

	for (i = 0; i < entity->num_sources; i++)
		add_route(route, entity->label, NULL, entity->sources[i]->label);

	return 0;
}

/* Device selector units are controlled through a group entity */
static int entity_parse_su_device(struct device *dev,
				  struct sdca_function_data *function,
				  struct sdca_entity *entity,
				  struct snd_soc_dapm_widget **widget,
				  struct snd_soc_dapm_route **route)
{
	struct sdca_control_range *range;
	int num_routes = 0;
	int i, j;

	if (!entity->group) {
		dev_err(dev, "%s: device selector unit missing group\n", entity->label);
		return -EINVAL;
	}

	range = sdca_selector_find_range(dev, entity->group, SDCA_CTL_GE_SELECTED_MODE,
					 SDCA_SELECTED_MODE_NCOLS, 0);
	if (!range)
		return -EINVAL;

	(*widget)->id = snd_soc_dapm_mux;
	(*widget)->kcontrol_news = entity->group->ge.kctl;
	(*widget)->num_kcontrols = 1;
	(*widget)++;

	for (i = 0; i < entity->group->ge.num_modes; i++) {
		struct sdca_ge_mode *mode = &entity->group->ge.modes[i];

		for (j = 0; j < mode->num_controls; j++) {
			struct sdca_ge_control *affected = &mode->controls[j];
			int term;

			if (affected->id != entity->id ||
			    affected->sel != SDCA_CTL_SU_SELECTOR ||
			    !affected->val)
				continue;

			if (affected->val - 1 >= entity->num_sources) {
				dev_err(dev, "%s: bad control value: %#x\n",
					entity->label, affected->val);
				return -EINVAL;
			}

			if (++num_routes > entity->num_sources) {
				dev_err(dev, "%s: too many input routes\n", entity->label);
				return -EINVAL;
			}

			term = sdca_range_search(range, SDCA_SELECTED_MODE_INDEX,
						 mode->val, SDCA_SELECTED_MODE_TERM_TYPE);
			if (!term) {
				dev_err(dev, "%s: mode not found: %#x\n",
					entity->label, mode->val);
				return -EINVAL;
			}

			add_route(route, entity->label, get_terminal_name(term),
				  entity->sources[affected->val - 1]->label);
		}
	}

	return 0;
}

/* Class selector units will be exported as an ALSA control */
static int entity_parse_su_class(struct device *dev,
				 struct sdca_function_data *function,
				 struct sdca_entity *entity,
				 struct sdca_control *control,
				 struct snd_soc_dapm_widget **widget,
				 struct snd_soc_dapm_route **route)
{
	struct snd_kcontrol_new *kctl;
	struct soc_enum *soc_enum;
	const char **texts;
	int i;

	kctl = devm_kzalloc(dev, sizeof(*kctl), GFP_KERNEL);
	if (!kctl)
		return -ENOMEM;

	soc_enum = devm_kzalloc(dev, sizeof(*soc_enum), GFP_KERNEL);
	if (!soc_enum)
		return -ENOMEM;

	texts = devm_kcalloc(dev, entity->num_sources + 1, sizeof(*texts), GFP_KERNEL);
	if (!texts)
		return -ENOMEM;

	texts[0] = "No Signal";
	for (i = 0; i < entity->num_sources; i++)
		texts[i + 1] = entity->sources[i]->label;

	soc_enum->reg = SDW_SDCA_CTL(function->desc->adr, entity->id, control->sel, 0);
	soc_enum->items = entity->num_sources + 1;
	soc_enum->mask = roundup_pow_of_two(soc_enum->items) - 1;
	soc_enum->texts = texts;

	kctl->iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	kctl->name = "Route";
	kctl->info = snd_soc_info_enum_double;
	kctl->get = snd_soc_dapm_get_enum_double;
	kctl->put = snd_soc_dapm_put_enum_double;
	kctl->private_value = (unsigned long)soc_enum;

	(*widget)->id = snd_soc_dapm_mux;
	(*widget)->kcontrol_news = kctl;
	(*widget)->num_kcontrols = 1;
	(*widget)++;

	for (i = 0; i < entity->num_sources; i++)
		add_route(route, entity->label, texts[i + 1], entity->sources[i]->label);

	return 0;
}

static int entity_parse_su(struct device *dev,
			   struct sdca_function_data *function,
			   struct sdca_entity *entity,
			   struct snd_soc_dapm_widget **widget,
			   struct snd_soc_dapm_route **route)
{
	struct sdca_control *control;

	if (!entity->num_sources) {
		dev_err(dev, "%s: selector with no inputs\n", entity->label);
		return -EINVAL;
	}

	control = sdca_selector_find_control(dev, entity, SDCA_CTL_SU_SELECTOR);
	if (!control)
		return -EINVAL;

	if (control->layers == SDCA_ACCESS_LAYER_DEVICE)
		return entity_parse_su_device(dev, function, entity, widget, route);

	if (control->layers != SDCA_ACCESS_LAYER_CLASS)
		dev_warn(dev, "%s: unexpected access layer: %x\n",
			 entity->label, control->layers);

	return entity_parse_su_class(dev, function, entity, control, widget, route);
}

static int entity_parse_mu(struct device *dev,
			   struct sdca_function_data *function,
			   struct sdca_entity *entity,
			   struct snd_soc_dapm_widget **widget,
			   struct snd_soc_dapm_route **route)
{
	struct sdca_control *control;
	struct snd_kcontrol_new *kctl;
	int i;

	if (!entity->num_sources) {
		dev_err(dev, "%s: selector 1 or more inputs\n", entity->label);
		return -EINVAL;
	}

	control = sdca_selector_find_control(dev, entity, SDCA_CTL_MU_MIXER);
	if (!control)
		return -EINVAL;

	/* MU control should be through DAPM */
	if (control->layers != SDCA_ACCESS_LAYER_CLASS)
		dev_warn(dev, "%s: unexpected access layer: %x\n",
			 entity->label, control->layers);

	kctl = devm_kcalloc(dev, entity->num_sources, sizeof(*kctl), GFP_KERNEL);
	if (!kctl)
		return -ENOMEM;

	for (i = 0; i < entity->num_sources; i++) {
		const char *control_name;
		struct soc_mixer_control *mc;

		control_name = devm_kasprintf(dev, GFP_KERNEL, "%s %d",
					      control->label, i + 1);
		if (!control_name)
			return -ENOMEM;

		mc = devm_kzalloc(dev, sizeof(*mc), GFP_KERNEL);
		if (!mc)
			return -ENOMEM;

		mc->reg = SND_SOC_NOPM;
		mc->rreg = SND_SOC_NOPM;
		mc->invert = 1; // Ensure default is connected
		mc->min = 0;
		mc->max = 1;

		kctl[i].name = control_name;
		kctl[i].private_value = (unsigned long)mc;
		kctl[i].iface = SNDRV_CTL_ELEM_IFACE_MIXER;
		kctl[i].info = snd_soc_info_volsw;
		kctl[i].get = snd_soc_dapm_get_volsw;
		kctl[i].put = snd_soc_dapm_put_volsw;
	}

	(*widget)->id = snd_soc_dapm_mixer;
	(*widget)->kcontrol_news = kctl;
	(*widget)->num_kcontrols = entity->num_sources;
	(*widget)++;

	for (i = 0; i < entity->num_sources; i++)
		add_route(route, entity->label, kctl[i].name, entity->sources[i]->label);

	return 0;
}

static int entity_cs_event(struct snd_soc_dapm_widget *widget,
			   struct snd_kcontrol *kctl, int event)
{
	struct snd_soc_component *component = widget->dapm->component;
	struct sdca_entity *entity = widget->priv;

	if (!component)
		return -EIO;

	if (entity->cs.max_delay)
		fsleep(entity->cs.max_delay);

	return 0;
}

static int entity_parse_cs(struct device *dev,
			   struct sdca_function_data *function,
			   struct sdca_entity *entity,
			   struct snd_soc_dapm_widget **widget,
			   struct snd_soc_dapm_route **route)
{
	int i;

	(*widget)->id = snd_soc_dapm_supply;
	(*widget)->subseq = 1; /* Ensure these run after PDEs */
	(*widget)->event_flags = SND_SOC_DAPM_POST_PMU;
	(*widget)->event = entity_cs_event;
	(*widget)->priv = entity;
	(*widget)++;

	for (i = 0; i < entity->num_sources; i++)
		add_route(route, entity->label, NULL, entity->sources[i]->label);

	return 0;
}

/**
 * sdca_asoc_populate_dapm - fill in arrays of DAPM widgets and routes
 * @dev: Pointer to the device against which allocations will be done.
 * @function: Pointer to the Function information.
 * @widget: Array of DAPM widgets to be populated.
 * @route: Array of DAPM routes to be populated.
 *
 * This function populates arrays of DAPM widgets and routes from the
 * DisCo information for a particular SDCA Function. Typically,
 * snd_soc_asoc_count_component will be used to allocate appropriately
 * sized arrays before calling this function.
 *
 * Return: Returns zero on success, and a negative error code on failure.
 */
int sdca_asoc_populate_dapm(struct device *dev, struct sdca_function_data *function,
			    struct snd_soc_dapm_widget *widget,
			    struct snd_soc_dapm_route *route)
{
	int ret;
	int i;

	for (i = 0; i < function->num_entities - 1; i++) {
		struct sdca_entity *entity = &function->entities[i];

		/*
		 * Some entities need to add controls "early" as they are
		 * referenced by other entities.
		 */
		switch (entity->type) {
		case SDCA_ENTITY_TYPE_GE:
			ret = entity_early_parse_ge(dev, function, entity);
			if (ret)
				return ret;
			break;
		default:
			break;
		}
	}

	for (i = 0; i < function->num_entities - 1; i++) {
		struct sdca_entity *entity = &function->entities[i];

		widget->name = entity->label;
		widget->reg = SND_SOC_NOPM;

		switch (entity->type) {
		case SDCA_ENTITY_TYPE_IT:
			ret = entity_parse_it(dev, function, entity, &widget, &route);
			break;
		case SDCA_ENTITY_TYPE_OT:
			ret = entity_parse_ot(dev, function, entity, &widget, &route);
			break;
		case SDCA_ENTITY_TYPE_PDE:
			ret = entity_parse_pde(dev, function, entity, &widget, &route);
			break;
		case SDCA_ENTITY_TYPE_SU:
			ret = entity_parse_su(dev, function, entity, &widget, &route);
			break;
		case SDCA_ENTITY_TYPE_MU:
			ret = entity_parse_mu(dev, function, entity, &widget, &route);
			break;
		case SDCA_ENTITY_TYPE_CS:
			ret = entity_parse_cs(dev, function, entity, &widget, &route);
			break;
		case SDCA_ENTITY_TYPE_CX:
			/*
			 * FIXME: For now we will just treat these as a supply,
			 * meaning all options are enabled.
			 */
			dev_warn(dev, "%s: clock selectors not fully supported yet\n",
				 entity->label);
			ret = entity_parse_simple(dev, function, entity, &widget,
						  &route, snd_soc_dapm_supply);
			break;
		case SDCA_ENTITY_TYPE_TG:
			ret = entity_parse_simple(dev, function, entity, &widget,
						  &route, snd_soc_dapm_siggen);
			break;
		case SDCA_ENTITY_TYPE_GE:
			ret = entity_parse_simple(dev, function, entity, &widget,
						  &route, snd_soc_dapm_supply);
			break;
		default:
			ret = entity_parse_simple(dev, function, entity, &widget,
						  &route, snd_soc_dapm_pga);
			break;
		}
		if (ret)
			return ret;

		if (entity->group)
			add_route(&route, entity->label, NULL, entity->group->label);
	}

	return 0;
}
EXPORT_SYMBOL_NS(sdca_asoc_populate_dapm, "SND_SOC_SDCA");

static int control_limit_kctl(struct device *dev,
			      struct sdca_entity *entity,
			      struct sdca_control *control,
			      struct snd_kcontrol_new *kctl)
{
	struct soc_mixer_control *mc = (struct soc_mixer_control *)kctl->private_value;
	struct sdca_control_range *range;
	int min, max, step;
	unsigned int *tlv;
	int shift;

	if (control->type != SDCA_CTL_DATATYPE_Q7P8DB)
		return 0;

	/*
	 * FIXME: For now only handle the simple case of a single linear range
	 */
	range = sdca_control_find_range(dev, entity, control, SDCA_VOLUME_LINEAR_NCOLS, 1);
	if (!range)
		return -EINVAL;

	min = sdca_range(range, SDCA_VOLUME_LINEAR_MIN, 0);
	max = sdca_range(range, SDCA_VOLUME_LINEAR_MAX, 0);
	step = sdca_range(range, SDCA_VOLUME_LINEAR_STEP, 0);

	min = sign_extend32(min, control->nbits - 1);
	max = sign_extend32(max, control->nbits - 1);

	/*
	 * FIXME: Only support power of 2 step sizes as this can be supported
	 * by a simple shift.
	 */
	if (hweight32(step) != 1) {
		dev_err(dev, "%s: %s: currently unsupported step size\n",
			entity->label, control->label);
		return -EINVAL;
	}

	/*
	 * The SDCA volumes are in steps of 1/256th of a dB, a step down of
	 * 64 (shift of 6) gives 1/4dB. 1/4dB is the smallest unit that is also
	 * representable in the ALSA TLVs which are in 1/100ths of a dB.
	 */
	shift = max(ffs(step) - 1, 6);

	tlv = devm_kcalloc(dev, 4, sizeof(*tlv), GFP_KERNEL);
	if (!tlv)
		return -ENOMEM;

	tlv[0] = SNDRV_CTL_TLVT_DB_SCALE;
	tlv[1] = 2 * sizeof(*tlv);
	tlv[2] = (min * 100) >> 8;
	tlv[3] = ((1 << shift) * 100) >> 8;

	mc->min = min >> shift;
	mc->max = max >> shift;
	mc->shift = shift;
	mc->rshift = shift;
	mc->sign_bit = 15 - shift;

	kctl->tlv.p = tlv;
	kctl->access |= SNDRV_CTL_ELEM_ACCESS_TLV_READ;

	return 0;
}

static int populate_control(struct device *dev,
			    struct sdca_function_data *function,
			    struct sdca_entity *entity,
			    struct sdca_control *control,
			    struct snd_kcontrol_new **kctl)
{
	const char *control_suffix = "";
	const char *control_name;
	struct soc_mixer_control *mc;
	int index = 0;
	int ret;
	int cn;

	if (!exported_control(entity, control))
		return 0;

	if (control->type == SDCA_CTL_DATATYPE_ONEBIT)
		control_suffix = " Switch";

	control_name = devm_kasprintf(dev, GFP_KERNEL, "%s %s%s", entity->label,
				      control->label, control_suffix);
	if (!control_name)
		return -ENOMEM;

	mc = devm_kzalloc(dev, sizeof(*mc), GFP_KERNEL);
	if (!mc)
		return -ENOMEM;

	for_each_set_bit(cn, (unsigned long *)&control->cn_list,
			 BITS_PER_TYPE(control->cn_list)) {
		switch (index++) {
		case 0:
			mc->reg = SDW_SDCA_CTL(function->desc->adr, entity->id,
					       control->sel, cn);
			mc->rreg = mc->reg;
			break;
		case 1:
			mc->rreg = SDW_SDCA_CTL(function->desc->adr, entity->id,
						control->sel, cn);
			break;
		default:
			dev_err(dev, "%s: %s: only mono/stereo controls supported\n",
				entity->label, control->label);
			return -EINVAL;
		}
	}

	mc->min = 0;
	mc->max = clamp((0x1ull << control->nbits) - 1, 0, type_max(mc->max));

	(*kctl)->name = control_name;
	(*kctl)->private_value = (unsigned long)mc;
	(*kctl)->iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	(*kctl)->info = snd_soc_info_volsw;
	(*kctl)->get = snd_soc_get_volsw;
	(*kctl)->put = snd_soc_put_volsw;

	if (readonly_control(control))
		(*kctl)->access = SNDRV_CTL_ELEM_ACCESS_READ;
	else
		(*kctl)->access = SNDRV_CTL_ELEM_ACCESS_READWRITE;

	ret = control_limit_kctl(dev, entity, control, *kctl);
	if (ret)
		return ret;

	(*kctl)++;

	return 0;
}

static int populate_pin_switch(struct device *dev,
			       struct sdca_entity *entity,
			       struct snd_kcontrol_new **kctl)
{
	const char *control_name;

	control_name = devm_kasprintf(dev, GFP_KERNEL, "%s Switch", entity->label);
	if (!control_name)
		return -ENOMEM;

	(*kctl)->name = control_name;
	(*kctl)->private_value = (unsigned long)entity->label;
	(*kctl)->iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	(*kctl)->info = snd_soc_dapm_info_pin_switch;
	(*kctl)->get = snd_soc_dapm_get_component_pin_switch;
	(*kctl)->put = snd_soc_dapm_put_component_pin_switch;
	(*kctl)++;

	return 0;
}

/**
 * sdca_asoc_populate_controls - fill in an array of ALSA controls for a Function
 * @dev: Pointer to the device against which allocations will be done.
 * @function: Pointer to the Function information.
 * @kctl: Array of ALSA controls to be populated.
 *
 * This function populates an array of ALSA controls from the DisCo
 * information for a particular SDCA Function. Typically,
 * snd_soc_asoc_count_component will be used to allocate an
 * appropriately sized array before calling this function.
 *
 * Return: Returns zero on success, and a negative error code on failure.
 */
int sdca_asoc_populate_controls(struct device *dev,
				struct sdca_function_data *function,
				struct snd_kcontrol_new *kctl)
{
	int i, j;
	int ret;

	for (i = 0; i < function->num_entities; i++) {
		struct sdca_entity *entity = &function->entities[i];

		switch (entity->type) {
		case SDCA_ENTITY_TYPE_IT:
		case SDCA_ENTITY_TYPE_OT:
			if (!entity->iot.is_dataport) {
				ret = populate_pin_switch(dev, entity, &kctl);
				if (ret)
					return ret;
			}
			break;
		default:
			break;
		}

		for (j = 0; j < entity->num_controls; j++) {
			ret = populate_control(dev, function, entity,
					       &entity->controls[j], &kctl);
			if (ret)
				return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL_NS(sdca_asoc_populate_controls, "SND_SOC_SDCA");

static unsigned int rate_find_mask(unsigned int rate)
{
	switch (rate) {
	case 0:
		return SNDRV_PCM_RATE_8000_768000;
	case 5512:
		return SNDRV_PCM_RATE_5512;
	case 8000:
		return SNDRV_PCM_RATE_8000;
	case 11025:
		return SNDRV_PCM_RATE_11025;
	case 16000:
		return SNDRV_PCM_RATE_16000;
	case 22050:
		return SNDRV_PCM_RATE_22050;
	case 32000:
		return SNDRV_PCM_RATE_32000;
	case 44100:
		return SNDRV_PCM_RATE_44100;
	case 48000:
		return SNDRV_PCM_RATE_48000;
	case 64000:
		return SNDRV_PCM_RATE_64000;
	case 88200:
		return SNDRV_PCM_RATE_88200;
	case 96000:
		return SNDRV_PCM_RATE_96000;
	case 176400:
		return SNDRV_PCM_RATE_176400;
	case 192000:
		return SNDRV_PCM_RATE_192000;
	case 352800:
		return SNDRV_PCM_RATE_352800;
	case 384000:
		return SNDRV_PCM_RATE_384000;
	case 705600:
		return SNDRV_PCM_RATE_705600;
	case 768000:
		return SNDRV_PCM_RATE_768000;
	case 12000:
		return SNDRV_PCM_RATE_12000;
	case 24000:
		return SNDRV_PCM_RATE_24000;
	case 128000:
		return SNDRV_PCM_RATE_128000;
	default:
		return 0;
	}
}

static u64 width_find_mask(unsigned int bits)
{
	switch (bits) {
	case 0:
		return SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_LE |
		       SNDRV_PCM_FMTBIT_S20_LE | SNDRV_PCM_FMTBIT_S24_LE |
		       SNDRV_PCM_FMTBIT_S32_LE;
	case 8:
		return SNDRV_PCM_FMTBIT_S8;
	case 16:
		return SNDRV_PCM_FMTBIT_S16_LE;
	case 20:
		return SNDRV_PCM_FMTBIT_S20_LE;
	case 24:
		return SNDRV_PCM_FMTBIT_S24_LE;
	case 32:
		return SNDRV_PCM_FMTBIT_S32_LE;
	default:
		return 0;
	}
}

static int populate_rate_format(struct device *dev,
				struct sdca_function_data *function,
				struct sdca_entity *entity,
				struct snd_soc_pcm_stream *stream)
{
	struct sdca_control_range *range;
	unsigned int sample_rate, sample_width;
	unsigned int clock_rates = 0;
	unsigned int rates = 0;
	u64 formats = 0;
	int sel, i;

	switch (entity->type) {
	case SDCA_ENTITY_TYPE_IT:
		sel = SDCA_CTL_IT_USAGE;
		break;
	case SDCA_ENTITY_TYPE_OT:
		sel = SDCA_CTL_OT_USAGE;
		break;
	default:
		dev_err(dev, "%s: entity type has no usage control\n",
			entity->label);
		return -EINVAL;
	}

	if (entity->iot.clock) {
		range = sdca_selector_find_range(dev, entity->iot.clock,
						 SDCA_CTL_CS_SAMPLERATEINDEX,
						 SDCA_SAMPLERATEINDEX_NCOLS, 0);
		if (!range)
			return -EINVAL;

		for (i = 0; i < range->rows; i++) {
			sample_rate = sdca_range(range, SDCA_SAMPLERATEINDEX_RATE, i);
			clock_rates |= rate_find_mask(sample_rate);
		}
	} else {
		clock_rates = UINT_MAX;
	}

	range = sdca_selector_find_range(dev, entity, sel, SDCA_USAGE_NCOLS, 0);
	if (!range)
		return -EINVAL;

	for (i = 0; i < range->rows; i++) {
		sample_rate = sdca_range(range, SDCA_USAGE_SAMPLE_RATE, i);
		sample_rate = rate_find_mask(sample_rate);

		if (sample_rate & clock_rates) {
			rates |= sample_rate;

			sample_width = sdca_range(range, SDCA_USAGE_SAMPLE_WIDTH, i);
			formats |= width_find_mask(sample_width);
		}
	}

	stream->formats = formats;
	stream->rates = rates;

	return 0;
}

/**
 * sdca_asoc_populate_dais - fill in an array of DAI drivers for a Function
 * @dev: Pointer to the device against which allocations will be done.
 * @function: Pointer to the Function information.
 * @dais: Array of DAI drivers to be populated.
 * @ops: DAI ops to be attached to each of the created DAI drivers.
 *
 * This function populates an array of ASoC DAI drivers from the DisCo
 * information for a particular SDCA Function. Typically,
 * snd_soc_asoc_count_component will be used to allocate an
 * appropriately sized array before calling this function.
 *
 * Return: Returns zero on success, and a negative error code on failure.
 */
int sdca_asoc_populate_dais(struct device *dev, struct sdca_function_data *function,
			    struct snd_soc_dai_driver *dais,
			    const struct snd_soc_dai_ops *ops)
{
	int i, j;
	int ret;

	for (i = 0, j = 0; i < function->num_entities - 1; i++) {
		struct sdca_entity *entity = &function->entities[i];
		struct snd_soc_pcm_stream *stream;
		const char *stream_suffix;

		switch (entity->type) {
		case SDCA_ENTITY_TYPE_IT:
			stream = &dais[j].playback;
			stream_suffix = "Playback";
			break;
		case SDCA_ENTITY_TYPE_OT:
			stream = &dais[j].capture;
			stream_suffix = "Capture";
			break;
		default:
			continue;
		}

		/* Can't check earlier as only terminals have an iot member. */
		if (!entity->iot.is_dataport)
			continue;

		stream->stream_name = devm_kasprintf(dev, GFP_KERNEL, "%s %s",
						     entity->label, stream_suffix);
		if (!stream->stream_name)
			return -ENOMEM;
		/* Channels will be further limited by constraints */
		stream->channels_min = 1;
		stream->channels_max = SDCA_MAX_CHANNEL_COUNT;

		ret = populate_rate_format(dev, function, entity, stream);
		if (ret)
			return ret;

		dais[j].id = i;
		dais[j].name = entity->label;
		dais[j].ops = ops;
		j++;
	}

	return 0;
}
EXPORT_SYMBOL_NS(sdca_asoc_populate_dais, "SND_SOC_SDCA");

/**
 * sdca_asoc_populate_component - fill in a component driver for a Function
 * @dev: Pointer to the device against which allocations will be done.
 * @function: Pointer to the Function information.
 * @component_drv: Pointer to the component driver to be populated.
 * @dai_drv: Pointer to the DAI driver array to be allocated and populated.
 * @num_dai_drv: Pointer to integer that will be populated with the number of
 * DAI drivers.
 * @ops: DAI ops pointer that will be used for each DAI driver.
 *
 * This function populates a snd_soc_component_driver structure based
 * on the DisCo information for a particular SDCA Function. It does
 * all allocation internally.
 *
 * Return: Returns zero on success, and a negative error code on failure.
 */
int sdca_asoc_populate_component(struct device *dev,
				 struct sdca_function_data *function,
				 struct snd_soc_component_driver *component_drv,
				 struct snd_soc_dai_driver **dai_drv, int *num_dai_drv,
				 const struct snd_soc_dai_ops *ops)
{
	struct snd_soc_dapm_widget *widgets;
	struct snd_soc_dapm_route *routes;
	struct snd_kcontrol_new *controls;
	struct snd_soc_dai_driver *dais;
	int num_widgets, num_routes, num_controls, num_dais;
	int ret;

	ret = sdca_asoc_count_component(dev, function, &num_widgets, &num_routes,
					&num_controls, &num_dais);
	if (ret)
		return ret;

	widgets = devm_kcalloc(dev, num_widgets, sizeof(*widgets), GFP_KERNEL);
	if (!widgets)
		return -ENOMEM;

	routes = devm_kcalloc(dev, num_routes, sizeof(*routes), GFP_KERNEL);
	if (!routes)
		return -ENOMEM;

	controls = devm_kcalloc(dev, num_controls, sizeof(*controls), GFP_KERNEL);
	if (!controls)
		return -ENOMEM;

	dais = devm_kcalloc(dev, num_dais, sizeof(*dais), GFP_KERNEL);
	if (!dais)
		return -ENOMEM;

	ret = sdca_asoc_populate_dapm(dev, function, widgets, routes);
	if (ret)
		return ret;

	ret = sdca_asoc_populate_controls(dev, function, controls);
	if (ret)
		return ret;

	ret = sdca_asoc_populate_dais(dev, function, dais, ops);
	if (ret)
		return ret;

	component_drv->dapm_widgets = widgets;
	component_drv->num_dapm_widgets = num_widgets;
	component_drv->dapm_routes = routes;
	component_drv->num_dapm_routes = num_routes;
	component_drv->controls = controls;
	component_drv->num_controls = num_controls;

	*dai_drv = dais;
	*num_dai_drv = num_dais;

	return 0;
}
EXPORT_SYMBOL_NS(sdca_asoc_populate_component, "SND_SOC_SDCA");

/**
 * sdca_asoc_set_constraints - constrain channels available on a DAI
 * @dev: Pointer to the device, used for error messages.
 * @regmap: Pointer to the Function register map.
 * @function: Pointer to the Function information.
 * @substream: Pointer to the PCM substream.
 * @dai: Pointer to the ASoC DAI.
 *
 * Typically called from startup().
 *
 * Return: Returns zero on success, and a negative error code on failure.
 */
int sdca_asoc_set_constraints(struct device *dev, struct regmap *regmap,
			      struct sdca_function_data *function,
			      struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	static const unsigned int channel_list[] = {
		 1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16,
		17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
	};
	struct sdca_entity *entity = &function->entities[dai->id];
	struct snd_pcm_hw_constraint_list *constraint;
	struct sdca_control_range *range;
	struct sdca_control *control;
	unsigned int channel_mask = 0;
	int i, ret;

	static_assert(ARRAY_SIZE(channel_list) == SDCA_MAX_CHANNEL_COUNT);
	static_assert(sizeof(channel_mask) * BITS_PER_BYTE >= SDCA_MAX_CHANNEL_COUNT);

	if (entity->type != SDCA_ENTITY_TYPE_IT)
		return 0;

	control = sdca_selector_find_control(dev, entity, SDCA_CTL_IT_CLUSTERINDEX);
	if (!control)
		return -EINVAL;

	range = sdca_control_find_range(dev, entity, control, SDCA_CLUSTER_NCOLS, 0);
	if (!range)
		return -EINVAL;

	for (i = 0; i < range->rows; i++) {
		int clusterid = sdca_range(range, SDCA_CLUSTER_CLUSTERID, i);
		struct sdca_cluster *cluster;

		cluster = sdca_id_find_cluster(dev, function, clusterid);
		if (!cluster)
			return -ENODEV;

		channel_mask |= (1 << (cluster->num_channels - 1));
	}

	dev_dbg(dev, "%s: set channel constraint mask: %#x\n",
		entity->label, channel_mask);

	constraint = kzalloc(sizeof(*constraint), GFP_KERNEL);
	if (!constraint)
		return -ENOMEM;

	constraint->count = ARRAY_SIZE(channel_list);
	constraint->list = channel_list;
	constraint->mask = channel_mask;

	ret = snd_pcm_hw_constraint_list(substream->runtime, 0,
					 SNDRV_PCM_HW_PARAM_CHANNELS,
					 constraint);
	if (ret) {
		dev_err(dev, "%s: failed to add constraint: %d\n", entity->label, ret);
		kfree(constraint);
		return ret;
	}

	dai->priv = constraint;

	return 0;
}
EXPORT_SYMBOL_NS(sdca_asoc_set_constraints, "SND_SOC_SDCA");

/**
 * sdca_asoc_free_constraints - free constraint allocations
 * @substream: Pointer to the PCM substream.
 * @dai: Pointer to the ASoC DAI.
 *
 * Typically called from shutdown().
 */
void sdca_asoc_free_constraints(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_pcm_hw_constraint_list *constraint = dai->priv;

	kfree(constraint);
}
EXPORT_SYMBOL_NS(sdca_asoc_free_constraints, "SND_SOC_SDCA");

/**
 * sdca_asoc_get_port - return SoundWire port for a DAI
 * @dev: Pointer to the device, used for error messages.
 * @regmap: Pointer to the Function register map.
 * @function: Pointer to the Function information.
 * @dai: Pointer to the ASoC DAI.
 *
 * Typically called from hw_params().
 *
 * Return: Returns a positive port number on success, and a negative error
 * code on failure.
 */
int sdca_asoc_get_port(struct device *dev, struct regmap *regmap,
		       struct sdca_function_data *function,
		       struct snd_soc_dai *dai)
{
	struct sdca_entity *entity = &function->entities[dai->id];
	struct sdca_control_range *range;
	unsigned int reg, val;
	int sel = -EINVAL;
	int i, ret;

	switch (entity->type) {
	case SDCA_ENTITY_TYPE_IT:
		sel = SDCA_CTL_IT_DATAPORT_SELECTOR;
		break;
	case SDCA_ENTITY_TYPE_OT:
		sel = SDCA_CTL_OT_DATAPORT_SELECTOR;
		break;
	default:
		break;
	}

	if (sel < 0 || !entity->iot.is_dataport) {
		dev_err(dev, "%s: port number only available for dataports\n",
			entity->label);
		return -EINVAL;
	}

	range = sdca_selector_find_range(dev, entity, sel, SDCA_DATAPORT_SELECTOR_NCOLS,
					 SDCA_DATAPORT_SELECTOR_NROWS);
	if (!range)
		return -EINVAL;

	reg = SDW_SDCA_CTL(function->desc->adr, entity->id, sel, 0);

	ret = regmap_read(regmap, reg, &val);
	if (ret) {
		dev_err(dev, "%s: failed to read dataport selector: %d\n",
			entity->label, ret);
		return ret;
	}

	for (i = 0; i < range->rows; i++) {
		static const u8 port_mask = 0xF;

		sel = sdca_range(range, val & port_mask, i);

		/*
		 * FIXME: Currently only a single dataport is supported, so
		 * return the first one found, technically up to 4 dataports
		 * could be linked, but this is not yet supported.
		 */
		if (sel != 0xFF)
			return sel;

		val >>= hweight8(port_mask);
	}

	dev_err(dev, "%s: no dataport found\n", entity->label);
	return -ENODEV;
}
EXPORT_SYMBOL_NS(sdca_asoc_get_port, "SND_SOC_SDCA");

static int set_cluster(struct device *dev, struct regmap *regmap,
		       struct sdca_function_data *function,
		       struct sdca_entity *entity, unsigned int channels)
{
	int sel = SDCA_CTL_IT_CLUSTERINDEX;
	struct sdca_control_range *range;
	int i, ret;

	range = sdca_selector_find_range(dev, entity, sel, SDCA_CLUSTER_NCOLS, 0);
	if (!range)
		return -EINVAL;

	for (i = 0; i < range->rows; i++) {
		int cluster_id = sdca_range(range, SDCA_CLUSTER_CLUSTERID, i);
		struct sdca_cluster *cluster;

		cluster = sdca_id_find_cluster(dev, function, cluster_id);
		if (!cluster)
			return -ENODEV;

		if (cluster->num_channels == channels) {
			int index = sdca_range(range, SDCA_CLUSTER_BYTEINDEX, i);
			unsigned int reg = SDW_SDCA_CTL(function->desc->adr,
							entity->id, sel, 0);

			ret = regmap_update_bits(regmap, reg, 0xFF, index);
			if (ret) {
				dev_err(dev, "%s: failed to write cluster index: %d\n",
					entity->label, ret);
				return ret;
			}

			dev_dbg(dev, "%s: set cluster to %d (%d channels)\n",
				entity->label, index, channels);

			return 0;
		}
	}

	dev_err(dev, "%s: no cluster for %d channels\n", entity->label, channels);
	return -EINVAL;
}

static int set_clock(struct device *dev, struct regmap *regmap,
		     struct sdca_function_data *function,
		     struct sdca_entity *entity, int target_rate)
{
	int sel = SDCA_CTL_CS_SAMPLERATEINDEX;
	struct sdca_control_range *range;
	int i, ret;

	range = sdca_selector_find_range(dev, entity, sel, SDCA_SAMPLERATEINDEX_NCOLS, 0);
	if (!range)
		return -EINVAL;

	for (i = 0; i < range->rows; i++) {
		unsigned int rate = sdca_range(range, SDCA_SAMPLERATEINDEX_RATE, i);

		if (rate == target_rate) {
			unsigned int index = sdca_range(range,
							SDCA_SAMPLERATEINDEX_INDEX,
							i);
			unsigned int reg = SDW_SDCA_CTL(function->desc->adr,
							entity->id, sel, 0);

			ret = regmap_update_bits(regmap, reg, 0xFF, index);
			if (ret) {
				dev_err(dev, "%s: failed to write clock rate: %d\n",
					entity->label, ret);
				return ret;
			}

			dev_dbg(dev, "%s: set clock rate to %d (%dHz)\n",
				entity->label, index, rate);

			return 0;
		}
	}

	dev_err(dev, "%s: no clock rate for %dHz\n", entity->label, target_rate);
	return -EINVAL;
}

static int set_usage(struct device *dev, struct regmap *regmap,
		     struct sdca_function_data *function,
		     struct sdca_entity *entity, int sel,
		     int target_rate, int target_width)
{
	struct sdca_control_range *range;
	int i, ret;

	range = sdca_selector_find_range(dev, entity, sel, SDCA_USAGE_NCOLS, 0);
	if (!range)
		return -EINVAL;

	for (i = 0; i < range->rows; i++) {
		unsigned int rate = sdca_range(range, SDCA_USAGE_SAMPLE_RATE, i);
		unsigned int width = sdca_range(range, SDCA_USAGE_SAMPLE_WIDTH, i);

		if ((!rate || rate == target_rate) && width == target_width) {
			unsigned int usage = sdca_range(range, SDCA_USAGE_NUMBER, i);
			unsigned int reg = SDW_SDCA_CTL(function->desc->adr,
							entity->id, sel, 0);

			ret = regmap_update_bits(regmap, reg, 0xFF, usage);
			if (ret) {
				dev_err(dev, "%s: failed to write usage: %d\n",
					entity->label, ret);
				return ret;
			}

			dev_dbg(dev, "%s: set usage to %#x (%dHz, %d bits)\n",
				entity->label, usage, target_rate, target_width);

			return 0;
		}
	}

	dev_err(dev, "%s: no usage for %dHz, %dbits\n",
		entity->label, target_rate, target_width);
	return -EINVAL;
}

/**
 * sdca_asoc_hw_params - set SDCA channels, sample rate and bit depth
 * @dev: Pointer to the device, used for error messages.
 * @regmap: Pointer to the Function register map.
 * @function: Pointer to the Function information.
 * @substream: Pointer to the PCM substream.
 * @params: Pointer to the hardware parameters.
 * @dai: Pointer to the ASoC DAI.
 *
 * Typically called from hw_params().
 *
 * Return: Returns zero on success, and a negative error code on failure.
 */
int sdca_asoc_hw_params(struct device *dev, struct regmap *regmap,
			struct sdca_function_data *function,
			struct snd_pcm_substream *substream,
			struct snd_pcm_hw_params *params,
			struct snd_soc_dai *dai)
{
	struct sdca_entity *entity = &function->entities[dai->id];
	int channels = params_channels(params);
	int width = params_width(params);
	int rate = params_rate(params);
	int usage_sel;
	int ret;

	switch (entity->type) {
	case SDCA_ENTITY_TYPE_IT:
		ret = set_cluster(dev, regmap, function, entity, channels);
		if (ret)
			return ret;

		usage_sel = SDCA_CTL_IT_USAGE;
		break;
	case SDCA_ENTITY_TYPE_OT:
		usage_sel = SDCA_CTL_OT_USAGE;
		break;
	default:
		dev_err(dev, "%s: hw_params on non-terminal entity\n", entity->label);
		return -EINVAL;
	}

	if (entity->iot.clock) {
		ret = set_clock(dev, regmap, function, entity->iot.clock, rate);
		if (ret)
			return ret;
	}

	ret = set_usage(dev, regmap, function, entity, usage_sel, rate, width);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL_NS(sdca_asoc_hw_params, "SND_SOC_SDCA");
