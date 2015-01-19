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
enum amlogic_pinconf_param{
	AML_PCON_ENOUT=1,
	AML_PCON_PULLUP=2,
	AML_PCON_PULLUP_SHEFFT=4,
	AML_ENOUT_VALUE_SHIFFT=8,
	AML_PCON_ENOUT_SHIFFT=12,
	AML_PCON_ENPULLUP_SHIFFT=16,
};

struct cfg_param {
	const char *property;
	enum amlogic_pinconf_param param;
};
#define AML_PINCONF_PACK_PULL(_param_, _arg_) (((_param_) << AML_PCON_PULLUP_SHEFFT) | (_arg_))
#define AML_PINCONF_PACK_ENOUT(_param_, _arg_) (((_param_) << AML_PCON_ENOUT_SHIFFT) |( (_arg_)<<AML_ENOUT_VALUE_SHIFFT))
#define AML_PINCONF_PACK_PULLEN(_param_, _arg_) (((_param_) << AML_PCON_PULLUP_SHEFFT)|((_arg_) << AML_PCON_ENPULLUP_SHIFFT))

#define AML_PINCONF_UNPACK_PULL_PARA(_conf_) (((_conf_) >> AML_PCON_PULLUP_SHEFFT)&0xf)
#define AML_PINCONF_UNPACK_PULL_ARG(_conf_) ((_conf_) & 0xf)
#define AML_PINCONF_UNPACK_PULL_EN(_conf_) (((_conf_) >>AML_PCON_ENPULLUP_SHIFFT)& 0xf)

#define AML_PINCONF_UNPACK_ENOUT_PARA(_conf_) (((_conf_) >>AML_PCON_ENOUT_SHIFFT)&0xf)
#define AML_PINCONF_UNPACK_ENOUT_ARG(_conf_) (((_conf_)>>AML_ENOUT_VALUE_SHIFFT)&0xf)

#ifdef CONFIG_PINCONF

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
 * @pin_config_group_get: get configurations for an entire pin group
 * @pin_config_group_set: configure all pins in a group
 * @pin_config_group_dbg_set: optional debugfs to modify a pin configuration
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
			       unsigned long config);
	int (*pin_config_group_get) (struct pinctrl_dev *pctldev,
				     unsigned selector,
				     unsigned long *config);
	int (*pin_config_group_set) (struct pinctrl_dev *pctldev,
				     unsigned selector,
				     unsigned long config);
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

#endif

#endif /* __LINUX_PINCTRL_PINCONF_H */
