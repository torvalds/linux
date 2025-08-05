// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2025 Cirrus Logic, Inc. and
//                    Cirrus Logic International Semiconductor Ltd.

/*
 * The MIPI SDCA specification is available for public downloads at
 * https://www.mipi.org/mipi-sdca-v1-0-download
 */

#include <linux/bitmap.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/regmap.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_registers.h>
#include <sound/sdca.h>
#include <sound/sdca_function.h>
#include <sound/sdca_interrupts.h>
#include <sound/soc-component.h>
#include <sound/soc.h>

#define IRQ_SDCA(number) REGMAP_IRQ_REG(number, ((number) / BITS_PER_BYTE), \
					SDW_SCP_SDCA_INTMASK_SDCA_##number)

static const struct regmap_irq regmap_irqs[SDCA_MAX_INTERRUPTS] = {
	IRQ_SDCA(0),
	IRQ_SDCA(1),
	IRQ_SDCA(2),
	IRQ_SDCA(3),
	IRQ_SDCA(4),
	IRQ_SDCA(5),
	IRQ_SDCA(6),
	IRQ_SDCA(7),
	IRQ_SDCA(8),
	IRQ_SDCA(9),
	IRQ_SDCA(10),
	IRQ_SDCA(11),
	IRQ_SDCA(12),
	IRQ_SDCA(13),
	IRQ_SDCA(14),
	IRQ_SDCA(15),
	IRQ_SDCA(16),
	IRQ_SDCA(17),
	IRQ_SDCA(18),
	IRQ_SDCA(19),
	IRQ_SDCA(20),
	IRQ_SDCA(21),
	IRQ_SDCA(22),
	IRQ_SDCA(23),
	IRQ_SDCA(24),
	IRQ_SDCA(25),
	IRQ_SDCA(26),
	IRQ_SDCA(27),
	IRQ_SDCA(28),
	IRQ_SDCA(29),
	IRQ_SDCA(30),
};

static const struct regmap_irq_chip sdca_irq_chip = {
	.name = "sdca_irq",

	.status_base = SDW_SCP_SDCA_INT1,
	.unmask_base = SDW_SCP_SDCA_INTMASK1,
	.ack_base = SDW_SCP_SDCA_INT1,
	.num_regs = 4,

	.irqs = regmap_irqs,
	.num_irqs = SDCA_MAX_INTERRUPTS,

	.runtime_pm = true,
};

static irqreturn_t base_handler(int irq, void *data)
{
	struct sdca_interrupt *interrupt = data;
	struct device *dev = interrupt->component->dev;

	dev_info(dev, "%s irq without full handling\n", interrupt->name);

	return IRQ_HANDLED;
}

static irqreturn_t function_status_handler(int irq, void *data)
{
	struct sdca_interrupt *interrupt = data;
	struct device *dev = interrupt->component->dev;
	unsigned int reg, val;
	unsigned long status;
	unsigned int mask;
	int ret;

	reg = SDW_SDCA_CTL(interrupt->function->desc->adr, interrupt->entity->id,
			   interrupt->control->sel, 0);

	ret = regmap_read(interrupt->component->regmap, reg, &val);
	if (ret < 0) {
		dev_err(dev, "failed to read function status: %d\n", ret);
		return IRQ_NONE;
	}

	dev_dbg(dev, "function status: %#x\n", val);

	status = val;
	for_each_set_bit(mask, &status, BITS_PER_BYTE) {
		mask = 1 << mask;

		switch (mask) {
		case SDCA_CTL_ENTITY_0_FUNCTION_NEEDS_INITIALIZATION:
			//FIXME: Add init writes
			break;
		case SDCA_CTL_ENTITY_0_FUNCTION_FAULT:
			dev_err(dev, "function fault\n");
			break;
		case SDCA_CTL_ENTITY_0_UMP_SEQUENCE_FAULT:
			dev_err(dev, "ump sequence fault\n");
			break;
		case SDCA_CTL_ENTITY_0_FUNCTION_BUSY:
			dev_info(dev, "unexpected function busy\n");
			break;
		case SDCA_CTL_ENTITY_0_DEVICE_NEWLY_ATTACHED:
		case SDCA_CTL_ENTITY_0_INTS_DISABLED_ABNORMALLY:
		case SDCA_CTL_ENTITY_0_STREAMING_STOPPED_ABNORMALLY:
		case SDCA_CTL_ENTITY_0_FUNCTION_HAS_BEEN_RESET:
			break;
		}
	}

	ret = regmap_write(interrupt->component->regmap, reg, val);
	if (ret < 0) {
		dev_err(dev, "failed to clear function status: %d\n", ret);
		return IRQ_NONE;
	}

	return IRQ_HANDLED;
}

static irqreturn_t detected_mode_handler(int irq, void *data)
{
	struct sdca_interrupt *interrupt = data;
	struct snd_soc_component *component = interrupt->component;
	struct device *dev = component->dev;
	struct snd_soc_card *card = component->card;
	struct rw_semaphore *rwsem = &card->snd_card->controls_rwsem;
	struct snd_kcontrol *kctl = interrupt->priv;
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
			return IRQ_NONE;
		}

		interrupt->priv = kctl;
	}

	soc_enum = (struct soc_enum *)kctl->private_value;

	reg = SDW_SDCA_CTL(interrupt->function->desc->adr, interrupt->entity->id,
			   interrupt->control->sel, 0);

	ret = regmap_read(component->regmap, reg, &val);
	if (ret < 0) {
		dev_err(dev, "failed to read detected mode: %d\n", ret);
		return IRQ_NONE;
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
		regcache_drop_region(component->regmap, reg, reg);

		ret = regmap_read(component->regmap, reg, &val);
		if (ret) {
			dev_err(dev, "failed to re-check selected mode: %d\n", ret);
			return IRQ_NONE;
		}
		break;
	default:
		break;
	}

	dev_dbg(dev, "%s: %#x\n", interrupt->name, val);

	ucontrol = kzalloc(sizeof(*ucontrol), GFP_KERNEL);
	if (!ucontrol)
		return IRQ_NONE;

	ucontrol->value.enumerated.item[0] = snd_soc_enum_val_to_item(soc_enum, val);

	down_write(rwsem);
	ret = kctl->put(kctl, ucontrol);
	up_write(rwsem);
	if (ret < 0) {
		dev_err(dev, "failed to update selected mode: %d\n", ret);
		return IRQ_NONE;
	}

	snd_ctl_notify(card->snd_card, SNDRV_CTL_EVENT_MASK_VALUE, &kctl->id);

	return IRQ_HANDLED;
}

static int sdca_irq_request_locked(struct device *dev,
				   struct sdca_interrupt_info *info,
				   int sdca_irq, const char *name,
				   irq_handler_t handler, void *data)
{
	int irq;
	int ret;

	irq = regmap_irq_get_virq(info->irq_data, sdca_irq);
	if (irq < 0)
		return irq;

	ret = devm_request_threaded_irq(dev, irq, NULL, handler,
					IRQF_ONESHOT, name, data);
	if (ret)
		return ret;

	dev_dbg(dev, "requested irq %d for %s\n", irq, name);

	return 0;
}

/**
 * sdca_request_irq - request an individual SDCA interrupt
 * @dev: Pointer to the struct device against which things should be allocated.
 * @interrupt_info: Pointer to the interrupt information structure.
 * @sdca_irq: SDCA interrupt position.
 * @name: Name to be given to the IRQ.
 * @handler: A callback thread function to be called for the IRQ.
 * @data: Private data pointer that will be passed to the handler.
 *
 * Typically this is handled internally by sdca_irq_populate, however if
 * a device requires custom IRQ handling this can be called manually before
 * calling sdca_irq_populate, which will then skip that IRQ whilst processing.
 *
 * Return: Zero on success, and a negative error code on failure.
 */
int sdca_irq_request(struct device *dev, struct sdca_interrupt_info *info,
		     int sdca_irq, const char *name, irq_handler_t handler,
		     void *data)
{
	int ret;

	if (sdca_irq < 0 || sdca_irq >= SDCA_MAX_INTERRUPTS) {
		dev_err(dev, "bad irq request: %d\n", sdca_irq);
		return -EINVAL;
	}

	guard(mutex)(&info->irq_lock);

	ret = sdca_irq_request_locked(dev, info, sdca_irq, name, handler, data);
	if (ret) {
		dev_err(dev, "failed to request irq %s: %d\n", name, ret);
		return ret;
	}

	info->irqs[sdca_irq].externally_requested = true;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(sdca_irq_request, "SND_SOC_SDCA");

/**
 * sdca_irq_data_populate - Populate common interrupt data
 * @component: Pointer to the ASoC component for the Function.
 * @function: Pointer to the SDCA Function.
 * @entity: Pointer to the SDCA Entity.
 * @control: Pointer to the SDCA Control.
 * @interrupt: Pointer to the SDCA interrupt for this IRQ.
 *
 * Return: Zero on success, and a negative error code on failure.
 */
int sdca_irq_data_populate(struct snd_soc_component *component,
			   struct sdca_function_data *function,
			   struct sdca_entity *entity,
			   struct sdca_control *control,
			   struct sdca_interrupt *interrupt)
{
	struct device *dev = component->dev;
	const char *name;

	name = devm_kasprintf(dev, GFP_KERNEL, "%s %s %s", function->desc->name,
			      entity->label, control->label);
	if (!name)
		return -ENOMEM;

	interrupt->name = name;
	interrupt->component = component;
	interrupt->function = function;
	interrupt->entity = entity;
	interrupt->control = control;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(sdca_irq_data_populate, "SND_SOC_SDCA");

/**
 * sdca_irq_populate - Request all the individual IRQs for an SDCA Function
 * @function: Pointer to the SDCA Function.
 * @component: Pointer to the ASoC component for the Function.
 * @info: Pointer to the SDCA interrupt info for this device.
 *
 * Typically this would be called from the driver for a single SDCA Function.
 *
 * Return: Zero on success, and a negative error code on failure.
 */
int sdca_irq_populate(struct sdca_function_data *function,
		      struct snd_soc_component *component,
		      struct sdca_interrupt_info *info)
{
	struct device *dev = component->dev;
	int i, j;

	guard(mutex)(&info->irq_lock);

	for (i = 0; i < function->num_entities; i++) {
		struct sdca_entity *entity = &function->entities[i];

		for (j = 0; j < entity->num_controls; j++) {
			struct sdca_control *control = &entity->controls[j];
			int irq = control->interrupt_position;
			struct sdca_interrupt *interrupt;
			irq_handler_t handler;
			int ret;

			if (irq == SDCA_NO_INTERRUPT) {
				continue;
			} else if (irq < 0 || irq >= SDCA_MAX_INTERRUPTS) {
				dev_err(dev, "bad irq position: %d\n", irq);
				return -EINVAL;
			}

			interrupt = &info->irqs[irq];

			if (interrupt->externally_requested) {
				dev_dbg(dev,
					"skipping irq %d, externally requested\n",
					irq);
				continue;
			}

			ret = sdca_irq_data_populate(component, function, entity,
						     control, interrupt);
			if (ret)
				return ret;

			handler = base_handler;

			switch (entity->type) {
			case SDCA_ENTITY_TYPE_ENTITY_0:
				if (control->sel == SDCA_CTL_ENTITY_0_FUNCTION_STATUS)
					handler = function_status_handler;
				break;
			case SDCA_ENTITY_TYPE_GE:
				if (control->sel == SDCA_CTL_GE_DETECTED_MODE)
					handler = detected_mode_handler;
				break;
			default:
				break;
			}

			ret = sdca_irq_request_locked(dev, info, irq, interrupt->name,
						      handler, interrupt);
			if (ret) {
				dev_err(dev, "failed to request irq %s: %d\n",
					interrupt->name, ret);
				return ret;
			}
		}
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(sdca_irq_populate, "SND_SOC_SDCA");

/**
 * sdca_irq_allocate - allocate an SDCA interrupt structure for a device
 * @dev: Device pointer against which things should be allocated.
 * @regmap: regmap to be used for accessing the SDCA IRQ registers.
 * @irq: The interrupt number.
 *
 * Typically this would be called from the top level driver for the whole
 * SDCA device, as only a single instance is required across all Functions
 * on the device.
 *
 * Return: A pointer to the allocated sdca_interrupt_info struct, or an
 * error code.
 */
struct sdca_interrupt_info *sdca_irq_allocate(struct device *dev,
					      struct regmap *regmap, int irq)
{
	struct sdca_interrupt_info *info;
	int ret;

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);

	info->irq_chip = sdca_irq_chip;

	ret = devm_mutex_init(dev, &info->irq_lock);
	if (ret)
		return ERR_PTR(ret);

	ret = devm_regmap_add_irq_chip(dev, regmap, irq, IRQF_ONESHOT, 0,
				       &info->irq_chip, &info->irq_data);
	if (ret) {
		dev_err(dev, "failed to register irq chip: %d\n", ret);
		return ERR_PTR(ret);
	}

	dev_dbg(dev, "registered on irq %d\n", irq);

	return info;
}
EXPORT_SYMBOL_NS_GPL(sdca_irq_allocate, "SND_SOC_SDCA");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SDCA IRQ library");
