/*	$OpenBSD: ofw_regulator.c,v 1.20 2024/06/14 20:00:32 kettenis Exp $	*/
/*
 * Copyright (c) 2016 Mark Kettenis
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/malloc.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/ofw_regulator.h>

#define REGULATOR_VOLTAGE	0
#define REGULATOR_CURRENT	1

LIST_HEAD(, regulator_device) regulator_devices =
	LIST_HEAD_INITIALIZER(regulator_devices);

LIST_HEAD(, regulator_notifier) regulator_notifiers =
	LIST_HEAD_INITIALIZER(regulator_notifiers);

int regulator_type(int);
uint32_t regulator_gpio_get(int);
int regulator_gpio_set(int, uint32_t);
void regulator_do_notify(uint32_t, uint32_t);

void
regulator_register(struct regulator_device *rd)
{
	rd->rd_volt_min = OF_getpropint(rd->rd_node,
	    "regulator-min-microvolt", 0);
	rd->rd_volt_max = OF_getpropint(rd->rd_node,
	    "regulator-max-microvolt", ~0);
	KASSERT(rd->rd_volt_min <= rd->rd_volt_max);

	rd->rd_amp_min = OF_getpropint(rd->rd_node,
	    "regulator-min-microamp", 0);
	rd->rd_amp_max = OF_getpropint(rd->rd_node,
	    "regulator-max-microamp", ~0);
	KASSERT(rd->rd_amp_min <= rd->rd_amp_max);

	rd->rd_ramp_delay =
	    OF_getpropint(rd->rd_node, "regulator-ramp-delay", 0);

	rd->rd_coupled =
	    OF_getpropint(rd->rd_node, "regulator-coupled-with", 0);
	rd->rd_max_spread =
	    OF_getpropint(rd->rd_node, "regulator-coupled-max-spread", 0);

	if (rd->rd_get_voltage && rd->rd_set_voltage) {
		uint32_t voltage = rd->rd_get_voltage(rd->rd_cookie);
		if (voltage < rd->rd_volt_min)
			rd->rd_set_voltage(rd->rd_cookie, rd->rd_volt_min);
		if (voltage > rd->rd_volt_max)
			rd->rd_set_voltage(rd->rd_cookie, rd->rd_volt_max);
	}

	if (rd->rd_get_current && rd->rd_set_current) {
		uint32_t current = rd->rd_get_current(rd->rd_cookie);
		if (current < rd->rd_amp_min)
			rd->rd_set_current(rd->rd_cookie, rd->rd_amp_min);
		if (current > rd->rd_amp_max)
			rd->rd_set_current(rd->rd_cookie, rd->rd_amp_max);
	}

	rd->rd_phandle = OF_getpropint(rd->rd_node, "phandle", 0);
	if (rd->rd_phandle == 0)
		return;

	LIST_INSERT_HEAD(&regulator_devices, rd, rd_list);

	if (rd->rd_get_voltage) {
		regulator_do_notify(rd->rd_phandle,
		    regulator_get_voltage(rd->rd_phandle));
	}
	if (rd->rd_get_current) {
		regulator_do_notify(rd->rd_phandle,
		    regulator_get_current(rd->rd_phandle));
	}
}

int
regulator_type(int node)
{
	char type[16] = { 0 };

	OF_getprop(node, "regulator-type", type, sizeof(type));
	if (strcmp(type, "current") == 0)
		return REGULATOR_CURRENT;

	return REGULATOR_VOLTAGE;
}

int
regulator_fixed_set(int node, int enable)
{
	uint32_t *gpio;
	uint32_t startup_delay;
	int len;
	char *prop = "gpio";

	/*
	 * This regulator may rely on another. That "parent" regulator
	 * may be used by multiple other devices/regulators, so unless
	 * we refcnt use of a regulator we can only turn it on.
	 */
	if (enable)
		regulator_enable(OF_getpropint(node, "vin-supply", 0));

	pinctrl_byname(node, "default");

	/* The "gpio"/"gpios" property is optional. */
	len = OF_getproplen(node, prop);
	if (len < 0) {
		prop = "gpios";
		len = OF_getproplen(node, prop);
		if (len < 0)
			return 0;
	}

	/*
	 * We deliberately ignore the "enable-active-high" property
	 * here.  Its presence (or absence) is used to override the
	 * polarity encoded by the GPIO flags in the device tree.  But
	 * supporting this behaviour is awkward since it would require
	 * interpreting the GPIO flags here which would be a layer
	 * violation since those flags may be driver-specific.  In
	 * practice the presence of "enable-active-high" is always
	 * aligned with the polarity encoded by the GPIO flags and any
	 * discrepancy is considered to be a bug by the Linux device
	 * tree maintainers.
	 */

	gpio = malloc(len, M_TEMP, M_WAITOK);
	OF_getpropintarray(node, prop, gpio, len);
	gpio_controller_config_pin(gpio, GPIO_CONFIG_OUTPUT);
	if (enable)
		gpio_controller_set_pin(gpio, 1);
	else
		gpio_controller_set_pin(gpio, 0);
	free(gpio, M_TEMP, len);

	startup_delay = OF_getpropint(node, "startup-delay-us", 0);
	if (enable && startup_delay > 0)
		delay(startup_delay);

	return 0;
}

int
regulator_set(uint32_t phandle, int enable)
{
	struct regulator_device *rd;
	int node;

	if (phandle == 0)
		return ENODEV;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return ENODEV;

	/* Never turn off regulators that should always be on. */
	if (OF_getproplen(node, "regulator-always-on") == 0 && !enable)
		return 0;

	LIST_FOREACH(rd, &regulator_devices, rd_list) {
		if (rd->rd_phandle == phandle)
			break;
	}

	if (rd && rd->rd_enable)
		return rd->rd_enable(rd->rd_cookie, enable);

	if (OF_is_compatible(node, "regulator-fixed"))
		return regulator_fixed_set(node, enable);

	return ENODEV;
}

int
regulator_enable(uint32_t phandle)
{
	return regulator_set(phandle, 1);
}

int
regulator_disable(uint32_t phandle)
{
	return regulator_set(phandle, 0);
}

uint32_t
regulator_get_voltage(uint32_t phandle)
{
	struct regulator_device *rd;
	int node;

	if (phandle == 0)
		return 0;

	LIST_FOREACH(rd, &regulator_devices, rd_list) {
		if (rd->rd_phandle == phandle)
			break;
	}

	if (rd && rd->rd_get_voltage)
		return rd->rd_get_voltage(rd->rd_cookie);

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return 0;

	if (OF_is_compatible(node, "regulator-fixed"))
		return OF_getpropint(node, "regulator-min-microvolt", 0);

	if (OF_is_compatible(node, "regulator-gpio") &&
	    regulator_type(node) == REGULATOR_VOLTAGE)
		return regulator_gpio_get(node);

	return 0;
}

int
regulator_set_voltage(uint32_t phandle, uint32_t voltage)
{
	struct regulator_device *rd;
	uint32_t old, delta;
	int error, node;

	if (phandle == 0)
		return ENODEV;

	LIST_FOREACH(rd, &regulator_devices, rd_list) {
		if (rd->rd_phandle == phandle)
			break;
	}

	/* Check limits. */
	if (rd && (voltage < rd->rd_volt_min || voltage > rd->rd_volt_max))
		return EINVAL;

	/* XXX Coupled regulators are unsupported for now. */
	if (rd && rd->rd_coupled)
		return ENOTSUP;

	if (rd && rd->rd_set_voltage) {
		regulator_do_notify(rd->rd_phandle, voltage);

		old = rd->rd_get_voltage(rd->rd_cookie);
		error = rd->rd_set_voltage(rd->rd_cookie, voltage);
		if (voltage > old && rd->rd_ramp_delay > 0) {
			delta = voltage - old;
			delay(howmany(delta, rd->rd_ramp_delay));
		}

		regulator_do_notify(rd->rd_phandle, voltage);
		return error;
	}

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return ENODEV;

	if (OF_is_compatible(node, "regulator-fixed") &&
	    OF_getpropint(node, "regulator-min-microvolt", 0) == voltage)
		return 0;

	if (OF_is_compatible(node, "regulator-gpio") &&
	    regulator_type(node) == REGULATOR_VOLTAGE)
		return regulator_gpio_set(node, voltage);

	return ENODEV;
}

uint32_t
regulator_get_current(uint32_t phandle)
{
	struct regulator_device *rd;
	int node;

	if (phandle == 0)
		return 0;

	LIST_FOREACH(rd, &regulator_devices, rd_list) {
		if (rd->rd_phandle == phandle)
			break;
	}

	if (rd && rd->rd_get_current)
		return rd->rd_get_current(rd->rd_cookie);

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return 0;

	if (OF_is_compatible(node, "regulator-fixed"))
		return OF_getpropint(node, "regulator-min-microamp", 0);

	if (OF_is_compatible(node, "regulator-gpio") &&
	    regulator_type(node) == REGULATOR_CURRENT)
		return regulator_gpio_get(node);

	return 0;
}

int
regulator_set_current(uint32_t phandle, uint32_t current)
{
	struct regulator_device *rd;
	uint32_t old, delta;
	int error, node;

	if (phandle == 0)
		return ENODEV;

	LIST_FOREACH(rd, &regulator_devices, rd_list) {
		if (rd->rd_phandle == phandle)
			break;
	}

	/* Check limits. */
	if (rd && (current < rd->rd_amp_min || current > rd->rd_amp_max))
		return EINVAL;

	if (rd && rd->rd_set_current) {
		regulator_do_notify(rd->rd_phandle, current);

		old = rd->rd_get_current(rd->rd_cookie);
		error = rd->rd_set_current(rd->rd_cookie, current);
		if (current > old && rd->rd_ramp_delay > 0) {
			delta = current - old;
			delay(howmany(delta, rd->rd_ramp_delay));
		}

		regulator_do_notify(rd->rd_phandle, current);
		return error;
	}

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return ENODEV;

	if (OF_is_compatible(node, "regulator-fixed") &&
	    OF_getpropint(node, "regulator-min-microamp", 0) == current)
		return 0;

	if (OF_is_compatible(node, "regulator-gpio") &&
	    regulator_type(node) == REGULATOR_CURRENT)
		return regulator_gpio_set(node, current);

	return ENODEV;
}

uint32_t
regulator_gpio_get(int node)
{
	uint32_t *gpio, *gpios, *states;
	uint32_t idx, value;
	int glen, slen, i;

	pinctrl_byname(node, "default");

	if ((glen = OF_getproplen(node, "gpios")) <= 0)
		return EINVAL;
	if ((slen = OF_getproplen(node, "states")) <= 0)
		return EINVAL;

	if (slen % (2 * sizeof(uint32_t)) != 0)
		return EINVAL;

	gpios = malloc(glen, M_TEMP, M_WAITOK);
	states = malloc(slen, M_TEMP, M_WAITOK);

	OF_getpropintarray(node, "gpios", gpios, glen);
	OF_getpropintarray(node, "states", states, slen);

	i = 0;
	idx = 0;
	gpio = gpios;
	while (gpio && gpio < gpios + (glen / sizeof(uint32_t))) {
		if (gpio_controller_get_pin(gpio))
			idx |= (1 << i);
		gpio = gpio_controller_next_pin(gpio);
		i++;
	}

	value = 0;
	for (i = 0; i < slen / (2 * sizeof(uint32_t)); i++) {
		if (states[2 * i + 1] == idx) {
			value = states[2 * i];
			break;
		}
	}
	if (i >= slen / (2 * sizeof(uint32_t)))
		return 0;

	free(gpios, M_TEMP, glen);
	free(states, M_TEMP, slen);

	return value;
}

int
regulator_gpio_set(int node, uint32_t value)
{
	uint32_t phandle = OF_getpropint(node, "phandle", 0);
	uint32_t *gpio, *gpios, *states;
	uint32_t min, max;
	uint32_t idx;
	int glen, slen, i;

	pinctrl_byname(node, "default");

	if (regulator_type(node) == REGULATOR_VOLTAGE) {
		min = OF_getpropint(node, "regulator-min-microvolt", 0);
		max = OF_getpropint(node, "regulator-max-microvolt", 0);
	}

	if (regulator_type(node) == REGULATOR_CURRENT) {
		min = OF_getpropint(node, "regulator-min-microamp", 0);
		max = OF_getpropint(node, "regulator-max-microamp", 0);
	}

	/* Check limits. */
	if (value < min || value > max)
		return EINVAL;

	if ((glen = OF_getproplen(node, "gpios")) <= 0)
		return EINVAL;
	if ((slen = OF_getproplen(node, "states")) <= 0)
		return EINVAL;

	if (slen % (2 * sizeof(uint32_t)) != 0)
		return EINVAL;

	gpios = malloc(glen, M_TEMP, M_WAITOK);
	states = malloc(slen, M_TEMP, M_WAITOK);

	OF_getpropintarray(node, "gpios", gpios, glen);
	OF_getpropintarray(node, "states", states, slen);

	idx = 0;
	for (i = 0; i < slen / (2 * sizeof(uint32_t)); i++) {
		if (states[2 * i] < min || states[2 * i] > max)
			continue;
		if (states[2 * i] == value) {
			idx = states[2 * i + 1];
			break;
		}
	}
	if (i >= slen / (2 * sizeof(uint32_t)))
		return EINVAL;

	regulator_do_notify(phandle, value);
	
	i = 0;
	gpio = gpios;
	while (gpio && gpio < gpios + (glen / sizeof(uint32_t))) {
		gpio_controller_set_pin(gpio, !!(idx & (1 << i)));
		gpio = gpio_controller_next_pin(gpio);
		i++;
	}

	regulator_do_notify(phandle, value);

	free(gpios, M_TEMP, glen);
	free(states, M_TEMP, slen);

	return 0;
}

void
regulator_notify(struct regulator_notifier *rn)
{
	LIST_INSERT_HEAD(&regulator_notifiers, rn, rn_list);
}

void
regulator_do_notify(uint32_t phandle, uint32_t value)
{
	struct regulator_notifier *rn;

	LIST_FOREACH(rn, &regulator_notifiers, rn_list) {
		if (rn->rn_phandle == phandle)
			rn->rn_notify(rn->rn_cookie, value);
	}
}
