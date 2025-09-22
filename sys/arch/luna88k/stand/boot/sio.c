/*	$OpenBSD: sio.c,v 1.4 2023/01/10 17:10:57 miod Exp $	*/
/*	$NetBSD: sio.c,v 1.3 2013/01/21 11:58:12 tsutsui Exp $	*/

/*
 * Copyright (c) 1992 OMRON Corporation.
 *
 * This code is derived from software contributed to Berkeley by
 * OMRON Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)sio.c	8.1 (Berkeley) 6/10/93
 */
/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * OMRON Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)sio.c	8.1 (Berkeley) 6/10/93
 */

/* sio.c   NOV-25-1991 */

#define NSIO 2

#include <sys/param.h>
#include <machine/board.h>
#include <luna88k/stand/boot/samachdep.h>
#include <luna88k/stand/boot/sioreg.h>
#include <luna88k/stand/boot/rcvbuf.h>
#include <luna88k/stand/boot/kbdreg.h>

static int sioreg(int, int);

struct rcvbuf	rcvbuf[NSIO];

int	sioconsole = -1;
struct	siodevice *sio_addr[2];

int
siointr(int unit)
{
/*	struct siodevice *sio = sio_addr[unit]; */
	int rr0 = sioreg(REG(unit, RR0), 0);
	int rr1 = sioreg(REG(unit, RR1), 0);

	if (rr0 & RR0_RXAVAIL) {
		if (rr1 & RR1_FRAMING)
			return 1;

		if (rr1 & (RR1_PARITY | RR1_OVERRUN))
		    sioreg(REG(unit, WR0), WR0_ERRRST); /* Channel-A Error Reset */

		if (unit == 1) {
			int c = kbd_decode(sio_addr[unit]->sio_data);

			if ((c & KC_TYPE) == KC_CODE)
				PUSH_RBUF(unit, c);
		} else {
			PUSH_RBUF(unit, sio_addr[unit]->sio_data);
		}
		return 1;
	}

	return 0;
}

/*
 * Following are all routines needed for SIO to act as console
 */
#include <dev/cons.h>

void
siocnprobe(struct consdev *cp)
{
	sio_addr[0] = (struct siodevice *)OBIO_SIO;
	sio_addr[1] = (struct siodevice *)OBIO_SIO + 1;

	/* make sure hardware exists */
	if (badaddr(sio_addr[0], 4) != 0) {
		cp->cn_pri = CN_DEAD;
		return;
	}

	/* locate the major number */

	/* initialize required fields */
	cp->cn_dev = 0;
	cp->cn_pri = CN_LOWPRI;
}

void
siocninit(struct consdev *cp)
{
	int unit = cp->cn_dev;

	sioinit();
	sioconsole = unit;
}

int
siocngetc(dev_t dev)
{
	int c, unit = dev & ~0x80, poll = (dev & 0x80) != 0;

	siointr(unit);

	if (poll) {
		if (RBUF_EMPTY(unit))
			return 0;
		PEEK_RBUF(unit, c);
		return c;

	}

	while (RBUF_EMPTY(unit)) {
		DELAY(1);
		siointr(unit);
	}

	POP_RBUF(unit, c);
	return c;
}

void
siocnputc(dev_t dev, int c)
{
	int unit = dev;

	if (sioconsole == -1) {
		(void) sioinit();
		sioconsole = unit;
	}

	/* wait for any pending transmission to finish */
	while ((sioreg(REG(unit, RR0), 0) & RR0_TXEMPTY) == 0);

	sio_addr[unit]->sio_data = (c & 0xFF);

	/* wait for any pending transmission to finish */
	while ((sioreg(REG(unit, RR0), 0) & RR0_TXEMPTY) == 0);
}

/* SIO misc routines */

void
sioinit(void)
{
	RBUF_INIT(0);
	RBUF_INIT(1);

	sioreg(REG(0, WR0), WR0_CHANRST);		/* Channel-A Reset */

	sioreg(WR2A, WR2_VEC86  | WR2_INTR_1);		/* Set CPU BUS Interface Mode */
	sioreg(WR2B, 0);				/* Set Interrupt Vector */

	sioreg(REG(0, WR0), WR0_RSTINT);		/* Reset E/S Interrupt */
	sioreg(REG(0, WR4), WR4_BAUD96 | WR4_STOP1 | WR4_NPARITY);	/* Tx/Rx */
	sioreg(REG(0, WR3), WR3_RX8BIT | WR3_RXENBL);		/* Rx */
	sioreg(REG(0, WR5), WR5_TX8BIT | WR5_TXENBL | WR5_DTR | WR5_RTS);		/* Tx */
	sioreg(REG(0, WR0), WR0_RSTINT);		/* Reset E/S Interrupt */
	sioreg(REG(0, WR1), WR1_RXALLS);		/* Interrupted All Char. */

	sioreg(REG(1, WR0), WR0_CHANRST);		/* Channel-A Reset */

	sioreg(REG(1, WR0), WR0_RSTINT);		/* Reset E/S Interrupt */
	sioreg(REG(1, WR4), WR4_BAUD96 | WR4_STOP1 | WR4_NPARITY);	/* Tx/Rx */
	sioreg(REG(1, WR3), WR3_RX8BIT | WR3_RXENBL);		/* Rx */
	sioreg(REG(1, WR5), WR5_TX8BIT | WR5_TXENBL);		/* Tx */
	sioreg(REG(1, WR0), WR0_RSTINT);		/* Reset E/S Interrupt */
	sioreg(REG(1, WR1), WR1_RXALLS);		/* Interrupted All Char. */
}

int
sioreg(int reg, int val)
{
	int chan;

	chan = CHANNEL(reg);

	if (isStatusReg(reg)) {
		if (REGNO(reg) != 0)
		    sio_addr[chan]->sio_cmd = REGNO(reg);
		return(sio_addr[chan]->sio_stat);
	} else {
		if (REGNO(reg) != 0)
		    sio_addr[chan]->sio_cmd = REGNO(reg);
		sio_addr[chan]->sio_cmd = val;
		return(val);
	}
}
