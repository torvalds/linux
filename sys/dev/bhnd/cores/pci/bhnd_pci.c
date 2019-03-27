/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015 Landon Fuller <landon@landonf.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Broadcom Common PCI/PCIe Support.
 * 
 * This base driver implementation is shared by the bhnd_pcib (root complex)
 * and bhnd_pci_hostb (host bridge) drivers.
 */

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <dev/bhnd/bhnd.h>
#include <dev/mdio/mdio.h>

#include "bhnd_pcireg.h"
#include "bhnd_pcivar.h"

static int	bhnd_pcie_mdio_wait_idle(struct bhnd_pci_softc *sc);
static int	bhnd_pcie_mdio_ioctl(struct bhnd_pci_softc *sc, uint32_t cmd);
static int	bhnd_pcie_mdio_enable(struct bhnd_pci_softc *sc);
static void	bhnd_pcie_mdio_disable(struct bhnd_pci_softc *sc);
static int	bhnd_pcie_mdio_cmd_write(struct bhnd_pci_softc *sc,
		    uint32_t cmd);
static int	bhnd_pcie_mdio_cmd_read(struct bhnd_pci_softc *sc, uint32_t cmd,
		    uint16_t *data_read);

static struct bhnd_device_quirk bhnd_pci_quirks[];
static struct bhnd_device_quirk bhnd_pcie_quirks[];

#define	BHND_PCI_QUIRKS		bhnd_pci_quirks
#define	BHND_PCIE_QUIRKS	bhnd_pcie_quirks
#define	BHND_PCI_DEV(_core, _desc, ...)					\
	{ BHND_DEVICE(BCM, _core, _desc, BHND_ ## _core ## _QUIRKS,	\
	    ## __VA_ARGS__), BHND_PCI_REGFMT_ ## _core }

static const struct bhnd_pci_device {
	struct bhnd_device	device;
	bhnd_pci_regfmt_t	regfmt;	/**< register format */
} bhnd_pci_devs[] = {
	BHND_PCI_DEV(PCI,	"Host-PCI bridge",		BHND_DF_HOSTB),	     
	BHND_PCI_DEV(PCI,	"PCI-BHND bridge",		BHND_DF_SOC),
	BHND_PCI_DEV(PCIE,	"PCIe-G1 Host-PCI bridge",	BHND_DF_HOSTB),
	BHND_PCI_DEV(PCIE,	"PCIe-G1 PCI-BHND bridge",	BHND_DF_SOC),

	{ BHND_DEVICE_END, 0 }
};

/* Device quirks tables */
static struct bhnd_device_quirk bhnd_pci_quirks[] = { BHND_DEVICE_QUIRK_END };
static struct bhnd_device_quirk bhnd_pcie_quirks[] = {
	BHND_CORE_QUIRK(HWREV_GTE(10),	BHND_PCI_QUIRK_SD_C22_EXTADDR),

	BHND_DEVICE_QUIRK_END
};

#define	BHND_PCIE_MDIO_CTL_DELAY	10	/**< usec delay required between
						  *  MDIO_CTL/MDIO_DATA accesses. */
#define	BHND_PCIE_MDIO_RETRY_DELAY	2000	/**< usec delay before retrying
						  *  BHND_PCIE_MDIOCTL_DONE. */
#define	BHND_PCIE_MDIO_RETRY_COUNT	200	/**< number of times to loop waiting
						  *  for BHND_PCIE_MDIOCTL_DONE. */

#define	BHND_PCI_READ_4(_sc, _reg)		\
	bhnd_bus_read_4((_sc)->mem_res, (_reg))
#define	BHND_PCI_WRITE_4(_sc, _reg, _val)	\
	bhnd_bus_write_4((_sc)->mem_res, (_reg), (_val))

#define	BHND_PCIE_ASSERT(sc)	\
	KASSERT(bhnd_get_class(sc->dev) == BHND_DEVCLASS_PCIE,	\
	    ("not a pcie device!"));

int
bhnd_pci_generic_probe(device_t dev)
{
	const struct bhnd_device	*id;

	id = bhnd_device_lookup(dev, &bhnd_pci_devs[0].device,
	    sizeof(bhnd_pci_devs[0]));
	if (id == NULL)
		return (ENXIO);

	bhnd_set_custom_core_desc(dev, id->desc);
	return (BUS_PROBE_DEFAULT);
}

int
bhnd_pci_generic_attach(device_t dev)
{
	struct bhnd_pci_softc	*sc;
	int			 error;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->quirks = bhnd_device_quirks(dev, &bhnd_pci_devs[0].device,
	    sizeof(bhnd_pci_devs[0]));

	/* Allocate bus resources */
	sc->mem_res = bhnd_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->mem_rid,
	    RF_ACTIVE);
	if (sc->mem_res == NULL)
		return (ENXIO);

	BHND_PCI_LOCK_INIT(sc);

	/* Probe and attach children */
	if ((error = bus_generic_attach(dev)))
		goto cleanup;

	return (0);

cleanup:
	bhnd_release_resource(dev, SYS_RES_MEMORY, sc->mem_rid, sc->mem_res);
	BHND_PCI_LOCK_DESTROY(sc);

	return (error);
}

int
bhnd_pci_generic_detach(device_t dev)
{
	struct bhnd_pci_softc	*sc;
	int			 error;

	sc = device_get_softc(dev);

	if ((error = bus_generic_detach(dev)))
		return (error);

	bhnd_release_resource(dev, SYS_RES_MEMORY, sc->mem_rid, sc->mem_res);
	
	BHND_PCI_LOCK_DESTROY(sc);

	return (0);
}

static struct resource_list *
bhnd_pci_get_resource_list(device_t dev, device_t child)
{
	struct bhnd_pci_devinfo *dinfo;

	if (device_get_parent(child) != dev)
		return (NULL);

	dinfo = device_get_ivars(child);
	return (&dinfo->resources);
}

static device_t
bhnd_pci_add_child(device_t dev, u_int order, const char *name, int unit)
{
	struct bhnd_pci_devinfo	*dinfo;
	device_t		 child;
	
	child = device_add_child_ordered(dev, order, name, unit);
	if (child == NULL)
		return (NULL);

	dinfo = malloc(sizeof(struct bhnd_pci_devinfo), M_DEVBUF, M_NOWAIT);
	if (dinfo == NULL) {
		device_delete_child(dev, child);
		return (NULL);
	}

	resource_list_init(&dinfo->resources);
	
	device_set_ivars(child, dinfo);
	return (child);
}

static void
bhnd_pci_child_deleted(device_t dev, device_t child)
{
	struct bhnd_pci_devinfo *dinfo;

	if (device_get_parent(child) != dev)
		return;

	dinfo = device_get_ivars(child);
	if (dinfo != NULL) {
		resource_list_free(&dinfo->resources);
		free(dinfo, M_DEVBUF);
	}

	device_set_ivars(child, NULL);
}

int
bhnd_pci_generic_suspend(device_t dev)
{
	return (bus_generic_suspend(dev));
}

int
bhnd_pci_generic_resume(device_t dev)
{
	return (bus_generic_resume(dev));
}

/**
 * Read a 32-bit PCIe TLP/DLLP/PLP protocol register.
 * 
 * @param sc The bhndb_pci driver state.
 * @param addr The protocol register offset.
 */
uint32_t
bhnd_pcie_read_proto_reg(struct bhnd_pci_softc *sc, uint32_t addr)
{
	uint32_t val;

	BHND_PCIE_ASSERT(sc);

	BHND_PCI_LOCK(sc);
	BHND_PCI_WRITE_4(sc, BHND_PCIE_IND_ADDR, addr);
	val = BHND_PCI_READ_4(sc, BHND_PCIE_IND_DATA);
	BHND_PCI_UNLOCK(sc);

	return (val);
}

/**
 * Write a 32-bit PCIe TLP/DLLP/PLP protocol register value.
 * 
 * @param sc The bhndb_pci driver state.
 * @param addr The protocol register offset.
 * @param val The value to write to @p addr.
 */
void
bhnd_pcie_write_proto_reg(struct bhnd_pci_softc *sc, uint32_t addr,
    uint32_t val)
{
	BHND_PCIE_ASSERT(sc);

	BHND_PCI_LOCK(sc);
	BHND_PCI_WRITE_4(sc, BHND_PCIE_IND_ADDR, addr);
	BHND_PCI_WRITE_4(sc, BHND_PCIE_IND_DATA, val);
	BHND_PCI_UNLOCK(sc);
}

/* Spin until the MDIO device reports itself as idle, or timeout is reached. */
static int
bhnd_pcie_mdio_wait_idle(struct bhnd_pci_softc *sc)
{
	uint32_t ctl;

	/* Spin waiting for the BUSY flag to clear */
	for (int i = 0; i < BHND_PCIE_MDIO_RETRY_COUNT; i++) {
		ctl = BHND_PCI_READ_4(sc, BHND_PCIE_MDIO_CTL);
		if ((ctl & BHND_PCIE_MDIOCTL_DONE))
			return (0);

		DELAY(BHND_PCIE_MDIO_RETRY_DELAY);
	}

	return (ETIMEDOUT);
}


/**
 * Write an MDIO IOCTL and wait for completion.
 */
static int
bhnd_pcie_mdio_ioctl(struct bhnd_pci_softc *sc, uint32_t cmd)
{
	BHND_PCI_LOCK_ASSERT(sc, MA_OWNED);

	BHND_PCI_WRITE_4(sc, BHND_PCIE_MDIO_CTL, cmd);
	DELAY(BHND_PCIE_MDIO_CTL_DELAY);
	return (0);
}

/**
 * Enable MDIO device
 */
static int
bhnd_pcie_mdio_enable(struct bhnd_pci_softc *sc)
{
	uint32_t ctl;

	BHND_PCIE_ASSERT(sc);

	/* Enable MDIO clock and preamble mode */
	ctl = BHND_PCIE_MDIOCTL_PREAM_EN|BHND_PCIE_MDIOCTL_DIVISOR_VAL;
	return (bhnd_pcie_mdio_ioctl(sc, ctl));
}

/**
 * Disable MDIO device.
 */
static void
bhnd_pcie_mdio_disable(struct bhnd_pci_softc *sc)
{
	if (bhnd_pcie_mdio_ioctl(sc, 0))
		device_printf(sc->dev, "failed to disable MDIO clock\n");
}


/**
 * Issue a write command and wait for completion
 */
static int
bhnd_pcie_mdio_cmd_write(struct bhnd_pci_softc *sc, uint32_t cmd)
{
	int error;

	BHND_PCI_LOCK_ASSERT(sc, MA_OWNED);

	cmd |= BHND_PCIE_MDIODATA_START|BHND_PCIE_MDIODATA_TA|BHND_PCIE_MDIODATA_CMD_WRITE;

	BHND_PCI_WRITE_4(sc, BHND_PCIE_MDIO_DATA, cmd);
	DELAY(BHND_PCIE_MDIO_CTL_DELAY);

	if ((error = bhnd_pcie_mdio_wait_idle(sc)))
		return (error);

	return (0);
}

/**
 * Issue an an MDIO read command, wait for completion, and return
 * the result in @p data_read.
 */
static int
bhnd_pcie_mdio_cmd_read(struct bhnd_pci_softc *sc, uint32_t cmd,
    uint16_t *data_read)
{
	int error;

	BHND_PCI_LOCK_ASSERT(sc, MA_OWNED);

	cmd |= BHND_PCIE_MDIODATA_START|BHND_PCIE_MDIODATA_TA|BHND_PCIE_MDIODATA_CMD_READ;
	BHND_PCI_WRITE_4(sc, BHND_PCIE_MDIO_DATA, cmd);
	DELAY(BHND_PCIE_MDIO_CTL_DELAY);

	if ((error = bhnd_pcie_mdio_wait_idle(sc)))
		return (error);

	*data_read = (BHND_PCI_READ_4(sc, BHND_PCIE_MDIO_DATA) & 
	    BHND_PCIE_MDIODATA_DATA_MASK);
	return (0);
}


int
bhnd_pcie_mdio_read(struct bhnd_pci_softc *sc, int phy, int reg)
{
	uint32_t	cmd;
	uint16_t	val;
	int		error;

	/* Enable MDIO access */
	BHND_PCI_LOCK(sc);
	bhnd_pcie_mdio_enable(sc);

	/* Issue the read */
	cmd = BHND_PCIE_MDIODATA_ADDR(phy, reg);
	error = bhnd_pcie_mdio_cmd_read(sc, cmd, &val);

	/* Disable MDIO access */
	bhnd_pcie_mdio_disable(sc);
	BHND_PCI_UNLOCK(sc);

	if (error)
		return (~0U);

	return (val);
}

int
bhnd_pcie_mdio_write(struct bhnd_pci_softc *sc, int phy, int reg, int val)
{
	uint32_t	cmd;
	int		error;

	/* Enable MDIO access */
	BHND_PCI_LOCK(sc);
	bhnd_pcie_mdio_enable(sc);

	/* Issue the write */
	cmd = BHND_PCIE_MDIODATA_ADDR(phy, reg) | (val & BHND_PCIE_MDIODATA_DATA_MASK);
	error = bhnd_pcie_mdio_cmd_write(sc, cmd);

	/* Disable MDIO access */
	bhnd_pcie_mdio_disable(sc);
	BHND_PCI_UNLOCK(sc);

	return (error);
}

int
bhnd_pcie_mdio_read_ext(struct bhnd_pci_softc *sc, int phy, int devaddr,
    int reg)
{
	uint32_t	cmd;
	uint16_t	val;
	int		error;

	if (devaddr == MDIO_DEVADDR_NONE)
		return (bhnd_pcie_mdio_read(sc, phy, reg));

	/* Extended register access is only supported for the SerDes device,
	 * using the non-standard C22 extended address mechanism */
	if (!(sc->quirks & BHND_PCI_QUIRK_SD_C22_EXTADDR) ||
	    phy != BHND_PCIE_PHYADDR_SD)
	{
		return (~0U);	
	}

	/* Enable MDIO access */
	BHND_PCI_LOCK(sc);
	bhnd_pcie_mdio_enable(sc);

	/* Write the block address to the address extension register */
	cmd = BHND_PCIE_MDIODATA_ADDR(phy, BHND_PCIE_SD_ADDREXT) | devaddr;
	if ((error = bhnd_pcie_mdio_cmd_write(sc, cmd)))
		goto cleanup;

	/* Issue the read */
	cmd = BHND_PCIE_MDIODATA_ADDR(phy, reg);
	error = bhnd_pcie_mdio_cmd_read(sc, cmd, &val);

cleanup:
	bhnd_pcie_mdio_disable(sc);
	BHND_PCI_UNLOCK(sc);

	if (error)
		return (~0U);

	return (val);
}

int
bhnd_pcie_mdio_write_ext(struct bhnd_pci_softc *sc, int phy, int devaddr,
    int reg, int val)
{	
	uint32_t	cmd;
	int		error;

	if (devaddr == MDIO_DEVADDR_NONE)
		return (bhnd_pcie_mdio_write(sc, phy, reg, val));

	/* Extended register access is only supported for the SerDes device,
	 * using the non-standard C22 extended address mechanism */
	if (!(sc->quirks & BHND_PCI_QUIRK_SD_C22_EXTADDR) ||
	    phy != BHND_PCIE_PHYADDR_SD)
	{
		return (~0U);	
	}

	/* Enable MDIO access */
	BHND_PCI_LOCK(sc);
	bhnd_pcie_mdio_enable(sc);

	/* Write the block address to the address extension register */
	cmd = BHND_PCIE_MDIODATA_ADDR(phy, BHND_PCIE_SD_ADDREXT) | devaddr;
	if ((error = bhnd_pcie_mdio_cmd_write(sc, cmd)))
		goto cleanup;

	/* Issue the write */
	cmd = BHND_PCIE_MDIODATA_ADDR(phy, reg) |
	    (val & BHND_PCIE_MDIODATA_DATA_MASK);
	error = bhnd_pcie_mdio_cmd_write(sc, cmd);

cleanup:
	bhnd_pcie_mdio_disable(sc);
	BHND_PCI_UNLOCK(sc);

	return (error);
}

static device_method_t bhnd_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,			bhnd_pci_generic_probe),
	DEVMETHOD(device_attach,		bhnd_pci_generic_attach),
	DEVMETHOD(device_detach,		bhnd_pci_generic_detach),
	DEVMETHOD(device_suspend,		bhnd_pci_generic_suspend),
	DEVMETHOD(device_resume,		bhnd_pci_generic_resume),

	/* Bus interface */
	DEVMETHOD(bus_add_child,		bhnd_pci_add_child),
	DEVMETHOD(bus_child_deleted,		bhnd_pci_child_deleted),
	DEVMETHOD(bus_print_child,		bus_generic_print_child),
	DEVMETHOD(bus_get_resource_list,	bhnd_pci_get_resource_list),
	DEVMETHOD(bus_get_resource,		bus_generic_rl_get_resource),
	DEVMETHOD(bus_set_resource,		bus_generic_rl_set_resource),
	DEVMETHOD(bus_delete_resource,		bus_generic_rl_delete_resource),

	DEVMETHOD(bus_alloc_resource,		bus_generic_rl_alloc_resource),
	DEVMETHOD(bus_activate_resource,        bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource,      bus_generic_deactivate_resource),
	DEVMETHOD(bus_adjust_resource,          bus_generic_adjust_resource),
	DEVMETHOD(bus_release_resource,		bus_generic_rl_release_resource),
	
	DEVMETHOD_END
};

DEFINE_CLASS_0(bhnd_pci, bhnd_pci_driver, bhnd_pci_methods, sizeof(struct bhnd_pci_softc));
MODULE_DEPEND(bhnd_pci, bhnd, 1, 1, 1);
MODULE_DEPEND(bhnd_pci, pci, 1, 1, 1);
MODULE_VERSION(bhnd_pci, 1);
