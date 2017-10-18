/*
 * Interface the generic pinconfig portions of the pinctrl subsystem
 *
 * Copyright (C) 2011 ST-Ericsson SA
 * Written on behalf of Linaro for ST-Ericsson
 * This interface is used in the core to keep track of pins.
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */
#ifndef __LINUX_PINCTRL_PINCONF_GENERIC_H
#define __LINUX_PINCTRL_PINCONF_GENERIC_H

/**
 * enum pin_config_param - possible pin configuration parameters
 * @PIN_CONFIG_BIAS_BUS_HOLD: the pin will be set to weakly latch so that it
 *	weakly drives the last value on a tristate bus, also known as a "bus
 *	holder", "bus keeper" or "repeater". This allows another device on the
 *	bus to change the value by driving the bus high or low and switching to
 *	tristate. The argument is ignored.
 * @PIN_CONFIG_BIAS_DISABLE: disable any pin bias on the pin, a
 *	transition from say pull-up to pull-down implies that you disable
 *	pull-up in the process, this setting disables all biasing.
 * @PIN_CONFIG_BIAS_HIGH_IMPEDANCE: the pin will be set to a high impedance
 *	mode, also know as "third-state" (tristate) or "high-Z" or "floating".
 *	On output pins this effectively disconnects the pin, which is useful
 *	if for example some other pin is going to drive the signal connected
 *	to it for a while. Pins used for input are usually always high
 *	impedance.
 * @PIN_CONFIG_BIAS_PULL_DOWN: the pin will be pulled down (usually with high
 *	impedance to GROUND). If the argument is != 0 pull-down is enabled,
 *	if it is 0, pull-down is total, i.e. the pin is connected to GROUND.
 * @PIN_CONFIG_BIAS_PULL_PIN_DEFAULT: the pin will be pulled up or down based
 *	on embedded knowledge of the controller hardware, like current mux
 *	function. The pull direction and possibly strength too will normally
 *	be decided completely inside the hardware block and not be readable
 *	from the kernel side.
 *	If the argument is != 0 pull up/down is enabled, if it is 0, the
 *	configuration is ignored. The proper way to disable it is to use
 *	@PIN_CONFIG_BIAS_DISABLE.
 * @PIN_CONFIG_BIAS_PULL_UP: the pin will be pulled up (usually with high
 *	impedance to VDD). If the argument is != 0 pull-up is enabled,
 *	if it is 0, pull-up is total, i.e. the pin is connected to VDD.
 * @PIN_CONFIG_DRIVE_OPEN_DRAIN: the pin will be driven with open drain (open
 *	collector) which means it is usually wired with other output ports
 *	which are then pulled up with an external resistor. Setting this
 *	config will enable open drain mode, the argument is ignored.
 * @PIN_CONFIG_DRIVE_OPEN_SOURCE: the pin will be driven with open source
 *	(open emitter). Setting this config will enable open source mode, the
 *	argument is ignored.
 * @PIN_CONFIG_DRIVE_PUSH_PULL: the pin will be driven actively high and
 *	low, this is the most typical case and is typically achieved with two
 *	active transistors on the output. Setting this config will enable
 *	push-pull mode, the argument is ignored.
 * @PIN_CONFIG_DRIVE_STRENGTH: the pin will sink or source at most the current
 *	passed as argument. The argument is in mA.
 * @PIN_CONFIG_INPUT_DEBOUNCE: this will configure the pin to debounce mode,
 *	which means it will wait for signals to settle when reading inputs. The
 *	argument gives the debounce time in usecs. Setting the
 *	argument to zero turns debouncing off.
 * @PIN_CONFIG_INPUT_ENABLE: enable the pin's input.  Note that this does not
 *	affect the pin's ability to drive output.  1 enables input, 0 disables
 *	input.
 * @PIN_CONFIG_INPUT_SCHMITT: this will configure an input pin to run in
 *	schmitt-trigger mode. If the schmitt-trigger has adjustable hysteresis,
 *	the threshold value is given on a custom format as argument when
 *	setting pins to this mode.
 * @PIN_CONFIG_INPUT_SCHMITT_ENABLE: control schmitt-trigger mode on the pin.
 *      If the argument != 0, schmitt-trigger mode is enabled. If it's 0,
 *      schmitt-trigger mode is disabled.
 * @PIN_CONFIG_LOW_POWER_MODE: this will configure the pin for low power
 *	operation, if several modes of operation are supported these can be
 *	passed in the argument on a custom form, else just use argument 1
 *	to indicate low power mode, argument 0 turns low power mode off.
 * @PIN_CONFIG_OUTPUT_ENABLE: this will enable the pin's output mode
 * 	without driving a value there. For most platforms this reduces to
 * 	enable the output buffers and then let the pin controller current
 * 	configuration (eg. the currently selected mux function) drive values on
 * 	the line. Use argument 1 to enable output mode, argument 0 to disable
 * 	it.
 * @PIN_CONFIG_OUTPUT: this will configure the pin as an output and drive a
 * 	value on the line. Use argument 1 to indicate high level, argument 0 to
 *	indicate low level. (Please see Documentation/driver-api/pinctl.rst,
 *	section "GPIO mode pitfalls" for a discussion around this parameter.)
 * @PIN_CONFIG_POWER_SOURCE: if the pin can select between different power
 *	supplies, the argument to this parameter (on a custom format) tells
 *	the driver which alternative power source to use.
 * @PIN_CONFIG_SLEW_RATE: if the pin can select slew rate, the argument to
 *	this parameter (on a custom format) tells the driver which alternative
 *	slew rate to use.
 * @PIN_CONFIG_END: this is the last enumerator for pin configurations, if
 *	you need to pass in custom configurations to the pin controller, use
 *	PIN_CONFIG_END+1 as the base offset.
 * @PIN_CONFIG_MAX: this is the maximum configuration value that can be
 *	presented using the packed format.
 */
enum pin_config_param {
	PIN_CONFIG_BIAS_BUS_HOLD,
	PIN_CONFIG_BIAS_DISABLE,
	PIN_CONFIG_BIAS_HIGH_IMPEDANCE,
	PIN_CONFIG_BIAS_PULL_DOWN,
	PIN_CONFIG_BIAS_PULL_PIN_DEFAULT,
	PIN_CONFIG_BIAS_PULL_UP,
	PIN_CONFIG_DRIVE_OPEN_DRAIN,
	PIN_CONFIG_DRIVE_OPEN_SOURCE,
	PIN_CONFIG_DRIVE_PUSH_PULL,
	PIN_CONFIG_DRIVE_STRENGTH,
	PIN_CONFIG_INPUT_DEBOUNCE,
	PIN_CONFIG_INPUT_ENABLE,
	PIN_CONFIG_INPUT_SCHMITT,
	PIN_CONFIG_INPUT_SCHMITT_ENABLE,
	PIN_CONFIG_LOW_POWER_MODE,
	PIN_CONFIG_OUTPUT_ENABLE,
	PIN_CONFIG_OUTPUT,
	PIN_CONFIG_POWER_SOURCE,
	PIN_CONFIG_SLEW_RATE,
	PIN_CONFIG_END = 0x7F,
	PIN_CONFIG_MAX = 0xFF,
};

/*
 * Helpful configuration macro to be used in tables etc.
 */
#define PIN_CONF_PACKED(p, a) ((a << 8) | ((unsigned long) p & 0xffUL))

/*
 * The following inlines stuffs a configuration parameter and data value
 * into and out of an unsigned long argument, as used by the generic pin config
 * system. We put the parameter in the lower 8 bits and the argument in the
 * upper 24 bits.
 */

static inline enum pin_config_param pinconf_to_config_param(unsigned long config)
{
	return (enum pin_config_param) (config & 0xffUL);
}

static inline u32 pinconf_to_config_argument(unsigned long config)
{
	return (u32) ((config >> 8) & 0xffffffUL);
}

static inline unsigned long pinconf_to_config_packed(enum pin_config_param param,
						     u32 argument)
{
	return PIN_CONF_PACKED(param, argument);
}

#ifdef CONFIG_GENERIC_PINCONF

#ifdef CONFIG_DEBUG_FS
#define PCONFDUMP(a, b, c, d) {					\
	.param = a, .display = b, .format = c, .has_arg = d	\
	}

struct pin_config_item {
	const enum pin_config_param param;
	const char * const display;
	const char * const format;
	bool has_arg;
};
#endif /* CONFIG_DEBUG_FS */

#ifdef CONFIG_OF

#include <linux/device.h>
#include <linux/pinctrl/machine.h>
struct pinctrl_dev;
struct pinctrl_map;

struct pinconf_generic_params {
	const char * const property;
	enum pin_config_param param;
	u32 default_value;
};

int pinconf_generic_dt_subnode_to_map(struct pinctrl_dev *pctldev,
		struct device_node *np, struct pinctrl_map **map,
		unsigned *reserved_maps, unsigned *num_maps,
		enum pinctrl_map_type type);
int pinconf_generic_dt_node_to_map(struct pinctrl_dev *pctldev,
		struct device_node *np_config, struct pinctrl_map **map,
		unsigned *num_maps, enum pinctrl_map_type type);
void pinconf_generic_dt_free_map(struct pinctrl_dev *pctldev,
		struct pinctrl_map *map, unsigned num_maps);

static inline int pinconf_generic_dt_node_to_map_group(
		struct pinctrl_dev *pctldev, struct device_node *np_config,
		struct pinctrl_map **map, unsigned *num_maps)
{
	return pinconf_generic_dt_node_to_map(pctldev, np_config, map, num_maps,
			PIN_MAP_TYPE_CONFIGS_GROUP);
}

static inline int pinconf_generic_dt_node_to_map_pin(
		struct pinctrl_dev *pctldev, struct device_node *np_config,
		struct pinctrl_map **map, unsigned *num_maps)
{
	return pinconf_generic_dt_node_to_map(pctldev, np_config, map, num_maps,
			PIN_MAP_TYPE_CONFIGS_PIN);
}

static inline int pinconf_generic_dt_node_to_map_all(
		struct pinctrl_dev *pctldev, struct device_node *np_config,
		struct pinctrl_map **map, unsigned *num_maps)
{
	/*
	 * passing the type as PIN_MAP_TYPE_INVALID causes the underlying parser
	 * to infer the map type from the DT properties used.
	 */
	return pinconf_generic_dt_node_to_map(pctldev, np_config, map, num_maps,
			PIN_MAP_TYPE_INVALID);
}
#endif

#endif /* CONFIG_GENERIC_PINCONF */

#endif /* __LINUX_PINCTRL_PINCONF_GENERIC_H */
