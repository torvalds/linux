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

#include "opt_syscons.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/cons.h>
#include <sys/kbio.h>
#include <sys/consio.h>
#include <sys/sysctl.h>

#if defined(__i386__) || defined(__amd64__)

#include <machine/clock.h>
#include <machine/md_var.h>
#include <machine/pc/bios.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_param.h>

#define BIOS_CLKED	(1 << 6)
#define BIOS_NLKED	(1 << 5)
#define BIOS_SLKED	(1 << 4)
#define BIOS_ALKED	0

#endif

#include <dev/syscons/syscons.h>

#include <isa/isavar.h>

static devclass_t	sc_devclass;

static sc_softc_t	main_softc;

static void
scidentify(driver_t *driver, device_t parent)
{

	BUS_ADD_CHILD(parent, ISA_ORDER_SPECULATIVE, "sc", 0);
}

static int
scprobe(device_t dev)
{

	/* No pnp support */
	if (isa_get_vendorid(dev))
		return (ENXIO);

	device_set_desc(dev, "System console");
	return (sc_probe_unit(device_get_unit(dev), device_get_flags(dev)));
}

static int
scattach(device_t dev)
{

	return (sc_attach_unit(device_get_unit(dev), device_get_flags(dev) |
	    SC_AUTODETECT_KBD));
}

int
sc_max_unit(void)
{

	return (devclass_get_maxunit(sc_devclass));
}

sc_softc_t
*sc_get_softc(int unit, int flags)
{
	sc_softc_t *sc;

	if (unit < 0)
		return (NULL);
	if ((flags & SC_KERNEL_CONSOLE) != 0) {
		/* FIXME: clear if it is wired to another unit! */
		sc = &main_softc;
	} else {
	        sc = device_get_softc(devclass_get_device(sc_devclass, unit));
		if (sc == NULL)
			return (NULL);
	}
	sc->unit = unit;
	if ((sc->flags & SC_INIT_DONE) == 0) {
		sc->keyboard = -1;
		sc->adapter = -1;
		sc->cursor_char = SC_CURSOR_CHAR;
		sc->mouse_char = SC_MOUSE_CHAR;
	}
	return (sc);
}

sc_softc_t
*sc_find_softc(struct video_adapter *adp, struct keyboard *kbd)
{
	sc_softc_t *sc;
	int i;
	int units;

	sc = &main_softc;
	if ((adp == NULL || adp == sc->adp) &&
	    (kbd == NULL || kbd == sc->kbd))
		return (sc);
	units = devclass_get_maxunit(sc_devclass);
	for (i = 0; i < units; ++i) {
	        sc = device_get_softc(devclass_get_device(sc_devclass, i));
		if (sc == NULL)
			continue;
		if ((adp == NULL || adp == sc->adp) &&
		    (kbd == NULL || kbd == sc->kbd))
			return (sc);
	}
	return (NULL);
}

int
sc_get_cons_priority(int *unit, int *flags)
{
	const char *at;
	int f, u;

	*unit = -1;
	for (u = 0; u < 16; u++) {
		if (resource_disabled(SC_DRIVER_NAME, u))
			continue;
		if (resource_string_value(SC_DRIVER_NAME, u, "at", &at) != 0)
			continue;
		if (resource_int_value(SC_DRIVER_NAME, u, "flags", &f) != 0)
			f = 0;
		if (f & SC_KERNEL_CONSOLE) {
			/* the user designates this unit to be the console */
			*unit = u;
			*flags = f;
			break;
		}
		if (*unit < 0) {
			/* ...otherwise remember the first found unit */
			*unit = u;
			*flags = f;
		}
	}
	if (*unit < 0) {
		*unit = 0;
		*flags = 0;
	}
#if 0
	return ((*flags & SC_KERNEL_CONSOLE) != 0 ? CN_INTERNAL : CN_NORMAL);
#endif
	return (CN_INTERNAL);
}

void
sc_get_bios_values(bios_values_t *values)
{
#if defined(__i386__) || defined(__amd64__)
	uint8_t shift;

	shift = *(uint8_t *)BIOS_PADDRTOVADDR(0x417);
	values->shift_state = ((shift & BIOS_CLKED) != 0 ? CLKED : 0) |
	    ((shift & BIOS_NLKED) != 0 ? NLKED : 0) |
	    ((shift & BIOS_SLKED) != 0 ? SLKED : 0) |
	    ((shift & BIOS_ALKED) != 0 ? ALKED : 0);
#endif
	values->bell_pitch = BELL_PITCH;
}

int
sc_tone(int herz)
{

#if defined(HAS_TIMER_SPKR)
	if (herz) {
		if (timer_spkr_acquire())
			return (EBUSY);
		timer_spkr_setfreq(herz);
	} else
		timer_spkr_release();
#endif

	return (0);
}

static device_method_t sc_methods[] = {
	DEVMETHOD(device_identify,	scidentify),
	DEVMETHOD(device_probe,         scprobe),
	DEVMETHOD(device_attach,        scattach),
	{ 0, 0 }
};

static driver_t sc_driver = {
	SC_DRIVER_NAME,
	sc_methods,
	sizeof(sc_softc_t),
};

DRIVER_MODULE(sc, isa, sc_driver, sc_devclass, 0, 0);
