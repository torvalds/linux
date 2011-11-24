//------------------------------------------------------------------------------
// <copyright file="htc.h" company="Atheros">
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

#ifndef __HTC_H__
#define __HTC_H__

#ifndef ATH_TARGET
#include "athstartpack.h"
#endif

#ifndef A_OFFSETOF
#define A_OFFSETOF(type,field) (unsigned long)(&(((type *)NULL)->field))
#endif

#define ASSEMBLE_UNALIGNED_UINT16(p,highbyte,lowbyte) \
        (((A_UINT16)(((A_UINT8 *)(p))[(highbyte)])) << 8 | (A_UINT16)(((A_UINT8 *)(p))[(lowbyte)]))

/* alignment independent macros (little-endian) to fetch UINT16s or UINT8s from a
 * structure using only the type and field name.
 * Use these macros if there is the potential for unaligned buffer accesses. */
#define A_GET_UINT16_FIELD(p,type,field) \
    ASSEMBLE_UNALIGNED_UINT16(p,\
                              A_OFFSETOF(type,field) + 1, \
                              A_OFFSETOF(type,field))

#define A_SET_UINT16_FIELD(p,type,field,value) \
{                                              \
    ((A_UINT8 *)(p))[A_OFFSETOF(type,field)] = (A_UINT8)(value);        \
    ((A_UINT8 *)(p))[A_OFFSETOF(type,field) + 1] = (A_UINT8)((value) >> 8); \
}

#define A_GET_UINT8_FIELD(p,type,field) \
            ((A_UINT8 *)(p))[A_OFFSETOF(type,field)]

#define A_SET_UINT8_FIELD(p,type,field,value) \
    ((A_UINT8 *)(p))[A_OFFSETOF(type,field)] = (value)

/****** DANGER DANGER ***************
 *
 *   The frame header length and message formats defined herein were
 *   selected to accommodate optimal alignment for target processing.  This reduces code
 *   size and improves performance.
 *
 *   Any changes to the header length may alter the alignment and cause exceptions
 *   on the target. When adding to the message structures insure that fields are
 *   properly aligned.
 *
 */

/* HTC frame header */
typedef PREPACK struct _HTC_FRAME_HDR{
        /* do not remove or re-arrange these fields, these are minimally required
         * to take advantage of 4-byte lookaheads in some hardware implementations */
    A_UINT8   EndpointID;
    A_UINT8   Flags;
    A_UINT16  PayloadLen;       /* length of data (including trailer) that follows the header */

    /***** end of 4-byte lookahead ****/

    A_UINT8   ControlBytes[2];

    /* message payload starts after the header */

} POSTPACK HTC_FRAME_HDR;

/* frame header flags */

    /* send direction */
#define HTC_FLAGS_NEED_CREDIT_UPDATE (1 << 0)
#define HTC_FLAGS_SEND_BUNDLE        (1 << 1)  /* start or part of bundle */
    /* receive direction */
#define HTC_FLAGS_RECV_UNUSED_0      (1 << 0)  /* bit 0 unused */
#define HTC_FLAGS_RECV_TRAILER       (1 << 1)  /* bit 1 trailer data present */
#define HTC_FLAGS_RECV_UNUSED_2      (1 << 0)  /* bit 2 unused */
#define HTC_FLAGS_RECV_UNUSED_3      (1 << 0)  /* bit 3 unused */
#define HTC_FLAGS_RECV_BUNDLE_CNT_MASK (0xF0)  /* bits 7..4  */
#define HTC_FLAGS_RECV_BUNDLE_CNT_SHIFT 4

#define HTC_HDR_LENGTH  (sizeof(HTC_FRAME_HDR))
#define HTC_MAX_TRAILER_LENGTH   255
#define HTC_MAX_PAYLOAD_LENGTH   (4096 - sizeof(HTC_FRAME_HDR))

/* HTC control message IDs */

#define HTC_MSG_READY_ID                    1
#define HTC_MSG_CONNECT_SERVICE_ID          2
#define HTC_MSG_CONNECT_SERVICE_RESPONSE_ID 3
#define HTC_MSG_SETUP_COMPLETE_ID           4
#define HTC_MSG_SETUP_COMPLETE_EX_ID        5

#define HTC_MAX_CONTROL_MESSAGE_LENGTH  256

/* base message ID header */
typedef PREPACK struct {
    A_UINT16 MessageID;
} POSTPACK HTC_UNKNOWN_MSG;

/* HTC ready message
 * direction : target-to-host  */
typedef PREPACK struct {
    A_UINT16  MessageID;    /* ID */
    A_UINT16  CreditCount;  /* number of credits the target can offer */
    A_UINT16  CreditSize;   /* size of each credit */
    A_UINT8   MaxEndpoints; /* maximum number of endpoints the target has resources for */
    A_UINT8   _Pad1;
} POSTPACK HTC_READY_MSG;

    /* extended HTC ready message */
typedef PREPACK struct {
    HTC_READY_MSG   Version2_0_Info;   /* legacy version 2.0 information at the front... */
    /* extended information */
    A_UINT8         HTCVersion;
    A_UINT8         MaxMsgsPerHTCBundle;
} POSTPACK HTC_READY_EX_MSG;

#define HTC_VERSION_2P0  0x00
#define HTC_VERSION_2P1  0x01  /* HTC 2.1 */

#define HTC_SERVICE_META_DATA_MAX_LENGTH 128

/* connect service
 * direction : host-to-target */
typedef PREPACK struct {
    A_UINT16  MessageID;
    A_UINT16  ServiceID;           /* service ID of the service to connect to */
    A_UINT16  ConnectionFlags;     /* connection flags */

#define HTC_CONNECT_FLAGS_REDUCE_CREDIT_DRIBBLE (1 << 2)  /* reduce credit dribbling when
                                                             the host needs credits */
#define HTC_CONNECT_FLAGS_THRESHOLD_LEVEL_MASK             (0x3)
#define HTC_CONNECT_FLAGS_THRESHOLD_LEVEL_ONE_FOURTH        0x0
#define HTC_CONNECT_FLAGS_THRESHOLD_LEVEL_ONE_HALF          0x1
#define HTC_CONNECT_FLAGS_THRESHOLD_LEVEL_THREE_FOURTHS     0x2
#define HTC_CONNECT_FLAGS_THRESHOLD_LEVEL_UNITY             0x3

    A_UINT8   ServiceMetaLength;   /* length of meta data that follows */
    A_UINT8   _Pad1;

    /* service-specific meta data starts after the header */

} POSTPACK HTC_CONNECT_SERVICE_MSG;

/* connect response
 * direction : target-to-host */
typedef PREPACK struct {
    A_UINT16  MessageID;
    A_UINT16  ServiceID;            /* service ID that the connection request was made */
    A_UINT8   Status;               /* service connection status */
    A_UINT8   EndpointID;           /* assigned endpoint ID */
    A_UINT16  MaxMsgSize;           /* maximum expected message size on this endpoint */
    A_UINT8   ServiceMetaLength;    /* length of meta data that follows */
    A_UINT8   _Pad1;

    /* service-specific meta data starts after the header */

} POSTPACK HTC_CONNECT_SERVICE_RESPONSE_MSG;

typedef PREPACK struct {
    A_UINT16  MessageID;
    /* currently, no other fields */
} POSTPACK HTC_SETUP_COMPLETE_MSG;

    /* extended setup completion message */
typedef PREPACK struct {
    A_UINT16  MessageID;
    A_UINT32  SetupFlags;
    A_UINT8   MaxMsgsPerBundledRecv;
    A_UINT8   Rsvd[3];
} POSTPACK HTC_SETUP_COMPLETE_EX_MSG;

#define HTC_SETUP_COMPLETE_FLAGS_ENABLE_BUNDLE_RECV     (1 << 0)

/* connect response status codes */
#define HTC_SERVICE_SUCCESS      0  /* success */
#define HTC_SERVICE_NOT_FOUND    1  /* service could not be found */
#define HTC_SERVICE_FAILED       2  /* specific service failed the connect */
#define HTC_SERVICE_NO_RESOURCES 3  /* no resources (i.e. no more endpoints) */
#define HTC_SERVICE_NO_MORE_EP   4  /* specific service is not allowing any more
                                       endpoints */

/* report record IDs */

#define HTC_RECORD_NULL             0
#define HTC_RECORD_CREDITS          1
#define HTC_RECORD_LOOKAHEAD        2
#define HTC_RECORD_LOOKAHEAD_BUNDLE 3

typedef PREPACK struct {
    A_UINT8 RecordID;     /* Record ID */
    A_UINT8 Length;       /* Length of record */
} POSTPACK HTC_RECORD_HDR;

typedef PREPACK struct {
    A_UINT8 EndpointID;     /* Endpoint that owns these credits */
    A_UINT8 Credits;        /* credits to report since last report */
} POSTPACK HTC_CREDIT_REPORT;

typedef PREPACK struct {
    A_UINT8 PreValid;         /* pre valid guard */
    A_UINT8 LookAhead[4];     /* 4 byte lookahead */
    A_UINT8 PostValid;        /* post valid guard */

   /* NOTE: the LookAhead array is guarded by a PreValid and Post Valid guard bytes.
    * The PreValid bytes must equal the inverse of the PostValid byte */

} POSTPACK HTC_LOOKAHEAD_REPORT;

typedef PREPACK struct {
    A_UINT8 LookAhead[4];     /* 4 byte lookahead */
} POSTPACK HTC_BUNDLED_LOOKAHEAD_REPORT;

#ifndef ATH_TARGET
#include "athendpack.h"
#endif


#endif /* __HTC_H__ */

