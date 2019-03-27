/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Juli Mallett <jmallett@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/clock.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/random.h>

#include <contrib/octeon-sdk/cvmx.h>
#include <contrib/octeon-sdk/cvmx-rng.h>

#define	OCTEON_RND_WORDS	2

struct octeon_rnd_softc {
	uint64_t sc_entropy[OCTEON_RND_WORDS];
	struct callout sc_callout;
};

static void	octeon_rnd_identify(driver_t *drv, device_t parent);
static int	octeon_rnd_attach(device_t dev);
static int	octeon_rnd_probe(device_t dev);
static int	octeon_rnd_detach(device_t dev);

static void	octeon_rnd_harvest(void *);

static device_method_t octeon_rnd_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	octeon_rnd_identify),
	DEVMETHOD(device_probe,		octeon_rnd_probe),
	DEVMETHOD(device_attach,	octeon_rnd_attach),
	DEVMETHOD(device_detach,	octeon_rnd_detach),

	{ 0, 0 }
};

static driver_t octeon_rnd_driver = {
	"rnd",
	octeon_rnd_methods,
	sizeof (struct octeon_rnd_softc)
};
static devclass_t octeon_rnd_devclass;
DRIVER_MODULE(rnd, nexus, octeon_rnd_driver, octeon_rnd_devclass, 0, 0);

static void
octeon_rnd_identify(driver_t *drv, device_t parent)
{
	BUS_ADD_CHILD(parent, 0, "rnd", 0);
}

static int
octeon_rnd_probe(device_t dev)
{
	if (device_get_unit(dev) != 0)
		return (ENXIO);

	device_set_desc(dev, "Cavium Octeon Random Number Generator");
	return (BUS_PROBE_NOWILDCARD);
}

static int
octeon_rnd_attach(device_t dev)
{
	struct octeon_rnd_softc *sc;

	sc = device_get_softc(dev);
	callout_init(&sc->sc_callout, 1);
	callout_reset(&sc->sc_callout, hz * 5, octeon_rnd_harvest, sc);

	cvmx_rng_enable();

	return (0);
}

static int
octeon_rnd_detach(device_t dev)
{
	struct octeon_rnd_softc *sc;

	sc = device_get_softc(dev);

	callout_stop(&sc->sc_callout);

	return (0);
}

static void
octeon_rnd_harvest(void *arg)
{
	struct octeon_rnd_softc *sc;
	unsigned i;

	sc = arg;

	for (i = 0; i < OCTEON_RND_WORDS; i++)
		sc->sc_entropy[i] = cvmx_rng_get_random64();
	/* MarkM: FIX!! Check that this does not swamp the harvester! */
	random_harvest_queue(sc->sc_entropy, sizeof sc->sc_entropy, RANDOM_PURE_OCTEON);

	callout_reset(&sc->sc_callout, hz * 5, octeon_rnd_harvest, sc);
}
