/* $OpenBSD: mms.c,v 1.21 2022/02/21 10:24:28 mpi Exp $ */
/*	$NetBSD: mms.c,v 1.35 2000/01/08 02:57:25 takemura Exp $	*/

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

#include <machine/intr.h>
#include <machine/bus.h>

#include <dev/isa/isavar.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsmousevar.h>

#define	MMS_ADDR	0	/* offset for register select */
#define	MMS_DATA	1	/* offset for InPort data */
#define	MMS_IDENT	2	/* offset for identification register */
#define	MMS_NPORTS	4

struct mms_softc {		/* driver status information */
	struct device sc_dev;
	void *sc_ih;

	bus_space_tag_t sc_iot;
	bus_space_handle_t sc_ioh;

	int sc_enabled; /* device is open */

	struct device *sc_wsmousedev;
};

int mmsprobe(struct device *, void *, void *);
void mmsattach(struct device *, struct device *, void *);
int mmsintr(void *);

const struct cfattach mms_ca = {
	sizeof(struct mms_softc), mmsprobe, mmsattach
};

int	mms_enable(void *);
int	mms_ioctl(void *, u_long, caddr_t, int, struct proc *);
void	mms_disable(void *);

const struct wsmouse_accessops mms_accessops = {
	mms_enable,
	mms_ioctl,
	mms_disable,
};

int
mmsprobe(struct device *parent, void *match, void *aux)
{
	struct isa_attach_args *ia = aux;
	bus_space_tag_t iot = ia->ia_iot;
	bus_space_handle_t ioh;
	int rv;

	/* Disallow wildcarded i/o address. */
	if (ia->ia_iobase == IOBASEUNK)
		return 0;

	/* Map the i/o space. */
	if (bus_space_map(iot, ia->ia_iobase, MMS_NPORTS, 0, &ioh))
		return 0;

	rv = 0;

	/* Read identification register to see if present */
	if (bus_space_read_1(iot, ioh, MMS_IDENT) != 0xde)
		goto out;

	/* Seems it was there; reset. */
	bus_space_write_1(iot, ioh, MMS_ADDR, 0x87);

	rv = 1;
	ia->ia_iosize = MMS_NPORTS;
	ia->ia_msize = 0;

out:
	bus_space_unmap(iot, ioh, MMS_NPORTS);
	return rv;
}

void
mmsattach(struct device *parent, struct device *self, void *aux)
{
	struct mms_softc *sc = (void *)self;
	struct isa_attach_args *ia = aux;
	bus_space_tag_t iot = ia->ia_iot;
	bus_space_handle_t ioh;
	struct wsmousedev_attach_args a;

	printf("\n");

	if (bus_space_map(iot, ia->ia_iobase, MMS_NPORTS, 0, &ioh)) {
		printf("%s: can't map i/o space\n", sc->sc_dev.dv_xname);
		return;
	}

	/* Other initialization was done by mmsprobe. */
	sc->sc_iot = iot;
	sc->sc_ioh = ioh;
	sc->sc_enabled = 0;

	sc->sc_ih = isa_intr_establish(ia->ia_ic, ia->ia_irq, IST_PULSE,
	    IPL_TTY, mmsintr, sc, sc->sc_dev.dv_xname);

	a.accessops = &mms_accessops;
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
mms_enable(void *v)
{
	struct mms_softc *sc = v;

	if (sc->sc_enabled)
		return EBUSY;

	sc->sc_enabled = 1;

	/* Enable interrupts. */
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, MMS_ADDR, 0x07);
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, MMS_DATA, 0x09);

	return 0;
}

void
mms_disable(void *v)
{
	struct mms_softc *sc = v;

	/* Disable interrupts. */
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, MMS_ADDR, 0x87);

	sc->sc_enabled = 0;
}

int
mms_ioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)
{
#if 0
	struct mms_softc *sc = v;
#endif

	switch (cmd) {
	case WSMOUSEIO_GTYPE:
		*(u_int *)data = WSMOUSE_TYPE_MMS;
		return (0);
	}
	return (-1);
}

int
mmsintr(void *arg)
{
	struct mms_softc *sc = arg;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_char status;
	signed char dx, dy;
	u_int buttons;
	int changed;

	if (!sc->sc_enabled)
		/* Interrupts are not expected. */
		return 0;

	/* Freeze InPort registers (disabling interrupts). */
	bus_space_write_1(iot, ioh, MMS_ADDR, 0x07);
	bus_space_write_1(iot, ioh, MMS_DATA, 0x29);

	bus_space_write_1(iot, ioh, MMS_ADDR, 0x00);
	status = bus_space_read_1(iot, ioh, MMS_DATA);

	if (status & 0x40) {
		bus_space_write_1(iot, ioh, MMS_ADDR, 1);
		dx = bus_space_read_1(iot, ioh, MMS_DATA);
		/* Bounding at -127 avoids a bug in XFree86. */
		dx = (dx == -128) ? -127 : dx;

		bus_space_write_1(iot, ioh, MMS_ADDR, 2);
		dy = bus_space_read_1(iot, ioh, MMS_DATA);
		dy = (dy == -128) ? 127 : -dy;
	} else
		dx = dy = 0;

	/* Unfreeze InPort registers (reenabling interrupts). */
	bus_space_write_1(iot, ioh, MMS_ADDR, 0x07);
	bus_space_write_1(iot, ioh, MMS_DATA, 0x09);

	buttons = ((status & 0x04) ? 0x1 : 0) |
		((status & 0x02) ? 0x2 : 0) |
		((status & 0x01) ? 0x4 : 0);
	changed = status & 0x38;

	if (dx || dy || changed)
		WSMOUSE_INPUT(sc->sc_wsmousedev, buttons, dx, dy, 0, 0);

	return -1;
}

struct cfdriver mms_cd = {
	NULL, "mms", DV_DULL
};
