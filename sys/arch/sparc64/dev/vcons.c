/*	$OpenBSD: vcons.c,v 1.19 2021/10/24 17:05:04 mpi Exp $	*/
/*
 * Copyright (c) 2008 Mark Kettenis
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
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/tty.h>

#ifdef DDB
#include <ddb/db_var.h>
#endif

#include <machine/autoconf.h>
#include <machine/conf.h>
#include <machine/hypervisor.h>
#include <machine/openfirm.h>

#include <dev/cons.h>
#include <sparc64/dev/vbusvar.h>

struct vcons_softc {
	struct device	sc_dv;
	bus_space_tag_t	sc_bustag;

	void		*sc_ih;
	uint64_t	sc_sysino;
	struct tty	*sc_tty;
	void		*sc_si;
};

int	vcons_match(struct device *, void *, void *);
void	vcons_attach(struct device *, struct device *, void *);

const struct cfattach vcons_ca = {
	sizeof(struct vcons_softc), vcons_match, vcons_attach
};

struct cfdriver vcons_cd = {
	NULL, "vcons", DV_DULL
};

int	vcons_intr(void *);

int	vcons_cnlookc(dev_t, int *);
int	vcons_cngetc(dev_t);
void	vcons_cnputc(dev_t, int);

void	vconsstart(struct tty *);
int	vconsparam(struct tty *, struct termios *);
void	vcons_softintr(void *);

int
vcons_match(struct device *parent, void *match, void *aux)
{
	struct vbus_attach_args *va = aux;

	if (strcmp(va->va_name, "console") == 0)
		return (1);

	return (0);
}

void
vcons_attach(struct device *parent, struct device *self, void *aux)
{
	struct vcons_softc *sc = (struct vcons_softc *)self;
	struct vbus_attach_args *va = aux;
	int vcons_is_input, vcons_is_output;
	int node, maj;

	sc->sc_si = softintr_establish(IPL_TTY, vcons_softintr, sc);
	if (sc->sc_si == NULL)
		panic(": can't establish soft interrupt");

	if (vbus_intr_map(va->va_node, va->va_intr[0], &sc->sc_sysino))
		printf(": can't map interrupt\n");
	printf(": ivec 0x%llx", sc->sc_sysino);

	sc->sc_bustag = va->va_bustag;
	sc->sc_ih = bus_intr_establish(sc->sc_bustag, sc->sc_sysino, IPL_TTY,
	    BUS_INTR_ESTABLISH_MPSAFE, vcons_intr, sc, sc->sc_dv.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(", can't establish interrupt\n");
		return;
	}

	node = OF_instance_to_package(OF_stdin());
	vcons_is_input = (va->va_node == node);
	node = OF_instance_to_package(OF_stdout());
	vcons_is_output = (va->va_node == node);

	if (vcons_is_input || vcons_is_output) {
		if (vcons_is_input) {
			cn_tab->cn_pollc = nullcnpollc;
			cn_tab->cn_getc = vcons_cngetc;

			/* Locate the major number. */
			for (maj = 0; maj < nchrdev; maj++)
				if (cdevsw[maj].d_open == vconsopen)
					break;
			cn_tab->cn_dev = makedev(maj, self->dv_unit);
		}
		if (vcons_is_output) 
			cn_tab->cn_putc = vcons_cnputc;

		printf(": console");
	}

	printf("\n");
}

int
vcons_intr(void *arg)
{
	struct vcons_softc *sc = arg;

	if (sc->sc_tty)
		softintr_schedule(sc->sc_si);

	return (1);
}

int
vcons_cnlookc(dev_t dev, int *cp)
{
	int64_t ch;

	if (hv_cons_getchar(&ch) == H_EOK) {
#ifdef DDB
		if (ch == -1 && db_console)
			db_enter();
#endif
		*cp = ch;
		return (1);
	}

	return (0);
}

int
vcons_cngetc(dev_t dev)
{
	int c;

	while(!vcons_cnlookc(dev, &c))
		;

	return (c);
}

void
vcons_cnputc(dev_t dev, int c)
{
	while (hv_cons_putchar(c) == H_EWOULDBLOCK);
}

int
vconsopen(dev_t dev, int flag, int mode, struct proc *p)
{
	struct vcons_softc *sc;
	struct tty *tp;
	int unit = minor(dev);
	int err;

	if (unit >= vcons_cd.cd_ndevs)
		return (ENXIO);
	sc = vcons_cd.cd_devs[unit];
	if (sc == NULL)
		return (ENXIO);

	if (sc->sc_tty)
		tp = sc->sc_tty;
	else
		tp = sc->sc_tty = ttymalloc(0);

	tp->t_oproc = vconsstart;
	tp->t_param = vconsparam;
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
		return (EBUSY);
	tp->t_state |= TS_CARR_ON;

	err = (*linesw[tp->t_line].l_open)(dev, tp, p);
	if (err == 0)
		vbus_intr_setenabled(sc->sc_bustag, sc->sc_sysino, INTR_ENABLED);

	return err;
}

int
vconsclose(dev_t dev, int flag, int mode, struct proc *p)
{
	struct vcons_softc *sc;
	struct tty *tp;
	int unit = minor(dev);

	if (unit >= vcons_cd.cd_ndevs)
		return (ENXIO);
	sc = vcons_cd.cd_devs[unit];
	if (sc == NULL)
		return (ENXIO);

	vbus_intr_setenabled(sc->sc_bustag, sc->sc_sysino, INTR_DISABLED);

	tp = sc->sc_tty;
	(*linesw[tp->t_line].l_close)(tp, flag, p);
	ttyclose(tp);
	return (0);
}

int
vconsread(dev_t dev, struct uio *uio, int flag)
{
	struct vcons_softc *sc;
	struct tty *tp;
	int unit = minor(dev);

	if (unit >= vcons_cd.cd_ndevs)
		return (ENXIO);
	sc = vcons_cd.cd_devs[unit];
	if (sc == NULL)
		return (ENXIO);

	tp = sc->sc_tty;
	return ((*linesw[tp->t_line].l_read)(tp, uio, flag));
}

int
vconswrite(dev_t dev, struct uio *uio, int flag)
{
	struct vcons_softc *sc;
	struct tty *tp;
	int unit = minor(dev);

	if (unit >= vcons_cd.cd_ndevs)
		return (ENXIO);
	sc = vcons_cd.cd_devs[unit];
	if (sc == NULL)
		return (ENXIO);

	tp = sc->sc_tty;
	return ((*linesw[tp->t_line].l_write)(tp, uio, flag));
}

int
vconsioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct vcons_softc *sc;
	struct tty *tp;
	int unit = minor(dev);
	int error;

	if (unit >= vcons_cd.cd_ndevs)
		return (ENXIO);
	sc = vcons_cd.cd_devs[unit];
	if (sc == NULL)
		return (ENXIO);

	tp = sc->sc_tty;
	error = (*linesw[tp->t_line].l_ioctl)(tp, cmd, data, flag, p);
	if (error >= 0)
		return error;
	error = ttioctl(tp, cmd, data, flag, p);
	if (error >= 0)
		return (error);

	return (ENOTTY);
}

void
vconsstart(struct tty *tp)
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
		vcons_cnputc(tp->t_dev, getc(&tp->t_outq));
	tp->t_state &= ~TS_BUSY;
	splx(s);
}

int
vconsstop(struct tty *tp, int flag)
{
	int s;

	s = spltty();
	if (tp->t_state & TS_BUSY)
		if ((tp->t_state & TS_TTSTOP) == 0)
			tp->t_state |= TS_FLUSH;
	splx(s);
	return (0);
}

struct tty *
vconstty(dev_t dev)
{
	struct vcons_softc *sc;
	int unit = minor(dev);

	if (unit >= vcons_cd.cd_ndevs)
		return (NULL);
	sc = vcons_cd.cd_devs[unit];
	if (sc == NULL)
		return (NULL);

	return sc->sc_tty;
}

int
vconsparam(struct tty *tp, struct termios *t)
{
	tp->t_ispeed = t->c_ispeed;
	tp->t_ospeed = t->c_ospeed;
	tp->t_cflag = t->c_cflag;
	return (0);
}

void
vcons_softintr(void *arg)
{
	struct vcons_softc *sc = arg;
	struct tty *tp = sc->sc_tty;
	int c;

	while (vcons_cnlookc(tp->t_dev, &c)) {
		if (tp->t_state & TS_ISOPEN)
			(*linesw[tp->t_line].l_rint)(c, tp);
	}

	vbus_intr_setstate(sc->sc_bustag, sc->sc_sysino, INTR_IDLE);
}
