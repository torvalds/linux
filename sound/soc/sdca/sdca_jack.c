// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2025 Cirrus Logic, Inc. and
//                    Cirrus Logic International Semiconductor Ltd.

/*
 * The MIPI SDCA specification is available for public downloads at
 * https://www.mipi.org/mipi-sdca-v1-0-download
 */

#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/dev_printk.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/sprintf.h>
#include <linux/regmap.h>
#include <linux/rwsem.h>
#include <sound/asound.h>
#include <sound/control.h>
#include <sound/jack.h>
#include <sound/sdca.h>
#include <sound/sdca_function.h>
#include <sound/sdca_interrupts.h>
#include <sound/sdca_jack.h>
#include <sound/soc-component.h>
#include <sound/soc-jack.h>
#include <sound/soc.h>

/**
 * sdca_jack_process - Process an SDCA jack event
 * @interrupt: SDCA interrupt structure
 *
 * Return: Zero on success or a negative error code.
 */
int sdca_jack_process(struct sdca_interrupt *interrupt)
{
	struct device *dev = interrupt->dev;
	struct snd_soc_component *component = interrupt->component;
	struct snd_soc_card *card = component->card;
	struct rw_semaphore *rwsem = &card->snd_card->controls_rwsem;
	struct jack_state *state = interrupt->priv;
	struct snd_kcontrol *kctl = state->kctl;
	struct snd_ctl_elem_value *ucontrol __free(kfree) = NULL;
	unsigned int reg, val;
	int ret;

	guard(rwsem_write)(rwsem);

	if (!kctl) {
		const char *name __free(kfree) = kasprintf(GFP_KERNEL, "%s %s",
							   interrupt->entity->label,
							   SDCA_CTL_SELECTED_MODE_NAME);

		if (!name)
			return -ENOMEM;

		kctl = snd_soc_component_get_kcontrol(component, name);
		if (!kctl)
			dev_dbg(dev, "control not found: %s\n", name);
		else
			state->kctl = kctl;
	}

	reg = SDW_SDCA_CTL(interrupt->function->desc->adr, interrupt->entity->id,
			   interrupt->control->sel, 0);

	ret = regmap_read(interrupt->function_regmap, reg, &val);
	if (ret < 0) {
		dev_err(dev, "failed to read detected mode: %d\n", ret);
		return ret;
	}

	reg = SDW_SDCA_CTL(interrupt->function->desc->adr, interrupt->entity->id,
			   SDCA_CTL_GE_SELECTED_MODE, 0);

	switch (val) {
	case SDCA_DETECTED_MODE_DETECTION_IN_PROGRESS:
	case SDCA_DETECTED_MODE_JACK_UNKNOWN:
		/*
		 * Selected mode is not normally marked as volatile register
		 * (RW), but here force a read from the hardware. If the
		 * detected mode is unknown we need to see what the device
		 * selected as a "safe" option.
		 */
		regcache_drop_region(interrupt->function_regmap, reg, reg);

		ret = regmap_read(interrupt->function_regmap, reg, &val);
		if (ret) {
			dev_err(dev, "failed to re-check selected mode: %d\n", ret);
			return ret;
		}
		break;
	default:
		break;
	}

	dev_dbg(dev, "%s: %#x\n", interrupt->name, val);

	if (kctl) {
		struct soc_enum *soc_enum = (struct soc_enum *)kctl->private_value;

		ucontrol = kzalloc(sizeof(*ucontrol), GFP_KERNEL);
		if (!ucontrol)
			return -ENOMEM;

		ucontrol->value.enumerated.item[0] = snd_soc_enum_val_to_item(soc_enum, val);

		ret = snd_soc_dapm_put_enum_double(kctl, ucontrol);
		if (ret < 0) {
			dev_err(dev, "failed to update selected mode: %d\n", ret);
			return ret;
		}

		snd_ctl_notify(card->snd_card, SNDRV_CTL_EVENT_MASK_VALUE, &kctl->id);
	} else {
		ret = regmap_write(interrupt->function_regmap, reg, val);
		if (ret) {
			dev_err(dev, "failed to write selected mode: %d\n", ret);
			return ret;
		}
	}

	return sdca_jack_report(interrupt);
}
EXPORT_SYMBOL_NS_GPL(sdca_jack_process, "SND_SOC_SDCA");

/**
 * sdca_jack_alloc_state - allocate state for a jack interrupt
 * @interrupt: SDCA interrupt structure.
 *
 * Return: Zero on success or a negative error code.
 */
int sdca_jack_alloc_state(struct sdca_interrupt *interrupt)
{
	struct device *dev = interrupt->dev;
	struct jack_state *jack_state;

	jack_state = devm_kzalloc(dev, sizeof(*jack_state), GFP_KERNEL);
	if (!jack_state)
		return -ENOMEM;

	interrupt->priv = jack_state;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(sdca_jack_alloc_state, "SND_SOC_SDCA");

/**
 * sdca_jack_set_jack - attach an ASoC jack to SDCA
 * @info: SDCA interrupt information.
 * @jack: ASoC jack to be attached.
 *
 * Return: Zero on success or a negative error code.
 */
int sdca_jack_set_jack(struct sdca_interrupt_info *info, struct snd_soc_jack *jack)
{
	int i, ret;

	guard(mutex)(&info->irq_lock);

	for (i = 0; i < SDCA_MAX_INTERRUPTS; i++) {
		struct sdca_interrupt *interrupt = &info->irqs[i];
		struct sdca_control *control = interrupt->control;
		struct sdca_entity *entity = interrupt->entity;
		struct jack_state *jack_state;

		if (!interrupt->irq)
			continue;

		switch (SDCA_CTL_TYPE(entity->type, control->sel)) {
		case SDCA_CTL_TYPE_S(GE, DETECTED_MODE):
			jack_state = interrupt->priv;
			jack_state->jack = jack;

			/* Report initial state in case IRQ was already handled */
			ret = sdca_jack_report(interrupt);
			if (ret)
				return ret;
			break;
		default:
			break;
		}
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(sdca_jack_set_jack, "SND_SOC_SDCA");

int sdca_jack_report(struct sdca_interrupt *interrupt)
{
	struct jack_state *jack_state = interrupt->priv;
	struct sdca_control_range *range;
	enum sdca_terminal_type type;
	unsigned int report = 0;
	unsigned int reg, val;
	int ret;

	reg = SDW_SDCA_CTL(interrupt->function->desc->adr, interrupt->entity->id,
			   SDCA_CTL_GE_SELECTED_MODE, 0);

	ret = regmap_read(interrupt->function_regmap, reg, &val);
	if (ret) {
		dev_err(interrupt->dev, "failed to read selected mode: %d\n", ret);
		return ret;
	}

	range = sdca_selector_find_range(interrupt->dev, interrupt->entity,
					 SDCA_CTL_GE_SELECTED_MODE,
					 SDCA_SELECTED_MODE_NCOLS, 0);
	if (!range)
		return -EINVAL;

	type = sdca_range_search(range, SDCA_SELECTED_MODE_INDEX,
				 val, SDCA_SELECTED_MODE_TERM_TYPE);

	switch (type) {
	case SDCA_TERM_TYPE_LINEIN_STEREO:
	case SDCA_TERM_TYPE_LINEIN_FRONT_LR:
	case SDCA_TERM_TYPE_LINEIN_CENTER_LFE:
	case SDCA_TERM_TYPE_LINEIN_SURROUND_LR:
	case SDCA_TERM_TYPE_LINEIN_REAR_LR:
		report = SND_JACK_LINEIN;
		break;
	case SDCA_TERM_TYPE_LINEOUT_STEREO:
	case SDCA_TERM_TYPE_LINEOUT_FRONT_LR:
	case SDCA_TERM_TYPE_LINEOUT_CENTER_LFE:
	case SDCA_TERM_TYPE_LINEOUT_SURROUND_LR:
	case SDCA_TERM_TYPE_LINEOUT_REAR_LR:
		report = SND_JACK_LINEOUT;
		break;
	case SDCA_TERM_TYPE_MIC_JACK:
		report = SND_JACK_MICROPHONE;
		break;
	case SDCA_TERM_TYPE_HEADPHONE_JACK:
		report = SND_JACK_HEADPHONE;
		break;
	case SDCA_TERM_TYPE_HEADSET_JACK:
		report = SND_JACK_HEADSET;
		break;
	default:
		break;
	}

	snd_soc_jack_report(jack_state->jack, report, 0xFFFF);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(sdca_jack_report, "SND_SOC_SDCA");
