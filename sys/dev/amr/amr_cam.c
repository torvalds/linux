/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Michael Smith
 * Copyright (c) 2000 BSDi
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
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
 * Copyright (c) 2002 Eric Moore
 * Copyright (c) 2002 LSI Logic Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
 * 3. The party using or redistributing the source code and binary forms
 *	agrees to the disclaimer below and the terms and conditions set forth
 *	herein.
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
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <sys/bio.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/stat.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt.h>
#include <cam/cam_xpt_sim.h>
#include <cam/cam_debug.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>

#include <machine/resource.h>
#include <machine/bus.h>

#include <dev/amr/amrreg.h>
#include <dev/amr/amrvar.h>

static int	amr_cam_probe(device_t dev);
static int	amr_cam_attach(device_t dev);
static int	amr_cam_detach(device_t dev);
static void	amr_cam_action(struct cam_sim *sim, union ccb *ccb);
static void	amr_cam_poll(struct cam_sim *sim);
static void	amr_cam_complete(struct amr_command *ac);
static int	amr_cam_command(struct amr_softc *sc, struct amr_command **acp);

static devclass_t	amr_pass_devclass;

static device_method_t	amr_pass_methods[] = {
	DEVMETHOD(device_probe,		amr_cam_probe),
	DEVMETHOD(device_attach,	amr_cam_attach),
	DEVMETHOD(device_detach,	amr_cam_detach),
	{ 0, 0 }
};

static driver_t	amr_pass_driver = {
	"amrp",
	amr_pass_methods,
	0
};

DRIVER_MODULE(amrp, amr, amr_pass_driver, amr_pass_devclass, 0, 0);
MODULE_DEPEND(amrp, cam, 1, 1, 1);

static MALLOC_DEFINE(M_AMRCAM, "amrcam", "AMR CAM memory");

/***********************************************************************
 * Enqueue/dequeue functions
 */
static __inline void
amr_enqueue_ccb(struct amr_softc *sc, union ccb *ccb)
{

	TAILQ_INSERT_TAIL(&sc->amr_cam_ccbq, &ccb->ccb_h, sim_links.tqe);
}

static __inline void
amr_requeue_ccb(struct amr_softc *sc, union ccb *ccb)
{

	TAILQ_INSERT_HEAD(&sc->amr_cam_ccbq, &ccb->ccb_h, sim_links.tqe);
}

static __inline union ccb *
amr_dequeue_ccb(struct amr_softc *sc)
{
	union ccb	*ccb;

	if ((ccb = (union ccb *)TAILQ_FIRST(&sc->amr_cam_ccbq)) != NULL)
		TAILQ_REMOVE(&sc->amr_cam_ccbq, &ccb->ccb_h, sim_links.tqe);
	return(ccb);
}

static int
amr_cam_probe(device_t dev)
{
	return (0);
}

/********************************************************************************
 * Attach our 'real' SCSI channels to CAM
 */
static int
amr_cam_attach(device_t dev)
{
	struct amr_softc *sc;
	struct cam_devq	*devq;
	int chn, error;

	sc = device_get_softc(dev);

	/* initialise the ccb queue */
	TAILQ_INIT(&sc->amr_cam_ccbq);

	/*
	 * Allocate a devq for all our channels combined.  This should
	 * allow for the maximum number of SCSI commands we will accept
	 * at one time. Save the pointer in the softc so we can find it later
	 * during detach.
	 */
	if ((devq = cam_simq_alloc(AMR_MAX_SCSI_CMDS)) == NULL)
		return(ENOMEM);
	sc->amr_cam_devq = devq;

	/*
	 * Iterate over our channels, registering them with CAM
	 */
	for (chn = 0; chn < sc->amr_maxchan; chn++) {

		/* allocate a sim */
		if ((sc->amr_cam_sim[chn] = cam_sim_alloc(amr_cam_action,
		    amr_cam_poll, "amr", sc, device_get_unit(sc->amr_dev),
		    &sc->amr_list_lock, 1, AMR_MAX_SCSI_CMDS, devq)) == NULL) {
			cam_simq_free(devq);
			device_printf(sc->amr_dev, "CAM SIM attach failed\n");
			return(ENOMEM);
		}

		/* register the bus ID so we can get it later */
		mtx_lock(&sc->amr_list_lock);
		error = xpt_bus_register(sc->amr_cam_sim[chn], sc->amr_dev,chn);
		mtx_unlock(&sc->amr_list_lock);
		if (error) {
			device_printf(sc->amr_dev,
			    "CAM XPT bus registration failed\n");
			return(ENXIO);
		}
	}
	/*
	 * XXX we should scan the config and work out which devices are
	 * actually protected.
	 */
	sc->amr_cam_command = amr_cam_command;
	return(0);
}

/********************************************************************************
 * Disconnect ourselves from CAM
 */
static int
amr_cam_detach(device_t dev)
{
	struct amr_softc *sc;
	int		chn;

	sc = device_get_softc(dev);
	mtx_lock(&sc->amr_list_lock);
	for (chn = 0; chn < sc->amr_maxchan; chn++) {
		/*
		 * If a sim was allocated for this channel, free it
		 */
		if (sc->amr_cam_sim[chn] != NULL) {
			xpt_bus_deregister(cam_sim_path(sc->amr_cam_sim[chn]));
			cam_sim_free(sc->amr_cam_sim[chn], FALSE);
		}
	}
	mtx_unlock(&sc->amr_list_lock);

	/* Now free the devq */
	if (sc->amr_cam_devq != NULL)
		cam_simq_free(sc->amr_cam_devq);

	return (0);
}

/***********************************************************************
 ***********************************************************************
			CAM passthrough interface
 ***********************************************************************
 ***********************************************************************/

/***********************************************************************
 * Handle a request for action from CAM
 */
static void
amr_cam_action(struct cam_sim *sim, union ccb *ccb)
{
	struct amr_softc	*sc = cam_sim_softc(sim);

	switch(ccb->ccb_h.func_code) {

	/*
	 * Perform SCSI I/O to a physical device.
	 */
	case XPT_SCSI_IO:
	{
		struct ccb_hdr		*ccbh = &ccb->ccb_h;
		struct ccb_scsiio	*csio = &ccb->csio;

		/* Validate the CCB */
		ccbh->status = CAM_REQ_INPROG;

		/* check the CDB length */
		if (csio->cdb_len > AMR_MAX_EXTCDB_LEN)
			ccbh->status = CAM_REQ_INVALID;

		if ((csio->cdb_len > AMR_MAX_CDB_LEN) &&
		    (sc->support_ext_cdb == 0))
			ccbh->status = CAM_REQ_INVALID;
	
		/* check that the CDB pointer is not to a physical address */
		if ((ccbh->flags & CAM_CDB_POINTER) &&
		    (ccbh->flags & CAM_CDB_PHYS))
			ccbh->status = CAM_REQ_INVALID;
		/*
		 * if there is data transfer, it must be to/from a virtual
		 * address
		 */
		if ((ccbh->flags & CAM_DIR_MASK) != CAM_DIR_NONE) {
			if ((ccbh->flags & CAM_DATA_MASK) != CAM_DATA_VADDR)
				/* we can't map it */
				ccbh->status = CAM_REQ_INVALID;
		}
	
		/*
		 * If the command is to a LUN other than 0, fail it.
		 * This is probably incorrect, but during testing the
		 * firmware did not seem to respect the LUN field, and thus
		 * devices appear echoed.
		 */
		if (csio->ccb_h.target_lun != 0)
			ccbh->status = CAM_DEV_NOT_THERE;

		/* if we're happy with the request, queue it for attention */
		if (ccbh->status == CAM_REQ_INPROG) {

			/* save the channel number in the ccb */
			csio->ccb_h.sim_priv.entries[0].field= cam_sim_bus(sim);

			amr_enqueue_ccb(sc, ccb);
			amr_startio(sc);
			return;
		}
		break;
	}

	case XPT_CALC_GEOMETRY:
	{
		cam_calc_geometry(&ccb->ccg, /*extended*/1);
		break;
	}

	/*
	 * Return path stats.  Some of these should probably be amended.
	 */
	case XPT_PATH_INQ:
	{
		struct ccb_pathinq	  *cpi = & ccb->cpi;

		debug(3, "XPT_PATH_INQ");
		cpi->version_num = 1;		   /* XXX??? */
		cpi->hba_inquiry = PI_SDTR_ABLE|PI_TAG_ABLE|PI_WIDE_16;
		cpi->target_sprt = 0;
		cpi->hba_misc = PIM_NOBUSRESET|PIM_SEQSCAN;
		cpi->hba_eng_cnt = 0;
		cpi->max_target = AMR_MAX_TARGETS;
		cpi->max_lun = 0 /* AMR_MAX_LUNS*/;
		cpi->initiator_id = 7;		  /* XXX variable? */
		strlcpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
		strlcpy(cpi->hba_vid, "LSI", HBA_IDLEN);
		strlcpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
		cpi->unit_number = cam_sim_unit(sim);
		cpi->bus_id = cam_sim_bus(sim);
		cpi->base_transfer_speed = 132 * 1024;  /* XXX */
		cpi->transport = XPORT_SPI;
		cpi->transport_version = 2;
		cpi->protocol = PROTO_SCSI;
		cpi->protocol_version = SCSI_REV_2;
		cpi->ccb_h.status = CAM_REQ_CMP;

		break;
	}

	case XPT_RESET_BUS:
	{
		struct ccb_pathinq	*cpi = & ccb->cpi;

		debug(1, "XPT_RESET_BUS");
		cpi->ccb_h.status = CAM_REQ_CMP;
		break;
	}

	case XPT_RESET_DEV:
	{
		debug(1, "XPT_RESET_DEV");
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	}

	case XPT_GET_TRAN_SETTINGS:
	{
		struct ccb_trans_settings	*cts = &(ccb->cts);

		debug(3, "XPT_GET_TRAN_SETTINGS");

		struct ccb_trans_settings_scsi *scsi;
		struct ccb_trans_settings_spi *spi;

		scsi = &cts->proto_specific.scsi;
		spi = &cts->xport_specific.spi;

		cts->protocol = PROTO_SCSI;
		cts->protocol_version = SCSI_REV_2;
		cts->transport = XPORT_SPI;
		cts->transport_version = 2;

		if (cts->type == CTS_TYPE_USER_SETTINGS) {
			ccb->ccb_h.status = CAM_FUNC_NOTAVAIL;
			break;
		}

		spi->flags = CTS_SPI_FLAGS_DISC_ENB;
		spi->bus_width = MSG_EXT_WDTR_BUS_32_BIT;
		spi->sync_period = 6;   /* 40MHz how wide is this bus? */
		spi->sync_offset = 31;  /* How to extract this from board? */

		spi->valid = CTS_SPI_VALID_SYNC_RATE
			| CTS_SPI_VALID_SYNC_OFFSET
			| CTS_SPI_VALID_BUS_WIDTH
			| CTS_SPI_VALID_DISC;
		scsi->valid = CTS_SCSI_VALID_TQ;
		ccb->ccb_h.status = CAM_REQ_CMP;
		break;
	}

	case XPT_SET_TRAN_SETTINGS:
		debug(3, "XPT_SET_TRAN_SETTINGS");
		ccb->ccb_h.status = CAM_FUNC_NOTAVAIL;
		break;


	/*
	 * Reject anything else as unsupported.
	 */
	default:
		/* we can't do this */
		ccb->ccb_h.status = CAM_REQ_INVALID;
		break;
	}

	mtx_assert(&sc->amr_list_lock, MA_OWNED);
	xpt_done(ccb);
}

/***********************************************************************
 * Convert a CAM CCB off the top of the CCB queue to a passthrough SCSI
 * command.
 */
static int
amr_cam_command(struct amr_softc *sc, struct amr_command **acp)
{
	struct amr_command		*ac;
	struct amr_passthrough		*ap;
	struct amr_ext_passthrough	*aep;
	struct ccb_scsiio		*csio;
	int				bus, target, error;

	error = 0;
	ac = NULL;
	ap = NULL;
	aep = NULL;

	/* check to see if there is a ccb for us to work with */
	if ((csio = (struct ccb_scsiio *)amr_dequeue_ccb(sc)) == NULL)
	goto out;

	/* get bus/target, XXX validate against protected devices? */
	bus = csio->ccb_h.sim_priv.entries[0].field;
	target = csio->ccb_h.target_id;

	/*
	 * Build a passthrough command.
	 */

	/* construct command */
	if ((ac = amr_alloccmd(sc)) == NULL) {
		error = ENOMEM;
		goto out;
	}

	/* construct passthrough */
	if (sc->support_ext_cdb ) {
		aep = &ac->ac_ccb->ccb_epthru;
		aep->ap_timeout = 2;
		aep->ap_ars = 1;
		aep->ap_request_sense_length = 14;
		aep->ap_islogical = 0;
		aep->ap_channel = bus;
		aep->ap_scsi_id = target;
		aep->ap_logical_drive_no = csio->ccb_h.target_lun;
		aep->ap_cdb_length = csio->cdb_len;
		aep->ap_data_transfer_length = csio->dxfer_len;
		if (csio->ccb_h.flags & CAM_CDB_POINTER) {
			bcopy(csio->cdb_io.cdb_ptr, aep->ap_cdb, csio->cdb_len);
		} else {
			bcopy(csio->cdb_io.cdb_bytes, aep->ap_cdb,
			    csio->cdb_len);
		}
		/*
		 * we leave the data s/g list and s/g count to the map routine
		 * later
		 */

		debug(2, " COMMAND %x/%d+%d to %d:%d:%d", aep->ap_cdb[0],
		    aep->ap_cdb_length, csio->dxfer_len, aep->ap_channel,
		    aep->ap_scsi_id, aep->ap_logical_drive_no);

	} else {
		ap = &ac->ac_ccb->ccb_pthru;
		ap->ap_timeout = 0;
		ap->ap_ars = 1;
		ap->ap_request_sense_length = 14;
		ap->ap_islogical = 0;
		ap->ap_channel = bus;
		ap->ap_scsi_id = target;
		ap->ap_logical_drive_no = csio->ccb_h.target_lun;
		ap->ap_cdb_length = csio->cdb_len;
		ap->ap_data_transfer_length = csio->dxfer_len;
		if (csio->ccb_h.flags & CAM_CDB_POINTER) {
			bcopy(csio->cdb_io.cdb_ptr, ap->ap_cdb, csio->cdb_len);
		} else {
			bcopy(csio->cdb_io.cdb_bytes, ap->ap_cdb,
			    csio->cdb_len);
		}
		/*
		 * we leave the data s/g list and s/g count to the map routine
		 * later
		 */

		debug(2, " COMMAND %x/%d+%d to %d:%d:%d", ap->ap_cdb[0],
		    ap->ap_cdb_length, csio->dxfer_len, ap->ap_channel,
		    ap->ap_scsi_id, ap->ap_logical_drive_no);
	}

	ac->ac_flags |= AMR_CMD_CCB;

	ac->ac_data = csio->data_ptr;
	ac->ac_length = csio->dxfer_len;
	if ((csio->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_IN)
		ac->ac_flags |= AMR_CMD_DATAIN;
	if ((csio->ccb_h.flags & CAM_DIR_MASK) == CAM_DIR_OUT)
		ac->ac_flags |= AMR_CMD_DATAOUT;

	ac->ac_private = csio;
	ac->ac_complete = amr_cam_complete;
	if ( sc->support_ext_cdb ) {
		ac->ac_mailbox.mb_command = AMR_CMD_EXTPASS;
	} else {
		ac->ac_mailbox.mb_command = AMR_CMD_PASS;
	}

out:
	if (error != 0) {
		if (ac != NULL)
			amr_releasecmd(ac);
		if (csio != NULL)
			/* put it back and try again later */
			amr_requeue_ccb(sc, (union ccb *)csio);
	}
	*acp = ac;
	return(error);
}

/***********************************************************************
 * Check for interrupt status
 */
static void
amr_cam_poll(struct cam_sim *sim)
{

	amr_done(cam_sim_softc(sim));
}

 /**********************************************************************
 * Handle completion of a command submitted via CAM.
 */
static void
amr_cam_complete(struct amr_command *ac)
{
	struct amr_passthrough		*ap;
	struct amr_ext_passthrough	*aep;
	struct ccb_scsiio		*csio;
	struct scsi_inquiry_data	*inq;
	int				scsi_status, cdb0;

	ap = &ac->ac_ccb->ccb_pthru;
	aep = &ac->ac_ccb->ccb_epthru;
	csio = (struct ccb_scsiio *)ac->ac_private;
	inq = (struct scsi_inquiry_data *)csio->data_ptr;

	if (ac->ac_mailbox.mb_command == AMR_CMD_EXTPASS)
		scsi_status = aep->ap_scsi_status;
	else
		scsi_status = ap->ap_scsi_status;
	debug(1, "status 0x%x  AP scsi_status 0x%x", ac->ac_status,
	    scsi_status);

	/* Make sure the status is sane */
	if ((ac->ac_status != AMR_STATUS_SUCCESS) && (scsi_status == 0)) {
		csio->ccb_h.status = CAM_REQ_CMP_ERR;
		goto out;
	}

	/*
	 * Hide disks from CAM so that they're not picked up and treated as
	 * 'normal' disks.
	 *
	 * If the configuration provides a mechanism to mark a disk a "not
	 * managed", we could add handling for that to allow disks to be
	 * selectively visible.
	 */

	/* handle passthrough SCSI status */
	switch(scsi_status) {
	case 0:	/* completed OK */
		if (ac->ac_mailbox.mb_command == AMR_CMD_EXTPASS)
			cdb0 = aep->ap_cdb[0];
		else
			cdb0 = ap->ap_cdb[0];
		if ((cdb0 == INQUIRY) && (SID_TYPE(inq) == T_DIRECT))
			inq->device = (inq->device & 0xe0) | T_NODEVICE;
		csio->ccb_h.status = CAM_REQ_CMP;
		break;

	case 0x02:
		csio->ccb_h.status = CAM_SCSI_STATUS_ERROR;
		csio->scsi_status = SCSI_STATUS_CHECK_COND;
		if (ac->ac_mailbox.mb_command == AMR_CMD_EXTPASS)
			bcopy(aep->ap_request_sense_area, &csio->sense_data,
			    AMR_MAX_REQ_SENSE_LEN);
		else
			bcopy(ap->ap_request_sense_area, &csio->sense_data,
			    AMR_MAX_REQ_SENSE_LEN);
		csio->sense_len = AMR_MAX_REQ_SENSE_LEN;
		csio->ccb_h.status |= CAM_AUTOSNS_VALID;
		break;

	case 0x08:
		csio->ccb_h.status = CAM_SCSI_BUSY;
		break;

	case 0xf0:
	case 0xf4:
	default:
		/*
		 * Non-zero LUNs are already filtered, so there's no need
		 * to return CAM_DEV_NOT_THERE.
		 */
		csio->ccb_h.status = CAM_SEL_TIMEOUT;
		break;
	}

out:
	if ((csio->ccb_h.flags & CAM_DIR_MASK) != CAM_DIR_NONE)
		debug(2, "%*D\n", imin(csio->dxfer_len, 16), csio->data_ptr,
		    " ");

	mtx_lock(&ac->ac_sc->amr_list_lock);
	xpt_done((union ccb *)csio);
	amr_releasecmd(ac);
	mtx_unlock(&ac->ac_sc->amr_list_lock);
}
