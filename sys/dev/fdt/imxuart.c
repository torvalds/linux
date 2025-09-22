/* $OpenBSD: imxuart.c,v 1.13 2022/07/02 08:50:42 visa Exp $ */
/*
 * Copyright (c) 2005 Dale Rahn <drahn@motorola.com>
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
#include <sys/uio.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/device.h>
#include <sys/syslog.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/cons.h>

#ifdef DDB
#include <ddb/db_var.h>
#endif

#include <dev/fdt/imxuartreg.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/fdt.h>

#define DEVUNIT(x)      (minor(x) & 0x7f)
#define DEVCUA(x)       (minor(x) & 0x80)

struct imxuart_softc {
	struct device	sc_dev;
	bus_space_tag_t sc_iot;
	bus_space_handle_t sc_ioh;
	int		sc_node;
	struct soft_intrhand *sc_si;
	void *sc_irq;
	struct tty	*sc_tty;
	struct timeout	sc_diag_tmo;
	struct timeout	sc_dtr_tmo;
	int		sc_overflows;
	int		sc_floods;
	int		sc_errors;
	int		sc_halt;
	u_int16_t	sc_ucr1;
	u_int16_t	sc_ucr2;
	u_int16_t	sc_ucr3;
	u_int16_t	sc_ucr4;
	u_int8_t	sc_hwflags;
#define COM_HW_NOIEN    0x01
#define COM_HW_FIFO     0x02
#define COM_HW_SIR      0x20
#define COM_HW_CONSOLE  0x40
	u_int8_t	sc_swflags;
#define COM_SW_SOFTCAR  0x01
#define COM_SW_CLOCAL   0x02
#define COM_SW_CRTSCTS  0x04
#define COM_SW_MDMBUF   0x08
#define COM_SW_PPS      0x10

	u_int8_t	sc_initialize;
	u_int8_t	sc_cua;
	u_int16_t 	*sc_ibuf, *sc_ibufp, *sc_ibufhigh, *sc_ibufend;
#define IMXUART_IBUFSIZE 128
#define IMXUART_IHIGHWATER 100
	u_int16_t		sc_ibufs[2][IMXUART_IBUFSIZE];
};

int	 imxuart_match(struct device *, void *, void *);
void	 imxuart_attach(struct device *, struct device *, void *);

void imxuartcnprobe(struct consdev *cp);
void imxuartcninit(struct consdev *cp);
int imxuartcnattach(bus_space_tag_t iot, bus_addr_t iobase, int rate,
    tcflag_t cflag);
int imxuartcngetc(dev_t dev);
void imxuartcnputc(dev_t dev, int c);
void imxuartcnpollc(dev_t dev, int on);
int  imxuart_param(struct tty *tp, struct termios *t);
void imxuart_start(struct tty *);
void imxuart_diag(void *arg);
void imxuart_raisedtr(void *arg);
void imxuart_softint(void *arg);
struct imxuart_softc *imxuart_sc(dev_t dev);

int imxuart_intr(void *);

/* XXX - we imitate 'com' serial ports and take over their entry points */
/* XXX: These belong elsewhere */
cdev_decl(com);
cdev_decl(imxuart);

struct cfdriver imxuart_cd = {
	NULL, "imxuart", DV_TTY
};

const struct cfattach imxuart_ca = {
	sizeof(struct imxuart_softc), imxuart_match, imxuart_attach
};

bus_space_tag_t	imxuartconsiot;
bus_space_handle_t imxuartconsioh;
bus_addr_t	imxuartconsaddr;
tcflag_t	imxuartconscflag = TTYDEF_CFLAG;
int		imxuartdefaultrate = B115200;

struct cdevsw imxuartdev =
	cdev_tty_init(3/*XXX NIMXUART */ ,imxuart);		/* 12: serial port */

void
imxuart_init_cons(void)
{
	struct fdt_reg reg;
	void *node;

	if ((node = fdt_find_cons("fsl,imx21-uart")) == NULL &&
	    (node = fdt_find_cons("fsl,imx6q-uart")) == NULL)
		return;

	if (fdt_get_reg(node, 0, &reg))
		return;

	imxuartcnattach(fdt_cons_bs_tag, reg.addr, B115200, TTYDEF_CFLAG);
}

int
imxuart_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "fsl,imx21-uart") ||
	    OF_is_compatible(faa->fa_node, "fsl,imx6q-uart"));
}

void
imxuart_attach(struct device *parent, struct device *self, void *aux)
{
	struct imxuart_softc *sc = (struct imxuart_softc *) self;
	struct fdt_attach_args *faa = aux;
	int maj;

	if (faa->fa_nreg < 1)
		return;

	pinctrl_byname(faa->fa_node, "default");

	sc->sc_irq = fdt_intr_establish(faa->fa_node, IPL_TTY,
	    imxuart_intr, sc, sc->sc_dev.dv_xname);

	sc->sc_node = faa->fa_node;
	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh))
		panic("imxuartattach: bus_space_map failed!");

	if (faa->fa_reg[0].addr == imxuartconsaddr) {
		/* Locate the major number. */
		for (maj = 0; maj < nchrdev; maj++)
			if (cdevsw[maj].d_open == imxuartopen)
				break;
		cn_tab->cn_dev = makedev(maj, sc->sc_dev.dv_unit);

		SET(sc->sc_hwflags, COM_HW_CONSOLE);
		printf(": console");
	}

	timeout_set(&sc->sc_diag_tmo, imxuart_diag, sc);
	timeout_set(&sc->sc_dtr_tmo, imxuart_raisedtr, sc);
	sc->sc_si = softintr_establish(IPL_TTY, imxuart_softint, sc);

	if(sc->sc_si == NULL)
		panic("%s: can't establish soft interrupt.",
		    sc->sc_dev.dv_xname);

	printf("\n");
}

int
imxuart_intr(void *arg)
{
	struct imxuart_softc *sc = arg;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	struct tty *tp = sc->sc_tty;
	u_int16_t sr1;
	u_int16_t *p;
	u_int16_t c;

	sr1 = bus_space_read_2(iot, ioh, IMXUART_USR1);
	if (ISSET(sr1, IMXUART_SR1_TRDY) && ISSET(tp->t_state, TS_BUSY)) {
		CLR(tp->t_state, TS_BUSY | TS_FLUSH);
		if (sc->sc_halt > 0)
			wakeup(&tp->t_outq);
		(*linesw[tp->t_line].l_start)(tp);
	}

	if (sc->sc_tty == NULL)
		return(0);

	if(!ISSET(bus_space_read_2(iot, ioh, IMXUART_USR2), IMXUART_SR2_RDR))
		return 0;

	p = sc->sc_ibufp;

	while(ISSET(bus_space_read_2(iot, ioh, IMXUART_USR2), IMXUART_SR2_RDR)) {
		c = bus_space_read_2(iot, ioh, IMXUART_URXD);
		if (ISSET(c, IMXUART_RX_BRK)) {
#ifdef DDB
			if (ISSET(sc->sc_hwflags, COM_HW_CONSOLE)) {
				if (db_console)
					db_enter();
				continue;
			}
#endif
			c &= ~0xff;
		}
		if (p >= sc->sc_ibufend) {
			sc->sc_floods++;
			if (sc->sc_errors++ == 0)
				timeout_add_sec(&sc->sc_diag_tmo, 60);
		} else {
			*p++ = c;
			if (p == sc->sc_ibufhigh &&
			    ISSET(tp->t_cflag, CRTSCTS)) {
				/* XXX */
				CLR(sc->sc_ucr3, IMXUART_CR3_DSR);
				bus_space_write_2(iot, ioh, IMXUART_UCR3,
				    sc->sc_ucr3);
			}

		}
		/* XXX - msr stuff ? */
	}
	sc->sc_ibufp = p;

	softintr_schedule(sc->sc_si);

	return 1;
}

int
imxuart_param(struct tty *tp, struct termios *t)
{
	struct imxuart_softc *sc = imxuart_cd.cd_devs[DEVUNIT(tp->t_dev)];
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	int ospeed = t->c_ospeed;
	int error;
	tcflag_t oldcflag;


	if (t->c_ospeed < 0 || (t->c_ispeed && t->c_ispeed != t->c_ospeed))
		return EINVAL;

	switch (ISSET(t->c_cflag, CSIZE)) {
	case CS5:
		return EINVAL;
	case CS6:
		return EINVAL;
	case CS7:
		CLR(sc->sc_ucr2, IMXUART_CR2_WS);
		break;
	case CS8:
		SET(sc->sc_ucr2, IMXUART_CR2_WS);
		break;
	}
//	bus_space_write_2(iot, ioh, IMXUART_UCR2, sc->sc_ucr2);

	if (ISSET(t->c_cflag, PARENB)) {
		SET(sc->sc_ucr2, IMXUART_CR2_PREN);
		bus_space_write_2(iot, ioh, IMXUART_UCR2, sc->sc_ucr2);
	}
	/* STOPB - XXX */
	if (ospeed == 0) {
		/* lower dtr */
	}

	if (ospeed != 0) {
		while (ISSET(tp->t_state, TS_BUSY)) {
			++sc->sc_halt;
			error = ttysleep(tp, &tp->t_outq,
			    TTOPRI | PCATCH, "imxuartprm");
			--sc->sc_halt;
			if (error) {
				imxuart_start(tp);
				return (error);
			}
		}
		/* set speed */
	}

	/* setup fifo */

	/* When not using CRTSCTS, RTS follows DTR. */
	/* sc->sc_dtr = MCR_DTR; */


	/* and copy to tty */
	tp->t_ispeed = t->c_ispeed;
	tp->t_ospeed = t->c_ospeed;
	oldcflag = tp->t_cflag;
	tp->t_cflag = t->c_cflag;

        /*
	 * If DCD is off and MDMBUF is changed, ask the tty layer if we should
	 * stop the device.
	 */
	 /* XXX */

	imxuart_start(tp);

	return 0;
}

void
imxuart_start(struct tty *tp)
{
        struct imxuart_softc *sc = imxuart_cd.cd_devs[DEVUNIT(tp->t_dev)];
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;

	int s;
	s = spltty();
	if (ISSET(tp->t_state, TS_BUSY)) {
		splx(s);
		return;
	}
	if (ISSET(tp->t_state, TS_TIMEOUT | TS_TTSTOP))
		goto stopped;
#ifdef DAMNFUCKSHIT
	/* clear to send (IE the RTS pin on this shit) is not directly \
	 * readable - skip check for now
	 */
	if (ISSET(tp->t_cflag, CRTSCTS) && !ISSET(sc->sc_msr, IMXUART_CTS))
		goto stopped;
#endif
	if (tp->t_outq.c_cc <= tp->t_lowat) {
		if (ISSET(tp->t_state, TS_ASLEEP)) {
			CLR(tp->t_state, TS_ASLEEP);
			wakeup(&tp->t_outq);
		}
		if (tp->t_outq.c_cc == 0)
			goto stopped;
		selwakeup(&tp->t_wsel);
	}
	SET(tp->t_state, TS_BUSY);

	if (!ISSET(sc->sc_ucr1, IMXUART_CR1_TXMPTYEN)) {
		SET(sc->sc_ucr1, IMXUART_CR1_TXMPTYEN);
		bus_space_write_2(iot, ioh, IMXUART_UCR1, sc->sc_ucr1);
	}

	{
		u_char buf[32];
		int n = q_to_b(&tp->t_outq, buf, 32/*XXX*/);
		int i;
		for (i = 0; i < n; i++)
			bus_space_write_1(iot, ioh, IMXUART_UTXD, buf[i]);
	}
	splx(s);
	return;
stopped:
	if (ISSET(sc->sc_ucr1, IMXUART_CR1_TXMPTYEN)) {
		CLR(sc->sc_ucr1, IMXUART_CR1_TXMPTYEN);
		bus_space_write_2(iot, ioh, IMXUART_UCR1, sc->sc_ucr1);
	}
	splx(s);
}

void
imxuart_diag(void *arg)
{
	struct imxuart_softc *sc = arg;
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

void
imxuart_raisedtr(void *arg)
{
	struct imxuart_softc *sc = arg;

	SET(sc->sc_ucr3, IMXUART_CR3_DSR); /* XXX */
	bus_space_write_2(sc->sc_iot, sc->sc_ioh, IMXUART_UCR3, sc->sc_ucr3);
}

void
imxuart_softint(void *arg)
{
	struct imxuart_softc *sc = arg;
	struct tty *tp;
	u_int16_t *ibufp;
	u_int16_t *ibufend;
	int c;
	int err;
	int s;

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
	sc->sc_ibufhigh = sc->sc_ibuf + IMXUART_IHIGHWATER;
	sc->sc_ibufend = sc->sc_ibuf + IMXUART_IBUFSIZE;

	if (ISSET(tp->t_cflag, CRTSCTS) &&
	    !ISSET(sc->sc_ucr3, IMXUART_CR3_DSR)) {
		/* XXX */
		SET(sc->sc_ucr3, IMXUART_CR3_DSR);
		bus_space_write_2(sc->sc_iot, sc->sc_ioh, IMXUART_UCR3,
		    sc->sc_ucr3);
	}

	splx(s);

	while (ibufp < ibufend) {
		c = *ibufp++;
		if (ISSET(c, IMXUART_RX_OVERRUN)) {
			sc->sc_overflows++;
			if (sc->sc_errors++ == 0)
				timeout_add_sec(&sc->sc_diag_tmo, 60);
		}
		/* This is ugly, but fast. */

		err = 0;
		if (ISSET(c, IMXUART_RX_PRERR))
			err |= TTY_PE;
		if (ISSET(c, IMXUART_RX_FRMERR))
			err |= TTY_FE;
		c = (c & 0xff) | err;
		(*linesw[tp->t_line].l_rint)(c, tp);
	}
}

int
imxuartopen(dev_t dev, int flag, int mode, struct proc *p)
{
	int unit = DEVUNIT(dev);
	struct imxuart_softc *sc;
	bus_space_tag_t iot;
	bus_space_handle_t ioh;
	struct tty *tp;
	int s;
	int error = 0;

	if (unit >= imxuart_cd.cd_ndevs)
		return ENXIO;
	sc = imxuart_cd.cd_devs[unit];
	if (sc == NULL)
		return ENXIO;

	s = spltty();
	if (sc->sc_tty == NULL)
		tp = sc->sc_tty = ttymalloc(0);
	else
		tp = sc->sc_tty;

	splx(s);

	tp->t_oproc = imxuart_start;
	tp->t_param = imxuart_param;
	tp->t_dev = dev;

	if (!ISSET(tp->t_state, TS_ISOPEN)) {
		SET(tp->t_state, TS_WOPEN);
		ttychars(tp);
		tp->t_iflag = TTYDEF_IFLAG;
		tp->t_oflag = TTYDEF_OFLAG;

		if (ISSET(sc->sc_hwflags, COM_HW_CONSOLE))
			tp->t_cflag = imxuartconscflag;
		else
			tp->t_cflag = TTYDEF_CFLAG;
		if (ISSET(sc->sc_swflags, COM_SW_CLOCAL))
			SET(tp->t_cflag, CLOCAL);
		if (ISSET(sc->sc_swflags, COM_SW_CRTSCTS))
			SET(tp->t_cflag, CRTSCTS);
		if (ISSET(sc->sc_swflags, COM_SW_MDMBUF))
			SET(tp->t_cflag, MDMBUF);
		tp->t_lflag = TTYDEF_LFLAG;
		tp->t_ispeed = tp->t_ospeed = imxuartdefaultrate;

		s = spltty();

		sc->sc_initialize = 1;
		imxuart_param(tp, &tp->t_termios);
		ttsetwater(tp);
		sc->sc_ibufp = sc->sc_ibuf = sc->sc_ibufs[0];
		sc->sc_ibufhigh = sc->sc_ibuf + IMXUART_IHIGHWATER;
		sc->sc_ibufend = sc->sc_ibuf + IMXUART_IBUFSIZE;

		iot = sc->sc_iot;
		ioh = sc->sc_ioh;

		sc->sc_ucr1 = bus_space_read_2(iot, ioh, IMXUART_UCR1);
		sc->sc_ucr2 = bus_space_read_2(iot, ioh, IMXUART_UCR2);
		sc->sc_ucr3 = bus_space_read_2(iot, ioh, IMXUART_UCR3);
		sc->sc_ucr4 = bus_space_read_2(iot, ioh, IMXUART_UCR4);

		/* interrupt after one char on tx/rx */
		/* reference frequency divider: 1 */
		bus_space_write_2(iot, ioh, IMXUART_UFCR,
		    1 << IMXUART_FCR_TXTL_SH |
		    5 << IMXUART_FCR_RFDIV_SH |
		    1 << IMXUART_FCR_RXTL_SH);

		bus_space_write_2(iot, ioh, IMXUART_UBIR,
		    (imxuartdefaultrate / 100) - 1);

		/* formula: clk / (rfdiv * 1600) */
		bus_space_write_2(iot, ioh, IMXUART_UBMR,
		    clock_get_frequency(sc->sc_node, "per") / 1600);

		SET(sc->sc_ucr1, IMXUART_CR1_EN|IMXUART_CR1_RRDYEN);
		SET(sc->sc_ucr2, IMXUART_CR2_TXEN|IMXUART_CR2_RXEN);
		bus_space_write_2(iot, ioh, IMXUART_UCR1, sc->sc_ucr1);
		bus_space_write_2(iot, ioh, IMXUART_UCR2, sc->sc_ucr2);

		/* sc->sc_mcr = MCR_DTR | MCR_RTS;  XXX */
		SET(sc->sc_ucr3, IMXUART_CR3_DSR); /* XXX */
		bus_space_write_2(iot, ioh, IMXUART_UCR3, sc->sc_ucr3);

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
imxuartclose(dev_t dev, int flag, int mode, struct proc *p)
{
	int unit = DEVUNIT(dev);
	struct imxuart_softc *sc = imxuart_cd.cd_devs[unit];
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	struct tty *tp = sc->sc_tty;
	int s;

	/* XXX This is for cons.c. */
	if (!ISSET(tp->t_state, TS_ISOPEN))
		return 0;

	(*linesw[tp->t_line].l_close)(tp, flag, p);
	s = spltty();
	if (ISSET(tp->t_state, TS_WOPEN)) {
		/* tty device is waiting for carrier; drop dtr then re-raise */
		CLR(sc->sc_ucr3, IMXUART_CR3_DSR);
		bus_space_write_2(iot, ioh, IMXUART_UCR3, sc->sc_ucr3);
		timeout_add_sec(&sc->sc_dtr_tmo, 2);
	}
	CLR(tp->t_state, TS_BUSY | TS_FLUSH);

	sc->sc_cua = 0;
	splx(s);
	ttyclose(tp);

	return 0;
}

int
imxuartread(dev_t dev, struct uio *uio, int flag)
{
	struct tty *tty;

	tty = imxuarttty(dev);
	if (tty == NULL)
		return ENODEV;

	return((*linesw[tty->t_line].l_read)(tty, uio, flag));
}

int
imxuartwrite(dev_t dev, struct uio *uio, int flag)
{
	struct tty *tty;

	tty = imxuarttty(dev);
	if (tty == NULL)
		return ENODEV;

	return((*linesw[tty->t_line].l_write)(tty, uio, flag));
}

int
imxuartioctl( dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct imxuart_softc *sc;
	struct tty *tp;
	int error;

	sc = imxuart_sc(dev);
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
		/* */
		break;

	case TIOCCBRK:
		/* */
		break;

	case TIOCSDTR:
#if 0
		(void) clmctl(dev, TIOCM_DTR | TIOCM_RTS, DMBIS);
#endif
		break;

	case TIOCCDTR:
#if 0
		(void) clmctl(dev, TIOCM_DTR | TIOCM_RTS, DMBIC);
#endif
		break;

	case TIOCMSET:
#if 0
		(void) clmctl(dev, *(int *) data, DMSET);
#endif
		break;

	case TIOCMBIS:
#if 0
		(void) clmctl(dev, *(int *) data, DMBIS);
#endif
		break;

	case TIOCMBIC:
#if 0
		(void) clmctl(dev, *(int *) data, DMBIC);
#endif
		break;

        case TIOCMGET:
#if 0
		*(int *)data = clmctl(dev, 0, DMGET);
#endif
		break;

	case TIOCGFLAGS:
#if 0
		*(int *)data = cl->cl_swflags;
#endif
		break;

	case TIOCSFLAGS:
		error = suser(p);
		if (error != 0)
			return(EPERM);

#if 0
		cl->cl_swflags = *(int *)data;
		cl->cl_swflags &= /* only allow valid flags */
		    (TIOCFLAG_SOFTCAR | TIOCFLAG_CLOCAL | TIOCFLAG_CRTSCTS);
#endif
		break;
	default:
		return (ENOTTY);
	}

	return 0;
}

int
imxuartstop(struct tty *tp, int flag)
{
	return 0;
}

struct tty *
imxuarttty(dev_t dev)
{
	int unit;
	struct imxuart_softc *sc;
	unit = DEVUNIT(dev);
	if (unit >= imxuart_cd.cd_ndevs)
		return NULL;
	sc = (struct imxuart_softc *)imxuart_cd.cd_devs[unit];
	if (sc == NULL)
		return NULL;
	return sc->sc_tty;
}

struct imxuart_softc *
imxuart_sc(dev_t dev)
{
	int unit;
	struct imxuart_softc *sc;
	unit = DEVUNIT(dev);
	if (unit >= imxuart_cd.cd_ndevs)
		return NULL;
	sc = (struct imxuart_softc *)imxuart_cd.cd_devs[unit];
	return sc;
}


/* serial console */
void
imxuartcnprobe(struct consdev *cp)
{
}

void
imxuartcninit(struct consdev *cp)
{
}

int
imxuartcnattach(bus_space_tag_t iot, bus_addr_t iobase, int rate, tcflag_t cflag)
{
	static struct consdev imxuartcons = {
		NULL, NULL, imxuartcngetc, imxuartcnputc, imxuartcnpollc, NULL,
		NODEV, CN_MIDPRI
	};
	int maj;

	if (bus_space_map(iot, iobase, IMXUART_SPACE, 0, &imxuartconsioh))
		return ENOMEM;

	/* Look for major of com(4) to replace. */
	for (maj = 0; maj < nchrdev; maj++)
		if (cdevsw[maj].d_open == comopen)
			break;
	if (maj == nchrdev)
		return ENXIO;

	cn_tab = &imxuartcons;
	cn_tab->cn_dev = makedev(maj, 0);
	cdevsw[maj] = imxuartdev; 	/* KLUDGE */

	imxuartconsiot = iot;
	imxuartconsaddr = iobase;
	imxuartconscflag = cflag;

	// XXXX: Overwrites some sensitive bits, recheck later.
	/*
	bus_space_write_2(imxuartconsiot, imxuartconsioh, IMXUART_UCR1,
	    IMXUART_CR1_EN);
	bus_space_write_2(imxuartconsiot, imxuartconsioh, IMXUART_UCR2,
	    IMXUART_CR2_TXEN|IMXUART_CR2_RXEN);
	*/

	return 0;
}

int
imxuartcngetc(dev_t dev)
{
	int c;
	int s;
	s = splhigh();
	while((bus_space_read_2(imxuartconsiot, imxuartconsioh, IMXUART_USR2) &
	    IMXUART_SR2_RDR) == 0)
		;
	c = bus_space_read_1(imxuartconsiot, imxuartconsioh, IMXUART_URXD);
	splx(s);
	return c;
}

void
imxuartcnputc(dev_t dev, int c)
{
	int s;
	s = splhigh();
	bus_space_write_1(imxuartconsiot, imxuartconsioh, IMXUART_UTXD, (uint8_t)c);
	while((bus_space_read_2(imxuartconsiot, imxuartconsioh, IMXUART_USR2) &
	    IMXUART_SR2_TXDC) == 0)
		;
	splx(s);
}

void
imxuartcnpollc(dev_t dev, int on)
{
}
