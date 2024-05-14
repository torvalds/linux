/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2007-2009 ST-Ericsson AB
 *
 * ABX500 core access functions.
 * The abx500 interface is used for the Analog Baseband chips.
 *
 * Author: Mattias Wallin <mattias.wallin@stericsson.com>
 * Author: Mattias Nilsson <mattias.i.nilsson@stericsson.com>
 * Author: Bengt Jonsson <bengt.g.jonsson@stericsson.com>
 * Author: Rickard Andersson <rickard.andersson@stericsson.com>
 */

#include <linux/regulator/machine.h>

struct device;

#ifndef MFD_ABX500_H
#define MFD_ABX500_H

/**
 * struct abx500_init_setting
 * Initial value of the registers for driver to use during setup.
 */
struct abx500_init_settings {
	u8 bank;
	u8 reg;
	u8 setting;
};

int abx500_set_register_interruptible(struct device *dev, u8 bank, u8 reg,
	u8 value);
int abx500_get_register_interruptible(struct device *dev, u8 bank, u8 reg,
	u8 *value);
int abx500_get_register_page_interruptible(struct device *dev, u8 bank,
	u8 first_reg, u8 *regvals, u8 numregs);
int abx500_set_register_page_interruptible(struct device *dev, u8 bank,
	u8 first_reg, u8 *regvals, u8 numregs);
/**
 * abx500_mask_and_set_register_inerruptible() - Modifies selected bits of a
 *	target register
 *
 * @dev: The AB sub device.
 * @bank: The i2c bank number.
 * @bitmask: The bit mask to use.
 * @bitvalues: The new bit values.
 *
 * Updates the value of an AB register:
 * value -> ((value & ~bitmask) | (bitvalues & bitmask))
 */
int abx500_mask_and_set_register_interruptible(struct device *dev, u8 bank,
	u8 reg, u8 bitmask, u8 bitvalues);
int abx500_get_chip_id(struct device *dev);
int abx500_event_registers_startup_state_get(struct device *dev, u8 *event);
int abx500_startup_irq_enabled(struct device *dev, unsigned int irq);

struct abx500_ops {
	int (*get_chip_id) (struct device *);
	int (*get_register) (struct device *, u8, u8, u8 *);
	int (*set_register) (struct device *, u8, u8, u8);
	int (*get_register_page) (struct device *, u8, u8, u8 *, u8);
	int (*set_register_page) (struct device *, u8, u8, u8 *, u8);
	int (*mask_and_set_register) (struct device *, u8, u8, u8, u8);
	int (*event_registers_startup_state_get) (struct device *, u8 *);
	int (*startup_irq_enabled) (struct device *, unsigned int);
	void (*dump_all_banks) (struct device *);
};

int abx500_register_ops(struct device *core_dev, struct abx500_ops *ops);
void abx500_remove_ops(struct device *dev);
#endif
