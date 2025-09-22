/*	$OpenBSD: z8530kbd.c,v 1.32 2024/05/13 01:15:50 jsg Exp $	*/
/*	$NetBSD: z8530tty.c,v 1.77 2001/05/30 15:24:24 lukem Exp $	*/

/*-
 * Copyright (c) 1993, 1994, 1995, 1996, 1997, 1998, 1999
 *	Charles M. Hannum.  All rights reserved.
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
 *	This product includes software developed by Charles M. Hannum.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 */

/*
 * Copyright (c) 1994 Gordon W. Ross
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
 *	@(#)zs.c	8.1 (Berkeley) 7/19/93
 */

/*
 * Zilog Z8530 Dual UART driver (tty interface)
 *
 * This is the "slave" driver that will be attached to
 * the "zsc" driver for plain "tty" async. serial lines.
 *
 * Credits, history:
 *
 * The original version of this code was the sparc/dev/zs.c driver
 * as distributed with the Berkeley 4.4 Lite release.  Since then,
 * Gordon Ross reorganized the code into the current parent/child
 * driver scheme, separating the Sun keyboard and mouse support
 * into independent child drivers.
 *
 * RTS/CTS flow-control support was a collaboration of:
 *	Gordon Ross <gwr@netbsd.org>,
 *	Bill Studenmund <wrstuden@loki.stanford.edu>
 *	Ian Dall <Ian.Dall@dsto.defence.gov.au>
 *
 * The driver was massively overhauled in November 1997 by Charles Hannum,
 * fixing *many* bugs, and substantially improving performance.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/device.h>
#include <sys/conf.h>
#include <sys/ioctl.h>
#include <sys/malloc.h>
#include <sys/tty.h>
#include <sys/time.h>
#include <sys/kernel.h>
#include <sys/syslog.h>

#include <machine/autoconf.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wskbdvar.h>

#include <dev/sun/sunkbdreg.h>
#include <dev/sun/sunkbdvar.h>

#include <dev/ic/z8530reg.h>
#include <machine/z8530var.h>

#include <dev/cons.h>

/*
 * How many input characters we can buffer.
 * The port-specific var.h may override this.
 * Note: must be a power of two!
 */
#ifndef	ZSKBD_RING_SIZE
#define	ZSKBD_RING_SIZE	2048
#endif

struct cfdriver zskbd_cd = {
	NULL, "zskbd", DV_TTY
};

/*
 * Make this an option variable one can patch.
 * But be warned:  this must be a power of 2!
 */
u_int zskbd_rbuf_size = ZSKBD_RING_SIZE;

/* Stop input when 3/4 of the ring is full; restart when only 1/4 is full. */
u_int zskbd_rbuf_hiwat = (ZSKBD_RING_SIZE * 1) / 4;
u_int zskbd_rbuf_lowat = (ZSKBD_RING_SIZE * 3) / 4;

struct zskbd_softc {
	struct sunkbd_softc	sc_base;

	struct	zs_chanstate *zst_cs;

	struct timeout zst_diag_ch;

	u_int zst_overflows,
	      zst_floods,
	      zst_errors;

	int zst_hwflags,	/* see z8530var.h */
	    zst_swflags;	/* TIOCFLAG_SOFTCAR, ... <ttycom.h> */

	u_int zst_r_hiwat,
	      zst_r_lowat;
	u_char *volatile zst_rbget,
	       *volatile zst_rbput;
	volatile u_int zst_rbavail;
	u_char *zst_rbuf,
	       *zst_ebuf;

	/*
	 * The transmit byte count and address are used for pseudo-DMA
	 * output in the hardware interrupt code.  PDMA can be suspended
	 * to get pending changes done; heldtbc is used for this.  It can
	 * also be stopped for ^S; this sets TS_TTSTOP in tp->t_state.
	 */
	u_char *zst_tba;		/* transmit buffer address */
	u_int zst_tbc,			/* transmit byte count */
	      zst_heldtbc;		/* held tbc while xmission stopped */

	u_char zst_tbuf[ZSKBD_RING_SIZE];
	u_char *zst_tbeg, *zst_tend, *zst_tbp;

	/* Flags to communicate with zskbd_softint() */
	volatile u_char zst_rx_flags,	/* receiver blocked */
#define	RX_TTY_BLOCKED		0x01
#define	RX_TTY_OVERFLOWED	0x02
#define	RX_IBUF_BLOCKED		0x04
#define	RX_IBUF_OVERFLOWED	0x08
#define	RX_ANY_BLOCK		0x0f
			zst_tx_busy,	/* working on an output chunk */
			zst_tx_done,	/* done with one output chunk */
			zst_tx_stopped,	/* H/W level stop (lost CTS) */
			zst_st_check,	/* got a status interrupt */
			zst_rx_ready;

	/* PPS signal on DCD, with or without inkernel clock disciplining */
	u_char  zst_ppsmask;			/* pps signal mask */
	u_char  zst_ppsassert;			/* pps leading edge */
	u_char  zst_ppsclear;			/* pps trailing edge */
};

/* Definition of the driver for autoconfig. */
static int	zskbd_match(struct device *, void *, void *);
static void	zskbd_attach(struct device *, struct device *, void *);

const struct cfattach zskbd_ca = {
	sizeof(struct zskbd_softc), zskbd_match, zskbd_attach
};

struct zsops zsops_kbd;

static void zs_modem(struct zskbd_softc *, int);
static void zs_hwiflow(struct zskbd_softc *);
static void zs_maskintr(struct zskbd_softc *);

/* Low-level routines. */
static void zskbd_rxint(struct zs_chanstate *);
static void zskbd_stint(struct zs_chanstate *, int);
static void zskbd_txint(struct zs_chanstate *);
static void zskbd_softint(struct zs_chanstate *);
static void zskbd_diag(void *);

int zskbd_init(struct zskbd_softc *);
void zskbd_putc(struct zskbd_softc *, u_int8_t);

/* wskbd glue */
void zskbd_cngetc(void *, u_int *, int *);
void zskbd_cnpollc(void *, int);

void zsstart_tx(struct zskbd_softc *);
int zsenqueue_tx(void *, u_int8_t *, u_int);

struct wskbd_consops zskbd_consops = {
	zskbd_cngetc,
	zskbd_cnpollc
};

#define	ZSKBDUNIT(x)	(minor(x) & 0x7ffff)

/*
 * zskbd_match: how is this zs channel configured?
 */
int 
zskbd_match(struct device *parent, void *vcf, void *aux)
{
	struct cfdata *cf = vcf;
	struct zsc_attach_args *args = aux;
	int ret;

	/* If we're not looking for a keyboard, just exit */
	if (strcmp(args->type, "keyboard") != 0)
		return (0);

	ret = 10;

	/* Exact match is better than wildcard. */
	if (cf->cf_loc[ZSCCF_CHANNEL] == args->channel)
		ret += 2;

	/* This driver accepts wildcard. */
	if (cf->cf_loc[ZSCCF_CHANNEL] == ZSCCF_CHANNEL_DEFAULT)
		ret += 1;

	return (ret);
}

void 
zskbd_attach(struct device *parent, struct device *self, void *aux)
{
	struct zsc_softc *zsc = (void *)parent;
	struct zskbd_softc *zst = (void *)self;
	struct sunkbd_softc *ss = (void *)self;
	struct cfdata *cf = self->dv_cfdata;
	struct zsc_attach_args *args = aux;
	struct wskbddev_attach_args a;
	struct zs_chanstate *cs;
	int channel, s, tty_unit, console = 0;
	dev_t dev;

	ss->sc_sendcmd = zsenqueue_tx;
	timeout_set(&ss->sc_bellto, sunkbd_bellstop, zst);

	timeout_set(&zst->zst_diag_ch, zskbd_diag, zst);

	zst->zst_tbp = zst->zst_tba = zst->zst_tbeg = zst->zst_tbuf;
	zst->zst_tend = zst->zst_tbeg + ZSKBD_RING_SIZE;

	tty_unit = ss->sc_dev.dv_unit;
	channel = args->channel;
	cs = zsc->zsc_cs[channel];
	cs->cs_private = zst;
	cs->cs_ops = &zsops_kbd;

	zst->zst_cs = cs;
	zst->zst_swflags = cf->cf_flags;	/* softcar, etc. */
	zst->zst_hwflags = args->hwflags;
	dev = makedev(zs_major, tty_unit);

	if (zst->zst_swflags)
		printf(" flags 0x%x", zst->zst_swflags);

	/*
	 * Check whether we serve as a console device.
	 * XXX - split console input/output channels aren't
	 *	 supported yet on /dev/console
	 */
	if ((zst->zst_hwflags & ZS_HWFLAG_CONSOLE_INPUT) != 0) {
		if ((args->hwflags & ZS_HWFLAG_USE_CONSDEV) != 0) {
			args->consdev->cn_dev = dev;
			cn_tab->cn_pollc = wskbd_cnpollc;
			cn_tab->cn_getc = wskbd_cngetc;
		}
		cn_tab->cn_dev = dev;
		console = 1;
	}

	zst->zst_rbuf = malloc(zskbd_rbuf_size << 1, M_DEVBUF, M_WAITOK);
	zst->zst_ebuf = zst->zst_rbuf + (zskbd_rbuf_size << 1);
	/* Disable the high water mark. */
	zst->zst_r_hiwat = 0;
	zst->zst_r_lowat = 0;
	zst->zst_rbget = zst->zst_rbput = zst->zst_rbuf;
	zst->zst_rbavail = zskbd_rbuf_size;

	/* if there are no enable/disable functions, assume the device
	   is always enabled */
	if (!cs->enable)
		cs->enabled = 1;

	/*
	 * Hardware init
	 */
	if (ISSET(zst->zst_hwflags, ZS_HWFLAG_CONSOLE)) {
		/* Call zsparam similar to open. */

		/* Wait a while for previous console output to complete */
		DELAY(10000);
	} else if (!ISSET(zst->zst_hwflags, ZS_HWFLAG_NORESET)) {
		/* Not the console; may need reset. */
		int reset;

		reset = (channel == 0) ? ZSWR9_A_RESET : ZSWR9_B_RESET;
		s = splzs();
		zs_write_reg(cs, 9, reset);
		splx(s);
	}

	/*
	 * Probe for a keyboard.
	 * If one is found, turn on receiver and status interrupts.
	 * We defer the actual write of the register to zsparam(),
	 * but we must make sure status interrupts are turned on by
	 * the time zsparam() reads the initial rr0 state.
	 */
	if (zskbd_init(zst)) {
		SET(cs->cs_preg[1], ZSWR1_RIE | ZSWR1_SIE);
		zs_write_reg(cs, 1, cs->cs_creg[1]);

		/* Make sure DTR is on now. */
		s = splzs();
		zs_modem(zst, 1);
		splx(s);
	} else {
		/* Will raise DTR in open. */
		s = splzs();
		zs_modem(zst, 0);
		splx(s);

		return;
	}

	ss->sc_click =
	    strcmp(getpropstring(optionsnode, "keyboard-click?"), "true") == 0;
	sunkbd_setclick(ss, ss->sc_click);

	a.console = console;
	if (ISTYPE5(ss->sc_layout)) {
		a.keymap = &sunkbd5_keymapdata;
#ifndef	SUNKBD5_LAYOUT
		if (ss->sc_layout < MAXSUNLAYOUT &&
		    sunkbd_layouts[ss->sc_layout] != -1)
			sunkbd5_keymapdata.layout =
			    sunkbd_layouts[ss->sc_layout];
#endif
	} else {
		a.keymap = &sunkbd_keymapdata;
#ifndef	SUNKBD_LAYOUT
		if (ss->sc_layout < MAXSUNLAYOUT &&
		    sunkbd_layouts[ss->sc_layout] != -1)
			sunkbd_keymapdata.layout =
			    sunkbd_layouts[ss->sc_layout];
#endif
	}
	a.accessops = &sunkbd_accessops;
	a.accesscookie = zst;

	if (console)
		wskbd_cnattach(&zskbd_consops, zst, a.keymap);

	sunkbd_attach(ss, &a);
}

int
zskbd_init(struct zskbd_softc *zst)
{
	struct sunkbd_softc *ss = (void *)zst;
	struct zs_chanstate *cs = zst->zst_cs;
	int s, tries;
	u_int8_t v3, v4, v5, rr0;

	/* setup for 1200n81 */
	if (zs_set_speed(cs, 1200)) {			/* set 1200bps */
		printf(": failed to set baudrate\n");
		return 0;
	}
	if (zs_set_modes(cs, CS8 | CLOCAL)) {
		printf(": failed to set modes\n");
		return 0;
	}

	s = splzs();

	zs_maskintr(zst);

	v3 = cs->cs_preg[3];				/* set 8 bit chars */
	v5 = cs->cs_preg[5];
	CLR(v3, ZSWR3_RXSIZE);
	CLR(v5, ZSWR5_TXSIZE);
	SET(v3, ZSWR3_RX_8);
	SET(v5, ZSWR5_TX_8);
	cs->cs_preg[3] = v3;
	cs->cs_preg[5] = v5;

	v4 = cs->cs_preg[4];				/* no parity 1 stop */
	CLR(v4, ZSWR4_SBMASK | ZSWR4_PARMASK);
	SET(v4, ZSWR4_ONESB | ZSWR4_EVENP);
	cs->cs_preg[4] = v4;

	if (!cs->cs_heldchange) {
		if (zst->zst_tx_busy) {
			zst->zst_heldtbc = zst->zst_tbc;
			zst->zst_tbc = 0;
			cs->cs_heldchange = 1;
		} else
			zs_loadchannelregs(cs);
	}

	/*
	 * Hardware flow control is disabled, turn off the buffer water
	 * marks and unblock any soft flow control state.  Otherwise, enable
	 * the water marks.
	 */
	zst->zst_r_hiwat = 0;
	zst->zst_r_lowat = 0;
	if (ISSET(zst->zst_rx_flags, RX_TTY_OVERFLOWED)) {
		CLR(zst->zst_rx_flags, RX_TTY_OVERFLOWED);
		zst->zst_rx_ready = 1;
		cs->cs_softreq = 1;
	}
	if (ISSET(zst->zst_rx_flags, RX_TTY_BLOCKED|RX_IBUF_BLOCKED)) {
		CLR(zst->zst_rx_flags, RX_TTY_BLOCKED|RX_IBUF_BLOCKED);
		zs_hwiflow(zst);
	}

	/*
	 * Force a recheck of the hardware carrier and flow control status,
	 * since we may have changed which bits we're looking at.
	 */
	zskbd_stint(cs, 1);

	splx(s);

	/*
	 * Hardware flow control is disabled, unblock any hard flow control
	 * state.
	 */
	if (zst->zst_tx_stopped) {
		zst->zst_tx_stopped = 0;
		zsstart_tx(zst);
	}

	zskbd_softint(cs);

	/* Ok, start the reset sequence... */

	s = splhigh();

	for (tries = 5; tries != 0; tries--) {
		int ltries;

		ss->sc_leds = 0;
		ss->sc_layout = -1;

		/* Send reset request */
		zskbd_putc(zst, SKBD_CMD_RESET);

		ltries = 1000;
		while (--ltries > 0) {
			rr0 = *cs->cs_reg_csr;
			if (rr0 & ZSRR0_RX_READY) {
				sunkbd_raw(ss, *cs->cs_reg_data);
				if (ss->sc_kbdstate == SKBD_STATE_RESET)
					break;
			}
			DELAY(1000);
		}
		if (ltries == 0)
			continue;

		/* Wait for reset to finish. */
		ltries = 1000;
		while (--ltries > 0) {
			rr0 = *cs->cs_reg_csr;
			if (rr0 & ZSRR0_RX_READY) {
				sunkbd_raw(ss, *cs->cs_reg_data);
				if (ss->sc_kbdstate == SKBD_STATE_GETKEY)
					break;
			}
			DELAY(1000);
		}
		if (ltries == 0)
			continue;

		/* Some Sun<=>PS/2 converters need some delay here */
		DELAY(5000);

		/* Send layout request */
		zskbd_putc(zst, SKBD_CMD_LAYOUT);

		ltries = 1000;
		while (--ltries > 0) {
			rr0 = *cs->cs_reg_csr;
			if (rr0 & ZSRR0_RX_READY) {
				sunkbd_raw(ss, *cs->cs_reg_data);
				if (ss->sc_layout != -1)
					break;
			}
			DELAY(1000);
		}
		if (ltries == 0)
			continue;
		break;
	}
	if (tries == 0)
		printf(": no keyboard\n");
	else
		printf(": layout %d\n", ss->sc_layout);
	splx(s);

	return tries;
}

void
zskbd_putc(struct zskbd_softc *zst, u_int8_t c)
{
	u_int8_t rr0;
	int s;

	s = splhigh();
	do {
		rr0 = *zst->zst_cs->cs_reg_csr;
	} while ((rr0 & ZSRR0_TX_READY) == 0);
	*zst->zst_cs->cs_reg_data = c;
	delay(2);
	splx(s);
}

int
zsenqueue_tx(void *v, u_int8_t *str, u_int len)
{
	struct zskbd_softc *zst = v;
	int s;
	u_int i;

	s = splzs();
	if (zst->zst_tbc + len > ZSKBD_RING_SIZE) {
		splx(s);
		return (-1);
	}
	zst->zst_tbc += len;
	for (i = 0; i < len; i++) {
		*zst->zst_tbp = str[i];
		if (++zst->zst_tbp == zst->zst_tend)
			zst->zst_tbp = zst->zst_tbeg;
	}
	splx(s);
	zsstart_tx(zst);
	return (0);
}

void
zsstart_tx(struct zskbd_softc *zst)
{
	struct zs_chanstate *cs = zst->zst_cs;
	int s, s1;

	s = spltty();

	if (zst->zst_tx_stopped)
		goto out;
	if (zst->zst_tbc == 0)
		goto out;

	s1 = splzs();

	zst->zst_tx_busy = 1;

	if (!ISSET(cs->cs_preg[1], ZSWR1_TIE)) {
		SET(cs->cs_preg[1], ZSWR1_TIE);
		cs->cs_creg[1] = cs->cs_preg[1];
		zs_write_reg(cs, 1, cs->cs_creg[1]);
	}

	zs_write_data(cs, *zst->zst_tba);

	zst->zst_tbc--;
	if (++zst->zst_tba == zst->zst_tend)
		zst->zst_tba = zst->zst_tbeg;

	splx(s1);

out:
	splx(s);
}

/*
 * Compute interrupt enable bits and set in the pending bits. Called both
 * in zsparam() and when PPS (pulse per second timing) state changes.
 * Must be called at splzs().
 */
static void
zs_maskintr(struct zskbd_softc *zst)
{
	struct zs_chanstate *cs = zst->zst_cs;
	int tmp15;

	cs->cs_rr0_mask = cs->cs_rr0_cts | cs->cs_rr0_dcd;
	if (zst->zst_ppsmask != 0)
		cs->cs_rr0_mask |= cs->cs_rr0_pps;
	tmp15 = cs->cs_preg[15];
	if (ISSET(cs->cs_rr0_mask, ZSRR0_DCD))
		SET(tmp15, ZSWR15_DCD_IE);
	else
		CLR(tmp15, ZSWR15_DCD_IE);
	if (ISSET(cs->cs_rr0_mask, ZSRR0_CTS))
		SET(tmp15, ZSWR15_CTS_IE);
	else
		CLR(tmp15, ZSWR15_CTS_IE);
	cs->cs_preg[15] = tmp15;
}


/*
 * Raise or lower modem control (DTR/RTS) signals.  If a character is
 * in transmission, the change is deferred.
 */
static void
zs_modem(struct zskbd_softc *zst, int onoff)
{
	struct zs_chanstate *cs = zst->zst_cs;

	if (cs->cs_wr5_dtr == 0)
		return;

	if (onoff)
		SET(cs->cs_preg[5], cs->cs_wr5_dtr);
	else
		CLR(cs->cs_preg[5], cs->cs_wr5_dtr);

	if (!cs->cs_heldchange) {
		if (zst->zst_tx_busy) {
			zst->zst_heldtbc = zst->zst_tbc;
			zst->zst_tbc = 0;
			cs->cs_heldchange = 1;
		} else
			zs_loadchannelregs(cs);
	}
}

/*
 * Internal version of zshwiflow
 * called at splzs
 */
static void
zs_hwiflow(struct zskbd_softc *zst)
{
	struct zs_chanstate *cs = zst->zst_cs;

	if (cs->cs_wr5_rts == 0)
		return;

	if (ISSET(zst->zst_rx_flags, RX_ANY_BLOCK)) {
		CLR(cs->cs_preg[5], cs->cs_wr5_rts);
		CLR(cs->cs_creg[5], cs->cs_wr5_rts);
	} else {
		SET(cs->cs_preg[5], cs->cs_wr5_rts);
		SET(cs->cs_creg[5], cs->cs_wr5_rts);
	}
	zs_write_reg(cs, 5, cs->cs_creg[5]);
}


/****************************************************************
 * Interface to the lower layer (zscc)
 ****************************************************************/

#define	integrate
integrate void zskbd_rxsoft(struct zskbd_softc *);
integrate void zskbd_txsoft(struct zskbd_softc *);
integrate void zskbd_stsoft(struct zskbd_softc *);
/*
 * receiver ready interrupt.
 * called at splzs
 */
static void
zskbd_rxint(struct zs_chanstate *cs)
{
	struct zskbd_softc *zst = cs->cs_private;
	u_char *put, *end;
	u_int cc;
	u_char rr0, rr1, c;

	end = zst->zst_ebuf;
	put = zst->zst_rbput;
	cc = zst->zst_rbavail;

	while (cc > 0) {
		/*
		 * First read the status, because reading the received char
		 * destroys the status of this char.
		 */
		rr1 = zs_read_reg(cs, 1);
		c = zs_read_data(cs);

		if (ISSET(rr1, ZSRR1_FE | ZSRR1_DO | ZSRR1_PE)) {
			/* Clear the receive error. */
			zs_write_csr(cs, ZSWR0_RESET_ERRORS);
		}

		put[0] = c;
		put[1] = rr1;
		put += 2;
		if (put >= end)
			put = zst->zst_rbuf;
		cc--;

		rr0 = zs_read_csr(cs);
		if (!ISSET(rr0, ZSRR0_RX_READY))
			break;
	}

	/*
	 * Current string of incoming characters ended because
	 * no more data was available or we ran out of space.
	 * Schedule a receive event if any data was received.
	 * If we're out of space, turn off receive interrupts.
	 */
	zst->zst_rbput = put;
	zst->zst_rbavail = cc;
	if (!ISSET(zst->zst_rx_flags, RX_TTY_OVERFLOWED)) {
		zst->zst_rx_ready = 1;
		cs->cs_softreq = 1;
	}

	/*
	 * See if we are in danger of overflowing a buffer. If
	 * so, use hardware flow control to ease the pressure.
	 */
	if (!ISSET(zst->zst_rx_flags, RX_IBUF_BLOCKED) &&
	    cc < zst->zst_r_hiwat) {
		SET(zst->zst_rx_flags, RX_IBUF_BLOCKED);
		zs_hwiflow(zst);
	}

	/*
	 * If we're out of space, disable receive interrupts
	 * until the queue has drained a bit.
	 */
	if (!cc) {
		SET(zst->zst_rx_flags, RX_IBUF_OVERFLOWED);
		CLR(cs->cs_preg[1], ZSWR1_RIE);
		cs->cs_creg[1] = cs->cs_preg[1];
		zs_write_reg(cs, 1, cs->cs_creg[1]);
	}
}

/*
 * transmitter ready interrupt.  (splzs)
 */
static void
zskbd_txint(struct zs_chanstate *cs)
{
	struct zskbd_softc *zst = cs->cs_private;

	/*
	 * If we've delayed a parameter change, do it now, and restart
	 * output.
	 */
	if (cs->cs_heldchange) {
		zs_loadchannelregs(cs);
		cs->cs_heldchange = 0;
		zst->zst_tbc = zst->zst_heldtbc;
		zst->zst_heldtbc = 0;
	}

	/* Output the next character in the buffer, if any. */
	if (zst->zst_tbc > 0) {
		zs_write_data(cs, *zst->zst_tba);
		zst->zst_tbc--;
		if (++zst->zst_tba == zst->zst_tend)
			zst->zst_tba = zst->zst_tbeg;
	} else {
		/* Disable transmit completion interrupts if necessary. */
		if (ISSET(cs->cs_preg[1], ZSWR1_TIE)) {
			CLR(cs->cs_preg[1], ZSWR1_TIE);
			cs->cs_creg[1] = cs->cs_preg[1];
			zs_write_reg(cs, 1, cs->cs_creg[1]);
		}
		if (zst->zst_tx_busy) {
			zst->zst_tx_busy = 0;
			zst->zst_tx_done = 1;
			cs->cs_softreq = 1;
		}
	}
}

/*
 * status change interrupt.  (splzs)
 */
static void
zskbd_stint(struct zs_chanstate *cs, int force)
{
	struct zskbd_softc *zst = cs->cs_private;
	u_char rr0, delta;

	rr0 = zs_read_csr(cs);
	zs_write_csr(cs, ZSWR0_RESET_STATUS);

	/*
	 * Check here for console break, so that we can abort
	 * even when interrupts are locking up the machine.
	 */
	if (!force)
		delta = rr0 ^ cs->cs_rr0;
	else
		delta = cs->cs_rr0_mask;
	cs->cs_rr0 = rr0;

	if (ISSET(delta, cs->cs_rr0_mask)) {
		SET(cs->cs_rr0_delta, delta);

		/*
		 * Stop output immediately if we lose the output
		 * flow control signal or carrier detect.
		 */
		if (ISSET(~rr0, cs->cs_rr0_mask)) {
			zst->zst_tbc = 0;
			zst->zst_heldtbc = 0;
		}

		zst->zst_st_check = 1;
		cs->cs_softreq = 1;
	}
}

void
zskbd_diag(void *arg)
{
	struct zskbd_softc *zst = arg;
	struct sunkbd_softc *ss = arg;
	int overflows, floods;
	int s;

	s = splzs();
	overflows = zst->zst_overflows;
	zst->zst_overflows = 0;
	floods = zst->zst_floods;
	zst->zst_floods = 0;
	zst->zst_errors = 0;
	splx(s);

	log(LOG_WARNING, "%s: %d silo overflow%s, %d ibuf flood%s\n",
	    ss->sc_dev.dv_xname,
	    overflows, overflows == 1 ? "" : "s",
	    floods, floods == 1 ? "" : "s");
}

integrate void
zskbd_rxsoft(struct zskbd_softc *zst)
{
	struct sunkbd_softc *ss = (void *)zst;
	struct zs_chanstate *cs = zst->zst_cs;
	u_char *get, *end;
	u_int cc, scc;
	u_char rr1;
	int code;
	int s;
	u_int8_t cbuf[SUNKBD_MAX_INPUT_SIZE], *c;

	end = zst->zst_ebuf;
	get = zst->zst_rbget;
	scc = cc = zskbd_rbuf_size - zst->zst_rbavail;

	if (cc == zskbd_rbuf_size) {
		zst->zst_floods++;
		if (zst->zst_errors++ == 0)
			timeout_add_sec(&zst->zst_diag_ch, 60);
	}

	c = cbuf;
	while (cc) {
		code = get[0];
		rr1 = get[1];
		if (ISSET(rr1, ZSRR1_DO | ZSRR1_FE | ZSRR1_PE)) {
			if (ISSET(rr1, ZSRR1_DO)) {
				zst->zst_overflows++;
				if (zst->zst_errors++ == 0)
					timeout_add_sec(&zst->zst_diag_ch, 60);
			}
			if (ISSET(rr1, ZSRR1_FE))
				SET(code, TTY_FE);
			if (ISSET(rr1, ZSRR1_PE))
				SET(code, TTY_PE);
		}

		*c++ = code;
		if (c - cbuf == sizeof cbuf) {
			sunkbd_input(ss, cbuf, c - cbuf);
			c = cbuf;
		}

		get += 2;
		if (get >= end)
			get = zst->zst_rbuf;
		cc--;
	}
	if (c != cbuf)
		sunkbd_input(ss, cbuf, c - cbuf);

	if (cc != scc) {
		zst->zst_rbget = get;
		s = splzs();
		cc = zst->zst_rbavail += scc - cc;
		/* Buffers should be ok again, release possible block. */
		if (cc >= zst->zst_r_lowat) {
			if (ISSET(zst->zst_rx_flags, RX_IBUF_OVERFLOWED)) {
				CLR(zst->zst_rx_flags, RX_IBUF_OVERFLOWED);
				SET(cs->cs_preg[1], ZSWR1_RIE);
				cs->cs_creg[1] = cs->cs_preg[1];
				zs_write_reg(cs, 1, cs->cs_creg[1]);
			}
			if (ISSET(zst->zst_rx_flags, RX_IBUF_BLOCKED)) {
				CLR(zst->zst_rx_flags, RX_IBUF_BLOCKED);
				zs_hwiflow(zst);
			}
		}
		splx(s);
	}
}

integrate void
zskbd_txsoft(struct zskbd_softc *zst)
{
}

integrate void
zskbd_stsoft(struct zskbd_softc *zst)
{
	struct zs_chanstate *cs = zst->zst_cs;
	u_char rr0, delta;
	int s;

	s = splzs();
	rr0 = cs->cs_rr0;
	delta = cs->cs_rr0_delta;
	cs->cs_rr0_delta = 0;
	splx(s);

	if (ISSET(delta, cs->cs_rr0_cts)) {
		/* Block or unblock output according to flow control. */
		if (ISSET(rr0, cs->cs_rr0_cts))
			zst->zst_tx_stopped = 0;
		else
			zst->zst_tx_stopped = 1;
	}
}

/*
 * Software interrupt.  Called at zssoft
 *
 * The main job to be done here is to empty the input ring
 * by passing its contents up to the tty layer.  The ring is
 * always emptied during this operation, therefore the ring
 * must not be larger than the space after "high water" in
 * the tty layer, or the tty layer might drop our input.
 *
 * Note: an "input blockage" condition is assumed to exist if
 * EITHER the TS_TBLOCK flag or zst_rx_blocked flag is set.
 */
static void
zskbd_softint(struct zs_chanstate *cs)
{
	struct zskbd_softc *zst = cs->cs_private;
	int s;

	s = spltty();

	if (zst->zst_rx_ready) {
		zst->zst_rx_ready = 0;
		zskbd_rxsoft(zst);
	}

	if (zst->zst_st_check) {
		zst->zst_st_check = 0;
		zskbd_stsoft(zst);
	}

	if (zst->zst_tx_done) {
		zst->zst_tx_done = 0;
		zskbd_txsoft(zst);
	}

	splx(s);
}

struct zsops zsops_kbd = {
	zskbd_rxint,	/* receive char available */
	zskbd_stint,	/* external/status */
	zskbd_txint,	/* xmit buffer empty */
	zskbd_softint,	/* process software interrupt */
};

void
zskbd_cnpollc(void *v, int on)
{
}

void
zskbd_cngetc(void *v, u_int *type, int *data)
{
	struct zskbd_softc *zst = v;
	int s;
	u_int8_t c, rr0;

	s = splhigh();
	do {
		rr0 = *zst->zst_cs->cs_reg_csr;
	} while ((rr0 & ZSRR0_RX_READY) == 0);

	c = *zst->zst_cs->cs_reg_data;
	splx(s);

	sunkbd_decode(c, type, data);
}
