/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2013	Justin Hibbits
 * All rights reserved.
 * Copyright (c) 1997, 1998, 1999
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Lucent WaveLAN/IEEE 802.11 MacIO attachment for FreeBSD.
 *
 * Based on the PCMCIA driver
 * Written by Bill Paul <wpaul@ctr.columbia.edu>
 * Electrical Engineering Department
 * Columbia University, New York City
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/bus.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/openfirm.h>
#include <machine/ofw_machdep.h>

#include <net/if.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/wi/if_wavelan_ieee.h>
#include <dev/wi/if_wireg.h>
#include <dev/wi/if_wivar.h>

#include <powerpc/powermac/maciovar.h>

static int wi_macio_probe(device_t);
static int wi_macio_attach(device_t);

static device_method_t wi_macio_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		wi_macio_probe),
	DEVMETHOD(device_attach,	wi_macio_attach),
	DEVMETHOD(device_detach,	wi_detach),
	DEVMETHOD(device_shutdown,	wi_shutdown),

	{ 0, 0 }
};

static driver_t wi_macio_driver = {
	"wi",
	wi_macio_methods,
	sizeof(struct wi_softc)
};

DRIVER_MODULE(wi, macio, wi_macio_driver, wi_devclass, 0, 0);
MODULE_DEPEND(wi, wlan, 1, 1, 1);

static int
wi_macio_probe(device_t dev)
{
	const char *name, *compat;

	/* Make sure we're a network driver */
	name = ofw_bus_get_name(dev);
	if (name == NULL)
		return (ENXIO);

	if (strcmp(name, "radio") != 0) {
		return ENXIO;
	}
	compat = ofw_bus_get_compat(dev);
	if (strcmp(compat, "wireless") != 0) {
		return ENXIO;
	}

	device_set_desc(dev, "Apple Airport");
	return 0;
}

static int
wi_macio_attach(device_t dev)
{
	struct wi_softc	*sc;
	int error;

	sc = device_get_softc(dev);
	sc->wi_gone = 0;
	sc->wi_bus_type = 0;

	error = wi_alloc(dev, 0);
	if (error == 0) {
		macio_enable_wireless(device_get_parent(dev), 1);
		/* Make sure interrupts are disabled. */
		CSR_WRITE_2(sc, WI_INT_EN, 0);
		CSR_WRITE_2(sc, WI_EVENT_ACK, 0xFFFF);

		error = wi_attach(dev);
		if (error != 0)
			wi_free(dev);
	}
	return error;
}
