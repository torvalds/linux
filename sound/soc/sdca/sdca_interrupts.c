// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2025 Cirrus Logic, Inc. and
//                    Cirrus Logic International Semiconductor Ltd.

/*
 * The MIPI SDCA specification is available for public downloads at
 * https://www.mipi.org/mipi-sdca-v1-0-download
 */

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

	if (sdca_irq < 0 || sdca_irq > SDCA_MAX_INTERRUPTS) {
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
EXPORT_SYMBOL_NS_GPL(sdca_irq_request, "SND_SOC_SDCA_IRQ");

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
EXPORT_SYMBOL_NS_GPL(sdca_irq_data_populate, "SND_SOC_SDCA_IRQ");

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
			const char *name;
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

			ret = sdca_irq_request_locked(dev, info, irq, interrupt->name,
						      base_handler, interrupt);
			if (ret) {
				dev_err(dev, "failed to request irq %s: %d\n",
					name, ret);
				return ret;
			}
		}
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(sdca_irq_populate, "SND_SOC_SDCA_IRQ");

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

	devm_mutex_init(dev, &info->irq_lock);

	ret = devm_regmap_add_irq_chip(dev, regmap, irq, IRQF_ONESHOT, 0,
				       &info->irq_chip, &info->irq_data);
	if (ret) {
		dev_err(dev, "failed to register irq chip: %d\n", ret);
		return ERR_PTR(ret);
	}

	dev_dbg(dev, "registered on irq %d\n", irq);

	return info;
}
EXPORT_SYMBOL_NS_GPL(sdca_irq_allocate, "SND_SOC_SDCA_IRQ");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SDCA IRQ library");
