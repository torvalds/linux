/* $OpenBSD: siotty.c,v 1.25 2021/01/09 02:34:21 aoyama Exp $ */
/* $NetBSD: siotty.c,v 1.9 2002/03/17 19:40:43 atatat Exp $ */

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Tohru Nishimura.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/conf.h>
#include <sys/ioctl.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/tty.h>
#include <sys/uio.h>
#include <sys/fcntl.h>
#include <dev/cons.h>

#include <machine/board.h>
#include <machine/cpu.h>

#include <luna88k/dev/sioreg.h>
#include <luna88k/dev/siovar.h>

#define	TIOCM_BREAK 01000 /* non standard use */

static const u_int8_t ch0_regs[6] = {
	WR0_RSTINT,				/* reset E/S interrupt */
	WR1_RXALLS | WR1_TXENBL,		/* Rx per char, Tx */
	0,					/* */
	WR3_RX8BIT | WR3_RXENBL,		/* Rx */
	WR4_BAUD96 | WR4_STOP1,			/* Tx/Rx */
	WR5_TX8BIT | WR5_TXENBL | WR5_DTR | WR5_RTS, /* Tx */
};

static const struct speedtab siospeedtab[] = {
	{ 2400,	WR4_BAUD24, },
	{ 4800,	WR4_BAUD48, },
	{ 9600,	WR4_BAUD96, },
	{ -1,	0, },
};

struct siotty_softc {
	struct device	sc_dev;
	struct tty	*sc_tty;
	struct sioreg	*sc_ctl;
	u_int 		sc_flags;
	u_int8_t	sc_wr[6];
	void		*sc_si;		/* software interrupt handler */
	u_int		sc_hwflags;
#define	SIOTTY_HW_CONSOLE	0x0001

	u_int8_t	*sc_rbuf;
	u_int8_t	*sc_rbufend;
	u_int8_t * volatile sc_rbget;
	u_int8_t * volatile sc_rbput;
	volatile u_int	sc_rbavail;

	u_int8_t	*sc_tba;
	u_int		sc_tbc;

	bool		sc_rx_ready;
	bool		sc_tx_busy;
	bool		sc_tx_done;
};

#define	SIOTTY_RING_SIZE	2048
u_int siotty_rbuf_size = SIOTTY_RING_SIZE;

cdev_decl(sio);
void siostart(struct tty *);
int  sioparam(struct tty *, struct termios *);
void siottyintr(void *);
void siottysoft(void *);
void siotty_rxsoft(struct siotty_softc *, struct tty *);
void siotty_txsoft(struct siotty_softc *, struct tty *);
int  siomctl(struct siotty_softc *, int, int);

int  siotty_match(struct device *, void *, void *);
void siotty_attach(struct device *, struct device *, void *);

const struct cfattach siotty_ca = {
	sizeof(struct siotty_softc), siotty_match, siotty_attach
};

struct cfdriver siotty_cd = {
	NULL, "siotty", DV_TTY
};

int 
siotty_match(struct device *parent, void *cf, void *aux)
{
	struct sio_attach_args *args = aux;

	if (args->channel != 0) /* XXX allow tty on Ch.B XXX */
		return 0;
	return 1;
}

void 
siotty_attach(struct device *parent, struct device *self, void *aux)
{
	struct sio_softc *siosc = (void *)parent;
	struct siotty_softc *sc = (void *)self;
	struct sio_attach_args *args = aux;
	int channel;
	struct tty *tp;

	channel = args->channel;
	sc->sc_ctl = &siosc->sc_ctl[channel];
	memcpy(sc->sc_wr, ch0_regs, sizeof(ch0_regs));
	siosc->sc_intrhand[channel].ih_func = siottyintr;
	siosc->sc_intrhand[channel].ih_arg = sc;
	if (args->hwflags == 1)
		sc->sc_hwflags |= SIOTTY_HW_CONSOLE;

	if (sc->sc_hwflags & SIOTTY_HW_CONSOLE) {
		printf(" (console)");
		sc->sc_flags = TIOCFLAG_SOFTCAR;
	} else {
		setsioreg(sc->sc_ctl, WR0, WR0_CHANRST);
		setsioreg(sc->sc_ctl, WR2A, WR2_VEC86 | WR2_INTR_1);
		setsioreg(sc->sc_ctl, WR2B, 0);
		setsioreg(sc->sc_ctl, WR0, sc->sc_wr[WR0]);
		setsioreg(sc->sc_ctl, WR4, sc->sc_wr[WR4]);
		setsioreg(sc->sc_ctl, WR3, sc->sc_wr[WR3]);
		setsioreg(sc->sc_ctl, WR5, sc->sc_wr[WR5]);
		setsioreg(sc->sc_ctl, WR0, sc->sc_wr[WR0]);
	}
	setsioreg(sc->sc_ctl, WR1, sc->sc_wr[WR1]); /* now interrupt driven */

	printf("\n");

	sc->sc_rbuf = malloc(siotty_rbuf_size * 2, M_DEVBUF, M_NOWAIT);
	if (sc->sc_rbuf == NULL) {
		printf("%s: unable to allocate ring buffer\n",
		    (sc->sc_dev).dv_xname);
		return;
	}
	sc->sc_rbufend = sc->sc_rbuf + (siotty_rbuf_size * 2);
	sc->sc_rbput = sc->sc_rbget = sc->sc_rbuf;
	sc->sc_rbavail = siotty_rbuf_size;

	tp = ttymalloc(0);
	tp->t_oproc = siostart;
	tp->t_param = sioparam;
	tp->t_hwiflow = NULL /* XXX siohwiflow XXX */;
	if (sc->sc_hwflags & SIOTTY_HW_CONSOLE)
		tp->t_dev = cn_tab->cn_dev;
	sc->sc_tty = tp;

	sc->sc_si = softintr_establish(IPL_SOFTTTY, siottysoft, sc);
}

/*--------------------  low level routine --------------------*/

void
siottyintr(void *arg)
{
	struct siotty_softc *sc;
	struct sioreg *sio;
	u_int8_t *put, *end;
	u_int8_t c;
	u_int16_t rr;
	int cc;

	sc = arg;
	end = sc->sc_rbufend;
	put = sc->sc_rbput;
	cc = sc->sc_rbavail;

	sio = sc->sc_ctl;
	rr = getsiocsr(sio);
	if (rr & RR_RXRDY) {
		do {
			if (cc > 0) {
				c = sio->sio_data;
				put[0] = c;
				put[1] = rr & 0xff;
				put += 2;
				if (put >= end)
					put = sc->sc_rbuf;
				cc--;
			}
			if (rr & (RR_FRAMING | RR_OVERRUN | RR_PARITY))
				sio->sio_cmd = WR0_ERRRST;

			sc->sc_rbput = put;
			sc->sc_rbavail = cc;
			sc->sc_rx_ready = true;
		} while ((rr = getsiocsr(sio)) & RR_RXRDY);
	}
	if (rr & RR_TXRDY) {
		sio->sio_cmd = WR0_RSTPEND;
		if (sc->sc_tbc > 0) {
			sio->sio_data = *sc->sc_tba;
			sc->sc_tba++;
			sc->sc_tbc--;
		} else {
			if (sc->sc_tx_busy) {
				sc->sc_tx_busy = false;
				sc->sc_tx_done = true;
			}
		}
	}
	softintr_schedule(sc->sc_si);
}

void
siottysoft(void *arg)
{
	struct siotty_softc *sc;
	struct tty *tp;

	sc = arg;
	tp = sc->sc_tty;

	if (sc->sc_rx_ready) {
		sc->sc_rx_ready = false;
		siotty_rxsoft(sc, tp);
	}
	if (sc->sc_tx_done) {
		sc->sc_tx_done = false;
		siotty_txsoft(sc, tp);
	}
}

void
siotty_rxsoft(struct siotty_softc *sc, struct tty *tp)
{
	uint8_t *get, *end;
	u_int cc, scc;
	unsigned int code;
	uint8_t stat;
	int s;

	end = sc->sc_rbufend;
	get = sc->sc_rbget;
	scc = cc = siotty_rbuf_size - sc->sc_rbavail;

	if (cc == siotty_rbuf_size) {
		printf("%s: rx buffer overflow\n", (sc->sc_dev).dv_xname);
	}

	while (cc > 0) {
		code = get[0];
		stat = get[1];
		if ((stat & RR_FRAMING) != 0)
			code |= TTY_FE;
		else if ((stat & RR_PARITY) != 0)
			code |= TTY_PE;

		(*linesw[tp->t_line].l_rint)(code, tp);
		get += 2;
		if (get >= end)
			get = sc->sc_rbuf;
		cc--;
	}

	if (cc != scc) {
		s = spltty();
		sc->sc_rbget = get;
		sc->sc_rbavail += scc - cc;
		splx(s);
	}
}

void
siotty_txsoft(struct siotty_softc *sc, struct tty *tp)
{

	tp->t_state &= ~TS_BUSY;
	if ((tp->t_state & TS_FLUSH) != 0)
		tp->t_state &= ~TS_FLUSH;
	else
		ndflush(&tp->t_outq, (int)(sc->sc_tba - tp->t_outq.c_cf));
	(*linesw[tp->t_line].l_start)(tp);
}

void
siostart(struct tty *tp)
{
	struct siotty_softc *sc = siotty_cd.cd_devs[minor(tp->t_dev)];
	int s;
	u_int8_t *tba;
	int tbc;
 
	s = spltty();
	if (tp->t_state & (TS_BUSY|TS_TIMEOUT|TS_TTSTOP))
		goto out;
	ttwakeupwr(tp);
	if (tp->t_outq.c_cc == 0)
		goto out;

	tp->t_state |= TS_BUSY;

	tba = tp->t_outq.c_cf;
	tbc = ndqb(&tp->t_outq, 0);

	sc->sc_tba = tba;
	sc->sc_tbc = tbc;
	sc->sc_tx_busy = true;

	sc->sc_ctl->sio_data = *sc->sc_tba;
	sc->sc_tba++;
	sc->sc_tbc--;
out:
	splx(s);
}

int
siostop(struct tty *tp, int flag)
{
	int s;

	s = spltty();
	if (TS_BUSY == (tp->t_state & (TS_BUSY|TS_TTSTOP))) {
		/*
		 * Device is transmitting; must stop it.
		 */
		tp->t_state |= TS_FLUSH;
	}
	splx(s);
	return (0);
}

int
sioparam(struct tty *tp, struct termios *t)
{
	struct siotty_softc *sc = siotty_cd.cd_devs[minor(tp->t_dev)];
	int wr4, s;

	if (t->c_ispeed && t->c_ispeed != t->c_ospeed)
		return EINVAL;
	wr4 = ttspeedtab(t->c_ospeed, siospeedtab);
	if (wr4 < 0)
		return EINVAL;

	if (sc->sc_flags & TIOCFLAG_SOFTCAR) {
		t->c_cflag |= CLOCAL;
		t->c_cflag &= ~HUPCL;
	}
	if (sc->sc_flags & TIOCFLAG_CLOCAL)
		t->c_cflag |= CLOCAL;

	/*
	 * If there were no changes, don't do anything.  This avoids dropping
	 * input and improves performance when all we did was frob things like
	 * VMIN and VTIME.
	 */
	if (tp->t_ospeed == t->c_ospeed && tp->t_cflag == t->c_cflag)
		return 0;

	tp->t_ispeed = t->c_ispeed;
	tp->t_ospeed = t->c_ospeed;
	tp->t_cflag = t->c_cflag;

	sc->sc_wr[WR3] &= 0x3f;
	sc->sc_wr[WR5] &= 0x9f;
	switch (tp->t_cflag & CSIZE) {
	case CS7:
		sc->sc_wr[WR3] |= WR3_RX7BIT; sc->sc_wr[WR5] |= WR5_TX7BIT;
		break;
	case CS8:
		sc->sc_wr[WR3] |= WR3_RX8BIT; sc->sc_wr[WR5] |= WR5_TX8BIT;
		break;
	}
	if (tp->t_cflag & PARENB) {
		wr4 |= WR4_PARENAB;
		if ((tp->t_cflag & PARODD) == 0)
			wr4 |= WR4_EPARITY;
	}
	wr4 |= (tp->t_cflag & CSTOPB) ? WR4_STOP2 : WR4_STOP1;	
	sc->sc_wr[WR4] = wr4;

	s = spltty();
	setsioreg(sc->sc_ctl, WR4, sc->sc_wr[WR4]);
	setsioreg(sc->sc_ctl, WR3, sc->sc_wr[WR3]);
	setsioreg(sc->sc_ctl, WR5, sc->sc_wr[WR5]);
	splx(s);

	return 0;
}

int
siomctl(struct siotty_softc *sc, int control, int op)
{
	int val, s, wr5, rr;

	val = 0;
	if (control & TIOCM_BREAK)
		val |= WR5_BREAK;
	if (control & TIOCM_DTR)
		val |= WR5_DTR;
	if (control & TIOCM_RTS)
		val |= WR5_RTS;
	s = spltty();
	wr5 = sc->sc_wr[WR5];
	switch (op) {
	case DMSET:
		wr5 &= ~(WR5_BREAK|WR5_DTR|WR5_RTS);
		/* FALLTHROUGH */
	case DMBIS:
		wr5 |= val;
		break;
	case DMBIC:
		wr5 &= ~val;
		break;
	case DMGET:
		val = 0;
		rr = getsiocsr(sc->sc_ctl);
		if (wr5 & WR5_DTR)
			val |= TIOCM_DTR;
		if (wr5 & WR5_RTS)
			val |= TIOCM_RTS;
		if (rr & RR_CTS)
			val |= TIOCM_CTS;
		if (rr & RR_DCD)
			val |= TIOCM_CD;
		goto done;
	}
	sc->sc_wr[WR5] = wr5;
	setsioreg(sc->sc_ctl, WR5, wr5);
	val = 0;
done:
	splx(s);
	return val;
}

/*--------------------  cdevsw[] interface --------------------*/

int
sioopen(dev_t dev, int flag, int mode, struct proc *p)
{
	struct siotty_softc *sc;
	struct tty *tp;
	int s;

	if ((sc = siotty_cd.cd_devs[minor(dev)]) == NULL)
		return ENXIO;

	tp = sc->sc_tty;

	if ((tp->t_state & TS_ISOPEN) && (tp->t_state & TS_XCLUDE)
	    && suser(p) != 0)
		return EBUSY;

	if ((tp->t_state & TS_ISOPEN) == 0) {
		struct termios t;

		tp->t_dev = dev;
		t.c_ispeed = t.c_ospeed = TTYDEF_SPEED;
		t.c_cflag = TTYDEF_CFLAG;
		tp->t_ospeed = 0; /* force register update */
		(void)sioparam(tp, &t);
		tp->t_iflag = TTYDEF_IFLAG;
		tp->t_oflag = TTYDEF_OFLAG;
		tp->t_lflag = TTYDEF_LFLAG;
		ttychars(tp);
		ttsetwater(tp);
		/* raise RTS and DTR here; but, DTR lead is not wired */
		/* then check DCD condition; but, DCD lead is not wired */
#if 0
		if ((sc->sc_flags & TIOCFLAG_SOFTCAR)
		    || (tp->t_cflag & MDMBUF)
		    || (getsiocsr(sc->sc_ctl) & RR_DCD))
			tp->t_state |= TS_CARR_ON;
		else
			tp->t_state &= ~TS_CARR_ON;
#else
		tp->t_state |= TS_CARR_ON; /* assume detected all the time */
#endif
		s = spltty();
		sc->sc_rbput = sc->sc_rbget = sc->sc_rbuf;
		sc->sc_rbavail = siotty_rbuf_size;
		splx(s);
	}

	return (*linesw[tp->t_line].l_open)(dev, tp, p);
}
 
int
sioclose(dev_t dev, int flag, int mode, struct proc *p)
{
	struct siotty_softc *sc = siotty_cd.cd_devs[minor(dev)];
	struct tty *tp = sc->sc_tty;
	int s;

	(*linesw[tp->t_line].l_close)(tp, flag, p);

	s = spltty();
	siomctl(sc, TIOCM_BREAK, DMBIC);
#if 0 /* because unable to feed DTR signal */
	if ((tp->t_cflag & HUPCL)
	    || tp->t_wopen || (tp->t_state & TS_ISOPEN) == 0) {
		siomctl(sc, TIOCM_DTR, DMBIC);
		/* Yield CPU time to others for 1 second, then ... */
		siomctl(sc, TIOCM_DTR, DMBIS);
	}
#endif
	splx(s);
	return ttyclose(tp);
}
 
int
sioread(dev_t dev, struct uio *uio, int flag)
{
	struct siotty_softc *sc = siotty_cd.cd_devs[minor(dev)];
	struct tty *tp = sc->sc_tty;
 
	return (*linesw[tp->t_line].l_read)(tp, uio, flag);
}
 
int
siowrite(dev_t dev, struct uio *uio, int flag)
{
	struct siotty_softc *sc = siotty_cd.cd_devs[minor(dev)];
	struct tty *tp = sc->sc_tty;
 
	return (*linesw[tp->t_line].l_write)(tp, uio, flag);
}

int
sioioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct siotty_softc *sc = siotty_cd.cd_devs[minor(dev)];
	struct tty *tp = sc->sc_tty;
	int error;

	error = (*linesw[tp->t_line].l_ioctl)(tp, cmd, data, flag, p);
	if (error >= 0)
		return error;

	error = ttioctl(tp, cmd, data, flag, p);
	if (error >= 0)
		return error;

	/* the last resort for TIOC ioctl traversing */
	switch (cmd) {
	case TIOCSBRK: /* Set the hardware into BREAK condition */
		siomctl(sc, TIOCM_BREAK, DMBIS);
		break;
	case TIOCCBRK: /* Clear the hardware BREAK condition */
		siomctl(sc, TIOCM_BREAK, DMBIC);
		break;
	case TIOCSDTR: /* Assert DTR signal */
		siomctl(sc, TIOCM_DTR|TIOCM_RTS, DMBIS);
		break;
	case TIOCCDTR: /* Clear DTR signal */
		siomctl(sc, TIOCM_DTR|TIOCM_RTS, DMBIC);
		break;
	case TIOCMSET: /* Set modem state replacing current one */
		siomctl(sc, *(int *)data, DMSET);
		break;
	case TIOCMGET: /* Return current modem state */
		*(int *)data = siomctl(sc, 0, DMGET);
		break;
	case TIOCMBIS: /* Set individual bits of modem state */
		siomctl(sc, *(int *)data, DMBIS);
		break;
	case TIOCMBIC: /* Clear individual bits of modem state */
		siomctl(sc, *(int *)data, DMBIC);
		break;
	case TIOCSFLAGS: /* Instruct how serial port behaves */
		error = suser(p);
		if (error != 0)
			return EPERM;
		sc->sc_flags = *(int *)data;
		break;
	case TIOCGFLAGS: /* Return current serial port state */
		*(int *)data = sc->sc_flags;
		break;
	default:
		return ENOTTY;
	}
	return 0;
}

/* ARSGUSED */
struct tty *
siotty(dev_t dev)
{
	struct siotty_softc *sc = siotty_cd.cd_devs[minor(dev)];
 
	return sc->sc_tty;
}

/*--------------------  miscellaneous routines --------------------*/

/* EXPORT */ void
setsioreg(struct sioreg *sio, int regno, int val)
{
	if (regno != 0)
		sio->sio_cmd = regno;	/* DELAY(); */
	sio->sio_cmd = val;		/* DELAY(); */
}

/* EXPORT */ int
getsiocsr(struct sioreg *sio)
{
	int val;

	val = sio->sio_stat << 8;	/* DELAY(); */
	sio->sio_cmd = 1;		/* DELAY(); */
	val |= sio->sio_stat;		/* DELAY(); */
	return val;
}

/*---------------------  console interface ----------------------*/

void syscnattach(int);
int  syscngetc(dev_t);
void syscnputc(dev_t, int);

struct consdev syscons = {
	NULL,
	NULL,
	syscngetc,
	syscnputc,
	nullcnpollc,
	NULL,
	NODEV,
	CN_HIGHPRI,
};

/* EXPORT */ void
syscnattach(int channel)
{
/*
 * Channel A is immediately initialized with 9600N1 right after cold
 * boot/reset/poweron.  ROM monitor emits one line message on CH.A.
 */
	struct sioreg *sio;
	sio = (struct sioreg *)OBIO_SIO + channel;

	syscons.cn_dev = makedev(12, channel);
	cn_tab = &syscons;

	setsioreg(sio, WR0, WR0_CHANRST);
	setsioreg(sio, WR2A, WR2_VEC86 | WR2_INTR_1);
	setsioreg(sio, WR2B, 0);
	setsioreg(sio, WR0, ch0_regs[WR0]);
	setsioreg(sio, WR4, ch0_regs[WR4]);
	setsioreg(sio, WR3, ch0_regs[WR3]);
	setsioreg(sio, WR5, ch0_regs[WR5]);
	setsioreg(sio, WR0, ch0_regs[WR0]);
}

/* EXPORT */ int
syscngetc(dev_t dev)
{
	struct sioreg *sio;
	int s, c;

	sio = (struct sioreg *)OBIO_SIO + ((int)dev & 0x1);
	s = splhigh();
	while ((getsiocsr(sio) & RR_RXRDY) == 0)
		;
	c = sio->sio_data;
	splx(s);

	return c;
}

/* EXPORT */ void
syscnputc(dev_t dev, int c)
{
	struct sioreg *sio;
	int s;

	sio = (struct sioreg *)OBIO_SIO + ((int)dev & 0x1);
	s = splhigh();
	while ((getsiocsr(sio) & RR_TXRDY) == 0)
		;
	sio->sio_cmd = WR0_RSTPEND;
	sio->sio_data = c;
	splx(s);
}
