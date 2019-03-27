/*-
 * Copyright (c) 2013 Ruslan Bukin <br@bsdpad.com>
 * Copyright (c) 2015 Semihalf.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_platform.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/intr.h>

#ifndef INTRNG
static int alpine_pic_decode_fdt(uint32_t iparent, uint32_t *intr,
    int *interrupt, int *trig, int *pol);

static int
alpine_pic_decode_fdt(uint32_t iparent, uint32_t *intr, int *interrupt,
    int *trig, int *pol)
{
	int rv = 0;

	rv = gic_decode_fdt(iparent, intr, interrupt, trig, pol);
	if (rv == 0) {
		/* This was recognized as our PIC and decoded. */
		interrupt = FDT_MAP_IRQ(iparent, interrupt);

		/* Configure the interrupt if callback provided */
		if (arm_config_irq)
			(*arm_config_irq)(*interrupt, *trig, *pol);
	}
	return (rv);
}

fdt_pic_decode_t fdt_pic_table[] = {
	&alpine_pic_decode_fdt,
	NULL
};
#endif
