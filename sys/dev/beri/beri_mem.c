/*-
 * Copyright (c) 2014 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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
 * BERI memory interface.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/timeet.h>
#include <sys/timetc.h>
#include <sys/conf.h>
#include <sys/uio.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/cpu.h>
#include <machine/intr.h>

struct beri_mem_softc {
	struct resource		*res[1];
	struct cdev		*mem_cdev;
	device_t		dev;
	int			mem_size;
	int			mem_start;
};

static struct resource_spec beri_mem_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};

static int
mem_open(struct cdev *dev, int flags __unused,
    int fmt __unused, struct thread *td __unused)
{
	struct beri_mem_softc *sc;

	sc = dev->si_drv1;

	return (0);
}

static int
mem_close(struct cdev *dev, int flags __unused,
    int fmt __unused, struct thread *td __unused)
{
	struct beri_mem_softc *sc;

	sc = dev->si_drv1;

	return (0);
}

static int
mem_ioctl(struct cdev *dev, u_long cmd, caddr_t addr, int flags,
    struct thread *td)
{

	return (0);
}

static int
mem_mmap(struct cdev *dev, vm_ooffset_t offset, vm_paddr_t *paddr, int nprot,
    vm_memattr_t *memattr)
{
	struct beri_mem_softc *sc;

	sc = dev->si_drv1;

	if (offset < sc->mem_size) {
		*paddr = sc->mem_start + offset;
		return (0);
        }

	return (EINVAL);
}

static struct cdevsw mem_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	mem_open,
	.d_close =	mem_close,
	.d_ioctl =	mem_ioctl,
	.d_mmap =	mem_mmap,
	.d_name =	"BERI memory",
};

static int
beri_mem_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "sri-cambridge,beri-mem"))
		return (ENXIO);

	device_set_desc(dev, "BERI memory");
	return (BUS_PROBE_DEFAULT);
}

static int
beri_mem_attach(device_t dev)
{
	struct beri_mem_softc *sc;

	sc = device_get_softc(dev);
	sc->dev = dev;

	if (bus_alloc_resources(dev, beri_mem_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	/* Memory info */
	sc->mem_size = rman_get_size(sc->res[0]);
	sc->mem_start = rman_get_start(sc->res[0]);

	sc->mem_cdev = make_dev(&mem_cdevsw, 0, UID_ROOT, GID_WHEEL,
	    0600, "beri_mem");

	if (sc->mem_cdev == NULL) {
		device_printf(dev, "Failed to create character device.\n");
		return (ENXIO);
	}

	sc->mem_cdev->si_drv1 = sc;

	return (0);
}

static device_method_t beri_mem_methods[] = {
	DEVMETHOD(device_probe,		beri_mem_probe),
	DEVMETHOD(device_attach,	beri_mem_attach),
	{ 0, 0 }
};

static driver_t beri_mem_driver = {
	"beri_mem",
	beri_mem_methods,
	sizeof(struct beri_mem_softc),
};

static devclass_t beri_mem_devclass;

DRIVER_MODULE(beri_mem, simplebus, beri_mem_driver, beri_mem_devclass, 0, 0);
