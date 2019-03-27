/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 1995 by Pavel Antonov, Moscow, Russia.
 * Copyright (C) 1995 by Andrey A. Chernov, Moscow, Russia.
 * All rights reserved.
 * Copyright (C) 2002 by John Baldwin <jhb@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND
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
 * $FreeBSD$
 */

/*
 * SDL Communications Riscom/8 (based on Cirrus Logic CL-CD180) driver
 *
 */

/*#define RCDEBUG*/

#include "opt_tty.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/interrupt.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/serial.h>
#include <sys/tty.h>
#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/ic/cd180.h>
#include <dev/rc/rcreg.h>
#include <isa/isavar.h>

#define	IOBASE_ADDRS	14

#define rcin(sc, port)		RC_IN(sc, port)
#define rcout(sc, port, v)	RC_OUT(sc, port, v)

#define WAITFORCCR(sc, chan)	rc_wait0((sc), (chan), __LINE__)

#define CCRCMD(sc, chan, cmd) do {					\
	WAITFORCCR((sc), (chan));					\
	rcout((sc), CD180_CCR, (cmd));					\
} while (0)

#define RC_IBUFSIZE     256
#define RB_I_HIGH_WATER (TTYHOG - 2 * RC_IBUFSIZE)
#define RC_OBUFSIZE     512
#define RC_IHIGHWATER   (3 * RC_IBUFSIZE / 4)
#define INPUT_FLAGS_SHIFT (2 * RC_IBUFSIZE)
#define LOTS_OF_EVENTS  64

#define RC_FAKEID       0x10

/* Per-channel structure */
struct rc_chans  {
	struct rc_softc *rc_rcb;                /* back ptr             */
	u_short          rc_flags;              /* Misc. flags          */
	int              rc_chan;               /* Channel #            */
	u_char           rc_ier;                /* intr. enable reg     */
	u_char           rc_msvr;               /* modem sig. status    */
	u_char           rc_cor2;               /* options reg          */
	u_char           rc_pendcmd;            /* special cmd pending  */
	u_int            rc_dcdwaits;           /* how many waits DCD in open */
	struct tty      *rc_tp;                 /* tty struct           */
	u_char          *rc_iptr;               /* Chars input buffer         */
	u_char          *rc_hiwat;              /* hi-water mark        */
	u_char          *rc_bufend;             /* end of buffer        */
	u_char          *rc_optr;               /* ptr in output buf    */
	u_char          *rc_obufend;            /* end of output buf    */
	u_char           rc_ibuf[4 * RC_IBUFSIZE];  /* input buffer         */
	u_char           rc_obuf[RC_OBUFSIZE];  /* output buffer        */
	struct callout	 rc_dtrcallout;
};

/* Per-board structure */
struct rc_softc {
	device_t	 sc_dev;
	struct resource *sc_irq;
	struct resource *sc_port[IOBASE_ADDRS];
	int		 sc_irqrid;
	void		*sc_hwicookie;
	bus_space_tag_t  sc_bt;
	bus_space_handle_t sc_bh;
	u_int            sc_unit;       /* unit #               */
	u_char           sc_dtr;        /* DTR status           */
	int		 sc_scheduled_event;
	void		*sc_swicookie;
	struct rc_chans  sc_channels[CD180_NCHAN]; /* channels */
};

/* Static prototypes */
static t_close_t rc_close;
static void rc_break(struct tty *, int);
static void rc_release_resources(device_t dev);
static void rc_intr(void *);
static void rc_hwreset(struct rc_softc *, unsigned int);
static int  rc_test(struct rc_softc *);
static void rc_discard_output(struct rc_chans *);
static int  rc_modem(struct tty *, int, int);
static void rc_start(struct tty *);
static void rc_stop(struct tty *, int rw);
static int  rc_param(struct tty *, struct termios *);
static void rc_pollcard(void *);
static void rc_reinit(struct rc_softc *);
#ifdef RCDEBUG
static void printrcflags();
#endif
static void rc_wait0(struct rc_softc *sc, int chan, int line);

static devclass_t rc_devclass;

/* Flags */
#define RC_DTR_OFF      0x0001          /* DTR wait, for close/open     */
#define RC_ACTOUT       0x0002          /* Dial-out port active         */
#define RC_RTSFLOW      0x0004          /* RTS flow ctl enabled         */
#define RC_CTSFLOW      0x0008          /* CTS flow ctl enabled         */
#define RC_DORXFER      0x0010          /* RXFER event planned          */
#define RC_DOXXFER      0x0020          /* XXFER event planned          */
#define RC_MODCHG       0x0040          /* Modem status changed         */
#define RC_OSUSP        0x0080          /* Output suspended             */
#define RC_OSBUSY       0x0100          /* start() routine in progress  */
#define RC_WAS_BUFOVFL  0x0200          /* low-level buffer ovferflow   */
#define RC_WAS_SILOVFL  0x0400          /* silo buffer overflow         */
#define RC_SEND_RDY     0x0800          /* ready to send */

/* Table for translation of RCSR status bits to internal form */
static int rc_rcsrt[16] = {
	0,             TTY_OE,               TTY_FE,
	TTY_FE|TTY_OE, TTY_PE,               TTY_PE|TTY_OE,
	TTY_PE|TTY_FE, TTY_PE|TTY_FE|TTY_OE, TTY_BI,
	TTY_BI|TTY_OE, TTY_BI|TTY_FE,        TTY_BI|TTY_FE|TTY_OE,
	TTY_BI|TTY_PE, TTY_BI|TTY_PE|TTY_OE, TTY_BI|TTY_PE|TTY_FE,
	TTY_BI|TTY_PE|TTY_FE|TTY_OE
};

static int rc_ports[] =
    { 0x220, 0x240, 0x250, 0x260, 0x2a0, 0x2b0, 0x300, 0x320 };
static int iobase_addrs[IOBASE_ADDRS] =
    { 0, 0x400, 0x800, 0xc00, 0x1400, 0x1800, 0x1c00, 0x2000,
      0x3000, 0x3400, 0x3800, 0x3c00, 0x4000, 0x8000 };

/**********************************************/

static int
rc_probe(device_t dev)
{
	u_int port;
	int i, found;

	/*
	 * We don't know of any PnP ID's for these cards.
	 */
	if (isa_get_logicalid(dev) != 0)
		return (ENXIO);

	/*
	 * We have to have an IO port hint that is valid.
	 */
	port = isa_get_port(dev);
	if (port == -1)
		return (ENXIO);
	found = 0;
	for (i = 0; i < nitems(rc_ports); i++)
		if (rc_ports[i] == port) {
			found = 1;
			break;
		}
	if (!found)
		return (ENXIO);

	/*
	 * We have to have an IRQ hint.
	 */
	if (isa_get_irq(dev) == -1)
		return (ENXIO);

	device_set_desc(dev, "SDL Riscom/8");
	return (0);
}

static int
rc_attach(device_t dev)
{
 	struct rc_chans *rc;
	struct tty *tp;
	struct rc_softc *sc;
	u_int port;
	int base, chan, error, i, x;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	/*
	 * We need to have IO ports.  Lots of them.  We need
	 * the following ranges relative to the base port:
	 * 0x0    -   0x10
	 * 0x400  -  0x410
	 * 0x800  -  0x810
	 * 0xc00  -  0xc10
	 * 0x1400 - 0x1410
	 * 0x1800 - 0x1810
	 * 0x1c00 - 0x1c10
	 * 0x2000 - 0x2010
	 * 0x3000 - 0x3010
	 * 0x3400 - 0x3410
	 * 0x3800 - 0x3810
	 * 0x3c00 - 0x3c10
	 * 0x4000 - 0x4010
	 * 0x8000 - 0x8010
	 */
	port = isa_get_port(dev);
	for (i = 0; i < IOBASE_ADDRS; i++)
		if (bus_set_resource(dev, SYS_RES_IOPORT, i,
		    port + iobase_addrs[i], 0x10) != 0)
			return (ENXIO);
	error = ENOMEM;
	for (i = 0; i < IOBASE_ADDRS; i++) {
		x = i;
		sc->sc_port[i] = bus_alloc_resource_anywhere(dev,
		    SYS_RES_IOPORT, &x, 0x10, RF_ACTIVE);
		if (x != i) {
			device_printf(dev, "ioport %d was rid %d\n", i, x);
			goto fail;
		}
		if (sc->sc_port[i] == NULL) {
			device_printf(dev, "failed to alloc ioports %x-%x\n",
			    port + iobase_addrs[i],
			    port + iobase_addrs[i] + 0x10);
			goto fail;
		}
	}
	sc->sc_bt = rman_get_bustag(sc->sc_port[0]);
	sc->sc_bh = rman_get_bushandle(sc->sc_port[0]);

	sc->sc_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->sc_irqrid,
	    RF_ACTIVE);
	if (sc->sc_irq == NULL) {
		device_printf(dev, "failed to alloc IRQ\n");
		goto fail;
	}

	/*
	 * Now do some actual tests to make sure it works.
	 */
	error = ENXIO;
	rcout(sc, CD180_PPRL, 0x22); /* Random values to Prescale reg. */
	rcout(sc, CD180_PPRH, 0x11);
	if (rcin(sc, CD180_PPRL) != 0x22 || rcin(sc, CD180_PPRH) != 0x11)
		goto fail;
	if (rc_test(sc))
		goto fail;

	/*
	 * Ok, start actually hooking things up.
	 */
	sc->sc_unit = device_get_unit(dev);
	/*sc->sc_chipid = 0x10 + device_get_unit(dev);*/
	device_printf(dev, "%d chans, firmware rev. %c\n",
		CD180_NCHAN, (rcin(sc, CD180_GFRCR) & 0xF) + 'A');
	rc = sc->sc_channels;
	base = CD180_NCHAN * sc->sc_unit;
	for (chan = 0; chan < CD180_NCHAN; chan++, rc++) {
		rc->rc_rcb     = sc;
		rc->rc_chan    = chan;
		rc->rc_iptr    = rc->rc_ibuf;
		rc->rc_bufend  = &rc->rc_ibuf[RC_IBUFSIZE];
		rc->rc_hiwat   = &rc->rc_ibuf[RC_IHIGHWATER];
		rc->rc_optr    = rc->rc_obufend  = rc->rc_obuf;
		callout_init(&rc->rc_dtrcallout, 0);
		tp = rc->rc_tp = ttyalloc();
		tp->t_sc = rc;
		tp->t_oproc   = rc_start;
		tp->t_param   = rc_param;
		tp->t_modem   = rc_modem;
		tp->t_break   = rc_break;
		tp->t_close   = rc_close;
		tp->t_stop    = rc_stop;
		ttycreate(tp, TS_CALLOUT, "m%d", chan + base);
	}

	error = bus_setup_intr(dev, sc->sc_irq, INTR_TYPE_TTY, NULL, rc_intr,
	    sc, &sc->sc_hwicookie);
	if (error) {
		device_printf(dev, "failed to register interrupt handler\n");
		goto fail;
	}
		
	swi_add(&tty_intr_event, "rc", rc_pollcard, sc, SWI_TTY, 0,
	    &sc->sc_swicookie);
	return (0);

fail:
	rc_release_resources(dev);
	return (error);
}

static int
rc_detach(device_t dev)
{
	struct rc_softc *sc;
	struct rc_chans *rc;
	int error, i;

	sc = device_get_softc(dev);

	rc = sc->sc_channels;
	for (i = 0; i < CD180_NCHAN; i++, rc++)
		ttyfree(rc->rc_tp);

	error = bus_teardown_intr(dev, sc->sc_irq, sc->sc_hwicookie);
	if (error)
		device_printf(dev, "failed to deregister interrupt handler\n");
	swi_remove(sc->sc_swicookie);
	rc_release_resources(dev);

	return (0);
}

static void
rc_release_resources(device_t dev)
{
	struct rc_softc *sc;
	int i;

	sc = device_get_softc(dev);
	if (sc->sc_irq != NULL) {
		bus_release_resource(dev, SYS_RES_IRQ, sc->sc_irqrid,
		    sc->sc_irq);
		sc->sc_irq = NULL;
	}
	for (i = 0; i < IOBASE_ADDRS; i++) {
		if (sc->sc_port[i] == NULL)
			break;
		bus_release_resource(dev, SYS_RES_IOPORT, i, sc->sc_port[i]);
		sc->sc_port[i] = NULL;
	}
}

/* RC interrupt handling */
static void
rc_intr(void *arg)
{
	struct rc_softc        *sc;
	struct rc_chans        *rc;
	int                    resid, chan;
	u_char                 val, iack, bsr, ucnt, *optr;
	int                    good_data, t_state;	

	sc = (struct rc_softc *)arg;
	bsr = ~(rcin(sc, RC_BSR));
	if (!(bsr & (RC_BSR_TOUT|RC_BSR_RXINT|RC_BSR_TXINT|RC_BSR_MOINT))) {
		device_printf(sc->sc_dev, "extra interrupt\n");
		rcout(sc, CD180_EOIR, 0);
		return;
	}

	while (bsr & (RC_BSR_TOUT|RC_BSR_RXINT|RC_BSR_TXINT|RC_BSR_MOINT)) {
#ifdef RCDEBUG_DETAILED
		device_printf(sc->sc_dev, "intr (%p) %s%s%s%s\n", arg, bsr,
			(bsr & RC_BSR_TOUT)?"TOUT ":"",
			(bsr & RC_BSR_RXINT)?"RXINT ":"",
			(bsr & RC_BSR_TXINT)?"TXINT ":"",
			(bsr & RC_BSR_MOINT)?"MOINT":"");
#endif
		if (bsr & RC_BSR_TOUT) {
			device_printf(sc->sc_dev,
			    "hardware failure, reset board\n");
			rcout(sc, RC_CTOUT, 0);
			rc_reinit(sc);
			return;
		}
		if (bsr & RC_BSR_RXINT) {
			iack = rcin(sc, RC_PILR_RX);
			good_data = (iack == (GIVR_IT_RGDI | RC_FAKEID));
			if (!good_data && iack != (GIVR_IT_REI | RC_FAKEID)) {
				device_printf(sc->sc_dev,
				    "fake rxint: %02x\n", iack);
				goto more_intrs;
			}
			chan = ((rcin(sc, CD180_GICR) & GICR_CHAN) >> GICR_LSH);
			rc = &sc->sc_channels[chan];
			t_state = rc->rc_tp->t_state;
			/* Do RTS flow control stuff */
			if (  (rc->rc_flags & RC_RTSFLOW)
			    || !(t_state & TS_ISOPEN)
			   ) {
				if (  (   !(t_state & TS_ISOPEN)
				       || (t_state & TS_TBLOCK)
				      )
				    && (rc->rc_msvr & MSVR_RTS)
				   )
					rcout(sc, CD180_MSVR,
						rc->rc_msvr &= ~MSVR_RTS);
				else if (!(rc->rc_msvr & MSVR_RTS))
					rcout(sc, CD180_MSVR,
						rc->rc_msvr |= MSVR_RTS);
			}
			ucnt  = rcin(sc, CD180_RDCR) & 0xF;
			resid = 0;

			if (t_state & TS_ISOPEN) {
				/* check for input buffer overflow */
				if ((rc->rc_iptr + ucnt) >= rc->rc_bufend) {
					resid  = ucnt;
					ucnt   = rc->rc_bufend - rc->rc_iptr;
					resid -= ucnt;
					if (!(rc->rc_flags & RC_WAS_BUFOVFL)) {
						rc->rc_flags |= RC_WAS_BUFOVFL;
						sc->sc_scheduled_event++;
					}
				}
				optr = rc->rc_iptr;
				/* check foor good data */
				if (good_data) {
					while (ucnt-- > 0) {
						val = rcin(sc, CD180_RDR);
						optr[0] = val;
						optr[INPUT_FLAGS_SHIFT] = 0;
						optr++;
						sc->sc_scheduled_event++;
						if (val != 0 && val == rc->rc_tp->t_hotchar)
							swi_sched(sc->sc_swicookie, 0);
					}
				} else {
					/* Store also status data */
					while (ucnt-- > 0) {
						iack = rcin(sc, CD180_RCSR);
						if (iack & RCSR_Timeout)
							break;
						if (   (iack & RCSR_OE)
						    && !(rc->rc_flags & RC_WAS_SILOVFL)) {
							rc->rc_flags |= RC_WAS_SILOVFL;
							sc->sc_scheduled_event++;
						}
						val = rcin(sc, CD180_RDR);
						/*
						  Don't store PE if IGNPAR and BREAK if IGNBRK,
						  this hack allows "raw" tty optimization
						  works even if IGN* is set.
						*/
						if (   !(iack & (RCSR_PE|RCSR_FE|RCSR_Break))
						    || ((!(iack & (RCSR_PE|RCSR_FE))
						    ||  !(rc->rc_tp->t_iflag & IGNPAR))
						    && (!(iack & RCSR_Break)
						    ||  !(rc->rc_tp->t_iflag & IGNBRK)))) {
							if (   (iack & (RCSR_PE|RCSR_FE))
							    && (t_state & TS_CAN_BYPASS_L_RINT)
							    && ((iack & RCSR_FE)
							    ||  ((iack & RCSR_PE)
							    &&  (rc->rc_tp->t_iflag & INPCK))))
								val = 0;
							else if (val != 0 && val == rc->rc_tp->t_hotchar)
								swi_sched(sc->sc_swicookie, 0);
							optr[0] = val;
							optr[INPUT_FLAGS_SHIFT] = iack;
							optr++;
							sc->sc_scheduled_event++;
						}
					}
				}
				rc->rc_iptr = optr;
				rc->rc_flags |= RC_DORXFER;
			} else
				resid = ucnt;
			/* Clear FIFO if necessary */
			while (resid-- > 0) {
				if (!good_data)
					iack = rcin(sc, CD180_RCSR);
				else
					iack = 0;
				if (iack & RCSR_Timeout)
					break;
				(void) rcin(sc, CD180_RDR);
			}
			goto more_intrs;
		}
		if (bsr & RC_BSR_MOINT) {
			iack = rcin(sc, RC_PILR_MODEM);
			if (iack != (GIVR_IT_MSCI | RC_FAKEID)) {
				device_printf(sc->sc_dev, "fake moint: %02x\n",
				    iack);
				goto more_intrs;
			}
			chan = ((rcin(sc, CD180_GICR) & GICR_CHAN) >> GICR_LSH);
			rc = &sc->sc_channels[chan];
			iack = rcin(sc, CD180_MCR);
			rc->rc_msvr = rcin(sc, CD180_MSVR);
			rcout(sc, CD180_MCR, 0);
#ifdef RCDEBUG
			printrcflags(rc, "moint");
#endif
			if (rc->rc_flags & RC_CTSFLOW) {
				if (rc->rc_msvr & MSVR_CTS)
					rc->rc_flags |= RC_SEND_RDY;
				else
					rc->rc_flags &= ~RC_SEND_RDY;
			} else
				rc->rc_flags |= RC_SEND_RDY;
			if ((iack & MCR_CDchg) && !(rc->rc_flags & RC_MODCHG)) {
				sc->sc_scheduled_event += LOTS_OF_EVENTS;
				rc->rc_flags |= RC_MODCHG;
				swi_sched(sc->sc_swicookie, 0);
			}
			goto more_intrs;
		}
		if (bsr & RC_BSR_TXINT) {
			iack = rcin(sc, RC_PILR_TX);
			if (iack != (GIVR_IT_TDI | RC_FAKEID)) {
				device_printf(sc->sc_dev, "fake txint: %02x\n",
				    iack);
				goto more_intrs;
			}
			chan = ((rcin(sc, CD180_GICR) & GICR_CHAN) >> GICR_LSH);
			rc = &sc->sc_channels[chan];
			if (    (rc->rc_flags & RC_OSUSP)
			    || !(rc->rc_flags & RC_SEND_RDY)
			   )
				goto more_intrs;
			/* Handle breaks and other stuff */
			if (rc->rc_pendcmd) {
				rcout(sc, CD180_COR2, rc->rc_cor2 |= COR2_ETC);
				rcout(sc, CD180_TDR,  CD180_C_ESC);
				rcout(sc, CD180_TDR,  rc->rc_pendcmd);
				rcout(sc, CD180_COR2, rc->rc_cor2 &= ~COR2_ETC);
				rc->rc_pendcmd = 0;
				goto more_intrs;
			}
			optr = rc->rc_optr;
			resid = rc->rc_obufend - optr;
			if (resid > CD180_NFIFO)
				resid = CD180_NFIFO;
			while (resid-- > 0)
				rcout(sc, CD180_TDR, *optr++);
			rc->rc_optr = optr;

			/* output completed? */
			if (optr >= rc->rc_obufend) {
				rcout(sc, CD180_IER, rc->rc_ier &= ~IER_TxRdy);
#ifdef RCDEBUG
				device_printf(sc->sc_dev,
				    "channel %d: output completed\n",
				    rc->rc_chan);
#endif
				if (!(rc->rc_flags & RC_DOXXFER)) {
					sc->sc_scheduled_event += LOTS_OF_EVENTS;
					rc->rc_flags |= RC_DOXXFER;
					swi_sched(sc->sc_swicookie, 0);
				}
			}
		}
	more_intrs:
		rcout(sc, CD180_EOIR, 0);   /* end of interrupt */
		rcout(sc, RC_CTOUT, 0);
		bsr = ~(rcin(sc, RC_BSR));
	}
}

/* Feed characters to output buffer */
static void
rc_start(struct tty *tp)
{
	struct rc_softc *sc;
	struct rc_chans *rc;
	int s;

	rc = tp->t_sc;
	if (rc->rc_flags & RC_OSBUSY)
		return;
	sc = rc->rc_rcb;
	s = spltty();
	rc->rc_flags |= RC_OSBUSY;
	critical_enter();
	if (tp->t_state & TS_TTSTOP)
		rc->rc_flags |= RC_OSUSP;
	else
		rc->rc_flags &= ~RC_OSUSP;
	/* Do RTS flow control stuff */
	if (   (rc->rc_flags & RC_RTSFLOW)
	    && (tp->t_state & TS_TBLOCK)
	    && (rc->rc_msvr & MSVR_RTS)
	   ) {
		rcout(sc, CD180_CAR, rc->rc_chan);
		rcout(sc, CD180_MSVR, rc->rc_msvr &= ~MSVR_RTS);
	} else if (!(rc->rc_msvr & MSVR_RTS)) {
		rcout(sc, CD180_CAR, rc->rc_chan);
		rcout(sc, CD180_MSVR, rc->rc_msvr |= MSVR_RTS);
	}
	critical_exit();
	if (tp->t_state & (TS_TIMEOUT|TS_TTSTOP))
		goto out;
#ifdef RCDEBUG
	printrcflags(rc, "rcstart");
#endif
	ttwwakeup(tp);
#ifdef RCDEBUG
	printf("rcstart: outq = %d obuf = %d\n",
		tp->t_outq.c_cc, rc->rc_obufend - rc->rc_optr);
#endif
	if (tp->t_state & TS_BUSY)
		goto out;    /* output still in progress ... */

	if (tp->t_outq.c_cc > 0) {
		u_int   ocnt;

		tp->t_state |= TS_BUSY;
		ocnt = q_to_b(&tp->t_outq, rc->rc_obuf, sizeof rc->rc_obuf);
		critical_enter();
		rc->rc_optr = rc->rc_obuf;
		rc->rc_obufend = rc->rc_optr + ocnt;
		critical_exit();
		if (!(rc->rc_ier & IER_TxRdy)) {
#ifdef RCDEBUG
			device_printf(sc->sc_dev,
			    "channel %d: rcstart enable txint\n", rc->rc_chan);
#endif
			rcout(sc, CD180_CAR, rc->rc_chan);
			rcout(sc, CD180_IER, rc->rc_ier |= IER_TxRdy);
		}
	}
out:
	rc->rc_flags &= ~RC_OSBUSY;
	(void) splx(s);
}

/* Handle delayed events. */
void
rc_pollcard(void *arg)
{
	struct rc_softc *sc;
	struct rc_chans *rc;
	struct tty *tp;
	u_char *tptr, *eptr;
	int chan, icnt;

	sc = (struct rc_softc *)arg;
	if (sc->sc_scheduled_event == 0)
		return;
	do {
		rc = sc->sc_channels;
		for (chan = 0; chan < CD180_NCHAN; rc++, chan++) {
			tp = rc->rc_tp;
#ifdef RCDEBUG
			if (rc->rc_flags & (RC_DORXFER|RC_DOXXFER|RC_MODCHG|
			    RC_WAS_BUFOVFL|RC_WAS_SILOVFL))
				printrcflags(rc, "rcevent");
#endif
			if (rc->rc_flags & RC_WAS_BUFOVFL) {
				critical_enter();
				rc->rc_flags &= ~RC_WAS_BUFOVFL;
				sc->sc_scheduled_event--;
				critical_exit();
				device_printf(sc->sc_dev,
			    "channel %d: interrupt-level buffer overflow\n",
				     chan);
			}
			if (rc->rc_flags & RC_WAS_SILOVFL) {
				critical_enter();
				rc->rc_flags &= ~RC_WAS_SILOVFL;
				sc->sc_scheduled_event--;
				critical_exit();
				device_printf(sc->sc_dev,
				    "channel %d: silo overflow\n", chan);
			}
			if (rc->rc_flags & RC_MODCHG) {
				critical_enter();
				rc->rc_flags &= ~RC_MODCHG;
				sc->sc_scheduled_event -= LOTS_OF_EVENTS;
				critical_exit();
				ttyld_modem(tp, !!(rc->rc_msvr & MSVR_CD));
			}
			if (rc->rc_flags & RC_DORXFER) {
				critical_enter();
				rc->rc_flags &= ~RC_DORXFER;
				eptr = rc->rc_iptr;
				if (rc->rc_bufend == &rc->rc_ibuf[2 * RC_IBUFSIZE])
					tptr = &rc->rc_ibuf[RC_IBUFSIZE];
				else
					tptr = rc->rc_ibuf;
				icnt = eptr - tptr;
				if (icnt > 0) {
					if (rc->rc_bufend == &rc->rc_ibuf[2 * RC_IBUFSIZE]) {
						rc->rc_iptr   = rc->rc_ibuf;
						rc->rc_bufend = &rc->rc_ibuf[RC_IBUFSIZE];
						rc->rc_hiwat  = &rc->rc_ibuf[RC_IHIGHWATER];
					} else {
						rc->rc_iptr   = &rc->rc_ibuf[RC_IBUFSIZE];
						rc->rc_bufend = &rc->rc_ibuf[2 * RC_IBUFSIZE];
						rc->rc_hiwat  =
							&rc->rc_ibuf[RC_IBUFSIZE + RC_IHIGHWATER];
					}
					if (   (rc->rc_flags & RC_RTSFLOW)
					    && (tp->t_state & TS_ISOPEN)
					    && !(tp->t_state & TS_TBLOCK)
					    && !(rc->rc_msvr & MSVR_RTS)
					    ) {
						rcout(sc, CD180_CAR, chan);
						rcout(sc, CD180_MSVR,
							rc->rc_msvr |= MSVR_RTS);
					}
					sc->sc_scheduled_event -= icnt;
				}
				critical_exit();

				if (icnt <= 0 || !(tp->t_state & TS_ISOPEN))
					goto done1;

				if (   (tp->t_state & TS_CAN_BYPASS_L_RINT)
				    && !(tp->t_state & TS_LOCAL)) {
					if ((tp->t_rawq.c_cc + icnt) >= RB_I_HIGH_WATER
					    && ((rc->rc_flags & RC_RTSFLOW) || (tp->t_iflag & IXOFF))
					    && !(tp->t_state & TS_TBLOCK))
						ttyblock(tp);
					tk_nin += icnt;
					tk_rawcc += icnt;
					tp->t_rawcc += icnt;
					if (b_to_q(tptr, icnt, &tp->t_rawq))
						device_printf(sc->sc_dev,
				    "channel %d: tty-level buffer overflow\n",
						    chan);
					ttwakeup(tp);
					if ((tp->t_state & TS_TTSTOP) && ((tp->t_iflag & IXANY)
					    || (tp->t_cc[VSTART] == tp->t_cc[VSTOP]))) {
						tp->t_state &= ~TS_TTSTOP;
						tp->t_lflag &= ~FLUSHO;
						rc_start(tp);
					}
				} else {
					for (; tptr < eptr; tptr++)
						ttyld_rint(tp,
						    (tptr[0] |
						    rc_rcsrt[tptr[INPUT_FLAGS_SHIFT] & 0xF]));
				}
done1: ;
			}
			if (rc->rc_flags & RC_DOXXFER) {
				critical_enter();
				sc->sc_scheduled_event -= LOTS_OF_EVENTS;
				rc->rc_flags &= ~RC_DOXXFER;
				rc->rc_tp->t_state &= ~TS_BUSY;
				critical_exit();
				ttyld_start(tp);
			}
			if (sc->sc_scheduled_event == 0)
				break;
		}
	} while (sc->sc_scheduled_event >= LOTS_OF_EVENTS);
}

static void
rc_stop(struct tty *tp, int rw)
{
	struct rc_softc *sc;
	struct rc_chans *rc;
	u_char *tptr, *eptr;

	rc = tp->t_sc;
	sc = rc->rc_rcb;
#ifdef RCDEBUG
	device_printf(sc->sc_dev, "channel %d: rc_stop %s%s\n",
	    rc->rc_chan, (rw & FWRITE)?"FWRITE ":"", (rw & FREAD)?"FREAD":"");
#endif
	if (rw & FWRITE)
		rc_discard_output(rc);
	critical_enter();
	if (rw & FREAD) {
		rc->rc_flags &= ~RC_DORXFER;
		eptr = rc->rc_iptr;
		if (rc->rc_bufend == &rc->rc_ibuf[2 * RC_IBUFSIZE]) {
			tptr = &rc->rc_ibuf[RC_IBUFSIZE];
			rc->rc_iptr = &rc->rc_ibuf[RC_IBUFSIZE];
		} else {
			tptr = rc->rc_ibuf;
			rc->rc_iptr = rc->rc_ibuf;
		}
		sc->sc_scheduled_event -= eptr - tptr;
	}
	if (tp->t_state & TS_TTSTOP)
		rc->rc_flags |= RC_OSUSP;
	else
		rc->rc_flags &= ~RC_OSUSP;
	critical_exit();
}

static void
rc_close(struct tty *tp)
{
	struct rc_chans *rc;
	struct rc_softc *sc;
	int s;

	rc = tp->t_sc;
	sc = rc->rc_rcb;
	s = spltty();
	rcout(sc, CD180_CAR, rc->rc_chan);

	/* Disable rx/tx intrs */
	rcout(sc, CD180_IER, rc->rc_ier = 0);
	if (   (tp->t_cflag & HUPCL)
	    || (!(rc->rc_flags & RC_ACTOUT)
	       && !(rc->rc_msvr & MSVR_CD)
	       && !(tp->t_cflag & CLOCAL))
	    || !(tp->t_state & TS_ISOPEN)
	   ) {
		CCRCMD(sc, rc->rc_chan, CCR_ResetChan);
		WAITFORCCR(sc, rc->rc_chan);
		(void) rc_modem(tp, SER_RTS, 0);
		ttydtrwaitstart(tp);
	}
	rc->rc_flags &= ~RC_ACTOUT;
	wakeup( &rc->rc_rcb);  /* wake bi */
	wakeup(TSA_CARR_ON(tp));
	(void) splx(s);
}

/* Reset the bastard */
static void
rc_hwreset(struct rc_softc *sc, u_int chipid)
{
	CCRCMD(sc, -1, CCR_HWRESET);            /* Hardware reset */
	DELAY(20000);
	WAITFORCCR(sc, -1);

	rcout(sc, RC_CTOUT, 0);             /* Clear timeout  */
	rcout(sc, CD180_GIVR,  chipid);
	rcout(sc, CD180_GICR,  0);

	/* Set Prescaler Registers (1 msec) */
	rcout(sc, CD180_PPRL, ((RC_OSCFREQ + 999) / 1000) & 0xFF);
	rcout(sc, CD180_PPRH, ((RC_OSCFREQ + 999) / 1000) >> 8);

	/* Initialize Priority Interrupt Level Registers */
	rcout(sc, CD180_PILR1, RC_PILR_MODEM);
	rcout(sc, CD180_PILR2, RC_PILR_TX);
	rcout(sc, CD180_PILR3, RC_PILR_RX);

	/* Reset DTR */
	rcout(sc, RC_DTREG, ~0);
}

/* Set channel parameters */
static int
rc_param(struct tty *tp, struct termios *ts)
{
	struct rc_softc *sc;
	struct rc_chans *rc;
	int idivs, odivs, s, val, cflag, iflag, lflag, inpflow;

	if (   ts->c_ospeed < 0 || ts->c_ospeed > 76800
	    || ts->c_ispeed < 0 || ts->c_ispeed > 76800
	   )
		return (EINVAL);
	if (ts->c_ispeed == 0)
		ts->c_ispeed = ts->c_ospeed;
	odivs = RC_BRD(ts->c_ospeed);
	idivs = RC_BRD(ts->c_ispeed);

	rc = tp->t_sc;
	sc = rc->rc_rcb;
	s = spltty();

	/* Select channel */
	rcout(sc, CD180_CAR, rc->rc_chan);

	/* If speed == 0, hangup line */
	if (ts->c_ospeed == 0) {
		CCRCMD(sc, rc->rc_chan, CCR_ResetChan);
		WAITFORCCR(sc, rc->rc_chan);
		(void) rc_modem(tp, 0, SER_DTR);
	}

	tp->t_state &= ~TS_CAN_BYPASS_L_RINT;
	cflag = ts->c_cflag;
	iflag = ts->c_iflag;
	lflag = ts->c_lflag;

	if (idivs > 0) {
		rcout(sc, CD180_RBPRL, idivs & 0xFF);
		rcout(sc, CD180_RBPRH, idivs >> 8);
	}
	if (odivs > 0) {
		rcout(sc, CD180_TBPRL, odivs & 0xFF);
		rcout(sc, CD180_TBPRH, odivs >> 8);
	}

	/* set timeout value */
	if (ts->c_ispeed > 0) {
		int itm = ts->c_ispeed > 2400 ? 5 : 10000 / ts->c_ispeed + 1;

		if (   !(lflag & ICANON)
		    && ts->c_cc[VMIN] != 0 && ts->c_cc[VTIME] != 0
		    && ts->c_cc[VTIME] * 10 > itm)
			itm = ts->c_cc[VTIME] * 10;

		rcout(sc, CD180_RTPR, itm <= 255 ? itm : 255);
	}

	switch (cflag & CSIZE) {
		case CS5:       val = COR1_5BITS;      break;
		case CS6:       val = COR1_6BITS;      break;
		case CS7:       val = COR1_7BITS;      break;
		default:
		case CS8:       val = COR1_8BITS;      break;
	}
	if (cflag & PARENB) {
		val |= COR1_NORMPAR;
		if (cflag & PARODD)
			val |= COR1_ODDP;
		if (!(cflag & INPCK))
			val |= COR1_Ignore;
	} else
		val |= COR1_Ignore;
	if (cflag & CSTOPB)
		val |= COR1_2SB;
	rcout(sc, CD180_COR1, val);

	/* Set FIFO threshold */
	val = ts->c_ospeed <= 4800 ? 1 : CD180_NFIFO / 2;
	inpflow = 0;
	if (   (iflag & IXOFF)
	    && (   ts->c_cc[VSTOP] != _POSIX_VDISABLE
		&& (   ts->c_cc[VSTART] != _POSIX_VDISABLE
		    || (iflag & IXANY)
		   )
	       )
	   ) {
		inpflow = 1;
		val |= COR3_SCDE|COR3_FCT;
	}
	rcout(sc, CD180_COR3, val);

	/* Initialize on-chip automatic flow control */
	val = 0;
	rc->rc_flags &= ~(RC_CTSFLOW|RC_SEND_RDY);
	if (cflag & CCTS_OFLOW) {
		rc->rc_flags |= RC_CTSFLOW;
		val |= COR2_CtsAE;
	} else
		rc->rc_flags |= RC_SEND_RDY;
	if (tp->t_state & TS_TTSTOP)
		rc->rc_flags |= RC_OSUSP;
	else
		rc->rc_flags &= ~RC_OSUSP;
	if (cflag & CRTS_IFLOW)
		rc->rc_flags |= RC_RTSFLOW;
	else
		rc->rc_flags &= ~RC_RTSFLOW;

	if (inpflow) {
		if (ts->c_cc[VSTART] != _POSIX_VDISABLE)
			rcout(sc, CD180_SCHR1, ts->c_cc[VSTART]);
		rcout(sc, CD180_SCHR2, ts->c_cc[VSTOP]);
		val |= COR2_TxIBE;
		if (iflag & IXANY)
			val |= COR2_IXM;
	}

	rcout(sc, CD180_COR2, rc->rc_cor2 = val);

	CCRCMD(sc, rc->rc_chan, CCR_CORCHG1 | CCR_CORCHG2 | CCR_CORCHG3);

	ttyldoptim(tp);

	/* modem ctl */
	val = cflag & CLOCAL ? 0 : MCOR1_CDzd;
	if (cflag & CCTS_OFLOW)
		val |= MCOR1_CTSzd;
	rcout(sc, CD180_MCOR1, val);

	val = cflag & CLOCAL ? 0 : MCOR2_CDod;
	if (cflag & CCTS_OFLOW)
		val |= MCOR2_CTSod;
	rcout(sc, CD180_MCOR2, val);

	/* enable i/o and interrupts */
	CCRCMD(sc, rc->rc_chan,
		CCR_XMTREN | ((cflag & CREAD) ? CCR_RCVREN : CCR_RCVRDIS));
	WAITFORCCR(sc, rc->rc_chan);

	rc->rc_ier = cflag & CLOCAL ? 0 : IER_CD;
	if (cflag & CCTS_OFLOW)
		rc->rc_ier |= IER_CTS;
	if (cflag & CREAD)
		rc->rc_ier |= IER_RxData;
	if (tp->t_state & TS_BUSY)
		rc->rc_ier |= IER_TxRdy;
	if (ts->c_ospeed != 0)
		rc_modem(tp, SER_DTR, 0);
	if ((cflag & CCTS_OFLOW) && (rc->rc_msvr & MSVR_CTS))
		rc->rc_flags |= RC_SEND_RDY;
	rcout(sc, CD180_IER, rc->rc_ier);
	(void) splx(s);
	return 0;
}

/* Re-initialize board after bogus interrupts */
static void
rc_reinit(struct rc_softc *sc)
{
	struct rc_chans *rc;
	int i;

	rc_hwreset(sc, RC_FAKEID);
	rc = sc->sc_channels;
	for (i = 0; i < CD180_NCHAN; i++, rc++)
		(void) rc_param(rc->rc_tp, &rc->rc_tp->t_termios);
}

/* Modem control routines */

static int
rc_modem(struct tty *tp, int biton, int bitoff)
{
	struct rc_chans *rc;
	struct rc_softc *sc;
	u_char *dtr;
	u_char msvr;

	rc = tp->t_sc;
	sc = rc->rc_rcb;
	dtr = &sc->sc_dtr;
	rcout(sc, CD180_CAR, rc->rc_chan);

	if (biton == 0 && bitoff == 0) {
		msvr = rc->rc_msvr = rcin(sc, CD180_MSVR);

		if (msvr & MSVR_RTS)
			biton |= SER_RTS;
		if (msvr & MSVR_CTS)
			biton |= SER_CTS;
		if (msvr & MSVR_DSR)
			biton |= SER_DSR;
		if (msvr & MSVR_DTR)
			biton |= SER_DTR;
		if (msvr & MSVR_CD)
			biton |= SER_DCD;
		if (~rcin(sc, RC_RIREG) & (1 << rc->rc_chan))
			biton |= SER_RI;
		return biton;
	}
	if (biton & SER_DTR)
		rcout(sc, RC_DTREG, ~(*dtr |= 1 << rc->rc_chan));
	if (bitoff & SER_DTR)
		rcout(sc, RC_DTREG, ~(*dtr &= ~(1 << rc->rc_chan)));
	msvr = rcin(sc, CD180_MSVR);
	if (biton & SER_DTR)
		msvr |= MSVR_DTR;
	if (bitoff & SER_DTR)
		msvr &= ~MSVR_DTR;
	if (biton & SER_RTS)
		msvr |= MSVR_RTS;
	if (bitoff & SER_RTS)
		msvr &= ~MSVR_RTS;
	rcout(sc, CD180_MSVR, msvr);
	return 0;
}

static void
rc_break(struct tty *tp, int brk)
{
	struct rc_chans *rc;

	rc = tp->t_sc;

	if (brk)
		rc->rc_pendcmd = CD180_C_SBRK;
	else
		rc->rc_pendcmd = CD180_C_EBRK;
}

#define ERR(s) do {							\
	device_printf(sc->sc_dev, "%s", "");				\
	printf s ;							\
	printf("\n");							\
	(void) splx(old_level);						\
	return 1;							\
} while (0)

/* Test the board. */
int
rc_test(struct rc_softc *sc)
{
	int     chan = 0;
	int     i = 0, rcnt, old_level;
	unsigned int    iack, chipid;
	unsigned short  divs;
	static  u_char  ctest[] = "\377\125\252\045\244\0\377";
#define CTLEN   8

	struct rtest {
		u_char  txbuf[CD180_NFIFO];     /* TX buffer  */
		u_char  rxbuf[CD180_NFIFO];     /* RX buffer  */
		int     rxptr;                  /* RX pointer */
		int     txptr;                  /* TX pointer */
	} tchans[CD180_NCHAN];

	old_level = spltty();

	chipid = RC_FAKEID;

	/* First, reset board to initial state */
	rc_hwreset(sc, chipid);

	divs = RC_BRD(19200);

	/* Initialize channels */
	for (chan = 0; chan < CD180_NCHAN; chan++) {

		/* Select and reset channel */
		rcout(sc, CD180_CAR, chan);
		CCRCMD(sc, chan, CCR_ResetChan);
		WAITFORCCR(sc, chan);

		/* Set speed */
		rcout(sc, CD180_RBPRL, divs & 0xFF);
		rcout(sc, CD180_RBPRH, divs >> 8);
		rcout(sc, CD180_TBPRL, divs & 0xFF);
		rcout(sc, CD180_TBPRH, divs >> 8);

		/* set timeout value */
		rcout(sc, CD180_RTPR,  0);

		/* Establish local loopback */
		rcout(sc, CD180_COR1, COR1_NOPAR | COR1_8BITS | COR1_1SB);
		rcout(sc, CD180_COR2, COR2_LLM);
		rcout(sc, CD180_COR3, CD180_NFIFO);
		CCRCMD(sc, chan, CCR_CORCHG1 | CCR_CORCHG2 | CCR_CORCHG3);
		CCRCMD(sc, chan, CCR_RCVREN | CCR_XMTREN);
		WAITFORCCR(sc, chan);
		rcout(sc, CD180_MSVR, MSVR_RTS);

		/* Fill TXBUF with test data */
		for (i = 0; i < CD180_NFIFO; i++) {
			tchans[chan].txbuf[i] = ctest[i];
			tchans[chan].rxbuf[i] = 0;
		}
		tchans[chan].txptr = tchans[chan].rxptr = 0;

		/* Now, start transmit */
		rcout(sc, CD180_IER, IER_TxMpty|IER_RxData);
	}
	/* Pseudo-interrupt poll stuff */
	for (rcnt = 10000; rcnt-- > 0; rcnt--) {
		i = ~(rcin(sc, RC_BSR));
		if (i & RC_BSR_TOUT)
			ERR(("BSR timeout bit set\n"));
		else if (i & RC_BSR_TXINT) {
			iack = rcin(sc, RC_PILR_TX);
			if (iack != (GIVR_IT_TDI | chipid))
				ERR(("Bad TX intr ack (%02x != %02x)\n",
					iack, GIVR_IT_TDI | chipid));
			chan = (rcin(sc, CD180_GICR) & GICR_CHAN) >> GICR_LSH;
			/* If no more data to transmit, disable TX intr */
			if (tchans[chan].txptr >= CD180_NFIFO) {
				iack = rcin(sc, CD180_IER);
				rcout(sc, CD180_IER, iack & ~IER_TxMpty);
			} else {
				for (iack = tchans[chan].txptr;
				    iack < CD180_NFIFO; iack++)
					rcout(sc, CD180_TDR,
					    tchans[chan].txbuf[iack]);
				tchans[chan].txptr = iack;
			}
			rcout(sc, CD180_EOIR, 0);
		} else if (i & RC_BSR_RXINT) {
			u_char ucnt;

			iack = rcin(sc, RC_PILR_RX);
			if (iack != (GIVR_IT_RGDI | chipid) &&
			    iack != (GIVR_IT_REI  | chipid))
				ERR(("Bad RX intr ack (%02x != %02x)\n",
					iack, GIVR_IT_RGDI | chipid));
			chan = (rcin(sc, CD180_GICR) & GICR_CHAN) >> GICR_LSH;
			ucnt = rcin(sc, CD180_RDCR) & 0xF;
			while (ucnt-- > 0) {
				iack = rcin(sc, CD180_RCSR);
				if (iack & RCSR_Timeout)
					break;
				if (iack & 0xF)
					ERR(("Bad char chan %d (RCSR = %02X)\n",
					    chan, iack));
				if (tchans[chan].rxptr > CD180_NFIFO)
					ERR(("Got extra chars chan %d\n",
					    chan));
				tchans[chan].rxbuf[tchans[chan].rxptr++] =
					rcin(sc, CD180_RDR);
			}
			rcout(sc, CD180_EOIR, 0);
		}
		rcout(sc, RC_CTOUT, 0);
		for (iack = chan = 0; chan < CD180_NCHAN; chan++)
			if (tchans[chan].rxptr >= CD180_NFIFO)
				iack++;
		if (iack == CD180_NCHAN)
			break;
	}
	for (chan = 0; chan < CD180_NCHAN; chan++) {
		/* Select and reset channel */
		rcout(sc, CD180_CAR, chan);
		CCRCMD(sc, chan, CCR_ResetChan);
	}

	if (!rcnt)
		ERR(("looses characters during local loopback\n"));
	/* Now, check data */
	for (chan = 0; chan < CD180_NCHAN; chan++)
		for (i = 0; i < CD180_NFIFO; i++)
			if (ctest[i] != tchans[chan].rxbuf[i])
				ERR(("data mismatch chan %d ptr %d (%d != %d)\n",
				    chan, i, ctest[i], tchans[chan].rxbuf[i]));
	(void) splx(old_level);
	return 0;
}

#ifdef RCDEBUG
static void
printrcflags(struct rc_chans *rc, char *comment)
{
	struct rc_softc *sc;
	u_short f = rc->rc_flags;

	sc = rc->rc_rcb;
	printf("rc%d/%d: %s flags: %s%s%s%s%s%s%s%s%s%s%s%s\n",
		rc->rc_rcb->rcb_unit, rc->rc_chan, comment,
		(f & RC_DTR_OFF)?"DTR_OFF " :"",
		(f & RC_ACTOUT) ?"ACTOUT " :"",
		(f & RC_RTSFLOW)?"RTSFLOW " :"",
		(f & RC_CTSFLOW)?"CTSFLOW " :"",
		(f & RC_DORXFER)?"DORXFER " :"",
		(f & RC_DOXXFER)?"DOXXFER " :"",
		(f & RC_MODCHG) ?"MODCHG "  :"",
		(f & RC_OSUSP)  ?"OSUSP " :"",
		(f & RC_OSBUSY) ?"OSBUSY " :"",
		(f & RC_WAS_BUFOVFL) ?"BUFOVFL " :"",
		(f & RC_WAS_SILOVFL) ?"SILOVFL " :"",
		(f & RC_SEND_RDY) ?"SEND_RDY":"");

	rcout(sc, CD180_CAR, rc->rc_chan);

	printf("rc%d/%d: msvr %02x ier %02x ccsr %02x\n",
		rc->rc_rcb->rcb_unit, rc->rc_chan,
		rcin(sc, CD180_MSVR),
		rcin(sc, CD180_IER),
		rcin(sc, CD180_CCSR));
}
#endif /* RCDEBUG */

static void
rc_discard_output(struct rc_chans *rc)
{
	critical_enter();
	if (rc->rc_flags & RC_DOXXFER) {
		rc->rc_rcb->sc_scheduled_event -= LOTS_OF_EVENTS;
		rc->rc_flags &= ~RC_DOXXFER;
	}
	rc->rc_optr = rc->rc_obufend;
	rc->rc_tp->t_state &= ~TS_BUSY;
	critical_exit();
	ttwwakeup(rc->rc_tp);
}

static void
rc_wait0(struct rc_softc *sc, int chan, int line)
{
	int rcnt;

	for (rcnt = 50; rcnt && rcin(sc, CD180_CCR); rcnt--)
		DELAY(30);
	if (rcnt == 0)
		device_printf(sc->sc_dev,
		    "channel %d command timeout, rc.c line: %d\n", chan, line);
}

static device_method_t rc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		rc_probe),
	DEVMETHOD(device_attach,	rc_attach),
	DEVMETHOD(device_detach,	rc_detach),
	{ 0, 0 }
};

static driver_t rc_driver = {
	"rc",
	rc_methods, sizeof(struct rc_softc),
};

DRIVER_MODULE(rc, isa, rc_driver, rc_devclass, 0, 0);
