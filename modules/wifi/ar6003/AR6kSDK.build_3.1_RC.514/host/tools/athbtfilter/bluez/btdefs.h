//------------------------------------------------------------------------------
// <copyright file="btdefs.h" company="Atheros">
//    Copyright (c) 2007 Atheros Corporation.  All rights reserved.
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
// Bluetooth spec definitions
//
// Author(s): ="Atheros"
//==============================================================================


#ifndef BTDEFS_H_
#define BTDEFS_H_

#define OGF_SHIFT                   10
#define OGF_MASK                    0xFC

#define MAKE_HCI_COMMAND(ogf,ocf)   (((ogf) << OGF_SHIFT) | (ocf))
#define HCI_GET_OP_CODE(p)          (((A_UINT16)((p)[1])) << 8) | ((A_UINT16)((p)[0]))
#define HCI_TEST_OGF(p,ogf)         (((p)[1] & OGF_MASK) == ((ogf) << 2))

#define HCI_LINK_CONTROL_OGF        0x01
#define IS_LINK_CONTROL_CMD(p)      HCI_TEST_OGF(p,HCI_LINK_CONTROL_OGF)
#define HCI_INQUIRY                 MAKE_HCI_COMMAND(HCI_LINK_CONTROL_OGF, 0x0001)
#define HCI_INQUIRY_CANCEL          MAKE_HCI_COMMAND(HCI_LINK_CONTROL_OGF, 0x0002)
#define HCI_PER_INQUIRY             MAKE_HCI_COMMAND(HCI_LINK_CONTROL_OGF, 0x0003)
#define HCI_PER_INQUIRY_CANCEL      MAKE_HCI_COMMAND(HCI_LINK_CONTROL_OGF, 0x0004)
#define HCI_CREATE_CONNECTION       MAKE_HCI_COMMAND(HCI_LINK_CONTROL_OGF, 0x0005)
#define HCI_DISCONNECT              MAKE_HCI_COMMAND(HCI_LINK_CONTROL_OGF, 0x0006)
#define HCI_ADD_SCO                 MAKE_HCI_COMMAND(HCI_LINK_CONTROL_OGF, 0x0007)
#define HCI_ACCEPT_CONN_REQ         MAKE_HCI_COMMAND(HCI_LINK_CONTROL_OGF, 0x0009)
#define HCI_REJECT_CONN_REQ         MAKE_HCI_COMMAND(HCI_LINK_CONTROL_OGF, 0x000A)
#define HCI_SETUP_SCO_CONN          MAKE_HCI_COMMAND(HCI_LINK_CONTROL_OGF, 0x0028) /* BT 2.0 */

//#define HCI_    MAKE_HCI_COMMAND(HCI_LINK_CONTROL_OGF, x)

#define HCI_GET_EVENT_CODE(p)       ((p)[0])
#define GET_BT_EVENT_LENGTH(p)      ((p)[1])
#define HCI_EVT_INQUIRY_COMPLETE    	0x01
#define HCI_EVT_CONNECT_COMPLETE   	 	0x03
#define HCI_EVT_CONNECT_REQUEST     	0x04
#define HCI_EVT_REMOTE_DEV_LMP_VERSION  0x0b
#define HCI_EVT_REMOTE_DEV_VERSION		0x0c
#define HCI_EVT_DISCONNECT          	0x05
#define HCI_EVT_REMOTE_NAME_REQ     	0x07
#define HCI_EVT_ROLE_CHANGE         	0x12
#define HCI_EVT_NUM_COMPLETED_PKTS  	0x13
#define HCI_EVT_MODE_CHANGE         0x14
#define HCI_EVT_SCO_CONNECT_COMPLETE 0x2C  /* new to 2.0 */


#define HCI_CMD_OPCODE_INQUIRY_START   0x401
#define HCI_CMD_OPCODE_INQUIRY_CANCEL  0x402
#define HCI_CMD_OPCODE_CONNECT	       0x405

/* HCI Connection Complete Event macros */
#define GET_BT_CONN_EVENT_STATUS(p) ((p)[2])
#define GET_BT_CONN_HANDLE(p)       ((A_UINT16)((p)[3]) | (((A_UINT16)((p)[4])) << 8))
#define GET_BT_CONN_LINK_TYPE(p)    ((p)[11])
#define BT_CONN_EVENT_STATUS_SUCCESS(p) (GET_BT_CONN_EVENT_STATUS(p) == 0)
#define INVALID_BT_CONN_HANDLE      0xFFFF
#define BT_LINK_TYPE_SCO            0x00
#define BT_LINK_TYPE_ACL            0x01
#define BT_LINK_TYPE_ESCO           0x02


/* SCO Connection Complete Event macros */
#define GET_TRANS_INTERVAL(p)   ((p)[12])
#define GET_RETRANS_INTERVAL(p) ((p)[13])
#define GET_RX_PKT_LEN(p)       ((A_UINT16)((p)[14]) | (((A_UINT16)((p)[15])) << 8))
#define GET_TX_PKT_LEN(p)       ((A_UINT16)((p)[16]) | (((A_UINT16)((p)[17])) << 8))


/* L2CAP Definitions */
#define SIGNALING        0x0001
#define CONNECTIONLESS   0x0002
#define NULL_ID          0x0000

#define CONNECT_REQ      0x02
#define CONNECT_RSP      0x03
#define DISCONNECT_REQ   0x06
#define DISCONNECT_RSP   0x07

#define STATE_SUCCESS 0
#define STATE_PENDING 1

#define STATE_DISCONNECT 0x00
#define STATE_CONNECTING 0x01
#define STATE_CONNECTED  0x02

#define TYPE_ACPT 0x02
#define TYPE_REJ  0x03

#define A2DP_TYPE      0x0019
#define RFCOMM_TYPE    0x0003
#define SDP_TYPE       0x0001
#define AVDTP_START   0x07
#define AVDTP_SUSPEND 0x08
#define AVDTP_CLOSE   0x09
#define AVDTP_OPEN    0x06

#define GETUINT16(p)(((A_UINT16)((p)[1])) << 8) | ((A_UINT16)((p)[0]))
    
#include "athstartpack.h"

typedef PREPACK struct _ACL_HEADER{
    A_UINT16 HANDLE;
    A_UINT16 Length;
} POSTPACK ACL_HEADER, *PACL_HEADER;

typedef PREPACK struct _L2CAP_HEADER{
    A_UINT16 Length;
    A_UINT16 CID;
} POSTPACK L2CAP_HEADER, *PL2CAP_HEADER;

typedef PREPACK struct _L2CAP_CONTROL{
    A_UINT8  CODE;
    A_UINT8  ID;
    A_UINT16 Length;
    A_UINT16 PSM;
    A_UINT16 DESTINATION_CID;
    A_UINT16 SOURCE_CID;
    A_UINT16 RESULT;
    A_UINT16 STATUS;
} POSTPACK L2CAP_CONTROL, *PL2CAP_CONTROL;

typedef PREPACK struct _AVDTP_HEADER{
    A_UINT8  MESSAGE_TYPE;
    A_UINT8  CMD_ID;
} POSTPACK AVDTP_HEADER, *PAVDTP_HEADER;

#include "athendpack.h"

#endif /*BTDEFS_H_*/



