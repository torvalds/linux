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


/* #define TWS_DEBUG on */

void tws_trace(const char *file, const char *fun, int linenum,
         struct tws_softc *sc,  char *desc, u_int64_t val1, u_int64_t val2);
void tws_log(struct tws_softc *sc, int index);
u_int32_t tws_read_reg(struct tws_softc *sc, 
                  int offset, int size);
void tws_write_reg(struct tws_softc *sc, int offset,
                  u_int32_t value, int size);

u_int16_t tws_swap16(u_int16_t val);
u_int32_t tws_swap32(u_int32_t val);
u_int64_t tws_swap64(u_int64_t val);

void tws_init_qs(struct tws_softc *sc);



/* ----------------- trace ----------------- */

#define TWS_TRACE_ON on /* Alawys on - use wisely to trace errors */

#ifdef TWS_DEBUG
    #define TWS_TRACE_DEBUG_ON on
#endif

#ifdef TWS_TRACE_DEBUG_ON
    #define TWS_TRACE_DEBUG(sc, desc, val1, val2) \
            tws_trace(__FILE__, __func__, __LINE__, sc, desc, \
                                   (u_int64_t)val1, (u_int64_t)val2)
#else
    #define TWS_TRACE_DEBUG(sc, desc, val1, val2)
#endif

#ifdef TWS_TRACE_ON
    #define TWS_TRACE(sc, desc, val1, val2) \
            tws_trace(__FILE__, __func__, __LINE__, sc, desc, \
                                   (u_int64_t)val1, (u_int64_t)val2)
#else
    #define TWS_TRACE(sc, desc, val1, val2)
#endif

/* ---------------- logging ---------------- */


/* ---------------- logging ---------------- */
enum error_index {
    SYSCTL_TREE_NODE_ADD,
    PCI_COMMAND_READ,
    ALLOC_MEMORY_RES,
    ALLOC_IRQ_RES,
    SETUP_INTR_RES,
    TWS_CAM_ATTACH,
    CAM_SIMQ_ALLOC,
    CAM_SIM_ALLOC,
    TWS_XPT_BUS_REGISTER,
    TWS_XPT_CREATE_PATH,
    TWS_BUS_SCAN_REQ,
    TWS_INIT_FAILURE,
    TWS_CTLR_INIT_FAILURE,
};

enum severity {
    ERROR = 1,
    WARNING,
    INFO,
#if 0
    DEBUG,
#endif
};

struct error_desc {
    char desc[256];
    u_int32_t error_code;
    int severity_level;
    char *fmt;
    char *error_str;
};

/* ----------- q services ------------- */

#define TWS_FREE_Q        0
#define TWS_PENDING_Q     1
#define TWS_BUSY_Q        2
#define TWS_COMPLETE_Q    3

/* req return codes */
#define TWS_REQ_RET_SUBMIT_SUCCESS 0
#define TWS_REQ_RET_PEND_NOMFA     1
#define TWS_REQ_RET_RESET          2
#define TWS_REQ_RET_INVALID   0xdead


/* ------------------------ */
#include <sys/clock.h>
#define TWS_LOCAL_TIME (time_second - utc_offset())
