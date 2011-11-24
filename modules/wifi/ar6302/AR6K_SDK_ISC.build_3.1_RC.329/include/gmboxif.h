//------------------------------------------------------------------------------
// Copyright (c) 2009-2010 Atheros Corporation.  All rights reserved.
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

#ifndef __GMBOXIF_H__
#define __GMBOXIF_H__

#ifndef ATH_TARGET
#include "athstartpack.h"
#endif

/* GMBOX interface definitions */
    
#define AR6K_GMBOX_CREDIT_COUNTER       1   /* we use credit counter 1 to track credits */
#define AR6K_GMBOX_CREDIT_SIZE_COUNTER  2   /* credit counter 2 is used to pass the size of each credit */


    /* HCI UART transport definitions when used over GMBOX interface */
#define HCI_UART_COMMAND_PKT 0x01
#define HCI_UART_ACL_PKT     0x02
#define HCI_UART_SCO_PKT     0x03
#define HCI_UART_EVENT_PKT   0x04

    /* definitions for BT HCI packets */
typedef PREPACK struct {
    A_UINT16 Flags_ConnHandle;
    A_UINT16 Length;
} POSTPACK BT_HCI_ACL_HEADER;

typedef PREPACK struct {
    A_UINT16 Flags_ConnHandle;
    A_UINT8  Length;
} POSTPACK BT_HCI_SCO_HEADER;

typedef PREPACK struct {
    A_UINT16 OpCode;
    A_UINT8  ParamLength;
} POSTPACK BT_HCI_COMMAND_HEADER;

typedef PREPACK struct {
    A_UINT8  EventCode;
    A_UINT8  ParamLength;
} POSTPACK BT_HCI_EVENT_HEADER;

/* MBOX host interrupt signal assignments */

#define MBOX_SIG_HCI_BRIDGE_MAX      8
#define MBOX_SIG_HCI_BRIDGE_BT_ON    0
#define MBOX_SIG_HCI_BRIDGE_BT_OFF   1
#define MBOX_SIG_HCI_BRIDGE_BAUD_SET 2
#define MBOX_SIG_HCI_BRIDGE_PWR_SAV_ON    3
#define MBOX_SIG_HCI_BRIDGE_PWR_SAV_OFF   4

/* Host interrupts target to change baud rate and 
 * baud rate info is stored in scratch registers 4 and 5
 */
#define LSB_SCRATCH_IDX     4
#define MSB_SCRATCH_IDX     5

#ifndef ATH_TARGET
#include "athendpack.h"
#endif

#endif /* __GMBOXIF_H__ */

