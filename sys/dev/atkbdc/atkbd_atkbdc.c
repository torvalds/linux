/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Kazutaka YOKOTA <yokota@zodiac.mech.utsunomiya-u.ac.jp>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_kbd.h"
#include "opt_evdev.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <sys/kbio.h>
#include <dev/kbd/kbdreg.h>
#include <dev/atkbdc/atkbdreg.h>
#include <dev/atkbdc/atkbdcreg.h>

typedef struct {
	struct resource	*intr;
	void		*ih;
} atkbd_softc_t;

static devclass_t	atkbd_devclass;

static void	atkbdidentify(driver_t *driver, device_t dev);
static int	atkbdprobe(device_t dev);
static int	atkbdattach(device_t dev);
static int	atkbdresume(device_t dev);
static void	atkbdintr(void *arg);

static device_method_t atkbd_methods[] = {
	DEVMETHOD(device_identify,	atkbdidentify),
	DEVMETHOD(device_probe,		atkbdprobe),
	DEVMETHOD(device_attach,	atkbdattach),
	DEVMETHOD(device_resume,	atkbdresume),
	{ 0, 0 }
};

static driver_t atkbd_driver = {
	ATKBD_DRIVER_NAME,
	atkbd_methods,
	sizeof(atkbd_softc_t),
};

static void
atkbdidentify(driver_t *driver, device_t parent)
{

	/* always add at least one child */
	BUS_ADD_CHILD(parent, KBDC_RID_KBD, driver->name, device_get_unit(parent));
}

static int
atkbdprobe(device_t dev)
{
	struct resource *res;
	u_long irq;
	int flags;
	int rid;

	device_set_desc(dev, "AT Keyboard");

	/* obtain parameters */
	flags = device_get_flags(dev);

	/* see if IRQ is available */
	rid = KBDC_RID_KBD;
	res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_ACTIVE);
	if (res == NULL) {
		if (bootverbose)
			device_printf(dev, "unable to allocate IRQ\n");
		return ENXIO;
	}
	irq = rman_get_start(res);
	bus_release_resource(dev, SYS_RES_IRQ, rid, res);

	/* probe the device */
	return atkbd_probe_unit(dev, irq, flags);
}

static int
atkbdattach(device_t dev)
{
	atkbd_softc_t *sc;
	keyboard_t *kbd;
	u_long irq;
	int flags;
	int rid;
	int error;

	sc = device_get_softc(dev);

	rid = KBDC_RID_KBD;
	irq = bus_get_resource_start(dev, SYS_RES_IRQ, rid);
	flags = device_get_flags(dev);
	error = atkbd_attach_unit(dev, &kbd, irq, flags);
	if (error)
		return error;

	/* declare our interrupt handler */
	sc->intr = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_ACTIVE);
	if (sc->intr == NULL)
		return ENXIO;
	error = bus_setup_intr(dev, sc->intr, INTR_TYPE_TTY, NULL, atkbdintr,
			       kbd, &sc->ih);
	if (error)
		bus_release_resource(dev, SYS_RES_IRQ, rid, sc->intr);

	return error;
}

static int
atkbdresume(device_t dev)
{
	atkbd_softc_t *sc;
	keyboard_t *kbd;
	int args[2];

	sc = device_get_softc(dev);
	kbd = kbd_get_keyboard(kbd_find_keyboard(ATKBD_DRIVER_NAME,
						 device_get_unit(dev)));
	if (kbd) {
		kbd->kb_flags &= ~KB_INITIALIZED;
		args[0] = device_get_unit(device_get_parent(dev));
		args[1] = rman_get_start(sc->intr);
		kbdd_init(kbd, device_get_unit(dev), &kbd, args,
		    device_get_flags(dev));
		kbdd_clear_state(kbd);
	}
	return 0;
}

static void
atkbdintr(void *arg)
{
	keyboard_t *kbd;

	kbd = (keyboard_t *)arg;
	kbdd_intr(kbd, NULL);
}

DRIVER_MODULE(atkbd, atkbdc, atkbd_driver, atkbd_devclass, 0, 0);
#ifdef EVDEV_SUPPORT
MODULE_DEPEND(atkbd, evdev, 1, 1, 1);
#endif
