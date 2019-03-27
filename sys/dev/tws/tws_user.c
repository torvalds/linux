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
#include <dev/tws/tws_user.h>


int tws_ioctl(struct cdev *dev, long unsigned int cmd, caddr_t buf, int flags, 
                                                    struct thread *td);
void tws_passthru_complete(struct tws_request *req);
extern void tws_circular_aenq_insert(struct tws_softc *sc,
                    struct tws_circular_q *cq, struct tws_event_packet *aen);


static int tws_passthru(struct tws_softc *sc, void *buf);
static int tws_ioctl_aen(struct tws_softc *sc, u_long cmd, void *buf);

extern int tws_bus_scan(struct tws_softc *sc);
extern struct tws_request *tws_get_request(struct tws_softc *sc, 
                                           u_int16_t type);
extern int32_t tws_map_request(struct tws_softc *sc, struct tws_request *req);
extern void tws_unmap_request(struct tws_softc *sc, struct tws_request *req);
extern uint8_t tws_get_state(struct tws_softc *sc);
extern void tws_timeout(void *arg);

int
tws_ioctl(struct cdev *dev, u_long cmd, caddr_t buf, int flags, 
                                                    struct thread *td)
{
    struct tws_softc *sc = (struct tws_softc *)(dev->si_drv1);
    int error;

    TWS_TRACE_DEBUG(sc, "entry", sc, cmd);
    sc->stats.ioctls++;
    switch(cmd) {
        case TWS_IOCTL_FIRMWARE_PASS_THROUGH :
            error = tws_passthru(sc, (void *)buf);
            break;
        case TWS_IOCTL_SCAN_BUS :
            TWS_TRACE_DEBUG(sc, "scan-bus", 0, 0);
            error = tws_bus_scan(sc);
            break;
        default :
            TWS_TRACE_DEBUG(sc, "ioctl-aen", cmd, buf);
            error = tws_ioctl_aen(sc, cmd, (void *)buf);
            break;

    }
    return(error);
}

static int 
tws_passthru(struct tws_softc *sc, void *buf)
{
    struct tws_request *req;
    struct tws_ioctl_no_data_buf *ubuf = (struct tws_ioctl_no_data_buf *)buf;
    int error;
    u_int32_t buffer_length;
    u_int16_t lun4;

    buffer_length = roundup2(ubuf->driver_pkt.buffer_length, 512);
    if ( buffer_length > TWS_MAX_IO_SIZE ) {
        return(EINVAL);
    }
    if ( tws_get_state(sc) != TWS_ONLINE) {
        return(EBUSY);
    }

    //==============================================================================================
    // Get a command
    //
    do {
        req = tws_get_request(sc, TWS_REQ_TYPE_PASSTHRU);
        if ( !req ) {
            error = tsleep(sc,  0, "tws_sleep", TWS_IOCTL_TIMEOUT*hz);
            if ( error == EWOULDBLOCK ) {
                return(ETIMEDOUT);
            }
        } else {
            // Make sure we are still ready for new commands...
            if ( tws_get_state(sc) != TWS_ONLINE) {
                return(EBUSY);
            }
            break;
        }
    } while(1);

    req->length = buffer_length;
    TWS_TRACE_DEBUG(sc, "datal,rid", req->length, req->request_id);
    if ( req->length ) {
        req->data = sc->ioctl_data_mem;
        req->dma_map = sc->ioctl_data_map;

        //==========================================================================================
        // Copy data in from user space
        //
        error = copyin(ubuf->pdata, req->data, req->length);
    }

    //==============================================================================================
    // Set command fields
    //
    req->flags = TWS_DIR_IN | TWS_DIR_OUT;
    req->cb = tws_passthru_complete;

    memcpy(&req->cmd_pkt->cmd, &ubuf->cmd_pkt.cmd, 
                              sizeof(struct tws_command_apache));

    if ( GET_OPCODE(req->cmd_pkt->cmd.pkt_a.res__opcode) == 
                                               TWS_FW_CMD_EXECUTE_SCSI ) { 
        lun4 = req->cmd_pkt->cmd.pkt_a.lun_l4__req_id & 0xF000;
        req->cmd_pkt->cmd.pkt_a.lun_l4__req_id = lun4 | req->request_id;
    } else {
        req->cmd_pkt->cmd.pkt_g.generic.request_id = (u_int8_t) req->request_id;
    }

    //==============================================================================================
    // Send command to controller
    //
    error = tws_map_request(sc, req);
    if (error) {
        ubuf->driver_pkt.os_status = error;
        goto out_data;
    }

    if ( req->state == TWS_REQ_STATE_COMPLETE ) {
        ubuf->driver_pkt.os_status = req->error_code;
        goto out_unmap;
    }

    mtx_lock(&sc->gen_lock);
    error = mtx_sleep(req, &sc->gen_lock, 0, "tws_passthru", TWS_IOCTL_TIMEOUT*hz);
    mtx_unlock(&sc->gen_lock);
    if (( req->state != TWS_REQ_STATE_COMPLETE ) && ( error == EWOULDBLOCK )) {
            TWS_TRACE_DEBUG(sc, "msleep timeout", error, req->request_id);
            tws_timeout((void*) req);
    }

out_unmap:
    if ( req->error_code == TWS_REQ_RET_RESET ) {
        error = EBUSY;
        req->error_code = EBUSY;
        TWS_TRACE_DEBUG(sc, "ioctl reset", error, req->request_id);
    }

    tws_unmap_request(sc, req);

    //==============================================================================================
    // Return command status to user space
    //
    memcpy(&ubuf->cmd_pkt.hdr, &req->cmd_pkt->hdr, sizeof(struct tws_command_apache));
    memcpy(&ubuf->cmd_pkt.cmd, &req->cmd_pkt->cmd, sizeof(struct tws_command_apache));

out_data:
    if ( req->length ) {
        //==========================================================================================
        // Copy data out to user space
        //
        if ( !error )
            error = copyout(req->data, ubuf->pdata, ubuf->driver_pkt.buffer_length);
    }

    if ( error ) 
        TWS_TRACE_DEBUG(sc, "errored", error, 0);

    if ( req->error_code != TWS_REQ_RET_SUBMIT_SUCCESS )
        ubuf->driver_pkt.os_status = error;

    //==============================================================================================
    // Free command
    //
    req->state = TWS_REQ_STATE_FREE;

    wakeup_one(sc);

    return(error);
}

void 
tws_passthru_complete(struct tws_request *req)
{
    req->state = TWS_REQ_STATE_COMPLETE;
    wakeup_one(req);

}

static void 
tws_retrive_aen(struct tws_softc *sc, u_long cmd, 
                            struct tws_ioctl_packet *ubuf)
{
    u_int16_t index=0;
    struct tws_event_packet eventp, *qp;

    if ( sc->aen_q.head == sc->aen_q.tail ) {
        ubuf->driver_pkt.status = TWS_AEN_NO_EVENTS;
        return;
    }
    
    ubuf->driver_pkt.status = 0;

    /* 
     * once this flag is set cli will not display alarms 
     * needs a revisit from tools?
     */
    if ( sc->aen_q.overflow ) {
        ubuf->driver_pkt.status = TWS_AEN_OVERFLOW;
        sc->aen_q.overflow = 0; /* reset */
    }

    qp = (struct tws_event_packet *)sc->aen_q.q;

    switch (cmd) {
        case TWS_IOCTL_GET_FIRST_EVENT :
            index = sc->aen_q.head;
            break;
        case TWS_IOCTL_GET_LAST_EVENT :
            /* index = tail-1 */ 
            index = (sc->aen_q.depth + sc->aen_q.tail - 1) % sc->aen_q.depth;
            break;
        case TWS_IOCTL_GET_NEXT_EVENT :
            memcpy(&eventp, ubuf->data_buf, sizeof(struct tws_event_packet));
            index = sc->aen_q.head;
            do {
                if ( qp[index].sequence_id == 
                           (eventp.sequence_id + 1) )
                    break;
                index  = (index+1) % sc->aen_q.depth;
            }while ( index != sc->aen_q.tail );
            if ( index == sc->aen_q.tail ) {
                ubuf->driver_pkt.status = TWS_AEN_NO_EVENTS;
                return;
            }
            break;
        case TWS_IOCTL_GET_PREVIOUS_EVENT :
            memcpy(&eventp, ubuf->data_buf, sizeof(struct tws_event_packet));
            index = sc->aen_q.head;
            do {
                if ( qp[index].sequence_id == 
                           (eventp.sequence_id - 1) )
                    break;
                index  = (index+1) % sc->aen_q.depth;
            }while ( index != sc->aen_q.tail );
            if ( index == sc->aen_q.tail ) {
                ubuf->driver_pkt.status = TWS_AEN_NO_EVENTS;
                return;
            }
            break;
        default :
            TWS_TRACE_DEBUG(sc, "not a valid event", sc, cmd);
            ubuf->driver_pkt.status = TWS_AEN_NO_EVENTS;
            return;
    }

    memcpy(ubuf->data_buf, &qp[index], 
                           sizeof(struct tws_event_packet));
    qp[index].retrieved = TWS_AEN_RETRIEVED;

    return;

}

static int 
tws_ioctl_aen(struct tws_softc *sc, u_long cmd, void *buf)
{

    struct tws_ioctl_packet *ubuf = (struct tws_ioctl_packet *)buf;
    struct tws_compatibility_packet cpkt;
    struct tws_lock_packet lpkt;
    time_t ctime;

    mtx_lock(&sc->gen_lock);
    ubuf->driver_pkt.status = 0;
    switch(cmd) {
        case TWS_IOCTL_GET_FIRST_EVENT :
        case TWS_IOCTL_GET_LAST_EVENT :
        case TWS_IOCTL_GET_NEXT_EVENT :
        case TWS_IOCTL_GET_PREVIOUS_EVENT :
            tws_retrive_aen(sc,cmd,ubuf);
            break;
        case TWS_IOCTL_GET_LOCK :
            ctime = TWS_LOCAL_TIME;
            memcpy(&lpkt, ubuf->data_buf, sizeof(struct tws_lock_packet));
            if ( (sc->ioctl_lock.lock == TWS_IOCTL_LOCK_FREE) ||
                 (lpkt.force_flag) ||
                 (ctime >= sc->ioctl_lock.timeout) ) {
                sc->ioctl_lock.lock = TWS_IOCTL_LOCK_HELD;
                sc->ioctl_lock.timeout = ctime + (lpkt.timeout_msec / 1000);
                lpkt.time_remaining_msec = lpkt.timeout_msec;
            }  else {
                lpkt.time_remaining_msec = (u_int32_t)
                          ((sc->ioctl_lock.timeout - ctime) * 1000);
                ubuf->driver_pkt.status = TWS_IOCTL_LOCK_ALREADY_HELD;

            }
            break;
        case TWS_IOCTL_RELEASE_LOCK :
            if (sc->ioctl_lock.lock == TWS_IOCTL_LOCK_FREE) {
                ubuf->driver_pkt.status = TWS_IOCTL_LOCK_NOT_HELD;
            } else {
                sc->ioctl_lock.lock = TWS_IOCTL_LOCK_FREE;
                ubuf->driver_pkt.status = 0;
            }
            break;
        case TWS_IOCTL_GET_COMPATIBILITY_INFO :
            TWS_TRACE_DEBUG(sc, "get comp info", sc, cmd);

            memcpy( cpkt.driver_version, TWS_DRIVER_VERSION_STRING,
                                         sizeof(TWS_DRIVER_VERSION_STRING));
            cpkt.working_srl = sc->cinfo.working_srl;
            cpkt.working_branch = sc->cinfo.working_branch;
            cpkt.working_build = sc->cinfo.working_build;
            cpkt.driver_srl_high = TWS_CURRENT_FW_SRL;
            cpkt.driver_branch_high = TWS_CURRENT_FW_BRANCH;
            cpkt.driver_build_high = TWS_CURRENT_FW_BUILD;
            cpkt.driver_srl_low = TWS_BASE_FW_SRL;
            cpkt.driver_branch_low = TWS_BASE_FW_BRANCH;
            cpkt.driver_build_low = TWS_BASE_FW_BUILD;
            cpkt.fw_on_ctlr_srl = sc->cinfo.fw_on_ctlr_srl;
            cpkt.fw_on_ctlr_branch = sc->cinfo.fw_on_ctlr_branch;
            cpkt.fw_on_ctlr_build = sc->cinfo.fw_on_ctlr_build;
            ubuf->driver_pkt.status = 0;
            int len = sizeof(struct tws_compatibility_packet);
            if ( ubuf->driver_pkt.buffer_length < len )
                len = ubuf->driver_pkt.buffer_length;
            memcpy(ubuf->data_buf, &cpkt, len);

            break;
        default :
            TWS_TRACE_DEBUG(sc, "not valid cmd", cmd, 
                           TWS_IOCTL_GET_COMPATIBILITY_INFO);
            break;

    }
    mtx_unlock(&sc->gen_lock);
    return(SUCCESS);

}

void
tws_circular_aenq_insert(struct tws_softc *sc, struct tws_circular_q *cq,
struct tws_event_packet *aen)
{

    struct tws_event_packet *q = (struct tws_event_packet *)cq->q;
    volatile u_int16_t head, tail;
    u_int8_t retr;
    mtx_assert(&sc->gen_lock, MA_OWNED);

    head = cq->head;
    tail = cq->tail;
    retr = q[tail].retrieved;

    memcpy(&q[tail], aen, sizeof(struct tws_event_packet));
    tail = (tail+1) % cq->depth;

    if ( head == tail ) { /* q is full */
        if ( retr != TWS_AEN_RETRIEVED )
            cq->overflow = 1;
        cq->head = (head+1) % cq->depth;
    }
    cq->tail = tail;

}
