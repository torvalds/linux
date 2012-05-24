/*
 * Interface the pinctrl subsystem
 *
 * Copyright (C) 2011 ST-Ericsson SA
 * Written on behalf of Linaro for ST-Ericsson
 * This interface is used in the core to keep track of pins.
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */
#ifndef __LINUX_PINCTRL_PINCTRL_H
#define __LINUX_PINCTRL_PINCTRL_H

#ifdef CONFIG_PINCTRL

#include <linux/radix-tree.h>
#include <linux/list.h>
#include <linux/seq_file.h>
#include "pinctrl-state.h"

struct device;
struct pinctrl_dev;
struct pinmux_ops;
struct pinconf_ops;
struct gpio_chip;

/**
 * struct pinctrl_pin_desc - boards/machines provide information on their
 * pins, pads or other muxable units in this struct
 * @number: unique pin number from the global pin number space
 * @name: a name for this pin
 */
struct pinctrl_pin_desc {
	unsigned number;
	const char *name;
};

/* Convenience macro to define a single named or anonymous pin descriptor */
#define PINCTRL_PIN(a, b) { .number = a, .name = b }
#define PINCTRL_PIN_ANON(a) { .number = a }

/**
 * struct pinctrl_gpio_range - each pin controller can provide subranges of
 * the GPIO number space to be handled by the controller
 * @node: list node for internal use
 * @name: a name for the chip in this range
 * @id: an ID number for the chip in this range
 * @base: base offset of the GPIO range
 * @pin_base: base pin number of the GPIO range
 * @npins: number of pins in the GPIO range, including the base number
 * @gc: an optional pointer to a gpio_chip
 */
struct pinctrl_gpio_range {
	struct list_head node;
	const char *name;
	unsigned int id;
	unsigned int base;
	unsigned int pin_base;
	unsigned int npins;
	struct gpio_chip *gc;
};

/**
 * struct pinctrl_ops - global pin control operations, to be implemented by
 * pin controller drivers.
 * @list_groups: list the number of selectable named groups available
 *	in this pinmux driver, the core will begin on 0 and call this
 *	repeatedly as long as it returns >= 0 to enumerate the groups
 * @get_group_name: return the group name of the pin group
 * @get_group_pins: return an array of pins corresponding to a certain
 *	group selector @pins, and the size of the array in @num_pins
 * @pin_dbg_show: optional debugfs display hook that will provide per-device
 *	info for a certain pin in debugfs
 */
struct pinctrl_ops {
	int (*list_groups) (struct pinctrl_dev *pctldev, unsigned selector);
	const char *(*get_group_name) (struct pinctrl_dev *pctldev,
				       unsigned selector);
	int (*get_group_pins) (struct pinctrl_dev *pctldev,
			       unsigned selector,
			       const unsigned **pins,
			       unsigned *num_pins);
	void (*pin_dbg_show) (struct pinctrl_dev *pctldev, struct seq_file *s,
			  unsigned offset);
};

/**
 * struct pinctrl_desc - pin controller descriptor, register this to pin
 * control subsystem
 * @name: name for the pin controller
 * @pins: an array of pin descriptors describing all the pins handled by
 *	this pin controller
 * @npins: number of descriptors in the array, usually just ARRAY_SIZE()
 *	of the pins field above
 * @pctlops: pin control operation vtable, to support global concepts like
 *	grouping of pins, this is optional.
 * @pmxops: pinmux operations vtable, if you support pinmuxing in your driver
 * @confops: pin config operations vtable, if you support pin configuration in
 *	your driver
 * @owner: module providing the pin controller, used for refcounting
 */
struct pinctrl_desc {
	const char *name;
	struct pinctrl_pin_desc const *pins;
	unsigned int npins;
	struct pinctrl_ops *pctlops;
	struct pinmux_ops *pmxops;
	struct pinconf_ops *confops;
	struct module *owner;
};

/* External interface to pin controller */
extern struct pinctrl_dev *pinctrl_register(struct pinctrl_desc *pctldesc,
				struct device *dev, void *driver_data);
extern void pinctrl_unregister(struct pinctrl_dev *pctldev);
extern bool pin_is_valid(struct pinctrl_dev *pctldev, int pin);
extern void pinctrl_add_gpio_range(struct pinctrl_dev *pctldev,
				struct pinctrl_gpio_range *range);
extern void pinctrl_remove_gpio_range(struct pinctrl_dev *pctldev,
				struct pinctrl_gpio_range *range);
extern const char *pinctrl_dev_get_name(struct pinctrl_dev *pctldev);
extern void *pinctrl_dev_get_drvdata(struct pinctrl_dev *pctldev);
#else

struct pinctrl_dev;

/* Sufficiently stupid default functions when pinctrl is not in use */
static inline bool pin_is_valid(struct pinctrl_dev *pctldev, int pin)
{
	return pin >= 0;
}

#endif /* !CONFIG_PINCTRL */

#endif /* __LINUX_PINCTRL_PINCTRL_H */
