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
 * @component: Pointer to the ASoC component owns the interrupt.
 * @function: Pointer to the Function that the interrupt is associated with.
 * @entity: Pointer to the Entity that the interrupt is associated with.
 * @control: Pointer to the Control that the interrupt is associated with.
 * @priv: Pointer to private data for use by the handler.
 * @externally_requested: Internal flag used to check if a client driver has
 * already requested the interrupt, for custom handling, allowing the core to
 * skip handling this interrupt.
 */
struct sdca_interrupt {
	const char *name;

	struct snd_soc_component *component;
	struct sdca_function_data *function;
	struct sdca_entity *entity;
	struct sdca_control *control;

	void *priv;

	bool externally_requested;
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
int sdca_irq_data_populate(struct snd_soc_component *component,
			   struct sdca_function_data *function,
			   struct sdca_entity *entity,
			   struct sdca_control *control,
			   struct sdca_interrupt *interrupt);
int sdca_irq_populate(struct sdca_function_data *function,
		      struct snd_soc_component *component,
		      struct sdca_interrupt_info *info);
struct sdca_interrupt_info *sdca_irq_allocate(struct device *dev,
					      struct regmap *regmap, int irq);

#endif
