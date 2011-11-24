/*
 * Copyright (c) 2009 Atheros Communications Inc.
 * All rights reserved.

 *
 * 
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
 *
 *
 */

#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>


#include <a_config.h>
#include <a_osapi.h>
#include "a_types.h"
#include "a_hci.h"
#include "pal_api.h"
#include "paldebug.h"
#include "pal_util.h"



typedef struct  event_code_tbl_t {
    A_UINT8 event_code;
    A_UINT8 *evt_name;
}EVENT_CODE_TBL;

#define N(a) (sizeof(a) / sizeof(a[0]))

EVENT_CODE_TBL  evt_tbl[] = {
                    {PAL_COMMAND_COMPLETE_EVENT, (A_UINT8 *)"COMMAND_COMPLETE_EVENT"},
                    {PAL_COMMAND_STATUS_EVENT, (A_UINT8 *)"COMMAND_STATUS_EVENT"},
                    {PAL_HARDWARE_ERROR_EVENT, (A_UINT8 *)"HARDWARE_ERROR_EVENT"},
                    {PAL_FLUSH_OCCURRED_EVENT, (A_UINT8 *)"FLUSH_OCCURRED_EVENT"},
                    {PAL_LOOPBACK_EVENT, (A_UINT8 *)"LOOPBACK_EVENT"},
                    {PAL_BUFFER_OVERFLOW_EVENT, (A_UINT8 *)"BUFFER_OVERFLOW_EVENT"},
                    {PAL_QOS_VIOLATION_EVENT, (A_UINT8 *)"QOS_VIOLATION_EVENT"},
                    {PAL_CHANNEL_SELECT_EVENT, (A_UINT8 *)"CHANNEL_SELECT_EVENT"},
                    {PAL_PHYSICAL_LINK_COMPL_EVENT, (A_UINT8 *)"PAL_PHYSICAL_LINK_COMPL_EVENT"},
                    {PAL_LOGICAL_LINK_COMPL_EVENT, (A_UINT8 *)"PAL_LOGICAL_LINK_COMPL_EVENT"},
                    {PAL_DISCONNECT_LOGICAL_LINK_COMPL_EVENT, (A_UINT8 *)"PAL_DISCONNECT_LOGICAL_LINK_COMPL_EVENT"},
                    {PAL_DISCONNECT_PHYSICAL_LINK_EVENT, (A_UINT8 *)"DISCONNECT_PHYSICAL_LINK_EVENT"},
                    {PAL_FLOW_SPEC_MODIFY_COMPL_EVENT, (A_UINT8 *)"FLOW_SPEC_MODIFY_COMPL_EVENT"},
                    {PAL_NUM_COMPL_DATA_BLOCK_EVENT, (A_UINT8 *)"NUM_COMPL_DATA_BLOCK_EVENT"},
                    {PAL_SHORT_RANGE_MODE_CHANGE_COMPL_EVENT, (A_UINT8 *)"PAL_SHORT_RANGE_MODE_CHANGE_COMPL_EVENT"},
                    };

void
pal_decode_event(A_UINT8 *buf, A_UINT16 sz)
{
    A_UINT8 i;
    
    for(i = 0; i < (N(evt_tbl) - 1); i++) {
        if(evt_tbl[i].event_code == ((HCI_EVENT_PKT *)buf)->event_code)
            break;
    }

    PAL_PRINT("PAL Send event -> %s\n", evt_tbl[i].evt_name);
    dump_frame(buf, sz);
}

void
dump_frame(A_UINT8 *frm, A_UINT32 len)
{
    unsigned int    i;
    PAL_PRINT("\n----------------------------------------------\n");
    for(i = 0; i < len; i++) {
        PAL_PRINT("0x%02x ", frm[i]);
        if((i+1) % 16 == 0) 
            PAL_PRINT("\n");
    }
    PAL_PRINT("\n===============================================\n\n");
}


A_UINT32 log_param=0;
FILE    *fp = 0;
void PRINTF(char *format,...)
{
    char     buffer[2000]; /* Output Buffer */
    int      len;
    va_list  args;

    if(!log_param)
        return;

    va_start(args, format);
    len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (log_param & 0x1) {
        fwrite(buffer, sizeof(char), strlen(buffer), fp);
        fflush(fp);
    }

    if(log_param & 0x2) {
        printf("%s",buffer);
    }
}


void
pal_log_cfg(void * dev, A_UINT32 log_cfg)
{
    log_param = log_cfg;

    if(log_cfg & 0x1) {
        if ((fp = fopen("pal.log", "a")) == NULL) {
            printf("CAN NOT OPEN file\n");
            exit (1);
        }
    }
}
