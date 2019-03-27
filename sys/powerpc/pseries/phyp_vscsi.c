/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2013 Nathan Whitehorn
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
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/selinfo.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/eventhandler.h>
#include <sys/rman.h>
#include <sys/bus_dma.h>
#include <sys/bio.h>
#include <sys/ioccom.h>
#include <sys/uio.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/sysctl.h>
#include <sys/endian.h>
#include <sys/vmem.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_debug.h>
#include <cam/cam_periph.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_periph.h>
#include <cam/cam_xpt_sim.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <powerpc/pseries/phyp-hvcall.h>

struct vscsi_softc;

/* VSCSI CRQ format from table 260 of PAPR spec 2.4 (page 760) */
struct vscsi_crq {
	uint8_t valid;
	uint8_t format;
	uint8_t reserved;
	uint8_t status;
	uint16_t timeout;
	uint16_t iu_length;
	uint64_t iu_data;
};

struct vscsi_xfer {
        TAILQ_ENTRY(vscsi_xfer) queue;
        struct vscsi_softc *sc;
        union ccb *ccb;
        bus_dmamap_t dmamap;
        uint64_t tag;
	
	vmem_addr_t srp_iu_offset;
	vmem_size_t srp_iu_size;
};

TAILQ_HEAD(vscsi_xferq, vscsi_xfer);

struct vscsi_softc {
	device_t	dev;
	struct cam_devq *devq;
	struct cam_sim	*sim;
	struct cam_path	*path;
	struct mtx io_lock;

	cell_t		unit;
	int		bus_initialized;
	int		bus_logged_in;
	int		max_transactions;

	int		irqid;
	struct resource	*irq;
	void		*irq_cookie;

	bus_dma_tag_t	crq_tag;
	struct vscsi_crq *crq_queue;
	int		n_crqs, cur_crq;
	bus_dmamap_t	crq_map;
	bus_addr_t	crq_phys;

	vmem_t		*srp_iu_arena;
	void		*srp_iu_queue;
	bus_addr_t	srp_iu_phys;

	bus_dma_tag_t	data_tag;

	struct vscsi_xfer loginxp;
	struct vscsi_xfer *xfer;
	struct vscsi_xferq active_xferq;
	struct vscsi_xferq free_xferq;
};

struct srp_login {
	uint8_t type;
	uint8_t reserved[7];
	uint64_t tag;
	uint64_t max_cmd_length;
	uint32_t reserved2;
	uint16_t buffer_formats;
	uint8_t flags;
	uint8_t reserved3[5];
	uint8_t initiator_port_id[16];
	uint8_t target_port_id[16];
} __packed;

struct srp_login_rsp {
	uint8_t type;
	uint8_t reserved[3];
	uint32_t request_limit_delta;
	uint8_t tag;
	uint32_t max_i_to_t_len;
	uint32_t max_t_to_i_len;
	uint16_t buffer_formats;
	uint8_t flags;
	/* Some reserved bits follow */
} __packed;

struct srp_cmd {
	uint8_t type;
	uint8_t flags1;
	uint8_t reserved[3];
	uint8_t formats;
	uint8_t out_buffer_count;
	uint8_t in_buffer_count;
	uint64_t tag;
	uint32_t reserved2;
	uint64_t lun;
	uint8_t reserved3[3];
	uint8_t additional_cdb;
	uint8_t cdb[16];
	uint8_t data_payload[0];
} __packed;

struct srp_rsp {
	uint8_t type;
	uint8_t reserved[3];
	uint32_t request_limit_delta;
	uint64_t tag;
	uint16_t reserved2;
	uint8_t flags;
	uint8_t status;
	uint32_t data_out_resid;
	uint32_t data_in_resid;
	uint32_t sense_data_len;
	uint32_t response_data_len;
	uint8_t data_payload[0];
} __packed;

struct srp_tsk_mgmt {
	uint8_t type;
	uint8_t reserved[7];
	uint64_t tag;
	uint32_t reserved2;
	uint64_t lun;
	uint8_t reserved3[2];
	uint8_t function;
	uint8_t reserved4;
	uint64_t manage_tag;
	uint64_t reserved5;
} __packed;

/* Message code type */
#define SRP_LOGIN_REQ	0x00
#define SRP_TSK_MGMT	0x01
#define SRP_CMD		0x02
#define SRP_I_LOGOUT	0x03

#define SRP_LOGIN_RSP	0xC0
#define SRP_RSP		0xC1
#define SRP_LOGIN_REJ	0xC2

#define SRP_T_LOGOUT	0x80
#define SRP_CRED_REQ	0x81
#define SRP_AER_REQ	0x82

#define SRP_CRED_RSP	0x41
#define SRP_AER_RSP	0x41

/* Flags for srp_rsp flags field */
#define SRP_RSPVALID	0x01
#define SRP_SNSVALID	0x02
#define SRP_DOOVER	0x04
#define SRP_DOUNDER	0x08
#define SRP_DIOVER	0x10
#define SRP_DIUNDER	0x20

#define	MAD_SUCESS			0x00
#define	MAD_NOT_SUPPORTED		0xf1
#define	MAD_FAILED			0xf7

#define	MAD_EMPTY_IU			0x01
#define	MAD_ERROR_LOGGING_REQUEST	0x02
#define	MAD_ADAPTER_INFO_REQUEST	0x03
#define	MAD_CAPABILITIES_EXCHANGE	0x05
#define	MAD_PHYS_ADAP_INFO_REQUEST	0x06
#define	MAD_TAPE_PASSTHROUGH_REQUEST	0x07
#define	MAD_ENABLE_FAST_FAIL		0x08

static int	vscsi_probe(device_t);
static int	vscsi_attach(device_t);
static int	vscsi_detach(device_t);
static void	vscsi_cam_action(struct cam_sim *, union ccb *);
static void	vscsi_cam_poll(struct cam_sim *);
static void	vscsi_intr(void *arg);
static void	vscsi_check_response_queue(struct vscsi_softc *sc);
static void	vscsi_setup_bus(struct vscsi_softc *sc);

static void	vscsi_srp_login(struct vscsi_softc *sc);
static void	vscsi_crq_load_cb(void *, bus_dma_segment_t *, int, int);
static void	vscsi_scsi_command(void *xxp, bus_dma_segment_t *segs,
		    int nsegs, int err);
static void	vscsi_task_management(struct vscsi_softc *sc, union ccb *ccb);
static void	vscsi_srp_response(struct vscsi_xfer *, struct vscsi_crq *);

static devclass_t	vscsi_devclass;
static device_method_t	vscsi_methods[] = {
	DEVMETHOD(device_probe,		vscsi_probe),
	DEVMETHOD(device_attach,	vscsi_attach),
	DEVMETHOD(device_detach,	vscsi_detach),

	DEVMETHOD_END
};
static driver_t vscsi_driver = {
	"vscsi",
	vscsi_methods,
	sizeof(struct vscsi_softc)
};
DRIVER_MODULE(vscsi, vdevice, vscsi_driver, vscsi_devclass, 0, 0);
MALLOC_DEFINE(M_VSCSI, "vscsi", "CAM device queue for VSCSI");

static int
vscsi_probe(device_t dev)
{

	if (!ofw_bus_is_compatible(dev, "IBM,v-scsi"))
		return (ENXIO);

	device_set_desc(dev, "POWER Hypervisor Virtual SCSI Bus");
	return (0);
}

static int
vscsi_attach(device_t dev)
{
	struct vscsi_softc *sc;
	struct vscsi_xfer *xp;
	int error, i;

	sc = device_get_softc(dev);
	if (sc == NULL)
		return (EINVAL);

	sc->dev = dev;
	mtx_init(&sc->io_lock, "vscsi", NULL, MTX_DEF);

	/* Get properties */
	OF_getencprop(ofw_bus_get_node(dev), "reg", &sc->unit,
	    sizeof(sc->unit));

	/* Setup interrupt */
	sc->irqid = 0;
	sc->irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->irqid,
	    RF_ACTIVE);

	if (!sc->irq) {
		device_printf(dev, "Could not allocate IRQ\n");
		mtx_destroy(&sc->io_lock);
		return (ENXIO);
	}

	bus_setup_intr(dev, sc->irq, INTR_TYPE_CAM | INTR_MPSAFE |
	    INTR_ENTROPY, NULL, vscsi_intr, sc, &sc->irq_cookie);

	/* Data DMA */
	error = bus_dma_tag_create(bus_get_dma_tag(dev), 1, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL, BUS_SPACE_MAXSIZE,
	    256, BUS_SPACE_MAXSIZE_32BIT, 0, busdma_lock_mutex, &sc->io_lock,
	    &sc->data_tag);

	TAILQ_INIT(&sc->active_xferq);
	TAILQ_INIT(&sc->free_xferq);

	/* First XFER for login data */
	sc->loginxp.sc = sc;
	bus_dmamap_create(sc->data_tag, 0, &sc->loginxp.dmamap);
	TAILQ_INSERT_TAIL(&sc->free_xferq, &sc->loginxp, queue);
	 
	/* CRQ area */
	error = bus_dma_tag_create(bus_get_dma_tag(dev), PAGE_SIZE, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL, 8*PAGE_SIZE,
	    1, BUS_SPACE_MAXSIZE, 0, NULL, NULL, &sc->crq_tag);
	error = bus_dmamem_alloc(sc->crq_tag, (void **)&sc->crq_queue,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO, &sc->crq_map);
	sc->crq_phys = 0;
	sc->n_crqs = 0;
	error = bus_dmamap_load(sc->crq_tag, sc->crq_map, sc->crq_queue,
	    8*PAGE_SIZE, vscsi_crq_load_cb, sc, 0);

	mtx_lock(&sc->io_lock);
	vscsi_setup_bus(sc);
	sc->xfer = malloc(sizeof(sc->xfer[0])*sc->max_transactions, M_VSCSI,
	    M_NOWAIT);
	for (i = 0; i < sc->max_transactions; i++) {
		xp = &sc->xfer[i];
		xp->sc = sc;

		error = bus_dmamap_create(sc->data_tag, 0, &xp->dmamap);
		if (error) {
			device_printf(dev, "Could not create DMA map (%d)\n",
			    error);
			break;
		}

		TAILQ_INSERT_TAIL(&sc->free_xferq, xp, queue);
	}
	mtx_unlock(&sc->io_lock);

	/* Allocate CAM bits */
	if ((sc->devq = cam_simq_alloc(sc->max_transactions)) == NULL)
		return (ENOMEM);

	sc->sim = cam_sim_alloc(vscsi_cam_action, vscsi_cam_poll, "vscsi", sc,
				device_get_unit(dev), &sc->io_lock,
				sc->max_transactions, sc->max_transactions,
				sc->devq);
	if (sc->sim == NULL) {
		cam_simq_free(sc->devq);
		sc->devq = NULL;
		device_printf(dev, "CAM SIM attach failed\n");
		return (EINVAL);
	}


	mtx_lock(&sc->io_lock);
	if (xpt_bus_register(sc->sim, dev, 0) != 0) {
		device_printf(dev, "XPT bus registration failed\n");
		cam_sim_free(sc->sim, FALSE);
		sc->sim = NULL;
		cam_simq_free(sc->devq);
		sc->devq = NULL;
		mtx_unlock(&sc->io_lock);
		return (EINVAL);
	}
	mtx_unlock(&sc->io_lock);

	return (0);
}

static int
vscsi_detach(device_t dev)
{
	struct vscsi_softc *sc;

	sc = device_get_softc(dev);
	if (sc == NULL)
		return (EINVAL);

	if (sc->sim != NULL) {
		mtx_lock(&sc->io_lock);
		xpt_bus_deregister(cam_sim_path(sc->sim));
		cam_sim_free(sc->sim, FALSE);
		sc->sim = NULL;
		mtx_unlock(&sc->io_lock);
	}

	if (sc->devq != NULL) {
		cam_simq_free(sc->devq);
		sc->devq = NULL;
	}
	
	mtx_destroy(&sc->io_lock);

	return (0);
}

static void
vscsi_cam_action(struct cam_sim *sim, union ccb *ccb)
{
	struct vscsi_softc *sc = cam_sim_softc(sim);

	mtx_assert(&sc->io_lock, MA_OWNED);

	switch (ccb->ccb_h.func_code) {
	case XPT_PATH_INQ:
	{
		struct ccb_pathinq *cpi = &ccb->cpi;

		cpi->version_num = 1;
		cpi->hba_inquiry = PI_TAG_ABLE;
		cpi->hba_misc = PIM_EXTLUNS;
		cpi->target_sprt = 0;
		cpi->hba_eng_cnt = 0;
		cpi->max_target = 0;
		cpi->max_lun = 0;
		cpi->initiator_id = ~0;
		strlcpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
		strlcpy(cpi->hba_vid, "IBM", HBA_IDLEN);
		strlcpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
		cpi->unit_number = cam_sim_unit(sim);
		cpi->bus_id = cam_sim_bus(sim);
		cpi->base_transfer_speed = 150000;
		cpi->transport = XPORT_SRP;
		cpi->transport_version = 0;
		cpi->protocol = PROTO_SCSI;
		cpi->protocol_version = SCSI_REV_SPC4;
		cpi->ccb_h.status = CAM_REQ_CMP;
		break;
	}
	case XPT_RESET_BUS:
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	case XPT_RESET_DEV:
		ccb->ccb_h.status = CAM_REQ_INPROG;
		vscsi_task_management(sc, ccb);
		return;
	case XPT_GET_TRAN_SETTINGS:
		ccb->cts.protocol = PROTO_SCSI;
		ccb->cts.protocol_version = SCSI_REV_SPC4;
		ccb->cts.transport = XPORT_SRP;
		ccb->cts.transport_version = 0;
		ccb->cts.proto_specific.valid = 0;
		ccb->cts.xport_specific.valid = 0;
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	case XPT_SET_TRAN_SETTINGS:
		ccb->ccb_h.status = CAM_FUNC_NOTAVAIL;
		break;
	case XPT_SCSI_IO:
	{
		struct vscsi_xfer *xp;

		ccb->ccb_h.status = CAM_REQ_INPROG;

		xp = TAILQ_FIRST(&sc->free_xferq);
		if (xp == NULL)
			panic("SCSI queue flooded");
		xp->ccb = ccb;
		TAILQ_REMOVE(&sc->free_xferq, xp, queue);
		TAILQ_INSERT_TAIL(&sc->active_xferq, xp, queue);
		bus_dmamap_load_ccb(sc->data_tag, xp->dmamap,
		    ccb, vscsi_scsi_command, xp, 0);

		return;
	}
	default:
		ccb->ccb_h.status = CAM_REQ_INVALID;
		break;
	}

	xpt_done(ccb);
	return;
}

static void
vscsi_srp_login(struct vscsi_softc *sc)
{
	struct vscsi_xfer *xp;
	struct srp_login *login;
	struct vscsi_crq crq;
	int err;

	mtx_assert(&sc->io_lock, MA_OWNED);

	xp = TAILQ_FIRST(&sc->free_xferq);
	if (xp == NULL)
		panic("SCSI queue flooded");
	xp->ccb = NULL;
	TAILQ_REMOVE(&sc->free_xferq, xp, queue);
	TAILQ_INSERT_TAIL(&sc->active_xferq, xp, queue);
	
	/* Set up command */
	xp->srp_iu_size = crq.iu_length = 64;
	err = vmem_alloc(xp->sc->srp_iu_arena, xp->srp_iu_size,
	    M_BESTFIT | M_NOWAIT, &xp->srp_iu_offset);
	if (err)
		panic("Error during VMEM allocation (%d)", err);

	login = (struct srp_login *)((uint8_t *)xp->sc->srp_iu_queue +
	    (uintptr_t)xp->srp_iu_offset);
	bzero(login, xp->srp_iu_size);
	login->type = SRP_LOGIN_REQ;
	login->tag = (uint64_t)(xp);
	login->max_cmd_length = htobe64(256);
	login->buffer_formats = htobe16(0x1 | 0x2); /* Direct and indirect */
	login->flags = 0;

	/* Create CRQ entry */
	crq.valid = 0x80;
	crq.format = 0x01;
	crq.iu_data = xp->sc->srp_iu_phys + xp->srp_iu_offset;
	bus_dmamap_sync(sc->crq_tag, sc->crq_map, BUS_DMASYNC_PREWRITE);

	err = phyp_hcall(H_SEND_CRQ, xp->sc->unit, ((uint64_t *)(&crq))[0],
	    ((uint64_t *)(&crq))[1]);
	if (err != 0)
		panic("CRQ send failure (%d)", err);
}

static void
vscsi_task_management(struct vscsi_softc *sc, union ccb *ccb)
{
	struct srp_tsk_mgmt *cmd;
	struct vscsi_xfer *xp;
	struct vscsi_crq crq;
	int err;

	mtx_assert(&sc->io_lock, MA_OWNED);

	xp = TAILQ_FIRST(&sc->free_xferq);
	if (xp == NULL)
		panic("SCSI queue flooded");
	xp->ccb = ccb;
	TAILQ_REMOVE(&sc->free_xferq, xp, queue);
	TAILQ_INSERT_TAIL(&sc->active_xferq, xp, queue);

	xp->srp_iu_size = crq.iu_length = sizeof(*cmd);
	err = vmem_alloc(xp->sc->srp_iu_arena, xp->srp_iu_size,
	    M_BESTFIT | M_NOWAIT, &xp->srp_iu_offset);
	if (err)
		panic("Error during VMEM allocation (%d)", err);

	cmd = (struct srp_tsk_mgmt *)((uint8_t *)xp->sc->srp_iu_queue +
	    (uintptr_t)xp->srp_iu_offset);
	bzero(cmd, xp->srp_iu_size);
	cmd->type = SRP_TSK_MGMT;
	cmd->tag = (uint64_t)xp;
	cmd->lun = htobe64(CAM_EXTLUN_BYTE_SWIZZLE(ccb->ccb_h.target_lun));

	switch (ccb->ccb_h.func_code) {
	case XPT_RESET_DEV:
		cmd->function = 0x08;
		break;
	default:
		panic("Unimplemented code %d", ccb->ccb_h.func_code);
		break;
	}

	bus_dmamap_sync(xp->sc->crq_tag, xp->sc->crq_map, BUS_DMASYNC_PREWRITE);

	/* Create CRQ entry */
	crq.valid = 0x80;
	crq.format = 0x01;
	crq.iu_data = xp->sc->srp_iu_phys + xp->srp_iu_offset;

	err = phyp_hcall(H_SEND_CRQ, xp->sc->unit, ((uint64_t *)(&crq))[0],
	    ((uint64_t *)(&crq))[1]);
	if (err != 0)
		panic("CRQ send failure (%d)", err);
}

static void
vscsi_scsi_command(void *xxp, bus_dma_segment_t *segs, int nsegs, int err)
{
	struct vscsi_xfer *xp = xxp;
	uint8_t *cdb;
	union ccb *ccb = xp->ccb;
	struct srp_cmd *cmd;
	uint64_t chunk_addr;
	uint32_t chunk_size;
	int desc_start, i;
	struct vscsi_crq crq;

	KASSERT(err == 0, ("DMA error %d\n", err));

	mtx_assert(&xp->sc->io_lock, MA_OWNED);

	cdb = (ccb->ccb_h.flags & CAM_CDB_POINTER) ?
	    ccb->csio.cdb_io.cdb_ptr : ccb->csio.cdb_io.cdb_bytes;

	/* Command format from Table 20, page 37 of SRP spec */
	crq.iu_length = 48 + ((nsegs > 1) ? 20 : 16) + 
	    ((ccb->csio.cdb_len > 16) ? (ccb->csio.cdb_len - 16) : 0);
	xp->srp_iu_size = crq.iu_length;
	if (nsegs > 1)
		xp->srp_iu_size += nsegs*16;
	xp->srp_iu_size = roundup(xp->srp_iu_size, 16);
	err = vmem_alloc(xp->sc->srp_iu_arena, xp->srp_iu_size,
	    M_BESTFIT | M_NOWAIT, &xp->srp_iu_offset);
	if (err)
		panic("Error during VMEM allocation (%d)", err);

	cmd = (struct srp_cmd *)((uint8_t *)xp->sc->srp_iu_queue +
	    (uintptr_t)xp->srp_iu_offset);
	bzero(cmd, xp->srp_iu_size);
	cmd->type = SRP_CMD;
	if (ccb->csio.cdb_len > 16)
		cmd->additional_cdb = (ccb->csio.cdb_len - 16) << 2;
	memcpy(cmd->cdb, cdb, ccb->csio.cdb_len);

	cmd->tag = (uint64_t)(xp); /* Let the responder find this again */
	cmd->lun = htobe64(CAM_EXTLUN_BYTE_SWIZZLE(ccb->ccb_h.target_lun));

	if (nsegs > 1) {
		/* Use indirect descriptors */
		switch (ccb->ccb_h.flags & CAM_DIR_MASK) {
		case CAM_DIR_OUT:
			cmd->formats = (2 << 4);
			break;
		case CAM_DIR_IN:
			cmd->formats = 2;
			break;
		default:
			panic("Does not support bidirectional commands (%d)",
			    ccb->ccb_h.flags & CAM_DIR_MASK);
			break;
		}

		desc_start = ((ccb->csio.cdb_len > 16) ?
		    ccb->csio.cdb_len - 16 : 0);
		chunk_addr = xp->sc->srp_iu_phys + xp->srp_iu_offset + 20 +
		    desc_start + sizeof(*cmd);
		chunk_size = 16*nsegs;
		memcpy(&cmd->data_payload[desc_start], &chunk_addr, 8);
		memcpy(&cmd->data_payload[desc_start+12], &chunk_size, 4);
		chunk_size = 0;
		for (i = 0; i < nsegs; i++)
			chunk_size += segs[i].ds_len;
		memcpy(&cmd->data_payload[desc_start+16], &chunk_size, 4);
		desc_start += 20;
		for (i = 0; i < nsegs; i++) {
			chunk_addr = segs[i].ds_addr;
			chunk_size = segs[i].ds_len;

			memcpy(&cmd->data_payload[desc_start + 16*i],
			    &chunk_addr, 8);
			/* Set handle tag to 0 */
			memcpy(&cmd->data_payload[desc_start + 16*i + 12],
			    &chunk_size, 4);
		}
	} else if (nsegs == 1) {
		switch (ccb->ccb_h.flags & CAM_DIR_MASK) {
		case CAM_DIR_OUT:
			cmd->formats = (1 << 4);
			break;
		case CAM_DIR_IN:
			cmd->formats = 1;
			break;
		default:
			panic("Does not support bidirectional commands (%d)",
			    ccb->ccb_h.flags & CAM_DIR_MASK);
			break;
		}

		/*
		 * Memory descriptor:
		 * 8 byte address
		 * 4 byte handle
		 * 4 byte length
		 */

		chunk_addr = segs[0].ds_addr;
		chunk_size = segs[0].ds_len;
		desc_start = ((ccb->csio.cdb_len > 16) ?
		    ccb->csio.cdb_len - 16 : 0);

		memcpy(&cmd->data_payload[desc_start], &chunk_addr, 8);
		/* Set handle tag to 0 */
		memcpy(&cmd->data_payload[desc_start+12], &chunk_size, 4);
		KASSERT(xp->srp_iu_size >= 48 + ((ccb->csio.cdb_len > 16) ?
		    ccb->csio.cdb_len : 16), ("SRP IU command length"));
	} else {
		cmd->formats = 0;
	}
	bus_dmamap_sync(xp->sc->crq_tag, xp->sc->crq_map, BUS_DMASYNC_PREWRITE);

	/* Create CRQ entry */
	crq.valid = 0x80;
	crq.format = 0x01;
	crq.iu_data = xp->sc->srp_iu_phys + xp->srp_iu_offset;

	err = phyp_hcall(H_SEND_CRQ, xp->sc->unit, ((uint64_t *)(&crq))[0],
	    ((uint64_t *)(&crq))[1]);
	if (err != 0)
		panic("CRQ send failure (%d)", err);
}

static void
vscsi_crq_load_cb(void *xsc, bus_dma_segment_t *segs, int nsegs, int err)
{
	struct vscsi_softc *sc = xsc;
	
	sc->crq_phys = segs[0].ds_addr;
	sc->n_crqs = PAGE_SIZE/sizeof(struct vscsi_crq);

	sc->srp_iu_queue = (uint8_t *)(sc->crq_queue);
	sc->srp_iu_phys = segs[0].ds_addr;
	sc->srp_iu_arena = vmem_create("VSCSI SRP IU", PAGE_SIZE,
	    segs[0].ds_len - PAGE_SIZE, 16, 0, M_BESTFIT | M_NOWAIT);
}

static void
vscsi_setup_bus(struct vscsi_softc *sc)
{
	struct vscsi_crq crq;
	struct vscsi_xfer *xp;
	int error;

	struct {
		uint32_t type;
		uint16_t status;
		uint16_t length;
		uint64_t tag;
		uint64_t buffer;
		struct {
			char srp_version[8];
			char partition_name[96];
			uint32_t partition_number;
			uint32_t mad_version;
			uint32_t os_type;
			uint32_t port_max_txu[8];
		} payload;
	} mad_adapter_info;

	bzero(&crq, sizeof(crq));

	/* Init message */
	crq.valid = 0xc0;
	crq.format = 0x01;

	do {
		error = phyp_hcall(H_FREE_CRQ, sc->unit);
	} while (error == H_BUSY);

	/* See initialization sequence page 757 */
	bzero(sc->crq_queue, sc->n_crqs*sizeof(sc->crq_queue[0]));
	sc->cur_crq = 0;
	sc->bus_initialized = 0;
	sc->bus_logged_in = 0;
	bus_dmamap_sync(sc->crq_tag, sc->crq_map, BUS_DMASYNC_PREWRITE);
	error = phyp_hcall(H_REG_CRQ, sc->unit, sc->crq_phys,
	    sc->n_crqs*sizeof(sc->crq_queue[0]));
	KASSERT(error == 0, ("CRQ registration success"));

	error = phyp_hcall(H_SEND_CRQ, sc->unit, ((uint64_t *)(&crq))[0],
	    ((uint64_t *)(&crq))[1]);
	if (error != 0)
		panic("CRQ setup failure (%d)", error);

	while (sc->bus_initialized == 0)
		vscsi_check_response_queue(sc);

	/* Send MAD adapter info */
	mad_adapter_info.type = MAD_ADAPTER_INFO_REQUEST;
	mad_adapter_info.status = 0;
	mad_adapter_info.length = sizeof(mad_adapter_info.payload);

	strcpy(mad_adapter_info.payload.srp_version, "16.a");
	strcpy(mad_adapter_info.payload.partition_name, "UNKNOWN");
	mad_adapter_info.payload.partition_number = -1;
	mad_adapter_info.payload.mad_version = 1;
	mad_adapter_info.payload.os_type = 2; /* Claim we are Linux */
	mad_adapter_info.payload.port_max_txu[0] = 0;
	/* If this fails, we get the defaults above */
	OF_getprop(OF_finddevice("/"), "ibm,partition-name",
	    mad_adapter_info.payload.partition_name,
	    sizeof(mad_adapter_info.payload.partition_name));
	OF_getprop(OF_finddevice("/"), "ibm,partition-no",
	    &mad_adapter_info.payload.partition_number,
	    sizeof(mad_adapter_info.payload.partition_number));

	xp = TAILQ_FIRST(&sc->free_xferq);
	xp->ccb = NULL;
	TAILQ_REMOVE(&sc->free_xferq, xp, queue);
	TAILQ_INSERT_TAIL(&sc->active_xferq, xp, queue);
	xp->srp_iu_size = crq.iu_length = sizeof(mad_adapter_info);
	vmem_alloc(xp->sc->srp_iu_arena, xp->srp_iu_size,
	    M_BESTFIT | M_NOWAIT, &xp->srp_iu_offset);
	mad_adapter_info.buffer = xp->sc->srp_iu_phys + xp->srp_iu_offset + 24;
	mad_adapter_info.tag = (uint64_t)xp;
	memcpy((uint8_t *)xp->sc->srp_iu_queue + (uintptr_t)xp->srp_iu_offset,
		&mad_adapter_info, sizeof(mad_adapter_info));
	crq.valid = 0x80;
	crq.format = 0x02;
	crq.iu_data = xp->sc->srp_iu_phys + xp->srp_iu_offset;
	bus_dmamap_sync(sc->crq_tag, sc->crq_map, BUS_DMASYNC_PREWRITE);
	phyp_hcall(H_SEND_CRQ, xp->sc->unit, ((uint64_t *)(&crq))[0],
	    ((uint64_t *)(&crq))[1]);

	while (TAILQ_EMPTY(&sc->free_xferq))
		vscsi_check_response_queue(sc);

	/* Send SRP login */
	vscsi_srp_login(sc);
	while (sc->bus_logged_in == 0)
		vscsi_check_response_queue(sc);

	error = phyp_hcall(H_VIO_SIGNAL, sc->unit, 1); /* Enable interrupts */
}
	

static void
vscsi_intr(void *xsc)
{
	struct vscsi_softc *sc = xsc;

	mtx_lock(&sc->io_lock);
	vscsi_check_response_queue(sc);
	mtx_unlock(&sc->io_lock);
}

static void
vscsi_srp_response(struct vscsi_xfer *xp, struct vscsi_crq *crq)
{
	union ccb *ccb = xp->ccb;
	struct vscsi_softc *sc = xp->sc;
	struct srp_rsp *rsp;
	uint32_t sense_len;

	/* SRP response packet in original request */
	rsp = (struct srp_rsp *)((uint8_t *)sc->srp_iu_queue +
	    (uintptr_t)xp->srp_iu_offset);
	ccb->csio.scsi_status = rsp->status;
	if (ccb->csio.scsi_status == SCSI_STATUS_OK)
		ccb->ccb_h.status = CAM_REQ_CMP;
	else
		ccb->ccb_h.status = CAM_SCSI_STATUS_ERROR;
#ifdef NOTYET
	/* Collect fast fail codes */
	if (crq->status != 0)
		ccb->ccb_h.status = CAM_REQ_CMP_ERR;
#endif

	if (ccb->ccb_h.status != CAM_REQ_CMP) {
		ccb->ccb_h.status |= CAM_DEV_QFRZN;
		xpt_freeze_devq(ccb->ccb_h.path, /*count*/ 1);
	}

	if (!(rsp->flags & SRP_RSPVALID))
		rsp->response_data_len = 0;
	if (!(rsp->flags & SRP_SNSVALID))
		rsp->sense_data_len = 0;
	if (!(rsp->flags & (SRP_DOOVER | SRP_DOUNDER)))
		rsp->data_out_resid = 0;
	if (!(rsp->flags & (SRP_DIOVER | SRP_DIUNDER)))
		rsp->data_in_resid = 0;

	if (rsp->flags & SRP_SNSVALID) {
		bzero(&ccb->csio.sense_data, sizeof(struct scsi_sense_data));
		ccb->ccb_h.status |= CAM_AUTOSNS_VALID;
		sense_len = min(be32toh(rsp->sense_data_len),
		    ccb->csio.sense_len);
		memcpy(&ccb->csio.sense_data,
		    &rsp->data_payload[be32toh(rsp->response_data_len)],
		    sense_len);
		ccb->csio.sense_resid = ccb->csio.sense_len -
		    be32toh(rsp->sense_data_len);
	}

	switch (ccb->ccb_h.flags & CAM_DIR_MASK) {
	case CAM_DIR_OUT:
		ccb->csio.resid = rsp->data_out_resid;
		break;
	case CAM_DIR_IN:
		ccb->csio.resid = rsp->data_in_resid;
		break;
	}

	bus_dmamap_sync(sc->data_tag, xp->dmamap, BUS_DMASYNC_POSTREAD);
	bus_dmamap_unload(sc->data_tag, xp->dmamap);
	xpt_done(ccb);
	xp->ccb = NULL;
}

static void
vscsi_login_response(struct vscsi_xfer *xp, struct vscsi_crq *crq)
{
	struct vscsi_softc *sc = xp->sc;
	struct srp_login_rsp *rsp;

	/* SRP response packet in original request */
	rsp = (struct srp_login_rsp *)((uint8_t *)sc->srp_iu_queue +
	    (uintptr_t)xp->srp_iu_offset);
	KASSERT(be16toh(rsp->buffer_formats) & 0x3, ("Both direct and indirect "
	    "buffers supported"));

	sc->max_transactions = be32toh(rsp->request_limit_delta);
	device_printf(sc->dev, "Queue depth %d commands\n",
	    sc->max_transactions);
	sc->bus_logged_in = 1;
}

static void
vscsi_cam_poll(struct cam_sim *sim)
{
	struct vscsi_softc *sc = cam_sim_softc(sim);

	vscsi_check_response_queue(sc);
}

static void
vscsi_check_response_queue(struct vscsi_softc *sc)
{
	struct vscsi_crq *crq;
	struct vscsi_xfer *xp;
	int code;

	mtx_assert(&sc->io_lock, MA_OWNED);

	while (sc->crq_queue[sc->cur_crq].valid != 0) {
		/* The hypercalls at both ends of this are not optimal */
		phyp_hcall(H_VIO_SIGNAL, sc->unit, 0);
		bus_dmamap_sync(sc->crq_tag, sc->crq_map, BUS_DMASYNC_POSTREAD);

		crq = &sc->crq_queue[sc->cur_crq];

		switch (crq->valid) {
		case 0xc0:
			if (crq->format == 0x02)
				sc->bus_initialized = 1;
			break;
		case 0x80:
			/* IU data is set to tag pointer (the XP) */
			xp = (struct vscsi_xfer *)crq->iu_data;

			switch (crq->format) {
			case 0x01:
				code = *((uint8_t *)sc->srp_iu_queue +
	    			    (uintptr_t)xp->srp_iu_offset);
				switch (code) {
				case SRP_RSP:
					vscsi_srp_response(xp, crq);
					break;
				case SRP_LOGIN_RSP:
					vscsi_login_response(xp, crq);
					break;
				default:
					device_printf(sc->dev, "Unknown SRP "
					    "response code %d\n", code);
					break;
				}
				break;
			case 0x02:
				/* Ignore management datagrams */
				break;
			default:
				panic("Unknown CRQ format %d\n", crq->format);
				break;
			}
			vmem_free(sc->srp_iu_arena, xp->srp_iu_offset,
			    xp->srp_iu_size);
			TAILQ_REMOVE(&sc->active_xferq, xp, queue);
			TAILQ_INSERT_TAIL(&sc->free_xferq, xp, queue);
			break;
		default:
			device_printf(sc->dev,
			    "Unknown CRQ message type %d\n", crq->valid);
			break;
		}

		crq->valid = 0;
		sc->cur_crq = (sc->cur_crq + 1) % sc->n_crqs;

		bus_dmamap_sync(sc->crq_tag, sc->crq_map, BUS_DMASYNC_PREWRITE);
		phyp_hcall(H_VIO_SIGNAL, sc->unit, 1);
	}
}

