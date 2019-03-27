/*-
 * Copyright (c) 2017 Broadcom. All rights reserved.
 * The term "Broadcom" refers to Broadcom Limited and/or its subsidiaries.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#define OCS_COPYRIGHT "Copyright (C) 2017 Broadcom. All rights reserved."

/**
 * @file
 * Implementation of required FreeBSD PCI interface functions
 */

#include "ocs.h"
#include "version.h"
#include <sys/sysctl.h>
#include <sys/malloc.h>

static MALLOC_DEFINE(M_OCS, "OCS", "OneCore Storage data");

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <machine/bus.h>

/**
 * Tunable parameters for transport
 */
int logmask = 0;
int ctrlmask = 2;
int logdest = 1;
int loglevel = LOG_INFO;
int ramlog_size = 1*1024*1024;
int ddump_saved_size = 0;
static const char *queue_topology = "eq cq rq cq mq $nulp($nwq(cq wq:ulp=$rpt1)) cq wq:len=256:class=1";

static void ocs_release_bus(struct ocs_softc *);
static int32_t ocs_intr_alloc(struct ocs_softc *);
static int32_t ocs_intr_setup(struct ocs_softc *);
static int32_t ocs_intr_teardown(struct ocs_softc *);
static int ocs_pci_intx_filter(void *);
static void ocs_pci_intr(void *);
static int32_t ocs_init_dma_tag(struct ocs_softc *ocs);

static int32_t ocs_setup_fcports(ocs_t *ocs);

ocs_t *ocs_devices[MAX_OCS_DEVICES];

/**
 * @brief Check support for the given device
 *
 * Determine support for a given device by examining the PCI vendor and
 * device IDs
 *
 * @param dev device abstraction
 *
 * @return 0 if device is supported, ENXIO otherwise
 */
static int
ocs_pci_probe(device_t dev)
{
	char	*desc = NULL;

	if (pci_get_vendor(dev) != PCI_VENDOR_EMULEX) { 
		return ENXIO;
	}

	switch (pci_get_device(dev)) {
	case PCI_PRODUCT_EMULEX_OCE16001:
		desc = "Emulex LightPulse FC Adapter";
		break;
	case PCI_PRODUCT_EMULEX_LPE31004:
		desc = "Emulex LightPulse FC Adapter";
		break;
	case PCI_PRODUCT_EMULEX_OCE50102:
		desc = "Emulex LightPulse 10GbE FCoE/NIC Adapter";
		break;
	default:
		return ENXIO;
	}

	device_set_desc(dev, desc);

	return BUS_PROBE_DEFAULT;
}

static int
ocs_map_bars(device_t dev, struct ocs_softc *ocs)
{

	/*
	 * Map PCI BAR0 register into the CPU's space.
	 */

	ocs->reg[0].rid = PCIR_BAR(PCI_64BIT_BAR0);
	ocs->reg[0].res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
			&ocs->reg[0].rid, RF_ACTIVE);

	if (ocs->reg[0].res == NULL) {
		device_printf(dev, "bus_alloc_resource failed rid=%#x\n",
				ocs->reg[0].rid);
		return ENXIO;
	}

	ocs->reg[0].btag = rman_get_bustag(ocs->reg[0].res);
	ocs->reg[0].bhandle = rman_get_bushandle(ocs->reg[0].res);
	return 0;
}


static int
ocs_setup_params(struct ocs_softc *ocs)
{
	int32_t	i = 0;
	const char	*hw_war_version;
	/* Setup tunable parameters */
	ocs->ctrlmask = ctrlmask;
	ocs->speed = 0;
	ocs->topology = 0;
	ocs->ethernet_license = 0;
	ocs->num_scsi_ios = 8192;
	ocs->enable_hlm = 0;
	ocs->hlm_group_size = 8;
	ocs->logmask = logmask;

	ocs->config_tgt = FALSE;
	if (0 == resource_int_value(device_get_name(ocs->dev), device_get_unit(ocs->dev),
					"target", &i)) {
		if (1 == i) {
			ocs->config_tgt = TRUE;
			device_printf(ocs->dev, "Enabling target\n");
		}
	}

	ocs->config_ini = TRUE;
	if (0 == resource_int_value(device_get_name(ocs->dev), device_get_unit(ocs->dev),
					"initiator", &i)) {
		if (0 == i) {
			ocs->config_ini = FALSE;
			device_printf(ocs->dev, "Disabling initiator\n");
		}
	}
	ocs->enable_ini = ocs->config_ini;

	if (!ocs->config_ini && !ocs->config_tgt) {
		device_printf(ocs->dev, "Unsupported, both initiator and target mode disabled.\n");
		return 1;

        }

	if (0 == resource_int_value(device_get_name(ocs->dev), device_get_unit(ocs->dev),
					"logmask", &logmask)) {
		device_printf(ocs->dev, "logmask = %#x\n", logmask);
	}

	if (0 == resource_int_value(device_get_name(ocs->dev), device_get_unit(ocs->dev),
					"logdest", &logdest)) {
		device_printf(ocs->dev, "logdest = %#x\n", logdest);
	}

	if (0 == resource_int_value(device_get_name(ocs->dev), device_get_unit(ocs->dev),
					"loglevel", &loglevel)) {
		device_printf(ocs->dev, "loglevel = %#x\n", loglevel);
	}

	if (0 == resource_int_value(device_get_name(ocs->dev), device_get_unit(ocs->dev),
					"ramlog_size", &ramlog_size)) {
		device_printf(ocs->dev, "ramlog_size = %#x\n", ramlog_size);
	}

	if (0 == resource_int_value(device_get_name(ocs->dev), device_get_unit(ocs->dev),
					"ddump_saved_size", &ddump_saved_size)) {
		device_printf(ocs->dev, "ddump_saved_size= %#x\n", ddump_saved_size);
	}

	/* If enabled, initailize a RAM logging buffer */
	if (logdest & 2) {
		ocs->ramlog = ocs_ramlog_init(ocs, ramlog_size/OCS_RAMLOG_DEFAULT_BUFFERS,
			OCS_RAMLOG_DEFAULT_BUFFERS);
		/* If NULL was returned, then we'll simply skip using the ramlog but */
		/* set logdest to 1 to ensure that we at least get default logging.  */
		if (ocs->ramlog == NULL) {
			logdest = 1;
		}
	}

	/* initialize a saved ddump */
	if (ddump_saved_size) {
		if (ocs_textbuf_alloc(ocs, &ocs->ddump_saved, ddump_saved_size)) {
			ocs_log_err(ocs, "failed to allocate memory for saved ddump\n");
		}
	}

	if (0 == resource_string_value(device_get_name(ocs->dev), device_get_unit(ocs->dev),
					"hw_war_version", &hw_war_version)) {
		device_printf(ocs->dev, "hw_war_version = %s\n", hw_war_version);
		ocs->hw_war_version = strdup(hw_war_version, M_OCS);
	}

	if (0 == resource_int_value(device_get_name(ocs->dev), device_get_unit(ocs->dev),
				    "explicit_buffer_list", &i)) {
		ocs->explicit_buffer_list = i;
	}

	if (0 == resource_int_value(device_get_name(ocs->dev), device_get_unit(ocs->dev),
					"ethernet_license", &i)) {
		ocs->ethernet_license = i;
	}

	if (0 == resource_int_value(device_get_name(ocs->dev), device_get_unit(ocs->dev),
					"speed", &i)) {
		device_printf(ocs->dev, "speed = %d Mbps\n", i);
		ocs->speed = i;
	}
	ocs->desc = device_get_desc(ocs->dev);

	ocs_device_lock_init(ocs);
	ocs->driver_version = STR_BE_MAJOR "." STR_BE_MINOR "." STR_BE_BUILD "." STR_BE_BRANCH;
	ocs->model = ocs_pci_model(ocs->pci_vendor, ocs->pci_device);

	if (0 == resource_int_value(device_get_name(ocs->dev), device_get_unit(ocs->dev),
				    "enable_hlm", &i)) {
		device_printf(ocs->dev, "enable_hlm = %d\n", i);
		ocs->enable_hlm = i;
		if (ocs->enable_hlm) {
			ocs->hlm_group_size = 8;

			if (0 == resource_int_value(device_get_name(ocs->dev), device_get_unit(ocs->dev),
						    "hlm_group_size", &i)) {
				ocs->hlm_group_size = i;
			}
			device_printf(ocs->dev, "hlm_group_size = %d\n", i);
		}
	}

	if (0 == resource_int_value(device_get_name(ocs->dev), device_get_unit(ocs->dev),
					"num_scsi_ios", &i)) {
		ocs->num_scsi_ios = i;
		device_printf(ocs->dev, "num_scsi_ios = %d\n", ocs->num_scsi_ios);
	} else {
		ocs->num_scsi_ios = 8192;
	}

	if (0 == resource_int_value(device_get_name(ocs->dev), device_get_unit(ocs->dev),
					"topology", &i)) {
		ocs->topology = i;
		device_printf(ocs->dev, "Setting topology=%#x\n", i);
	}

	if (0 == resource_int_value(device_get_name(ocs->dev), device_get_unit(ocs->dev),
				    "num_vports", &i)) {
		if (i >= 0 && i <= 254) {
			device_printf(ocs->dev, "num_vports = %d\n", i);
			ocs->num_vports = i;
		} else {
			device_printf(ocs->dev, "num_vports: %d not supported \n", i);
		}
	}


	if (0 == resource_int_value(device_get_name(ocs->dev), device_get_unit(ocs->dev),
				    "external_loopback", &i)) {
		device_printf(ocs->dev, "external_loopback = %d\n", i);
		ocs->external_loopback = i;
	}

	if (0 == resource_int_value(device_get_name(ocs->dev), device_get_unit(ocs->dev),
				    "tgt_rscn_delay", &i)) {
		device_printf(ocs->dev, "tgt_rscn_delay = %d\n", i);
		ocs->tgt_rscn_delay_msec = i * 1000;
	}

	if (0 == resource_int_value(device_get_name(ocs->dev), device_get_unit(ocs->dev),
				    "tgt_rscn_period", &i)) {
		device_printf(ocs->dev, "tgt_rscn_period = %d\n", i);
		ocs->tgt_rscn_period_msec = i * 1000;
	}

	if (0 == resource_int_value(device_get_name(ocs->dev), device_get_unit(ocs->dev),
				    "target_io_timer", &i)) {
		device_printf(ocs->dev, "target_io_timer = %d\n", i);
		ocs->target_io_timer_sec = i;
	}

	hw_global.queue_topology_string = queue_topology;
	ocs->rq_selection_policy = 0;
	ocs->rr_quanta = 1;
	ocs->filter_def = "0,0,0,0";
	
	return 0;
}

static int32_t
ocs_setup_fcports(ocs_t *ocs)
{
	uint32_t        i = 0, role = 0;
	uint64_t sli_wwpn, sli_wwnn;
	size_t size;
	ocs_xport_t *xport = ocs->xport;
	ocs_vport_spec_t *vport;
	ocs_fcport *fcp = NULL;

	size = sizeof(ocs_fcport) * (ocs->num_vports + 1);

	ocs->fcports = ocs_malloc(ocs, size, M_ZERO|M_NOWAIT);
	if (ocs->fcports == NULL) {
		device_printf(ocs->dev, "Can't allocate fcport \n");
		return 1;
	}

	role = (ocs->enable_ini)? KNOB_ROLE_INITIATOR: 0 |
		(ocs->enable_tgt)? KNOB_ROLE_TARGET: 0;

	fcp = FCPORT(ocs, i);
	fcp->role = role;
	i++;

	ocs_list_foreach(&xport->vport_list, vport) {
		fcp = FCPORT(ocs, i);
		vport->tgt_data = fcp;
		fcp->vport = vport;
		fcp->role = role;

		if (ocs_hw_get_def_wwn(ocs, i, &sli_wwpn, &sli_wwnn)) {
			ocs_log_err(ocs, "Get default wwn failed \n");
			i++;
			continue;
		}

		vport->wwpn = ocs_be64toh(sli_wwpn);
		vport->wwnn = ocs_be64toh(sli_wwnn);
		i++;
		ocs_log_debug(ocs, "VPort wwpn: %lx wwnn: %lx \n", vport->wwpn, vport->wwnn);
	}

	return 0;
}

int32_t
ocs_device_attach(ocs_t *ocs)
{
        int32_t i;
	ocs_io_t *io = NULL;

        if (ocs->attached) {
                ocs_log_warn(ocs, "%s: Device is already attached\n", __func__);
                return -1;
        } 

	/* Allocate transport object and bring online */
	ocs->xport = ocs_xport_alloc(ocs);
	if (ocs->xport == NULL) {
		device_printf(ocs->dev, "failed to allocate transport object\n");
		return ENOMEM;
	} else if (ocs_xport_attach(ocs->xport) != 0) {
		device_printf(ocs->dev, "%s: failed to attach transport object\n", __func__);
		goto fail_xport_attach;
	} else if (ocs_xport_initialize(ocs->xport) != 0) {
		device_printf(ocs->dev, "%s: failed to initialize transport object\n", __func__);
		goto fail_xport_init;
	}

	if (ocs_init_dma_tag(ocs)) {
		goto fail_intr_setup; 
	}

	for (i = 0; (io = ocs_io_get_instance(ocs, i)); i++) {
		if (bus_dmamap_create(ocs->buf_dmat, 0, &io->tgt_io.dmap)) {
			device_printf(ocs->dev, "%s: bad dma map create\n", __func__);
		}

		io->tgt_io.state = OCS_CAM_IO_FREE;
	}

	if (ocs_setup_fcports(ocs)) {
		device_printf(ocs->dev, "FCports creation failed\n");
		goto fail_intr_setup;
	}

	if(ocs_cam_attach(ocs)) {
		device_printf(ocs->dev, "cam attach failed \n");
		goto fail_intr_setup;
	}

	if (ocs_intr_setup(ocs)) {
		device_printf(ocs->dev, "Interrupt setup failed\n");
		goto fail_intr_setup;
	}

	if (ocs->enable_ini || ocs->enable_tgt) {
		if (ocs_xport_control(ocs->xport, OCS_XPORT_PORT_ONLINE)) {
			device_printf(ocs->dev, "Can't init port\n");
			goto fail_xport_online;
		}
	}

	ocs->attached = true;
	
	return 0;

fail_xport_online:
	if (ocs_xport_control(ocs->xport, OCS_XPORT_SHUTDOWN)) {
		device_printf(ocs->dev, "Transport Shutdown timed out\n");
	}
	ocs_intr_teardown(ocs);
fail_intr_setup:
fail_xport_init:
	ocs_xport_detach(ocs->xport);
	if (ocs->config_tgt)
		ocs_scsi_tgt_del_device(ocs);

	ocs_xport_free(ocs->xport);
	ocs->xport = NULL;
fail_xport_attach:
	if (ocs->xport)	
		ocs_free(ocs, ocs->xport, sizeof(*(ocs->xport)));
	ocs->xport = NULL;
	return ENXIO;
}

/**
 * @brief Connect the driver to the given device
 *
 * If the probe routine is successful, the OS will give the driver
 * the opportunity to connect itself to the device. This routine
 * maps PCI resources (memory BARs and interrupts) and initialize a
 * hardware object.
 *
 * @param dev device abstraction
 *
 * @return 0 if the driver attaches to the device, ENXIO otherwise
 */

static int
ocs_pci_attach(device_t dev)
{
	struct ocs_softc	*ocs;
	int			instance;

	instance = device_get_unit(dev);
	
	ocs = (struct ocs_softc *)device_get_softc(dev);
	if (NULL == ocs) {
		device_printf(dev, "cannot allocate softc\n");
		return ENOMEM;
	}
	memset(ocs, 0, sizeof(struct ocs_softc));

	if (instance < ARRAY_SIZE(ocs_devices)) {
		ocs_devices[instance] = ocs;
	} else {
		device_printf(dev, "got unexpected ocs instance number %d\n", instance);
	}

	ocs->instance_index = instance;

	ocs->dev = dev;

	pci_enable_io(dev, SYS_RES_MEMORY);
	pci_enable_busmaster(dev);

	ocs->pci_vendor = pci_get_vendor(dev);
	ocs->pci_device = pci_get_device(dev);
	snprintf(ocs->businfo, sizeof(ocs->businfo), "%02X:%02X:%02X",
		pci_get_bus(dev), pci_get_slot(dev), pci_get_function(dev));

	/* Map all memory BARs */
        if (ocs_map_bars(dev, ocs)) {
		device_printf(dev, "Failed to map pci bars\n");
		goto release_bus;
        }
	
	/* create a root DMA tag for the device */
	if (bus_dma_tag_create(bus_get_dma_tag(dev),
				1,		/* byte alignment */
				0,		/* no boundary restrictions */
				BUS_SPACE_MAXADDR, /* no minimum low address */
				BUS_SPACE_MAXADDR, /* no maximum high address */
				NULL,		/* no filter function */
				NULL,		/* or arguments */
				BUS_SPACE_MAXSIZE, /* max size covered by tag */
				BUS_SPACE_UNRESTRICTED, /* no segment count restrictions */
				BUS_SPACE_MAXSIZE, /* no segment length restrictions */
				0,		/* flags */
				NULL,		/* no lock manipulation function */
				NULL,		/* or arguments */
				&ocs->dmat)) {
		device_printf(dev, "parent DMA tag allocation failed\n");
		goto release_bus;
	}

	if (ocs_intr_alloc(ocs)) {
		device_printf(dev, "Interrupt allocation failed\n");
		goto release_bus;
	}

	if (PCIC_SERIALBUS == pci_get_class(dev) &&
			PCIS_SERIALBUS_FC == pci_get_subclass(dev))
		ocs->ocs_xport = OCS_XPORT_FC;
	else {
		device_printf(dev, "unsupported class (%#x : %#x)\n",
				pci_get_class(dev),
				pci_get_class(dev));
		goto release_bus;
	}

	/* Setup tunable parameters */
	if (ocs_setup_params(ocs)) {
		device_printf(ocs->dev, "failed to setup params\n");
		goto release_bus;
	}

	if (ocs_device_attach(ocs)) {
		device_printf(ocs->dev, "failed to attach device\n");
		goto release_params;
	}

	ocs->fc_type = FC_TYPE_FCP;

	ocs_debug_attach(ocs);

	return 0;

release_params:
	ocs_ramlog_free(ocs, ocs->ramlog);
	ocs_device_lock_free(ocs);
	free(ocs->hw_war_version, M_OCS);
release_bus:
	ocs_release_bus(ocs);
	return ENXIO;
}

/**
 * @brief free resources when pci device detach
 *
 * @param ocs pointer to ocs structure
 *
 * @return 0 for success, a negative error code value for failure.
 */

int32_t
ocs_device_detach(ocs_t *ocs)
{
        int32_t rc = 0, i;
	ocs_io_t *io = NULL;

        if (ocs != NULL) {
                if (!ocs->attached) {
                        ocs_log_warn(ocs, "%s: Device is not attached\n", __func__);
                        return -1;
                }

                ocs->attached = FALSE;

                rc = ocs_xport_control(ocs->xport, OCS_XPORT_SHUTDOWN);
                if (rc) {
                        ocs_log_err(ocs, "%s: Transport Shutdown timed out\n", __func__);
                }

		ocs_intr_teardown(ocs);

                if (ocs_xport_detach(ocs->xport) != 0) {
                        ocs_log_err(ocs, "%s: Transport detach failed\n", __func__);
                }

		ocs_cam_detach(ocs);
		ocs_free(ocs, ocs->fcports, sizeof(*(ocs->fcports)));

		for (i = 0; (io = ocs_io_get_instance(ocs, i)); i++) {
			if (bus_dmamap_destroy(ocs->buf_dmat, io->tgt_io.dmap)) {
				device_printf(ocs->dev, "%s: bad dma map destroy\n", __func__);
			}
		}
		bus_dma_tag_destroy(ocs->dmat);
                ocs_xport_free(ocs->xport);
                ocs->xport = NULL;

        }

        return 0;
}


/**
 * @brief Detach the driver from the given device
 *
 * If the driver is a loadable module, this routine gets called at unload
 * time. This routine will stop the device and free any allocated resources.
 *
 * @param dev device abstraction
 *
 * @return 0 if the driver detaches from the device, ENXIO otherwise
 */
static int
ocs_pci_detach(device_t dev)
{
	struct ocs_softc	*ocs;

	ocs = (struct ocs_softc *)device_get_softc(dev);
	if (!ocs) {
		device_printf(dev, "no driver context?!?\n");
		return -1;
	}

	if (ocs->config_tgt && ocs->enable_tgt) {
		device_printf(dev, "can't detach with target mode enabled\n");
		return EBUSY;
	}

	ocs_device_detach(ocs);

	/*
	 * Workaround for OCS SCSI Transport quirk.
	 *
	 * CTL requires that target mode is disabled prior to unloading the
	 * driver (ie ocs->enable_tgt = FALSE), but once the target is disabled,
	 * the transport will not call ocs_scsi_tgt_del_device() which deallocates
	 * CAM resources. The workaround is to explicitly make the call here.
	 */
	if (ocs->config_tgt)
		ocs_scsi_tgt_del_device(ocs);
        
	/* free strdup created buffer.*/
	free(ocs->hw_war_version, M_OCS);

	ocs_device_lock_free(ocs);

	ocs_debug_detach(ocs);

	ocs_ramlog_free(ocs, ocs->ramlog);

	ocs_release_bus(ocs);

	return 0;
}

/**
 * @brief Notify driver of system shutdown
 *
 * @param dev device abstraction
 *
 * @return 0 if the driver attaches to the device, ENXIO otherwise
 */
static int
ocs_pci_shutdown(device_t dev)
{
	device_printf(dev, "%s\n", __func__);
	return 0;
}

/**
 * @brief Release bus resources allocated within the soft context
 *
 * @param ocs Pointer to the driver's context
 *
 * @return none
 */
static void
ocs_release_bus(struct ocs_softc *ocs)
{

	if (NULL != ocs) {
		uint32_t	i;

		ocs_intr_teardown(ocs);

		if (ocs->irq) {
			bus_release_resource(ocs->dev, SYS_RES_IRQ,
					rman_get_rid(ocs->irq), ocs->irq);

			if (ocs->n_vec) {
				pci_release_msi(ocs->dev);
				ocs->n_vec = 0;
			}

			ocs->irq = NULL;
		}

		bus_dma_tag_destroy(ocs->dmat);

		for (i = 0; i < PCI_MAX_BAR; i++) {
			if (ocs->reg[i].res) {
				bus_release_resource(ocs->dev, SYS_RES_MEMORY,
						ocs->reg[i].rid,
						ocs->reg[i].res);
			}
		}
	}
}

/**
 * @brief Allocate and initialize interrupts
 *
 * @param ocs Pointer to the driver's context
 *
 * @return none
 */
static int32_t
ocs_intr_alloc(struct ocs_softc *ocs)
{

	ocs->n_vec = 1;
	if (pci_alloc_msix(ocs->dev, &ocs->n_vec)) {
		device_printf(ocs->dev, "MSI-X allocation failed\n");
		if (pci_alloc_msi(ocs->dev, &ocs->n_vec)) {
			device_printf(ocs->dev, "MSI allocation failed \n");
			ocs->irqid = 0;
			ocs->n_vec = 0;
		} else 
			ocs->irqid = 1;
	} else {
		ocs->irqid = 1;
	}

	ocs->irq = bus_alloc_resource_any(ocs->dev, SYS_RES_IRQ, &ocs->irqid,
			RF_ACTIVE | RF_SHAREABLE);
	if (NULL == ocs->irq) {
		device_printf(ocs->dev, "could not allocate interrupt\n");
		return -1;
	}

	ocs->intr_ctx.vec = 0;
	ocs->intr_ctx.softc = ocs;
	snprintf(ocs->intr_ctx.name, sizeof(ocs->intr_ctx.name),
			"%s_intr_%d",
			device_get_nameunit(ocs->dev),
			ocs->intr_ctx.vec);

	return 0;
}

/**
 * @brief Create and attach an interrupt handler
 *
 * @param ocs Pointer to the driver's context
 *
 * @return 0 on success, non-zero otherwise
 */
static int32_t
ocs_intr_setup(struct ocs_softc *ocs)
{
	driver_filter_t	*filter = NULL;

	if (0 == ocs->n_vec) {
		filter = ocs_pci_intx_filter;
	}

	if (bus_setup_intr(ocs->dev, ocs->irq, INTR_MPSAFE | INTR_TYPE_CAM,
				filter, ocs_pci_intr, &ocs->intr_ctx,
				&ocs->tag)) {
		device_printf(ocs->dev, "could not initialize interrupt\n");
		return -1;
	}

	return 0;
}


/**
 * @brief Detach an interrupt handler
 *
 * @param ocs Pointer to the driver's context
 *
 * @return 0 on success, non-zero otherwise
 */
static int32_t
ocs_intr_teardown(struct ocs_softc *ocs)
{

	if (!ocs) {
		printf("%s: bad driver context?!?\n", __func__);
		return -1;
	}

	if (ocs->tag) {
		bus_teardown_intr(ocs->dev, ocs->irq, ocs->tag);
		ocs->tag = NULL;
	}

	return 0;
}

/**
 * @brief PCI interrupt handler
 *
 * @param arg pointer to the driver's software context
 *
 * @return FILTER_HANDLED if interrupt is processed, FILTER_STRAY otherwise
 */
static int
ocs_pci_intx_filter(void *arg)
{
	ocs_intr_ctx_t	*intr = arg;
	struct ocs_softc *ocs = NULL;
	uint16_t	val = 0;

	if (NULL == intr) {
		return FILTER_STRAY;
	}

	ocs = intr->softc;
#ifndef PCIM_STATUS_INTR
#define PCIM_STATUS_INTR	0x0008
#endif
	val = pci_read_config(ocs->dev, PCIR_STATUS, 2);
	if (0xffff == val) {
		device_printf(ocs->dev, "%s: pci_read_config(PCIR_STATUS) failed\n", __func__);
		return FILTER_STRAY;
	}
	if (0 == (val & PCIM_STATUS_INTR)) {
		return FILTER_STRAY;
	}

	val = pci_read_config(ocs->dev, PCIR_COMMAND, 2);
	val |= PCIM_CMD_INTxDIS;
	pci_write_config(ocs->dev, PCIR_COMMAND, val, 2);

	return FILTER_SCHEDULE_THREAD;
}

/**
 * @brief interrupt handler
 *
 * @param context pointer to the interrupt context
 */
static void
ocs_pci_intr(void *context)
{
	ocs_intr_ctx_t	*intr = context;
	struct ocs_softc *ocs = intr->softc;

	mtx_lock(&ocs->sim_lock);
		ocs_hw_process(&ocs->hw, intr->vec, OCS_OS_MAX_ISR_TIME_MSEC);
	mtx_unlock(&ocs->sim_lock);
}

/**
 * @brief Initialize DMA tag
 *
 * @param ocs the driver instance's software context
 *
 * @return 0 on success, non-zero otherwise
 */
static int32_t
ocs_init_dma_tag(struct ocs_softc *ocs)
{
	uint32_t	max_sgl = 0;
	uint32_t	max_sge = 0;

	/*
	 * IOs can't use the parent DMA tag and must create their
	 * own, based primarily on a restricted number of DMA segments.
	 * This is more of a BSD requirement than a SLI Port requirement
	 */
	ocs_hw_get(&ocs->hw, OCS_HW_N_SGL, &max_sgl);
	ocs_hw_get(&ocs->hw, OCS_HW_MAX_SGE, &max_sge);

	if (bus_dma_tag_create(ocs->dmat,
				1,		/* byte alignment */
				0,		/* no boundary restrictions */
				BUS_SPACE_MAXADDR, /* no minimum low address */
				BUS_SPACE_MAXADDR, /* no maximum high address */
				NULL,		/* no filter function */
				NULL,		/* or arguments */
				BUS_SPACE_MAXSIZE, /* max size covered by tag */
				max_sgl, 	/* segment count restrictions */
				max_sge,	/* segment length restrictions */
				0,		/* flags */
				NULL,		/* no lock manipulation function */
				NULL,		/* or arguments */
				&ocs->buf_dmat)) {
		device_printf(ocs->dev, "%s: bad bus_dma_tag_create(buf_dmat)\n", __func__);
		return -1;
	}
	return 0;
}

int32_t
ocs_get_property(const char *prop_name, char *buffer, uint32_t buffer_len)
{
	return -1;
}

/**
 * @brief return pointer to ocs structure given instance index
 *
 * A pointer to an ocs structure is returned given an instance index.
 *
 * @param index index to ocs_devices array
 *
 * @return ocs pointer
 */

ocs_t *ocs_get_instance(uint32_t index)
{
	if (index < ARRAY_SIZE(ocs_devices)) {
		return ocs_devices[index];
	}
	return NULL;
}

/**
 * @brief Return instance index of an opaque ocs structure
 *
 * Returns the ocs instance index
 *
 * @param os pointer to ocs instance
 *
 * @return pointer to ocs instance index
 */
uint32_t
ocs_instance(void *os)
{
	ocs_t *ocs = os;
	return ocs->instance_index;
}

static device_method_t ocs_methods[] = {
	DEVMETHOD(device_probe,		ocs_pci_probe),
	DEVMETHOD(device_attach,	ocs_pci_attach),
	DEVMETHOD(device_detach,	ocs_pci_detach),
	DEVMETHOD(device_shutdown,	ocs_pci_shutdown),
	{0, 0}
};

static driver_t ocs_driver = {
	"ocs_fc",
	ocs_methods,
	sizeof(struct ocs_softc)
};

static devclass_t ocs_devclass;

DRIVER_MODULE(ocs_fc, pci, ocs_driver, ocs_devclass, 0, 0);
MODULE_VERSION(ocs_fc, 1);

