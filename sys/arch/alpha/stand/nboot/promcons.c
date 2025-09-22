/*	$OpenBSD: promcons.c,v 1.1 2023/03/11 20:56:01 miod Exp $	*/
/*	$NetBSD: prom.c,v 1.2 1996/11/25 16:18:16 cgd Exp $	*/

/*  
 * Mach Operating System
 * Copyright (c) 1992 Carnegie Mellon University
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

#include <sys/types.h>
#include <machine/rpb.h>
#include <machine/prom.h>
#include <dev/cons.h>

#include "libsa.h"

void
prom_cnprobe(struct consdev *cn)
{
	char buf[4];
	int console;

	/* Look for console tty. */
	prom_getenv(PROM_E_TTY_DEV, buf, 4);
	console = buf[0] - '0';

	cn->cn_pri = CN_MIDPRI;
	cn->cn_dev = makedev(0, console);
}

void
prom_cninit(struct consdev *cn)
{
}

int
prom_cngetc(dev_t dev)
{
	static int stash = 0;
	int unit = dev & ~0x80;
	int poll = (dev & 0x80) != 0;
	int c;
	prom_return_t ret;

	if (stash != 0) {
		c = stash;
		if (!poll)
			stash = 0;
		return c;
	}

	for (;;) {
		ret.bits = prom_dispatch(PROM_R_GETC, unit, 0, 0, 0);
		if (ret.u.status == 0 || ret.u.status == 1) {
			c = ret.u.retval;
			if (poll)
				stash = c;
			return c;
		}
		if (poll)
			return 0;
	}
}

void
prom_cnputc(dev_t dev, int c)
{
	int unit = dev & ~0x80;
	prom_return_t ret;
	char cbuf = c;

	do {
		ret.bits = prom_dispatch(PROM_R_PUTS, unit,
		    (u_int64_t)&cbuf, 1, 0);
	} while ((ret.u.retval & 1) == 0);
}
