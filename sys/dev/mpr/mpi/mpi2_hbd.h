/*-
 *  Copyright 2000-2020 Broadcom Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 * Broadcom Inc. (LSI) MPT-Fusion Host Adapter FreeBSD
 *
 * $FreeBSD$
 */

/*
 *  Copyright 2000-2020 Broadcom Inc. All rights reserved.
 *
 *
 *           Name:  mpi2_hbd.h
 *          Title:  MPI Host Based Discovery messages and structures
 *  Creation Date:  October 21, 2009
 *
 *  mpi2_hbd.h Version:  02.00.04
 *
 *  NOTE: Names (typedefs, defines, etc.) beginning with an MPI25 or Mpi25
 *        prefix are for use only on MPI v2.5 products, and must not be used
 *        with MPI v2.0 products. Unless otherwise noted, names beginning with
 *        MPI2 or Mpi2 are for use with both MPI v2.0 and MPI v2.5 products.
 *
 *  Version History
 *  ---------------
 *
 *  Date      Version   Description
 *  --------  --------  ------------------------------------------------------
 *  10-28-09  02.00.00  Initial version.
 *  08-11-10  02.00.01  Removed PortGroups, DmaGroup, and ControlGroup from
 *                      HBD Action request, replaced by AdditionalInfo field.
 *  11-18-11  02.00.02  Incorporating additions for MPI v2.5.
 *  11-18-14  02.00.03  Updated copyright information.
 *  02-17-16  02.00.04  Added SAS 4 22.5 gbs speed support.
 *  --------------------------------------------------------------------------
 */

#ifndef MPI2_HBD_H
#define MPI2_HBD_H

/****************************************************************************
*  Host Based Discovery Action messages
****************************************************************************/

/* Host Based Discovery Action Request Message */
typedef struct _MPI2_HBD_ACTION_REQUEST
{
    U8                      Operation;          /* 0x00 */
    U8                      Reserved1;          /* 0x01 */
    U8                      ChainOffset;        /* 0x02 */
    U8                      Function;           /* 0x03 */
    U16                     DevHandle;          /* 0x04 */
    U8                      Reserved2;          /* 0x06 */
    U8                      MsgFlags;           /* 0x07 */
    U8                      VP_ID;              /* 0x08 */
    U8                      VF_ID;              /* 0x09 */
    U16                     Reserved3;          /* 0x0A */
    U32                     Reserved4;          /* 0x0C */
    U64                     SASAddress;         /* 0x10 */
    U32                     Reserved5;          /* 0x18 */
    U32                     HbdDeviceInfo;      /* 0x1C */
    U16                     ParentDevHandle;    /* 0x20 */
    U16                     MaxQDepth;          /* 0x22 */
    U8                      FirstPhyIdentifier; /* 0x24 */
    U8                      Port;               /* 0x25 */
    U8                      MaxConnections;     /* 0x26 */
    U8                      MaxRate;            /* 0x27 */
    U32                     AdditionalInfo;     /* 0x28 */
    U16                     InitialAWT;         /* 0x2C */
    U16                     Reserved7;          /* 0x2E */
    U32                     Reserved8;          /* 0x30 */
} MPI2_HBD_ACTION_REQUEST, MPI2_POINTER PTR_MPI2_HBD_ACTION_REQUEST,
  Mpi2HbdActionRequest_t, MPI2_POINTER pMpi2HbdActionRequest_t;

/* values for the Operation field */
#define MPI2_HBD_OP_ADD_DEVICE                  (0x01)
#define MPI2_HBD_OP_REMOVE_DEVICE               (0x02)
#define MPI2_HBD_OP_UPDATE_DEVICE               (0x03)

/* values for the HbdDeviceInfo field */
#define MPI2_HBD_DEVICE_INFO_VIRTUAL_DEVICE     (0x00004000)
#define MPI2_HBD_DEVICE_INFO_ATAPI_DEVICE       (0x00002000)
#define MPI2_HBD_DEVICE_INFO_DIRECT_ATTACH      (0x00000800)
#define MPI2_HBD_DEVICE_INFO_SSP_TARGET         (0x00000400)
#define MPI2_HBD_DEVICE_INFO_STP_TARGET         (0x00000200)
#define MPI2_HBD_DEVICE_INFO_SMP_TARGET         (0x00000100)
#define MPI2_HBD_DEVICE_INFO_SATA_DEVICE        (0x00000080)
#define MPI2_HBD_DEVICE_INFO_SSP_INITIATOR      (0x00000040)
#define MPI2_HBD_DEVICE_INFO_STP_INITIATOR      (0x00000020)
#define MPI2_HBD_DEVICE_INFO_SMP_INITIATOR      (0x00000010)
#define MPI2_HBD_DEVICE_INFO_SATA_HOST          (0x00000008)

#define MPI2_HBD_DEVICE_INFO_MASK_DEVICE_TYPE   (0x00000007)
#define MPI2_HBD_DEVICE_INFO_NO_DEVICE          (0x00000000)
#define MPI2_HBD_DEVICE_INFO_END_DEVICE         (0x00000001)
#define MPI2_HBD_DEVICE_INFO_EDGE_EXPANDER      (0x00000002)
#define MPI2_HBD_DEVICE_INFO_FANOUT_EXPANDER    (0x00000003)

/* values for the MaxRate field */
#define MPI2_HBD_MAX_RATE_MASK                  (0x0F)
#define MPI2_HBD_MAX_RATE_1_5                   (0x08)
#define MPI2_HBD_MAX_RATE_3_0                   (0x09)
#define MPI2_HBD_MAX_RATE_6_0                   (0x0A)
#define MPI25_HBD_MAX_RATE_12_0                 (0x0B)
#define MPI26_HBD_MAX_RATE_22_5                 (0x0C)


/* Host Based Discovery Action Reply Message */
typedef struct _MPI2_HBD_ACTION_REPLY
{
    U8                      Operation;          /* 0x00 */
    U8                      Reserved1;          /* 0x01 */
    U8                      MsgLength;          /* 0x02 */
    U8                      Function;           /* 0x03 */
    U16                     DevHandle;          /* 0x04 */
    U8                      Reserved2;          /* 0x06 */
    U8                      MsgFlags;           /* 0x07 */
    U8                      VP_ID;              /* 0x08 */
    U8                      VF_ID;              /* 0x09 */
    U16                     Reserved3;          /* 0x0A */
    U16                     Reserved4;          /* 0x0C */
    U16                     IOCStatus;          /* 0x0E */
    U32                     IOCLogInfo;         /* 0x10 */
} MPI2_HBD_ACTION_REPLY, MPI2_POINTER PTR_MPI2_HBD_ACTION_REPLY,
  Mpi2HbdActionReply_t, MPI2_POINTER pMpi2HbdActionReply_t;


#endif


