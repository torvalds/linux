/*	$OpenBSD: amluart.c,v 1.4 2022/07/15 17:14:49 kettenis Exp $	*/
/*
 * Copyright (c) 2019 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/tty.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/cons.h>

#include <dev/ofw/fdt.h>
#include <dev/ofw/openfirm.h>

#define UART_WFIFO			0x0000
#define UART_RFIFO			0x0004
#define UART_CONTROL			0x0008
#define  UART_CONTROL_TX_INT		(1 << 28)
#define  UART_CONTROL_RX_INT		(1 << 27)
#define  UART_CONTROL_CLEAR_ERROR	(1 << 24)
#define UART_STATUS			0x000c
#define  UART_STATUS_RX_FIFO_OVERFLOW	(1 << 24)
#define  UART_STATUS_TX_FIFO_FULL	(1 << 21)
#define  UART_STATUS_RX_FIFO_EMPTY	(1 << 20)
#define  UART_STATUS_FRAME_ERROR	(1 << 17)
#define  UART_STATUS_PARITY_ERROR	(1 << 16)
#define  UART_STATUS_ERROR 		(1 << 24 | 0x7 << 16)
#define UART_MISC			0x0010
#define  UART_MISC_TX_INT_CNT_MASK	(0xff << 16)
#define  UART_MISC_TX_INT_CNT_SHIFT	16
#define  UART_MISC_RX_INT_CNT_MASK	(0xff << 0)
#define  UART_MISC_RX_INT_CNT_SHIFT	0

#define UART_SPACE			24

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

cdev_decl(com);
cdev_decl(amluart);

#define DEVUNIT(x)	(minor(x) & 0x7f)
#define DEVCUA(x)	(minor(x) & 0x80)

struct cdevsw amluartdev = cdev_tty_init(3, amluart);

struct amluart_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct soft_intrhand	*sc_si;
	void			*sc_ih;

	struct tty		*sc_tty;
	int			sc_conspeed;
	int			sc_floods;
	int			sc_overflows;
	int			sc_halt;
	int			sc_cua;
	int	 		*sc_ibuf, *sc_ibufp, *sc_ibufhigh, *sc_ibufend;
#define AMLUART_IBUFSIZE	128
#define AMLUART_IHIGHWATER	100
	int			sc_ibufs[2][AMLUART_IBUFSIZE];
};

int	amluart_match(struct device *, void *, void *);
void	amluart_attach(struct device *, struct device *, void *);

struct cfdriver amluart_cd = {
	NULL, "amluart", DV_TTY
};

const struct cfattach amluart_ca = {
	sizeof(struct amluart_softc), amluart_match, amluart_attach
};

bus_space_tag_t	amluartconsiot;
bus_space_handle_t amluartconsioh;

struct amluart_softc *amluart_sc(dev_t);

int	amluart_intr(void *);
void	amluart_softintr(void *);
void	amluart_start(struct tty *);

int	amluartcnattach(bus_space_tag_t, bus_addr_t);
int	amluartcngetc(dev_t);
void	amluartcnputc(dev_t, int);
void	amluartcnpollc(dev_t, int);

void
amluart_init_cons(void)
{
	struct fdt_reg reg;
	void *node;

	if ((node = fdt_find_cons("amlogic,meson-gx-uart")) == NULL)
		return;
	if (fdt_get_reg(node, 0, &reg))
		return;

	amluartcnattach(fdt_cons_bs_tag, reg.addr);
}

int
amluart_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "amlogic,meson-gx-uart");
}

void
amluart_attach(struct device *parent, struct device *self, void *aux)
{
	struct amluart_softc *sc = (struct amluart_softc *)self;
	struct fdt_attach_args *faa = aux;
	uint32_t reg;
	int maj;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	if (faa->fa_node == stdout_node) {
		/* Locate the major number. */
		for (maj = 0; maj < nchrdev; maj++)
			if (cdevsw[maj].d_open == amluartopen)
				break;
		cn_tab->cn_dev = makedev(maj, sc->sc_dev.dv_unit);
		sc->sc_conspeed = stdout_speed;
		printf(": console");
	}

	sc->sc_si = softintr_establish(IPL_TTY, amluart_softintr, sc);
	if (sc->sc_si == NULL) {
		printf(": can't establish soft interrupt\n");
		return;
	}

	sc->sc_ih = fdt_intr_establish_idx(faa->fa_node, 0, IPL_TTY,
	    amluart_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish hard interrupt\n");
		return;
	}

	printf("\n");

	/*
	 * Generate interrupts if the Tx FIFO is half-empty or if
	 * there is anything in the Rx FIFO.
	 */
	reg = HREAD4(sc, UART_MISC);
	reg &= ~UART_MISC_TX_INT_CNT_MASK;
	reg |= (32 << UART_MISC_TX_INT_CNT_SHIFT);
	reg &= ~UART_MISC_RX_INT_CNT_MASK;
	reg |= (1 << UART_MISC_RX_INT_CNT_SHIFT);
	HWRITE4(sc, UART_MISC, reg);
}

int
amluart_intr(void *arg)
{
	struct amluart_softc *sc = arg;
	struct tty *tp = sc->sc_tty;
	int *p;
	u_int32_t stat;
	u_char c;
	int handled = 0;

	if (tp == NULL)
		return 0;

	stat = HREAD4(sc, UART_STATUS);
	if (!ISSET(stat, UART_STATUS_TX_FIFO_FULL) &&
	    ISSET(tp->t_state, TS_BUSY)) {
		CLR(tp->t_state, TS_BUSY | TS_FLUSH);
		if (sc->sc_halt > 0)
			wakeup(&tp->t_outq);
		(*linesw[tp->t_line].l_start)(tp);
		handled = 1;
	}

	p = sc->sc_ibufp;
	while (!ISSET(stat, UART_STATUS_RX_FIFO_EMPTY)) {
		c = HREAD4(sc, UART_RFIFO);
		if (ISSET(stat, UART_STATUS_FRAME_ERROR))
			c |= TTY_FE;
		if (ISSET(stat, UART_STATUS_PARITY_ERROR))
			c |= TTY_PE;
		if (ISSET(stat, UART_STATUS_RX_FIFO_OVERFLOW))
			sc->sc_overflows++;

		if (p >= sc->sc_ibufend)
			sc->sc_floods++;
		else
			*p++ = c;

		if (stat & UART_STATUS_ERROR)
			HSET4(sc, UART_CONTROL, UART_CONTROL_CLEAR_ERROR);
		stat = HREAD4(sc, UART_STATUS);
		handled = 1;
	}
	if (sc->sc_ibufp != p) {
		sc->sc_ibufp = p;
		softintr_schedule(sc->sc_si);
	}

	return handled;
}

void
amluart_softintr(void *arg)
{
	struct amluart_softc *sc = arg;
	struct tty *tp = sc->sc_tty;
	int *ibufp, *ibufend;
	int s;

	if (sc->sc_ibufp == sc->sc_ibuf)
		return;

	s = spltty();

	ibufp = sc->sc_ibuf;
	ibufend = sc->sc_ibufp;

	if (ibufp == ibufend) {
		splx(s);
		return;
	}

	sc->sc_ibufp = sc->sc_ibuf = (ibufp == sc->sc_ibufs[0]) ?
	    sc->sc_ibufs[1] : sc->sc_ibufs[0];
	sc->sc_ibufhigh = sc->sc_ibuf + AMLUART_IHIGHWATER;
	sc->sc_ibufend = sc->sc_ibuf + AMLUART_IBUFSIZE;

	if (tp == NULL || !ISSET(tp->t_state, TS_ISOPEN)) {
		splx(s);
		return;
	}

	splx(s);

	while (ibufp < ibufend) {
		int i = *ibufp++;
#ifdef DDB
		if (tp->t_dev == cn_tab->cn_dev) {
			int j = db_rint(i);

			if (j == 1)	/* Escape received, skip */
				continue;
			if (j == 2)	/* Second char wasn't 'D' */
				(*linesw[tp->t_line].l_rint)(27, tp);
		}
#endif
		(*linesw[tp->t_line].l_rint)(i, tp);
	}
}

int
amluart_param(struct tty *tp, struct termios *t)
{
	struct amluart_softc *sc = amluart_sc(tp->t_dev);
	int ospeed = t->c_ospeed;

	/* Check requested parameters. */
	if (ospeed < 0 || (t->c_ispeed && t->c_ispeed != t->c_ospeed))
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
			int error;

			sc->sc_halt++;
			error = ttysleep(tp, &tp->t_outq,
			    TTOPRI | PCATCH, "amluprm");
			sc->sc_halt--;
			if (error) {
				amluart_start(tp);
				return error;
			}
		}
	}

	tp->t_ispeed = t->c_ispeed;
	tp->t_ospeed = t->c_ospeed;
	tp->t_cflag = t->c_cflag;

	/* Just to be sure... */
	amluart_start(tp);
	return 0;
}

void
amluart_start(struct tty *tp)
{
	struct amluart_softc *sc = amluart_sc(tp->t_dev);
	int stat;
	int s;

	s = spltty();
	if (ISSET(tp->t_state, TS_BUSY))
		goto out;
	if (ISSET(tp->t_state, TS_TIMEOUT | TS_TTSTOP) || sc->sc_halt > 0)
		goto out;
	ttwakeupwr(tp);
	if (tp->t_outq.c_cc == 0)
		goto out;
	SET(tp->t_state, TS_BUSY);

	stat = HREAD4(sc, UART_STATUS);
	while ((stat & UART_STATUS_TX_FIFO_FULL) == 0) {
		HWRITE4(sc, UART_WFIFO, getc(&tp->t_outq));
		stat = HREAD4(sc, UART_STATUS);
	}
out:
	splx(s);
}

int
amluartopen(dev_t dev, int flag, int mode, struct proc *p)
{
	struct amluart_softc *sc = amluart_sc(dev);
	struct tty *tp;
	int error;
	int s;

	if (sc == NULL)
		return ENXIO;

	s = spltty();
	if (sc->sc_tty == NULL)
		tp = sc->sc_tty = ttymalloc(0);
	else
		tp = sc->sc_tty;
	splx(s);

	tp->t_oproc = amluart_start;
	tp->t_param = amluart_param;
	tp->t_dev = dev;

	if (!ISSET(tp->t_state, TS_ISOPEN)) {
		SET(tp->t_state, TS_WOPEN);
		ttychars(tp);
		tp->t_iflag = TTYDEF_IFLAG;
		tp->t_oflag = TTYDEF_OFLAG;
		tp->t_cflag = TTYDEF_CFLAG;
		tp->t_lflag = TTYDEF_LFLAG;
		tp->t_ispeed = tp->t_ospeed =
		    sc->sc_conspeed ? sc->sc_conspeed : B115200;
		
		s = spltty();

		amluart_param(tp, &tp->t_termios);
		ttsetwater(tp);

		sc->sc_ibufp = sc->sc_ibuf = sc->sc_ibufs[0];
		sc->sc_ibufhigh = sc->sc_ibuf + AMLUART_IHIGHWATER;
		sc->sc_ibufend = sc->sc_ibuf + AMLUART_IBUFSIZE;

		/* Enable interrupts */
		HSET4(sc, UART_CONTROL,
		    UART_CONTROL_TX_INT | UART_CONTROL_RX_INT);

		/* No carrier detect support. */
		SET(tp->t_state, TS_CARR_ON);
	} else if (ISSET(tp->t_state, TS_XCLUDE) && suser(p) != 0)
		return EBUSY;
	else
		s = spltty();

	if (DEVCUA(dev)) {
		if (ISSET(tp->t_state, TS_ISOPEN)) {
			/* Ah, but someone already is dialed in... */
			splx(s);
			return EBUSY;
		}
		sc->sc_cua = 1;		/* We go into CUA mode. */
	} else {
		if (ISSET(flag, O_NONBLOCK) && sc->sc_cua) {
			/* Opening TTY non-blocking... but the CUA is busy. */
			splx(s);
			return EBUSY;
		} else {
			while (sc->sc_cua) {
				SET(tp->t_state, TS_WOPEN);
				error = ttysleep(tp, &tp->t_rawq,
				    TTIPRI | PCATCH, ttopen);
				/*
				 * If TS_WOPEN has been reset, that means the
				 * cua device has been closed.
				 * We don't want to fail in that case,
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

	return (*linesw[tp->t_line].l_open)(dev, tp, p);
}

int
amluartclose(dev_t dev, int flag, int mode, struct proc *p)
{
	struct amluart_softc *sc = amluart_sc(dev);
	struct tty *tp = sc->sc_tty;
	int s;

	if (!ISSET(tp->t_state, TS_ISOPEN))
		return 0;

	(*linesw[tp->t_line].l_close)(tp, flag, p);
	s = spltty();
	if (!ISSET(tp->t_state, TS_WOPEN)) {
		/* Disable interrupts */
		HCLR4(sc, UART_CONTROL,
		    UART_CONTROL_TX_INT | UART_CONTROL_RX_INT);
	}
	CLR(tp->t_state, TS_BUSY | TS_FLUSH);
	sc->sc_cua = 0;
	splx(s);
	ttyclose(tp);

	return 0;
}

int
amluartread(dev_t dev, struct uio *uio, int flag)
{
	struct tty *tp = amluarttty(dev);

	if (tp == NULL)
		return ENODEV;
	
	return (*linesw[tp->t_line].l_read)(tp, uio, flag);
}

int
amluartwrite(dev_t dev, struct uio *uio, int flag)
{
	struct tty *tp = amluarttty(dev);

	if (tp == NULL)
		return ENODEV;
	
	return (*linesw[tp->t_line].l_write)(tp, uio, flag);
}

int
amluartioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct amluart_softc *sc = amluart_sc(dev);
	struct tty *tp;
	int error;

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
			return EPERM;
		break;
	default:
		return ENOTTY;
	}

	return 0;
}

int
amluartstop(struct tty *tp, int flag)
{
	return 0;
}

struct tty *
amluarttty(dev_t dev)
{
	struct amluart_softc *sc = amluart_sc(dev);

	if (sc == NULL)
		return NULL;
	return sc->sc_tty;
}

struct amluart_softc *
amluart_sc(dev_t dev)
{
	int unit = DEVUNIT(dev);

	if (unit >= amluart_cd.cd_ndevs)
		return NULL;
	return (struct amluart_softc *)amluart_cd.cd_devs[unit];
}

int
amluartcnattach(bus_space_tag_t iot, bus_addr_t iobase)
{
	static struct consdev amluartcons = {
		NULL, NULL, amluartcngetc, amluartcnputc, amluartcnpollc, NULL,
		NODEV, CN_MIDPRI
	};
	int maj;

	amluartconsiot = iot;
	if (bus_space_map(iot, iobase, UART_SPACE, 0, &amluartconsioh))
		return ENOMEM;

	/* Look for major of com(4) to replace. */
	for (maj = 0; maj < nchrdev; maj++)
		if (cdevsw[maj].d_open == comopen)
			break;
	if (maj == nchrdev)
		return ENXIO;

	cn_tab = &amluartcons;
	cn_tab->cn_dev = makedev(maj, 0);
	cdevsw[maj] = amluartdev; 	/* KLUDGE */

	return 0;
}

int
amluartcngetc(dev_t dev)
{
	uint8_t c;
	
	while (bus_space_read_4(amluartconsiot, amluartconsioh, UART_STATUS) &
	    UART_STATUS_RX_FIFO_EMPTY)
		CPU_BUSY_CYCLE();
	c = bus_space_read_4(amluartconsiot, amluartconsioh, UART_RFIFO);
	return c;
}

void
amluartcnputc(dev_t dev, int c)
{
	while (bus_space_read_4(amluartconsiot, amluartconsioh, UART_STATUS) &
	    UART_STATUS_TX_FIFO_FULL)
		CPU_BUSY_CYCLE();
	bus_space_write_4(amluartconsiot, amluartconsioh, UART_WFIFO, c);
}

void
amluartcnpollc(dev_t dev, int on)
{
}
