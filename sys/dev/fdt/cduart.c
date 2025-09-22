/*	$OpenBSD: cduart.c,v 1.1 2021/04/24 07:49:11 visa Exp $	*/

/*
 * Copyright (c) 2021 Visa Hankala
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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
 * Driver for Cadence UART.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/fcntl.h>
#include <sys/tty.h>
#include <sys/syslog.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/cons.h>

#include <dev/ofw/fdt.h>
#include <dev/ofw/openfirm.h>

#define CDUART_CR			0x0000
#define  CDUART_CR_STOPBRK			(1 << 8)
#define  CDUART_CR_STARTBRK			(1 << 7)
#define  CDUART_CR_TORST			(1 << 6)
#define  CDUART_CR_TXDIS			(1 << 5)
#define  CDUART_CR_TXEN				(1 << 4)
#define  CDUART_CR_RXDIS			(1 << 3)
#define  CDUART_CR_RXEN				(1 << 2)
#define  CDUART_CR_TXRST			(1 << 1)
#define  CDUART_CR_RXRST			(1 << 0)
#define CDUART_MR			0x0004
#define  CDUART_MR_CHMODE_MASK			(0x3 << 8)
#define  CDUART_MR_NBSTOP_MASK			(0x3 << 6)
#define  CDUART_MR_PAR_MASK			(0x7 << 3)
#define  CDUART_MR_PAR_NO			(0x4 << 3)
#define  CDUART_MR_PAR_FORCED1			(0x3 << 3)
#define  CDUART_MR_PAR_FORCED0			(0x2 << 3)
#define  CDUART_MR_PAR_ODD			(0x1 << 3)
#define  CDUART_MR_PAR_EVEN			(0x0 << 3)
#define  CDUART_MR_CHRL_MASK			(0x3 << 1)
#define  CDUART_MR_CHRL_C6			(0x3 << 1)
#define  CDUART_MR_CHRL_C7			(0x2 << 1)
#define  CDUART_MR_CHRL_C8			(0x0 << 1)
#define  CDUART_MR_CLKSEL			(1 << 0)
#define CDUART_IER			0x0008
#define CDUART_IDR			0x000c
#define CDUART_IMR			0x0010
#define CDUART_ISR			0x0014
#define  CDUART_ISR_TXOVR			(1 << 12)
#define  CDUART_ISR_TNFUL			(1 << 11)
#define  CDUART_ISR_TTRIG			(1 << 10)
#define  CDUART_IXR_DMS				(1 << 9)
#define  CDUART_IXR_TOUT			(1 << 8)
#define  CDUART_IXR_PARITY			(1 << 7)
#define  CDUART_IXR_FRAMING			(1 << 6)
#define  CDUART_IXR_RXOVR			(1 << 5)
#define  CDUART_IXR_TXFULL			(1 << 4)
#define  CDUART_IXR_TXEMPTY			(1 << 3)
#define  CDUART_IXR_RXFULL			(1 << 2)
#define  CDUART_IXR_RXEMPTY			(1 << 1)
#define  CDUART_IXR_RTRIG			(1 << 0)
#define CDUART_RXTOUT			0x001c
#define CDUART_RXWM			0x0020
#define CDUART_SR			0x002c
#define  CDUART_SR_TNFUL			(1 << 14)
#define  CDUART_SR_TTRIG			(1 << 13)
#define  CDUART_SR_FLOWDEL			(1 << 12)
#define  CDUART_SR_TACTIVE			(1 << 11)
#define  CDUART_SR_RACTIVE			(1 << 10)
#define  CDUART_SR_TXFULL			(1 << 4)
#define  CDUART_SR_TXEMPTY			(1 << 3)
#define  CDUART_SR_RXFULL			(1 << 2)
#define  CDUART_SR_RXEMPTY			(1 << 1)
#define  CDUART_SR_RXOVR			(1 << 0)
#define CDUART_FIFO			0x0030

#define CDUART_SPACE_SIZE		0x0048

#define CDUART_FIFOSIZE		64
#define CDUART_IBUFSIZE		128

struct cduart_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	void			*sc_ih;
	void			*sc_si;

	struct tty		*sc_tty;
	uint8_t			sc_cua;

	struct timeout		sc_diag_tmo;
	int			sc_overflows;
	int			sc_floods;
	int			sc_errors;

	int			sc_ibufs[2][CDUART_IBUFSIZE];
	int			*sc_ibuf;
	int			*sc_ibufend;
	int			*sc_ibufp;
};

int	cduart_match(struct device *, void *, void *);
void	cduart_attach(struct device *, struct device *, void *);
void	cduart_diag(void *);
int	cduart_intr(void *);
void	cduart_softintr(void *);

struct tty *cduarttty(dev_t);
struct cduart_softc *cduart_sc(dev_t);

int	cduartparam(struct tty *, struct termios *);
void	cduartstart(struct tty *);

int	cduartcnattach(bus_space_tag_t, bus_addr_t, int, tcflag_t);
void	cduartcnprobe(struct consdev *);
void	cduartcninit(struct consdev *);
int	cduartcngetc(dev_t);
void	cduartcnputc(dev_t, int);
void	cduartcnpollc(dev_t, int);

cdev_decl(com);
cdev_decl(cduart);

const struct cfattach cduart_ca = {
	sizeof(struct cduart_softc), cduart_match, cduart_attach
};

struct cfdriver cduart_cd = {
	NULL, "cduart", DV_DULL
};

bus_space_tag_t	cduartconsiot;
bus_space_handle_t cduartconsioh;
int		cduartconsrate;
tcflag_t	cduartconscflag;
struct cdevsw	cduartdev = cdev_tty_init(3, cduart);

#define DEVUNIT(x)	(minor(x) & 0x7f)
#define DEVCUA(x)	(minor(x) & 0x80)

static inline uint32_t
cduart_read(struct cduart_softc *sc, uint32_t reg)
{
	return bus_space_read_4(sc->sc_iot, sc->sc_ioh, reg);
}

static inline void
cduart_write(struct cduart_softc *sc, uint32_t reg, uint32_t val)
{
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, reg, val);
}

void
cduart_init_cons(void)
{
	struct fdt_reg reg;
	void *node;

	if ((node = fdt_find_cons("cdns,uart-r1p8")) == NULL &&
	    (node = fdt_find_cons("cdns,uart-r1p12")) == NULL)
		return;
	if (fdt_get_reg(node, 0, &reg) != 0)
		return;

	cduartcnattach(fdt_cons_bs_tag, reg.addr, B115200, TTYDEF_CFLAG);
}

int
cduart_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "cdns,uart-r1p8") ||
	    OF_is_compatible(faa->fa_node, "cdns,uart-r1p12");
}

void
cduart_attach(struct device *parent, struct device *self, void *aux)
{
	struct fdt_attach_args *faa = aux;
	struct cduart_softc *sc = (struct cduart_softc *)self;
	uint32_t cr, isr;
	int maj;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh) != 0) {
		printf(": can't map registers\n");
		return;
	}

	/* Disable all interrupts. */
	cduart_write(sc, CDUART_IDR, ~0U);

	/* Clear any pending interrupts. */
	isr = cduart_read(sc, CDUART_ISR);
	cduart_write(sc, CDUART_ISR, isr);

	sc->sc_ih = fdt_intr_establish_idx(faa->fa_node, 0, IPL_TTY,
	    cduart_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		goto fail;
	}

	timeout_set(&sc->sc_diag_tmo, cduart_diag, sc);
	sc->sc_si = softintr_establish(IPL_TTY, cduart_softintr, sc);
	if (sc->sc_si == NULL) {
		printf(": can't establish soft interrupt\n");
		goto fail;
	}

	if (faa->fa_node == stdout_node) {
		/* Locate the major number. */
		for (maj = 0; maj < nchrdev; maj++) {
			if (cdevsw[maj].d_open == cduartopen)
				break;
		}
		KASSERT(maj < nchrdev);
		cn_tab->cn_dev = makedev(maj, sc->sc_dev.dv_unit);
		printf(": console");
	}

	/* Enable transmitter and receiver. */
	cr = cduart_read(sc, CDUART_CR);
	cr &= ~(CDUART_CR_TXDIS | CDUART_CR_RXDIS);
	cr |= CDUART_CR_TXEN | CDUART_CR_RXEN;
	cduart_write(sc, CDUART_CR, cr);

	printf("\n");

	return;

fail:
	if (sc->sc_si != NULL)
		softintr_disestablish(sc->sc_si);
	if (sc->sc_ih != NULL)
		fdt_intr_disestablish(sc->sc_ih);
	if (sc->sc_ioh != 0)
		bus_space_unmap(sc->sc_iot, sc->sc_ioh, CDUART_SPACE_SIZE);
}

int
cduart_intr(void *arg)
{
	struct cduart_softc *sc = arg;
	struct tty *tp = sc->sc_tty;
	int *ibufp;
	uint32_t isr, sr;
	int c, handled = 0;

	if (tp == NULL)
		return 0;

	isr = cduart_read(sc, CDUART_ISR);
	cduart_write(sc, CDUART_ISR, isr);

	if ((isr & CDUART_IXR_TXEMPTY) && (tp->t_state & TS_BUSY)) {
		tp->t_state &= ~TS_BUSY;
		(*linesw[tp->t_line].l_start)(tp);
		handled = 1;
	}

	if (isr & (CDUART_IXR_TOUT | CDUART_IXR_RTRIG)) {
		ibufp = sc->sc_ibufp;
		for (;;) {
			sr = cduart_read(sc, CDUART_SR);
			if (sr & CDUART_SR_RXEMPTY)
				break;
			c = cduart_read(sc, CDUART_FIFO) & 0xff;

			if (ibufp < sc->sc_ibufend) {
				*ibufp++ = c;
			} else {
				sc->sc_floods++;
				if (sc->sc_errors++ == 0)
					timeout_add_sec(&sc->sc_diag_tmo, 60);
			}
		}
		if (sc->sc_ibufp != ibufp) {
			sc->sc_ibufp = ibufp;
			softintr_schedule(sc->sc_si);
		}
		handled = 1;
	}

	if (isr & CDUART_IXR_RXOVR) {
		sc->sc_overflows++;
		if (sc->sc_errors++ == 0)
			timeout_add_sec(&sc->sc_diag_tmo, 60);
		handled = 1;
	}

	return handled;
}

void
cduart_softintr(void *arg)
{
	struct cduart_softc *sc = arg;
	struct tty *tp = sc->sc_tty;
	int *ibufend, *ibufp;
	int s;

	s = spltty();

	ibufp = sc->sc_ibuf;
	ibufend = sc->sc_ibufp;

	if (ibufp == ibufend) {
		splx(s);
		return;
	}

	sc->sc_ibufp = sc->sc_ibuf = (ibufp == sc->sc_ibufs[0]) ?
	    sc->sc_ibufs[1] : sc->sc_ibufs[0];
	sc->sc_ibufend = sc->sc_ibuf + CDUART_IBUFSIZE;

	if (tp == NULL || (tp->t_state & TS_ISOPEN) == 0) {
		splx(s);
		return;
	}

	splx(s);

	while (ibufp < ibufend)
		(*linesw[tp->t_line].l_rint)(*ibufp++, tp);
}

void
cduart_diag(void *arg)
{
	struct cduart_softc *sc = arg;
	int overflows, floods;
	int s;

	s = spltty();
	sc->sc_errors = 0;
	overflows = sc->sc_overflows;
	sc->sc_overflows = 0;
	floods = sc->sc_floods;
	sc->sc_floods = 0;
	splx(s);
	log(LOG_WARNING, "%s: %d silo overflow%s, %d ibuf overflow%s\n",
	    sc->sc_dev.dv_xname,
	    overflows, overflows == 1 ? "" : "s",
	    floods, floods == 1 ? "" : "s");
}

int
cduartopen(dev_t dev, int flag, int mode, struct proc *p)
{
	struct cduart_softc *sc;
	struct tty *tp;
	uint32_t sr;
	int error, s;

	sc = cduart_sc(dev);
	if (sc == NULL)
		return ENXIO;

	s = spltty();

	if (sc->sc_tty == NULL)
		sc->sc_tty = ttymalloc(0);

	tp = sc->sc_tty;
	tp->t_oproc = cduartstart;
	tp->t_param = cduartparam;
	tp->t_dev = dev;
	if ((tp->t_state & TS_ISOPEN) == 0) {
		tp->t_state |= TS_WOPEN | TS_CARR_ON;
		ttychars(tp);
		tp->t_iflag = TTYDEF_IFLAG;
		tp->t_oflag = TTYDEF_OFLAG;
		tp->t_cflag = TTYDEF_CFLAG;
		tp->t_lflag = TTYDEF_LFLAG;
		tp->t_ispeed = B115200; /* XXX */
		tp->t_ospeed = tp->t_ispeed;
		ttsetwater(tp);

		sc->sc_ibufp = sc->sc_ibuf = sc->sc_ibufs[0];
		sc->sc_ibufend = sc->sc_ibuf + CDUART_IBUFSIZE;

		cduart_write(sc, CDUART_RXTOUT, 10);
		cduart_write(sc, CDUART_RXWM, CDUART_FIFOSIZE / 2);

		/* Clear any pending I/O. */
		for (;;) {
			sr = cduart_read(sc, CDUART_SR);
			if (sr & CDUART_SR_RXEMPTY)
				break;
			(void)cduart_read(sc, CDUART_FIFO);
		}

		cduart_write(sc, CDUART_IER,
		    CDUART_IXR_TOUT | CDUART_IXR_RXOVR | CDUART_IXR_RTRIG);
	} else if ((tp->t_state & TS_XCLUDE) && suser(p) != 0) {
		splx(s);
		return EBUSY;
	}

	if (DEVCUA(dev)) {
		if (tp->t_state & TS_ISOPEN) {
			splx(s);
			return EBUSY;
		}
		sc->sc_cua = 1;
	} else {
		if ((flag & O_NONBLOCK) && sc->sc_cua) {
			splx(s);
			return EBUSY;
		} else {
			while (sc->sc_cua) {
				tp->t_state |= TS_WOPEN;
				error = ttysleep(tp, &tp->t_rawq,
				    TTIPRI | PCATCH, ttopen);
				if (error != 0 && (tp->t_state & TS_WOPEN)) {
					tp->t_state &= ~TS_WOPEN;
					splx(s);
					return error;
				}
			}
		}
	}

	splx(s);

	return (*linesw[tp->t_line].l_open)(dev, tp, p);
}

int
cduartclose(dev_t dev, int flag, int mode, struct proc *p)
{
	struct cduart_softc *sc;
	struct tty *tp;
	int s;

	sc = cduart_sc(dev);
	tp = sc->sc_tty;

	if ((tp->t_state & TS_ISOPEN) == 0)
		return 0;

	(*linesw[tp->t_line].l_close)(tp, flag, p);
	s = spltty();
	/* Disable interrupts. */
	cduart_write(sc, CDUART_IDR, ~0U);
	sc->sc_cua = 0;
	splx(s);
	ttyclose(tp);

	return 0;
}

int
cduartread(dev_t dev, struct uio *uio, int flag)
{
	struct tty *tp;

	tp = cduarttty(dev);
	if (tp == NULL)
		return ENODEV;

	return (*linesw[tp->t_line].l_read)(tp, uio, flag);
}

int
cduartwrite(dev_t dev, struct uio *uio, int flag)
{
	struct tty *tp;

	tp = cduarttty(dev);
	if (tp == NULL)
		return ENODEV;

	return (*linesw[tp->t_line].l_write)(tp, uio, flag);
}

int
cduartioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct cduart_softc *sc;
	struct tty *tp;
	int error;

	sc = cduart_sc(dev);
	if (sc == NULL)
		return ENODEV;

	tp = sc->sc_tty;
	if (tp == NULL)
		return ENXIO;

	error = (*linesw[tp->t_line].l_ioctl)(tp, cmd, data, flag, p);
	if (error >= 0)
		return error;

	error = ttioctl(tp, cmd, data, flag, p);
	if (error >= 0)
		return error;

	/* XXX */
	switch (cmd) {
	case TIOCSBRK:
	case TIOCCBRK:
	case TIOCSDTR:
	case TIOCCDTR:
	case TIOCMSET:
	case TIOCMBIC:
	case TIOCMGET:
	case TIOCGFLAGS:
		break;
	case TIOCSFLAGS:
		error = suser(p);
		if (error != 0)
			return EPERM;
		break;
	default:
		return ENOTTY;
	}

	return 0;
}

int
cduartparam(struct tty *tp, struct termios *t)
{
	return 0;
}

void
cduartstart(struct tty *tp)
{
	struct cduart_softc *sc;
	uint32_t sr;
	int s;

	sc = cduart_sc(tp->t_dev);

	s = spltty();
	if (tp->t_state & TS_BUSY)
		goto out;
	if (tp->t_state & (TS_TIMEOUT | TS_TTSTOP))
		goto stopped;
	ttwakeupwr(tp);
	if (tp->t_outq.c_cc == 0)
		goto stopped;
	tp->t_state |= TS_BUSY;

	cduart_write(sc, CDUART_ISR, CDUART_IXR_TXEMPTY);

	sr = cduart_read(sc, CDUART_SR);
	while ((sr & CDUART_SR_TXFULL) == 0 && tp->t_outq.c_cc != 0) {
		cduart_write(sc, CDUART_FIFO, getc(&tp->t_outq));
		sr = cduart_read(sc, CDUART_SR);
	}

	cduart_write(sc, CDUART_IER, CDUART_IXR_TXEMPTY);
out:
	splx(s);
	return;
stopped:
	cduart_write(sc, CDUART_IDR, CDUART_IXR_TXEMPTY);
	splx(s);
}

int
cduartstop(struct tty *tp, int flag)
{
	return 0;
}

struct tty *
cduarttty(dev_t dev)
{
	struct cduart_softc *sc;

	sc = cduart_sc(dev);
	if (sc == NULL)
		return NULL;

	return sc->sc_tty;
}

struct cduart_softc *
cduart_sc(dev_t dev)
{
	int unit = DEVUNIT(dev);

	if (unit >= cduart_cd.cd_ndevs)
		return NULL;
	return (struct cduart_softc *)cduart_cd.cd_devs[unit];
}

struct consdev cduartcons = {
	.cn_probe	= NULL,
	.cn_init	= NULL,
	.cn_getc	= cduartcngetc,
	.cn_putc	= cduartcnputc,
	.cn_pollc	= cduartcnpollc,
	.cn_bell	= NULL,
	.cn_dev		= NODEV,
	.cn_pri		= CN_MIDPRI,
};

int
cduartcnattach(bus_space_tag_t iot, bus_addr_t iobase, int rate,
    tcflag_t cflag)
{
	bus_space_handle_t ioh;
	int maj;

	/* Look for major of com(4) to replace. */
	for (maj = 0; maj < nchrdev; maj++) {
		if (cdevsw[maj].d_open == comopen)
			break;
	}
	if (maj == nchrdev)
		return ENXIO;

	if (bus_space_map(iot, iobase, CDUART_SPACE_SIZE, 0, &ioh) != 0)
		return ENOMEM;

	cn_tab = &cduartcons;
	cn_tab->cn_dev = makedev(maj, 0);
	cdevsw[maj] = cduartdev;

	cduartconsiot = iot;
	cduartconsioh = ioh;
	cduartconsrate = rate;
	cduartconscflag = cflag;

	return 0;
}

void
cduartcnprobe(struct consdev *cp)
{
}

void
cduartcninit(struct consdev *cp)
{
}

int
cduartcngetc(dev_t dev)
{
	int s;
	uint8_t c;

	s = splhigh();
	while (bus_space_read_4(cduartconsiot, cduartconsioh,
	    CDUART_SR) & CDUART_SR_RXEMPTY)
		CPU_BUSY_CYCLE();
	c = bus_space_read_4(cduartconsiot, cduartconsioh, CDUART_FIFO);
	splx(s);

	return c;
}

void
cduartcnputc(dev_t dev, int c)
{
	int s;

	s = splhigh();
	while (bus_space_read_4(cduartconsiot, cduartconsioh,
	    CDUART_SR) & CDUART_SR_TXFULL)
		CPU_BUSY_CYCLE();
	bus_space_write_4(cduartconsiot, cduartconsioh, CDUART_FIFO, c);
	splx(s);
}

void
cduartcnpollc(dev_t dev, int on)
{
}
