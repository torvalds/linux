/*	$OpenBSD: gateA20.c,v 1.3 2017/09/08 05:36:51 deraadt Exp $	*/

/*
 * Ported to boot 386BSD by Julian Elischer (julian@tfs.com) Sept 1992
 *
 * Mach Operating System
 * Copyright (c) 1992, 1991 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */

#include <sys/param.h>
#include <machine/pio.h>
#include <dev/ic/i8042reg.h>
#include <dev/isa/isareg.h>

#include "libsa.h"

int ps2model = 0;			/* Not set in amd64 */

#define KB_A20		0xdf		/* enable A20,
					   enable output buffer full interrupt
					   enable data line
					   enable clock line */


/*
 * "Probe"-style routine (no parameters) to turn A20 on
 */
void
gateA20on(void)
{
	gateA20(1);
}


/*
 * Gate A20 for high memory
 */
void
gateA20(int on)
{
	if (ps2model == 0xf82 ||
	    (inb(IO_KBD + KBSTATP) == 0xff && inb(IO_KBD + KBDATAP) == 0xff)) {
		int data;

		/* Try to use 0x92 to turn on A20 */
		if (on) {
			data = inb(0x92);
			outb(0x92, data | 0x2);
		} else {
			data = inb(0x92);
			outb(0x92, data & ~0x2);
		}
	} else {

		while (inb(IO_KBD + KBSTATP) & KBS_IBF);

		while (inb(IO_KBD + KBSTATP) & KBS_DIB)
			(void)inb(IO_KBD + KBDATAP);

		outb(IO_KBD + KBCMDP, KBC_CMDWOUT);
		while (inb(IO_KBD + KBSTATP) & KBS_IBF);

		if (on)
			outb(IO_KBD + KBDATAP, KB_A20);
		else
			outb(IO_KBD + KBDATAP, 0xcd);
		while (inb(IO_KBD + KBSTATP) & KBS_IBF);

		while (inb(IO_KBD + KBSTATP) & KBS_DIB)
			(void)inb(IO_KBD + KBDATAP);
	}
}
