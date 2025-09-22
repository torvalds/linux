/*	$OpenBSD: lms.c,v 1.22 2022/02/21 10:24:28 mpi Exp $	*/
/*	$NetBSD: lms.c,v 1.38 2000/01/08 02:57:25 takemura Exp $	*/

/*-
 * Copyright (c) 1993, 1994 Charles M. Hannum.
 * Copyright (c) 1992, 1993 Erik Forsberg.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * THIS SOFTWARE IS PROVIDED BY ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO EVENT SHALL I BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/ioctl.h>
#include <sys/device.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/isa/isavar.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsmousevar.h>

#define	LMS_DATA	0       /* offset for data port, read-only */
#define	LMS_SIGN	1       /* offset for signature port, read-write */
#define	LMS_INTR	2       /* offset for interrupt port, read-only */
#define	LMS_CNTRL	2       /* offset for control port, write-only */
#define	LMS_CONFIG	3	/* for configuration port, read-write */
#define	LMS_NPORTS	4

struct lms_softc {		/* driver status information */
	struct device sc_dev;
	void *sc_ih;

	bus_space_tag_t sc_iot;		/* bus i/o space identifier */
	bus_space_handle_t sc_ioh;	/* bus i/o handle */

	int sc_enabled; /* device is open */
	int oldbuttons;	/* mouse button status */

	struct device *sc_wsmousedev;
};

int lmsprobe(struct device *, void *, void *);
void lmsattach(struct device *, struct device *, void *);
int lmsintr(void *);

const struct cfattach lms_ca = {
	sizeof(struct lms_softc), lmsprobe, lmsattach
};

int	lms_enable(void *);
int	lms_ioctl(void *, u_long, caddr_t, int, struct proc *);
void	lms_disable(void *);

const struct wsmouse_accessops lms_accessops = {
	lms_enable,
	lms_ioctl,
	lms_disable,
};

int
lmsprobe(struct device *parent, void *match, void *aux)
{
	struct isa_attach_args *ia = aux;
	bus_space_tag_t iot = ia->ia_iot;
	bus_space_handle_t ioh;
	int rv;
	
	/* Disallow wildcarded i/o base. */
	if (ia->ia_iobase == IOBASEUNK)
		return 0;

	/* Map the i/o space. */
	if (bus_space_map(iot, ia->ia_iobase, LMS_NPORTS, 0, &ioh))
		return 0;

	rv = 0;

	/* Configure and check for port present. */
	bus_space_write_1(iot, ioh, LMS_CONFIG, 0x91);
	delay(10);
	bus_space_write_1(iot, ioh, LMS_SIGN, 0x0c);
	delay(10);
	if (bus_space_read_1(iot, ioh, LMS_SIGN) != 0x0c)
		goto out;
	bus_space_write_1(iot, ioh, LMS_SIGN, 0x50);
	delay(10);
	if (bus_space_read_1(iot, ioh, LMS_SIGN) != 0x50)
		goto out;

	/* Disable interrupts. */
	bus_space_write_1(iot, ioh, LMS_CNTRL, 0x10);

	rv = 1;
	ia->ia_iosize = LMS_NPORTS;
	ia->ia_msize = 0;

out:
	bus_space_unmap(iot, ioh, LMS_NPORTS);
	return rv;
}

void
lmsattach(struct device *parent, struct device *self, void *aux)
{
	struct lms_softc *sc = (void *)self;
	struct isa_attach_args *ia = aux;
	bus_space_tag_t iot = ia->ia_iot;
	bus_space_handle_t ioh;
	struct wsmousedev_attach_args a;

	printf("\n");

	if (bus_space_map(iot, ia->ia_iobase, LMS_NPORTS, 0, &ioh)) {
		printf("%s: can't map i/o space\n", sc->sc_dev.dv_xname);
		return;
	}

	/* Other initialization was done by lmsprobe. */
	sc->sc_iot = iot;
	sc->sc_ioh = ioh;
	sc->sc_enabled = 0;

	sc->sc_ih = isa_intr_establish(ia->ia_ic, ia->ia_irq, IST_PULSE,
	    IPL_TTY, lmsintr, sc, sc->sc_dev.dv_xname);

	a.accessops = &lms_accessops;
	a.accesscookie = sc;

	/*
	 * Attach the wsmouse, saving a handle to it.
	 * Note that we don't need to check this pointer against NULL
	 * here or in psmintr, because if this fails lms_enable() will
	 * never be called, so lmsintr() will never be called.
	 */
	sc->sc_wsmousedev = config_found(self, &a, wsmousedevprint);
}

int
lms_enable(void *v)
{
	struct lms_softc *sc = v;

	if (sc->sc_enabled)
		return EBUSY;

	sc->sc_enabled = 1;
	sc->oldbuttons = 0;

	/* Enable interrupts. */
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, LMS_CNTRL, 0);

	return 0;
}

void
lms_disable(void *v)
{
	struct lms_softc *sc = v;

	/* Disable interrupts. */
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, LMS_CNTRL, 0x10);

	sc->sc_enabled = 0;
}

int
lms_ioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)
{
#if 0
	struct lms_softc *sc = v;
#endif

	switch (cmd) {
	case WSMOUSEIO_GTYPE:
		*(u_int *)data = WSMOUSE_TYPE_LMS;
		return (0);
	}
	return (-1);
}

int
lmsintr(void *arg)
{
	struct lms_softc *sc = arg;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_char hi, lo;
	signed char dx, dy;
	u_int buttons;
	int changed;

	if (!sc->sc_enabled)
		/* Interrupts are not expected. */
		return 0;

	bus_space_write_1(iot, ioh, LMS_CNTRL, 0xab);
	hi = bus_space_read_1(iot, ioh, LMS_DATA);
	bus_space_write_1(iot, ioh, LMS_CNTRL, 0x90);
	lo = bus_space_read_1(iot, ioh, LMS_DATA);
	dx = ((hi & 0x0f) << 4) | (lo & 0x0f);
	/* Bounding at -127 avoids a bug in XFree86. */
	dx = (dx == -128) ? -127 : dx;

	bus_space_write_1(iot, ioh, LMS_CNTRL, 0xf0);
	hi = bus_space_read_1(iot, ioh, LMS_DATA);
	bus_space_write_1(iot, ioh, LMS_CNTRL, 0xd0);
	lo = bus_space_read_1(iot, ioh, LMS_DATA);
	dy = ((hi & 0x0f) << 4) | (lo & 0x0f);
	dy = (dy == -128) ? 127 : -dy;

	bus_space_write_1(iot, ioh, LMS_CNTRL, 0);

	buttons = ((hi & 0x80) ? 0 : 0x1) |
		((hi & 0x40) ? 0 : 0x2) |
		((hi & 0x20) ? 0 : 0x4);
	changed = (buttons ^ sc->oldbuttons);
	sc->oldbuttons = buttons;

	if (dx || dy || changed)
		WSMOUSE_INPUT(sc->sc_wsmousedev, buttons, dx, dy, 0, 0);

	return -1;
}

struct cfdriver lms_cd = {
	NULL, "lms", DV_DULL
};
