/*	$OpenBSD: if_le_dec.c,v 1.6 2014/12/22 02:28:51 tedu Exp $	*/
/*	$NetBSD: if_le_dec.c,v 1.12 2001/11/13 12:49:45 lukem Exp $	*/

/*-
 * Copyright (c) 1997 Jonathan Stone. All rights reserved.
 * Copyright (c) 1995 Charles M. Hannum.  All rights reserved.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell and Rick Macklem.
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
 *	@(#)if_le.c	8.2 (Berkeley) 11/16/93
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/syslog.h>
#include <sys/socket.h>
#include <sys/device.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <dev/ic/lancereg.h>
#include <dev/ic/lancevar.h>
#include <dev/ic/am7990reg.h>
#include <dev/ic/am7990var.h>

#include <dev/tc/if_levar.h>
#include <dev/tc/tcvar.h>

#include <machine/bus.h>

/* access LANCE registers */
void le_dec_writereg(volatile u_short *regptr, u_short val);
#define	LERDWR(cntl, src, dst)	{ (dst) = (src); tc_mb(); }
#define	LEWREG(src, dst)	le_dec_writereg(&(dst), (src))

void le_dec_wrcsr(struct lance_softc *, u_int16_t, u_int16_t);
u_int16_t le_dec_rdcsr(struct lance_softc *, u_int16_t);

void
dec_le_common_attach(struct am7990_softc *sc, u_char *eap)
{
	struct lance_softc *lsc = &sc->lsc;
	int i;

	lsc->sc_rdcsr = le_dec_rdcsr;
	lsc->sc_wrcsr = le_dec_wrcsr;
	lsc->sc_hwinit = NULL;

	lsc->sc_conf3 = 0;
	lsc->sc_addr = 0;
	lsc->sc_memsize = 65536;

	/*
	 * Get the ethernet address out of rom
	 */
	for (i = 0; i < sizeof(lsc->sc_arpcom.ac_enaddr); i++) {
		lsc->sc_arpcom.ac_enaddr[i] = *eap;
		eap += 4;
	}

	am7990_config(sc);
}

void
le_dec_wrcsr(struct lance_softc *sc, u_int16_t port, u_int16_t val)
{
	struct lereg1 *ler1 = ((struct le_softc *)sc)->sc_r1;

	LEWREG(port, ler1->ler1_rap);
	LERDWR(port, val, ler1->ler1_rdp);
}

u_int16_t
le_dec_rdcsr(struct lance_softc *sc, u_int16_t port)
{
	struct lereg1 *ler1 = ((struct le_softc *)sc)->sc_r1;
	u_int16_t val;

	LEWREG(port, ler1->ler1_rap);
	LERDWR(0, ler1->ler1_rdp, val);
	return (val);
}

/*
 * Write a lance register port, reading it back to ensure success. This seems
 * to be necessary during initialization, since the chip appears to be a bit
 * pokey sometimes.
 */
void
le_dec_writereg(volatile u_short *regptr, u_short val)
{
	int i = 0;

	while (*regptr != val) {
		*regptr = val;
		tc_mb();
		if (++i > 10000) {
			printf("le: Reg did not settle (to x%x): x%x\n", val,
			    *regptr);
			return;
		}
		DELAY(100);
	}
}

/*
 * Routines for accessing the transmit and receive buffers are provided
 * by lance.c, because of the LE_NEED_BUF_* macros defined above.
 * Unfortunately, CPU addressing of these buffers is done in one of
 * 3 ways:
 * - contiguous (for the 3max and turbochannel option card)
 * - gap2, which means shorts (2 bytes) interspersed with short (2 byte)
 *   spaces (for the pmax, vax 3400, and ioasic LANCE descriptors)
 * - gap16, which means 16bytes interspersed with 16byte spaces
 *   for buffers which must begin on a 32byte boundary (for 3min, maxine,
 *   and alpha)
 * The buffer offset is the logical byte offset, assuming contiguous storage.
 */
