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
#include <sound/sdca.h>
#include <sound/sdca_function.h>
#include <sound/sdca_interrupts.h>
#include <sound/sdca_jack.h>
#include <sound/soc-component.h>
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
	struct soc_enum *soc_enum;
	unsigned int reg, val;
	int ret;

	if (!kctl) {
		const char *name __free(kfree) = kasprintf(GFP_KERNEL, "%s %s",
							   interrupt->entity->label,
							   SDCA_CTL_SELECTED_MODE_NAME);

		if (!name)
			return -ENOMEM;

		kctl = snd_soc_component_get_kcontrol(component, name);
		if (!kctl) {
			dev_dbg(dev, "control not found: %s\n", name);
			return -ENOENT;
		}

		state->kctl = kctl;
	}

	soc_enum = (struct soc_enum *)kctl->private_value;

	reg = SDW_SDCA_CTL(interrupt->function->desc->adr, interrupt->entity->id,
			   interrupt->control->sel, 0);

	ret = regmap_read(interrupt->function_regmap, reg, &val);
	if (ret < 0) {
		dev_err(dev, "failed to read detected mode: %d\n", ret);
		return ret;
	}

	switch (val) {
	case SDCA_DETECTED_MODE_DETECTION_IN_PROGRESS:
	case SDCA_DETECTED_MODE_JACK_UNKNOWN:
		reg = SDW_SDCA_CTL(interrupt->function->desc->adr,
				   interrupt->entity->id,
				   SDCA_CTL_GE_SELECTED_MODE, 0);

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

	ucontrol = kzalloc(sizeof(*ucontrol), GFP_KERNEL);
	if (!ucontrol)
		return -ENOMEM;

	ucontrol->value.enumerated.item[0] = snd_soc_enum_val_to_item(soc_enum, val);

	down_write(rwsem);
	ret = kctl->put(kctl, ucontrol);
	up_write(rwsem);
	if (ret < 0) {
		dev_err(dev, "failed to update selected mode: %d\n", ret);
		return ret;
	}

	snd_ctl_notify(card->snd_card, SNDRV_CTL_EVENT_MASK_VALUE, &kctl->id);

	return 0;
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
