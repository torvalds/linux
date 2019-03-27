/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Justin Hibbits
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <dev/ofw/openfirm.h>

#include "cpufreq_if.h"

static int pstate_ids[256];
static int pstate_freqs[256];
static int npstates;

static void parse_pstates(void)
{
	phandle_t node;

	node = OF_finddevice("/ibm,opal/power-mgt");

	/* If this fails, npstates will remain 0, and any attachment will bail. */
	if (node == -1)
		return;

	npstates = OF_getencprop(node, "ibm,pstate-ids", pstate_ids,
	    sizeof(pstate_ids));
	if (npstates < 0) {
		npstates = 0;
		return;
	}

	if (OF_getencprop(node, "ibm,pstate-frequencies-mhz", pstate_freqs,
	    sizeof(pstate_freqs)) != npstates) {
		npstates = 0;
		return;
	}
	npstates /= sizeof(cell_t);

}

/* Make this a sysinit so it runs before the cpufreq driver attaches. */
SYSINIT(parse_pstates, SI_SUB_DRIVERS, SI_ORDER_MIDDLE, parse_pstates, NULL);

#define	PMCR_UPPERPS_MASK	0xff00000000000000UL
#define	PMCR_UPPERPS_SHIFT	56
#define	PMCR_LOWERPS_MASK	0x00ff000000000000UL
#define	PMCR_LOWERPS_SHIFT	48
#define	PMCR_VERSION_MASK	0x0000000f
#define	  PMCR_VERSION_1	  1

struct pmcr_softc {
	device_t dev;
};

static void	pmcr_identify(driver_t *driver, device_t parent);
static int	pmcr_probe(device_t dev);
static int	pmcr_attach(device_t dev);
static int	pmcr_settings(device_t dev, struct cf_setting *sets, int *count);
static int	pmcr_set(device_t dev, const struct cf_setting *set);
static int	pmcr_get(device_t dev, struct cf_setting *set);
static int	pmcr_type(device_t dev, int *type);

static device_method_t pmcr_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	pmcr_identify),
	DEVMETHOD(device_probe,		pmcr_probe),
	DEVMETHOD(device_attach,	pmcr_attach),

	/* cpufreq interface */
	DEVMETHOD(cpufreq_drv_set,	pmcr_set),
	DEVMETHOD(cpufreq_drv_get,	pmcr_get),
	DEVMETHOD(cpufreq_drv_type,	pmcr_type),
	DEVMETHOD(cpufreq_drv_settings,	pmcr_settings),

	{0, 0}
};

static driver_t pmcr_driver = {
	"pmcr",
	pmcr_methods,
	sizeof(struct pmcr_softc)
};

static devclass_t pmcr_devclass;
DRIVER_MODULE(pmcr, cpu, pmcr_driver, pmcr_devclass, 0, 0);

static void
pmcr_identify(driver_t *driver, device_t parent)
{

	/* Make sure we're not being doubly invoked. */
	if (device_find_child(parent, "pmcr", -1) != NULL)
		return;

	/*
	 * We attach a child for every CPU since settings need to
	 * be performed on every CPU in the SMP case.
	 */
	if (BUS_ADD_CHILD(parent, 10, "pmcr", -1) == NULL)
		device_printf(parent, "add pmcr child failed\n");
}

static int
pmcr_probe(device_t dev)
{
	if (resource_disabled("pmcr", 0))
		return (ENXIO);

	if (npstates == 0)
		return (ENXIO);

	device_set_desc(dev, "Power Management Control Register");
	return (0);
}

static int
pmcr_attach(device_t dev)
{
	struct pmcr_softc *sc;

	sc = device_get_softc(dev);
	sc->dev = dev;

	cpufreq_register(dev);
	return (0);
}

static int
pmcr_settings(device_t dev, struct cf_setting *sets, int *count)
{
	struct pmcr_softc *sc;
	int i;

	sc = device_get_softc(dev);
	if (sets == NULL || count == NULL)
		return (EINVAL);
	if (*count < npstates)
		return (E2BIG);

	/* Return a list of valid settings for this driver. */
	memset(sets, CPUFREQ_VAL_UNKNOWN, sizeof(*sets) * npstates);

	for (i = 0; i < npstates; i++) {
		sets[i].freq = pstate_freqs[i];
		sets[i].spec[0] = pstate_ids[i];
		sets[i].spec[1] = i;
		sets[i].dev = dev;
	}
	*count = npstates;

	return (0);
}

static int
pmcr_set(device_t dev, const struct cf_setting *set)
{
	register_t pmcr;
	
	if (set == NULL)
		return (EINVAL);

	if (set->spec[1] < 0 || set->spec[1] >= npstates)
		return (EINVAL);

	pmcr = ((long)set->spec[0] << PMCR_LOWERPS_SHIFT) & PMCR_LOWERPS_MASK;
	pmcr |= ((long)set->spec[0] << PMCR_UPPERPS_SHIFT) & PMCR_UPPERPS_MASK;
	pmcr |= PMCR_VERSION_1;

	mtspr(SPR_PMCR, pmcr);
	powerpc_sync(); isync();

	return (0);
}

static int
pmcr_get(device_t dev, struct cf_setting *set)
{
	struct pmcr_softc *sc;
	register_t pmcr;
	int i, pstate;

	if (set == NULL)
		return (EINVAL);
	sc = device_get_softc(dev);

	memset(set, CPUFREQ_VAL_UNKNOWN, sizeof(*set));

	pmcr = mfspr(SPR_PMCR);

	pstate = (pmcr & PMCR_LOWERPS_MASK) >> PMCR_LOWERPS_SHIFT;

	for (i = 0; i < npstates && pstate_ids[i] != pstate; i++)
		;

	if (i == npstates)
		return (EINVAL);

	set->spec[0] = pstate;
	set->spec[1] = i;
	set->freq = pstate_freqs[i];

	set->dev = dev;

	return (0);
}

static int
pmcr_type(device_t dev, int *type)
{

	if (type == NULL)
		return (EINVAL);

	*type = CPUFREQ_TYPE_ABSOLUTE;
	return (0);
}

