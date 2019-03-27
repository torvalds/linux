/*-
 * Copyright (c) 2016 Netflix, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/ioccom.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/smp.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>
#include <cam/cam_debug.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include "nvme_private.h"

#define ccb_accb_ptr spriv_ptr0
#define ccb_ctrlr_ptr spriv_ptr1
static void	nvme_sim_action(struct cam_sim *sim, union ccb *ccb);
static void	nvme_sim_poll(struct cam_sim *sim);

#define sim2softc(sim)	((struct nvme_sim_softc *)cam_sim_softc(sim))
#define sim2ctrlr(sim)	(sim2softc(sim)->s_ctrlr)

struct nvme_sim_softc
{
	struct nvme_controller	*s_ctrlr;
	struct cam_sim		*s_sim;
	struct cam_path		*s_path;
};

static void
nvme_sim_nvmeio_done(void *ccb_arg, const struct nvme_completion *cpl)
{
	union ccb *ccb = (union ccb *)ccb_arg;

	/*
	 * Let the periph know the completion, and let it sort out what
	 * it means. Make our best guess, though for the status code.
	 */
	memcpy(&ccb->nvmeio.cpl, cpl, sizeof(*cpl));
	ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
	if (nvme_completion_is_error(cpl)) {
		ccb->ccb_h.status = CAM_REQ_CMP_ERR;
		xpt_done(ccb);
	} else {
		ccb->ccb_h.status = CAM_REQ_CMP;
		xpt_done_direct(ccb);
	}
}

static void
nvme_sim_nvmeio(struct cam_sim *sim, union ccb *ccb)
{
	struct ccb_nvmeio	*nvmeio = &ccb->nvmeio;
	struct nvme_request	*req;
	void			*payload;
	uint32_t		size;
	struct nvme_controller *ctrlr;

	ctrlr = sim2ctrlr(sim);
	payload = nvmeio->data_ptr;
	size = nvmeio->dxfer_len;
	/* SG LIST ??? */
	if ((nvmeio->ccb_h.flags & CAM_DATA_MASK) == CAM_DATA_BIO)
		req = nvme_allocate_request_bio((struct bio *)payload,
		    nvme_sim_nvmeio_done, ccb);
	else if ((nvmeio->ccb_h.flags & CAM_DATA_SG) == CAM_DATA_SG)
		req = nvme_allocate_request_ccb(ccb, nvme_sim_nvmeio_done, ccb);
	else if (payload == NULL)
		req = nvme_allocate_request_null(nvme_sim_nvmeio_done, ccb);
	else
		req = nvme_allocate_request_vaddr(payload, size,
		    nvme_sim_nvmeio_done, ccb);

	if (req == NULL) {
		nvmeio->ccb_h.status = CAM_RESRC_UNAVAIL;
		xpt_done(ccb);
		return;
	}
	ccb->ccb_h.status |= CAM_SIM_QUEUED;

	memcpy(&req->cmd, &ccb->nvmeio.cmd, sizeof(ccb->nvmeio.cmd));

	if (ccb->ccb_h.func_code == XPT_NVME_IO)
		nvme_ctrlr_submit_io_request(ctrlr, req);
	else
		nvme_ctrlr_submit_admin_request(ctrlr, req);
}

static uint32_t
nvme_link_kBps(struct nvme_controller *ctrlr)
{
	uint32_t speed, lanes, link[] = { 1, 250000, 500000, 985000, 1970000 };
	uint32_t status;

	status = pcie_read_config(ctrlr->dev, PCIER_LINK_STA, 2);
	speed = status & PCIEM_LINK_STA_SPEED;
	lanes = (status & PCIEM_LINK_STA_WIDTH) >> 4;
	/*
	 * Failsafe on link speed indicator. If it is insane report the number of
	 * lanes as the speed. Not 100% accurate, but may be diagnostic.
	 */
	if (speed >= nitems(link))
		speed = 0;
	return link[speed] * lanes;
}

static void
nvme_sim_action(struct cam_sim *sim, union ccb *ccb)
{
	struct nvme_controller *ctrlr;

	CAM_DEBUG(ccb->ccb_h.path, CAM_DEBUG_TRACE,
	    ("nvme_sim_action: func= %#x\n",
		ccb->ccb_h.func_code));

	ctrlr = sim2ctrlr(sim);

	switch (ccb->ccb_h.func_code) {
	case XPT_CALC_GEOMETRY:		/* Calculate Geometry Totally nuts ? XXX */
		/* 
		 * Only meaningful for old-school SCSI disks since only the SCSI
		 * da driver generates them. Reject all these that slip through.
		 */
		/*FALLTHROUGH*/
	case XPT_ABORT:			/* Abort the specified CCB */
		ccb->ccb_h.status = CAM_REQ_INVALID;
		break;
	case XPT_SET_TRAN_SETTINGS:
		/*
		 * NVMe doesn't really have different transfer settings, but
		 * other parts of CAM think failure here is a big deal.
		 */
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	case XPT_PATH_INQ:		/* Path routing inquiry */
	{
		struct ccb_pathinq	*cpi = &ccb->cpi;
		device_t		dev = ctrlr->dev;

		/*
		 * NVMe may have multiple LUNs on the same path. Current generation
		 * of NVMe devives support only a single name space. Multiple name
		 * space drives are coming, but it's unclear how we should report
		 * them up the stack.
		 */
		cpi->version_num = 1;
		cpi->hba_inquiry = 0;
		cpi->target_sprt = 0;
		cpi->hba_misc =  PIM_UNMAPPED | PIM_NOSCAN;
		cpi->hba_eng_cnt = 0;
		cpi->max_target = 0;
		cpi->max_lun = ctrlr->cdata.nn;
		cpi->maxio = ctrlr->max_xfer_size;
		cpi->initiator_id = 0;
		cpi->bus_id = cam_sim_bus(sim);
		cpi->base_transfer_speed = nvme_link_kBps(ctrlr);
		strlcpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
		strlcpy(cpi->hba_vid, "NVMe", HBA_IDLEN);
		strlcpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
		cpi->unit_number = cam_sim_unit(sim);
		cpi->transport = XPORT_NVME;		/* XXX XPORT_PCIE ? */
		cpi->transport_version = nvme_mmio_read_4(ctrlr, vs);
		cpi->protocol = PROTO_NVME;
		cpi->protocol_version = nvme_mmio_read_4(ctrlr, vs);
		cpi->xport_specific.nvme.nsid = xpt_path_lun_id(ccb->ccb_h.path);
		cpi->xport_specific.nvme.domain = pci_get_domain(dev);
		cpi->xport_specific.nvme.bus = pci_get_bus(dev);
		cpi->xport_specific.nvme.slot = pci_get_slot(dev);
		cpi->xport_specific.nvme.function = pci_get_function(dev);
		cpi->xport_specific.nvme.extra = 0;
		cpi->ccb_h.status = CAM_REQ_CMP;
		break;
	}
	case XPT_GET_TRAN_SETTINGS:	/* Get transport settings */
	{
		struct ccb_trans_settings	*cts;
		struct ccb_trans_settings_nvme	*nvmep;
		struct ccb_trans_settings_nvme	*nvmex;
		device_t dev;
		uint32_t status, caps;

		dev = ctrlr->dev;
		cts = &ccb->cts;
		nvmex = &cts->xport_specific.nvme;
		nvmep = &cts->proto_specific.nvme;

		status = pcie_read_config(dev, PCIER_LINK_STA, 2);
		caps = pcie_read_config(dev, PCIER_LINK_CAP, 2);
		nvmex->valid = CTS_NVME_VALID_SPEC | CTS_NVME_VALID_LINK;
		nvmex->spec = nvme_mmio_read_4(ctrlr, vs);
		nvmex->speed = status & PCIEM_LINK_STA_SPEED;
		nvmex->lanes = (status & PCIEM_LINK_STA_WIDTH) >> 4;
		nvmex->max_speed = caps & PCIEM_LINK_CAP_MAX_SPEED;
		nvmex->max_lanes = (caps & PCIEM_LINK_CAP_MAX_WIDTH) >> 4;

		/* XXX these should be something else maybe ? */
		nvmep->valid = 1;
		nvmep->spec = nvmex->spec;

		cts->transport = XPORT_NVME;
		cts->protocol = PROTO_NVME;
		cts->ccb_h.status = CAM_REQ_CMP;
		break;
	}
	case XPT_TERM_IO:		/* Terminate the I/O process */
		/*
		 * every driver handles this, but nothing generates it. Assume
		 * it's OK to just say 'that worked'.
		 */
		/*FALLTHROUGH*/
	case XPT_RESET_DEV:		/* Bus Device Reset the specified device */
	case XPT_RESET_BUS:		/* Reset the specified bus */
		/*
		 * NVMe doesn't really support physically resetting the bus. It's part
		 * of the bus scanning dance, so return sucess to tell the process to
		 * proceed.
		 */
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	case XPT_NVME_IO:		/* Execute the requested I/O operation */
	case XPT_NVME_ADMIN:		/* or Admin operation */
		nvme_sim_nvmeio(sim, ccb);
		return;			/* no done */
	default:
		ccb->ccb_h.status = CAM_REQ_INVALID;
		break;
	}
	xpt_done(ccb);
}

static void
nvme_sim_poll(struct cam_sim *sim)
{

	nvme_ctrlr_poll(sim2ctrlr(sim));
}

static void *
nvme_sim_new_controller(struct nvme_controller *ctrlr)
{
	struct nvme_sim_softc *sc;
	struct cam_devq *devq;
	int max_trans;

	max_trans = ctrlr->max_hw_pend_io;
	devq = cam_simq_alloc(max_trans);
	if (devq == NULL)
		return (NULL);

	sc = malloc(sizeof(*sc), M_NVME, M_ZERO | M_WAITOK);
	sc->s_ctrlr = ctrlr;

	sc->s_sim = cam_sim_alloc(nvme_sim_action, nvme_sim_poll,
	    "nvme", sc, device_get_unit(ctrlr->dev),
	    NULL, max_trans, max_trans, devq);
	if (sc->s_sim == NULL) {
		printf("Failed to allocate a sim\n");
		cam_simq_free(devq);
		goto err1;
	}
	if (xpt_bus_register(sc->s_sim, ctrlr->dev, 0) != CAM_SUCCESS) {
		printf("Failed to create a bus\n");
		goto err2;
	}
	if (xpt_create_path(&sc->s_path, /*periph*/NULL, cam_sim_path(sc->s_sim),
	    CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
		printf("Failed to create a path\n");
		goto err3;
	}

	return (sc);

err3:
	xpt_bus_deregister(cam_sim_path(sc->s_sim));
err2:
	cam_sim_free(sc->s_sim, /*free_devq*/TRUE);
err1:
	free(sc, M_NVME);
	return (NULL);
}

static void *
nvme_sim_new_ns(struct nvme_namespace *ns, void *sc_arg)
{
	struct nvme_sim_softc *sc = sc_arg;
	union ccb *ccb;

	ccb = xpt_alloc_ccb_nowait();
	if (ccb == NULL) {
		printf("unable to alloc CCB for rescan\n");
		return (NULL);
	}

	if (xpt_create_path(&ccb->ccb_h.path, /*periph*/NULL,
	    cam_sim_path(sc->s_sim), 0, ns->id) != CAM_REQ_CMP) {
		printf("unable to create path for rescan\n");
		xpt_free_ccb(ccb);
		return (NULL);
	}

	xpt_rescan(ccb);

	return (ns);
}

static void
nvme_sim_controller_fail(void *ctrlr_arg)
{
	struct nvme_sim_softc *sc = ctrlr_arg;

	xpt_async(AC_LOST_DEVICE, sc->s_path, NULL);
	xpt_free_path(sc->s_path);
	xpt_bus_deregister(cam_sim_path(sc->s_sim));
	cam_sim_free(sc->s_sim, /*free_devq*/TRUE);
	free(sc, M_NVME);
}

struct nvme_consumer *consumer_cookie;

static void
nvme_sim_init(void)
{
	if (nvme_use_nvd)
		return;

	consumer_cookie = nvme_register_consumer(nvme_sim_new_ns,
	    nvme_sim_new_controller, NULL, nvme_sim_controller_fail);
}

SYSINIT(nvme_sim_register, SI_SUB_DRIVERS, SI_ORDER_ANY,
    nvme_sim_init, NULL);

static void
nvme_sim_uninit(void)
{
	if (nvme_use_nvd)
		return;
	/* XXX Cleanup */

	nvme_unregister_consumer(consumer_cookie);
}

SYSUNINIT(nvme_sim_unregister, SI_SUB_DRIVERS, SI_ORDER_ANY,
    nvme_sim_uninit, NULL);
