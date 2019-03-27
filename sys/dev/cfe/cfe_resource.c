/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007 Bruce M. Simpson.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Driver to swallow up memory ranges reserved by CFE platform firmware.
 * CFE on Sentry5 doesn't specify reserved ranges, so this is not useful
 * at the present time.
 * TODO: Don't attach this off nexus.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/socket.h>

#include <sys/module.h>
#include <sys/bus.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/cfe/cfe_api.h>
#include <dev/cfe/cfe_error.h>

#define MAX_CFE_RESERVATIONS 16

struct cferes_softc {
	int		 rnum;
	int		 rid[MAX_CFE_RESERVATIONS];
	struct resource	*res[MAX_CFE_RESERVATIONS];
};

static int
cferes_probe(device_t dev)
{

	return (BUS_PROBE_NOWILDCARD);
}

static int
cferes_attach(device_t dev)
{

	return (0);
}

static void
cferes_identify(driver_t* driver, device_t parent)
{
	device_t		 child;
	int			 i;
	struct resource		*res;
	int			 result;
	int			 rid;
	struct cferes_softc	*sc;
	uint64_t		 addr, len, type;

	child = BUS_ADD_CHILD(parent, 100, "cferes", -1);
	device_set_driver(child, driver);
	sc = device_get_softc(child);

	sc->rnum = 0;
	for (i = 0; i < ~0U; i++) {
		result = cfe_enummem(i, CFE_FLG_FULL_ARENA, &addr, &len, &type);
		if (result < 0)
			break;
		if (type != CFE_MI_RESERVED) {
			if (bootverbose)
			printf("%s: skipping non reserved range 0x%0jx(%jd)\n",
			    device_getnameunit(child),
			    (uintmax_t)addr, (uintmax_t)len);
			continue;
		}

		bus_set_resource(child, SYS_RES_MEMORY, sc->rnum, addr, len);
		rid = sc->rnum;
		res = bus_alloc_resource_any(child, SYS_RES_MEMORY, &rid, 0);
		if (res == NULL) {
			bus_delete_resource(child, SYS_RES_MEMORY, sc->rnum);
			continue;
		}
		sc->rid[sc->rnum] = rid;
		sc->res[sc->rnum] = res;

		sc->rnum++;
		if (sc->rnum == MAX_CFE_RESERVATIONS)
			break;
	}

	if (sc->rnum == 0) {
		device_delete_child(parent, child);
		return;
	}

	device_set_desc(child, "CFE reserved memory");
}

static int
cferes_detach(device_t dev)
{
	int			i;
	struct cferes_softc	*sc = device_get_softc(dev);

	for (i = 0; i < sc->rnum; i++) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->rid[i],
		    sc->res[i]);
	}

	return (0);
}

static device_method_t cferes_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	cferes_identify),
	DEVMETHOD(device_probe,		cferes_probe),
	DEVMETHOD(device_attach,	cferes_attach),
	DEVMETHOD(device_detach,	cferes_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),
	{ 0, 0 }
};

static driver_t cferes_driver = {
	"cferes",
	cferes_methods,
	sizeof (struct cferes_softc)
};

static devclass_t cferes_devclass;

DRIVER_MODULE(cfe, nexus, cferes_driver, cferes_devclass, 0, 0);
