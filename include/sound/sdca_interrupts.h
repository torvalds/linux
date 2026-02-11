/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The MIPI SDCA specification is available for public downloads at
 * https://www.mipi.org/mipi-sdca-v1-0-download
 *
 * Copyright (C) 2025 Cirrus Logic, Inc. and
 *                    Cirrus Logic International Semiconductor Ltd.
 */

#ifndef __SDCA_INTERRUPTS_H__
#define __SDCA_INTERRUPTS_H__

#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/regmap.h>

struct device;
struct snd_soc_component;
struct sdca_function_data;

#define SDCA_MAX_INTERRUPTS 31 /* the last bit is reserved for future extensions */

/**
 * struct sdca_interrupt - contains information about a single SDCA interrupt
 * @name: The name of the interrupt.
 * @dev: Pointer to the Function device.
 * @device_regmap: Pointer to the IRQ regmap.
 * @function_regmap: Pointer to the SDCA Function regmap.
 * @component: Pointer to the ASoC component owns the interrupt.
 * @function: Pointer to the Function that the interrupt is associated with.
 * @entity: Pointer to the Entity that the interrupt is associated with.
 * @control: Pointer to the Control that the interrupt is associated with.
 * @priv: Pointer to private data for use by the handler.
 * @irq: IRQ number allocated to this interrupt, also used internally to track
 * the IRQ being assigned.
 */
struct sdca_interrupt {
	const char *name;

	struct device *dev;
	struct regmap *device_regmap;
	struct regmap *function_regmap;
	struct snd_soc_component *component;
	struct sdca_function_data *function;
	struct sdca_entity *entity;
	struct sdca_control *control;

	void *priv;

	int irq;
};

/**
 * struct sdca_interrupt_info - contains top-level SDCA interrupt information
 * @irq_chip: regmap irq chip structure.
 * @irq_data: regmap irq chip data structure.
 * @irqs: Array of data for each individual IRQ.
 * @irq_lock: Protects access to the list of sdca_interrupt structures.
 */
struct sdca_interrupt_info {
	struct regmap_irq_chip irq_chip;
	struct regmap_irq_chip_data *irq_data;

	struct sdca_interrupt irqs[SDCA_MAX_INTERRUPTS];

	struct mutex irq_lock; /* Protect irqs list across functions */
};

int sdca_irq_request(struct device *dev, struct sdca_interrupt_info *interrupt_info,
		     int sdca_irq, const char *name, irq_handler_t handler,
		     void *data);
int sdca_irq_data_populate(struct device *dev, struct regmap *function_regmap,
			   struct snd_soc_component *component,
			   struct sdca_function_data *function,
			   struct sdca_entity *entity,
			   struct sdca_control *control,
			   struct sdca_interrupt *interrupt);
int sdca_irq_populate_early(struct device *dev, struct regmap *function_regmap,
			    struct sdca_function_data *function,
			    struct sdca_interrupt_info *info);
int sdca_irq_populate(struct sdca_function_data *function,
		      struct snd_soc_component *component,
		      struct sdca_interrupt_info *info);
struct sdca_interrupt_info *sdca_irq_allocate(struct device *dev,
					      struct regmap *regmap, int irq);

void sdca_irq_enable_early(struct sdca_function_data *function,
			   struct sdca_interrupt_info *info);
void sdca_irq_enable(struct sdca_function_data *function,
		     struct sdca_interrupt_info *info);
void sdca_irq_disable(struct sdca_function_data *function,
		      struct sdca_interrupt_info *info);

#endif
