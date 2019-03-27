/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2010, LSI Corp.
 * All rights reserved.
 * Author : Manjunath Ranganathaiah
 * Support: freebsdraid@lsi.com
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of the <ORGANIZATION> nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */


#include <dev/tws/tws.h>
#include <dev/tws/tws_services.h>
#include <dev/tws/tws_hdm.h>


int tws_use_32bit_sgls=0;
extern u_int64_t mfa_base;
extern struct tws_request *tws_get_request(struct tws_softc *sc, 
                                           u_int16_t type);
extern void tws_q_insert_tail(struct tws_softc *sc, struct tws_request *req,
                                u_int8_t q_type );
extern struct tws_request * tws_q_remove_request(struct tws_softc *sc,
                                   struct tws_request *req, u_int8_t q_type );

extern void tws_cmd_complete(struct tws_request *req);
extern void tws_print_stats(void *arg);
extern int tws_send_scsi_cmd(struct tws_softc *sc, int cmd);
extern int tws_set_param(struct tws_softc *sc, u_int32_t table_id, 
           u_int32_t param_id, u_int32_t param_size, void *data);
extern int tws_get_param(struct tws_softc *sc, u_int32_t table_id,
            u_int32_t param_id, u_int32_t param_size, void *data);
extern void tws_reset(void *arg);

int tws_init_connect(struct tws_softc *sc, u_int16_t mc);
int tws_init_ctlr(struct tws_softc *sc);
int tws_submit_command(struct tws_softc *sc, struct tws_request *req);
void tws_nop_cmd(void *arg);
u_int16_t tws_poll4_response(struct tws_softc *sc, u_int64_t *mfa);
boolean tws_get_response(struct tws_softc *sc, u_int16_t *req_id, 
                                               u_int64_t *mfa);
boolean tws_ctlr_ready(struct tws_softc *sc);
void tws_turn_on_interrupts(struct tws_softc *sc);
void tws_turn_off_interrupts(struct tws_softc *sc);
boolean tws_ctlr_reset(struct tws_softc *sc);
void tws_assert_soft_reset(struct tws_softc *sc);

int tws_send_generic_cmd(struct tws_softc *sc, u_int8_t opcode);
void tws_fetch_aen(void *arg);
void tws_disable_db_intr(struct tws_softc *sc);
void tws_enable_db_intr(struct tws_softc *sc);
void tws_aen_synctime_with_host(struct tws_softc *sc);
void tws_init_obfl_q(struct tws_softc *sc);
void tws_display_ctlr_info(struct tws_softc *sc);

int 
tws_init_ctlr(struct tws_softc *sc)
{
    u_int64_t reg;
    u_int32_t regh, regl;

    TWS_TRACE_DEBUG(sc, "entry", sc, sc->is64bit);
    sc->obfl_q_overrun = false;
    if ( tws_init_connect(sc, tws_queue_depth) )
    {
        TWS_TRACE_DEBUG(sc, "initConnect failed", 0, sc->is64bit);
        return(FAILURE);
        
    }


    while( 1 ) {
        regh = tws_read_reg(sc, TWS_I2O0_IOPOBQPH, 4);
        regl = tws_read_reg(sc, TWS_I2O0_IOPOBQPL, 4);
        reg = (((u_int64_t)regh) << 32) | regl;
        TWS_TRACE_DEBUG(sc, "host outbound cleanup",reg, regl);
        if ( regh == TWS_FIFO_EMPTY32 )
            break;
    } 

    tws_init_obfl_q(sc);
    tws_display_ctlr_info(sc);
    tws_write_reg(sc, TWS_I2O0_HOBDBC, ~0, 4);
    tws_turn_on_interrupts(sc);
    return(SUCCESS);
}

void
tws_init_obfl_q(struct tws_softc *sc)
{
    int i=0;
    u_int64_t paddr;
    u_int32_t paddrh, paddrl, status;

    TWS_TRACE_DEBUG(sc, "entry", 0, sc->obfl_q_overrun);

    while ( i < tws_queue_depth ) {
        paddr = sc->sense_bufs[i].hdr_pkt_phy;
        paddrh = (u_int32_t)( paddr>>32);
        paddrl = (u_int32_t) paddr;
        tws_write_reg(sc, TWS_I2O0_HOBQPH, paddrh, 4);
        tws_write_reg(sc, TWS_I2O0_HOBQPL, paddrl, 4);
  
        status = tws_read_reg(sc, TWS_I2O0_STATUS, 4);
        if ( status & TWS_BIT13 ) {
            device_printf(sc->tws_dev,  "OBFL Overrun\n");
            sc->obfl_q_overrun = true;
            break;
        }
        i++;
    }

    if ( i == tws_queue_depth )
        sc->obfl_q_overrun = false;
}

int
tws_init_connect(struct tws_softc *sc, u_int16_t mcreadits )
{
    struct tws_request *req;
    struct tws_cmd_init_connect *initc;
    u_int16_t reqid;
    u_int64_t mfa;

    TWS_TRACE_DEBUG(sc, "entry", 0, mcreadits);
#if       0
    req = tws_get_request(sc, TWS_REQ_TYPE_INTERNAL_CMD);
#else  // 0
    req = &sc->reqs[TWS_REQ_TYPE_INTERNAL_CMD];
    bzero(&req->cmd_pkt->cmd, sizeof(struct tws_command_apache));
    req->data = NULL;
    req->length = 0;
    req->type = TWS_REQ_TYPE_INTERNAL_CMD;
    req->flags = TWS_DIR_UNKNOWN;
    req->error_code = TWS_REQ_RET_INVALID;
    req->cb = NULL;
    req->ccb_ptr = NULL;
    callout_stop(&req->timeout);
    req->next = req->prev = NULL;
    req->state = TWS_REQ_STATE_BUSY;
#endif // 0

    if ( req == NULL ) {
        TWS_TRACE_DEBUG(sc, "no requests", 0, 0);
//      device_printf(sc->tws_dev,  "No requests for initConnect\n");
        return(FAILURE);
    }

    tws_swap16(0xbeef); /* just for test */
    tws_swap32(0xdeadbeef); /* just for test */
    tws_swap64(0xdeadbeef); /* just for test */
    initc = &(req->cmd_pkt->cmd.pkt_g.init_connect);
    /* req->cmd_pkt->hdr.header_desc.size_header = 128; */

    initc->res1__opcode = 
              BUILD_RES__OPCODE(0, TWS_FW_CMD_INIT_CONNECTION);
    initc->size = 6;
    initc->request_id = req->request_id;
    initc->message_credits = mcreadits;
    initc->features |= TWS_BIT_EXTEND;
    if ( sc->is64bit && !tws_use_32bit_sgls )
        initc->features |= TWS_64BIT_SG_ADDRESSES;
    /* assuming set features is always on */ 

    initc->size = 6;
    initc->fw_srl = sc->cinfo.working_srl = TWS_CURRENT_FW_SRL;
    initc->fw_arch_id = 0;
    initc->fw_branch = sc->cinfo.working_branch = 0;
    initc->fw_build = sc->cinfo.working_build = 0;

    req->error_code = tws_submit_command(sc, req);
    reqid = tws_poll4_response(sc, &mfa);
    if ( reqid != TWS_INVALID_REQID && reqid == req->request_id ) {
        sc->cinfo.fw_on_ctlr_srl = initc->fw_srl;
        sc->cinfo.fw_on_ctlr_branch = initc->fw_branch;
        sc->cinfo.fw_on_ctlr_build = initc->fw_build;
        sc->stats.reqs_out++;
        req->state = TWS_REQ_STATE_FREE;
    }
    else {
        /*
         * REVISIT::If init connect fails we need to reset the ctlr
         * and try again? 
         */ 
        TWS_TRACE(sc, "unexpected req_id ", reqid, 0);
        TWS_TRACE(sc, "INITCONNECT FAILED", reqid, 0);
        return(FAILURE);
    }
    return(SUCCESS);
}

void 
tws_display_ctlr_info(struct tws_softc *sc)
{

    uint8_t fw_ver[16], bios_ver[16], ctlr_model[16], num_phys=0;
    uint32_t error[4];

    error[0] = tws_get_param(sc, TWS_PARAM_PHYS_TABLE,
                             TWS_PARAM_CONTROLLER_PHYS_COUNT, 1, &num_phys);
    error[1] = tws_get_param(sc, TWS_PARAM_VERSION_TABLE,
                             TWS_PARAM_VERSION_FW, 16, fw_ver);
    error[2] = tws_get_param(sc, TWS_PARAM_VERSION_TABLE,
                             TWS_PARAM_VERSION_BIOS, 16, bios_ver);
    error[3] = tws_get_param(sc, TWS_PARAM_VERSION_TABLE,
                             TWS_PARAM_CTLR_MODEL, 16, ctlr_model);

    if ( !error[0] && !error[1] && !error[2] && !error[3] ) {
        device_printf( sc->tws_dev, 
        "Controller details: Model %.16s, %d Phys, Firmware %.16s, BIOS %.16s\n",
         ctlr_model, num_phys, fw_ver, bios_ver);
    }

}

int
tws_send_generic_cmd(struct tws_softc *sc, u_int8_t opcode)
{
    struct tws_request *req;
    struct tws_cmd_generic *cmd;

    TWS_TRACE_DEBUG(sc, "entry", sc, opcode);
    req = tws_get_request(sc, TWS_REQ_TYPE_INTERNAL_CMD);

    if ( req == NULL ) {
        TWS_TRACE_DEBUG(sc, "no requests", 0, 0);
        return(FAILURE);
    }

    cmd = &(req->cmd_pkt->cmd.pkt_g.generic);
    bzero(cmd, sizeof(struct tws_cmd_generic));
    /* req->cmd_pkt->hdr.header_desc.size_header = 128; */
    req->cb = tws_cmd_complete;

    cmd->sgl_off__opcode = BUILD_RES__OPCODE(0, opcode);
    cmd->size = 2;
    cmd->request_id = req->request_id;
    cmd->host_id__unit = 0;
    cmd->status = 0;
    cmd->flags = 0;
    cmd->count = 0;

    req->error_code = tws_submit_command(sc, req);

    return(SUCCESS);

}


int
tws_submit_command(struct tws_softc *sc, struct tws_request *req)
{
    u_int32_t regl, regh;
    u_int64_t mfa=0;
    
    /* 
     * mfa register  read and write must be in order. 
     * Get the io_lock to protect against simultinous 
     * passthru calls 
     */
    mtx_lock(&sc->io_lock);

    if ( sc->obfl_q_overrun ) {
        tws_init_obfl_q(sc);
    }
       
#ifdef TWS_PULL_MODE_ENABLE
    regh = (u_int32_t)(req->cmd_pkt_phy >> 32);
    /* regh = regh | TWS_MSG_ACC_MASK; */ 
    mfa = regh;
    mfa = mfa << 32;
    regl = (u_int32_t)req->cmd_pkt_phy;
    regl = regl | TWS_BIT0;
    mfa = mfa | regl;
#else
    regh = tws_read_reg(sc, TWS_I2O0_HIBQPH, 4);
    mfa = regh;
    mfa = mfa << 32;
    regl = tws_read_reg(sc, TWS_I2O0_HIBQPL, 4);
    mfa = mfa | regl;
#endif

    mtx_unlock(&sc->io_lock);

    if ( mfa == TWS_FIFO_EMPTY ) {
        TWS_TRACE_DEBUG(sc, "inbound fifo empty", mfa, 0);

        /* 
         * Generally we should not get here.
         * If the fifo was empty we can't do any thing much 
         * retry later 
         */
        return(TWS_REQ_RET_PEND_NOMFA);

    }

#ifndef TWS_PULL_MODE_ENABLE
    for (int i=mfa; i<(sizeof(struct tws_command_packet)+ mfa - 
                            sizeof( struct tws_command_header)); i++) {

        bus_space_write_1(sc->bus_mfa_tag, sc->bus_mfa_handle,i, 
                               ((u_int8_t *)&req->cmd_pkt->cmd)[i-mfa]);

    }
#endif

    if ( req->type == TWS_REQ_TYPE_SCSI_IO ) {
        mtx_lock(&sc->q_lock);
        tws_q_insert_tail(sc, req, TWS_BUSY_Q);
        mtx_unlock(&sc->q_lock);
    }

    /* 
     * mfa register  read and write must be in order. 
     * Get the io_lock to protect against simultinous 
     * passthru calls 
     */
    mtx_lock(&sc->io_lock);

    tws_write_reg(sc, TWS_I2O0_HIBQPH, regh, 4);
    tws_write_reg(sc, TWS_I2O0_HIBQPL, regl, 4);

    sc->stats.reqs_in++;
    mtx_unlock(&sc->io_lock);
    
    return(TWS_REQ_RET_SUBMIT_SUCCESS);

}

/* 
 * returns true if the respose was available othewise, false.
 * In the case of error the arg mfa will contain the address and
 * req_id will be TWS_INVALID_REQID
 */
boolean
tws_get_response(struct tws_softc *sc, u_int16_t *req_id, u_int64_t *mfa) 
{
    u_int64_t out_mfa=0, val=0;
    struct tws_outbound_response out_res;

    *req_id = TWS_INVALID_REQID;
    out_mfa = (u_int64_t)tws_read_reg(sc, TWS_I2O0_HOBQPH, 4);

    if ( out_mfa == TWS_FIFO_EMPTY32 ) {
        return(false);

    }
    out_mfa = out_mfa << 32;
    val = tws_read_reg(sc, TWS_I2O0_HOBQPL, 4);
    out_mfa = out_mfa | val;
    
    out_res =  *(struct tws_outbound_response *)&out_mfa;

    if ( !out_res.not_mfa ) {
        *mfa = out_mfa;
        return(true);
    } else {
        *req_id = out_res.request_id;
    }
    
    return(true);
}




u_int16_t
tws_poll4_response(struct tws_softc *sc, u_int64_t *mfa)
{
    u_int16_t req_id;
    time_t endt;

    endt = TWS_LOCAL_TIME + TWS_POLL_TIMEOUT;
    do {
        if(tws_get_response(sc, &req_id, mfa)) {

            if ( req_id == TWS_INVALID_REQID ) {
                TWS_TRACE_DEBUG(sc, "invalid req_id", 0, req_id);
                return(TWS_INVALID_REQID);
            }
            return(req_id);
        }
    } while (TWS_LOCAL_TIME <= endt);
    TWS_TRACE_DEBUG(sc, "poll timeout", 0, 0);
    return(TWS_INVALID_REQID);
}

boolean
tws_ctlr_ready(struct tws_softc *sc)
{
    u_int32_t reg;

    reg = tws_read_reg(sc, TWS_I2O0_SCRPD3, 4);
    if ( reg & TWS_BIT13 )
        return(true);
    else
        return(false);
}

void
tws_turn_on_interrupts(struct tws_softc *sc)
{

    TWS_TRACE_DEBUG(sc, "entry", 0, 0);
    /* turn on response and db interrupt only */
    tws_write_reg(sc, TWS_I2O0_HIMASK, TWS_BIT0, 4);

}

void
tws_turn_off_interrupts(struct tws_softc *sc)
{

    TWS_TRACE_DEBUG(sc, "entry", 0, 0);
    
    tws_write_reg(sc, TWS_I2O0_HIMASK, ~0, 4);

}

void
tws_disable_db_intr(struct tws_softc *sc)
{
    u_int32_t reg;

    TWS_TRACE_DEBUG(sc, "entry", 0, 0);
    reg = tws_read_reg(sc, TWS_I2O0_HIMASK, 4);
    reg = reg | TWS_BIT2;
    tws_write_reg(sc, TWS_I2O0_HIMASK, reg, 4);
}

void
tws_enable_db_intr(struct tws_softc *sc)
{
    u_int32_t reg;

    TWS_TRACE_DEBUG(sc, "entry", 0, 0);
    reg = tws_read_reg(sc, TWS_I2O0_HIMASK, 4);
    reg = reg & ~TWS_BIT2;
    tws_write_reg(sc, TWS_I2O0_HIMASK, reg, 4);
}

boolean
tws_ctlr_reset(struct tws_softc *sc)
{

    u_int32_t reg;
    time_t endt;
    /* int i=0; */

    TWS_TRACE_DEBUG(sc, "entry", 0, 0);

    tws_assert_soft_reset(sc);

    do {
        reg = tws_read_reg(sc, TWS_I2O0_SCRPD3, 4);
    } while ( reg & TWS_BIT13 );

    endt = TWS_LOCAL_TIME + TWS_RESET_TIMEOUT;
    do {
        if(tws_ctlr_ready(sc))
            return(true);
    } while (TWS_LOCAL_TIME <= endt);
    return(false);

}

void
tws_assert_soft_reset(struct tws_softc *sc)
{
    u_int32_t reg;

    reg = tws_read_reg(sc, TWS_I2O0_HIBDB, 4);
    TWS_TRACE_DEBUG(sc, "in bound door bell read ", reg, TWS_I2O0_HIBDB);
    tws_write_reg(sc, TWS_I2O0_HIBDB, reg | TWS_BIT8, 4);

}

void
tws_fetch_aen(void *arg)
{
    struct tws_softc *sc = (struct tws_softc *)arg;
    int error = 0;

    TWS_TRACE_DEBUG(sc, "entry", 0, 0);

    if ((error = tws_send_scsi_cmd(sc, 0x03 /* REQUEST_SENSE */))) {
        TWS_TRACE_DEBUG(sc, "aen fetch send in progress", 0, 0);
    }
}

void
tws_aen_synctime_with_host(struct tws_softc *sc)
{

    int error;
    long int sync_time;

    TWS_TRACE_DEBUG(sc, "entry", sc, 0);

    sync_time = (TWS_LOCAL_TIME - (3 * 86400)) % 604800;
    TWS_TRACE_DEBUG(sc, "sync_time,ts", sync_time, time_second);
    TWS_TRACE_DEBUG(sc, "utc_offset", utc_offset(), 0);
    error = tws_set_param(sc, TWS_PARAM_TIME_TABLE, TWS_PARAM_TIME_SCHED_TIME,
                           4, &sync_time);
    if ( error )
        TWS_TRACE_DEBUG(sc, "set param failed", sync_time, error);
}

TUNABLE_INT("hw.tws.use_32bit_sgls", &tws_use_32bit_sgls);
