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
#include <dev/tws/tws_hdm.h>
#include <dev/tws/tws_services.h>
#include <sys/time.h>

void tws_q_insert_tail(struct tws_softc *sc, struct tws_request *req, 
                                u_int8_t q_type );
struct tws_request * tws_q_remove_request(struct tws_softc *sc, 
                                struct tws_request *req, u_int8_t q_type );
struct tws_request *tws_q_remove_head(struct tws_softc *sc, u_int8_t q_type );
void tws_q_insert_head(struct tws_softc *sc, struct tws_request *req,
                                u_int8_t q_type );
struct tws_request * tws_q_remove_tail(struct tws_softc *sc, u_int8_t q_type );
void tws_print_stats(void *arg);

struct tws_sense *tws_find_sense_from_mfa(struct tws_softc *sc, u_int64_t mfa);



static struct error_desc array[] = {
    { "Cannot add sysctl tree node", 0x2000, ERROR,
       "%s: (0x%02X: 0x%04X): %s:\n", "ERROR" },
    { "Register window not available", 0x2001, ERROR,
       "%s: (0x%02X: 0x%04X): %s:\n", "ERROR" },
    { "Can't allocate register window", 0x2002, ERROR,
       "%s: (0x%02X: 0x%04X): %s:\n", "ERROR" },
    { "Can't allocate interrupt", 0x2003, ERROR,
       "%s: (0x%02X: 0x%04X): %s:\n", "ERROR" },
    { "Can't set up interrupt", 0x2004, ERROR,
       "%s: (0x%02X: 0x%04X): %s:\n", "ERROR" },
    { "Couldn't intialize CAM", 0x2007, ERROR,
       "%s: (0x%02X: 0x%04X): %s:\n", "ERROR" },
    { "Couldn't create SIM device queue", 0x2100, ENOMEM,
       "%s: (0x%02X: 0x%04X): %s:\n", "ERROR" },
    { "Unable to  create SIM entry", 0x2101, ENOMEM,
       "%s: (0x%02X: 0x%04X): %s:\n", "ERROR" },
    { "Unable to  register the bus", 0x2102, ENXIO,
       "%s: (0x%02X: 0x%04X): %s:\n", "ERROR" },
    { "Unable to  create the path", 0x2103, ENXIO,
       "%s: (0x%02X: 0x%04X): %s:\n", "ERROR" },
    { "Bus scan request to CAM failed", 0x2104, ENXIO,
       "%s: (0x%02X: 0x%04X): %s:\n", "ERROR" },
    { "Unable to intialize the driver", 0x2008, ENXIO,
       "%s: (0x%02X: 0x%04X): %s:\n", "ERROR" },
    { "Unable to intialize the controller", 0x2009, ENXIO,
       "%s: (0x%02X: 0x%04X): %s:\n", "ERROR" },
};

void
tws_trace(const char *file, const char *fun, int linenum,
          struct tws_softc *sc, char *desc, u_int64_t val1, u_int64_t val2)
{ 


    struct tws_trace_rec *rec = (struct tws_trace_rec *)sc->trace_q.q;
    volatile u_int16_t head, tail;
    char fmt[256];

    head = sc->trace_q.head;
    tail = sc->trace_q.tail;
/*
    getnanotime(&rec[tail].ts);
*/
    strncpy(rec[tail].fname, file, TWS_TRACE_FNAME_LEN);
    strncpy(rec[tail].func, fun, TWS_TRACE_FUNC_LEN);
    rec[tail].linenum = linenum;
    strncpy(rec[tail].desc, desc, TWS_TRACE_DESC_LEN);
    rec[tail].val1 = val1;
    rec[tail].val2 = val2;

    tail = (tail+1) % sc->trace_q.depth;

    if ( head == tail ) { 
        sc->trace_q.overflow = 1;
        sc->trace_q.head = (head+1) % sc->trace_q.depth;
    }
    sc->trace_q.tail = tail;

/*
    tws_circular_q_insert(sc, &sc->trace_q, 
                              &rec, sizeof(struct tws_trace_rec));
*/
    if ( sc->is64bit )
        strcpy(fmt, "%05d:%s::%s :%s: 0x%016lx : 0x%016lx \n");
    else
        strcpy(fmt, "%05d:%s::%s :%s: 0x%016llx : 0x%016llx \n");

/*  
    printf("%05d:%s::%s :%s: 0x%016llx : 0x%016llx \n", 
            linenum, file, fun, desc, val1, val2);
*/
    printf(fmt, linenum, file, fun, desc, val1, val2);
}

void
tws_log(struct tws_softc *sc, int index)
{
    device_printf((sc)->tws_dev, array[index].fmt,
                    array[index].error_str,
                    array[index].error_code,
                    array[index].severity_level,
                    array[index].desc );
}

/* ----------- swap functions ----------- */


u_int16_t 
tws_swap16(u_int16_t val)
{
    return((val << 8) | (val >> 8));
}

u_int32_t 
tws_swap32(u_int32_t val)
{
    return(((val << 24) | ((val << 8) & (0xFF0000)) | 
           ((val >> 8) & (0xFF00)) | (val >> 24)));
}


u_int64_t 
tws_swap64(u_int64_t val)
{
    return((((u_int64_t)(tws_swap32(((u_int32_t *)(&(val)))[1]))) << 32) |
           ((u_int32_t)(tws_swap32(((u_int32_t *)(&(val)))[0]))));
}


/* ----------- reg access ----------- */


void
tws_write_reg(struct tws_softc *sc, int offset, 
                  u_int32_t value, int size)
{
    bus_space_tag_t         bus_tag = sc->bus_tag;
    bus_space_handle_t      bus_handle = sc->bus_handle;

    if (size == 4)
        bus_space_write_4(bus_tag, bus_handle, offset, value);
    else 
        if (size == 2)
            bus_space_write_2(bus_tag, bus_handle, offset, 
                                     (u_int16_t)value);
        else
            bus_space_write_1(bus_tag, bus_handle, offset, (u_int8_t)value);
}

u_int32_t
tws_read_reg(struct tws_softc *sc, int offset, int size)
{
    bus_space_tag_t bus_tag = sc->bus_tag;
    bus_space_handle_t bus_handle = sc->bus_handle;

    if (size == 4)
        return((u_int32_t)bus_space_read_4(bus_tag, bus_handle, offset));
    else if (size == 2)
            return((u_int32_t)bus_space_read_2(bus_tag, bus_handle, offset));
         else
            return((u_int32_t)bus_space_read_1(bus_tag, bus_handle, offset));
}

/* --------------------- Q service --------------------- */

/* 
 * intialize q  pointers with null.
 */
void
tws_init_qs(struct tws_softc *sc)
{

    mtx_lock(&sc->q_lock);
    for(int i=0;i<TWS_MAX_QS;i++) {
        sc->q_head[i] = NULL;
        sc->q_tail[i] = NULL;
    }
    mtx_unlock(&sc->q_lock);

}

/* called with lock held */
static void
tws_insert2_empty_q(struct tws_softc *sc, struct tws_request *req, 
                                u_int8_t q_type )
{

    mtx_assert(&sc->q_lock, MA_OWNED);
    req->next = req->prev = NULL;
    sc->q_head[q_type] = sc->q_tail[q_type] = req;

}

/* called with lock held */
void
tws_q_insert_head(struct tws_softc *sc, struct tws_request *req, 
                                u_int8_t q_type )
{

    mtx_assert(&sc->q_lock, MA_OWNED);
    if ( sc->q_head[q_type] == NULL ) {
        tws_insert2_empty_q(sc, req, q_type);
    } else {
        req->next = sc->q_head[q_type];
        req->prev = NULL;
        sc->q_head[q_type]->prev = req;
        sc->q_head[q_type] = req;
    }

}

/* called with lock held */
void
tws_q_insert_tail(struct tws_softc *sc, struct tws_request *req, 
                                u_int8_t q_type )
{

    mtx_assert(&sc->q_lock, MA_OWNED);
    if ( sc->q_tail[q_type] == NULL ) {
        tws_insert2_empty_q(sc, req, q_type);
    } else {
        req->prev = sc->q_tail[q_type];
        req->next = NULL;
        sc->q_tail[q_type]->next = req;
        sc->q_tail[q_type] = req;
    }

}

/* called with lock held */
struct tws_request *
tws_q_remove_head(struct tws_softc *sc, u_int8_t q_type )
{

    struct tws_request *r;

    mtx_assert(&sc->q_lock, MA_OWNED);
    r = sc->q_head[q_type];
    if ( !r ) 
        return(NULL);
    if ( r->next == NULL &&  r->prev == NULL ) {
        /* last element  */
        sc->q_head[q_type] = sc->q_tail[q_type] = NULL;
    } else {
        sc->q_head[q_type] = r->next;
        r->next->prev = NULL;
        r->next = NULL;
        r->prev = NULL;
    }
    return(r);
}

/* called with lock held */
struct tws_request *
tws_q_remove_tail(struct tws_softc *sc, u_int8_t q_type )
{

    struct tws_request *r;

    mtx_assert(&sc->q_lock, MA_OWNED);
    r = sc->q_tail[q_type];
    if ( !r ) 
        return(NULL);
    if ( r->next == NULL &&  r->prev == NULL ) {
        /* last element  */
        sc->q_head[q_type] = sc->q_tail[q_type] = NULL;
    } else {
        sc->q_tail[q_type] = r->prev;
        r->prev->next = NULL;
        r->next = NULL;
        r->prev = NULL;
    }
    return(r);
}

/* returns removed request if successful. return NULL otherwise */ 
/* called with lock held */
struct tws_request *
tws_q_remove_request(struct tws_softc *sc, struct tws_request *req,
                                 u_int8_t q_type )
{

    struct tws_request *r;

    mtx_assert(&sc->q_lock, MA_OWNED);
    if ( req == NULL ) {
        TWS_TRACE_DEBUG(sc, "null req", 0, q_type);
        return(NULL);
    }

    if ( req == sc->q_head[q_type] )
        return(tws_q_remove_head(sc, q_type));
    if ( req == sc->q_tail[q_type] )
        return(tws_q_remove_tail(sc, q_type));


    /* The given node is not at head or tail.
     * It's in the middle and there are more than
     * 2 elements on the q.
     */

    if ( req->next == NULL || req->prev == NULL ) {
        TWS_TRACE_DEBUG(sc, "invalid req", 0, q_type);
        return(NULL);
    }

/* debug only */
    r = sc->q_head[q_type];
    while ( r ) {
        if ( req == r )
            break;
        r = r->next;
    } 

    if ( !r ) {
        TWS_TRACE_DEBUG(sc, "req not in q", 0, req->request_id);
        return(NULL);
    }
/* debug end */

    req->prev->next = r->next;
    req->next->prev = r->prev;
    req->next = NULL;
    req->prev = NULL;
    return(req);
}

struct tws_sense *
tws_find_sense_from_mfa(struct tws_softc *sc, u_int64_t mfa)
{
    struct tws_sense *s;
    int i;
    TWS_TRACE_DEBUG(sc, "entry",sc,mfa);

    i = (mfa - sc->dma_mem_phys) / sizeof(struct tws_command_packet);
    if ( i>= 0 && i<tws_queue_depth) {
        s = &sc->sense_bufs[i];
        if ( mfa == s->hdr_pkt_phy )
            return(s);
    }

    TWS_TRACE_DEBUG(sc, "return null",0,mfa);
    return(NULL);

}

/* --------------------- Q service end --------------------- */
/* --------------------- misc service start --------------------- */


void
tws_print_stats(void *arg)
{

    struct tws_softc *sc = (struct tws_softc *)arg;
     
    TWS_TRACE(sc, "reqs(in, out)", sc->stats.reqs_in, sc->stats.reqs_out);
    TWS_TRACE(sc, "reqs(err, intrs)", sc->stats.reqs_errored
                                      , sc->stats.num_intrs);
    TWS_TRACE(sc, "reqs(ioctls, scsi)", sc->stats.ioctls
                                      , sc->stats.scsi_ios);
    callout_reset(&sc->stats_timer, 300 * hz, tws_print_stats, sc);
}
/* --------------------- misc service end --------------------- */
