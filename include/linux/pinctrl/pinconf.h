/*
 * Interface the pinconfig portions of the pinctrl subsystem
 *
 * Copyright (C) 2011 ST-Ericsson SA
 * Written on behalf of Linaro for ST-Ericsson
 * This interface is used in the core to keep track of pins.
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */
#ifndef __LINUX_PINCTRL_PINCONF_H
#define __LINUX_PINCTRL_PINCONF_H

#include <linux/types.h>

struct pinctrl_dev;
struct seq_file;

/**
 * struct pinconf_ops - pin config operations, to be implemented by
 * pin configuration capable drivers.
 * @is_generic: for pin controllers that want to use the generic interface,
 *	this flag tells the framework that it's generic.
 * @pin_config_get: get the config of a certain pin, if the requested config
 *	is not available on this controller this should return -ENOTSUPP
 *	and if it is available but disabled it should return -EINVAL
 * @pin_config_set: configure an individual pin
 * @pin_config_group_get: get configurations for an entire pin group; should
 *	return -ENOTSUPP and -EINVAL using the same rules as pin_config_get.
 * @pin_config_group_set: configure all pins in a group
 * @pin_config_dbg_show: optional debugfs display hook that will provide
 *	per-device info for a certain pin in debugfs
 * @pin_config_group_dbg_show: optional debugfs display hook that will provide
 *	per-device info for a certain group in debugfs
 * @pin_config_config_dbg_show: optional debugfs display hook that will decode
 *	and display a driver's pin configuration parameter
 */
struct pinconf_ops {
#ifdef CONFIG_GENERIC_PINCONF
	bool is_generic;
#endif
	int (*pin_config_get) (struct pinctrl_dev *pctldev,
			       unsigned pin,
			       unsigned long *config);
	int (*pin_config_set) (struct pinctrl_dev *pctldev,
			       unsigned pin,
			       unsigned long *configs,
			       unsigned num_configs);
	int (*pin_config_group_get) (struct pinctrl_dev *pctldev,
				     unsigned selector,
				     unsigned long *config);
	int (*pin_config_group_set) (struct pinctrl_dev *pctldev,
				     unsigned selector,
				     unsigned long *configs,
				     unsigned num_configs);
	void (*pin_config_dbg_show) (struct pinctrl_dev *pctldev,
				     struct seq_file *s,
				     unsigned offset);
	void (*pin_config_group_dbg_show) (struct pinctrl_dev *pctldev,
					   struct seq_file *s,
					   unsigned selector);
	void (*pin_config_config_dbg_show) (struct pinctrl_dev *pctldev,
					    struct seq_file *s,
					    unsigned long config);
};

#endif /* __LINUX_PINCTRL_PINCONF_H */
