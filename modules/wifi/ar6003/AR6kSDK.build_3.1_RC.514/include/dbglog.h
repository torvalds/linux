//------------------------------------------------------------------------------
// <copyright file="dbglog.h" company="Atheros">
//    Copyright (c) 2004-2010 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// Author(s): ="Atheros"
//==============================================================================

#ifndef _DBGLOG_H_
#define _DBGLOG_H_

#ifndef ATH_TARGET
#include "athstartpack.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define DBGLOG_TIMESTAMP_OFFSET          0
#define DBGLOG_TIMESTAMP_MASK            0x0000FFFF /* Bit 0-15. Contains bit
                                                       8-23 of the LF0 timer */
#define DBGLOG_DBGID_OFFSET              16
#define DBGLOG_DBGID_MASK                0x03FF0000 /* Bit 16-25 */
#define DBGLOG_DBGID_NUM_MAX             256 /* Upper limit is width of mask */

#define DBGLOG_MODULEID_OFFSET           26
#define DBGLOG_MODULEID_MASK             0x3C000000 /* Bit 26-29 */
#define DBGLOG_MODULEID_NUM_MAX          16 /* Upper limit is width of mask */

/*
 * Please ensure that the definition of any new module intrduced is captured
 * between the DBGLOG_MODULEID_START and DBGLOG_MODULEID_END defines. The 
 * structure is required for the parser to correctly pick up the values for
 * different modules.
 */
#define DBGLOG_MODULEID_START
#define DBGLOG_MODULEID_INF                   0
#define DBGLOG_MODULEID_WMI                   1
#define DBGLOG_MODULEID_MISC                  2
#define DBGLOG_MODULEID_PM                    3
#define DBGLOG_MODULEID_TXRX_MGMTBUF          4
#define DBGLOG_MODULEID_TXRX_TXBUF            5
#define DBGLOG_MODULEID_TXRX_RXBUF            6
#define DBGLOG_MODULEID_WOW                   7
#define DBGLOG_MODULEID_WHAL                  8
#define DBGLOG_MODULEID_DC                    9
#define DBGLOG_MODULEID_CO                    10
#define DBGLOG_MODULEID_RO                    11
#define DBGLOG_MODULEID_CM                    12
#define DBGLOG_MODULEID_MGMT                  13
#define DBGLOG_MODULEID_TMR                   14
#define DBGLOG_MODULEID_BTCOEX                15
#define DBGLOG_MODULEID_END

#define DBGLOG_NUM_ARGS_OFFSET             30
#define DBGLOG_NUM_ARGS_MASK               0xC0000000 /* Bit 30-31 */
#define DBGLOG_NUM_ARGS_MAX                2 /* Upper limit is width of mask */

#define DBGLOG_MODULE_LOG_ENABLE_OFFSET    0
#define DBGLOG_MODULE_LOG_ENABLE_MASK      0x0000FFFF

#define DBGLOG_REPORTING_ENABLED_OFFSET    16
#define DBGLOG_REPORTING_ENABLED_MASK      0x00010000

#define DBGLOG_TIMESTAMP_RESOLUTION_OFFSET 17
#define DBGLOG_TIMESTAMP_RESOLUTION_MASK   0x000E0000

#define DBGLOG_REPORT_SIZE_OFFSET          20
#define DBGLOG_REPORT_SIZE_MASK            0x3FF00000

#define DBGLOG_LOG_BUFFER_SIZE             1500
#define DBGLOG_DBGID_DEFINITION_LEN_MAX    90

PREPACK struct dbglog_buf_s {
    struct dbglog_buf_s *next;
    A_UINT8             *buffer;
    A_UINT32             bufsize;
    A_UINT32             length;
    A_UINT32             count;
    A_UINT32             free;
} POSTPACK;

PREPACK struct dbglog_hdr_s {
    struct dbglog_buf_s *dbuf;
    A_UINT32             dropped;
} POSTPACK;

PREPACK struct dbglog_config_s {
    A_UINT32                    cfgvalid; /* Mask with valid config bits */
    union {
        /* TODO: Take care of endianness */
        struct {
            A_UINT32            mmask:16; /* Mask of modules with logging on */
            A_UINT32            rep:1; /* Reporting enabled or not */
            A_UINT32            tsr:3; /* Time stamp resolution. Def: 1 ms */
            A_UINT32            size:10; /* Report size in number of messages */
            A_UINT32            reserved:2;
        } dbglog_config;

        A_UINT32                value;
    } u;
} POSTPACK;

#define cfgmmask                   u.dbglog_config.mmask
#define cfgrep                     u.dbglog_config.rep
#define cfgtsr                     u.dbglog_config.tsr
#define cfgsize                    u.dbglog_config.size
#define cfgvalue                   u.value

#ifdef __cplusplus
}
#endif

#ifndef ATH_TARGET
#include "athendpack.h"
#endif

#endif /* _DBGLOG_H_ */
