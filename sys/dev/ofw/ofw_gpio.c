/*	$OpenBSD: ofw_gpio.c,v 1.4 2025/01/09 19:38:13 kettenis Exp $	*/
/*
 * Copyright (c) 2016, 2019 Mark Kettenis
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

#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_gpio.h>

LIST_HEAD(, gpio_controller) gpio_controllers =
	LIST_HEAD_INITIALIZER(gpio_controllers);

void
gpio_controller_register(struct gpio_controller *gc)
{
	int child;

	gc->gc_cells = OF_getpropint(gc->gc_node, "#gpio-cells", 2);
	gc->gc_phandle = OF_getpropint(gc->gc_node, "phandle", 0);
	if (gc->gc_phandle == 0)
		return;

	LIST_INSERT_HEAD(&gpio_controllers, gc, gc_list);

	/* Process GPIO hogs. */
	for (child = OF_child(gc->gc_node); child; child = OF_peer(child)) {
		uint32_t *gpios;
		uint32_t *gpio;
		int len, config, active;

		if (OF_getproplen(child, "gpio-hog") != 0)
			continue;

		len = OF_getproplen(child, "gpios");
		if (len <= 0)
			continue;

		/*
		 * These need to be processed in the order prescribed
		 * by the device tree binding.  First match wins.
		 */
		if (OF_getproplen(child, "input") == 0) {
			config = GPIO_CONFIG_INPUT;
			active = 0;
		} else if (OF_getproplen(child, "output-low") == 0) {
			config = GPIO_CONFIG_OUTPUT;
			active = 0;
		} else if (OF_getproplen(child, "output-high") == 0) {
			config = GPIO_CONFIG_OUTPUT;
			active = 1;
		} else
			continue;

		gpios = malloc(len, M_TEMP, M_WAITOK);
		OF_getpropintarray(child, "gpios", gpios, len);

		gpio = gpios;
		while (gpio && gpio < gpios + (len / sizeof(uint32_t))) {
			gc->gc_config_pin(gc->gc_cookie, gpio, config);
			if (config & GPIO_CONFIG_OUTPUT)
				gc->gc_set_pin(gc->gc_cookie, gpio, active);
			gpio += gc->gc_cells;
		}

		free(gpios, M_TEMP, len);
	}
}

void
gpio_controller_config_pin(uint32_t *cells, int config)
{
	struct gpio_controller *gc;
	uint32_t phandle = cells[0];

	LIST_FOREACH(gc, &gpio_controllers, gc_list) {
		if (gc->gc_phandle == phandle)
			break;
	}

	if (gc && gc->gc_config_pin)
		gc->gc_config_pin(gc->gc_cookie, &cells[1], config);
}

int
gpio_controller_get_pin(uint32_t *cells)
{
	struct gpio_controller *gc;
	uint32_t phandle = cells[0];
	int val = 0;

	LIST_FOREACH(gc, &gpio_controllers, gc_list) {
		if (gc->gc_phandle == phandle)
			break;
	}

	if (gc && gc->gc_get_pin)
		val = gc->gc_get_pin(gc->gc_cookie, &cells[1]);

	return val;
}

void
gpio_controller_set_pin(uint32_t *cells, int val)
{
	struct gpio_controller *gc;
	uint32_t phandle = cells[0];

	LIST_FOREACH(gc, &gpio_controllers, gc_list) {
		if (gc->gc_phandle == phandle)
			break;
	}

	if (gc && gc->gc_set_pin)
		gc->gc_set_pin(gc->gc_cookie, &cells[1], val);
}

uint32_t *
gpio_controller_next_pin(uint32_t *cells)
{
	struct gpio_controller *gc;
	uint32_t phandle = cells[0];

	LIST_FOREACH(gc, &gpio_controllers, gc_list)
		if (gc->gc_phandle == phandle)
			return cells + gc->gc_cells + 1;

	return NULL;
}

void *
gpio_controller_intr_establish(uint32_t *cells, int ipl, struct cpu_info *ci,
    int (*func)(void *), void *arg, char *name)
{
	struct gpio_controller *gc;
	uint32_t phandle = cells[0];

	LIST_FOREACH(gc, &gpio_controllers, gc_list) {
		if (gc->gc_phandle == phandle)
			break;
	}

	if (gc && gc->gc_intr_establish) {
		return gc->gc_intr_establish(gc->gc_cookie, &cells[1], ipl,
		    ci, func, arg, name);
	}

	return NULL;
}

void
gpio_controller_intr_disestablish(void *ih)
{
	fdt_intr_disestablish(ih);
}
