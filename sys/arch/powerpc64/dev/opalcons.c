/*	$OpenBSD: opalcons.c,v 1.4 2022/04/06 18:59:27 naddy Exp $	*/
/*
 * Copyright (c) 2020 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/device.h>
#include <sys/systm.h>
#include <sys/tty.h>

#include <machine/bus.h>
#include <machine/conf.h>
#include <machine/fdt.h>
#include <machine/opal.h>

#include <dev/cons.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

struct opalcons_softc {
	struct device	sc_dev;
	uint64_t	sc_reg;

	struct tty	*sc_tty;
	void		*sc_ih;
	void		*sc_si;
};

int	opalcons_match(struct device *, void *, void *);
void	opalcons_attach(struct device *, struct device *, void *);

const struct cfattach opalcons_ca = {
	sizeof (struct opalcons_softc), opalcons_match, opalcons_attach
};

struct cfdriver opalcons_cd = {
	NULL, "opalcons", DV_DULL
};

int	opalcons_intr(void *);
void	opalcons_softintr(void *);

void	opalconsstart(struct tty *);
int	opalconsparam(struct tty *, struct termios *);

int
opalcons_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "ibm,opal-console-raw");
}

void
opalcons_attach(struct device *parent, struct device *self, void *aux)
{
	struct opalcons_softc *sc = (struct opalcons_softc *)self;
	struct fdt_attach_args *faa = aux;
	int maj;

	sc->sc_reg = OF_getpropint(faa->fa_node, "reg", 0);

	/* Locate the major number. */
	for (maj = 0; maj < nchrdev; maj++)
		if (cdevsw[maj].d_open == opalconsopen)
			break;

	/*
	 * Unconditionally set the major/minor here since we attach
	 * early and are the fallback console device
	 */
	cn_tab->cn_dev = makedev(maj, self->dv_unit);

	if (faa->fa_node == stdout_node)
		printf(": console");

	sc->sc_si = softintr_establish(IPL_TTY, opalcons_softintr, sc);
	if (sc->sc_si == NULL) {
		printf(": can't establish soft interrupt");
		return;
	}

	sc->sc_ih = opal_intr_establish(OPAL_EVENT_CONSOLE_INPUT, IPL_TTY,
	    opalcons_intr, sc);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		return;
	}

	printf("\n");
}

struct opalcons_softc *
opalcons_sc(dev_t dev)
{
	int unit = minor(dev);

	if (unit >= opalcons_cd.cd_ndevs)
		return NULL;
	return (struct opalcons_softc *)opalcons_cd.cd_devs[unit];
}


int
opalcons_intr(void *arg)
{
	struct opalcons_softc *sc = arg;

	if (sc->sc_tty)
		softintr_schedule(sc->sc_si);

	return 1;
}

void
opalcons_putc(dev_t dev, int c)
{
	struct opalcons_softc *sc = opalcons_sc(dev);
	uint64_t len = 1;
	char ch = c;

	opal_console_write(sc->sc_reg, opal_phys(&len), opal_phys(&ch));
}

int
opalconsopen(dev_t dev, int flag, int mode, struct proc *p)
{
	struct opalcons_softc *sc = opalcons_sc(dev);
	struct tty *tp;

	if (sc == NULL)
		return ENXIO;

	if (sc->sc_tty)
		tp = sc->sc_tty;
	else
		tp = sc->sc_tty = ttymalloc(0);

	tp->t_oproc = opalconsstart;
	tp->t_param = opalconsparam;
	tp->t_dev = dev;
	if ((tp->t_state & TS_ISOPEN) == 0) {
		ttychars(tp);
		tp->t_iflag = TTYDEF_IFLAG;
		tp->t_oflag = TTYDEF_OFLAG;
		tp->t_cflag = TTYDEF_CFLAG;
		tp->t_lflag = TTYDEF_LFLAG;
		tp->t_ispeed = tp->t_ospeed = TTYDEF_SPEED;
		ttsetwater(tp);
	} else if ((tp->t_state & TS_XCLUDE) && suser(p))
		return EBUSY;
	tp->t_state |= TS_CARR_ON;

	return (*linesw[tp->t_line].l_open)(dev, tp, p);
}

int
opalconsclose(dev_t dev, int flag, int mode, struct proc *p)
{
	struct opalcons_softc *sc = opalcons_sc(dev);
	struct tty *tp;

	if (sc == NULL)
		return ENXIO;

	tp = sc->sc_tty;
	(*linesw[tp->t_line].l_close)(tp, flag, p);
	ttyclose(tp);
	return 0;
}

int
opalconsread(dev_t dev, struct uio *uio, int flag)
{
	struct opalcons_softc *sc = opalcons_sc(dev);
	struct tty *tp;

	if (sc == NULL)
		return ENXIO;

	tp = sc->sc_tty;
	return (*linesw[tp->t_line].l_read)(tp, uio, flag);
}

int
opalconswrite(dev_t dev, struct uio *uio, int flag)
{
	struct opalcons_softc *sc = opalcons_sc(dev);
	struct tty *tp;

	if (sc == NULL)
		return ENXIO;

	tp = sc->sc_tty;
	return (*linesw[tp->t_line].l_write)(tp, uio, flag);
}

int
opalconsioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct opalcons_softc *sc = opalcons_sc(dev);
	struct tty *tp;
	int error;

	if (sc == NULL)
		return ENXIO;

	tp = sc->sc_tty;
	error = (*linesw[tp->t_line].l_ioctl)(tp, cmd, data, flag, p);
	if (error >= 0)
		return error;
	error = ttioctl(tp, cmd, data, flag, p);
	if (error >= 0)
		return error;

	return ENOTTY;
}

void
opalconsstart(struct tty *tp)
{
	int s;

	s = spltty();
	if (tp->t_state & (TS_TTSTOP | TS_BUSY)) {
		splx(s);
		return;
	}
	ttwakeupwr(tp);
	tp->t_state |= TS_BUSY;
	while (tp->t_outq.c_cc != 0)
		opalcons_putc(tp->t_dev, getc(&tp->t_outq));
	tp->t_state &= ~TS_BUSY;
	splx(s);
}

int
opalconsstop(struct tty *tp, int flag)
{
	int s;

	s = spltty();
	if (tp->t_state & TS_BUSY)
		if ((tp->t_state & TS_TTSTOP) == 0)
			tp->t_state |= TS_FLUSH;
	splx(s);
	return 0;
}

struct tty *
opalconstty(dev_t dev)
{
	struct opalcons_softc *sc = opalcons_sc(dev);

	if (sc == NULL)
		return NULL;

	return sc->sc_tty;
}

int
opalconsparam(struct tty *tp, struct termios *t)
{
	tp->t_ispeed = t->c_ispeed;
	tp->t_ospeed = t->c_ospeed;
	tp->t_cflag = t->c_cflag;
	return 0;
}

int
opalcons_getc(struct opalcons_softc *sc, int *cp)
{
	uint64_t len = 1;
	uint64_t error;
	char ch;

	error = opal_console_read(sc->sc_reg, opal_phys(&len), opal_phys(&ch));
	if (error != OPAL_SUCCESS || len != 1)
		return 0;

	*cp = ch;
	return 1;
}

void
opalcons_softintr(void *arg)
{
	struct opalcons_softc *sc = arg;
	struct tty *tp = sc->sc_tty;
	int c;

	while (opalcons_getc(sc, &c)) {
		if (tp->t_state & TS_ISOPEN)
			(*linesw[tp->t_line].l_rint)(c, tp);
	}
}
