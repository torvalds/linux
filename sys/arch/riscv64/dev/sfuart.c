/*	$OpenBSD: sfuart.c,v 1.6 2022/07/12 17:14:12 jca Exp $	*/
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
#include <dev/ofw/ofw_clock.h>

#define UART_TXDATA			0x0000
#define  UART_TXDATA_FULL		(1U << 31)
#define UART_RXDATA			0x0004
#define  UART_RXDATA_EMPTY		(1U << 31)
#define UART_TXCTRL			0x0008
#define  UART_TXCTRL_TXEN		(1 << 0)
#define  UART_TXCTRL_NSTOP		(1 << 1)
#define  UART_TXCTRL_TXCNT_SHIFT	16
#define  UART_TXCTRL_TXCNT_MASK		(7 << 16)
#define UART_RXCTRL			0x000c
#define  UART_RXCTRL_RXEN		(1 << 0)
#define  UART_RXCTRL_RXCNT_SHIFT	16
#define  UART_RXCTRL_RXCNT_MASK		(7 << 16)
#define UART_IE				0x0010
#define  UART_IE_TXWM			(1 << 0)
#define  UART_IE_RXWM			(1 << 1)
#define UART_IP				0x0014
#define  UART_IP_TXWM			(1 << 0)
#define  UART_IP_RXWM			(1 << 1)
#define UART_DIV			0x0018

#define UART_SPACE			28
#define UART_FIFO_SIZE			8

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

cdev_decl(com);
cdev_decl(sfuart);

#define DEVUNIT(x)	(minor(x) & 0x7f)
#define DEVCUA(x)	(minor(x) & 0x80)

struct cdevsw sfuartdev = cdev_tty_init(2, sfuart);

struct sfuart_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	uint32_t		sc_frequency;

	struct soft_intrhand	*sc_si;
	void			*sc_ih;

	struct tty		*sc_tty;
	int			sc_conspeed;
	int			sc_floods;
	int			sc_overflows;
	int			sc_halt;
	int			sc_cua;
	int	 		*sc_ibuf, *sc_ibufp, *sc_ibufhigh, *sc_ibufend;
#define SFUART_IBUFSIZE		128
#define SFUART_IHIGHWATER	100
	int			sc_ibufs[2][SFUART_IBUFSIZE];
};

int	sfuart_match(struct device *, void *, void *);
void	sfuart_attach(struct device *, struct device *, void *);

struct cfdriver sfuart_cd = {
	NULL, "sfuart", DV_TTY
};

const struct cfattach sfuart_ca = {
	sizeof(struct sfuart_softc), sfuart_match, sfuart_attach
};

bus_space_tag_t	sfuartconsiot;
bus_space_handle_t sfuartconsioh;

struct sfuart_softc *sfuart_sc(dev_t);

int	sfuart_intr(void *);
void	sfuart_softintr(void *);
void	sfuart_start(struct tty *);

int	sfuartcnattach(bus_space_tag_t, bus_addr_t);
int	sfuartcngetc(dev_t);
void	sfuartcnputc(dev_t, int);
void	sfuartcnpollc(dev_t, int);

void
sfuart_init_cons(void)
{
	struct fdt_reg reg;
	void *node;

	if ((node = fdt_find_cons("sifive,uart0")) == NULL)
		return;
	if (fdt_get_reg(node, 0, &reg))
		return;

	sfuartcnattach(fdt_cons_bs_tag, reg.addr);
}

int
sfuart_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "sifive,uart0");
}

void
sfuart_attach(struct device *parent, struct device *self, void *aux)
{
	struct sfuart_softc *sc = (struct sfuart_softc *)self;
	struct fdt_attach_args *faa = aux;
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

	sc->sc_frequency = clock_get_frequency(faa->fa_node, NULL);
	if (faa->fa_node == stdout_node) {
		/* Locate the major number. */
		for (maj = 0; maj < nchrdev; maj++)
			if (cdevsw[maj].d_open == sfuartopen)
				break;
		cn_tab->cn_dev = makedev(maj, sc->sc_dev.dv_unit);
		sc->sc_conspeed = stdout_speed;
		printf(": console");
	}

	sc->sc_si = softintr_establish(IPL_TTY, sfuart_softintr, sc);
	if (sc->sc_si == NULL) {
		printf(": can't establish soft interrupt\n");
		return;
	}

	sc->sc_ih = fdt_intr_establish_idx(faa->fa_node, 0, IPL_TTY,
	    sfuart_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish hard interrupt\n");
		return;
	}

	printf("\n");
}

int
sfuart_intr(void *arg)
{
	struct sfuart_softc *sc = arg;
	struct tty *tp = sc->sc_tty;
	int *p;
	uint32_t val;
	int c, handled = 0;

	if (tp == NULL)
		return 0;

	if (!ISSET(HREAD4(sc, UART_TXDATA), UART_TXDATA_FULL) &&
	    ISSET(tp->t_state, TS_BUSY)) {
		CLR(tp->t_state, TS_BUSY | TS_FLUSH);
		if (sc->sc_halt > 0)
			wakeup(&tp->t_outq);
		(*linesw[tp->t_line].l_start)(tp);
		handled = 1;
	}

	p = sc->sc_ibufp;
	val = HREAD4(sc, UART_RXDATA);
	while (!ISSET(val, UART_RXDATA_EMPTY)) {
		c = val & 0xff;

		if (p >= sc->sc_ibufend)
			sc->sc_floods++;
		else
			*p++ = c;

		val = HREAD4(sc, UART_RXDATA);
		handled = 1;
	}
	if (sc->sc_ibufp != p) {
		sc->sc_ibufp = p;
		softintr_schedule(sc->sc_si);
	}

	return handled;
}

void
sfuart_softintr(void *arg)
{
	struct sfuart_softc *sc = arg;
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
	sc->sc_ibufhigh = sc->sc_ibuf + SFUART_IHIGHWATER;
	sc->sc_ibufend = sc->sc_ibuf + SFUART_IBUFSIZE;

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
sfuart_param(struct tty *tp, struct termios *t)
{
	struct sfuart_softc *sc = sfuart_sc(tp->t_dev);
	int ospeed = t->c_ospeed;
	uint32_t div;

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
			    TTOPRI | PCATCH, "sfuprm");
			sc->sc_halt--;
			if (error) {
				sfuart_start(tp);
				return error;
			}
		}

		div = (sc->sc_frequency + ospeed / 2) / ospeed;
		if (div < 16 || div > 65536)
			return EINVAL;
		HWRITE4(sc, UART_DIV, div - 1);
	}

	tp->t_ispeed = t->c_ispeed;
	tp->t_ospeed = t->c_ospeed;
	tp->t_cflag = t->c_cflag;

	/* Just to be sure... */
	sfuart_start(tp);
	return 0;
}

void
sfuart_start(struct tty *tp)
{
	struct sfuart_softc *sc = sfuart_sc(tp->t_dev);
	int stat;
	int s;

	s = spltty();
	if (ISSET(tp->t_state, TS_BUSY))
		goto out;
	if (ISSET(tp->t_state, TS_TIMEOUT | TS_TTSTOP) || sc->sc_halt > 0)
		goto out;
	ttwakeupwr(tp);
	if (tp->t_outq.c_cc == 0) {
		HCLR4(sc, UART_IE, UART_IE_TXWM);
		goto out;
	}
	SET(tp->t_state, TS_BUSY);

	stat = HREAD4(sc, UART_TXDATA);
	while ((stat & UART_TXDATA_FULL) == 0 && tp->t_outq.c_cc != 0) {
		HWRITE4(sc, UART_TXDATA, getc(&tp->t_outq));
		stat = HREAD4(sc, UART_TXDATA);
	}
	HSET4(sc, UART_IE, UART_IE_TXWM);
out:
	splx(s);
}

int
sfuartopen(dev_t dev, int flag, int mode, struct proc *p)
{
	struct sfuart_softc *sc = sfuart_sc(dev);
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

	tp->t_oproc = sfuart_start;
	tp->t_param = sfuart_param;
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

		sfuart_param(tp, &tp->t_termios);
		ttsetwater(tp);

		sc->sc_ibufp = sc->sc_ibuf = sc->sc_ibufs[0];
		sc->sc_ibufhigh = sc->sc_ibuf + SFUART_IHIGHWATER;
		sc->sc_ibufend = sc->sc_ibuf + SFUART_IBUFSIZE;

		/* Enable transmit. */
		HWRITE4(sc, UART_TXCTRL, UART_TXCTRL_TXEN |
		    ((UART_FIFO_SIZE / 2) << UART_TXCTRL_TXCNT_SHIFT));

		/* Enable receive. */
		HWRITE4(sc, UART_RXCTRL, UART_RXCTRL_RXEN |
		    (0 << UART_RXCTRL_RXCNT_SHIFT));

		/* Enable interrupts. */
		HSET4(sc, UART_IE, UART_IE_RXWM);

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
sfuartclose(dev_t dev, int flag, int mode, struct proc *p)
{
	struct sfuart_softc *sc = sfuart_sc(dev);
	struct tty *tp = sc->sc_tty;
	int s;

	if (!ISSET(tp->t_state, TS_ISOPEN))
		return 0;

	(*linesw[tp->t_line].l_close)(tp, flag, p);
	s = spltty();
	if (!ISSET(tp->t_state, TS_WOPEN)) {
		/* Disable interrupts */
		HCLR4(sc, UART_IE, UART_IE_TXWM | UART_IE_RXWM);
	}
	CLR(tp->t_state, TS_BUSY | TS_FLUSH);
	sc->sc_cua = 0;
	splx(s);
	ttyclose(tp);

	return 0;
}

int
sfuartread(dev_t dev, struct uio *uio, int flag)
{
	struct tty *tp = sfuarttty(dev);

	if (tp == NULL)
		return ENODEV;

	return (*linesw[tp->t_line].l_read)(tp, uio, flag);
}

int
sfuartwrite(dev_t dev, struct uio *uio, int flag)
{
	struct tty *tp = sfuarttty(dev);

	if (tp == NULL)
		return ENODEV;

	return (*linesw[tp->t_line].l_write)(tp, uio, flag);
}

int
sfuartioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct sfuart_softc *sc = sfuart_sc(dev);
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
sfuartstop(struct tty *tp, int flag)
{
	return 0;
}

struct tty *
sfuarttty(dev_t dev)
{
	struct sfuart_softc *sc = sfuart_sc(dev);

	if (sc == NULL)
		return NULL;
	return sc->sc_tty;
}

struct sfuart_softc *
sfuart_sc(dev_t dev)
{
	int unit = DEVUNIT(dev);

	if (unit >= sfuart_cd.cd_ndevs)
		return NULL;
	return (struct sfuart_softc *)sfuart_cd.cd_devs[unit];
}

int
sfuartcnattach(bus_space_tag_t iot, bus_addr_t iobase)
{
	static struct consdev sfuartcons = {
		NULL, NULL, sfuartcngetc, sfuartcnputc, sfuartcnpollc, NULL,
		NODEV, CN_MIDPRI
	};
	int maj;

	sfuartconsiot = iot;
	if (bus_space_map(iot, iobase, UART_SPACE, 0, &sfuartconsioh))
		return ENOMEM;

	/* Look for major of com(4) to replace. */
	for (maj = 0; maj < nchrdev; maj++)
		if (cdevsw[maj].d_open == comopen)
			break;
	if (maj == nchrdev)
		return ENXIO;

	cn_tab = &sfuartcons;
	cn_tab->cn_dev = makedev(maj, 0);
	cdevsw[maj] = sfuartdev; 	/* KLUDGE */

	return 0;
}

int
sfuartcngetc(dev_t dev)
{
	uint32_t val;

	do {
		val = bus_space_read_4(sfuartconsiot, sfuartconsioh, UART_RXDATA);
		if (val & UART_RXDATA_EMPTY)
			CPU_BUSY_CYCLE();
	} while ((val & UART_RXDATA_EMPTY));

	return (val & 0xff);
}

void
sfuartcnputc(dev_t dev, int c)
{
	while (bus_space_read_4(sfuartconsiot, sfuartconsioh, UART_TXDATA) &
	    UART_TXDATA_FULL)
		CPU_BUSY_CYCLE();
	bus_space_write_4(sfuartconsiot, sfuartconsioh, UART_TXDATA, c);
}

void
sfuartcnpollc(dev_t dev, int on)
{
}
