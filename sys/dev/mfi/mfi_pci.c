/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD AND BSD-2-Clause
 *
 * Copyright (c) 2006 IronPort Systems
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
/*-
 * Copyright (c) 2007 LSI Corp.
 * Copyright (c) 2007 Rajesh Prabhakaran.
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

/* PCI/PCI-X/PCIe bus interface for the LSI MegaSAS controllers */

#include "opt_mfi.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/selinfo.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/bio.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/uio.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/mfi/mfireg.h>
#include <dev/mfi/mfi_ioctl.h>
#include <dev/mfi/mfivar.h>

static int	mfi_pci_probe(device_t);
static int	mfi_pci_attach(device_t);
static int	mfi_pci_detach(device_t);
static int	mfi_pci_suspend(device_t);
static int	mfi_pci_resume(device_t);
static void	mfi_pci_free(struct mfi_softc *);

static device_method_t mfi_methods[] = {
	DEVMETHOD(device_probe,		mfi_pci_probe),
	DEVMETHOD(device_attach,	mfi_pci_attach),
	DEVMETHOD(device_detach,	mfi_pci_detach),
	DEVMETHOD(device_suspend,	mfi_pci_suspend),
	DEVMETHOD(device_resume,	mfi_pci_resume),

	DEVMETHOD_END
};

static driver_t mfi_pci_driver = {
	"mfi",
	mfi_methods,
	sizeof(struct mfi_softc)
};

static devclass_t	mfi_devclass;

static int	mfi_msi = 1;
SYSCTL_INT(_hw_mfi, OID_AUTO, msi, CTLFLAG_RDTUN, &mfi_msi, 0,
    "Enable use of MSI interrupts");

static int	mfi_mrsas_enable;
SYSCTL_INT(_hw_mfi, OID_AUTO, mrsas_enable, CTLFLAG_RDTUN, &mfi_mrsas_enable,
     0, "Allow mrasas to take newer cards");

struct mfi_ident {
	uint16_t	vendor;
	uint16_t	device;
	uint16_t	subvendor;
	uint16_t	subdevice;
	int		flags;
	const char	*desc;
} mfi_identifiers[] = {
	{0x1000, 0x005b, 0x1028, 0x1f2d, MFI_FLAGS_SKINNY| MFI_FLAGS_TBOLT| MFI_FLAGS_MRSAS, "Dell PERC H810 Adapter"},
	{0x1000, 0x005b, 0x1028, 0x1f30, MFI_FLAGS_SKINNY| MFI_FLAGS_TBOLT| MFI_FLAGS_MRSAS, "Dell PERC H710 Embedded"},
	{0x1000, 0x005b, 0x1028, 0x1f31, MFI_FLAGS_SKINNY| MFI_FLAGS_TBOLT| MFI_FLAGS_MRSAS, "Dell PERC H710P Adapter"},
	{0x1000, 0x005b, 0x1028, 0x1f33, MFI_FLAGS_SKINNY| MFI_FLAGS_TBOLT| MFI_FLAGS_MRSAS, "Dell PERC H710P Mini (blades)"},
	{0x1000, 0x005b, 0x1028, 0x1f34, MFI_FLAGS_SKINNY| MFI_FLAGS_TBOLT| MFI_FLAGS_MRSAS, "Dell PERC H710P Mini (monolithics)"},
	{0x1000, 0x005b, 0x1028, 0x1f35, MFI_FLAGS_SKINNY| MFI_FLAGS_TBOLT| MFI_FLAGS_MRSAS, "Dell PERC H710 Adapter"},
	{0x1000, 0x005b, 0x1028, 0x1f37, MFI_FLAGS_SKINNY| MFI_FLAGS_TBOLT| MFI_FLAGS_MRSAS, "Dell PERC H710 Mini (blades)"},
	{0x1000, 0x005b, 0x1028, 0x1f38, MFI_FLAGS_SKINNY| MFI_FLAGS_TBOLT| MFI_FLAGS_MRSAS, "Dell PERC H710 Mini (monolithics)"},
	{0x1000, 0x005b, 0x8086, 0x9265, MFI_FLAGS_SKINNY| MFI_FLAGS_TBOLT| MFI_FLAGS_MRSAS, "Intel (R) RAID Controller RS25DB080"},
	{0x1000, 0x005b, 0x8086, 0x9285, MFI_FLAGS_SKINNY| MFI_FLAGS_TBOLT| MFI_FLAGS_MRSAS, "Intel (R) RAID Controller RS25NB008"},
	{0x1000, 0x005b, 0xffff, 0xffff, MFI_FLAGS_SKINNY| MFI_FLAGS_TBOLT| MFI_FLAGS_MRSAS, "ThunderBolt"},
	{0x1000, 0x005d, 0xffff, 0xffff, MFI_FLAGS_SKINNY| MFI_FLAGS_TBOLT| MFI_FLAGS_MRSAS| MFI_FLAGS_INVADER, "Invader"},
	{0x1000, 0x005f, 0xffff, 0xffff, MFI_FLAGS_SKINNY| MFI_FLAGS_TBOLT| MFI_FLAGS_MRSAS| MFI_FLAGS_FURY, "Fury"},
	{0x1000, 0x0060, 0x1028, 0xffff, MFI_FLAGS_1078,  "Dell PERC 6"},
	{0x1000, 0x0060, 0xffff, 0xffff, MFI_FLAGS_1078,  "LSI MegaSAS 1078"},
	{0x1000, 0x0071, 0xffff, 0xffff, MFI_FLAGS_SKINNY, "Drake Skinny"},
	{0x1000, 0x0073, 0xffff, 0xffff, MFI_FLAGS_SKINNY, "Drake Skinny"},
	{0x1000, 0x0078, 0xffff, 0xffff, MFI_FLAGS_GEN2,  "LSI MegaSAS Gen2"},
	{0x1000, 0x0079, 0x1028, 0x1f15, MFI_FLAGS_GEN2,  "Dell PERC H800 Adapter"},
	{0x1000, 0x0079, 0x1028, 0x1f16, MFI_FLAGS_GEN2,  "Dell PERC H700 Adapter"},
	{0x1000, 0x0079, 0x1028, 0x1f17, MFI_FLAGS_GEN2,  "Dell PERC H700 Integrated"},
	{0x1000, 0x0079, 0x1028, 0x1f18, MFI_FLAGS_GEN2,  "Dell PERC H700 Modular"},
	{0x1000, 0x0079, 0x1028, 0x1f19, MFI_FLAGS_GEN2,  "Dell PERC H700"},
	{0x1000, 0x0079, 0x1028, 0x1f1a, MFI_FLAGS_GEN2,  "Dell PERC H800 Proto Adapter"},
	{0x1000, 0x0079, 0x1028, 0x1f1b, MFI_FLAGS_GEN2,  "Dell PERC H800"},
	{0x1000, 0x0079, 0x1028, 0xffff, MFI_FLAGS_GEN2,  "Dell PERC Gen2"},
	{0x1000, 0x0079, 0xffff, 0xffff, MFI_FLAGS_GEN2,  "LSI MegaSAS Gen2"},
	{0x1000, 0x007c, 0xffff, 0xffff, MFI_FLAGS_1078,  "LSI MegaSAS 1078"},
	{0x1000, 0x0411, 0xffff, 0xffff, MFI_FLAGS_1064R, "LSI MegaSAS 1064R"}, /* Brocton IOP */
	{0x1000, 0x0413, 0xffff, 0xffff, MFI_FLAGS_1064R, "LSI MegaSAS 1064R"}, /* Verde ZCR */
	{0x1028, 0x0015, 0xffff, 0xffff, MFI_FLAGS_1064R, "Dell PERC 5/i"},
	{0, 0, 0, 0, 0, NULL}
};

DRIVER_MODULE(mfi, pci, mfi_pci_driver, mfi_devclass, 0, 0);
MODULE_PNP_INFO("U16:vendor;U16:device;U16:subvendor;U16:subdevice", pci, mfi,
    mfi_identifiers, nitems(mfi_identifiers) - 1);
MODULE_VERSION(mfi, 1);

static struct mfi_ident *
mfi_find_ident(device_t dev)
{
	struct mfi_ident *m;

	for (m = mfi_identifiers; m->vendor != 0; m++) {
		if ((m->vendor == pci_get_vendor(dev)) &&
		    (m->device == pci_get_device(dev)) &&
		    ((m->subvendor == pci_get_subvendor(dev)) ||
		    (m->subvendor == 0xffff)) &&
		    ((m->subdevice == pci_get_subdevice(dev)) ||
		    (m->subdevice == 0xffff)))
			return (m);
	}

	return (NULL);
}

static int
mfi_pci_probe(device_t dev)
{
	struct mfi_ident *id;

	if ((id = mfi_find_ident(dev)) != NULL) {
		device_set_desc(dev, id->desc);

		/* give priority to mrsas if tunable set */
		if ((id->flags & MFI_FLAGS_MRSAS) && mfi_mrsas_enable)
			return (BUS_PROBE_LOW_PRIORITY);
		else
			return (BUS_PROBE_DEFAULT);
	}
	return (ENXIO);
}

static int
mfi_pci_attach(device_t dev)
{
	struct mfi_softc *sc;
	struct mfi_ident *m;
	int count, error;

	sc = device_get_softc(dev);
	bzero(sc, sizeof(*sc));
	sc->mfi_dev = dev;
	m = mfi_find_ident(dev);
	sc->mfi_flags = m->flags;

	/* Ensure busmastering is enabled */
	pci_enable_busmaster(dev);

	/* Allocate PCI registers */
	if ((sc->mfi_flags & MFI_FLAGS_1064R) ||
	    (sc->mfi_flags & MFI_FLAGS_1078)) {
		/* 1068/1078: Memory mapped BAR is at offset 0x10 */
		sc->mfi_regs_rid = PCIR_BAR(0);
	}
	else if ((sc->mfi_flags & MFI_FLAGS_GEN2) ||
		 (sc->mfi_flags & MFI_FLAGS_SKINNY) ||
		(sc->mfi_flags & MFI_FLAGS_TBOLT)) { 
		/* Gen2/Skinny: Memory mapped BAR is at offset 0x14 */
		sc->mfi_regs_rid = PCIR_BAR(1);
	}
	if ((sc->mfi_regs_resource = bus_alloc_resource_any(sc->mfi_dev,
	    SYS_RES_MEMORY, &sc->mfi_regs_rid, RF_ACTIVE)) == NULL) {
		device_printf(dev, "Cannot allocate PCI registers\n");
		return (ENXIO);
	}
	sc->mfi_btag = rman_get_bustag(sc->mfi_regs_resource);
	sc->mfi_bhandle = rman_get_bushandle(sc->mfi_regs_resource);

	error = ENOMEM;

	/* Allocate parent DMA tag */
	if (bus_dma_tag_create(	bus_get_dma_tag(dev),	/* PCI parent */
				1, 0,			/* algnmnt, boundary */
				BUS_SPACE_MAXADDR,	/* lowaddr */
				BUS_SPACE_MAXADDR,	/* highaddr */
				NULL, NULL,		/* filter, filterarg */
				BUS_SPACE_MAXSIZE_32BIT,/* maxsize */
				BUS_SPACE_UNRESTRICTED,	/* nsegments */
				BUS_SPACE_MAXSIZE_32BIT,/* maxsegsize */
				0,			/* flags */
				NULL, NULL,		/* lockfunc, lockarg */
				&sc->mfi_parent_dmat)) {
		device_printf(dev, "Cannot allocate parent DMA tag\n");
		goto out;
	}

	/* Allocate IRQ resource. */
	sc->mfi_irq_rid = 0;
	count = 1;
	if (mfi_msi && pci_alloc_msi(sc->mfi_dev, &count) == 0) {
		device_printf(sc->mfi_dev, "Using MSI\n");
		sc->mfi_irq_rid = 1;
	}
	if ((sc->mfi_irq = bus_alloc_resource_any(sc->mfi_dev, SYS_RES_IRQ,
	    &sc->mfi_irq_rid, RF_SHAREABLE | RF_ACTIVE)) == NULL) {
		device_printf(sc->mfi_dev, "Cannot allocate interrupt\n");
		error = EINVAL;
		goto out;
	}

	error = mfi_attach(sc);
out:
	if (error) {
		mfi_free(sc);
		mfi_pci_free(sc);
	}

	return (error);
}

static int
mfi_pci_detach(device_t dev)
{
	struct mfi_softc *sc;
	int error, devcount, i;
	device_t *devlist;

	sc = device_get_softc(dev);

	sx_xlock(&sc->mfi_config_lock);
	mtx_lock(&sc->mfi_io_lock);
	if ((sc->mfi_flags & MFI_FLAGS_OPEN) != 0) {
		mtx_unlock(&sc->mfi_io_lock);
		sx_xunlock(&sc->mfi_config_lock);
		return (EBUSY);
	}
	sc->mfi_detaching = 1;
	mtx_unlock(&sc->mfi_io_lock);

	if ((error = device_get_children(sc->mfi_dev, &devlist, &devcount)) != 0) {
		sx_xunlock(&sc->mfi_config_lock);
		return error;
	}
	for (i = 0; i < devcount; i++)
		device_delete_child(sc->mfi_dev, devlist[i]);
	free(devlist, M_TEMP);
	sx_xunlock(&sc->mfi_config_lock);

	EVENTHANDLER_DEREGISTER(shutdown_final, sc->mfi_eh);

	mfi_shutdown(sc);
	mfi_free(sc);
	mfi_pci_free(sc);
	return (0);
}

static void
mfi_pci_free(struct mfi_softc *sc)
{

	if (sc->mfi_regs_resource != NULL) {
		bus_release_resource(sc->mfi_dev, SYS_RES_MEMORY,
		    sc->mfi_regs_rid, sc->mfi_regs_resource);
	}
	if (sc->mfi_irq_rid != 0)
		pci_release_msi(sc->mfi_dev);

	return;
}

static int
mfi_pci_suspend(device_t dev)
{

	return (EINVAL);
}

static int
mfi_pci_resume(device_t dev)
{

	return (EINVAL);
}
