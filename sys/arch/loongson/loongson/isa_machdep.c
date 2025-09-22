/*	$OpenBSD: isa_machdep.c,v 1.5 2022/11/14 17:15:41 visa Exp $	*/

/*
 * Copyright (c) 2009, 2010 Miodrag Vallat.
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

/*
 * Legacy device support.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/autoconf.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include <dev/ic/i8259reg.h>

#include <dev/pci/pcivar.h>

#include <dev/isa/isareg.h>
#include <dev/isa/isavar.h>

#include <loongson/dev/bonitoreg.h>
#include <loongson/dev/bonitovar.h>
#include <loongson/dev/bonito_irq.h>

void	 loongson_set_isa_imr(uint);
void	 loongson_isa_splx(int);
void	 loongson_isa_setintrmask(int);

uint	loongson_isaimr;

void
loongson_set_isa_imr(uint newimr)
{
	uint imr1, imr2;

	imr1 = 0xff & ~newimr;
	imr1 &= ~(1 << 2);	/* enable cascade */
	imr2 = 0xff & ~(newimr >> 8);

	/*
	 * For some reason, trying to write the same value to the PIC
	 * registers causes an immediate system freeze (at least on the
	 * 2F and CS5536 based Lemote Yeeloong), so we only do this if
	 * the value changes.
	 * Note that interrupts have been disabled by the caller.
	 */
	if ((newimr ^ loongson_isaimr) & 0xff00)
		REGVAL8(BONITO_PCIIO_BASE + IO_ICU2 + 1) = imr2;
	if ((newimr ^ loongson_isaimr) & 0x00ff)
		REGVAL8(BONITO_PCIIO_BASE + IO_ICU1 + 1) = imr1;
	loongson_isaimr = newimr;
}

void
loongson_isa_specific_eoi(int bit)
{
	if (bit & 8) {
		REGVAL8(BONITO_PCIIO_BASE + IO_ICU2 + PIC_OCW2) =
		    OCW2_SELECT | OCW2_EOI | OCW2_SL | OCW2_ILS(bit & 7);
		bit = 2;
	}
	REGVAL8(BONITO_PCIIO_BASE + IO_ICU1 + PIC_OCW2) =
	    OCW2_SELECT | OCW2_EOI | OCW2_SL | OCW2_ILS(bit);
}

void
loongson_isa_splx(int newipl)
{
	struct cpu_info *ci = curcpu();

	/* Update masks to new ipl. Order highly important! */
	ci->ci_ipl = newipl;
	loongson_isa_setintrmask(newipl);

	/* Trigger deferred clock interrupt if it is now unmasked. */
	if (ci->ci_clock_deferred && newipl < IPL_CLOCK)
		md_triggerclock();

	/* If we still have softints pending trigger processing. */
	if (ci->ci_softpending != 0 && newipl < IPL_SOFTINT)
		setsoftintr0();
}

void
loongson_isa_setintrmask(int level)
{
	uint64_t active;
	register_t sr;

	active = bonito_intem & ~bonito_imask[level];

	sr = disableintr();
	bonito_setintrmask(level);
	loongson_set_isa_imr(BONITO_ISA_MASK(active));
	setsr(sr);
}

void
loongson_generic_isa_attach_hook(struct device *parent, struct device *self,
    struct isabus_attach_args *iba)
{
	loongson_isaimr = 0;
	/* overrides bonito callback */
	register_splx_handler(loongson_isa_splx);
}
