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

/*
 * You shouldn't even be able to compile with these enums etc unless you're
 * using generic pin config. That is why this is defined out.
 */
#ifdef CONFIG_GENERIC_PINCONF

/**
 * enum pin_config_param - possible pin configuration parameters
 * @PIN_CONFIG_BIAS_DISABLE: disable any pin bias on the pin, a
 *	transition from say pull-up to pull-down implies that you disable
 *	pull-up in the process, this setting disables all biasing.
 * @PIN_CONFIG_BIAS_HIGH_IMPEDANCE: the pin will be set to a high impedance
 *	mode, also know as "third-state" (tristate) or "high-Z" or "floating".
 *	On output pins this effectively disconnects the pin, which is useful
 *	if for example some other pin is going to drive the signal connected
 *	to it for a while. Pins used for input are usually always high
 *	impedance.
 * @PIN_CONFIG_BIAS_PULL_UP: the pin will be pulled up (usually with high
 *	impedance to VDD). If the argument is != 0 pull-up is enabled,
 *	if it is 0, pull-up is disabled.
 * @PIN_CONFIG_BIAS_PULL_DOWN: the pin will be pulled down (usually with high
 *	impedance to GROUND). If the argument is != 0 pull-down is enabled,
 *	if it is 0, pull-down is disabled.
 * @PIN_CONFIG_DRIVE_PUSH_PULL: the pin will be driven actively high and
 *	low, this is the most typical case and is typically achieved with two
 *	active transistors on the output. Sending this config will enabale
 *	push-pull mode, the argument is ignored.
 * @PIN_CONFIG_DRIVE_OPEN_DRAIN: the pin will be driven with open drain (open
 *	collector) which means it is usually wired with other output ports
 *	which are then pulled up with an external resistor. Sending this
 *	config will enabale open drain mode, the argument is ignored.
 * @PIN_CONFIG_DRIVE_OPEN_SOURCE: the pin will be driven with open source
 *	(open emitter). Sending this config will enabale open drain mode, the
 *	argument is ignored.
 * @PIN_CONFIG_DRIVE_STRENGTH: the pin will output the current passed as
 * 	argument. The argument is in mA.
 * @PIN_CONFIG_INPUT_SCHMITT_ENABLE: control schmitt-trigger mode on the pin.
 *      If the argument != 0, schmitt-trigger mode is enabled. If it's 0,
 *      schmitt-trigger mode is disabled.
 * @PIN_CONFIG_INPUT_SCHMITT: this will configure an input pin to run in
 *	schmitt-trigger mode. If the schmitt-trigger has adjustable hysteresis,
 *	the threshold value is given on a custom format as argument when
 *	setting pins to this mode.
 * @PIN_CONFIG_INPUT_DEBOUNCE: this will configure the pin to debounce mode,
 *	which means it will wait for signals to settle when reading inputs. The
 *	argument gives the debounce time on a custom format. Setting the
 *	argument to zero turns debouncing off.
 * @PIN_CONFIG_POWER_SOURCE: if the pin can select between different power
 *	supplies, the argument to this parameter (on a custom format) tells
 *	the driver which alternative power source to use.
 * @PIN_CONFIG_SLEW_RATE: if the pin can select slew rate, the argument to
 * 	this parameter (on a custom format) tells the driver which alternative
 * 	slew rate to use.
 * @PIN_CONFIG_LOW_POWER_MODE: this will configure the pin for low power
 *	operation, if several modes of operation are supported these can be
 *	passed in the argument on a custom form, else just use argument 1
 *	to indicate low power mode, argument 0 turns low power mode off.
 * @PIN_CONFIG_OUTPUT: this will configure the pin in output, use argument
 *	1 to indicate high level, argument 0 to indicate low level.
 * @PIN_CONFIG_END: this is the last enumerator for pin configurations, if
 *	you need to pass in custom configurations to the pin controller, use
 *	PIN_CONFIG_END+1 as the base offset.
 */
enum pin_config_param {
	PIN_CONFIG_BIAS_DISABLE,
	PIN_CONFIG_BIAS_HIGH_IMPEDANCE,
	PIN_CONFIG_BIAS_PULL_UP,
	PIN_CONFIG_BIAS_PULL_DOWN,
	PIN_CONFIG_DRIVE_PUSH_PULL,
	PIN_CONFIG_DRIVE_OPEN_DRAIN,
	PIN_CONFIG_DRIVE_OPEN_SOURCE,
	PIN_CONFIG_DRIVE_STRENGTH,
	PIN_CONFIG_INPUT_SCHMITT_ENABLE,
	PIN_CONFIG_INPUT_SCHMITT,
	PIN_CONFIG_INPUT_DEBOUNCE,
	PIN_CONFIG_POWER_SOURCE,
	PIN_CONFIG_SLEW_RATE,
	PIN_CONFIG_LOW_POWER_MODE,
	PIN_CONFIG_OUTPUT,
	PIN_CONFIG_END = 0x7FFF,
};

/*
 * Helpful configuration macro to be used in tables etc.
 */
#define PIN_CONF_PACKED(p, a) ((a << 16) | ((unsigned long) p & 0xffffUL))

/*
 * The following inlines stuffs a configuration parameter and data value
 * into and out of an unsigned long argument, as used by the generic pin config
 * system. We put the parameter in the lower 16 bits and the argument in the
 * upper 16 bits.
 */

static inline enum pin_config_param pinconf_to_config_param(unsigned long config)
{
	return (enum pin_config_param) (config & 0xffffUL);
}

static inline u16 pinconf_to_config_argument(unsigned long config)
{
	return (enum pin_config_param) ((config >> 16) & 0xffffUL);
}

static inline unsigned long pinconf_to_config_packed(enum pin_config_param param,
						     u16 argument)
{
	return PIN_CONF_PACKED(param, argument);
}

#endif /* CONFIG_GENERIC_PINCONF */

#endif /* __LINUX_PINCTRL_PINCONF_GENERIC_H */
