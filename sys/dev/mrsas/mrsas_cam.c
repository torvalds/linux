/*
 * Copyright (c) 2015, AVAGO Tech. All rights reserved. Author: Marian Choy
 * Copyright (c) 2014, LSI Corp. All rights reserved. Author: Marian Choy
 * Support: freebsdraid@avagotech.com
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer. 2. Redistributions
 * in binary form must reproduce the above copyright notice, this list of
 * conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution. 3. Neither the name of the
 * <ORGANIZATION> nor the names of its contributors may be used to endorse or
 * promote products derived from this software without specific prior written
 * permission.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "dev/mrsas/mrsas.h"

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>
#include <cam/cam_debug.h>
#include <cam/cam_periph.h>
#include <cam/cam_xpt_periph.h>

#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>
#include <sys/taskqueue.h>
#include <sys/kernel.h>

#include <sys/time.h>			/* XXX for pcpu.h */
#include <sys/pcpu.h>			/* XXX for PCPU_GET */

#define	smp_processor_id()  PCPU_GET(cpuid)

/*
 * Function prototypes
 */
int	mrsas_cam_attach(struct mrsas_softc *sc);
int	mrsas_find_io_type(struct cam_sim *sim, union ccb *ccb);
int	mrsas_bus_scan(struct mrsas_softc *sc);
int	mrsas_bus_scan_sim(struct mrsas_softc *sc, struct cam_sim *sim);
int 
mrsas_map_request(struct mrsas_softc *sc,
    struct mrsas_mpt_cmd *cmd, union ccb *ccb);
int
mrsas_build_ldio_rw(struct mrsas_softc *sc, struct mrsas_mpt_cmd *cmd,
    union ccb *ccb);
int
mrsas_build_ldio_nonrw(struct mrsas_softc *sc, struct mrsas_mpt_cmd *cmd,
    union ccb *ccb);
int
mrsas_build_syspdio(struct mrsas_softc *sc, struct mrsas_mpt_cmd *cmd,
    union ccb *ccb, struct cam_sim *sim, u_int8_t fp_possible);
int
mrsas_setup_io(struct mrsas_softc *sc, struct mrsas_mpt_cmd *cmd,
    union ccb *ccb, u_int32_t device_id,
    MRSAS_RAID_SCSI_IO_REQUEST * io_request);
void	mrsas_xpt_freeze(struct mrsas_softc *sc);
void	mrsas_xpt_release(struct mrsas_softc *sc);
void	mrsas_cam_detach(struct mrsas_softc *sc);
void	mrsas_release_mpt_cmd(struct mrsas_mpt_cmd *cmd);
void	mrsas_unmap_request(struct mrsas_softc *sc, struct mrsas_mpt_cmd *cmd);
void	mrsas_cmd_done(struct mrsas_softc *sc, struct mrsas_mpt_cmd *cmd);
void
mrsas_fire_cmd(struct mrsas_softc *sc, u_int32_t req_desc_lo,
    u_int32_t req_desc_hi);
void
mrsas_set_pd_lba(MRSAS_RAID_SCSI_IO_REQUEST * io_request,
    u_int8_t cdb_len, struct IO_REQUEST_INFO *io_info, union ccb *ccb,
    MR_DRV_RAID_MAP_ALL * local_map_ptr, u_int32_t ref_tag,
    u_int32_t ld_block_size);
static void mrsas_freeze_simq(struct mrsas_mpt_cmd *cmd, struct cam_sim *sim);
static void mrsas_cam_poll(struct cam_sim *sim);
static void mrsas_action(struct cam_sim *sim, union ccb *ccb);
static void mrsas_scsiio_timeout(void *data);
static int mrsas_track_scsiio(struct mrsas_softc *sc, target_id_t id, u_int32_t bus_id);
static void mrsas_tm_response_code(struct mrsas_softc *sc,
    MPI2_SCSI_TASK_MANAGE_REPLY *mpi_reply);
static int mrsas_issue_tm(struct mrsas_softc *sc,
    MRSAS_REQUEST_DESCRIPTOR_UNION *req_desc);
static void
mrsas_data_load_cb(void *arg, bus_dma_segment_t *segs,
    int nseg, int error);
static int32_t
mrsas_startio(struct mrsas_softc *sc, struct cam_sim *sim,
    union ccb *ccb);

static boolean_t mrsas_is_prp_possible(struct mrsas_mpt_cmd *cmd,
	bus_dma_segment_t *segs, int nsegs);
static void mrsas_build_ieee_sgl(struct mrsas_mpt_cmd *cmd,
	bus_dma_segment_t *segs, int nseg);
static void mrsas_build_prp_nvme(struct mrsas_mpt_cmd *cmd,
	bus_dma_segment_t *segs, int nseg);

struct mrsas_mpt_cmd *mrsas_get_mpt_cmd(struct mrsas_softc *sc);
MRSAS_REQUEST_DESCRIPTOR_UNION *
	mrsas_get_request_desc(struct mrsas_softc *sc, u_int16_t index);

extern int mrsas_reset_targets(struct mrsas_softc *sc);
extern u_int16_t MR_TargetIdToLdGet(u_int32_t ldTgtId, MR_DRV_RAID_MAP_ALL * map);
extern u_int32_t
MR_LdBlockSizeGet(u_int32_t ldTgtId, MR_DRV_RAID_MAP_ALL * map);
extern void mrsas_isr(void *arg);
extern void mrsas_aen_handler(struct mrsas_softc *sc);
extern u_int8_t
MR_BuildRaidContext(struct mrsas_softc *sc,
    struct IO_REQUEST_INFO *io_info, RAID_CONTEXT * pRAID_Context,
    MR_DRV_RAID_MAP_ALL * map);
extern u_int16_t
MR_LdSpanArrayGet(u_int32_t ld, u_int32_t span,
    MR_DRV_RAID_MAP_ALL * map);
extern u_int16_t 
mrsas_get_updated_dev_handle(struct mrsas_softc *sc,
    PLD_LOAD_BALANCE_INFO lbInfo, struct IO_REQUEST_INFO *io_info);
extern int mrsas_complete_cmd(struct mrsas_softc *sc, u_int32_t MSIxIndex);
extern MR_LD_RAID *MR_LdRaidGet(u_int32_t ld, MR_DRV_RAID_MAP_ALL * map);
extern void mrsas_disable_intr(struct mrsas_softc *sc);
extern void mrsas_enable_intr(struct mrsas_softc *sc);
void mrsas_prepare_secondRaid1_IO(struct mrsas_softc *sc,
    struct mrsas_mpt_cmd *cmd);

/*
 * mrsas_cam_attach:	Main entry to CAM subsystem
 * input:				Adapter instance soft state
 *
 * This function is called from mrsas_attach() during initialization to perform
 * SIM allocations and XPT bus registration.  If the kernel version is 7.4 or
 * earlier, it would also initiate a bus scan.
 */
int
mrsas_cam_attach(struct mrsas_softc *sc)
{
	struct cam_devq *devq;
	int mrsas_cam_depth;

	mrsas_cam_depth = sc->max_scsi_cmds;

	if ((devq = cam_simq_alloc(mrsas_cam_depth)) == NULL) {
		device_printf(sc->mrsas_dev, "Cannot allocate SIM queue\n");
		return (ENOMEM);
	}
	/*
	 * Create SIM for bus 0 and register, also create path
	 */
	sc->sim_0 = cam_sim_alloc(mrsas_action, mrsas_cam_poll, "mrsas", sc,
	    device_get_unit(sc->mrsas_dev), &sc->sim_lock, mrsas_cam_depth,
	    mrsas_cam_depth, devq);
	if (sc->sim_0 == NULL) {
		cam_simq_free(devq);
		device_printf(sc->mrsas_dev, "Cannot register SIM\n");
		return (ENXIO);
	}
	/* Initialize taskqueue for Event Handling */
	TASK_INIT(&sc->ev_task, 0, (void *)mrsas_aen_handler, sc);
	sc->ev_tq = taskqueue_create("mrsas_taskq", M_NOWAIT | M_ZERO,
	    taskqueue_thread_enqueue, &sc->ev_tq);

	/* Run the task queue with lowest priority */
	taskqueue_start_threads(&sc->ev_tq, 1, 255, "%s taskq",
	    device_get_nameunit(sc->mrsas_dev));
	mtx_lock(&sc->sim_lock);
	if (xpt_bus_register(sc->sim_0, sc->mrsas_dev, 0) != CAM_SUCCESS) {
		cam_sim_free(sc->sim_0, TRUE);	/* passing true frees the devq */
		mtx_unlock(&sc->sim_lock);
		return (ENXIO);
	}
	if (xpt_create_path(&sc->path_0, NULL, cam_sim_path(sc->sim_0),
	    CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
		xpt_bus_deregister(cam_sim_path(sc->sim_0));
		cam_sim_free(sc->sim_0, TRUE);	/* passing true will free the
						 * devq */
		mtx_unlock(&sc->sim_lock);
		return (ENXIO);
	}
	mtx_unlock(&sc->sim_lock);

	/*
	 * Create SIM for bus 1 and register, also create path
	 */
	sc->sim_1 = cam_sim_alloc(mrsas_action, mrsas_cam_poll, "mrsas", sc,
	    device_get_unit(sc->mrsas_dev), &sc->sim_lock, mrsas_cam_depth,
	    mrsas_cam_depth, devq);
	if (sc->sim_1 == NULL) {
		cam_simq_free(devq);
		device_printf(sc->mrsas_dev, "Cannot register SIM\n");
		return (ENXIO);
	}
	mtx_lock(&sc->sim_lock);
	if (xpt_bus_register(sc->sim_1, sc->mrsas_dev, 1) != CAM_SUCCESS) {
		cam_sim_free(sc->sim_1, TRUE);	/* passing true frees the devq */
		mtx_unlock(&sc->sim_lock);
		return (ENXIO);
	}
	if (xpt_create_path(&sc->path_1, NULL, cam_sim_path(sc->sim_1),
	    CAM_TARGET_WILDCARD,
	    CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
		xpt_bus_deregister(cam_sim_path(sc->sim_1));
		cam_sim_free(sc->sim_1, TRUE);
		mtx_unlock(&sc->sim_lock);
		return (ENXIO);
	}
	mtx_unlock(&sc->sim_lock);

#if (__FreeBSD_version <= 704000)
	if (mrsas_bus_scan(sc)) {
		device_printf(sc->mrsas_dev, "Error in bus scan.\n");
		return (1);
	}
#endif
	return (0);
}

/*
 * mrsas_cam_detach:	De-allocates and teardown CAM
 * input:				Adapter instance soft state
 *
 * De-registers and frees the paths and SIMs.
 */
void
mrsas_cam_detach(struct mrsas_softc *sc)
{
	if (sc->ev_tq != NULL)
		taskqueue_free(sc->ev_tq);
	mtx_lock(&sc->sim_lock);
	if (sc->path_0)
		xpt_free_path(sc->path_0);
	if (sc->sim_0) {
		xpt_bus_deregister(cam_sim_path(sc->sim_0));
		cam_sim_free(sc->sim_0, FALSE);
	}
	if (sc->path_1)
		xpt_free_path(sc->path_1);
	if (sc->sim_1) {
		xpt_bus_deregister(cam_sim_path(sc->sim_1));
		cam_sim_free(sc->sim_1, TRUE);
	}
	mtx_unlock(&sc->sim_lock);
}

/*
 * mrsas_action:	SIM callback entry point
 * input:			pointer to SIM pointer to CAM Control Block
 *
 * This function processes CAM subsystem requests. The type of request is stored
 * in ccb->ccb_h.func_code.  The preprocessor #ifdef is necessary because
 * ccb->cpi.maxio is not supported for FreeBSD version 7.4 or earlier.
 */
static void
mrsas_action(struct cam_sim *sim, union ccb *ccb)
{
	struct mrsas_softc *sc = (struct mrsas_softc *)cam_sim_softc(sim);
	struct ccb_hdr *ccb_h = &(ccb->ccb_h);
	u_int32_t device_id;

	/*
     * Check if the system going down
     * or the adapter is in unrecoverable critical error
     */
    if (sc->remove_in_progress ||
        (sc->adprecovery == MRSAS_HW_CRITICAL_ERROR)) {
        ccb->ccb_h.status |= CAM_DEV_NOT_THERE;
        xpt_done(ccb);
        return;
    }

	switch (ccb->ccb_h.func_code) {
	case XPT_SCSI_IO:
		{
			device_id = ccb_h->target_id;

			/*
			 * bus 0 is LD, bus 1 is for system-PD
			 */
			if (cam_sim_bus(sim) == 1 &&
			    sc->pd_list[device_id].driveState != MR_PD_STATE_SYSTEM) {
				ccb->ccb_h.status |= CAM_DEV_NOT_THERE;
				xpt_done(ccb);
			} else {
				if (mrsas_startio(sc, sim, ccb)) {
					ccb->ccb_h.status |= CAM_REQ_INVALID;
					xpt_done(ccb);
				}
			}
			break;
		}
	case XPT_ABORT:
		{
			ccb->ccb_h.status = CAM_UA_ABORT;
			xpt_done(ccb);
			break;
		}
	case XPT_RESET_BUS:
		{
			xpt_done(ccb);
			break;
		}
	case XPT_GET_TRAN_SETTINGS:
		{
			ccb->cts.protocol = PROTO_SCSI;
			ccb->cts.protocol_version = SCSI_REV_2;
			ccb->cts.transport = XPORT_SPI;
			ccb->cts.transport_version = 2;
			ccb->cts.xport_specific.spi.valid = CTS_SPI_VALID_DISC;
			ccb->cts.xport_specific.spi.flags = CTS_SPI_FLAGS_DISC_ENB;
			ccb->cts.proto_specific.scsi.valid = CTS_SCSI_VALID_TQ;
			ccb->cts.proto_specific.scsi.flags = CTS_SCSI_FLAGS_TAG_ENB;
			ccb->ccb_h.status = CAM_REQ_CMP;
			xpt_done(ccb);
			break;
		}
	case XPT_SET_TRAN_SETTINGS:
		{
			ccb->ccb_h.status = CAM_FUNC_NOTAVAIL;
			xpt_done(ccb);
			break;
		}
	case XPT_CALC_GEOMETRY:
		{
			cam_calc_geometry(&ccb->ccg, 1);
			xpt_done(ccb);
			break;
		}
	case XPT_PATH_INQ:
		{
			ccb->cpi.version_num = 1;
			ccb->cpi.hba_inquiry = 0;
			ccb->cpi.target_sprt = 0;
#if (__FreeBSD_version >= 902001)
			ccb->cpi.hba_misc = PIM_UNMAPPED;
#else
			ccb->cpi.hba_misc = 0;
#endif
			ccb->cpi.hba_eng_cnt = 0;
			ccb->cpi.max_lun = MRSAS_SCSI_MAX_LUNS;
			ccb->cpi.unit_number = cam_sim_unit(sim);
			ccb->cpi.bus_id = cam_sim_bus(sim);
			ccb->cpi.initiator_id = MRSAS_SCSI_INITIATOR_ID;
			ccb->cpi.base_transfer_speed = 150000;
			strlcpy(ccb->cpi.sim_vid, "FreeBSD", SIM_IDLEN);
			strlcpy(ccb->cpi.hba_vid, "AVAGO", HBA_IDLEN);
			strlcpy(ccb->cpi.dev_name, cam_sim_name(sim), DEV_IDLEN);
			ccb->cpi.transport = XPORT_SPI;
			ccb->cpi.transport_version = 2;
			ccb->cpi.protocol = PROTO_SCSI;
			ccb->cpi.protocol_version = SCSI_REV_2;
			if (ccb->cpi.bus_id == 0)
				ccb->cpi.max_target = MRSAS_MAX_PD - 1;
			else
				ccb->cpi.max_target = MRSAS_MAX_LD_IDS - 1;
#if (__FreeBSD_version > 704000)
			ccb->cpi.maxio = sc->max_num_sge * MRSAS_PAGE_SIZE;
#endif
			ccb->ccb_h.status = CAM_REQ_CMP;
			xpt_done(ccb);
			break;
		}
	default:
		{
			ccb->ccb_h.status = CAM_REQ_INVALID;
			xpt_done(ccb);
			break;
		}
	}
}

/*
 * mrsas_scsiio_timeout:	Callback function for IO timed out
 * input:					mpt command context
 *
 * This function will execute after timeout value provided by ccb header from
 * CAM layer, if timer expires. Driver will run timer for all DCDM and LDIO
 * coming from CAM layer. This function is callback function for IO timeout
 * and it runs in no-sleep context. Set do_timedout_reset in Adapter context
 * so that it will execute OCR/Kill adpter from ocr_thread context.
 */
static void
mrsas_scsiio_timeout(void *data)
{
	struct mrsas_mpt_cmd *cmd;
	struct mrsas_softc *sc;
	u_int32_t target_id;

	if (!data)
		return;

	cmd = (struct mrsas_mpt_cmd *)data;
	sc = cmd->sc;

	if (cmd->ccb_ptr == NULL) {
		printf("command timeout with NULL ccb\n");
		return;
	}

	/*
	 * Below callout is dummy entry so that it will be cancelled from
	 * mrsas_cmd_done(). Now Controller will go to OCR/Kill Adapter based
	 * on OCR enable/disable property of Controller from ocr_thread
	 * context.
	 */
#if (__FreeBSD_version >= 1000510)
	callout_reset_sbt(&cmd->cm_callout, SBT_1S * 180, 0,
	    mrsas_scsiio_timeout, cmd, 0);
#else
	callout_reset(&cmd->cm_callout, (180000 * hz) / 1000,
	    mrsas_scsiio_timeout, cmd);
#endif

	if (cmd->ccb_ptr->cpi.bus_id == 0)
		target_id = cmd->ccb_ptr->ccb_h.target_id;
	else
		target_id = (cmd->ccb_ptr->ccb_h.target_id + (MRSAS_MAX_PD - 1));

	/* Save the cmd to be processed for TM, if it is not there in the array */
	if (sc->target_reset_pool[target_id] == NULL) {
		sc->target_reset_pool[target_id] = cmd;
		mrsas_atomic_inc(&sc->target_reset_outstanding);
	}

	return;
}

/*
 * mrsas_startio:	SCSI IO entry point
 * input:			Adapter instance soft state
 * 					pointer to CAM Control Block
 *
 * This function is the SCSI IO entry point and it initiates IO processing. It
 * copies the IO and depending if the IO is read/write or inquiry, it would
 * call mrsas_build_ldio() or mrsas_build_dcdb(), respectively.  It returns 0
 * if the command is sent to firmware successfully, otherwise it returns 1.
 */
static int32_t
mrsas_startio(struct mrsas_softc *sc, struct cam_sim *sim,
    union ccb *ccb)
{
	struct mrsas_mpt_cmd *cmd, *r1_cmd = NULL;
	struct ccb_hdr *ccb_h = &(ccb->ccb_h);
	struct ccb_scsiio *csio = &(ccb->csio);
	MRSAS_REQUEST_DESCRIPTOR_UNION *req_desc;
	u_int8_t cmd_type;

	if ((csio->cdb_io.cdb_bytes[0]) == SYNCHRONIZE_CACHE &&
		(!sc->fw_sync_cache_support)) {
		ccb->ccb_h.status = CAM_REQ_CMP;
		xpt_done(ccb);
		return (0);
	}
	ccb_h->status |= CAM_SIM_QUEUED;

	if (mrsas_atomic_inc_return(&sc->fw_outstanding) > sc->max_scsi_cmds) {
		ccb_h->status |= CAM_REQUEUE_REQ;
		xpt_done(ccb);
		mrsas_atomic_dec(&sc->fw_outstanding); 
		return (0);
	}

	cmd = mrsas_get_mpt_cmd(sc);

	if (!cmd) {
		ccb_h->status |= CAM_REQUEUE_REQ;
		xpt_done(ccb);
		mrsas_atomic_dec(&sc->fw_outstanding); 
		return (0);
	}

	if ((ccb_h->flags & CAM_DIR_MASK) != CAM_DIR_NONE) {
		if (ccb_h->flags & CAM_DIR_IN)
			cmd->flags |= MRSAS_DIR_IN;
		if (ccb_h->flags & CAM_DIR_OUT)
			cmd->flags |= MRSAS_DIR_OUT;
	} else
		cmd->flags = MRSAS_DIR_NONE;	/* no data */

/* For FreeBSD 9.2 and higher */
#if (__FreeBSD_version >= 902001)
	/*
	 * XXX We don't yet support physical addresses here.
	 */
	switch ((ccb->ccb_h.flags & CAM_DATA_MASK)) {
	case CAM_DATA_PADDR:
	case CAM_DATA_SG_PADDR:
		device_printf(sc->mrsas_dev, "%s: physical addresses not supported\n",
		    __func__);
		mrsas_release_mpt_cmd(cmd);
		ccb_h->status = CAM_REQ_INVALID;
		ccb_h->status &= ~CAM_SIM_QUEUED;
		goto done;
	case CAM_DATA_SG:
		device_printf(sc->mrsas_dev, "%s: scatter gather is not supported\n",
		    __func__);
		mrsas_release_mpt_cmd(cmd);
		ccb_h->status = CAM_REQ_INVALID;
		goto done;
	case CAM_DATA_VADDR:
		if (csio->dxfer_len > (sc->max_num_sge * MRSAS_PAGE_SIZE)) {
			mrsas_release_mpt_cmd(cmd);
			ccb_h->status = CAM_REQ_TOO_BIG;
			goto done;
		}
		cmd->length = csio->dxfer_len;
		if (cmd->length)
			cmd->data = csio->data_ptr;
		break;
	case CAM_DATA_BIO:
		if (csio->dxfer_len > (sc->max_num_sge * MRSAS_PAGE_SIZE)) {
			mrsas_release_mpt_cmd(cmd);
			ccb_h->status = CAM_REQ_TOO_BIG;
			goto done;
		}
		cmd->length = csio->dxfer_len;
		if (cmd->length)
			cmd->data = csio->data_ptr;
		break;
	default:
		ccb->ccb_h.status = CAM_REQ_INVALID;
		goto done;
	}
#else
	if (!(ccb_h->flags & CAM_DATA_PHYS)) {	/* Virtual data address */
		if (!(ccb_h->flags & CAM_SCATTER_VALID)) {
			if (csio->dxfer_len > (sc->max_num_sge * MRSAS_PAGE_SIZE)) {
				mrsas_release_mpt_cmd(cmd);
				ccb_h->status = CAM_REQ_TOO_BIG;
				goto done;
			}
			cmd->length = csio->dxfer_len;
			if (cmd->length)
				cmd->data = csio->data_ptr;
		} else {
			mrsas_release_mpt_cmd(cmd);
			ccb_h->status = CAM_REQ_INVALID;
			goto done;
		}
	} else {			/* Data addresses are physical. */
		mrsas_release_mpt_cmd(cmd);
		ccb_h->status = CAM_REQ_INVALID;
		ccb_h->status &= ~CAM_SIM_QUEUED;
		goto done;
	}
#endif
	/* save ccb ptr */
	cmd->ccb_ptr = ccb;

	req_desc = mrsas_get_request_desc(sc, (cmd->index) - 1);
	if (!req_desc) {
		device_printf(sc->mrsas_dev, "Cannot get request_descriptor.\n");
		return (FAIL);
	}
	memset(req_desc, 0, sizeof(MRSAS_REQUEST_DESCRIPTOR_UNION));
	cmd->request_desc = req_desc;

	if (ccb_h->flags & CAM_CDB_POINTER)
		bcopy(csio->cdb_io.cdb_ptr, cmd->io_request->CDB.CDB32, csio->cdb_len);
	else
		bcopy(csio->cdb_io.cdb_bytes, cmd->io_request->CDB.CDB32, csio->cdb_len);
	mtx_lock(&sc->raidmap_lock);

	/* Check for IO type READ-WRITE targeted for Logical Volume */
	cmd_type = mrsas_find_io_type(sim, ccb);
	switch (cmd_type) {
	case READ_WRITE_LDIO:
		/* Build READ-WRITE IO for Logical Volume  */
		if (mrsas_build_ldio_rw(sc, cmd, ccb)) {
			device_printf(sc->mrsas_dev, "Build RW LDIO failed.\n");
			mtx_unlock(&sc->raidmap_lock);
			mrsas_release_mpt_cmd(cmd);
			return (1);
		}
		break;
	case NON_READ_WRITE_LDIO:
		/* Build NON READ-WRITE IO for Logical Volume  */
		if (mrsas_build_ldio_nonrw(sc, cmd, ccb)) {
			device_printf(sc->mrsas_dev, "Build NON-RW LDIO failed.\n");
			mtx_unlock(&sc->raidmap_lock);
			mrsas_release_mpt_cmd(cmd);
			return (1);
		}
		break;
	case READ_WRITE_SYSPDIO:
	case NON_READ_WRITE_SYSPDIO:
		if (sc->secure_jbod_support &&
		    (cmd_type == NON_READ_WRITE_SYSPDIO)) {
			/* Build NON-RW IO for JBOD */
			if (mrsas_build_syspdio(sc, cmd, ccb, sim, 0)) {
				device_printf(sc->mrsas_dev,
				    "Build SYSPDIO failed.\n");
				mtx_unlock(&sc->raidmap_lock);
				mrsas_release_mpt_cmd(cmd);
				return (1);
			}
		} else {
			/* Build RW IO for JBOD */
			if (mrsas_build_syspdio(sc, cmd, ccb, sim, 1)) {
				device_printf(sc->mrsas_dev,
				    "Build SYSPDIO failed.\n");
				mtx_unlock(&sc->raidmap_lock);
				mrsas_release_mpt_cmd(cmd);
				return (1);
			}
		}
	}
	mtx_unlock(&sc->raidmap_lock);

	if (cmd->flags == MRSAS_DIR_IN)	/* from device */
		cmd->io_request->Control |= MPI2_SCSIIO_CONTROL_READ;
	else if (cmd->flags == MRSAS_DIR_OUT)	/* to device */
		cmd->io_request->Control |= MPI2_SCSIIO_CONTROL_WRITE;

	cmd->io_request->SGLFlags = MPI2_SGE_FLAGS_64_BIT_ADDRESSING;
	cmd->io_request->SGLOffset0 = offsetof(MRSAS_RAID_SCSI_IO_REQUEST, SGL) / 4;
	cmd->io_request->SenseBufferLowAddress = cmd->sense_phys_addr;
	cmd->io_request->SenseBufferLength = MRSAS_SCSI_SENSE_BUFFERSIZE;

	req_desc = cmd->request_desc;
	req_desc->SCSIIO.SMID = cmd->index;

	/*
	 * Start timer for IO timeout. Default timeout value is 90 second.
	 */
	cmd->callout_owner = true;
#if (__FreeBSD_version >= 1000510)
	callout_reset_sbt(&cmd->cm_callout, SBT_1S * 180, 0,
	    mrsas_scsiio_timeout, cmd, 0);
#else
	callout_reset(&cmd->cm_callout, (180000 * hz) / 1000,
	    mrsas_scsiio_timeout, cmd);
#endif

	if (mrsas_atomic_read(&sc->fw_outstanding) > sc->io_cmds_highwater)
		sc->io_cmds_highwater++;

	/*
	 *  if it is raid 1/10 fp write capable.
	 *  try to get second command from pool and construct it.
	 *  From FW, it has confirmed that lba values of two PDs corresponds to
	 *  single R1/10 LD are always same
	 *
	 */
	/*
	 * driver side count always should be less than max_fw_cmds to get
	 * new command
	 */
	if (cmd->r1_alt_dev_handle != MR_DEVHANDLE_INVALID) {
		mrsas_prepare_secondRaid1_IO(sc, cmd);
		mrsas_fire_cmd(sc, req_desc->addr.u.low,
			req_desc->addr.u.high);
		r1_cmd = cmd->peer_cmd;
		mrsas_fire_cmd(sc, r1_cmd->request_desc->addr.u.low,
				r1_cmd->request_desc->addr.u.high);
	} else {
		mrsas_fire_cmd(sc, req_desc->addr.u.low,
			req_desc->addr.u.high);
	}

	return (0);

done:
	xpt_done(ccb);
	mrsas_atomic_dec(&sc->fw_outstanding); 
	return (0);
}

/*
 * mrsas_find_io_type:	Determines if IO is read/write or inquiry
 * input:			pointer to CAM Control Block
 *
 * This function determines if the IO is read/write or inquiry.  It returns a 1
 * if the IO is read/write and 0 if it is inquiry.
 */
int 
mrsas_find_io_type(struct cam_sim *sim, union ccb *ccb)
{
	struct ccb_scsiio *csio = &(ccb->csio);

	switch (csio->cdb_io.cdb_bytes[0]) {
	case READ_10:
	case WRITE_10:
	case READ_12:
	case WRITE_12:
	case READ_6:
	case WRITE_6:
	case READ_16:
	case WRITE_16:
		return (cam_sim_bus(sim) ?
		    READ_WRITE_SYSPDIO : READ_WRITE_LDIO);
	default:
		return (cam_sim_bus(sim) ?
		    NON_READ_WRITE_SYSPDIO : NON_READ_WRITE_LDIO);
	}
}

/*
 * mrsas_get_mpt_cmd:	Get a cmd from free command pool
 * input:				Adapter instance soft state
 *
 * This function removes an MPT command from the command free list and
 * initializes it.
 */
struct mrsas_mpt_cmd *
mrsas_get_mpt_cmd(struct mrsas_softc *sc)
{
	struct mrsas_mpt_cmd *cmd = NULL;

	mtx_lock(&sc->mpt_cmd_pool_lock);
	if (!TAILQ_EMPTY(&sc->mrsas_mpt_cmd_list_head)) {
		cmd = TAILQ_FIRST(&sc->mrsas_mpt_cmd_list_head);
		TAILQ_REMOVE(&sc->mrsas_mpt_cmd_list_head, cmd, next);
	} else {
		goto out;
	}

	memset((uint8_t *)cmd->io_request, 0, MRSAS_MPI2_RAID_DEFAULT_IO_FRAME_SIZE);
	cmd->data = NULL;
	cmd->length = 0;
	cmd->flags = 0;
	cmd->error_code = 0;
	cmd->load_balance = 0;
	cmd->ccb_ptr = NULL;
out:
	mtx_unlock(&sc->mpt_cmd_pool_lock);
	return cmd;
}

/*
 * mrsas_release_mpt_cmd:	Return a cmd to free command pool
 * input:					Command packet for return to free command pool
 *
 * This function returns an MPT command to the free command list.
 */
void
mrsas_release_mpt_cmd(struct mrsas_mpt_cmd *cmd)
{
	struct mrsas_softc *sc = cmd->sc;

	mtx_lock(&sc->mpt_cmd_pool_lock);
	cmd->r1_alt_dev_handle = MR_DEVHANDLE_INVALID;
	cmd->sync_cmd_idx = (u_int32_t)MRSAS_ULONG_MAX;
	cmd->peer_cmd = NULL;
	cmd->cmd_completed = 0;
	memset((uint8_t *)cmd->io_request, 0,
		sizeof(MRSAS_RAID_SCSI_IO_REQUEST));
	TAILQ_INSERT_HEAD(&(sc->mrsas_mpt_cmd_list_head), cmd, next);
	mtx_unlock(&sc->mpt_cmd_pool_lock);

	return;
}

/*
 * mrsas_get_request_desc:	Get request descriptor from array
 * input:					Adapter instance soft state
 * 							SMID index
 *
 * This function returns a pointer to the request descriptor.
 */
MRSAS_REQUEST_DESCRIPTOR_UNION *
mrsas_get_request_desc(struct mrsas_softc *sc, u_int16_t index)
{
	u_int8_t *p;

	KASSERT(index < sc->max_fw_cmds, ("req_desc is out of range"));
	p = sc->req_desc + sizeof(MRSAS_REQUEST_DESCRIPTOR_UNION) * index;

	return (MRSAS_REQUEST_DESCRIPTOR_UNION *) p;
}




/* mrsas_prepare_secondRaid1_IO
 * It prepares the raid 1 second IO
 */
void
mrsas_prepare_secondRaid1_IO(struct mrsas_softc *sc,
    struct mrsas_mpt_cmd *cmd)
{
	MRSAS_REQUEST_DESCRIPTOR_UNION *req_desc, *req_desc2 = NULL;
	struct mrsas_mpt_cmd *r1_cmd;

	r1_cmd = cmd->peer_cmd;
	req_desc = cmd->request_desc;

	/*
	 * copy the io request frame as well as 8 SGEs data for r1
	 * command
	 */
	memcpy(r1_cmd->io_request, cmd->io_request,
	    (sizeof(MRSAS_RAID_SCSI_IO_REQUEST)));
	memcpy(&r1_cmd->io_request->SGL, &cmd->io_request->SGL,
	    (sc->max_sge_in_main_msg * sizeof(MPI2_SGE_IO_UNION)));

	/* sense buffer is different for r1 command */
	r1_cmd->io_request->SenseBufferLowAddress = r1_cmd->sense_phys_addr;
	r1_cmd->ccb_ptr = cmd->ccb_ptr;

	req_desc2 = mrsas_get_request_desc(sc, r1_cmd->index - 1);
	req_desc2->addr.Words = 0;
	r1_cmd->request_desc = req_desc2;
	req_desc2->SCSIIO.SMID = r1_cmd->index;
	req_desc2->SCSIIO.RequestFlags = req_desc->SCSIIO.RequestFlags;
	r1_cmd->request_desc->SCSIIO.DevHandle = cmd->r1_alt_dev_handle;
	r1_cmd->r1_alt_dev_handle =  cmd->io_request->DevHandle;
	r1_cmd->io_request->DevHandle = cmd->r1_alt_dev_handle;
	cmd->io_request->RaidContext.raid_context_g35.smid.peerSMID =
	    r1_cmd->index;
	r1_cmd->io_request->RaidContext.raid_context_g35.smid.peerSMID =
		cmd->index;
	/*
	 * MSIxIndex of both commands request descriptors
	 * should be same
	 */
	r1_cmd->request_desc->SCSIIO.MSIxIndex = cmd->request_desc->SCSIIO.MSIxIndex;
	/* span arm is different for r1 cmd */
	r1_cmd->io_request->RaidContext.raid_context_g35.spanArm =
	    cmd->io_request->RaidContext.raid_context_g35.spanArm + 1;

}


/*
 * mrsas_build_ldio_rw:	Builds an LDIO command
 * input:				Adapter instance soft state
 * 						Pointer to command packet
 * 						Pointer to CCB
 *
 * This function builds the LDIO command packet.  It returns 0 if the command is
 * built successfully, otherwise it returns a 1.
 */
int
mrsas_build_ldio_rw(struct mrsas_softc *sc, struct mrsas_mpt_cmd *cmd,
    union ccb *ccb)
{
	struct ccb_hdr *ccb_h = &(ccb->ccb_h);
	struct ccb_scsiio *csio = &(ccb->csio);
	u_int32_t device_id;
	MRSAS_RAID_SCSI_IO_REQUEST *io_request;

	device_id = ccb_h->target_id;

	io_request = cmd->io_request;
	io_request->RaidContext.raid_context.VirtualDiskTgtId = device_id;
	io_request->RaidContext.raid_context.status = 0;
	io_request->RaidContext.raid_context.exStatus = 0;

	/* just the cdb len, other flags zero, and ORed-in later for FP */
	io_request->IoFlags = csio->cdb_len;

	if (mrsas_setup_io(sc, cmd, ccb, device_id, io_request) != SUCCESS)
		device_printf(sc->mrsas_dev, "Build ldio or fpio error\n");

	io_request->DataLength = cmd->length;

	if (mrsas_map_request(sc, cmd, ccb) == SUCCESS) {
		if (cmd->sge_count > sc->max_num_sge) {
			device_printf(sc->mrsas_dev, "Error: sge_count (0x%x) exceeds"
			    "max (0x%x) allowed\n", cmd->sge_count, sc->max_num_sge);
			return (FAIL);
		}
		if (sc->is_ventura || sc->is_aero)
			io_request->RaidContext.raid_context_g35.numSGE = cmd->sge_count;
		else {
			/*
			 * numSGE store lower 8 bit of sge_count. numSGEExt store
			 * higher 8 bit of sge_count
			 */
			io_request->RaidContext.raid_context.numSGE = cmd->sge_count;
			io_request->RaidContext.raid_context.numSGEExt = (uint8_t)(cmd->sge_count >> 8);
		}

	} else {
		device_printf(sc->mrsas_dev, "Data map/load failed.\n");
		return (FAIL);
	}
	return (0);
}

/* stream detection on read and and write IOs */
static void
mrsas_stream_detect(struct mrsas_softc *sc, struct mrsas_mpt_cmd *cmd,
    struct IO_REQUEST_INFO *io_info)
{
	u_int32_t device_id = io_info->ldTgtId;
	LD_STREAM_DETECT *current_ld_SD = sc->streamDetectByLD[device_id];
	u_int32_t *track_stream = &current_ld_SD->mruBitMap;
	u_int32_t streamNum, shiftedValues, unshiftedValues;
	u_int32_t indexValueMask, shiftedValuesMask;
	int i;
	boolean_t isReadAhead = false;
	STREAM_DETECT *current_SD;

	/* find possible stream */
	for (i = 0; i < MAX_STREAMS_TRACKED; ++i) {
		streamNum = (*track_stream >> (i * BITS_PER_INDEX_STREAM)) &
				STREAM_MASK;
		current_SD = &current_ld_SD->streamTrack[streamNum];
		/*
		 * if we found a stream, update the raid context and
		 * also update the mruBitMap
		 */
		if (current_SD->nextSeqLBA &&
		    io_info->ldStartBlock >= current_SD->nextSeqLBA &&
		    (io_info->ldStartBlock <= (current_SD->nextSeqLBA+32)) &&
		    (current_SD->isRead == io_info->isRead)) {
			if (io_info->ldStartBlock != current_SD->nextSeqLBA &&
			    (!io_info->isRead || !isReadAhead)) {
				/*
				 * Once the API availible we need to change this.
				 * At this point we are not allowing any gap
				 */
				continue;
			}
			cmd->io_request->RaidContext.raid_context_g35.streamDetected = TRUE;
			current_SD->nextSeqLBA = io_info->ldStartBlock + io_info->numBlocks;
			/*
			 * update the mruBitMap LRU
			 */
			shiftedValuesMask = (1 << i * BITS_PER_INDEX_STREAM) - 1 ;
			shiftedValues = ((*track_stream & shiftedValuesMask) <<
			    BITS_PER_INDEX_STREAM);
			indexValueMask = STREAM_MASK << i * BITS_PER_INDEX_STREAM;
			unshiftedValues = (*track_stream) &
			    (~(shiftedValuesMask | indexValueMask));
			*track_stream =
			    (unshiftedValues | shiftedValues | streamNum);
			return;
		}
	}
	/*
	 * if we did not find any stream, create a new one from the least recently used
	 */
	streamNum = (*track_stream >>
	    ((MAX_STREAMS_TRACKED - 1) * BITS_PER_INDEX_STREAM)) & STREAM_MASK;
	current_SD = &current_ld_SD->streamTrack[streamNum];
	current_SD->isRead = io_info->isRead;
	current_SD->nextSeqLBA = io_info->ldStartBlock + io_info->numBlocks;
	*track_stream = (((*track_stream & ZERO_LAST_STREAM) << 4) | streamNum);
	return;
}


/*
 * mrsas_setup_io:	Set up data including Fast Path I/O
 * input:			Adapter instance soft state
 * 					Pointer to command packet
 * 					Pointer to CCB
 *
 * This function builds the DCDB inquiry command.  It returns 0 if the command
 * is built successfully, otherwise it returns a 1.
 */
int
mrsas_setup_io(struct mrsas_softc *sc, struct mrsas_mpt_cmd *cmd,
    union ccb *ccb, u_int32_t device_id,
    MRSAS_RAID_SCSI_IO_REQUEST * io_request)
{
	struct ccb_hdr *ccb_h = &(ccb->ccb_h);
	struct ccb_scsiio *csio = &(ccb->csio);
	struct IO_REQUEST_INFO io_info;
	MR_DRV_RAID_MAP_ALL *map_ptr;
	struct mrsas_mpt_cmd *r1_cmd = NULL;

	MR_LD_RAID *raid;
	u_int8_t fp_possible;
	u_int32_t start_lba_hi, start_lba_lo, ld_block_size, ld;
	u_int32_t datalength = 0;

	io_request->RaidContext.raid_context.VirtualDiskTgtId = device_id;

	start_lba_lo = 0;
	start_lba_hi = 0;
	fp_possible = 0;

	/*
	 * READ_6 (0x08) or WRITE_6 (0x0A) cdb
	 */
	if (csio->cdb_len == 6) {
		datalength = (u_int32_t)csio->cdb_io.cdb_bytes[4];
		start_lba_lo = ((u_int32_t)csio->cdb_io.cdb_bytes[1] << 16) |
		    ((u_int32_t)csio->cdb_io.cdb_bytes[2] << 8) |
		    (u_int32_t)csio->cdb_io.cdb_bytes[3];
		start_lba_lo &= 0x1FFFFF;
	}
	/*
	 * READ_10 (0x28) or WRITE_6 (0x2A) cdb
	 */
	else if (csio->cdb_len == 10) {
		datalength = (u_int32_t)csio->cdb_io.cdb_bytes[8] |
		    ((u_int32_t)csio->cdb_io.cdb_bytes[7] << 8);
		start_lba_lo = ((u_int32_t)csio->cdb_io.cdb_bytes[2] << 24) |
		    ((u_int32_t)csio->cdb_io.cdb_bytes[3] << 16) |
		    (u_int32_t)csio->cdb_io.cdb_bytes[4] << 8 |
		    ((u_int32_t)csio->cdb_io.cdb_bytes[5]);
	}
	/*
	 * READ_12 (0xA8) or WRITE_12 (0xAA) cdb
	 */
	else if (csio->cdb_len == 12) {
		datalength = (u_int32_t)csio->cdb_io.cdb_bytes[6] << 24 |
		    ((u_int32_t)csio->cdb_io.cdb_bytes[7] << 16) |
		    ((u_int32_t)csio->cdb_io.cdb_bytes[8] << 8) |
		    ((u_int32_t)csio->cdb_io.cdb_bytes[9]);
		start_lba_lo = ((u_int32_t)csio->cdb_io.cdb_bytes[2] << 24) |
		    ((u_int32_t)csio->cdb_io.cdb_bytes[3] << 16) |
		    (u_int32_t)csio->cdb_io.cdb_bytes[4] << 8 |
		    ((u_int32_t)csio->cdb_io.cdb_bytes[5]);
	}
	/*
	 * READ_16 (0x88) or WRITE_16 (0xx8A) cdb
	 */
	else if (csio->cdb_len == 16) {
		datalength = (u_int32_t)csio->cdb_io.cdb_bytes[10] << 24 |
		    ((u_int32_t)csio->cdb_io.cdb_bytes[11] << 16) |
		    ((u_int32_t)csio->cdb_io.cdb_bytes[12] << 8) |
		    ((u_int32_t)csio->cdb_io.cdb_bytes[13]);
		start_lba_lo = ((u_int32_t)csio->cdb_io.cdb_bytes[6] << 24) |
		    ((u_int32_t)csio->cdb_io.cdb_bytes[7] << 16) |
		    (u_int32_t)csio->cdb_io.cdb_bytes[8] << 8 |
		    ((u_int32_t)csio->cdb_io.cdb_bytes[9]);
		start_lba_hi = ((u_int32_t)csio->cdb_io.cdb_bytes[2] << 24) |
		    ((u_int32_t)csio->cdb_io.cdb_bytes[3] << 16) |
		    (u_int32_t)csio->cdb_io.cdb_bytes[4] << 8 |
		    ((u_int32_t)csio->cdb_io.cdb_bytes[5]);
	}
	memset(&io_info, 0, sizeof(struct IO_REQUEST_INFO));
	io_info.ldStartBlock = ((u_int64_t)start_lba_hi << 32) | start_lba_lo;
	io_info.numBlocks = datalength;
	io_info.ldTgtId = device_id;
	io_info.r1_alt_dev_handle = MR_DEVHANDLE_INVALID;

	io_request->DataLength = cmd->length;

	switch (ccb_h->flags & CAM_DIR_MASK) {
	case CAM_DIR_IN:
		io_info.isRead = 1;
		break;
	case CAM_DIR_OUT:
		io_info.isRead = 0;
		break;
	case CAM_DIR_NONE:
	default:
		mrsas_dprint(sc, MRSAS_TRACE, "From %s : DMA Flag is %d \n", __func__, ccb_h->flags & CAM_DIR_MASK);
		break;
	}

	map_ptr = sc->ld_drv_map[(sc->map_id & 1)];
	ld_block_size = MR_LdBlockSizeGet(device_id, map_ptr);

	ld = MR_TargetIdToLdGet(device_id, map_ptr);
	if ((ld >= MAX_LOGICAL_DRIVES_EXT) || (!sc->fast_path_io)) {
		io_request->RaidContext.raid_context.regLockFlags = 0;
		fp_possible = 0;
	} else {
		if (MR_BuildRaidContext(sc, &io_info, &io_request->RaidContext.raid_context, map_ptr))
			fp_possible = io_info.fpOkForIo;
	}

	raid = MR_LdRaidGet(ld, map_ptr);
	/* Store the TM capability value in cmd */
	cmd->tmCapable = raid->capability.tmCapable;

	cmd->request_desc->SCSIIO.MSIxIndex =
	    sc->msix_vectors ? smp_processor_id() % sc->msix_vectors : 0;

	if (sc->is_ventura || sc->is_aero) {
		if (sc->streamDetectByLD) {
			mtx_lock(&sc->stream_lock);
			mrsas_stream_detect(sc, cmd, &io_info);
			mtx_unlock(&sc->stream_lock);
			/* In ventura if stream detected for a read and
			 * it is read ahead capable make this IO as LDIO */
			if (io_request->RaidContext.raid_context_g35.streamDetected &&
					io_info.isRead && io_info.raCapable)
				fp_possible = FALSE;
		}

		/* Set raid 1/10 fast path write capable bit in io_info.
		 * Note - reset peer_cmd and r1_alt_dev_handle if fp_possible
		 * disabled after this point. Try not to add more check for
		 * fp_possible toggle after this.
		 */
		if (fp_possible &&
				(io_info.r1_alt_dev_handle != MR_DEVHANDLE_INVALID) &&
				(raid->level == 1) && !io_info.isRead) {
			r1_cmd = mrsas_get_mpt_cmd(sc);
			if (mrsas_atomic_inc_return(&sc->fw_outstanding) > sc->max_scsi_cmds) {
				fp_possible = FALSE;
				mrsas_atomic_dec(&sc->fw_outstanding); 
			} else {
				r1_cmd = mrsas_get_mpt_cmd(sc);
				if (!r1_cmd) {
					fp_possible = FALSE;
					mrsas_atomic_dec(&sc->fw_outstanding); 
				}
				else {
					cmd->peer_cmd = r1_cmd;
					r1_cmd->peer_cmd = cmd;
				}
 			}
		}
	}

	if (fp_possible) {
		mrsas_set_pd_lba(io_request, csio->cdb_len, &io_info, ccb, map_ptr,
		    start_lba_lo, ld_block_size);
		io_request->Function = MPI2_FUNCTION_SCSI_IO_REQUEST;
		cmd->request_desc->SCSIIO.RequestFlags =
		    (MPI2_REQ_DESCRIPT_FLAGS_FP_IO <<
		    MRSAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);
		if (sc->mrsas_gen3_ctrl) {
			if (io_request->RaidContext.raid_context.regLockFlags == REGION_TYPE_UNUSED)
				cmd->request_desc->SCSIIO.RequestFlags =
				    (MRSAS_REQ_DESCRIPT_FLAGS_NO_LOCK <<
				    MRSAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);
			io_request->RaidContext.raid_context.Type = MPI2_TYPE_CUDA;
			io_request->RaidContext.raid_context.nseg = 0x1;
			io_request->IoFlags |= MPI25_SAS_DEVICE0_FLAGS_ENABLED_FAST_PATH;
			io_request->RaidContext.raid_context.regLockFlags |=
			    (MR_RL_FLAGS_GRANT_DESTINATION_CUDA |
			    MR_RL_FLAGS_SEQ_NUM_ENABLE);
		} else if (sc->is_ventura || sc->is_aero) {
			io_request->RaidContext.raid_context_g35.Type = MPI2_TYPE_CUDA;
			io_request->RaidContext.raid_context_g35.nseg = 0x1;
			io_request->RaidContext.raid_context_g35.routingFlags.bits.sqn = 1;
			io_request->IoFlags |= MPI25_SAS_DEVICE0_FLAGS_ENABLED_FAST_PATH;
			if (io_request->RaidContext.raid_context_g35.routingFlags.bits.sld) {
					io_request->RaidContext.raid_context_g35.RAIDFlags =
					(MR_RAID_FLAGS_IO_SUB_TYPE_CACHE_BYPASS
					<< MR_RAID_CTX_RAID_FLAGS_IO_SUB_TYPE_SHIFT);
			}
		}
		if ((sc->load_balance_info[device_id].loadBalanceFlag) &&
		    (io_info.isRead)) {
			io_info.devHandle =
			    mrsas_get_updated_dev_handle(sc,
			    &sc->load_balance_info[device_id], &io_info);
			cmd->load_balance = MRSAS_LOAD_BALANCE_FLAG;
			cmd->pd_r1_lb = io_info.pd_after_lb;
			if (sc->is_ventura || sc->is_aero)
				io_request->RaidContext.raid_context_g35.spanArm = io_info.span_arm;
			else
				io_request->RaidContext.raid_context.spanArm = io_info.span_arm;
		} else
			cmd->load_balance = 0;

		if (sc->is_ventura || sc->is_aero)
				cmd->r1_alt_dev_handle = io_info.r1_alt_dev_handle;
		else
				cmd->r1_alt_dev_handle = MR_DEVHANDLE_INVALID;

		cmd->request_desc->SCSIIO.DevHandle = io_info.devHandle;
		io_request->DevHandle = io_info.devHandle;
		cmd->pdInterface = io_info.pdInterface;
	} else {
		/* Not FP IO */
		io_request->RaidContext.raid_context.timeoutValue = map_ptr->raidMap.fpPdIoTimeoutSec;
		cmd->request_desc->SCSIIO.RequestFlags =
		    (MRSAS_REQ_DESCRIPT_FLAGS_LD_IO <<
		    MRSAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);
		if (sc->mrsas_gen3_ctrl) {
			if (io_request->RaidContext.raid_context.regLockFlags == REGION_TYPE_UNUSED)
				cmd->request_desc->SCSIIO.RequestFlags =
				    (MRSAS_REQ_DESCRIPT_FLAGS_NO_LOCK <<
				    MRSAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);
			io_request->RaidContext.raid_context.Type = MPI2_TYPE_CUDA;
			io_request->RaidContext.raid_context.regLockFlags |=
			    (MR_RL_FLAGS_GRANT_DESTINATION_CPU0 |
			    MR_RL_FLAGS_SEQ_NUM_ENABLE);
			io_request->RaidContext.raid_context.nseg = 0x1;
		} else if (sc->is_ventura || sc->is_aero) {
			io_request->RaidContext.raid_context_g35.Type = MPI2_TYPE_CUDA;
			io_request->RaidContext.raid_context_g35.routingFlags.bits.sqn = 1;
			io_request->RaidContext.raid_context_g35.nseg = 0x1;
		}
		io_request->Function = MRSAS_MPI2_FUNCTION_LD_IO_REQUEST;
		io_request->DevHandle = device_id;
	}
	return (0);
}

/*
 * mrsas_build_ldio_nonrw:	Builds an LDIO command
 * input:				Adapter instance soft state
 * 						Pointer to command packet
 * 						Pointer to CCB
 *
 * This function builds the LDIO command packet.  It returns 0 if the command is
 * built successfully, otherwise it returns a 1.
 */
int
mrsas_build_ldio_nonrw(struct mrsas_softc *sc, struct mrsas_mpt_cmd *cmd,
    union ccb *ccb)
{
	struct ccb_hdr *ccb_h = &(ccb->ccb_h);
	u_int32_t device_id, ld;
	MR_DRV_RAID_MAP_ALL *map_ptr;
	MR_LD_RAID *raid;
	RAID_CONTEXT *pRAID_Context;
	MRSAS_RAID_SCSI_IO_REQUEST *io_request;

	io_request = cmd->io_request;
	device_id = ccb_h->target_id;

	map_ptr = sc->ld_drv_map[(sc->map_id & 1)];
	ld = MR_TargetIdToLdGet(device_id, map_ptr);
	raid = MR_LdRaidGet(ld, map_ptr);
	/* get RAID_Context pointer */
	pRAID_Context = &io_request->RaidContext.raid_context;
	/* Store the TM capability value in cmd */
	cmd->tmCapable = raid->capability.tmCapable;

	/* FW path for LD Non-RW (SCSI management commands) */
	io_request->Function = MRSAS_MPI2_FUNCTION_LD_IO_REQUEST;
	io_request->DevHandle = device_id;
	cmd->request_desc->SCSIIO.RequestFlags =
	    (MPI2_REQ_DESCRIPT_FLAGS_SCSI_IO <<
	    MRSAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);

	io_request->RaidContext.raid_context.VirtualDiskTgtId = device_id;
	io_request->LUN[1] = ccb_h->target_lun & 0xF;
	io_request->DataLength = cmd->length;

	if (mrsas_map_request(sc, cmd, ccb) == SUCCESS) {
		if (cmd->sge_count > sc->max_num_sge) {
			device_printf(sc->mrsas_dev, "Error: sge_count (0x%x) exceeds"
			    "max (0x%x) allowed\n", cmd->sge_count, sc->max_num_sge);
			return (1);
		}
		if (sc->is_ventura || sc->is_aero)
			io_request->RaidContext.raid_context_g35.numSGE = cmd->sge_count;
		else {
			/*
			 * numSGE store lower 8 bit of sge_count. numSGEExt store
			 * higher 8 bit of sge_count
			 */
			io_request->RaidContext.raid_context.numSGE = cmd->sge_count;
			io_request->RaidContext.raid_context.numSGEExt = (uint8_t)(cmd->sge_count >> 8);
		}
	} else {
		device_printf(sc->mrsas_dev, "Data map/load failed.\n");
		return (1);
	}
	return (0);
}

/*
 * mrsas_build_syspdio:	Builds an DCDB command
 * input:				Adapter instance soft state
 * 						Pointer to command packet
 * 						Pointer to CCB
 *
 * This function builds the DCDB inquiry command.  It returns 0 if the command
 * is built successfully, otherwise it returns a 1.
 */
int
mrsas_build_syspdio(struct mrsas_softc *sc, struct mrsas_mpt_cmd *cmd,
    union ccb *ccb, struct cam_sim *sim, u_int8_t fp_possible)
{
	struct ccb_hdr *ccb_h = &(ccb->ccb_h);
	u_int32_t device_id;
	MR_DRV_RAID_MAP_ALL *local_map_ptr;
	MRSAS_RAID_SCSI_IO_REQUEST *io_request;
	RAID_CONTEXT *pRAID_Context;
	struct MR_PD_CFG_SEQ_NUM_SYNC *pd_sync;

	io_request = cmd->io_request;
	/* get RAID_Context pointer */
	pRAID_Context = &io_request->RaidContext.raid_context;
	device_id = ccb_h->target_id;
	local_map_ptr = sc->ld_drv_map[(sc->map_id & 1)];
	io_request->RaidContext.raid_context.RAIDFlags = MR_RAID_FLAGS_IO_SUB_TYPE_SYSTEM_PD
	    << MR_RAID_CTX_RAID_FLAGS_IO_SUB_TYPE_SHIFT;
	io_request->RaidContext.raid_context.regLockFlags = 0;
	io_request->RaidContext.raid_context.regLockRowLBA = 0;
	io_request->RaidContext.raid_context.regLockLength = 0;

	cmd->pdInterface = sc->target_list[device_id].interface_type;

	/* If FW supports PD sequence number */
	if (sc->use_seqnum_jbod_fp &&
	    sc->pd_list[device_id].driveType == 0x00) {
		//printf("Using Drv seq num\n");
		pd_sync = (void *)sc->jbodmap_mem[(sc->pd_seq_map_id - 1) & 1];
		cmd->tmCapable = pd_sync->seq[device_id].capability.tmCapable;
		/* More than 256 PD/JBOD support for Ventura */
		if (sc->support_morethan256jbod)
			io_request->RaidContext.raid_context.VirtualDiskTgtId =
				pd_sync->seq[device_id].pdTargetId;
		else
			io_request->RaidContext.raid_context.VirtualDiskTgtId =
				device_id + 255;
		io_request->RaidContext.raid_context.configSeqNum = pd_sync->seq[device_id].seqNum;
		io_request->DevHandle = pd_sync->seq[device_id].devHandle;
		if (sc->is_ventura || sc->is_aero)
			io_request->RaidContext.raid_context_g35.routingFlags.bits.sqn = 1;
		else
			io_request->RaidContext.raid_context.regLockFlags |=
			    (MR_RL_FLAGS_SEQ_NUM_ENABLE | MR_RL_FLAGS_GRANT_DESTINATION_CUDA);
		/* raid_context.Type = MPI2_TYPE_CUDA is valid only,
		 * if FW support Jbod Sequence number
		 */
		io_request->RaidContext.raid_context.Type = MPI2_TYPE_CUDA;
		io_request->RaidContext.raid_context.nseg = 0x1;
	} else if (sc->fast_path_io) {
		//printf("Using LD RAID map\n");
		io_request->RaidContext.raid_context.VirtualDiskTgtId = device_id;
		io_request->RaidContext.raid_context.configSeqNum = 0;
		local_map_ptr = sc->ld_drv_map[(sc->map_id & 1)];
		io_request->DevHandle =
		    local_map_ptr->raidMap.devHndlInfo[device_id].curDevHdl;
	} else {
		//printf("Using FW PATH\n");
		/* Want to send all IO via FW path */
		io_request->RaidContext.raid_context.VirtualDiskTgtId = device_id;
		io_request->RaidContext.raid_context.configSeqNum = 0;
		io_request->DevHandle = MR_DEVHANDLE_INVALID;
	}

	cmd->request_desc->SCSIIO.DevHandle = io_request->DevHandle;
	cmd->request_desc->SCSIIO.MSIxIndex =
	    sc->msix_vectors ? smp_processor_id() % sc->msix_vectors : 0;

	if (!fp_possible) {
		/* system pd firmware path */
		io_request->Function = MRSAS_MPI2_FUNCTION_LD_IO_REQUEST;
		cmd->request_desc->SCSIIO.RequestFlags =
		    (MPI2_REQ_DESCRIPT_FLAGS_SCSI_IO <<
		    MRSAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);
		io_request->RaidContext.raid_context.timeoutValue =
		    local_map_ptr->raidMap.fpPdIoTimeoutSec;
		io_request->RaidContext.raid_context.VirtualDiskTgtId = device_id;
	} else {
		/* system pd fast path */
		io_request->Function = MPI2_FUNCTION_SCSI_IO_REQUEST;
		io_request->RaidContext.raid_context.timeoutValue = local_map_ptr->raidMap.fpPdIoTimeoutSec;

		/*
		 * NOTE - For system pd RW cmds only IoFlags will be FAST_PATH
		 * Because the NON RW cmds will now go via FW Queue
		 * and not the Exception queue
		 */
		if (sc->mrsas_gen3_ctrl || sc->is_ventura || sc->is_aero)
			io_request->IoFlags |= MPI25_SAS_DEVICE0_FLAGS_ENABLED_FAST_PATH;

		cmd->request_desc->SCSIIO.RequestFlags =
		    (MPI2_REQ_DESCRIPT_FLAGS_FP_IO <<
		    MRSAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);
	}

	io_request->LUN[1] = ccb_h->target_lun & 0xF;
	io_request->DataLength = cmd->length;

	if (mrsas_map_request(sc, cmd, ccb) == SUCCESS) {
		if (cmd->sge_count > sc->max_num_sge) {
			device_printf(sc->mrsas_dev, "Error: sge_count (0x%x) exceeds"
			    "max (0x%x) allowed\n", cmd->sge_count, sc->max_num_sge);
			return (1);
		}
		if (sc->is_ventura || sc->is_aero)
			io_request->RaidContext.raid_context_g35.numSGE = cmd->sge_count;
		else {
			/*
			 * numSGE store lower 8 bit of sge_count. numSGEExt store
			 * higher 8 bit of sge_count
			 */
			io_request->RaidContext.raid_context.numSGE = cmd->sge_count;
			io_request->RaidContext.raid_context.numSGEExt = (uint8_t)(cmd->sge_count >> 8);
		}
	} else {
		device_printf(sc->mrsas_dev, "Data map/load failed.\n");
		return (1);
	}
	return (0);
}

/*
 * mrsas_is_prp_possible:	This function will tell whether PRPs should be built or not
 * sc:						Adapter instance soft state
 * cmd:						MPT command frame pointer
 * nsesg:					Number of OS SGEs
 *
 * This function will check whether IO is qualified to build PRPs
 * return:				true: if PRP should be built
 *						false: if IEEE SGLs should be built
 */
static boolean_t mrsas_is_prp_possible(struct mrsas_mpt_cmd *cmd,
	bus_dma_segment_t *segs, int nsegs)
{
	struct mrsas_softc *sc = cmd->sc;
	int i;
	u_int32_t data_length = 0;
	bool build_prp = false;
	u_int32_t mr_nvme_pg_size;

	mr_nvme_pg_size = max(sc->nvme_page_size, MR_DEFAULT_NVME_PAGE_SIZE);
	data_length = cmd->length;

	if (data_length > (mr_nvme_pg_size * 5))
		build_prp = true;
	else if ((data_length > (mr_nvme_pg_size * 4)) &&
		(data_length <= (mr_nvme_pg_size * 5)))  {
		/* check if 1st SG entry size is < residual beyond 4 pages */
		if ((segs[0].ds_len) < (data_length - (mr_nvme_pg_size * 4)))
			build_prp = true;
	}

	/*check for SGE holes here*/
	for (i = 0; i < nsegs; i++) {
		/* check for mid SGEs */
		if ((i != 0) && (i != (nsegs - 1))) {
				if ((segs[i].ds_addr % mr_nvme_pg_size) ||
					(segs[i].ds_len % mr_nvme_pg_size)) {
					build_prp = false;
					mrsas_atomic_inc(&sc->sge_holes);
					break;
				}
		}

		/* check for first SGE*/
		if ((nsegs > 1) && (i == 0)) {
				if ((segs[i].ds_addr + segs[i].ds_len) % mr_nvme_pg_size) {
					build_prp = false;
					mrsas_atomic_inc(&sc->sge_holes);
					break;
				}
		}

		/* check for Last SGE*/
		if ((nsegs > 1) && (i == (nsegs - 1))) {
				if (segs[i].ds_addr % mr_nvme_pg_size) {
					build_prp = false;
					mrsas_atomic_inc(&sc->sge_holes);
					break;
				}
		}

	}

	return build_prp;
}

/*
 * mrsas_map_request:	Map and load data
 * input:				Adapter instance soft state
 * 						Pointer to command packet
 *
 * For data from OS, map and load the data buffer into bus space.  The SG list
 * is built in the callback.  If the  bus dmamap load is not successful,
 * cmd->error_code will contain the  error code and a 1 is returned.
 */
int 
mrsas_map_request(struct mrsas_softc *sc,
    struct mrsas_mpt_cmd *cmd, union ccb *ccb)
{
	u_int32_t retcode = 0;
	struct cam_sim *sim;

	sim = xpt_path_sim(cmd->ccb_ptr->ccb_h.path);

	if (cmd->data != NULL) {
		/* Map data buffer into bus space */
		mtx_lock(&sc->io_lock);
#if (__FreeBSD_version >= 902001)
		retcode = bus_dmamap_load_ccb(sc->data_tag, cmd->data_dmamap, ccb,
		    mrsas_data_load_cb, cmd, 0);
#else
		retcode = bus_dmamap_load(sc->data_tag, cmd->data_dmamap, cmd->data,
		    cmd->length, mrsas_data_load_cb, cmd, BUS_DMA_NOWAIT);
#endif
		mtx_unlock(&sc->io_lock);
		if (retcode)
			device_printf(sc->mrsas_dev, "bus_dmamap_load(): retcode = %d\n", retcode);
		if (retcode == EINPROGRESS) {
			device_printf(sc->mrsas_dev, "request load in progress\n");
			mrsas_freeze_simq(cmd, sim);
		}
	}
	if (cmd->error_code)
		return (1);
	return (retcode);
}

/*
 * mrsas_unmap_request:	Unmap and unload data
 * input:				Adapter instance soft state
 * 						Pointer to command packet
 *
 * This function unmaps and unloads data from OS.
 */
void
mrsas_unmap_request(struct mrsas_softc *sc, struct mrsas_mpt_cmd *cmd)
{
	if (cmd->data != NULL) {
		if (cmd->flags & MRSAS_DIR_IN)
			bus_dmamap_sync(sc->data_tag, cmd->data_dmamap, BUS_DMASYNC_POSTREAD);
		if (cmd->flags & MRSAS_DIR_OUT)
			bus_dmamap_sync(sc->data_tag, cmd->data_dmamap, BUS_DMASYNC_POSTWRITE);
		mtx_lock(&sc->io_lock);
		bus_dmamap_unload(sc->data_tag, cmd->data_dmamap);
		mtx_unlock(&sc->io_lock);
	}
}

/**
 * mrsas_build_ieee_sgl -	Prepare IEEE SGLs
 * @sc:						Adapter soft state
 * @segs:					OS SGEs pointers
 * @nseg:					Number of OS SGEs
 * @cmd:					Fusion command frame
 * return:					void
 */
static void mrsas_build_ieee_sgl(struct mrsas_mpt_cmd *cmd, bus_dma_segment_t *segs, int nseg)
{
	struct mrsas_softc *sc = cmd->sc;
	MRSAS_RAID_SCSI_IO_REQUEST *io_request;
	pMpi25IeeeSgeChain64_t sgl_ptr;
	int i = 0, sg_processed = 0;

	io_request = cmd->io_request;
	sgl_ptr = (pMpi25IeeeSgeChain64_t)&io_request->SGL;

	if (sc->mrsas_gen3_ctrl || sc->is_ventura || sc->is_aero) {
		pMpi25IeeeSgeChain64_t sgl_ptr_end = sgl_ptr;

		sgl_ptr_end += sc->max_sge_in_main_msg - 1;
		sgl_ptr_end->Flags = 0;
	}
	if (nseg != 0) {
		for (i = 0; i < nseg; i++) {
			sgl_ptr->Address = segs[i].ds_addr;
			sgl_ptr->Length = segs[i].ds_len;
			sgl_ptr->Flags = 0;
			if (sc->mrsas_gen3_ctrl || sc->is_ventura || sc->is_aero) {
				if (i == nseg - 1)
					sgl_ptr->Flags = IEEE_SGE_FLAGS_END_OF_LIST;
			}
			sgl_ptr++;
			sg_processed = i + 1;
			if ((sg_processed == (sc->max_sge_in_main_msg - 1)) &&
				(nseg > sc->max_sge_in_main_msg)) {
				pMpi25IeeeSgeChain64_t sg_chain;

				if (sc->mrsas_gen3_ctrl || sc->is_ventura || sc->is_aero) {
					if ((cmd->io_request->IoFlags & MPI25_SAS_DEVICE0_FLAGS_ENABLED_FAST_PATH)
						!= MPI25_SAS_DEVICE0_FLAGS_ENABLED_FAST_PATH)
						cmd->io_request->ChainOffset = sc->chain_offset_io_request;
					else
						cmd->io_request->ChainOffset = 0;
				} else
					cmd->io_request->ChainOffset = sc->chain_offset_io_request;
				sg_chain = sgl_ptr;
				if (sc->mrsas_gen3_ctrl || sc->is_ventura || sc->is_aero)
					sg_chain->Flags = IEEE_SGE_FLAGS_CHAIN_ELEMENT;
				else
					sg_chain->Flags = (IEEE_SGE_FLAGS_CHAIN_ELEMENT | MPI2_IEEE_SGE_FLAGS_IOCPLBNTA_ADDR);
				sg_chain->Length = (sizeof(MPI2_SGE_IO_UNION) * (nseg - sg_processed));
				sg_chain->Address = cmd->chain_frame_phys_addr;
				sgl_ptr = (pMpi25IeeeSgeChain64_t)cmd->chain_frame;
			}
		}
	}
}

/**
 * mrsas_build_prp_nvme - Prepare PRPs(Physical Region Page)- SGLs specific to NVMe drives only
 * @sc:						Adapter soft state
 * @segs:					OS SGEs pointers
 * @nseg:					Number of OS SGEs
 * @cmd:					Fusion command frame
 * return:					void
 */
static void mrsas_build_prp_nvme(struct mrsas_mpt_cmd *cmd, bus_dma_segment_t *segs, int nseg)
{
	struct mrsas_softc *sc = cmd->sc;
	int sge_len, offset, num_prp_in_chain = 0;
	pMpi25IeeeSgeChain64_t main_chain_element, ptr_first_sgl, sgl_ptr;
	u_int64_t *ptr_sgl;
	bus_addr_t ptr_sgl_phys;
	u_int64_t sge_addr;
	u_int32_t page_mask, page_mask_result, i = 0;
	u_int32_t first_prp_len;
	int data_len = cmd->length;
	u_int32_t mr_nvme_pg_size = max(sc->nvme_page_size,
					MR_DEFAULT_NVME_PAGE_SIZE);

	sgl_ptr = (pMpi25IeeeSgeChain64_t) &cmd->io_request->SGL;
	/*
	 * NVMe has a very convoluted PRP format.  One PRP is required
	 * for each page or partial page.  We need to split up OS SG
	 * entries if they are longer than one page or cross a page
	 * boundary.  We also have to insert a PRP list pointer entry as
	 * the last entry in each physical page of the PRP list.
	 *
	 * NOTE: The first PRP "entry" is actually placed in the first
	 * SGL entry in the main message in IEEE 64 format.  The 2nd
	 * entry in the main message is the chain element, and the rest
	 * of the PRP entries are built in the contiguous PCIe buffer.
	 */
	page_mask = mr_nvme_pg_size - 1;
	ptr_sgl = (u_int64_t *) cmd->chain_frame;
	ptr_sgl_phys = cmd->chain_frame_phys_addr;
	memset(ptr_sgl, 0, sc->max_chain_frame_sz);

	/* Build chain frame element which holds all PRPs except first*/
	main_chain_element = (pMpi25IeeeSgeChain64_t)
	    ((u_int8_t *)sgl_ptr + sizeof(MPI25_IEEE_SGE_CHAIN64));


	main_chain_element->Address = cmd->chain_frame_phys_addr;
	main_chain_element->NextChainOffset = 0;
	main_chain_element->Flags = IEEE_SGE_FLAGS_CHAIN_ELEMENT |
					IEEE_SGE_FLAGS_SYSTEM_ADDR |
					MPI26_IEEE_SGE_FLAGS_NSF_NVME_PRP;


	/* Build first PRP, SGE need not to be PAGE aligned*/
	ptr_first_sgl = sgl_ptr;
	sge_addr = segs[i].ds_addr;
	sge_len = segs[i].ds_len;
	i++;

	offset = (u_int32_t) (sge_addr & page_mask);
	first_prp_len = mr_nvme_pg_size - offset;

	ptr_first_sgl->Address = sge_addr;
	ptr_first_sgl->Length = first_prp_len;

	data_len -= first_prp_len;

	if (sge_len > first_prp_len) {
		sge_addr += first_prp_len;
		sge_len -= first_prp_len;
	} else if (sge_len == first_prp_len) {
		sge_addr = segs[i].ds_addr;
		sge_len = segs[i].ds_len;
		i++;
	}

	for (;;) {

		offset = (u_int32_t) (sge_addr & page_mask);

		/* Put PRP pointer due to page boundary*/
		page_mask_result = (uintptr_t)(ptr_sgl + 1) & page_mask;
		if (!page_mask_result) {
			device_printf(sc->mrsas_dev, "BRCM: Put prp pointer as we are at page boundary"
					" ptr_sgl: 0x%p\n", ptr_sgl);
			ptr_sgl_phys++;
			*ptr_sgl = (uintptr_t)ptr_sgl_phys;
			ptr_sgl++;
			num_prp_in_chain++;
		}

		*ptr_sgl = sge_addr;
		ptr_sgl++;
		ptr_sgl_phys++;
		num_prp_in_chain++;


		sge_addr += mr_nvme_pg_size;
		sge_len -= mr_nvme_pg_size;
		data_len -= mr_nvme_pg_size;

		if (data_len <= 0)
			break;

		if (sge_len > 0)
			continue;

		sge_addr = segs[i].ds_addr;
		sge_len = segs[i].ds_len;
		i++;
	}

	main_chain_element->Length = num_prp_in_chain * sizeof(u_int64_t);
	mrsas_atomic_inc(&sc->prp_count);

}

/*
 * mrsas_data_load_cb:	Callback entry point to build SGLs
 * input:				Pointer to command packet as argument
 *						Pointer to segment
 *						Number of segments Error
 *
 * This is the callback function of the bus dma map load.  It builds SG list
 */
static void
mrsas_data_load_cb(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	struct mrsas_mpt_cmd *cmd = (struct mrsas_mpt_cmd *)arg;
	struct mrsas_softc *sc = cmd->sc;
	boolean_t build_prp = false;

	if (error) {
		cmd->error_code = error;
		device_printf(sc->mrsas_dev, "mrsas_data_load_cb_prp: error=%d\n", error);
		if (error == EFBIG) {
			cmd->ccb_ptr->ccb_h.status = CAM_REQ_TOO_BIG;
			return;
		}
	}
	if (cmd->flags & MRSAS_DIR_IN)
		bus_dmamap_sync(cmd->sc->data_tag, cmd->data_dmamap,
		    BUS_DMASYNC_PREREAD);
	if (cmd->flags & MRSAS_DIR_OUT)
		bus_dmamap_sync(cmd->sc->data_tag, cmd->data_dmamap,
		    BUS_DMASYNC_PREWRITE);
	if (nseg > sc->max_num_sge) {
		device_printf(sc->mrsas_dev, "SGE count is too large or 0.\n");
		return;
	}

	/* Check for whether PRPs should be built or IEEE SGLs*/
	if ((cmd->io_request->IoFlags & MPI25_SAS_DEVICE0_FLAGS_ENABLED_FAST_PATH) &&
			(cmd->pdInterface == NVME_PD))
		build_prp = mrsas_is_prp_possible(cmd, segs, nseg);

	if (build_prp == true)
		mrsas_build_prp_nvme(cmd, segs, nseg);
	else
		mrsas_build_ieee_sgl(cmd, segs, nseg);

	cmd->sge_count = nseg;
}

/*
 * mrsas_freeze_simq:	Freeze SIM queue
 * input:				Pointer to command packet
 * 						Pointer to SIM
 *
 * This function freezes the sim queue.
 */
static void
mrsas_freeze_simq(struct mrsas_mpt_cmd *cmd, struct cam_sim *sim)
{
	union ccb *ccb = (union ccb *)(cmd->ccb_ptr);

	xpt_freeze_simq(sim, 1);
	ccb->ccb_h.status |= CAM_RELEASE_SIMQ;
	ccb->ccb_h.status |= CAM_REQUEUE_REQ;
}

void
mrsas_xpt_freeze(struct mrsas_softc *sc)
{
	xpt_freeze_simq(sc->sim_0, 1);
	xpt_freeze_simq(sc->sim_1, 1);
}

void
mrsas_xpt_release(struct mrsas_softc *sc)
{
	xpt_release_simq(sc->sim_0, 1);
	xpt_release_simq(sc->sim_1, 1);
}

/*
 * mrsas_cmd_done:	Perform remaining command completion
 * input:			Adapter instance soft state  Pointer to command packet
 *
 * This function calls ummap request and releases the MPT command.
 */
void
mrsas_cmd_done(struct mrsas_softc *sc, struct mrsas_mpt_cmd *cmd)
{
	mrsas_unmap_request(sc, cmd);
	
	mtx_lock(&sc->sim_lock);
	if (cmd->callout_owner) {
		callout_stop(&cmd->cm_callout);
		cmd->callout_owner  = false;
	}
	xpt_done(cmd->ccb_ptr);
	cmd->ccb_ptr = NULL;
	mtx_unlock(&sc->sim_lock);
	mrsas_release_mpt_cmd(cmd);
}

/*
 * mrsas_cam_poll:	Polling entry point
 * input:			Pointer to SIM
 *
 * This is currently a stub function.
 */
static void
mrsas_cam_poll(struct cam_sim *sim)
{
	int i;
	struct mrsas_softc *sc = (struct mrsas_softc *)cam_sim_softc(sim);

	if (sc->msix_vectors != 0){
		for (i=0; i<sc->msix_vectors; i++){
			mrsas_complete_cmd(sc, i);
		}
	} else {
		mrsas_complete_cmd(sc, 0);
	}
}

/*
 * mrsas_bus_scan:	Perform bus scan
 * input:			Adapter instance soft state
 *
 * This mrsas_bus_scan function is needed for FreeBSD 7.x.  Also, it should not
 * be called in FreeBSD 8.x and later versions, where the bus scan is
 * automatic.
 */
int
mrsas_bus_scan(struct mrsas_softc *sc)
{
	union ccb *ccb_0;
	union ccb *ccb_1;

	if ((ccb_0 = xpt_alloc_ccb()) == NULL) {
		return (ENOMEM);
	}
	if ((ccb_1 = xpt_alloc_ccb()) == NULL) {
		xpt_free_ccb(ccb_0);
		return (ENOMEM);
	}
	mtx_lock(&sc->sim_lock);
	if (xpt_create_path(&ccb_0->ccb_h.path, xpt_periph, cam_sim_path(sc->sim_0),
	    CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
		xpt_free_ccb(ccb_0);
		xpt_free_ccb(ccb_1);
		mtx_unlock(&sc->sim_lock);
		return (EIO);
	}
	if (xpt_create_path(&ccb_1->ccb_h.path, xpt_periph, cam_sim_path(sc->sim_1),
	    CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
		xpt_free_ccb(ccb_0);
		xpt_free_ccb(ccb_1);
		mtx_unlock(&sc->sim_lock);
		return (EIO);
	}
	mtx_unlock(&sc->sim_lock);
	xpt_rescan(ccb_0);
	xpt_rescan(ccb_1);

	return (0);
}

/*
 * mrsas_bus_scan_sim:	Perform bus scan per SIM
 * input:				adapter instance soft state
 *
 * This function will be called from Event handler on LD creation/deletion,
 * JBOD on/off.
 */
int
mrsas_bus_scan_sim(struct mrsas_softc *sc, struct cam_sim *sim)
{
	union ccb *ccb;

	if ((ccb = xpt_alloc_ccb()) == NULL) {
		return (ENOMEM);
	}
	mtx_lock(&sc->sim_lock);
	if (xpt_create_path(&ccb->ccb_h.path, xpt_periph, cam_sim_path(sim),
	    CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
		xpt_free_ccb(ccb);
		mtx_unlock(&sc->sim_lock);
		return (EIO);
	}
	mtx_unlock(&sc->sim_lock);
	xpt_rescan(ccb);

	return (0);
}

/*
 * mrsas_track_scsiio:  Track IOs for a given target in the mpt_cmd_list
 * input:           Adapter instance soft state
 *                  Target ID of target
 *                  Bus ID of the target
 *
 * This function checks for any pending IO in the whole mpt_cmd_list pool
 * with the bus_id and target_id passed in arguments. If some IO is found
 * that means target reset is not successfully completed.
 *
 * Returns FAIL if IOs pending to the target device, else return SUCCESS
 */
static int
mrsas_track_scsiio(struct mrsas_softc *sc, target_id_t tgt_id, u_int32_t bus_id)
{
	int i;
	struct mrsas_mpt_cmd *mpt_cmd = NULL;

	for (i = 0 ; i < sc->max_fw_cmds; i++) {
		mpt_cmd = sc->mpt_cmd_list[i];

	/*
	 * Check if the target_id and bus_id is same as the timeout IO
	 */
	if (mpt_cmd->ccb_ptr) {
		/* bus_id = 1 denotes a VD */
		if (bus_id == 1)
			tgt_id = (mpt_cmd->ccb_ptr->ccb_h.target_id - (MRSAS_MAX_PD - 1));

			if (mpt_cmd->ccb_ptr->cpi.bus_id == bus_id &&
			    mpt_cmd->ccb_ptr->ccb_h.target_id == tgt_id) {
				device_printf(sc->mrsas_dev,
				    "IO commands pending to target id %d\n", tgt_id);
				return FAIL;
			}
		}
	}

	return SUCCESS;
}

#if TM_DEBUG
/*
 * mrsas_tm_response_code: Prints TM response code received from FW
 * input:           Adapter instance soft state
 *                  MPI reply returned from firmware
 *
 * Returns nothing.
 */
static void
mrsas_tm_response_code(struct mrsas_softc *sc,
	MPI2_SCSI_TASK_MANAGE_REPLY *mpi_reply)
{
	char *desc;

	switch (mpi_reply->ResponseCode) {
	case MPI2_SCSITASKMGMT_RSP_TM_COMPLETE:
		desc = "task management request completed";
		break;
	case MPI2_SCSITASKMGMT_RSP_INVALID_FRAME:
		desc = "invalid frame";
		break;
	case MPI2_SCSITASKMGMT_RSP_TM_NOT_SUPPORTED:
		desc = "task management request not supported";
		break;
	case MPI2_SCSITASKMGMT_RSP_TM_FAILED:
		desc = "task management request failed";
		break;
	case MPI2_SCSITASKMGMT_RSP_TM_SUCCEEDED:
		desc = "task management request succeeded";
		break;
	case MPI2_SCSITASKMGMT_RSP_TM_INVALID_LUN:
		desc = "invalid lun";
		break;
	case 0xA:
		desc = "overlapped tag attempted";
		break;
	case MPI2_SCSITASKMGMT_RSP_IO_QUEUED_ON_IOC:
		desc = "task queued, however not sent to target";
		break;
	default:
		desc = "unknown";
		break;
	}
	device_printf(sc->mrsas_dev, "response_code(%01x): %s\n",
	    mpi_reply->ResponseCode, desc);
	device_printf(sc->mrsas_dev,
	    "TerminationCount/DevHandle/Function/TaskType/IOCStat/IOCLoginfo\n"
	    "0x%x/0x%x/0x%x/0x%x/0x%x/0x%x\n",
	    mpi_reply->TerminationCount, mpi_reply->DevHandle,
	    mpi_reply->Function, mpi_reply->TaskType,
	    mpi_reply->IOCStatus, mpi_reply->IOCLogInfo);
}
#endif

/*
 * mrsas_issue_tm:  Fires the TM command to FW and waits for completion
 * input:           Adapter instance soft state
 *                  reqest descriptor compiled by mrsas_reset_targets
 *
 * Returns FAIL if TM command TIMEDOUT from FW else SUCCESS.
 */
static int
mrsas_issue_tm(struct mrsas_softc *sc,
	MRSAS_REQUEST_DESCRIPTOR_UNION *req_desc)
{
	int sleep_stat;

	mrsas_fire_cmd(sc, req_desc->addr.u.low, req_desc->addr.u.high);
	sleep_stat = msleep(&sc->ocr_chan, &sc->sim_lock, PRIBIO, "tm_sleep", 50*hz);

	if (sleep_stat == EWOULDBLOCK) {
		device_printf(sc->mrsas_dev, "tm cmd TIMEDOUT\n");
		return FAIL;
	}

	return SUCCESS;
}

/*
 * mrsas_reset_targets : Gathers info to fire a target reset command
 * input:           Adapter instance soft state
 *
 * This function compiles data for a target reset command to be fired to the FW
 * and then traverse the target_reset_pool to see targets with TIMEDOUT IOs.
 *
 * Returns SUCCESS or FAIL
 */
int mrsas_reset_targets(struct mrsas_softc *sc)
{
	struct mrsas_mpt_cmd *tm_mpt_cmd = NULL;
	struct mrsas_mpt_cmd *tgt_mpt_cmd = NULL;
	MR_TASK_MANAGE_REQUEST *mr_request;
	MPI2_SCSI_TASK_MANAGE_REQUEST *tm_mpi_request;
	MRSAS_REQUEST_DESCRIPTOR_UNION *req_desc;
	int retCode = FAIL, count, i, outstanding;
	u_int32_t MSIxIndex, bus_id;
	target_id_t tgt_id;
#if TM_DEBUG
	MPI2_SCSI_TASK_MANAGE_REPLY *mpi_reply;
#endif

	outstanding = mrsas_atomic_read(&sc->fw_outstanding);

	if (!outstanding) {
		device_printf(sc->mrsas_dev, "NO IOs pending...\n");
		mrsas_atomic_set(&sc->target_reset_outstanding, 0);
		retCode = SUCCESS;
		goto return_status;
	} else if (sc->adprecovery != MRSAS_HBA_OPERATIONAL) {
		device_printf(sc->mrsas_dev, "Controller is not operational\n");
		goto return_status;
	} else {
		/* Some more error checks will be added in future */
	}

	/* Get an mpt frame and an index to fire the TM cmd */
	tm_mpt_cmd = mrsas_get_mpt_cmd(sc);
	if (!tm_mpt_cmd) {
		retCode = FAIL;
		goto return_status;
	}

	req_desc = mrsas_get_request_desc(sc, (tm_mpt_cmd->index) - 1);
	if (!req_desc) {
		device_printf(sc->mrsas_dev, "Cannot get request_descriptor for tm.\n");
		retCode = FAIL;
		goto release_mpt;
	}
	memset(req_desc, 0, sizeof(MRSAS_REQUEST_DESCRIPTOR_UNION));

	req_desc->HighPriority.SMID = tm_mpt_cmd->index;
	req_desc->HighPriority.RequestFlags =
	    (MPI2_REQ_DESCRIPT_FLAGS_HIGH_PRIORITY <<
	    MRSAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT);
	req_desc->HighPriority.MSIxIndex =  0;
	req_desc->HighPriority.LMID = 0;
	req_desc->HighPriority.Reserved1 = 0;
	tm_mpt_cmd->request_desc = req_desc;

	mr_request = (MR_TASK_MANAGE_REQUEST *) tm_mpt_cmd->io_request;
	memset(mr_request, 0, sizeof(MR_TASK_MANAGE_REQUEST));

	tm_mpi_request = (MPI2_SCSI_TASK_MANAGE_REQUEST *) &mr_request->TmRequest;
	tm_mpi_request->Function = MPI2_FUNCTION_SCSI_TASK_MGMT;
	tm_mpi_request->TaskType = MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET;
	tm_mpi_request->TaskMID = 0; /* smid task */
	tm_mpi_request->LUN[1] = 0;

	/* Traverse the tm_mpt pool to get valid entries */
	for (i = 0 ; i < MRSAS_MAX_TM_TARGETS; i++) {
		if(!sc->target_reset_pool[i]) {
			continue;
		} else {
			tgt_mpt_cmd = sc->target_reset_pool[i];
		}

		tgt_id = i;

		/* See if the target is tm capable or NOT */
		if (!tgt_mpt_cmd->tmCapable) {
			device_printf(sc->mrsas_dev, "Task management NOT SUPPORTED for "
			    "CAM target:%d\n", tgt_id);

			retCode = FAIL;
			goto release_mpt;
		}

		tm_mpi_request->DevHandle = tgt_mpt_cmd->io_request->DevHandle;

		if (i < (MRSAS_MAX_PD - 1)) {
			mr_request->uTmReqReply.tmReqFlags.isTMForPD = 1;
			bus_id = 0;
		} else {
			mr_request->uTmReqReply.tmReqFlags.isTMForLD = 1;
			bus_id = 1;
		}

		device_printf(sc->mrsas_dev, "TM will be fired for "
		    "CAM target:%d and bus_id %d\n", tgt_id, bus_id);

		sc->ocr_chan = (void *)&tm_mpt_cmd;
		retCode = mrsas_issue_tm(sc, req_desc);
		if (retCode == FAIL)
			goto release_mpt;

#if TM_DEBUG
		mpi_reply =
		    (MPI2_SCSI_TASK_MANAGE_REPLY *) &mr_request->uTmReqReply.TMReply;
		mrsas_tm_response_code(sc, mpi_reply);
#endif
		mrsas_atomic_dec(&sc->target_reset_outstanding);
		sc->target_reset_pool[i] = NULL;

		/* Check for pending cmds in the mpt_cmd_pool with the tgt_id */
		mrsas_disable_intr(sc);
		/* Wait for 1 second to complete parallel ISR calling same
		 * mrsas_complete_cmd()
		 */
		msleep(&sc->ocr_chan, &sc->sim_lock, PRIBIO, "mrsas_reset_wakeup",
		   1 * hz);
		count = sc->msix_vectors > 0 ? sc->msix_vectors : 1;
		mtx_unlock(&sc->sim_lock);
		for (MSIxIndex = 0; MSIxIndex < count; MSIxIndex++)
		    mrsas_complete_cmd(sc, MSIxIndex);
		mtx_lock(&sc->sim_lock);
		retCode = mrsas_track_scsiio(sc, tgt_id, bus_id);
		mrsas_enable_intr(sc);

		if (retCode == FAIL)
			goto release_mpt;
	}

	device_printf(sc->mrsas_dev, "Number of targets outstanding "
	    "after reset: %d\n", mrsas_atomic_read(&sc->target_reset_outstanding));

release_mpt:
	mrsas_release_mpt_cmd(tm_mpt_cmd);
return_status:
	device_printf(sc->mrsas_dev, "target reset %s!!\n",
		(retCode == SUCCESS) ? "SUCCESS" : "FAIL");

	return retCode;
}

