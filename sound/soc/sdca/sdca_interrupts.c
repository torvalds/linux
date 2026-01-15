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
#include <linux/dev_printk.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_registers.h>
#include <sound/sdca.h>
#include <sound/sdca_fdl.h>
#include <sound/sdca_function.h>
#include <sound/sdca_hid.h>
#include <sound/sdca_interrupts.h>
#include <sound/sdca_jack.h>
#include <sound/sdca_ump.h>
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
	struct device *dev = interrupt->dev;

	dev_info(dev, "%s irq without full handling\n", interrupt->name);

	return IRQ_HANDLED;
}

static irqreturn_t function_status_handler(int irq, void *data)
{
	struct sdca_interrupt *interrupt = data;
	struct device *dev = interrupt->dev;
	irqreturn_t irqret = IRQ_NONE;
	unsigned int reg, val;
	unsigned long status;
	unsigned int mask;
	int ret;

	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		dev_err(dev, "failed to resume for function status: %d\n", ret);
		goto error;
	}

	reg = SDW_SDCA_CTL(interrupt->function->desc->adr, interrupt->entity->id,
			   interrupt->control->sel, 0);

	ret = regmap_read(interrupt->function_regmap, reg, &val);
	if (ret < 0) {
		dev_err(dev, "failed to read function status: %d\n", ret);
		goto error;
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

	ret = regmap_write(interrupt->function_regmap, reg, val);
	if (ret < 0) {
		dev_err(dev, "failed to clear function status: %d\n", ret);
		goto error;
	}

	irqret = IRQ_HANDLED;
error:
	pm_runtime_put(dev);
	return irqret;
}

static irqreturn_t detected_mode_handler(int irq, void *data)
{
	struct sdca_interrupt *interrupt = data;
	struct device *dev = interrupt->dev;
	irqreturn_t irqret = IRQ_NONE;
	int ret;

	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		dev_err(dev, "failed to resume for detected mode: %d\n", ret);
		goto error;
	}

	ret = sdca_jack_process(interrupt);
	if (ret)
		goto error;

	irqret = IRQ_HANDLED;
error:
	pm_runtime_put(dev);
	return irqret;
}

static irqreturn_t hid_handler(int irq, void *data)
{
	struct sdca_interrupt *interrupt = data;
	struct device *dev = interrupt->dev;
	irqreturn_t irqret = IRQ_NONE;
	int ret;

	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		dev_err(dev, "failed to resume for hid: %d\n", ret);
		goto error;
	}

	ret = sdca_hid_process_report(interrupt);
	if (ret)
		goto error;

	irqret = IRQ_HANDLED;
error:
	pm_runtime_put(dev);
	return irqret;
}

#ifdef CONFIG_PM_SLEEP
static bool no_pm_in_progress(struct device *dev)
{
	return completion_done(&dev->power.completion);
}
#else
static bool no_pm_in_progress(struct device *dev)
{
	return true;
}
#endif

static irqreturn_t fdl_owner_handler(int irq, void *data)
{
	struct sdca_interrupt *interrupt = data;
	struct device *dev = interrupt->dev;
	irqreturn_t irqret = IRQ_NONE;
	int ret;

	/*
	 * FDL has to run from the system resume handler, at which point
	 * we can't wait for the pm runtime.
	 */
	if (no_pm_in_progress(dev)) {
		ret = pm_runtime_get_sync(dev);
		if (ret < 0) {
			dev_err(dev, "failed to resume for fdl: %d\n", ret);
			goto error;
		}
	}

	ret = sdca_fdl_process(interrupt);
	if (ret)
		goto error;

	irqret = IRQ_HANDLED;
error:
	if (no_pm_in_progress(dev))
		pm_runtime_put(dev);
	return irqret;
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

	info->irqs[sdca_irq].irq = irq;

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

	return 0;
}
EXPORT_SYMBOL_NS_GPL(sdca_irq_request, "SND_SOC_SDCA");

/**
 * sdca_irq_data_populate - Populate common interrupt data
 * @dev: Pointer to the Function device.
 * @regmap: Pointer to the Function regmap.
 * @component: Pointer to the ASoC component for the Function.
 * @function: Pointer to the SDCA Function.
 * @entity: Pointer to the SDCA Entity.
 * @control: Pointer to the SDCA Control.
 * @interrupt: Pointer to the SDCA interrupt for this IRQ.
 *
 * Return: Zero on success, and a negative error code on failure.
 */
int sdca_irq_data_populate(struct device *dev, struct regmap *regmap,
			   struct snd_soc_component *component,
			   struct sdca_function_data *function,
			   struct sdca_entity *entity,
			   struct sdca_control *control,
			   struct sdca_interrupt *interrupt)
{
	const char *name;

	if (!dev && component)
		dev = component->dev;
	if (!dev)
		return -ENODEV;

	name = devm_kasprintf(dev, GFP_KERNEL, "%s %s %s", function->desc->name,
			      entity->label, control->label);
	if (!name)
		return -ENOMEM;

	interrupt->name = name;
	interrupt->dev = dev;
	if (!regmap && component)
		interrupt->function_regmap = component->regmap;
	else
		interrupt->function_regmap = regmap;
	interrupt->component = component;
	interrupt->function = function;
	interrupt->entity = entity;
	interrupt->control = control;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(sdca_irq_data_populate, "SND_SOC_SDCA");

static struct sdca_interrupt *get_interrupt_data(struct device *dev, int irq,
						 struct sdca_interrupt_info *info)
{
	if (irq == SDCA_NO_INTERRUPT) {
		return NULL;
	} else if (irq < 0 || irq >= SDCA_MAX_INTERRUPTS) {
		dev_err(dev, "bad irq position: %d\n", irq);
		return ERR_PTR(-EINVAL);
	}

	if (info->irqs[irq].irq) {
		dev_dbg(dev, "skipping irq %d, already requested\n", irq);
		return NULL;
	}

	return &info->irqs[irq];
}

/**
 * sdca_irq_populate_early - process pre-audio card IRQ registrations
 * @dev: Device pointer for SDCA Function.
 * @regmap: Regmap pointer for the SDCA Function.
 * @function: Pointer to the SDCA Function.
 * @info: Pointer to the SDCA interrupt info for this device.
 *
 * This is intended to be used as part of the Function boot process. It
 * can be called before the soundcard is registered (ie. doesn't depend
 * on component) and will register the FDL interrupts.
 *
 * Return: Zero on success, and a negative error code on failure.
 */
int sdca_irq_populate_early(struct device *dev, struct regmap *regmap,
			    struct sdca_function_data *function,
			    struct sdca_interrupt_info *info)
{
	int i, j;

	guard(mutex)(&info->irq_lock);

	for (i = 0; i < function->num_entities; i++) {
		struct sdca_entity *entity = &function->entities[i];

		for (j = 0; j < entity->num_controls; j++) {
			struct sdca_control *control = &entity->controls[j];
			int irq = control->interrupt_position;
			struct sdca_interrupt *interrupt;
			int ret;

			interrupt = get_interrupt_data(dev, irq, info);
			if (IS_ERR(interrupt))
				return PTR_ERR(interrupt);
			else if (!interrupt)
				continue;

			switch (SDCA_CTL_TYPE(entity->type, control->sel)) {
			case SDCA_CTL_TYPE_S(XU, FDL_CURRENTOWNER):
				ret = sdca_irq_data_populate(dev, regmap, NULL,
							     function, entity,
							     control, interrupt);
				if (ret)
					return ret;

				ret = sdca_fdl_alloc_state(interrupt);
				if (ret)
					return ret;

				ret = sdca_irq_request_locked(dev, info, irq,
							      interrupt->name,
							      fdl_owner_handler,
							      interrupt);
				if (ret) {
					dev_err(dev, "failed to request irq %s: %d\n",
						interrupt->name, ret);
					return ret;
				}
				break;
			default:
				break;
			}
		}
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(sdca_irq_populate_early, "SND_SOC_SDCA");

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

			interrupt = get_interrupt_data(dev, irq, info);
			if (IS_ERR(interrupt))
				return PTR_ERR(interrupt);
			else if (!interrupt)
				continue;

			ret = sdca_irq_data_populate(dev, NULL, component,
						     function, entity, control,
						     interrupt);
			if (ret)
				return ret;

			handler = base_handler;

			switch (SDCA_CTL_TYPE(entity->type, control->sel)) {
			case SDCA_CTL_TYPE_S(ENTITY_0, FUNCTION_STATUS):
				handler = function_status_handler;
				break;
			case SDCA_CTL_TYPE_S(GE, DETECTED_MODE):
				ret = sdca_jack_alloc_state(interrupt);
				if (ret)
					return ret;

				handler = detected_mode_handler;
				break;
			case SDCA_CTL_TYPE_S(XU, FDL_CURRENTOWNER):
				ret = sdca_fdl_alloc_state(interrupt);
				if (ret)
					return ret;

				handler = fdl_owner_handler;
				break;
			case SDCA_CTL_TYPE_S(HIDE, HIDTX_CURRENTOWNER):
				handler = hid_handler;
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
 * @sdev: Device pointer against which things should be allocated.
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
struct sdca_interrupt_info *sdca_irq_allocate(struct device *sdev,
					      struct regmap *regmap, int irq)
{
	struct sdca_interrupt_info *info;
	int ret, i;

	info = devm_kzalloc(sdev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);

	info->irq_chip = sdca_irq_chip;

	for (i = 0; i < ARRAY_SIZE(info->irqs); i++)
		info->irqs[i].device_regmap = regmap;

	ret = devm_mutex_init(sdev, &info->irq_lock);
	if (ret)
		return ERR_PTR(ret);

	ret = devm_regmap_add_irq_chip(sdev, regmap, irq, IRQF_ONESHOT, 0,
				       &info->irq_chip, &info->irq_data);
	if (ret) {
		dev_err(sdev, "failed to register irq chip: %d\n", ret);
		return ERR_PTR(ret);
	}

	dev_dbg(sdev, "registered on irq %d\n", irq);

	return info;
}
EXPORT_SYMBOL_NS_GPL(sdca_irq_allocate, "SND_SOC_SDCA");

static void irq_enable_flags(struct sdca_function_data *function,
			     struct sdca_interrupt_info *info, bool early)
{
	struct sdca_interrupt *interrupt;
	int i;

	for (i = 0; i < SDCA_MAX_INTERRUPTS; i++) {
		interrupt = &info->irqs[i];

		if (!interrupt || interrupt->function != function)
			continue;

		switch (SDCA_CTL_TYPE(interrupt->entity->type,
				      interrupt->control->sel)) {
		case SDCA_CTL_TYPE_S(XU, FDL_CURRENTOWNER):
			if (early)
				enable_irq(interrupt->irq);
			break;
		default:
			if (!early)
				enable_irq(interrupt->irq);
			break;
		}
	}
}

/**
 * sdca_irq_enable_early - Re-enable early SDCA IRQs for a given function
 * @function: Pointer to the SDCA Function.
 * @info: Pointer to the SDCA interrupt info for this device.
 *
 * The early version of the IRQ enable allows enabling IRQs which may be
 * necessary to bootstrap functionality for other IRQs, such as the FDL
 * process.
 */
void sdca_irq_enable_early(struct sdca_function_data *function,
			   struct sdca_interrupt_info *info)
{
	irq_enable_flags(function, info, true);
}
EXPORT_SYMBOL_NS_GPL(sdca_irq_enable_early, "SND_SOC_SDCA");

/**
 * sdca_irq_enable - Re-enable SDCA IRQs for a given function
 * @function: Pointer to the SDCA Function.
 * @info: Pointer to the SDCA interrupt info for this device.
 */
void sdca_irq_enable(struct sdca_function_data *function,
		     struct sdca_interrupt_info *info)
{
	irq_enable_flags(function, info, false);
}
EXPORT_SYMBOL_NS_GPL(sdca_irq_enable, "SND_SOC_SDCA");

/**
 * sdca_irq_disable - Disable SDCA IRQs for a given function
 * @function: Pointer to the SDCA Function.
 * @info: Pointer to the SDCA interrupt info for this device.
 */
void sdca_irq_disable(struct sdca_function_data *function,
		      struct sdca_interrupt_info *info)
{
	struct sdca_interrupt *interrupt;
	int i;

	for (i = 0; i < SDCA_MAX_INTERRUPTS; i++) {
		interrupt = &info->irqs[i];

		if (!interrupt || interrupt->function != function)
			continue;

		disable_irq(interrupt->irq);
	}
}
EXPORT_SYMBOL_NS_GPL(sdca_irq_disable, "SND_SOC_SDCA");
