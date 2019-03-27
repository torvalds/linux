/*-
 * PCI specific probe and attach routines for LSI Fusion Adapters
 * FreeBSD Version.
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD AND BSD-3-Clause
 *
 * Copyright (c) 2000, 2001 by Greg Ansley
 * Partially derived from Matt Jacob's ISP driver.
 * Copyright (c) 1997, 1998, 1999, 2000, 2001, 2002 by Matthew Jacob
 * Feral Software
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*-
 * Copyright (c) 2002, 2006 by Matthew Jacob
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon including
 *    a substantially similar Disclaimer requirement for further binary
 *    redistribution.
 * 3. Neither the names of the above listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF THE COPYRIGHT
 * OWNER OR CONTRIBUTOR IS ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Support from Chris Ellsworth in order to make SAS adapters work
 * is gratefully acknowledged.
 *
 * Support from LSI-Logic has also gone a great deal toward making this a
 * workable subsystem and is gratefully acknowledged.
 */
/*
 * Copyright (c) 2004, Avid Technology, Inc. and its contributors.
 * Copyright (c) 2005, WHEEL Sp. z o.o.
 * Copyright (c) 2004, 2005 Justin T. Gibbs
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon including
 *    a substantially similar Disclaimer requirement for further binary
 *    redistribution.
 * 3. Neither the names of the above listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF THE COPYRIGHT
 * OWNER OR CONTRIBUTOR IS ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <dev/mpt/mpt.h>
#include <dev/mpt/mpt_cam.h>
#include <dev/mpt/mpt_raid.h>

/*
 * XXX it seems no other MPT driver knows about the following chips.
 */

#ifndef	MPI_MANUFACTPAGE_DEVICEID_FC909_FB
#define	MPI_MANUFACTPAGE_DEVICEID_FC909_FB	0x0620
#endif

#ifndef	MPI_MANUFACTPAGE_DEVICEID_FC919_LAN_FB
#define	MPI_MANUFACTPAGE_DEVICEID_FC919_LAN_FB	0x0625
#endif

#ifndef	MPI_MANUFACTPAGE_DEVICEID_FC929_LAN_FB
#define	MPI_MANUFACTPAGE_DEVICEID_FC929_LAN_FB	0x0623
#endif

#ifndef	MPI_MANUFACTPAGE_DEVICEID_FC929X_LAN_FB
#define	MPI_MANUFACTPAGE_DEVICEID_FC929X_LAN_FB	0x0627
#endif

#ifndef	MPI_MANUFACTPAGE_DEVICEID_FC919X_LAN_FB
#define	MPI_MANUFACTPAGE_DEVICEID_FC919X_LAN_FB	0x0629
#endif

#ifndef MPI_MANUFACTPAGE_DEVID_SAS1068A_FB
#define MPI_MANUFACTPAGE_DEVID_SAS1068A_FB	0x0055
#endif

#ifndef	MPI_MANUFACTPAGE_DEVID_SAS1068E_FB
#define	MPI_MANUFACTPAGE_DEVID_SAS1068E_FB	0x0059
#endif

#ifndef	MPI_MANUFACTPAGE_DEVID_SAS1078DE_FB
#define	MPI_MANUFACTPAGE_DEVID_SAS1078DE_FB	0x007C
#endif

static int mpt_pci_probe(device_t);
static int mpt_pci_attach(device_t);
static void mpt_free_bus_resources(struct mpt_softc *mpt);
static int mpt_pci_detach(device_t);
static int mpt_pci_shutdown(device_t);
static int mpt_dma_mem_alloc(struct mpt_softc *mpt);
static void mpt_dma_mem_free(struct mpt_softc *mpt);
#if 0
static void mpt_read_config_regs(struct mpt_softc *mpt);
static void mpt_set_config_regs(struct mpt_softc *mpt);
#endif
static void mpt_pci_intr(void *);

static device_method_t mpt_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		mpt_pci_probe),
	DEVMETHOD(device_attach,	mpt_pci_attach),
	DEVMETHOD(device_detach,	mpt_pci_detach),
	DEVMETHOD(device_shutdown,	mpt_pci_shutdown),
	DEVMETHOD_END
};

static driver_t mpt_driver = {
	"mpt", mpt_methods, sizeof(struct mpt_softc)
};

static devclass_t mpt_devclass;
DRIVER_MODULE(mpt, pci, mpt_driver, mpt_devclass, NULL, NULL);
MODULE_DEPEND(mpt, pci, 1, 1, 1);
MODULE_VERSION(mpt, 1);

static int
mpt_pci_probe(device_t dev)
{
	const char *desc;
	int rval;

	if (pci_get_vendor(dev) != MPI_MANUFACTPAGE_VENDORID_LSILOGIC)
		return (ENXIO);

	rval = BUS_PROBE_DEFAULT;
	switch (pci_get_device(dev)) {
	case MPI_MANUFACTPAGE_DEVICEID_FC909_FB:
		desc = "LSILogic FC909 FC Adapter";
		break;
	case MPI_MANUFACTPAGE_DEVICEID_FC909:
		desc = "LSILogic FC909A FC Adapter";
		break;
	case MPI_MANUFACTPAGE_DEVICEID_FC919:
		desc = "LSILogic FC919 FC Adapter";
		break;
	case MPI_MANUFACTPAGE_DEVICEID_FC919_LAN_FB:
		desc = "LSILogic FC919 LAN Adapter";
		break;
	case MPI_MANUFACTPAGE_DEVICEID_FC929:
		desc = "Dual LSILogic FC929 FC Adapter";
		break;
	case MPI_MANUFACTPAGE_DEVICEID_FC929_LAN_FB:
		desc = "Dual LSILogic FC929 LAN Adapter";
		break;
	case MPI_MANUFACTPAGE_DEVICEID_FC919X:
		desc = "LSILogic FC919 FC PCI-X Adapter";
		break;
	case MPI_MANUFACTPAGE_DEVICEID_FC919X_LAN_FB:
		desc = "LSILogic FC919 LAN PCI-X Adapter";
		break;
	case MPI_MANUFACTPAGE_DEVICEID_FC929X:
		desc = "Dual LSILogic FC929X 2Gb/s FC PCI-X Adapter";
		break;
	case MPI_MANUFACTPAGE_DEVICEID_FC929X_LAN_FB:
		desc = "Dual LSILogic FC929X LAN PCI-X Adapter";
		break;
	case MPI_MANUFACTPAGE_DEVICEID_FC949E:
		desc = "Dual LSILogic FC7X04X 4Gb/s FC PCI-Express Adapter";
		break;
	case MPI_MANUFACTPAGE_DEVICEID_FC949X:
		desc = "Dual LSILogic FC7X04X 4Gb/s FC PCI-X Adapter";
		break;
	case MPI_MANUFACTPAGE_DEVID_53C1030:
	case MPI_MANUFACTPAGE_DEVID_53C1030ZC:
		desc = "LSILogic 1030 Ultra4 Adapter";
		break;
	case MPI_MANUFACTPAGE_DEVID_SAS1068E_FB:
		/*
		 * Allow mfi(4) to claim this device in case it's in MegaRAID
		 * mode.
		 */
		rval = BUS_PROBE_LOW_PRIORITY;
		/* FALLTHROUGH */
	case MPI_MANUFACTPAGE_DEVID_SAS1064:
	case MPI_MANUFACTPAGE_DEVID_SAS1064A:
	case MPI_MANUFACTPAGE_DEVID_SAS1064E:
	case MPI_MANUFACTPAGE_DEVID_SAS1066:
	case MPI_MANUFACTPAGE_DEVID_SAS1066E:
	case MPI_MANUFACTPAGE_DEVID_SAS1068:
	case MPI_MANUFACTPAGE_DEVID_SAS1068A_FB:
	case MPI_MANUFACTPAGE_DEVID_SAS1068E:
	case MPI_MANUFACTPAGE_DEVID_SAS1078:
	case MPI_MANUFACTPAGE_DEVID_SAS1078DE_FB:
		desc = "LSILogic SAS/SATA Adapter";
		break;
	default:
		return (ENXIO);
	}

	device_set_desc(dev, desc);
	return (rval);
}

static void
mpt_set_options(struct mpt_softc *mpt)
{
	int tval;

	tval = 0;
	if (resource_int_value(device_get_name(mpt->dev),
	    device_get_unit(mpt->dev), "debug", &tval) == 0 && tval != 0) {
		mpt->verbose = tval;
	}
	tval = -1;
	if (resource_int_value(device_get_name(mpt->dev),
	    device_get_unit(mpt->dev), "role", &tval) == 0 && tval >= 0 &&
	    tval <= 3) {
		mpt->cfg_role = tval;
		mpt->do_cfg_role = 1;
	}
	tval = 0;
	mpt->msi_enable = 0;
	if (mpt->is_sas)
		mpt->msi_enable = 1;
	if (resource_int_value(device_get_name(mpt->dev),
	    device_get_unit(mpt->dev), "msi_enable", &tval) == 0) {
		mpt->msi_enable = tval;
	}
}

#if 0
static void
mpt_link_peer(struct mpt_softc *mpt)
{
	struct mpt_softc *mpt2;

	if (mpt->unit == 0) {
		return;
	}
	/*
	 * XXX: depends on probe order
	 */
	mpt2 = (struct mpt_softc *)devclass_get_softc(mpt_devclass,mpt->unit-1);

	if (mpt2 == NULL) {
		return;
	}
	if (pci_get_vendor(mpt2->dev) != pci_get_vendor(mpt->dev)) {
		return;
	}
	if (pci_get_device(mpt2->dev) != pci_get_device(mpt->dev)) {
		return;
	}
	mpt->mpt2 = mpt2;
	mpt2->mpt2 = mpt;
	if (mpt->verbose >= MPT_PRT_DEBUG) {
		mpt_prt(mpt, "linking with peer (mpt%d)\n",
		    device_get_unit(mpt2->dev));
	}
}

static void
mpt_unlink_peer(struct mpt_softc *mpt)
{

	if (mpt->mpt2) {
		mpt->mpt2->mpt2 = NULL;
	}
}
#endif

static int
mpt_pci_attach(device_t dev)
{
	struct mpt_softc *mpt;
	int		  iqd;
	uint32_t	  val;
	int		  mpt_io_bar, mpt_mem_bar;

	mpt  = (struct mpt_softc*)device_get_softc(dev);

	switch (pci_get_device(dev)) {
	case MPI_MANUFACTPAGE_DEVICEID_FC909_FB:
	case MPI_MANUFACTPAGE_DEVICEID_FC909:
	case MPI_MANUFACTPAGE_DEVICEID_FC919:
	case MPI_MANUFACTPAGE_DEVICEID_FC919_LAN_FB:
	case MPI_MANUFACTPAGE_DEVICEID_FC929:
	case MPI_MANUFACTPAGE_DEVICEID_FC929_LAN_FB:
	case MPI_MANUFACTPAGE_DEVICEID_FC929X:
	case MPI_MANUFACTPAGE_DEVICEID_FC929X_LAN_FB:
	case MPI_MANUFACTPAGE_DEVICEID_FC919X:
	case MPI_MANUFACTPAGE_DEVICEID_FC919X_LAN_FB:
	case MPI_MANUFACTPAGE_DEVICEID_FC949E:
	case MPI_MANUFACTPAGE_DEVICEID_FC949X:
		mpt->is_fc = 1;
		break;
	case MPI_MANUFACTPAGE_DEVID_SAS1078:
	case MPI_MANUFACTPAGE_DEVID_SAS1078DE_FB:
		mpt->is_1078 = 1;
		/* FALLTHROUGH */
	case MPI_MANUFACTPAGE_DEVID_SAS1064:
	case MPI_MANUFACTPAGE_DEVID_SAS1064A:
	case MPI_MANUFACTPAGE_DEVID_SAS1064E:
	case MPI_MANUFACTPAGE_DEVID_SAS1066:
	case MPI_MANUFACTPAGE_DEVID_SAS1066E:
	case MPI_MANUFACTPAGE_DEVID_SAS1068:
	case MPI_MANUFACTPAGE_DEVID_SAS1068A_FB:
	case MPI_MANUFACTPAGE_DEVID_SAS1068E:
	case MPI_MANUFACTPAGE_DEVID_SAS1068E_FB:
		mpt->is_sas = 1;
		break;
	default:
		mpt->is_spi = 1;
		break;
	}
	mpt->dev = dev;
	mpt->unit = device_get_unit(dev);
	mpt->raid_resync_rate = MPT_RAID_RESYNC_RATE_DEFAULT;
	mpt->raid_mwce_setting = MPT_RAID_MWCE_DEFAULT;
	mpt->raid_queue_depth = MPT_RAID_QUEUE_DEPTH_DEFAULT;
	mpt->verbose = MPT_PRT_NONE;
	mpt->role = MPT_ROLE_NONE;
	mpt->mpt_ini_id = MPT_INI_ID_NONE;
#ifdef __sparc64__
	if (mpt->is_spi)
		mpt->mpt_ini_id = OF_getscsinitid(dev);
#endif
	mpt_set_options(mpt);
	if (mpt->verbose == MPT_PRT_NONE) {
		mpt->verbose = MPT_PRT_WARN;
		/* Print INFO level (if any) if bootverbose is set */
		mpt->verbose += (bootverbose != 0)? 1 : 0;
	}

	/*
	 * Make sure that SERR, PERR, WRITE INVALIDATE and BUSMASTER are set.
	 */
	val = pci_read_config(dev, PCIR_COMMAND, 2);
	val |= PCIM_CMD_SERRESPEN | PCIM_CMD_PERRESPEN |
	    PCIM_CMD_BUSMASTEREN | PCIM_CMD_MWRICEN;
	pci_write_config(dev, PCIR_COMMAND, val, 2);

	/*
	 * Make sure we've disabled the ROM.
	 */
	val = pci_read_config(dev, PCIR_BIOS, 4);
	val &= ~PCIM_BIOS_ENABLE;
	pci_write_config(dev, PCIR_BIOS, val, 4);

#if 0
	/*
	 * Is this part a dual?
	 * If so, link with our partner (around yet)
	 */
	switch (pci_get_device(dev)) {
	case MPI_MANUFACTPAGE_DEVICEID_FC929:
	case MPI_MANUFACTPAGE_DEVICEID_FC929_LAN_FB:
	case MPI_MANUFACTPAGE_DEVICEID_FC949E:
	case MPI_MANUFACTPAGE_DEVICEID_FC949X:
	case MPI_MANUFACTPAGE_DEVID_53C1030:
	case MPI_MANUFACTPAGE_DEVID_53C1030ZC:
		mpt_link_peer(mpt);
		break;
	default:
		break;
	}
#endif

	/*
	 * Figure out which are the I/O and MEM Bars
	 */
	val = pci_read_config(dev, PCIR_BAR(0), 4);
	if (PCI_BAR_IO(val)) {
		/* BAR0 is IO, BAR1 is memory */
		mpt_io_bar = 0;
		mpt_mem_bar = 1;
	} else {
		/* BAR0 is memory, BAR1 is IO */
		mpt_mem_bar = 0;
		mpt_io_bar = 1;
	}

	/*
	 * Set up register access.  PIO mode is required for
	 * certain reset operations (but must be disabled for
	 * some cards otherwise).
	 */
	mpt_io_bar = PCIR_BAR(mpt_io_bar);
	mpt->pci_pio_reg = bus_alloc_resource_any(dev, SYS_RES_IOPORT,
	    &mpt_io_bar, RF_ACTIVE);
	if (mpt->pci_pio_reg == NULL) {
		if (bootverbose) {
			device_printf(dev,
			    "unable to map registers in PIO mode\n");
		}
	} else {
		mpt->pci_pio_st = rman_get_bustag(mpt->pci_pio_reg);
		mpt->pci_pio_sh = rman_get_bushandle(mpt->pci_pio_reg);
	}

	mpt_mem_bar = PCIR_BAR(mpt_mem_bar);
	mpt->pci_reg = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &mpt_mem_bar, RF_ACTIVE);
	if (mpt->pci_reg == NULL) {
		if (bootverbose || mpt->is_sas || mpt->pci_pio_reg == NULL) {
			device_printf(dev,
			    "Unable to memory map registers.\n");
		}
		if (mpt->is_sas || mpt->pci_pio_reg == NULL) {
			device_printf(dev, "Giving Up.\n");
			goto bad;
		}
		if (bootverbose) {
			device_printf(dev, "Falling back to PIO mode.\n");
		}
		mpt->pci_st = mpt->pci_pio_st;
		mpt->pci_sh = mpt->pci_pio_sh;
	} else {
		mpt->pci_st = rman_get_bustag(mpt->pci_reg);
		mpt->pci_sh = rman_get_bushandle(mpt->pci_reg);
	}

	/* Get a handle to the interrupt */
	iqd = 0;
	if (mpt->msi_enable) {
		/*
		 * First try to alloc an MSI-X message.  If that
		 * fails, then try to alloc an MSI message instead.
		 */
		val = 1;
		if (pci_alloc_msix(dev, &val) == 0)
			iqd = 1;
		val = 1;
		if (iqd == 0 && pci_alloc_msi(dev, &val) == 0)
			iqd = 1;
	}
	mpt->pci_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &iqd,
	    RF_ACTIVE | (iqd != 0 ? 0 : RF_SHAREABLE));
	if (mpt->pci_irq == NULL) {
		device_printf(dev, "could not allocate interrupt\n");
		goto bad;
	}

	MPT_LOCK_SETUP(mpt);

	/* Disable interrupts at the part */
	mpt_disable_ints(mpt);

	/* Register the interrupt handler */
	if (bus_setup_intr(dev, mpt->pci_irq, MPT_IFLAGS, NULL, mpt_pci_intr,
	    mpt, &mpt->ih)) {
		device_printf(dev, "could not setup interrupt\n");
		goto bad;
	}

	/* Allocate dma memory */
	if (mpt_dma_mem_alloc(mpt)) {
		mpt_prt(mpt, "Could not allocate DMA memory\n");
		goto bad;
	}

#if 0
	/*
	 * Save the PCI config register values
 	 *
	 * Hard resets are known to screw up the BAR for diagnostic
	 * memory accesses (Mem1).
	 *
	 * Using Mem1 is known to make the chip stop responding to 
	 * configuration space transfers, so we need to save it now
	 */

	mpt_read_config_regs(mpt);
#endif

	/*
	 * Disable PIO until we need it
	 */
	if (mpt->is_sas) {
		pci_disable_io(dev, SYS_RES_IOPORT);
	}

	/* Initialize the hardware */
	if (mpt->disabled == 0) {
		if (mpt_attach(mpt) != 0) {
			goto bad;
		}
	} else {
		mpt_prt(mpt, "device disabled at user request\n");
		goto bad;
	}

	mpt->eh = EVENTHANDLER_REGISTER(shutdown_post_sync, mpt_pci_shutdown,
	    dev, SHUTDOWN_PRI_LAST);

	if (mpt->eh == NULL) {
		mpt_prt(mpt, "shutdown event registration failed\n");
		mpt_disable_ints(mpt);
		(void) mpt_detach(mpt);
		mpt_reset(mpt, /*reinit*/FALSE);
		mpt_raid_free_mem(mpt);
		goto bad;
	}
	return (0);

bad:
	mpt_dma_mem_free(mpt);
	mpt_free_bus_resources(mpt);
#if 0
	mpt_unlink_peer(mpt);
#endif

	MPT_LOCK_DESTROY(mpt);

	/*
	 * but return zero to preserve unit numbering
	 */
	return (0);
}

/*
 * Free bus resources
 */
static void
mpt_free_bus_resources(struct mpt_softc *mpt)
{

	if (mpt->ih) {
		bus_teardown_intr(mpt->dev, mpt->pci_irq, mpt->ih);
		mpt->ih = NULL;
	}

	if (mpt->pci_irq) {
		bus_release_resource(mpt->dev, SYS_RES_IRQ,
		    rman_get_rid(mpt->pci_irq), mpt->pci_irq);
		pci_release_msi(mpt->dev);
		mpt->pci_irq = NULL;
	}

	if (mpt->pci_pio_reg) {
		bus_release_resource(mpt->dev, SYS_RES_IOPORT,
		    rman_get_rid(mpt->pci_pio_reg), mpt->pci_pio_reg);
		mpt->pci_pio_reg = NULL;
	}

	if (mpt->pci_reg) {
		bus_release_resource(mpt->dev, SYS_RES_MEMORY,
		    rman_get_rid(mpt->pci_reg), mpt->pci_reg);
		mpt->pci_reg = NULL;
	}
}

/*
 * Disconnect ourselves from the system.
 */
static int
mpt_pci_detach(device_t dev)
{
	struct mpt_softc *mpt;

	mpt  = (struct mpt_softc*)device_get_softc(dev);

	if (mpt) {
		mpt_disable_ints(mpt);
		mpt_detach(mpt);
		mpt_reset(mpt, /*reinit*/FALSE);
		mpt_raid_free_mem(mpt);
		mpt_dma_mem_free(mpt);
		mpt_free_bus_resources(mpt);
#if 0
		mpt_unlink_peer(mpt);
#endif
		if (mpt->eh != NULL) {
                        EVENTHANDLER_DEREGISTER(shutdown_post_sync, mpt->eh);
		}
		MPT_LOCK_DESTROY(mpt);
	}
	return(0);
}

/*
 * Disable the hardware
 */
static int
mpt_pci_shutdown(device_t dev)
{
	struct mpt_softc *mpt;

	mpt = (struct mpt_softc *)device_get_softc(dev);
	if (mpt)
		return (mpt_shutdown(mpt));
	return(0);
}

static int
mpt_dma_mem_alloc(struct mpt_softc *mpt)
{
	size_t len;
	struct mpt_map_info mi;

	/* Check if we alreay have allocated the reply memory */
	if (mpt->reply_phys != 0) {
		return 0;
	}

	len = sizeof (request_t) * MPT_MAX_REQUESTS(mpt);
	mpt->request_pool = (request_t *)malloc(len, M_DEVBUF, M_WAITOK|M_ZERO);

	/*
	 * Create a parent dma tag for this device.
	 *
	 * Align at byte boundaries,
	 * Limit to 32-bit addressing for request/reply queues.
	 */
	if (mpt_dma_tag_create(mpt, /*parent*/bus_get_dma_tag(mpt->dev),
	    /*alignment*/1, /*boundary*/0, /*lowaddr*/BUS_SPACE_MAXADDR,
	    /*highaddr*/BUS_SPACE_MAXADDR, /*filter*/NULL, /*filterarg*/NULL,
	    /*maxsize*/BUS_SPACE_MAXSIZE_32BIT,
	    /*nsegments*/BUS_SPACE_UNRESTRICTED,
	    /*maxsegsz*/BUS_SPACE_MAXSIZE_32BIT, /*flags*/0,
	    &mpt->parent_dmat) != 0) {
		mpt_prt(mpt, "cannot create parent dma tag\n");
		return (1);
	}

	/* Create a child tag for reply buffers */
	if (mpt_dma_tag_create(mpt, mpt->parent_dmat, PAGE_SIZE, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR,
	    NULL, NULL, 2 * PAGE_SIZE, 1, BUS_SPACE_MAXSIZE_32BIT, 0,
	    &mpt->reply_dmat) != 0) {
		mpt_prt(mpt, "cannot create a dma tag for replies\n");
		return (1);
	}

	/* Allocate some DMA accessible memory for replies */
	if (bus_dmamem_alloc(mpt->reply_dmat, (void **)&mpt->reply,
	    BUS_DMA_NOWAIT, &mpt->reply_dmap) != 0) {
		mpt_prt(mpt, "cannot allocate %lu bytes of reply memory\n",
		    (u_long) (2 * PAGE_SIZE));
		return (1);
	}

	mi.mpt = mpt;
	mi.error = 0;

	/* Load and lock it into "bus space" */
	bus_dmamap_load(mpt->reply_dmat, mpt->reply_dmap, mpt->reply,
	    2 * PAGE_SIZE, mpt_map_rquest, &mi, 0);

	if (mi.error) {
		mpt_prt(mpt, "error %d loading dma map for DMA reply queue\n",
		    mi.error);
		return (1);
	}
	mpt->reply_phys = mi.phys;

	return (0);
}

/* Deallocate memory that was allocated by mpt_dma_mem_alloc 
 */
static void
mpt_dma_mem_free(struct mpt_softc *mpt)
{

        /* Make sure we aren't double destroying */
        if (mpt->reply_dmat == 0) {
		mpt_lprt(mpt, MPT_PRT_DEBUG, "already released dma memory\n");
		return;
        }
                
	bus_dmamap_unload(mpt->reply_dmat, mpt->reply_dmap);
	bus_dmamem_free(mpt->reply_dmat, mpt->reply, mpt->reply_dmap);
	bus_dma_tag_destroy(mpt->reply_dmat);
	bus_dma_tag_destroy(mpt->parent_dmat);
	mpt->reply_dmat = NULL;
	free(mpt->request_pool, M_DEVBUF);
	mpt->request_pool = NULL;
}

#if 0
/* Reads modifiable (via PCI transactions) config registers */
static void
mpt_read_config_regs(struct mpt_softc *mpt)
{

	mpt->pci_cfg.Command = pci_read_config(mpt->dev, PCIR_COMMAND, 2);
	mpt->pci_cfg.LatencyTimer_LineSize =
	    pci_read_config(mpt->dev, PCIR_CACHELNSZ, 2);
	mpt->pci_cfg.IO_BAR = pci_read_config(mpt->dev, PCIR_BAR(0), 4);
	mpt->pci_cfg.Mem0_BAR[0] = pci_read_config(mpt->dev, PCIR_BAR(1), 4);
	mpt->pci_cfg.Mem0_BAR[1] = pci_read_config(mpt->dev, PCIR_BAR(2), 4);
	mpt->pci_cfg.Mem1_BAR[0] = pci_read_config(mpt->dev, PCIR_BAR(3), 4);
	mpt->pci_cfg.Mem1_BAR[1] = pci_read_config(mpt->dev, PCIR_BAR(4), 4);
	mpt->pci_cfg.ROM_BAR = pci_read_config(mpt->dev, PCIR_BIOS, 4);
	mpt->pci_cfg.IntLine = pci_read_config(mpt->dev, PCIR_INTLINE, 1);
	mpt->pci_cfg.PMCSR = pci_read_config(mpt->dev, 0x44, 4);
}

/* Sets modifiable config registers */
static void
mpt_set_config_regs(struct mpt_softc *mpt)
{
	uint32_t val;

#define MPT_CHECK(reg, offset, size)					\
	val = pci_read_config(mpt->dev, offset, size);			\
	if (mpt->pci_cfg.reg != val) {					\
		mpt_prt(mpt,						\
		    "Restoring " #reg " to 0x%X from 0x%X\n",		\
		    mpt->pci_cfg.reg, val);				\
	}

	if (mpt->verbose >= MPT_PRT_DEBUG) {
		MPT_CHECK(Command, PCIR_COMMAND, 2);
		MPT_CHECK(LatencyTimer_LineSize, PCIR_CACHELNSZ, 2);
		MPT_CHECK(IO_BAR, PCIR_BAR(0), 4);
		MPT_CHECK(Mem0_BAR[0], PCIR_BAR(1), 4);
		MPT_CHECK(Mem0_BAR[1], PCIR_BAR(2), 4);
		MPT_CHECK(Mem1_BAR[0], PCIR_BAR(3), 4);
		MPT_CHECK(Mem1_BAR[1], PCIR_BAR(4), 4);
		MPT_CHECK(ROM_BAR, PCIR_BIOS, 4);
		MPT_CHECK(IntLine, PCIR_INTLINE, 1);
		MPT_CHECK(PMCSR, 0x44, 4);
	}
#undef MPT_CHECK

	pci_write_config(mpt->dev, PCIR_COMMAND, mpt->pci_cfg.Command, 2);
	pci_write_config(mpt->dev, PCIR_CACHELNSZ,
	    mpt->pci_cfg.LatencyTimer_LineSize, 2);
	pci_write_config(mpt->dev, PCIR_BAR(0), mpt->pci_cfg.IO_BAR, 4);
	pci_write_config(mpt->dev, PCIR_BAR(1), mpt->pci_cfg.Mem0_BAR[0], 4);
	pci_write_config(mpt->dev, PCIR_BAR(2), mpt->pci_cfg.Mem0_BAR[1], 4);
	pci_write_config(mpt->dev, PCIR_BAR(3), mpt->pci_cfg.Mem1_BAR[0], 4);
	pci_write_config(mpt->dev, PCIR_BAR(4), mpt->pci_cfg.Mem1_BAR[1], 4);
	pci_write_config(mpt->dev, PCIR_BIOS, mpt->pci_cfg.ROM_BAR, 4);
	pci_write_config(mpt->dev, PCIR_INTLINE, mpt->pci_cfg.IntLine, 1);
	pci_write_config(mpt->dev, 0x44, mpt->pci_cfg.PMCSR, 4);
}
#endif

static void
mpt_pci_intr(void *arg)
{
	struct mpt_softc *mpt;

	mpt = (struct mpt_softc *)arg;
	MPT_LOCK(mpt);
	mpt_intr(mpt);
	MPT_UNLOCK(mpt);
}
