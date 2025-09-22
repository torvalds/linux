/* $OpenBSD: omap3.c,v 1.6 2020/03/31 10:33:10 kettenis Exp $ */

/*
 * Copyright (c) 2011 Uwe Stuehler <uwe@openbsd.org>
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

#define PRCM_ADDR	0x48004000
#define PRCM_SIZE	0x2000

#define GPTIMERx_SIZE	0x100
#define GPTIMER1_ADDR	0x48318000
#define GPTIMER1_IRQ	37
#define GPTIMER2_ADDR	0x49032000
#define GPTIMER2_IRQ	38

#define USBTLL_ADDR	0x48062000
#define USBTLL_SIZE	0x1000

struct armv7_dev omap3_devs[] = {

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

	{ .name = "gptimer",
	  .unit = 1,			/* XXX see gptimer.c */
	  .mem = { { GPTIMER1_ADDR, GPTIMERx_SIZE } },
	  .irq = { GPTIMER1_IRQ }
	},

	{ .name = "gptimer",
	  .unit = 0,			/* XXX see gptimer.c */
	  .mem = { { GPTIMER2_ADDR, GPTIMERx_SIZE } },
	  .irq = { GPTIMER2_IRQ }
	},

	/*
	 * USB
	 */

	{ .name = "omusbtll",
	  .unit = 0,
	  .mem = { { USBTLL_ADDR, USBTLL_SIZE } },
	},

	/* Terminator */
	{ .name = NULL,
	  .unit = 0,
	}
};

void
omap3_init(void)
{
	armv7_set_devs(omap3_devs);
}
