/* $OpenBSD: am335x.c,v 1.12 2020/03/31 10:33:10 kettenis Exp $ */

/*
 * Copyright (c) 2011 Uwe Stuehler <uwe@openbsd.org>
 * Copyright (c) 2013 Raphael Graf <r@undefined.ch>
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

#include <armv7/armv7/armv7var.h>

#define PRCM_SIZE	0x2000
#define PRCM_ADDR	0x44E00000

#define DMTIMERx_SIZE	0x80
#define DMTIMER0_ADDR	0x44E05000
#define DMTIMER1_ADDR	0x44E31000	/* 1MS */
#define DMTIMER2_ADDR	0x48040000
#define DMTIMER3_ADDR	0x48042000
#define DMTIMER4_ADDR	0x48044000
#define DMTIMER5_ADDR	0x48046000
#define DMTIMER6_ADDR	0x48048000
#define DMTIMER7_ADDR	0x4804A000
#define DMTIMER0_IRQ	66
#define DMTIMER1_IRQ	67
#define DMTIMER2_IRQ	68
#define DMTIMER3_IRQ	69
#define DMTIMER4_IRQ	92
#define DMTIMER5_IRQ	93
#define DMTIMER6_IRQ	94
#define DMTIMER7_IRQ	95

struct armv7_dev am335x_devs[] = {

	/*
	 * Power, Reset and Clock Manager
	 */

	{ .name = "prcm",
	  .unit = 0,
	  .mem = { { PRCM_ADDR, PRCM_SIZE } },
	},

	/*
	 * General Purpose Timers
	 */

	{ .name = "dmtimer",
	  .unit = 0,
	  .mem = { { DMTIMER2_ADDR, DMTIMERx_SIZE } },
	  .irq = { DMTIMER2_IRQ }
	},

	{ .name = "dmtimer",
	  .unit = 1,
	  .mem = { { DMTIMER3_ADDR, DMTIMERx_SIZE } },
	  .irq = { DMTIMER3_IRQ }
	},

	/* Terminator */
	{ .name = NULL,
	  .unit = 0
	}
};

void
am335x_init(void)
{
	armv7_set_devs(am335x_devs);
}
