/*
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 LSI Corp. 
 * All rights reserved.
 * Author : Manjunath Ranganathaiah <manjunath.ranganathaiah@lsi.com>
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
 *
 * $FreeBSD$
 */

#include <dev/tws/tws.h>
#include <dev/tws/tws_services.h>
#include <dev/tws/tws_hdm.h>
#include <dev/tws/tws_user.h>
#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>
#include <cam/cam_debug.h>
#include <cam/cam_periph.h>

#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>

static int tws_cam_depth=(TWS_MAX_REQS - TWS_RESERVED_REQS);
static char tws_sev_str[5][8]={"","ERROR","WARNING","INFO","DEBUG"};

static void  tws_action(struct cam_sim *sim, union ccb *ccb);
static void  tws_poll(struct cam_sim *sim);
static void tws_scsi_complete(struct tws_request *req);



void tws_unmap_request(struct tws_softc *sc, struct tws_request *req);
int32_t tws_map_request(struct tws_softc *sc, struct tws_request *req);
int tws_bus_scan(struct tws_softc *sc);
int tws_cam_attach(struct tws_softc *sc);
void tws_cam_detach(struct tws_softc *sc);
void tws_reset(void *arg);

static void tws_reset_cb(void *arg);
static void tws_reinit(void *arg);
static int32_t tws_execute_scsi(struct tws_softc *sc, union ccb *ccb);
static void tws_freeze_simq(struct tws_softc *sc, struct tws_request *req);
static void tws_dmamap_data_load_cbfn(void *arg, bus_dma_segment_t *segs,
                            int nseg, int error);
static void tws_fill_sg_list(struct tws_softc *sc, void *sgl_src, 
                            void *sgl_dest, u_int16_t num_sgl_entries);
static void tws_err_complete(struct tws_softc *sc, u_int64_t mfa);
static void tws_scsi_err_complete(struct tws_request *req, 
                                               struct tws_command_header *hdr);
static void tws_passthru_err_complete(struct tws_request *req, 
                                               struct tws_command_header *hdr);


void tws_timeout(void *arg);
static void tws_intr_attn_aen(struct tws_softc *sc);
static void tws_intr_attn_error(struct tws_softc *sc);
static void tws_intr_resp(struct tws_softc *sc);
void tws_intr(void *arg);
void tws_cmd_complete(struct tws_request *req);
void tws_aen_complete(struct tws_request *req);
int tws_send_scsi_cmd(struct tws_softc *sc, int cmd);
void tws_getset_param_complete(struct tws_request *req);
int tws_set_param(struct tws_softc *sc, u_int32_t table_id, u_int32_t param_id,
              u_int32_t param_size, void *data);
int tws_get_param(struct tws_softc *sc, u_int32_t table_id, u_int32_t param_id,  
              u_int32_t param_size, void *data);


extern struct tws_request *tws_get_request(struct tws_softc *sc, 
                                            u_int16_t type);
extern void *tws_release_request(struct tws_request *req);
extern int tws_submit_command(struct tws_softc *sc, struct tws_request *req);
extern boolean tws_get_response(struct tws_softc *sc, 
                                           u_int16_t *req_id, u_int64_t *mfa);
extern void tws_q_insert_tail(struct tws_softc *sc, struct tws_request *req,
                                u_int8_t q_type );
extern struct tws_request * tws_q_remove_request(struct tws_softc *sc,
                                   struct tws_request *req, u_int8_t q_type );
extern void tws_send_event(struct tws_softc *sc, u_int8_t event);

extern struct tws_sense *
tws_find_sense_from_mfa(struct tws_softc *sc, u_int64_t mfa);

extern void tws_fetch_aen(void *arg);
extern void tws_disable_db_intr(struct tws_softc *sc);
extern void tws_enable_db_intr(struct tws_softc *sc);
extern void tws_passthru_complete(struct tws_request *req);
extern void tws_aen_synctime_with_host(struct tws_softc *sc);
extern void tws_circular_aenq_insert(struct tws_softc *sc, 
                    struct tws_circular_q *cq, struct tws_event_packet *aen);
extern int tws_use_32bit_sgls;
extern boolean tws_ctlr_reset(struct tws_softc *sc);
extern struct tws_request * tws_q_remove_tail(struct tws_softc *sc, 
                                                           u_int8_t q_type );
extern void tws_turn_off_interrupts(struct tws_softc *sc);
extern void tws_turn_on_interrupts(struct tws_softc *sc);
extern int tws_init_connect(struct tws_softc *sc, u_int16_t mc);
extern void tws_init_obfl_q(struct tws_softc *sc);
extern uint8_t tws_get_state(struct tws_softc *sc);
extern void tws_assert_soft_reset(struct tws_softc *sc);
extern boolean tws_ctlr_ready(struct tws_softc *sc);
extern u_int16_t tws_poll4_response(struct tws_softc *sc, u_int64_t *mfa);
extern int tws_setup_intr(struct tws_softc *sc, int irqs);
extern int tws_teardown_intr(struct tws_softc *sc);



int
tws_cam_attach(struct tws_softc *sc)
{
    struct cam_devq *devq;

    TWS_TRACE_DEBUG(sc, "entry", 0, sc);
    /* Create a device queue for sim */

    /* 
     * if the user sets cam depth to less than 1 
     * cam may get confused 
     */
    if ( tws_cam_depth < 1 )
        tws_cam_depth = 1;
    if ( tws_cam_depth > (tws_queue_depth - TWS_RESERVED_REQS)  )
        tws_cam_depth = tws_queue_depth - TWS_RESERVED_REQS;

    TWS_TRACE_DEBUG(sc, "depths,ctlr,cam", tws_queue_depth, tws_cam_depth);

    if ((devq = cam_simq_alloc(tws_cam_depth)) == NULL) {
        tws_log(sc, CAM_SIMQ_ALLOC);
        return(ENOMEM);
    }

   /*
    * Create a SIM entry.  Though we can support tws_cam_depth
    * simultaneous requests, we claim to be able to handle only
    * (tws_cam_depth), so that we always have reserved  requests
    * packet available to service ioctls and internal commands.
    */
    sc->sim = cam_sim_alloc(tws_action, tws_poll, "tws", sc,
                      device_get_unit(sc->tws_dev), 
                      &sc->sim_lock,
                      tws_cam_depth, 1, devq);
                      /* 1, 1, devq); */
    if (sc->sim == NULL) {
        cam_simq_free(devq);
        tws_log(sc, CAM_SIM_ALLOC);
    }
    /* Register the bus. */
    mtx_lock(&sc->sim_lock);
    if (xpt_bus_register(sc->sim, 
                         sc->tws_dev, 
                         0) != CAM_SUCCESS) {
        cam_sim_free(sc->sim, TRUE); /* passing true will free the devq */
        sc->sim = NULL; /* so cam_detach will not try to free it */
        mtx_unlock(&sc->sim_lock);
        tws_log(sc, TWS_XPT_BUS_REGISTER);
        return(ENXIO);
    }
    if (xpt_create_path(&sc->path, NULL, cam_sim_path(sc->sim),
                         CAM_TARGET_WILDCARD,
                         CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
        xpt_bus_deregister(cam_sim_path(sc->sim));
        /* Passing TRUE to cam_sim_free will free the devq as well. */
        cam_sim_free(sc->sim, TRUE);
        tws_log(sc, TWS_XPT_CREATE_PATH);
        mtx_unlock(&sc->sim_lock);
        return(ENXIO);
    }
    mtx_unlock(&sc->sim_lock);

    return(0);
}

void
tws_cam_detach(struct tws_softc *sc)
{
    TWS_TRACE_DEBUG(sc, "entry", 0, 0);
    mtx_lock(&sc->sim_lock);
    if (sc->path)
        xpt_free_path(sc->path);
    if (sc->sim) {
        xpt_bus_deregister(cam_sim_path(sc->sim));
        cam_sim_free(sc->sim, TRUE);
    }
    mtx_unlock(&sc->sim_lock);
}

int
tws_bus_scan(struct tws_softc *sc)
{
    union ccb       *ccb;

    TWS_TRACE_DEBUG(sc, "entry", sc, 0);
    if (!(sc->sim))
        return(ENXIO);
    ccb = xpt_alloc_ccb();
    mtx_lock(&sc->sim_lock);
    if (xpt_create_path(&ccb->ccb_h.path, NULL, cam_sim_path(sc->sim),
                  CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
	mtx_unlock(&sc->sim_lock);
        xpt_free_ccb(ccb);
        return(EIO);
    }
    xpt_rescan(ccb);
    mtx_unlock(&sc->sim_lock);
    return(0);
}

static void
tws_action(struct cam_sim *sim, union ccb *ccb)
{
    struct tws_softc *sc = (struct tws_softc *)cam_sim_softc(sim);


    switch( ccb->ccb_h.func_code ) {
        case XPT_SCSI_IO:   
        {
            if ( tws_execute_scsi(sc, ccb) ) 
                TWS_TRACE_DEBUG(sc, "execute scsi failed", 0, 0);
            break;
        }
        case XPT_ABORT:
        {
            TWS_TRACE_DEBUG(sc, "abort i/o", 0, 0);
            ccb->ccb_h.status = CAM_UA_ABORT;
            xpt_done(ccb);
            break;
        }
        case XPT_RESET_BUS:
        {
            TWS_TRACE_DEBUG(sc, "reset bus", sim, ccb);
            break;
        }
        case XPT_SET_TRAN_SETTINGS:
        {
            TWS_TRACE_DEBUG(sc, "set tran settings", sim, ccb);
            ccb->ccb_h.status = CAM_FUNC_NOTAVAIL;
            xpt_done(ccb);

            break;
        }
        case XPT_GET_TRAN_SETTINGS:
        {
            TWS_TRACE_DEBUG(sc, "get tran settings", sim, ccb);

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
        case XPT_CALC_GEOMETRY:
        {
            TWS_TRACE_DEBUG(sc, "calc geometry(ccb,block-size)", ccb, 
                                          ccb->ccg.block_size);
            cam_calc_geometry(&ccb->ccg, 1/* extended */);
            xpt_done(ccb);

            break;
        }
        case XPT_PATH_INQ:
        {
            TWS_TRACE_DEBUG(sc, "path inquiry", sim, ccb);
            ccb->cpi.version_num = 1;
            ccb->cpi.hba_inquiry = 0;
            ccb->cpi.target_sprt = 0;
            ccb->cpi.hba_misc = 0;
            ccb->cpi.hba_eng_cnt = 0;
            ccb->cpi.max_target = TWS_MAX_NUM_UNITS;
            ccb->cpi.max_lun = TWS_MAX_NUM_LUNS - 1;
            ccb->cpi.unit_number = cam_sim_unit(sim);
            ccb->cpi.bus_id = cam_sim_bus(sim);
            ccb->cpi.initiator_id = TWS_SCSI_INITIATOR_ID;
            ccb->cpi.base_transfer_speed = 6000000;
            strlcpy(ccb->cpi.sim_vid, "FreeBSD", SIM_IDLEN);
            strlcpy(ccb->cpi.hba_vid, "3ware", HBA_IDLEN);
            strlcpy(ccb->cpi.dev_name, cam_sim_name(sim), DEV_IDLEN);
            ccb->cpi.transport = XPORT_SPI;
            ccb->cpi.transport_version = 2;
            ccb->cpi.protocol = PROTO_SCSI;
            ccb->cpi.protocol_version = SCSI_REV_2;
            ccb->cpi.maxio = TWS_MAX_IO_SIZE;
            ccb->ccb_h.status = CAM_REQ_CMP;
            xpt_done(ccb);

            break;
        }
        default:
            TWS_TRACE_DEBUG(sc, "default", sim, ccb);
            ccb->ccb_h.status = CAM_REQ_INVALID;
            xpt_done(ccb);
            break;
    }
}

static void
tws_scsi_complete(struct tws_request *req)
{
    struct tws_softc *sc = req->sc;

    mtx_lock(&sc->q_lock);
    tws_q_remove_request(sc, req, TWS_BUSY_Q);
    mtx_unlock(&sc->q_lock);

    callout_stop(&req->timeout);
    tws_unmap_request(req->sc, req);


    req->ccb_ptr->ccb_h.status = CAM_REQ_CMP;
    mtx_lock(&sc->sim_lock);
    xpt_done(req->ccb_ptr);
    mtx_unlock(&sc->sim_lock);

    mtx_lock(&sc->q_lock);
    tws_q_insert_tail(sc, req, TWS_FREE_Q);
    mtx_unlock(&sc->q_lock);
}

void
tws_getset_param_complete(struct tws_request *req)
{
    struct tws_softc *sc = req->sc;

    TWS_TRACE_DEBUG(sc, "getset complete", req, req->request_id);

    callout_stop(&req->timeout);
    tws_unmap_request(sc, req);

    free(req->data, M_TWS);

    req->state = TWS_REQ_STATE_FREE;
}

void
tws_aen_complete(struct tws_request *req)
{
    struct tws_softc *sc = req->sc;
    struct tws_command_header *sense;
    struct tws_event_packet event;
    u_int16_t aen_code=0;

    TWS_TRACE_DEBUG(sc, "aen complete", 0, req->request_id);

    callout_stop(&req->timeout);
    tws_unmap_request(sc, req);

    sense = (struct tws_command_header *)req->data;

    TWS_TRACE_DEBUG(sc,"sense code, key",sense->sense_data[0], 
                                   sense->sense_data[2]);
    TWS_TRACE_DEBUG(sc,"sense rid, seve",sense->header_desc.request_id, 
                                   sense->status_block.res__severity);
    TWS_TRACE_DEBUG(sc,"sense srcnum, error",sense->status_block.srcnum, 
                                   sense->status_block.error);
    TWS_TRACE_DEBUG(sc,"sense shdr, ssense",sense->header_desc.size_header, 
                                   sense->header_desc.size_sense);

    aen_code = sense->status_block.error;

    switch ( aen_code ) {
        case TWS_AEN_SYNC_TIME_WITH_HOST :
            tws_aen_synctime_with_host(sc);
            break;
        case TWS_AEN_QUEUE_EMPTY :
            break;
        default :
            bzero(&event, sizeof(struct tws_event_packet));
            event.sequence_id = sc->seq_id;
            event.time_stamp_sec = (u_int32_t)TWS_LOCAL_TIME;
            event.aen_code = sense->status_block.error;
            event.severity = sense->status_block.res__severity & 0x7;
            event.event_src = TWS_SRC_CTRL_EVENT;
            strcpy(event.severity_str, tws_sev_str[event.severity]);
            event.retrieved = TWS_AEN_NOT_RETRIEVED;

            bcopy(sense->err_specific_desc, event.parameter_data, 
                                    TWS_ERROR_SPECIFIC_DESC_LEN);
            event.parameter_data[TWS_ERROR_SPECIFIC_DESC_LEN - 1] = '\0';
            event.parameter_len = (u_int8_t)strlen(event.parameter_data)+1;

            if ( event.parameter_len < TWS_ERROR_SPECIFIC_DESC_LEN ) {
                event.parameter_len += ((u_int8_t)strlen(event.parameter_data +
                                                event.parameter_len) + 1);
            }

            device_printf(sc->tws_dev, "%s: (0x%02X: 0x%04X): %s: %s\n",
                event.severity_str,
                event.event_src,
                event.aen_code,
                event.parameter_data + 
                     (strlen(event.parameter_data) + 1), 
                event.parameter_data);

            mtx_lock(&sc->gen_lock);
            tws_circular_aenq_insert(sc, &sc->aen_q, &event);
            sc->seq_id++;
            mtx_unlock(&sc->gen_lock);
            break;

    }
    
    free(req->data, M_TWS);

    req->state = TWS_REQ_STATE_FREE;

    if ( aen_code != TWS_AEN_QUEUE_EMPTY ) {
        /* timeout(tws_fetch_aen, sc, 1);*/
        sc->stats.num_aens++;
        tws_fetch_aen((void *)sc);
    } 
}

void
tws_cmd_complete(struct tws_request *req)
{
    struct tws_softc *sc = req->sc;

    callout_stop(&req->timeout);
    tws_unmap_request(sc, req);
}
                                   
static void
tws_err_complete(struct tws_softc *sc, u_int64_t mfa)
{
    struct tws_command_header *hdr;
    struct tws_sense *sen;
    struct tws_request *req;
    u_int16_t req_id;
    u_int32_t reg, status;

    if ( !mfa ) {
        TWS_TRACE_DEBUG(sc, "null mfa", 0, mfa);
        return;
    } else {
        /* lookup the sense */
        sen = tws_find_sense_from_mfa(sc, mfa);
        if ( sen == NULL ) {
            TWS_TRACE_DEBUG(sc, "found null req", 0, mfa);
            return;
        }
        hdr = sen->hdr;
        TWS_TRACE_DEBUG(sc, "sen, hdr", sen, hdr);
        req_id = hdr->header_desc.request_id;
        req = &sc->reqs[req_id];
        TWS_TRACE_DEBUG(sc, "req, id", req, req_id);
        if ( req->error_code != TWS_REQ_RET_SUBMIT_SUCCESS )
            TWS_TRACE_DEBUG(sc, "submit failure?", 0, req->error_code);
    }

    switch (req->type) {
        case TWS_REQ_TYPE_PASSTHRU :
            tws_passthru_err_complete(req, hdr);
            break;
        case TWS_REQ_TYPE_GETSET_PARAM :
            tws_getset_param_complete(req);
            break;
        case TWS_REQ_TYPE_SCSI_IO :
            tws_scsi_err_complete(req, hdr);
            break;
            
    }

    mtx_lock(&sc->io_lock);
    hdr->header_desc.size_header = 128;
    reg = (u_int32_t)( mfa>>32);
    tws_write_reg(sc, TWS_I2O0_HOBQPH, reg, 4);
    reg = (u_int32_t)(mfa);
    tws_write_reg(sc, TWS_I2O0_HOBQPL, reg, 4);

    status = tws_read_reg(sc, TWS_I2O0_STATUS, 4);
    if ( status & TWS_BIT13 ) {
        device_printf(sc->tws_dev,  "OBFL Overrun\n");
        sc->obfl_q_overrun = true;
    }
    mtx_unlock(&sc->io_lock);
}

static void
tws_scsi_err_complete(struct tws_request *req, struct tws_command_header *hdr)
{ 
    u_int8_t *sense_data;
    struct tws_softc *sc = req->sc;
    union ccb *ccb = req->ccb_ptr;

    TWS_TRACE_DEBUG(sc, "sbe, cmd_status", hdr->status_block.error, 
                                 req->cmd_pkt->cmd.pkt_a.status);
    if ( hdr->status_block.error == TWS_ERROR_LOGICAL_UNIT_NOT_SUPPORTED ||
         hdr->status_block.error == TWS_ERROR_UNIT_OFFLINE ) {

        if ( ccb->ccb_h.target_lun ) {
            TWS_TRACE_DEBUG(sc, "invalid lun error",0,0);
            ccb->ccb_h.status |= CAM_DEV_NOT_THERE;
        } else {
            TWS_TRACE_DEBUG(sc, "invalid target error",0,0);
            ccb->ccb_h.status |= CAM_SEL_TIMEOUT;
        }

    } else {
        TWS_TRACE_DEBUG(sc, "scsi status  error",0,0);
        ccb->ccb_h.status |= CAM_SCSI_STATUS_ERROR;
        if (((ccb->csio.cdb_io.cdb_bytes[0] == 0x1A) && 
              (hdr->status_block.error == TWS_ERROR_NOT_SUPPORTED))) {
            ccb->ccb_h.status |= CAM_SCSI_STATUS_ERROR | CAM_AUTOSNS_VALID;
            TWS_TRACE_DEBUG(sc, "page mode not supported",0,0);
        }
    }

    /* if there were no error simply mark complete error */ 
    if (ccb->ccb_h.status == 0)
        ccb->ccb_h.status = CAM_REQ_CMP_ERR;

    sense_data = (u_int8_t *)&ccb->csio.sense_data;
    if (sense_data) {
        memcpy(sense_data, hdr->sense_data, TWS_SENSE_DATA_LENGTH );
        ccb->csio.sense_len = TWS_SENSE_DATA_LENGTH;
        ccb->ccb_h.status |= CAM_AUTOSNS_VALID;
    }
    ccb->csio.scsi_status = req->cmd_pkt->cmd.pkt_a.status;
 
    ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
    mtx_lock(&sc->sim_lock);
    xpt_done(ccb);
    mtx_unlock(&sc->sim_lock);

    callout_stop(&req->timeout);
    tws_unmap_request(req->sc, req);
    mtx_lock(&sc->q_lock);
    tws_q_remove_request(sc, req, TWS_BUSY_Q);
    tws_q_insert_tail(sc, req, TWS_FREE_Q);
    mtx_unlock(&sc->q_lock);
}

static void
tws_passthru_err_complete(struct tws_request *req, 
                                          struct tws_command_header *hdr)
{ 
    TWS_TRACE_DEBUG(req->sc, "entry", hdr, req->request_id);
    req->error_code = hdr->status_block.error;
    memcpy(&(req->cmd_pkt->hdr), hdr, sizeof(struct tws_command_header));
    tws_passthru_complete(req);
}

static void
tws_drain_busy_queue(struct tws_softc *sc)
{
    struct tws_request *req;
    union ccb          *ccb;
    TWS_TRACE_DEBUG(sc, "entry", 0, 0);

    mtx_lock(&sc->q_lock);
    req = tws_q_remove_tail(sc, TWS_BUSY_Q);
    mtx_unlock(&sc->q_lock);
    while ( req ) {
        TWS_TRACE_DEBUG(sc, "moved to TWS_COMPLETE_Q", 0, req->request_id);
	callout_stop(&req->timeout);

        req->error_code = TWS_REQ_RET_RESET;
        ccb = (union ccb *)(req->ccb_ptr);

        ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
        ccb->ccb_h.status |=  CAM_REQUEUE_REQ;
        ccb->ccb_h.status |=  CAM_SCSI_BUS_RESET;

        tws_unmap_request(req->sc, req);

        mtx_lock(&sc->sim_lock);
        xpt_done(req->ccb_ptr);
        mtx_unlock(&sc->sim_lock);

        mtx_lock(&sc->q_lock);
        tws_q_insert_tail(sc, req, TWS_FREE_Q);
        req = tws_q_remove_tail(sc, TWS_BUSY_Q);
        mtx_unlock(&sc->q_lock);
    } 
}


static void
tws_drain_reserved_reqs(struct tws_softc *sc)
{
    struct tws_request *r;

    r = &sc->reqs[TWS_REQ_TYPE_AEN_FETCH];
    if ( r->state != TWS_REQ_STATE_FREE ) {
        TWS_TRACE_DEBUG(sc, "reset aen req", 0, 0);
	callout_stop(&r->timeout);
        tws_unmap_request(sc, r);
        free(r->data, M_TWS);
        r->state = TWS_REQ_STATE_FREE;
        r->error_code = TWS_REQ_RET_RESET;
    } 

    r = &sc->reqs[TWS_REQ_TYPE_PASSTHRU];
    if ( r->state == TWS_REQ_STATE_BUSY ) {
        TWS_TRACE_DEBUG(sc, "reset passthru req", 0, 0);
        r->error_code = TWS_REQ_RET_RESET;
    } 

    r = &sc->reqs[TWS_REQ_TYPE_GETSET_PARAM];
    if ( r->state != TWS_REQ_STATE_FREE ) {
        TWS_TRACE_DEBUG(sc, "reset setparam req", 0, 0);
	callout_stop(&r->timeout);
        tws_unmap_request(sc, r);
        free(r->data, M_TWS);
        r->state = TWS_REQ_STATE_FREE;
        r->error_code = TWS_REQ_RET_RESET;
    } 
}

static void
tws_drain_response_queue(struct tws_softc *sc)
{
    u_int16_t req_id;
    u_int64_t mfa;
    while ( tws_get_response(sc, &req_id, &mfa) );
}


static int32_t
tws_execute_scsi(struct tws_softc *sc, union ccb *ccb)
{
    struct tws_command_packet *cmd_pkt;
    struct tws_request *req;
    struct ccb_hdr *ccb_h = &(ccb->ccb_h);
    struct ccb_scsiio *csio = &(ccb->csio);
    int error;
    u_int16_t lun;

    mtx_assert(&sc->sim_lock, MA_OWNED);
    if (ccb_h->target_id >= TWS_MAX_NUM_UNITS) {
        TWS_TRACE_DEBUG(sc, "traget id too big", ccb_h->target_id, ccb_h->target_lun);
        ccb_h->status |= CAM_TID_INVALID;
        xpt_done(ccb);
        return(0);
    }
    if (ccb_h->target_lun >= TWS_MAX_NUM_LUNS) {
        TWS_TRACE_DEBUG(sc, "target lun 2 big", ccb_h->target_id, ccb_h->target_lun);
        ccb_h->status |= CAM_LUN_INVALID;
        xpt_done(ccb);
        return(0);
    }

    if(ccb_h->flags & CAM_CDB_PHYS) {
        TWS_TRACE_DEBUG(sc, "cdb phy", ccb_h->target_id, ccb_h->target_lun);
        ccb_h->status = CAM_REQ_INVALID;
        xpt_done(ccb);
        return(0);
    }

    /*
     * We are going to work on this request.  Mark it as enqueued (though
     * we don't actually queue it...)
     */
    ccb_h->status |= CAM_SIM_QUEUED;

    req = tws_get_request(sc, TWS_REQ_TYPE_SCSI_IO);
    if ( !req ) {
        TWS_TRACE_DEBUG(sc, "no reqs", ccb_h->target_id, ccb_h->target_lun);
        ccb_h->status |= CAM_REQUEUE_REQ;
        xpt_done(ccb);
        return(0);
    }

    if((ccb_h->flags & CAM_DIR_MASK) != CAM_DIR_NONE) {
        if(ccb_h->flags & CAM_DIR_IN)
            req->flags |= TWS_DIR_IN;
        if(ccb_h->flags & CAM_DIR_OUT)
            req->flags |= TWS_DIR_OUT;
    } else {
        req->flags = TWS_DIR_NONE; /* no data */
    }

    req->type = TWS_REQ_TYPE_SCSI_IO;
    req->cb = tws_scsi_complete;

    cmd_pkt = req->cmd_pkt;
    /* cmd_pkt->hdr.header_desc.size_header = 128; */
    cmd_pkt->cmd.pkt_a.res__opcode = TWS_FW_CMD_EXECUTE_SCSI;
    cmd_pkt->cmd.pkt_a.unit = ccb_h->target_id;
    cmd_pkt->cmd.pkt_a.status = 0;
    cmd_pkt->cmd.pkt_a.sgl_offset = 16;

    /* lower nibble */
    lun = ccb_h->target_lun & 0XF;
    lun = lun << 12;
    cmd_pkt->cmd.pkt_a.lun_l4__req_id = lun | req->request_id;
    /* upper nibble */
    lun = ccb_h->target_lun & 0XF0;
    lun = lun << 8;
    cmd_pkt->cmd.pkt_a.lun_h4__sgl_entries = lun;

#ifdef TWS_DEBUG 
    if ( csio->cdb_len > 16 ) 
         TWS_TRACE(sc, "cdb len too big", ccb_h->target_id, csio->cdb_len);
#endif

    if(ccb_h->flags & CAM_CDB_POINTER)
        bcopy(csio->cdb_io.cdb_ptr, cmd_pkt->cmd.pkt_a.cdb, csio->cdb_len);
    else
        bcopy(csio->cdb_io.cdb_bytes, cmd_pkt->cmd.pkt_a.cdb, csio->cdb_len);

    req->data = ccb;
    req->flags |= TWS_DATA_CCB;
    /* save ccb ptr */
    req->ccb_ptr = ccb;
    /* 
     * tws_map_load_data_callback will fill in the SGL,
     * and submit the I/O.
     */
    sc->stats.scsi_ios++;
    callout_reset_sbt(&req->timeout, SBT_1MS * ccb->ccb_h.timeout, 0,
      tws_timeout, req, 0);
    error = tws_map_request(sc, req);
    return(error);
}


int
tws_send_scsi_cmd(struct tws_softc *sc, int cmd)
{
    struct tws_request *req;
    struct tws_command_packet *cmd_pkt;
    int error;

    TWS_TRACE_DEBUG(sc, "entry",sc, cmd);
    req = tws_get_request(sc, TWS_REQ_TYPE_AEN_FETCH);

    if ( req == NULL )
        return(ENOMEM);

    req->cb = tws_aen_complete;

    cmd_pkt = req->cmd_pkt;
    cmd_pkt->cmd.pkt_a.res__opcode = TWS_FW_CMD_EXECUTE_SCSI;
    cmd_pkt->cmd.pkt_a.status = 0;
    cmd_pkt->cmd.pkt_a.unit = 0;
    cmd_pkt->cmd.pkt_a.sgl_offset = 16;
    cmd_pkt->cmd.pkt_a.lun_l4__req_id = req->request_id;

    cmd_pkt->cmd.pkt_a.cdb[0] = (u_int8_t)cmd;
    cmd_pkt->cmd.pkt_a.cdb[4] = 128;

    req->length = TWS_SECTOR_SIZE;
    req->data = malloc(TWS_SECTOR_SIZE, M_TWS, M_NOWAIT);
    if ( req->data == NULL )
        return(ENOMEM);
    bzero(req->data, TWS_SECTOR_SIZE);
    req->flags = TWS_DIR_IN;

    callout_reset(&req->timeout, (TWS_IO_TIMEOUT * hz), tws_timeout, req);
    error = tws_map_request(sc, req);
    return(error);

}

int
tws_set_param(struct tws_softc *sc, u_int32_t table_id, u_int32_t param_id,
              u_int32_t param_size, void *data)
{
    struct tws_request *req;
    struct tws_command_packet *cmd_pkt;
    union tws_command_giga *cmd;
    struct tws_getset_param *param;
    int error;

    req = tws_get_request(sc, TWS_REQ_TYPE_GETSET_PARAM);
    if ( req == NULL ) {
        TWS_TRACE_DEBUG(sc, "null req", 0, 0);
        return(ENOMEM);
    }

    req->length = TWS_SECTOR_SIZE;
    req->data = malloc(TWS_SECTOR_SIZE, M_TWS, M_NOWAIT);
    if ( req->data == NULL )
        return(ENOMEM);
    bzero(req->data, TWS_SECTOR_SIZE);
    param = (struct tws_getset_param *)req->data;

    req->cb = tws_getset_param_complete;
    req->flags = TWS_DIR_OUT;
    cmd_pkt = req->cmd_pkt;

    cmd = &cmd_pkt->cmd.pkt_g;
    cmd->param.sgl_off__opcode =
            BUILD_SGL_OFF__OPCODE(2, TWS_FW_CMD_SET_PARAM);
    cmd->param.request_id = (u_int8_t)req->request_id;
    cmd->param.host_id__unit = 0;
    cmd->param.param_count = 1;
    cmd->param.size = 2; /* map routine will add sgls */

    /* Specify which parameter we want to set. */
    param->table_id = (table_id | TWS_9K_PARAM_DESCRIPTOR);
    param->parameter_id = (u_int8_t)(param_id);
    param->parameter_size_bytes = (u_int16_t)param_size;
    memcpy(param->data, data, param_size);

    callout_reset(&req->timeout, (TWS_IOCTL_TIMEOUT * hz), tws_timeout, req);
    error = tws_map_request(sc, req);
    return(error);

}

int
tws_get_param(struct tws_softc *sc, u_int32_t table_id, u_int32_t param_id,
              u_int32_t param_size, void *data)
{
    struct tws_request *req;
    struct tws_command_packet *cmd_pkt;
    union tws_command_giga *cmd;
    struct tws_getset_param *param;
    u_int16_t reqid;
    u_int64_t mfa;
    int error = SUCCESS;


    req = tws_get_request(sc, TWS_REQ_TYPE_GETSET_PARAM);
    if ( req == NULL ) {
        TWS_TRACE_DEBUG(sc, "null req", 0, 0);
        return(FAILURE);
    }

    req->length = TWS_SECTOR_SIZE;
    req->data = malloc(TWS_SECTOR_SIZE, M_TWS, M_NOWAIT);
    if ( req->data == NULL )
        return(FAILURE);
    bzero(req->data, TWS_SECTOR_SIZE);
    param = (struct tws_getset_param *)req->data;

    req->cb = NULL;
    req->flags = TWS_DIR_IN;
    cmd_pkt = req->cmd_pkt;

    cmd = &cmd_pkt->cmd.pkt_g;
    cmd->param.sgl_off__opcode =
            BUILD_SGL_OFF__OPCODE(2, TWS_FW_CMD_GET_PARAM);
    cmd->param.request_id = (u_int8_t)req->request_id;
    cmd->param.host_id__unit = 0;
    cmd->param.param_count = 1;
    cmd->param.size = 2; /* map routine will add sgls */

    /* Specify which parameter we want to set. */
    param->table_id = (table_id | TWS_9K_PARAM_DESCRIPTOR);
    param->parameter_id = (u_int8_t)(param_id);
    param->parameter_size_bytes = (u_int16_t)param_size;
   
    error = tws_map_request(sc, req);
    if (!error) {
        reqid = tws_poll4_response(sc, &mfa);
        tws_unmap_request(sc, req);

        if ( reqid == TWS_REQ_TYPE_GETSET_PARAM ) {
            memcpy(data, param->data, param_size);
        } else {
            error = FAILURE;
        }
    }
  
    free(req->data, M_TWS);
    req->state = TWS_REQ_STATE_FREE;
    return(error);

}

void 
tws_unmap_request(struct tws_softc *sc, struct tws_request *req)
{
    if (req->data != NULL) {
        if ( req->flags & TWS_DIR_IN )
            bus_dmamap_sync(sc->data_tag, req->dma_map, 
                                            BUS_DMASYNC_POSTREAD);
        if ( req->flags & TWS_DIR_OUT )
            bus_dmamap_sync(sc->data_tag, req->dma_map, 
                                            BUS_DMASYNC_POSTWRITE);
        mtx_lock(&sc->io_lock);
        bus_dmamap_unload(sc->data_tag, req->dma_map);
        mtx_unlock(&sc->io_lock);
    }
}

int32_t
tws_map_request(struct tws_softc *sc, struct tws_request *req)
{
    int32_t error = 0;


    /* If the command involves data, map that too. */       
    if (req->data != NULL) {
        int my_flags = ((req->type == TWS_REQ_TYPE_SCSI_IO) ? BUS_DMA_WAITOK : BUS_DMA_NOWAIT);

        /*
         * Map the data buffer into bus space and build the SG list.
         */
        mtx_lock(&sc->io_lock);
	if (req->flags & TWS_DATA_CCB)
		error = bus_dmamap_load_ccb(sc->data_tag, req->dma_map,
					    req->data,
					    tws_dmamap_data_load_cbfn, req,
					    my_flags);
	else
		error = bus_dmamap_load(sc->data_tag, req->dma_map,
					req->data, req->length,
					tws_dmamap_data_load_cbfn, req,
					my_flags);
        mtx_unlock(&sc->io_lock);

        if (error == EINPROGRESS) {
            TWS_TRACE(sc, "in progress", 0, error);
            tws_freeze_simq(sc, req);
            error = 0;  // EINPROGRESS is not a fatal error.
        } 
    } else { /* no data involved */
        error = tws_submit_command(sc, req);
    }
    return(error);
}


static void
tws_dmamap_data_load_cbfn(void *arg, bus_dma_segment_t *segs, 
                            int nseg, int error)
{
    struct tws_request *req = (struct tws_request *)arg;
    struct tws_softc *sc = req->sc;
    u_int16_t sgls = nseg;
    void *sgl_ptr;
    struct tws_cmd_generic *gcmd;


    if ( error ) {
        TWS_TRACE(sc, "SOMETHING BAD HAPPENED! error = %d\n", error, 0);
    }

    if ( error == EFBIG ) {
        TWS_TRACE(sc, "not enough data segs", 0, nseg);
        req->error_code = error;
        req->ccb_ptr->ccb_h.status = CAM_REQ_TOO_BIG;
        return;
    }

    if ( req->flags & TWS_DIR_IN )
        bus_dmamap_sync(req->sc->data_tag, req->dma_map, 
                                            BUS_DMASYNC_PREREAD);
    if ( req->flags & TWS_DIR_OUT )
        bus_dmamap_sync(req->sc->data_tag, req->dma_map, 
                                        BUS_DMASYNC_PREWRITE);
    if ( segs ) {
        if ( (req->type == TWS_REQ_TYPE_PASSTHRU && 
             GET_OPCODE(req->cmd_pkt->cmd.pkt_a.res__opcode) != 
                            TWS_FW_CMD_EXECUTE_SCSI) ||
              req->type == TWS_REQ_TYPE_GETSET_PARAM) {
            gcmd = &req->cmd_pkt->cmd.pkt_g.generic;
            sgl_ptr = (u_int32_t *)(gcmd) + gcmd->size;
            gcmd->size += sgls * 
                          ((req->sc->is64bit && !tws_use_32bit_sgls) ? 4 : 2 );
            tws_fill_sg_list(req->sc, (void *)segs, sgl_ptr, sgls);

        } else {
            tws_fill_sg_list(req->sc, (void *)segs, 
                      (void *)&(req->cmd_pkt->cmd.pkt_a.sg_list), sgls);
            req->cmd_pkt->cmd.pkt_a.lun_h4__sgl_entries |= sgls ;
        }
    }


    req->error_code = tws_submit_command(req->sc, req);

}


static void
tws_fill_sg_list(struct tws_softc *sc, void *sgl_src, void *sgl_dest, 
                          u_int16_t num_sgl_entries)
{
    int i;

    if ( sc->is64bit ) {
        struct tws_sg_desc64 *sgl_s = (struct tws_sg_desc64 *)sgl_src;

        if ( !tws_use_32bit_sgls ) { 
            struct tws_sg_desc64 *sgl_d = (struct tws_sg_desc64 *)sgl_dest;
            if ( num_sgl_entries > TWS_MAX_64BIT_SG_ELEMENTS )
                TWS_TRACE(sc, "64bit sg overflow", num_sgl_entries, 0);
            for (i = 0; i < num_sgl_entries; i++) {
                sgl_d[i].address = sgl_s->address;
                sgl_d[i].length = sgl_s->length;
                sgl_d[i].flag = 0;
                sgl_d[i].reserved = 0;
                sgl_s = (struct tws_sg_desc64 *) (((u_int8_t *)sgl_s) + 
                                               sizeof(bus_dma_segment_t));
            }
        } else {
            struct tws_sg_desc32 *sgl_d = (struct tws_sg_desc32 *)sgl_dest;
            if ( num_sgl_entries > TWS_MAX_32BIT_SG_ELEMENTS )
                TWS_TRACE(sc, "32bit sg overflow", num_sgl_entries, 0);
            for (i = 0; i < num_sgl_entries; i++) {
                sgl_d[i].address = sgl_s->address;
                sgl_d[i].length = sgl_s->length;
                sgl_d[i].flag = 0;
                sgl_s = (struct tws_sg_desc64 *) (((u_int8_t *)sgl_s) + 
                                               sizeof(bus_dma_segment_t));
            }
        }
    } else {
        struct tws_sg_desc32 *sgl_s = (struct tws_sg_desc32 *)sgl_src;
        struct tws_sg_desc32 *sgl_d = (struct tws_sg_desc32 *)sgl_dest;

        if ( num_sgl_entries > TWS_MAX_32BIT_SG_ELEMENTS )
            TWS_TRACE(sc, "32bit sg overflow", num_sgl_entries, 0);


        for (i = 0; i < num_sgl_entries; i++) {
            sgl_d[i].address = sgl_s[i].address;
            sgl_d[i].length = sgl_s[i].length;
            sgl_d[i].flag = 0;
        }
    }
}
 

void
tws_intr(void *arg)
{
    struct tws_softc *sc = (struct tws_softc *)arg;
    u_int32_t histat=0, db=0;

    if (!(sc)) {
        device_printf(sc->tws_dev, "null softc!!!\n");
        return;
    }

    if ( tws_get_state(sc) == TWS_RESET ) {
        return;
    }

    if ( tws_get_state(sc) != TWS_ONLINE ) {
        return;
    }

    sc->stats.num_intrs++;
    histat = tws_read_reg(sc, TWS_I2O0_HISTAT, 4);
    if ( histat & TWS_BIT2 ) {
        TWS_TRACE_DEBUG(sc, "door bell :)", histat, TWS_I2O0_HISTAT);
        db = tws_read_reg(sc, TWS_I2O0_IOBDB, 4);
        if ( db & TWS_BIT21 ) {
            tws_intr_attn_error(sc);
            return;
        }
        if ( db & TWS_BIT18 ) {
            tws_intr_attn_aen(sc);
        }
    }

    if ( histat & TWS_BIT3 ) {
        tws_intr_resp(sc);
    }
}

static void
tws_intr_attn_aen(struct tws_softc *sc)
{
    u_int32_t db=0;

    /* maskoff db intrs until all the aens are fetched */
    /* tws_disable_db_intr(sc); */
    tws_fetch_aen((void *)sc);
    tws_write_reg(sc, TWS_I2O0_HOBDBC, TWS_BIT18, 4);
    db = tws_read_reg(sc, TWS_I2O0_IOBDB, 4);

}

static void
tws_intr_attn_error(struct tws_softc *sc)
{
    u_int32_t db=0;

    TWS_TRACE(sc, "attn error", 0, 0);
    tws_write_reg(sc, TWS_I2O0_HOBDBC, ~0, 4);
    db = tws_read_reg(sc, TWS_I2O0_IOBDB, 4);
    device_printf(sc->tws_dev, "Micro controller error.\n");
    tws_reset(sc);
}

static void
tws_intr_resp(struct tws_softc *sc)
{
    u_int16_t req_id;
    u_int64_t mfa;

    while ( tws_get_response(sc, &req_id, &mfa) ) {
        sc->stats.reqs_out++;
        if ( req_id == TWS_INVALID_REQID ) {
            TWS_TRACE_DEBUG(sc, "invalid req_id", mfa, req_id);
            sc->stats.reqs_errored++;
            tws_err_complete(sc, mfa);
            continue;
        }
        sc->reqs[req_id].cb(&sc->reqs[req_id]);
    }

}


static void
tws_poll(struct cam_sim *sim)
{
    struct tws_softc *sc = (struct tws_softc *)cam_sim_softc(sim);
    TWS_TRACE_DEBUG(sc, "entry", 0, 0);
    tws_intr((void *) sc);
}

void
tws_timeout(void *arg)
{
    struct tws_request *req = (struct tws_request *)arg;
    struct tws_softc *sc = req->sc;


    if ( req->error_code == TWS_REQ_RET_RESET ) {
        return;
    }

    mtx_lock(&sc->gen_lock);
    if ( req->error_code == TWS_REQ_RET_RESET ) {
        mtx_unlock(&sc->gen_lock);
        return;
    }

    if ( tws_get_state(sc) == TWS_RESET ) {
        mtx_unlock(&sc->gen_lock);
        return;
    }

    xpt_freeze_simq(sc->sim, 1);

    tws_send_event(sc, TWS_RESET_START);

    if (req->type == TWS_REQ_TYPE_SCSI_IO) {
        device_printf(sc->tws_dev, "I/O Request timed out... Resetting controller\n");
    } else if (req->type == TWS_REQ_TYPE_PASSTHRU) {
        device_printf(sc->tws_dev, "IOCTL Request timed out... Resetting controller\n");
    } else {
        device_printf(sc->tws_dev, "Internal Request timed out... Resetting controller\n");
    }

    tws_assert_soft_reset(sc);
    tws_turn_off_interrupts(sc);
    tws_reset_cb( (void*) sc );
    tws_reinit( (void*) sc );

//  device_printf(sc->tws_dev,  "Controller Reset complete!\n");
    tws_send_event(sc, TWS_RESET_COMPLETE);
    mtx_unlock(&sc->gen_lock);

    xpt_release_simq(sc->sim, 1);
}

void
tws_reset(void *arg)
{
    struct tws_softc *sc = (struct tws_softc *)arg;

    mtx_lock(&sc->gen_lock);
    if ( tws_get_state(sc) == TWS_RESET ) {
        mtx_unlock(&sc->gen_lock);
        return;
    }

    xpt_freeze_simq(sc->sim, 1);

    tws_send_event(sc, TWS_RESET_START);

    device_printf(sc->tws_dev,  "Resetting controller\n");

    tws_assert_soft_reset(sc);
    tws_turn_off_interrupts(sc);
    tws_reset_cb( (void*) sc );
    tws_reinit( (void*) sc );

//  device_printf(sc->tws_dev,  "Controller Reset complete!\n");
    tws_send_event(sc, TWS_RESET_COMPLETE);
    mtx_unlock(&sc->gen_lock);

    xpt_release_simq(sc->sim, 1);
}

static void
tws_reset_cb(void *arg)
{
    struct tws_softc *sc = (struct tws_softc *)arg;
    time_t endt;
    int found = 0;
    u_int32_t reg;
  
    if ( tws_get_state(sc) != TWS_RESET ) {
        return;
    }

//  device_printf(sc->tws_dev,  "Draining Busy Queue\n");
    tws_drain_busy_queue(sc);
//  device_printf(sc->tws_dev,  "Draining Reserved Reqs\n");
    tws_drain_reserved_reqs(sc);
//  device_printf(sc->tws_dev,  "Draining Response Queue\n");
    tws_drain_response_queue(sc);

//  device_printf(sc->tws_dev,  "Looking for controller ready flag...\n");
    endt = TWS_LOCAL_TIME + TWS_POLL_TIMEOUT;
    while ((TWS_LOCAL_TIME <= endt) && (!found)) {
        reg = tws_read_reg(sc, TWS_I2O0_SCRPD3, 4);
        if ( reg & TWS_BIT13 ) {
            found = 1;
//          device_printf(sc->tws_dev,  " ... Got it!\n");
        }
    }
    if ( !found )
            device_printf(sc->tws_dev,  " ... Controller ready flag NOT found!\n");
}

static void
tws_reinit(void *arg)
{
    struct tws_softc *sc = (struct tws_softc *)arg;
    int timeout_val=0;
    int try=2;
    int done=0;


//  device_printf(sc->tws_dev,  "Waiting for Controller Ready\n");
    while ( !done && try ) {
        if ( tws_ctlr_ready(sc) ) {
            done = 1;
            break;
        } else {
            timeout_val += 5;
            if ( timeout_val >= TWS_RESET_TIMEOUT ) {
               timeout_val = 0;
               if ( try )
                   tws_assert_soft_reset(sc);
               try--;
            }
            mtx_sleep(sc, &sc->gen_lock, 0, "tws_reinit", 5*hz);
        }
    }

    if (!done) {
        device_printf(sc->tws_dev,  "FAILED to get Controller Ready!\n");
        return;
    }

    sc->obfl_q_overrun = false;
//  device_printf(sc->tws_dev,  "Sending initConnect\n");
    if ( tws_init_connect(sc, tws_queue_depth) ) {
        TWS_TRACE_DEBUG(sc, "initConnect failed", 0, sc->is64bit);
    }
    tws_init_obfl_q(sc);

    tws_turn_on_interrupts(sc);

    wakeup_one(sc);
}


static void
tws_freeze_simq(struct tws_softc *sc, struct tws_request *req)
{
    /* Only for IO commands */
    if (req->type == TWS_REQ_TYPE_SCSI_IO) {
        union ccb   *ccb = (union ccb *)(req->ccb_ptr);

        xpt_freeze_simq(sc->sim, 1);
        ccb->ccb_h.status |= CAM_RELEASE_SIMQ;
        ccb->ccb_h.status |= CAM_REQUEUE_REQ;
    }
}


TUNABLE_INT("hw.tws.cam_depth", &tws_cam_depth);
