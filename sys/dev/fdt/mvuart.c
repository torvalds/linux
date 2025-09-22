/* $OpenBSD: mvuart.c,v 1.4 2021/10/24 17:52:26 mpi Exp $ */
/*
 * Copyright (c) 2005 Dale Rahn <drahn@motorola.com>
 * Copyright (c) 2018 Patrick Wildt <patrick@blueri.se>
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

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/proc.h>
#include <sys/tty.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/conf.h>
#include <sys/fcntl.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/cons.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/fdt.h>

#define MVUART_RBR			0x00
#define MVUART_TSH			0x04
#define MVUART_CTRL			0x08
#define  MVUART_CTRL_RX_RDY_INT			(1 << 4)
#define  MVUART_CTRL_TX_RDY_INT			(1 << 5)
#define MVUART_STAT			0x0c
#define  MVUART_STAT_STD_OVR_ERR		(1 << 0)
#define  MVUART_STAT_STD_PAR_ERR		(1 << 1)
#define  MVUART_STAT_STD_FRM_ERR		(1 << 2)
#define  MVUART_STAT_STD_BRK_DET		(1 << 3)
#define  MVUART_STAT_STD_ERROR_MASK		(0xf << 0)
#define  MVUART_STAT_STD_RX_RDY			(1 << 4)
#define  MVUART_STAT_STD_TX_RDY			(1 << 5)
#define  MVUART_STAT_STD_TX_EMPTY		(1 << 6)
#define  MVUART_STAT_STD_TX_FIFO_FULL		(1 << 11)
#define  MVUART_STAT_STD_TX_FIFO_EMPTY		(1 << 13)
#define MVUART_BAUD_RATE_DIV		0x10
#define  MVUART_BAUD_RATE_DIV_MASK		0x3ff

#define DEVUNIT(x)	(minor(x) & 0x7f)
#define DEVCUA(x)	(minor(x) & 0x80)

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct mvuart_softc {
	struct device		 sc_dev;
	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_ioh;
	int			 sc_node;
	struct soft_intrhand	*sc_si;
	void			*sc_ih;
	struct tty		*sc_tty;
	int			 sc_floods;
	int			 sc_errors;
	int			 sc_halt;
	uint8_t			 sc_hwflags;
#define COM_HW_NOIEN			0x01
#define COM_HW_FIFO			0x02
#define COM_HW_SIR			0x20
#define COM_HW_CONSOLE			0x40
	uint8_t			 sc_cua;
	uint16_t 		*sc_ibuf, *sc_ibufp, *sc_ibufhigh, *sc_ibufend;
#define MVUART_IBUFSIZE			 128
#define MVUART_IHIGHWATER		 100
	uint16_t		 sc_ibufs[2][MVUART_IBUFSIZE];
};

int	 mvuart_match(struct device *, void *, void *);
void	 mvuart_attach(struct device *, struct device *, void *);

void	 mvuartcnprobe(struct consdev *cp);
void	 mvuartcninit(struct consdev *cp);
int	 mvuartcnattach(bus_space_tag_t, bus_addr_t, int, tcflag_t);
int	 mvuartcngetc(dev_t dev);
void	 mvuartcnputc(dev_t dev, int c);
void	 mvuartcnpollc(dev_t dev, int on);
int	 mvuart_param(struct tty *, struct termios *);
void	 mvuart_start(struct tty *);
void	 mvuart_softint(void *arg);

struct mvuart_softc *mvuart_sc(dev_t dev);

int	 mvuart_intr(void *);
int	 mvuart_intr_rx(struct mvuart_softc *);
int	 mvuart_intr_tx(struct mvuart_softc *);

/* XXX - we imitate 'com' serial ports and take over their entry points */
/* XXX: These belong elsewhere */
cdev_decl(com);
cdev_decl(mvuart);

struct cfdriver mvuart_cd = {
	NULL, "mvuart", DV_TTY
};

const struct cfattach mvuart_ca = {
	sizeof(struct mvuart_softc), mvuart_match, mvuart_attach
};

bus_space_tag_t		mvuartconsiot;
bus_space_handle_t	mvuartconsioh;
bus_addr_t		mvuartconsaddr;

struct cdevsw mvuartdev =
	cdev_tty_init(3/*XXX NMVUART */, mvuart);		/* 12: serial port */

void
mvuart_init_cons(void)
{
	struct fdt_reg reg;
	void *node;

	if ((node = fdt_find_cons("marvell,armada-3700-uart")) == NULL)
		return;

	if (fdt_get_reg(node, 0, &reg))
		return;

	mvuartcnattach(fdt_cons_bs_tag, reg.addr, B115200, TTYDEF_CFLAG);
}

int
mvuart_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "marvell,armada-3700-uart");
}

void
mvuart_attach(struct device *parent, struct device *self, void *aux)
{
	struct mvuart_softc *sc = (struct mvuart_softc *)self;
	struct fdt_attach_args *faa = aux;
	int maj;

	if (faa->fa_nreg < 1)
		return;

	pinctrl_byname(faa->fa_node, "default");

	sc->sc_node = faa->fa_node;
	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		panic("%s: bus_space_map failed", sc->sc_dev.dv_xname);
		return;
	}

	if (faa->fa_reg[0].addr == mvuartconsaddr) {
		/* Locate the major number. */
		for (maj = 0; maj < nchrdev; maj++)
			if (cdevsw[maj].d_open == mvuartopen)
				break;
		cn_tab->cn_dev = makedev(maj, sc->sc_dev.dv_unit);

		printf(": console");
	}

	printf("\n");

	sc->sc_ih = fdt_intr_establish_idx(faa->fa_node, 0, IPL_TTY,
	    mvuart_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL)
		panic("%s: can't establish hard interrupt",
		    sc->sc_dev.dv_xname);

	sc->sc_si = softintr_establish(IPL_TTY, mvuart_softint, sc);
	if (sc->sc_si == NULL)
		panic("%s: can't establish soft interrupt",
		    sc->sc_dev.dv_xname);
}

int
mvuart_intr(void *arg)
{
	struct mvuart_softc *sc = arg;
	uint32_t stat;
	int ret = 0;

	if (sc->sc_tty == NULL)
		return 0;

	stat = HREAD4(sc, MVUART_STAT);

	if ((stat & MVUART_STAT_STD_RX_RDY) != 0)
		ret |= mvuart_intr_rx(sc);

	if ((stat & MVUART_STAT_STD_TX_RDY) != 0)
		ret |= mvuart_intr_tx(sc);

	return ret;
}

int
mvuart_intr_rx(struct mvuart_softc *sc)
{
	uint32_t stat;
	uint16_t *p, c;

	p = sc->sc_ibufp;

	stat = HREAD4(sc, MVUART_STAT);
	while ((stat & MVUART_STAT_STD_RX_RDY) != 0) {
		c = HREAD4(sc, MVUART_RBR);
		c |= (stat & MVUART_STAT_STD_ERROR_MASK) << 8;
		if (p >= sc->sc_ibufend) {
			sc->sc_floods++;
		} else {
			*p++ = c;
		}
		stat = HREAD4(sc, MVUART_STAT);
	}
	sc->sc_ibufp = p;

	softintr_schedule(sc->sc_si);
	return 1;
}

int
mvuart_intr_tx(struct mvuart_softc *sc)
{
	struct tty *tp = sc->sc_tty;

	HCLR4(sc, MVUART_CTRL, MVUART_CTRL_TX_RDY_INT);
	if (ISSET(tp->t_state, TS_BUSY)) {
		CLR(tp->t_state, TS_BUSY | TS_FLUSH);
		if (sc->sc_halt > 0)
			wakeup(&tp->t_outq);
		(*linesw[tp->t_line].l_start)(tp);
	}

	return 1;
}

int
mvuart_param(struct tty *tp, struct termios *t)
{
	struct mvuart_softc *sc = mvuart_sc(tp->t_dev);
	int error, ospeed = t->c_ospeed;
	tcflag_t oldcflag;

	if (t->c_ospeed < 0 || (t->c_ispeed && t->c_ispeed != t->c_ospeed))
		return EINVAL;

	switch (ISSET(t->c_cflag, CSIZE)) {
	case CS5:
	case CS6:
	case CS7:
		return EINVAL;
	case CS8:
		break;
	}

	if (ospeed != 0) {
		while (ISSET(tp->t_state, TS_BUSY)) {
			++sc->sc_halt;
			error = ttysleep(tp, &tp->t_outq,
			    TTOPRI | PCATCH, "mvuartprm");
			--sc->sc_halt;
			if (error) {
				mvuart_start(tp);
				return (error);
			}
		}
	}

	/* and copy to tty */
	tp->t_ispeed = t->c_ispeed;
	tp->t_ospeed = t->c_ospeed;
	oldcflag = tp->t_cflag;
	tp->t_cflag = t->c_cflag;

	mvuart_start(tp);
	return 0;
}

void
mvuart_start(struct tty *tp)
{
	struct mvuart_softc *sc = mvuart_sc(tp->t_dev);
	uint8_t buf;
	int i, n, s;

	s = spltty();
	if (ISSET(tp->t_state, TS_BUSY)) {
		splx(s);
		return;
	}
	if (ISSET(tp->t_state, TS_TIMEOUT | TS_TTSTOP))
		goto out;
	if (tp->t_outq.c_cc <= tp->t_lowat) {
		if (ISSET(tp->t_state, TS_ASLEEP)) {
			CLR(tp->t_state, TS_ASLEEP);
			wakeup(&tp->t_outq);
		}
		if (tp->t_outq.c_cc == 0)
			goto out;
		selwakeup(&tp->t_wsel);
	}
	SET(tp->t_state, TS_BUSY);

	for (i = 0; i < 32; i++) {
		n = q_to_b(&tp->t_outq, &buf, 1);
		if (n < 1)
			break;
		HWRITE4(sc, MVUART_TSH, buf);
		if (HREAD4(sc, MVUART_STAT) & MVUART_STAT_STD_TX_FIFO_FULL)
			break;
	}
	HSET4(sc, MVUART_CTRL, MVUART_CTRL_TX_RDY_INT);

out:
	splx(s);
}

void
mvuart_softint(void *arg)
{
	struct mvuart_softc *sc = arg;
	struct tty *tp;
	uint16_t *ibufp;
	uint16_t *ibufend;
	int c, err, s;

	if (sc == NULL || sc->sc_ibufp == sc->sc_ibuf)
		return;

	tp = sc->sc_tty;
	s = spltty();

	ibufp = sc->sc_ibuf;
	ibufend = sc->sc_ibufp;

	if (ibufp == ibufend || tp == NULL || !ISSET(tp->t_state, TS_ISOPEN)) {
		splx(s);
		return;
	}

	sc->sc_ibufp = sc->sc_ibuf = (ibufp == sc->sc_ibufs[0]) ?
	    sc->sc_ibufs[1] : sc->sc_ibufs[0];
	sc->sc_ibufhigh = sc->sc_ibuf + MVUART_IHIGHWATER;
	sc->sc_ibufend = sc->sc_ibuf + MVUART_IBUFSIZE;

	splx(s);

	while (ibufp < ibufend) {
		err = 0;
		c = *ibufp++;
		if (ISSET(c, (MVUART_STAT_STD_PAR_ERR << 8)))
			err |= TTY_PE;
		if (ISSET(c, (MVUART_STAT_STD_FRM_ERR << 8)))
			err |= TTY_FE;
		c = (c & 0xff) | err;
		(*linesw[tp->t_line].l_rint)(c, tp);
	}
}

int
mvuartopen(dev_t dev, int flag, int mode, struct proc *p)
{
	struct mvuart_softc *sc;
	struct tty *tp;
	int s, error = 0;

	sc = mvuart_sc(dev);
	if (sc == NULL)
		return ENXIO;

	s = spltty();
	if (sc->sc_tty == NULL)
		tp = sc->sc_tty = ttymalloc(0);
	else
		tp = sc->sc_tty;

	splx(s);

	tp->t_oproc = mvuart_start;
	tp->t_param = mvuart_param;
	tp->t_dev = dev;

	if (!ISSET(tp->t_state, TS_ISOPEN)) {
		SET(tp->t_state, TS_WOPEN);
		ttychars(tp);
		tp->t_iflag = TTYDEF_IFLAG;
		tp->t_oflag = TTYDEF_OFLAG;

		tp->t_cflag = TTYDEF_CFLAG;
		tp->t_lflag = TTYDEF_LFLAG;
		tp->t_ispeed = tp->t_ospeed = B115200;

		s = spltty();

		mvuart_param(tp, &tp->t_termios);
		ttsetwater(tp);
		sc->sc_ibufp = sc->sc_ibuf = sc->sc_ibufs[0];
		sc->sc_ibufhigh = sc->sc_ibuf + MVUART_IHIGHWATER;
		sc->sc_ibufend = sc->sc_ibuf + MVUART_IBUFSIZE;

		/* Enable interrupts */
		HSET4(sc, MVUART_CTRL, MVUART_CTRL_RX_RDY_INT);

		SET(tp->t_state, TS_CARR_ON); /* XXX */
	} else if (ISSET(tp->t_state, TS_XCLUDE) && suser(p) != 0)
		return EBUSY;
	else
		s = spltty();

	if (DEVCUA(dev)) {
		if (ISSET(tp->t_state, TS_ISOPEN)) {
			splx(s);
			return EBUSY;
		}
		sc->sc_cua = 1;
	} else {
		/* tty (not cua) device; wait for carrier if necessary */
		if (ISSET(flag, O_NONBLOCK)) {
			if (sc->sc_cua) {
				/* Opening TTY non-blocking... but the CUA is busy */
				splx(s);
				return EBUSY;
			}
		} else {
			while (sc->sc_cua ||
			    (!ISSET(tp->t_cflag, CLOCAL) &&
				!ISSET(tp->t_state, TS_CARR_ON))) {
				SET(tp->t_state, TS_WOPEN);
				error = ttysleep(tp, &tp->t_rawq,
				    TTIPRI | PCATCH, ttopen);
				/*
				 * If TS_WOPEN has been reset, that means the
				 * cua device has been closed.  We don't want
				 * to fail in that case,
				 * so just go around again.
				 */
				if (error && ISSET(tp->t_state, TS_WOPEN)) {
					CLR(tp->t_state, TS_WOPEN);
					splx(s);
					return error;
				}
			}
		}
	}
	splx(s);
	return (*linesw[tp->t_line].l_open)(dev,tp,p);
}

int
mvuartclose(dev_t dev, int flag, int mode, struct proc *p)
{
	struct mvuart_softc *sc = mvuart_sc(dev);
	struct tty *tp = sc->sc_tty;
	int s;

	if (!ISSET(tp->t_state, TS_ISOPEN))
		return 0;

	(*linesw[tp->t_line].l_close)(tp, flag, p);
	s = spltty();
	if (!ISSET(tp->t_state, TS_WOPEN)) {
		/* Disable interrupts */
		HCLR4(sc, MVUART_CTRL, MVUART_CTRL_RX_RDY_INT |
		    MVUART_CTRL_TX_RDY_INT);
	}
	CLR(tp->t_state, TS_BUSY | TS_FLUSH);
	sc->sc_cua = 0;
	splx(s);
	ttyclose(tp);

	return 0;
}

int
mvuartread(dev_t dev, struct uio *uio, int flag)
{
	struct tty *tty;

	tty = mvuarttty(dev);
	if (tty == NULL)
		return ENODEV;

	return((*linesw[tty->t_line].l_read)(tty, uio, flag));
}

int
mvuartwrite(dev_t dev, struct uio *uio, int flag)
{
	struct tty *tty;

	tty = mvuarttty(dev);
	if (tty == NULL)
		return ENODEV;

	return((*linesw[tty->t_line].l_write)(tty, uio, flag));
}

int
mvuartioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct mvuart_softc *sc;
	struct tty *tp;
	int error;

	sc = mvuart_sc(dev);
	if (sc == NULL)
		return (ENODEV);

	tp = sc->sc_tty;
	if (tp == NULL)
		return (ENXIO);

	error = (*linesw[tp->t_line].l_ioctl)(tp, cmd, data, flag, p);
	if (error >= 0)
		return (error);

	error = ttioctl(tp, cmd, data, flag, p);
	if (error >= 0)
		return (error);

	switch(cmd) {
	case TIOCSBRK:
	case TIOCCBRK:
	case TIOCSDTR:
	case TIOCCDTR:
	case TIOCMSET:
	case TIOCMBIS:
	case TIOCMBIC:
	case TIOCMGET:
	case TIOCGFLAGS:
		break;
	case TIOCSFLAGS:
		error = suser(p);
		if (error != 0)
			return(EPERM);
		break;
	default:
		return (ENOTTY);
	}

	return 0;
}

int
mvuartstop(struct tty *tp, int flag)
{
	return 0;
}

struct tty *
mvuarttty(dev_t dev)
{
	struct mvuart_softc *sc;
	sc = mvuart_sc(dev);
	if (sc == NULL)
		return NULL;
	return sc->sc_tty;
}

struct mvuart_softc *
mvuart_sc(dev_t dev)
{
	int unit;
	unit = DEVUNIT(dev);
	if (unit >= mvuart_cd.cd_ndevs)
		return NULL;
	return (struct mvuart_softc *)mvuart_cd.cd_devs[unit];
}

/* serial console */
void
mvuartcnprobe(struct consdev *cp)
{
}

void
mvuartcninit(struct consdev *cp)
{
}

int
mvuartcnattach(bus_space_tag_t iot, bus_addr_t iobase, int rate, tcflag_t cflag)
{
	static struct consdev mvuartcons = {
		NULL, NULL, mvuartcngetc, mvuartcnputc, mvuartcnpollc, NULL,
		NODEV, CN_MIDPRI
	};
	int maj;

	if (bus_space_map(iot, iobase, 0x200, 0, &mvuartconsioh))
		return ENOMEM;

	/* Look for major of com(4) to replace. */
	for (maj = 0; maj < nchrdev; maj++)
		if (cdevsw[maj].d_open == comopen)
			break;
	if (maj == nchrdev)
		return ENXIO;

	cn_tab = &mvuartcons;
	cn_tab->cn_dev = makedev(maj, 0);
	cdevsw[maj] = mvuartdev; 	/* KLUDGE */

	mvuartconsiot = iot;
	mvuartconsaddr = iobase;

	return 0;
}

int
mvuartcngetc(dev_t dev)
{
	int c;
	int s;
	s = splhigh();
	while ((bus_space_read_4(mvuartconsiot, mvuartconsioh, MVUART_STAT) &
	    MVUART_STAT_STD_RX_RDY) == 0)
		;
	c = bus_space_read_4(mvuartconsiot, mvuartconsioh, MVUART_RBR);
	splx(s);
	return c;
}

void
mvuartcnputc(dev_t dev, int c)
{
	int s;
	s = splhigh();
	while ((bus_space_read_4(mvuartconsiot, mvuartconsioh, MVUART_STAT) &
	    MVUART_STAT_STD_TX_FIFO_FULL) != 0)
		;
	bus_space_write_4(mvuartconsiot, mvuartconsioh, MVUART_TSH, (uint8_t)c);
	while ((bus_space_read_4(mvuartconsiot, mvuartconsioh, MVUART_STAT) &
	    MVUART_STAT_STD_TX_FIFO_EMPTY) != 0)
		;
	splx(s);
}

void
mvuartcnpollc(dev_t dev, int on)
{
}
