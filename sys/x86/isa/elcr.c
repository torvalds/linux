/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 John Baldwin <jhb@FreeBSD.org>
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * The ELCR is a register that controls the trigger mode and polarity of
 * EISA and ISA interrupts.  In FreeBSD 3.x and 4.x, the ELCR was only
 * consulted for determining the appropriate trigger mode of EISA
 * interrupts when using an APIC.  However, it seems that almost all
 * systems that include PCI also include an ELCR that manages the ISA
 * IRQs 0 through 15.  Thus, we check for the presence of an ELCR on
 * every machine by checking to see if the values found at bootup are
 * sane.  Note that the polarity of ISA and EISA IRQs are linked to the
 * trigger mode.  All edge triggered IRQs use active-hi polarity, and
 * all level triggered interrupts use active-lo polarity.
 *
 * The format of the ELCR is simple: it is a 16-bit bitmap where bit 0
 * controls IRQ 0, bit 1 controls IRQ 1, etc.  If the bit is zero, the
 * associated IRQ is edge triggered.  If the bit is one, the IRQ is
 * level triggered.
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/systm.h>
#include <machine/intr_machdep.h>

#define	ELCR_PORT	0x4d0
#define	ELCR_MASK(irq)	(1 << (irq))

static int elcr_status;
int elcr_found;

/*
 * Check to see if we have what looks like a valid ELCR.  We do this by
 * verifying that IRQs 0, 1, 2, and 13 are all edge triggered.
 */
int
elcr_probe(void)
{
	int i;

	elcr_status = inb(ELCR_PORT) | inb(ELCR_PORT + 1) << 8;
	if ((elcr_status & (ELCR_MASK(0) | ELCR_MASK(1) | ELCR_MASK(2) |
	    ELCR_MASK(8) | ELCR_MASK(13))) != 0)
		return (ENXIO);
	if (bootverbose) {
		printf("ELCR Found.  ISA IRQs programmed as:\n");
		for (i = 0; i < 16; i++)
			printf(" %2d", i);
		printf("\n");
		for (i = 0; i < 16; i++)
			if (elcr_status & ELCR_MASK(i))
				printf("  L");
			else
				printf("  E");
		printf("\n");
	}
	if (resource_disabled("elcr", 0))
		return (ENXIO);
	elcr_found = 1;
	return (0);
}

/*
 * Returns 1 for level trigger, 0 for edge.
 */
enum intr_trigger
elcr_read_trigger(u_int irq)
{

	KASSERT(elcr_found, ("%s: no ELCR was found!", __func__));
	KASSERT(irq <= 15, ("%s: invalid IRQ %u", __func__, irq));
	if (elcr_status & ELCR_MASK(irq))
		return (INTR_TRIGGER_LEVEL);
	else
		return (INTR_TRIGGER_EDGE);
}

/*
 * Set the trigger mode for a specified IRQ.  Mode of 0 means edge triggered,
 * and a mode of 1 means level triggered.
 */
void
elcr_write_trigger(u_int irq, enum intr_trigger trigger)
{
	int new_status;

	KASSERT(elcr_found, ("%s: no ELCR was found!", __func__));
	KASSERT(irq <= 15, ("%s: invalid IRQ %u", __func__, irq));
	if (trigger == INTR_TRIGGER_LEVEL)
		new_status = elcr_status | ELCR_MASK(irq);
	else
		new_status = elcr_status & ~ELCR_MASK(irq);
	if (new_status == elcr_status)
		return;
	elcr_status = new_status;
	if (irq >= 8)
		outb(ELCR_PORT + 1, elcr_status >> 8);
	else
		outb(ELCR_PORT, elcr_status & 0xff);
}

void
elcr_resume(void)
{

	KASSERT(elcr_found, ("%s: no ELCR was found!", __func__));
	outb(ELCR_PORT, elcr_status & 0xff);
	outb(ELCR_PORT + 1, elcr_status >> 8);
}
