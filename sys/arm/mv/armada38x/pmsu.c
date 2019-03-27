/*-
 * Copyright (c) 2015 Semihalf.
 * Copyright (c) 2015 Stormshield.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/rman.h>
#include <sys/types.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/resource.h>
#include <sys/systm.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/cpu.h>
#include <machine/fdt.h>
#include <machine/smp.h>

#include <dev/ofw/ofw_bus_subr.h>

#include <arm/mv/mvreg.h>

#include "pmsu.h"

static struct resource_spec pmsu_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};

struct pmsu_softc {
	device_t	dev;
	struct resource	*res;
};

static int pmsu_probe(device_t dev);
static int pmsu_attach(device_t dev);
static int pmsu_detach(device_t dev);

static device_method_t pmsu_methods[] = {
	DEVMETHOD(device_probe,		pmsu_probe),
	DEVMETHOD(device_attach,	pmsu_attach),
	DEVMETHOD(device_detach,	pmsu_detach),

	{ 0, 0 }
};

static driver_t pmsu_driver = {
	"pmsu",
	pmsu_methods,
	sizeof(struct pmsu_softc)
};

static devclass_t pmsu_devclass;

DRIVER_MODULE(pmsu, simplebus, pmsu_driver, pmsu_devclass, 0, 0);
DRIVER_MODULE(pmsu, ofwbus, pmsu_driver, pmsu_devclass, 0, 0);

static int
pmsu_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "marvell,armada-380-pmsu"))
		return (ENXIO);

	device_set_desc(dev, "Power Management Service Unit");

	return (BUS_PROBE_DEFAULT);
}

static int
pmsu_attach(device_t dev)
{
	struct pmsu_softc *sc;
	int err;

	sc = device_get_softc(dev);
	sc->dev = dev;

	err = bus_alloc_resources(dev, pmsu_spec, &sc->res);
	if (err != 0) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	return (0);
}

static int
pmsu_detach(device_t dev)
{
	struct pmsu_softc *sc;

	sc = device_get_softc(dev);

	bus_release_resources(dev, pmsu_spec, &sc->res);

	return (0);
}

#ifdef SMP
int
pmsu_boot_secondary_cpu(void)
{
	bus_space_handle_t vaddr;
	int rv;

	rv = bus_space_map(fdtbus_bs_tag, (bus_addr_t)MV_PMSU_BASE, MV_PMSU_REGS_LEN,
	    0, &vaddr);
	if (rv != 0)
		return (rv);

	/* Boot cpu1 */
	bus_space_write_4(fdtbus_bs_tag, vaddr, PMSU_BOOT_ADDR_REDIRECT_OFFSET(1),
	    pmap_kextract((vm_offset_t)mpentry));

	dcache_wbinv_poc_all();
	dsb();
	sev();

	bus_space_unmap(fdtbus_bs_tag, vaddr, MV_PMSU_REGS_LEN);

	return (0);
}
#endif
