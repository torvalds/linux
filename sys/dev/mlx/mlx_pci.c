/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Michael Smith
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
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/sx.h>

#include <sys/bus.h>
#include <sys/conf.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <geom/geom_disk.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/mlx/mlxio.h>
#include <dev/mlx/mlxvar.h>
#include <dev/mlx/mlxreg.h>

static int			mlx_pci_probe(device_t dev);
static int			mlx_pci_attach(device_t dev);

static device_method_t mlx_methods[] = {
    /* Device interface */
    DEVMETHOD(device_probe,	mlx_pci_probe),
    DEVMETHOD(device_attach,	mlx_pci_attach),
    DEVMETHOD(device_detach,	mlx_detach),
    DEVMETHOD(device_shutdown,	mlx_shutdown),
    DEVMETHOD(device_suspend,	mlx_suspend),
    DEVMETHOD(device_resume,	mlx_resume),

    DEVMETHOD_END
};

static driver_t mlx_pci_driver = {
	"mlx",
	mlx_methods,
	sizeof(struct mlx_softc)
};

DRIVER_MODULE(mlx, pci, mlx_pci_driver, mlx_devclass, 0, 0);

struct mlx_ident
{
    u_int16_t	vendor;
    u_int16_t	device;
    u_int16_t	subvendor;
    u_int16_t	subdevice;
    int		iftype;
    char	*desc;
} mlx_identifiers[] = {
    {0x1069, 0x0001, 0x0000, 0x0000, MLX_IFTYPE_2, "Mylex version 2 RAID interface"},
    {0x1069, 0x0002, 0x0000, 0x0000, MLX_IFTYPE_3, "Mylex version 3 RAID interface"},
    {0x1069, 0x0010, 0x0000, 0x0000, MLX_IFTYPE_4, "Mylex version 4 RAID interface"},
    {0x1011, 0x1065, 0x1069, 0x0020, MLX_IFTYPE_5, "Mylex version 5 RAID interface"},
    {0, 0, 0, 0, 0, 0}
};

static struct mlx_ident *
mlx_pci_match(device_t dev)
{
    struct mlx_ident *m;

    for (m = mlx_identifiers; m->vendor != 0; m++) {
	if ((m->vendor == pci_get_vendor(dev)) &&
	    (m->device == pci_get_device(dev)) &&
	    ((m->subvendor == 0) || ((m->subvendor == pci_get_subvendor(dev)) &&
				     (m->subdevice == pci_get_subdevice(dev)))))
	    return (m);
    }
    return (NULL);
}

static int
mlx_pci_probe(device_t dev)
{
    struct mlx_ident	*m;

    debug_called(1);

    m = mlx_pci_match(dev);
    if (m != NULL) {
	device_set_desc(dev, m->desc);
	return(BUS_PROBE_DEFAULT);
    }
    return(ENXIO);
}

static int
mlx_pci_attach(device_t dev)
{
    struct mlx_softc	*sc;
    struct mlx_ident	*m;
    int			error;

    debug_called(1);

    pci_enable_busmaster(dev);

    sc = device_get_softc(dev);
    sc->mlx_dev = dev;

    /*
     * Work out what sort of adapter this is (we need to know this in order
     * to map the appropriate interface resources).
     */
    m = mlx_pci_match(dev);
    if (m == NULL)		/* shouldn't happen */
	return(ENXIO);
    sc->mlx_iftype = m->iftype;

    mtx_init(&sc->mlx_io_lock, "mlx I/O", NULL, MTX_DEF);
    sx_init(&sc->mlx_config_lock, "mlx config");
    callout_init_mtx(&sc->mlx_timeout, &sc->mlx_io_lock, 0);

    /*
     * Allocate the PCI register window.
     */
    
    /* type 2/3 adapters have an I/O region we don't prefer at base 0 */
    switch(sc->mlx_iftype) {
    case MLX_IFTYPE_2:
    case MLX_IFTYPE_3:
	sc->mlx_mem_type = SYS_RES_MEMORY;
	sc->mlx_mem_rid = MLX_CFG_BASE1;
	sc->mlx_mem = bus_alloc_resource_any(dev, sc->mlx_mem_type,
		&sc->mlx_mem_rid, RF_ACTIVE);
	if (sc->mlx_mem == NULL) {
	    sc->mlx_mem_type = SYS_RES_IOPORT;
	    sc->mlx_mem_rid = MLX_CFG_BASE0;
	    sc->mlx_mem = bus_alloc_resource_any(dev, sc->mlx_mem_type,
		&sc->mlx_mem_rid, RF_ACTIVE);
	}
	break;
    case MLX_IFTYPE_4:
    case MLX_IFTYPE_5:
	sc->mlx_mem_type = SYS_RES_MEMORY;
	sc->mlx_mem_rid = MLX_CFG_BASE0;
	sc->mlx_mem = bus_alloc_resource_any(dev, sc->mlx_mem_type,
		&sc->mlx_mem_rid, RF_ACTIVE);
	break;
    }
    if (sc->mlx_mem == NULL) {
	device_printf(sc->mlx_dev, "couldn't allocate mailbox window\n");
	mlx_free(sc);
	return(ENXIO);
    }

    /*
     * Allocate the parent bus DMA tag appropriate for PCI.
     */
    error = bus_dma_tag_create(bus_get_dma_tag(dev),	/* PCI parent */
			       1, 0, 			/* alignment, boundary */
			       BUS_SPACE_MAXADDR_32BIT, /* lowaddr */
			       BUS_SPACE_MAXADDR, 	/* highaddr */
			       NULL, NULL, 		/* filter, filterarg */
			       BUS_SPACE_MAXSIZE_32BIT, /* maxsize */
			       BUS_SPACE_UNRESTRICTED,	/* nsegments */
			       BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
			       BUS_DMA_ALLOCNOW,	/* flags */
			       NULL,			/* lockfunc */
			       NULL,			/* lockarg */
			       &sc->mlx_parent_dmat);
    if (error != 0) {
	device_printf(dev, "can't allocate parent DMA tag\n");
	mlx_free(sc);
	return(ENOMEM);
    }

    /*
     * Do bus-independant initialisation.
     */
    error = mlx_attach(sc);
    if (error != 0) {
	mlx_free(sc);
	return(error);
    }
    
    /*
     * Start the controller.
     */
    mlx_startup(sc);
    return(0);
}
