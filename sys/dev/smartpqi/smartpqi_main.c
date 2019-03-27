/*-
 * Copyright (c) 2018 Microsemi Corporation.
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

/* $FreeBSD$ */

/*
 * Driver for the Microsemi Smart storage controllers
 */

#include "smartpqi_includes.h"
#include "smartpqi_prototypes.h"

/*
 * Supported devices
 */
struct pqi_ident
{
	u_int16_t		vendor;
	u_int16_t		device;
	u_int16_t		subvendor;
	u_int16_t		subdevice;
	int			hwif;
	char			*desc;
} pqi_identifiers[] = {
	/* (MSCC PM8205 8x12G based) */
	{0x9005, 0x028f, 0x103c, 0x600, PQI_HWIF_SRCV, "P408i-p SR Gen10"},
	{0x9005, 0x028f, 0x103c, 0x601, PQI_HWIF_SRCV, "P408e-p SR Gen10"},
	{0x9005, 0x028f, 0x103c, 0x602, PQI_HWIF_SRCV, "P408i-a SR Gen10"},
	{0x9005, 0x028f, 0x103c, 0x603, PQI_HWIF_SRCV, "P408i-c SR Gen10"},
	{0x9005, 0x028f, 0x1028, 0x1FE0, PQI_HWIF_SRCV, "SmartRAID 3162-8i/eDell"},
	{0x9005, 0x028f, 0x9005, 0x608, PQI_HWIF_SRCV, "SmartRAID 3162-8i/e"},
	{0x9005, 0x028f, 0x103c, 0x609, PQI_HWIF_SRCV, "P408i-sb SR G10"},

	/* (MSCC PM8225 8x12G based) */
	{0x9005, 0x028f, 0x103c, 0x650, PQI_HWIF_SRCV, "E208i-p SR Gen10"},
	{0x9005, 0x028f, 0x103c, 0x651, PQI_HWIF_SRCV, "E208e-p SR Gen10"},
	{0x9005, 0x028f, 0x103c, 0x652, PQI_HWIF_SRCV, "E208i-c SR Gen10"},
	{0x9005, 0x028f, 0x103c, 0x654, PQI_HWIF_SRCV, "E208i-a SR Gen10"},
	{0x9005, 0x028f, 0x103c, 0x655, PQI_HWIF_SRCV, "P408e-m SR Gen10"},

	/* (MSCC PM8221 8x12G based) */
	{0x9005, 0x028f, 0x103c, 0x700, PQI_HWIF_SRCV, "P204i-c SR Gen10"},
	{0x9005, 0x028f, 0x103c, 0x701, PQI_HWIF_SRCV, "P204i-b SR Gen10"},

	/* (MSCC PM8204 8x12G based) */
	{0x9005, 0x028f, 0x9005, 0x800, PQI_HWIF_SRCV, "SmartRAID 3154-8i"},
	{0x9005, 0x028f, 0x9005, 0x801, PQI_HWIF_SRCV, "SmartRAID 3152-8i"},
	{0x9005, 0x028f, 0x9005, 0x802, PQI_HWIF_SRCV, "SmartRAID 3151-4i"},
	{0x9005, 0x028f, 0x9005, 0x803, PQI_HWIF_SRCV, "SmartRAID 3101-4i"},
	{0x9005, 0x028f, 0x9005, 0x804, PQI_HWIF_SRCV, "SmartRAID 3154-8e"},
	{0x9005, 0x028f, 0x9005, 0x805, PQI_HWIF_SRCV, "SmartRAID 3102-8i"},
	{0x9005, 0x028f, 0x9005, 0x806, PQI_HWIF_SRCV, "SmartRAID 3100"},
	{0x9005, 0x028f, 0x9005, 0x807, PQI_HWIF_SRCV, "SmartRAID 3162-8i"},
	{0x9005, 0x028f, 0x152d, 0x8a22, PQI_HWIF_SRCV, "QS-8204-8i"},
	{0x9005, 0x028f, 0x193d, 0xf460, PQI_HWIF_SRCV, "UN RAID P460-M4"},
	{0x9005, 0x028f, 0x193d, 0xf461, PQI_HWIF_SRCV, "UN RAID P460-B4"},
	{0x9005, 0x028f, 0x1bd4, 0x004b, PQI_HWIF_SRCV, "INSPUR RAID PM8204-2GB"},
	{0x9005, 0x028f, 0x1bd4, 0x004c, PQI_HWIF_SRCV, "INSPUR RAID PM8204-4GB"},

	/* (MSCC PM8222 8x12G based) */
	{0x9005, 0x028f, 0x9005, 0x900, PQI_HWIF_SRCV, "SmartHBA 2100-8i"},
	{0x9005, 0x028f, 0x9005, 0x901, PQI_HWIF_SRCV, "SmartHBA 2100-4i"},
	{0x9005, 0x028f, 0x9005, 0x902, PQI_HWIF_SRCV, "HBA 1100-8i"},
	{0x9005, 0x028f, 0x9005, 0x903, PQI_HWIF_SRCV, "HBA 1100-4i"},
	{0x9005, 0x028f, 0x9005, 0x904, PQI_HWIF_SRCV, "SmartHBA 2100-8e"},
	{0x9005, 0x028f, 0x9005, 0x905, PQI_HWIF_SRCV, "HBA 1100-8e"},
	{0x9005, 0x028f, 0x9005, 0x906, PQI_HWIF_SRCV, "SmartHBA 2100-4i4e"},
	{0x9005, 0x028f, 0x9005, 0x907, PQI_HWIF_SRCV, "HBA 1100"},
	{0x9005, 0x028f, 0x9005, 0x908, PQI_HWIF_SRCV, "SmartHBA 2100"},
	{0x9005, 0x028f, 0x9005, 0x90a, PQI_HWIF_SRCV, "SmartHBA 2100A-8i"},
	{0x9005, 0x028f, 0x193d, 0x8460, PQI_HWIF_SRCV, "UN HBA H460-M1"},
	{0x9005, 0x028f, 0x193d, 0x8461, PQI_HWIF_SRCV, "UN HBA H460-B1"},
	{0x9005, 0x028f, 0x1bd4, 0x004a, PQI_HWIF_SRCV, "INSPUR SMART-HBA PM8222-SHBA"},
	{0x9005, 0x028f, 0x13fe, 0x8312, PQI_HWIF_SRCV, "MIC-8312BridgeB"},

	/* (SRCx MSCC FVB 24x12G based) */
	{0x9005, 0x028f, 0x103c, 0x1001, PQI_HWIF_SRCV, "MSCC FVB"},

	/* (MSCC PM8241 24x12G based) */

	/* (MSCC PM8242 24x12G based) */
	{0x9005, 0x028f, 0x152d, 0x8a37, PQI_HWIF_SRCV, "QS-8242-24i"},
	{0x9005, 0x028f, 0x9005, 0x1300, PQI_HWIF_SRCV, "HBA 1100-8i8e"},
	{0x9005, 0x028f, 0x9005, 0x1301, PQI_HWIF_SRCV, "HBA 1100-24i"},
	{0x9005, 0x028f, 0x9005, 0x1302, PQI_HWIF_SRCV, "SmartHBA 2100-8i8e"},
	{0x9005, 0x028f, 0x9005, 0x1303, PQI_HWIF_SRCV, "SmartHBA 2100-24i"},
	{0x9005, 0x028f, 0x105b, 0x1321, PQI_HWIF_SRCV, "8242-24i"},
	{0x9005, 0x028f, 0x1bd4, 0x0045, PQI_HWIF_SRCV, "INSPUR SMART-HBA 8242-24i"},

	/* (MSCC PM8236 16x12G based) */
	{0x9005, 0x028f, 0x152d, 0x8a24, PQI_HWIF_SRCV, "QS-8236-16i"},
	{0x9005, 0x028f, 0x9005, 0x1380, PQI_HWIF_SRCV, "SmartRAID 3154-16i"},
	{0x9005, 0x028f, 0x1bd4, 0x0046, PQI_HWIF_SRCV, "INSPUR RAID 8236-16i"},

	/* (MSCC PM8237 24x12G based) */
	{0x9005, 0x028f, 0x103c, 0x1100, PQI_HWIF_SRCV, "P816i-a SR Gen10"},
	{0x9005, 0x028f, 0x103c, 0x1101, PQI_HWIF_SRCV, "P416ie-m SR G10"},

	/* (MSCC PM8238 16x12G based) */
	{0x9005, 0x028f, 0x152d, 0x8a23, PQI_HWIF_SRCV, "QS-8238-16i"},
	{0x9005, 0x028f, 0x9005, 0x1280, PQI_HWIF_SRCV, "HBA 1100-16i"},
	{0x9005, 0x028f, 0x9005, 0x1281, PQI_HWIF_SRCV, "HBA 1100-16e"},
	{0x9005, 0x028f, 0x105b, 0x1211, PQI_HWIF_SRCV, "8238-16i"},
	{0x9005, 0x028f, 0x1bd4, 0x0048, PQI_HWIF_SRCV, "INSPUR SMART-HBA 8238-16i"},
	{0x9005, 0x028f, 0x9005, 0x1282, PQI_HWIF_SRCV, "SmartHBA 2100-16i"},

	/* (MSCC PM8240 24x12G based) */
	{0x9005, 0x028f, 0x152d, 0x8a36, PQI_HWIF_SRCV, "QS-8240-24i"},
	{0x9005, 0x028f, 0x9005, 0x1200, PQI_HWIF_SRCV, "SmartRAID 3154-24i"},
	{0x9005, 0x028f, 0x9005, 0x1201, PQI_HWIF_SRCV, "SmartRAID 3154-8i16e"},
	{0x9005, 0x028f, 0x9005, 0x1202, PQI_HWIF_SRCV, "SmartRAID 3154-8i8e"},
	{0x9005, 0x028f, 0x1bd4, 0x0047, PQI_HWIF_SRCV, "INSPUR RAID 8240-24i"},

	{0, 0, 0, 0, 0, 0}
};

struct pqi_ident
pqi_family_identifiers[] = {
	{0x9005, 0x028f, 0, 0, PQI_HWIF_SRCV, "Smart Array Storage Controller"},
	{0, 0, 0, 0, 0, 0}
};

/*
 * Function to identify the installed adapter.
 */
static struct pqi_ident *
pqi_find_ident(device_t dev)
{
	struct pqi_ident *m;
	u_int16_t vendid, devid, sub_vendid, sub_devid;

	vendid = pci_get_vendor(dev);
	devid = pci_get_device(dev);
	sub_vendid = pci_get_subvendor(dev);
	sub_devid = pci_get_subdevice(dev);

	for (m = pqi_identifiers; m->vendor != 0; m++) {
		if ((m->vendor == vendid) && (m->device == devid) &&
			(m->subvendor == sub_vendid) &&
			(m->subdevice == sub_devid)) {
			return (m);
		}
	}

	for (m = pqi_family_identifiers; m->vendor != 0; m++) {
		if ((m->vendor == vendid) && (m->device == devid)) {
			return (m);
		}
	}

	return (NULL);
}

/*
 * Determine whether this is one of our supported adapters.
 */
static int
smartpqi_probe(device_t dev)
{
	struct pqi_ident *id;

	if ((id = pqi_find_ident(dev)) != NULL) {
		device_set_desc(dev, id->desc);	
		return(BUS_PROBE_VENDOR);
	}

	return(ENXIO);
}

/*
 * Store Bus/Device/Function in softs
 */
void pqisrc_save_controller_info(struct pqisrc_softstate *softs)
{
	device_t dev = softs->os_specific.pqi_dev;

	softs->bus_id = (uint32_t)pci_get_bus(dev);
	softs->device_id = (uint32_t)pci_get_device(dev);
	softs->func_id = (uint32_t)pci_get_function(dev);	
}


/*
 * Allocate resources for our device, set up the bus interface.
 * Initialize the PQI related functionality, scan devices, register sim to
 * upper layer, create management interface device node etc.
 */
static int
smartpqi_attach(device_t dev)
{
	struct pqisrc_softstate *softs = NULL;
	struct pqi_ident *id = NULL;
	int error = 0;
	u_int32_t command = 0, i = 0;
	int card_index = device_get_unit(dev);
	rcb_t *rcbp = NULL;

	/*
	 * Initialise softc.
	 */
	softs = device_get_softc(dev);

	if (!softs) {
		printf("Could not get softc\n");
		error = EINVAL;
		goto out;
	}
	memset(softs, 0, sizeof(*softs));
	softs->os_specific.pqi_dev = dev;

	DBG_FUNC("IN\n");

	/* assume failure is 'not configured' */
	error = ENXIO;

	/* 
	 * Verify that the adapter is correctly set up in PCI space.
	 */
	pci_enable_busmaster(softs->os_specific.pqi_dev);
	command = pci_read_config(softs->os_specific.pqi_dev, PCIR_COMMAND, 2);
	if ((command & PCIM_CMD_MEMEN) == 0) {
		DBG_ERR("memory window not available command = %d\n", command);
		error = ENXIO;
		goto out;
	}

	/* 
	 * Detect the hardware interface version, set up the bus interface
	 * indirection.
	 */
	id = pqi_find_ident(dev);
	softs->os_specific.pqi_hwif = id->hwif;

	switch(softs->os_specific.pqi_hwif) {
		case PQI_HWIF_SRCV:
			DBG_INFO("set hardware up for PMC SRCv for %p", softs);
			break;
		default:
			softs->os_specific.pqi_hwif = PQI_HWIF_UNKNOWN;
			DBG_ERR("unknown hardware type\n");
			error = ENXIO;
			goto out;
	}

	pqisrc_save_controller_info(softs);

	/*
	 * Allocate the PCI register window.
	 */
	softs->os_specific.pqi_regs_rid0 = PCIR_BAR(0);
	if ((softs->os_specific.pqi_regs_res0 =
		bus_alloc_resource_any(softs->os_specific.pqi_dev, SYS_RES_MEMORY,
		&softs->os_specific.pqi_regs_rid0, RF_ACTIVE)) == NULL) {
		DBG_ERR("couldn't allocate register window 0\n");
		/* assume failure is 'out of memory' */
		error = ENOMEM;
		goto out;
	}

	bus_get_resource_start(softs->os_specific.pqi_dev, SYS_RES_MEMORY,
		softs->os_specific.pqi_regs_rid0);

	softs->pci_mem_handle.pqi_btag = rman_get_bustag(softs->os_specific.pqi_regs_res0);
	softs->pci_mem_handle.pqi_bhandle = rman_get_bushandle(softs->os_specific.pqi_regs_res0);
	/* softs->pci_mem_base_vaddr = (uintptr_t)rman_get_virtual(softs->os_specific.pqi_regs_res0); */
	softs->pci_mem_base_vaddr = (char *)rman_get_virtual(softs->os_specific.pqi_regs_res0);

	/*
	 * Allocate the parent bus DMA tag appropriate for our PCI interface.
	 * 
	 * Note that some of these controllers are 64-bit capable.
	 */
	if (bus_dma_tag_create(bus_get_dma_tag(dev), 	/* parent */
				PAGE_SIZE, 0,		/* algnmnt, boundary */
				BUS_SPACE_MAXADDR_32BIT,/* lowaddr */
				BUS_SPACE_MAXADDR, 	/* highaddr */
				NULL, NULL, 		/* filter, filterarg */
				BUS_SPACE_MAXSIZE_32BIT,	/* maxsize */
				BUS_SPACE_UNRESTRICTED,	/* nsegments */
				BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
				0,			/* flags */
				NULL, NULL,		/* No locking needed */
				&softs->os_specific.pqi_parent_dmat)) {
		DBG_ERR("can't allocate parent DMA tag\n");
		/* assume failure is 'out of memory' */
		error = ENOMEM;
		goto dma_out;
	}

	softs->os_specific.sim_registered = FALSE;
	softs->os_name = "FreeBSD ";
	
	/* Initialize the PQI library */
	error = pqisrc_init(softs);
	if (error) {
		DBG_ERR("Failed to initialize pqi lib error = %d\n", error);
		error = PQI_STATUS_FAILURE;
		goto out;
	}

        mtx_init(&softs->os_specific.cam_lock, "cam_lock", NULL, MTX_DEF);
        softs->os_specific.mtx_init = TRUE;
        mtx_init(&softs->os_specific.map_lock, "map_lock", NULL, MTX_DEF);

        /*
         * Create DMA tag for mapping buffers into controller-addressable space.
         */
        if (bus_dma_tag_create(softs->os_specific.pqi_parent_dmat,/* parent */
				1, 0,			/* algnmnt, boundary */
				BUS_SPACE_MAXADDR_32BIT,/* lowaddr */
				BUS_SPACE_MAXADDR,	/* highaddr */
				NULL, NULL,		/* filter, filterarg */
				softs->pqi_cap.max_sg_elem*PAGE_SIZE,/*maxsize*/
				softs->pqi_cap.max_sg_elem,	/* nsegments */
				BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
				BUS_DMA_ALLOCNOW,		/* flags */
				busdma_lock_mutex,		/* lockfunc */
				&softs->os_specific.map_lock,	/* lockfuncarg*/
				&softs->os_specific.pqi_buffer_dmat)) {
		DBG_ERR("can't allocate buffer DMA tag for pqi_buffer_dmat\n");
		return (ENOMEM);
        }

	rcbp = &softs->rcb[1];
	for( i = 1;  i <= softs->pqi_cap.max_outstanding_io; i++, rcbp++ ) {
		if ((error = bus_dmamap_create(softs->os_specific.pqi_buffer_dmat, 0, &rcbp->cm_datamap)) != 0) {
			DBG_ERR("Cant create datamap for buf @"
			"rcbp = %p maxio = %d error = %d\n", 
			rcbp, softs->pqi_cap.max_outstanding_io, error);
			goto dma_out;
		}
	}

	os_start_heartbeat_timer((void *)softs); /* Start the heart-beat timer */
	softs->os_specific.wellness_periodic = timeout( os_wellness_periodic, 
							softs, 120*hz);
	/* Register our shutdown handler. */
	softs->os_specific.eh = EVENTHANDLER_REGISTER(shutdown_final, 
				smartpqi_shutdown, softs, SHUTDOWN_PRI_DEFAULT);

	error = pqisrc_scan_devices(softs);
	if (error) {
		DBG_ERR("Failed to scan lib error = %d\n", error);
		error = PQI_STATUS_FAILURE;
		goto out;
	}

	error = register_sim(softs, card_index);
	if (error) {
		DBG_ERR("Failed to register sim index = %d error = %d\n", 
			card_index, error);
		goto out;
	}

	smartpqi_target_rescan(softs);		

	TASK_INIT(&softs->os_specific.event_task, 0, pqisrc_event_worker,softs);

	error = create_char_dev(softs, card_index);
	if (error) {
		DBG_ERR("Failed to register character device index=%d r=%d\n", 
			card_index, error);
		goto out;
	}
	goto out;

dma_out:
	if (softs->os_specific.pqi_regs_res0 != NULL)
		bus_release_resource(softs->os_specific.pqi_dev, SYS_RES_MEMORY,
			softs->os_specific.pqi_regs_rid0, 
			softs->os_specific.pqi_regs_res0);
out:
	DBG_FUNC("OUT error = %d\n", error);
	return(error);
}

/*
 * Deallocate resources for our device.
 */
static int
smartpqi_detach(device_t dev)
{
	struct pqisrc_softstate *softs = NULL;
	softs = device_get_softc(dev);
	DBG_FUNC("IN\n");

	EVENTHANDLER_DEREGISTER(shutdown_final, softs->os_specific.eh);

	/* kill the periodic event */
	untimeout(os_wellness_periodic, softs, 
			softs->os_specific.wellness_periodic);
	/* Kill the heart beat event */
	untimeout(os_start_heartbeat_timer, softs, 
			softs->os_specific.heartbeat_timeout_id);

	smartpqi_shutdown(softs);
	destroy_char_dev(softs);
	pqisrc_uninit(softs);
	deregister_sim(softs);
	pci_release_msi(dev);
	
	DBG_FUNC("OUT\n");
	return 0;
}

/*
 * Bring the controller to a quiescent state, ready for system suspend.
 */
static int
smartpqi_suspend(device_t dev)
{
	struct pqisrc_softstate *softs;
	softs = device_get_softc(dev);
	DBG_FUNC("IN\n");

	DBG_INFO("Suspending the device %p\n", softs);
	softs->os_specific.pqi_state |= SMART_STATE_SUSPEND;

	DBG_FUNC("OUT\n");
	return(0);
}

/*
 * Bring the controller back to a state ready for operation.
 */
static int
smartpqi_resume(device_t dev)
{
	struct pqisrc_softstate *softs;
	softs = device_get_softc(dev);
	DBG_FUNC("IN\n");

	softs->os_specific.pqi_state &= ~SMART_STATE_SUSPEND;

	DBG_FUNC("OUT\n");
	return(0);
}

/*
 * Do whatever is needed during a system shutdown.
 */
int
smartpqi_shutdown(void *arg)
{
	struct pqisrc_softstate *softs = NULL;
	int rval = 0;

	DBG_FUNC("IN\n");

	softs = (struct pqisrc_softstate *)arg;

	rval = pqisrc_flush_cache(softs, PQISRC_SHUTDOWN);
	if (rval != PQI_STATUS_SUCCESS) {
		DBG_ERR("Unable to flush adapter cache! rval = %d", rval);
	}

	DBG_FUNC("OUT\n");
		
	return rval;
}

/*
 * PCI bus interface.
 */
static device_method_t pqi_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		smartpqi_probe),
	DEVMETHOD(device_attach,	smartpqi_attach),
	DEVMETHOD(device_detach,	smartpqi_detach),
	DEVMETHOD(device_suspend,	smartpqi_suspend),
	DEVMETHOD(device_resume,	smartpqi_resume),
	{ 0, 0 }
};

static devclass_t	pqi_devclass;
static driver_t smartpqi_pci_driver = {
	"smartpqi",
	pqi_methods,
	sizeof(struct pqisrc_softstate)
};

DRIVER_MODULE(smartpqi, pci, smartpqi_pci_driver, pqi_devclass, 0, 0);
MODULE_DEPEND(smartpqi, pci, 1, 1, 1);
