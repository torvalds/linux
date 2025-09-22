/*	$OpenBSD: omap_machdep.c,v 1.13 2021/03/25 04:12:01 jsg Exp $	*/
/*
 * Copyright (c) 2013 Sylvestre Gallon <ccna.syl@gmail.com>
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
#include <sys/device.h>
#include <sys/systm.h>

#include <machine/bus.h>

#include <arm/mainbus/mainbus.h>
#include <armv7/armv7/armv7_machdep.h>

extern void omap4_smc_call(uint32_t, uint32_t);
extern void omdog_reset(void);
extern struct board_dev *omap_board_devs(void);
extern void omap_board_init(void);

void
omap_platform_smc_write(bus_space_tag_t iot, bus_space_handle_t ioh,
    bus_size_t off, uint32_t op, uint32_t val)
{
	switch (op) {
	case 0x100:	/* PL310 DEBUG */
	case 0x102:	/* PL310 CTL */
		break;
	default:
		panic("platform_smc_write: invalid operation %d", op);
	}

	omap4_smc_call(op, val);
}

void
omap_platform_init_mainbus(struct device *self)
{
	mainbus_legacy_found(self, "cortex");
	mainbus_legacy_found(self, "omap");
}

void
omap_platform_watchdog_reset(void)
{
	omdog_reset();
}

void
omap_platform_powerdown(void)
{

}

void
omap_platform_board_init(void)
{
	omap_board_init();
}

struct armv7_platform omap_platform = {
	.board_init = omap_platform_board_init,
	.smc_write = omap_platform_smc_write,
	.watchdog_reset = omap_platform_watchdog_reset,
	.powerdown = omap_platform_powerdown,
	.init_mainbus = omap_platform_init_mainbus,
};

struct armv7_platform *
omap_platform_match(void)
{
	struct board_dev *devs;

	devs = omap_board_devs();
	if (devs == NULL)
		return (NULL);

	omap_platform.devs = devs;
	return (&omap_platform);
}
