/*	$OpenBSD: z8530ms.c,v 1.4 2016/10/24 06:13:52 deraadt Exp $	*/
/*	$NetBSD: ms.c,v 1.12 1997/07/17 01:17:47 jtk Exp $	*/

/*
 * Copyright (c) 2002, 2009, Miodrag Vallat
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
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
 *	@(#)ms.c	8.1 (Berkeley) 6/11/93
 */

/*
 * Zilog Z8530 Dual UART driver (mouse interface)
 *
 * This is the "slave" driver that will be attached to
 * the "zsc" driver for a Sun mouse.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/syslog.h>

#include <dev/ic/z8530reg.h>
#include <machine/z8530var.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsmousevar.h>
#include <dev/sun/sunmsvar.h>

/*
 * How many input characters we can buffer.
 * Note: must be a power of two!
 */
#define	MS_RX_RING_SIZE	256
#define MS_RX_RING_MASK (MS_RX_RING_SIZE-1)

struct zsms_softc {
	struct	sunms_softc sc_base;
	struct	zs_chanstate *sc_cs;

	/* Flags to communicate with zsms_softint() */
	volatile int sc_intr_flags;
#define	INTR_RX_OVERRUN 	0x01
#define INTR_TX_EMPTY   	0x02
#define INTR_ST_CHECK   	0x04
#define	INTR_BPS_CHANGE		0x08

	/*
	 * The receive ring buffer.
	 */
	uint	sc_rbget;		/* ring buffer `get' index */
	volatile uint	sc_rbput;	/* ring buffer `put' index */
	uint16_t sc_rbuf[MS_RX_RING_SIZE]; /* rr1, data pairs */
};

/*
 * autoconf glue.
 */

int	zsms_match(struct device *, void *, void *);
void	zsms_attach(struct device *, struct device *, void *);

const struct cfattach zsms_ca = {
	sizeof(struct zsms_softc), zsms_match, zsms_attach
};

struct cfdriver zsms_cd = {
	NULL, "zsms", DV_DULL
};

/*
 * wsmouse accessops.
 */

void	zsms_disable(void *);
int	zsms_enable(void *);

const struct wsmouse_accessops zsms_accessops = {
	zsms_enable,
	sunms_ioctl,
	zsms_disable
};

/*
 * zs glue.
 */

void	zsms_rxint(struct zs_chanstate *);
void	zsms_softint(struct zs_chanstate *);
void	zsms_stint(struct zs_chanstate *, int);
void	zsms_txint(struct zs_chanstate *);

struct zsops zsops_ms = {
	zsms_rxint,	/* receive char available */
	zsms_stint,	/* external/status */
	zsms_txint,	/* xmit buffer empty */
	zsms_softint	/* process software interrupt */
};

void	zsms_speed_change(void *, uint);

/*
 * autoconf glue.
 */

int
zsms_match(struct device *parent, void *vcf, void *aux)
{
	struct cfdata *cf = vcf;
	struct zsc_attach_args *args = aux;
	int rc;

	/* If we're not looking for a mouse, just exit */
	if (strcmp(args->type, "mouse") != 0)
		return 0;

	rc = 10;

	/* Exact match is better than wildcard. */
	if (cf->cf_loc[ZSCCF_CHANNEL] == args->channel)
		rc += 2;

	/* This driver accepts wildcard. */
	if (cf->cf_loc[ZSCCF_CHANNEL] == ZSCCF_CHANNEL_DEFAULT)
		rc += 1;

	return rc;
}

void
zsms_attach(struct device *parent, struct device *self, void *aux)
{
	struct zsc_softc *zsc = (struct zsc_softc *)parent;
	struct zsms_softc *sc = (struct zsms_softc *)self;
	struct zsc_attach_args *args = aux;
	struct zs_chanstate *cs;
	int channel;
	int s;

	channel = args->channel;
	cs = zsc->zsc_cs[channel];
	cs->cs_private = sc;
	cs->cs_ops = &zsops_ms;
	sc->sc_cs = cs;

	/* Initialize hardware. */
	s = splzs();
	zs_write_reg(cs, 9, channel == 0 ?  ZSWR9_A_RESET : ZSWR9_B_RESET);
	/* disable interrupts until the mouse is enabled */
	CLR(cs->cs_preg[1], ZSWR1_RIE | ZSWR1_SIE | ZSWR1_TIE);
	/* 8 data bits is already our default */
	/* no parity, 2 stop bits */
	CLR(cs->cs_preg[4], ZSWR4_SBMASK | ZSWR4_PARMASK);
	SET(cs->cs_preg[4], ZSWR4_TWOSB);
	(void)zs_set_speed(cs, INIT_SPEED);
	zs_loadchannelregs(cs);
	splx(s);

	sc->sc_base.sc_speed_change = zsms_speed_change;

	sunms_attach(&sc->sc_base, &zsms_accessops);
}

/*
 * wsmouse accessops.
 */

void
zsms_disable(void *v)
{
	struct zsms_softc *sc = v;
	struct zs_chanstate *cs = sc->sc_cs;
	int s;

	s = splzs();
	/* disable RX and status change interrupts */
	CLR(cs->cs_preg[1], ZSWR1_RIE | ZSWR1_SIE);
	zs_loadchannelregs(cs);
	splx(s);
}

int
zsms_enable(void *v)
{
	struct zsms_softc *sc = v;
	struct zs_chanstate *cs = sc->sc_cs;
	int s;

	s = splzs();
	/* enable RX and status change interrupts */
	SET(cs->cs_preg[1], ZSWR1_RIE | ZSWR1_SIE);
	zs_loadchannelregs(cs);
	splx(s);

	return 0;
}

/*
 * zs glue.
 */

void
zsms_rxint(struct zs_chanstate *cs)
{
	struct zsms_softc *sc = cs->cs_private;
	int put, put_next;
	u_char rr0, rr1, c;

	put = sc->sc_rbput;

	for (;;) {
		/*
		 * First read the status, because reading the received char
		 * destroys the status of this char.
		 */
		rr1 = zs_read_reg(cs, 1);
		c = zs_read_data(cs);

		/*
		 * Note that we do not try to change speed upon encountering
		 * framing errors, as this is not as reliable as breaks.
		 */
		if (ISSET(rr1, ZSRR1_FE | ZSRR1_DO | ZSRR1_PE)) {
			/* Clear the receive error. */
			zs_write_csr(cs, ZSWR0_RESET_ERRORS);
		}

		if (sc->sc_base.sc_state != STATE_RATE_CHANGE) {
			sc->sc_rbuf[put] = (c << 8) | rr1;
			put_next = (put + 1) & MS_RX_RING_MASK;

			/* Would overrun if increment makes (put==get). */
			if (put_next == sc->sc_rbget) {
				sc->sc_intr_flags |= INTR_RX_OVERRUN;
				break;
			} else {
				/* OK, really increment. */
				put = put_next;
			}
		}

		rr0 = zs_read_csr(cs);
		if (!ISSET(rr0, ZSRR0_RX_READY))
			break;
	}

	/* Done reading. */
	sc->sc_rbput = put;

	cs->cs_softreq = 1;
}

void
zsms_txint(struct zs_chanstate *cs)
{
	/*
	 * This function should never be invoked as we don't accept TX
	 * interrupts.  If someone alters our configuration behind our
	 * back, just disable TX interrupts again.
	 */
	zs_write_csr(cs, ZSWR0_RESET_TXINT);

	/* disable tx interrupts */
	CLR(cs->cs_preg[1], ZSWR1_TIE);
	zs_loadchannelregs(cs);
}

void
zsms_stint(struct zs_chanstate *cs, int force)
{
	struct zsms_softc *sc = cs->cs_private;
	uint8_t rr0, delta;

	rr0 = zs_read_csr(cs);
	zs_write_csr(cs, ZSWR0_RESET_STATUS);

	/*
	 * A break can occur if the speed is not correct.
	 * However, we do not change speed until we get the second
	 * break, for switching speed when the mouse is unplugged
	 * will trigger a break and thus we'd loop changing speeds
	 * until the mouse is plugged again.
	 */
	if (!force && ISSET(rr0, ZSRR0_BREAK)) {
		if (sc->sc_base.sc_state != STATE_RATE_CHANGE &&
		    ++sc->sc_base.sc_brk > 1) {
			sc->sc_intr_flags |= INTR_BPS_CHANGE;
			sc->sc_base.sc_state = STATE_RATE_CHANGE;
			cs->cs_softreq = 1;
#ifdef DEBUG
			printf("%s: break detected, changing speed\n",
			    sc->sc_base.sc_dev.dv_xname);
#endif
		}
	}

	if (!force)
		delta = rr0 ^ cs->cs_rr0;
	else
		delta = cs->cs_rr0_mask;
	cs->cs_rr0 = rr0;

	if (ISSET(delta, cs->cs_rr0_mask)) {
		SET(cs->cs_rr0_delta, delta);
		
		sc->sc_intr_flags |= INTR_ST_CHECK;
		cs->cs_softreq = 1;
	}
}

void
zsms_softint(struct zs_chanstate *cs)
{
	struct zsms_softc *sc;
	int get, c, s, s2;
	int intr_flags;
	u_short ring_data;

	sc = cs->cs_private;

	/* Atomically get and clear flags. */
	s = spltty();
	s2 = splzs();
	intr_flags = sc->sc_intr_flags;
	sc->sc_intr_flags = 0;
	/* Now lower to spltty for the rest. */
	splx(s2);

	/*
	 * If we have a baud rate change pending, do it now.
	 * This will reset the rx ring, so we can proceed safely.
	 */
	if (ISSET(intr_flags, INTR_BPS_CHANGE)) {
		CLR(intr_flags, INTR_RX_OVERRUN);
		sunms_speed_change(&sc->sc_base);
	}

	/*
	 * Copy data from the receive ring, if any, to the event layer.
	 */
	get = sc->sc_rbget;
	while (get != sc->sc_rbput) {
		ring_data = sc->sc_rbuf[get];
		get = (get + 1) & MS_RX_RING_MASK;

		/* low byte of ring_data is rr1 */
		c = (ring_data >> 8) & 0xff;

		if (ring_data & ZSRR1_DO)
			SET(intr_flags, INTR_RX_OVERRUN);

		/* Pass this up to the "middle" layer. */
		sunms_input(&sc->sc_base, c);
	}
	if (ISSET(intr_flags, INTR_RX_OVERRUN))
		log(LOG_ERR, "%s: input overrun\n",
		    sc->sc_base.sc_dev.dv_xname);
	sc->sc_rbget = get;

	/*
	 * Status line change.  Not expected except for break conditions.
	 */
	if (ISSET(intr_flags, INTR_ST_CHECK)) {
		cs->cs_rr0_delta = 0;
	}

	splx(s);
}

/*
 * Reinitialize the line to a different speed.  Invoked at spltty().
 */
void
zsms_speed_change(void *v, uint bps)
{
	struct zsms_softc *sc = v;
	struct zs_chanstate *cs = sc->sc_cs;
	uint8_t rr0;
	int s;

	s = splzs();

	/*
	 * Eat everything on the line.
	 */
	for (;;) {
		rr0 = zs_read_csr(cs);
		if (!ISSET(rr0, ZSRR0_RX_READY))
			break;
		(void)zs_read_data(cs);
	}

	(void)zs_set_speed(cs, sc->sc_base.sc_bps);
	zs_loadchannelregs(cs);
	zsms_stint(cs, 1);

	sc->sc_rbget = sc->sc_rbput = 0;

	splx(s);
}
