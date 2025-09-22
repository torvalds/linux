/*	$OpenBSD: romcons.c,v 1.2 2023/03/11 10:33:27 aoyama Exp $	*/
/*
 * Copyright (c) 2022 Kenji Aoyama
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
 * Mach Operating System
 * Copyright (c) 1993-1991 Carnegie Mellon University
 * Copyright (c) 1991 OMRON Corporation
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON AND OMRON ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON AND OMRON DISCLAIM ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

/*
 * LUNA-88K{,2} ROM console routines:
 * Enables printing of boot messages before consinit().
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/device.h>

#include <dev/cons.h>

#include <machine/intr.h>

#define __ROM_FUNC_TABLE	((int **)0x00001100)
#define ROMGETC()	(*(int (*)(void))__ROM_FUNC_TABLE[3])()
#define ROMPUTC(x)	(*(void (*)(int))__ROM_FUNC_TABLE[4])(x)

void
romcnprobe(struct consdev *cp)
{
	cp->cn_dev = makedev(14, 0);
	cp->cn_pri = CN_LOWPRI;
}

void
romcninit(struct consdev *cp)
{
	/* Nothing to do */
}

int
romcngetc(dev_t dev)
{
	int s, c;

	do {
		s = splhigh();
		c = ROMGETC();
		splx(s);
	} while (c == -1);
	return c;
}

void
romcnputc(dev_t dev, int c)
{
	int s;

	s = splhigh();
	ROMPUTC(c);
	splx(s);
}

/*
 * This is to fake out the console routines, while booting.
 * We could use directly the rom console, but we want to be able to
 * configure a kernel without rom since we do not necessarily need a
 * full-blown console driver.
 */
cons_decl(rom);
extern void nullcnpollc(dev_t, int);

struct consdev romcons = {
	NULL, 
	NULL, 
	romcngetc, 
	romcnputc,
	nullcnpollc,
	NULL,
	makedev(14, 0),
	CN_LOWPRI,
};

struct consdev *cn_tab = &romcons;
