/*	$OpenBSD: platform.c,v 1.27 2021/05/16 03:39:28 jsg Exp $	*/
/*
 * Copyright (c) 2014 Patrick Wildt <patrick@blueri.se>
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

#include <sys/param.h>

#include <machine/bus.h>

#include <arm/mainbus/mainbus.h>
#include <armv7/armv7/armv7var.h>
#include <armv7/armv7/armv7_machdep.h>
#include <arm/cortex/smc.h>

#include "omap.h"

static struct armv7_platform *platform;

void	agtimer_init(void);

extern void	cduart_init_cons(void);
extern void	exuart_init_cons(void);
extern void	imxuart_init_cons(void);
extern void	com_fdt_init_cons(void);
extern void	pluart_init_cons(void);
extern void	simplefb_init_cons(bus_space_tag_t);

struct armv7_platform *omap_platform_match(void);

struct armv7_platform * (*plat_match[])(void) = {
#if NOMAP > 0
	omap_platform_match,
#endif
};

struct board_dev no_devs[] = {
	{ NULL,	0 }
};

void
platform_init(void)
{
	int i;

	agtimer_init();

	for (i = 0; i < nitems(plat_match); i++) {
		platform = plat_match[i]();
		if (platform != NULL)
			break;
	}

	if (platform == NULL)
		return;

	cpuresetfn = platform_watchdog_reset;
	powerdownfn = platform_powerdown;
	if (platform->board_init)
		platform->board_init();
}

void
platform_smc_write(bus_space_tag_t iot, bus_space_handle_t ioh, bus_size_t off,
    uint32_t op, uint32_t val)
{
	if (platform && platform->smc_write)
		platform->smc_write(iot, ioh, off, op, val);
	else
		bus_space_write_4(iot, ioh, off, val);
}

void
platform_init_cons(void)
{
	if (platform && platform->init_cons) {
		platform->init_cons();
		return;
	}
	cduart_init_cons();
	exuart_init_cons();
	imxuart_init_cons();
	com_fdt_init_cons();
	pluart_init_cons();
	simplefb_init_cons(&armv7_bs_tag);
}

void
platform_init_mainbus(struct device *self)
{
	if (platform && platform->init_mainbus)
		platform->init_mainbus(self);
	else
		mainbus_legacy_found(self, "cortex");
}

void
platform_watchdog_reset(void)
{
	if (platform && platform->watchdog_reset)
		platform->watchdog_reset();
}

void
platform_powerdown(void)
{
	if (platform && platform->powerdown)
		platform->powerdown();
}

struct board_dev *
platform_board_devs(void)
{
	if (platform && platform->devs)
		return (platform->devs);
	else
		return (no_devs);
}
