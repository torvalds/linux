/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006-2015 LSI Corp.
 * Copyright (c) 2013-2015 Avago Technologies
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 * Avago Technologies (LSI) MPT-Fusion Host Adapter FreeBSD
 *
 * $FreeBSD$
 */

/*
 *  Copyright (c) 2006-2015 LSI Corporation.
 *  Copyright (c) 2013-2015 Avago Technologies
 *
 *
 *           Name:  mpi2_targ.h
 *          Title:  MPI Target mode messages and structures
 *  Creation Date:  September 8, 2006
 *
 *    mpi2_targ.h Version: 02.00.04
 *
 *  Version History
 *  ---------------
 *
 *  Date      Version   Description
 *  --------  --------  ------------------------------------------------------
 *  04-30-07  02.00.00  Corresponds to Fusion-MPT MPI Specification Rev A.
 *  08-31-07  02.00.01  Added Command Buffer Data Location Address Space bits to
 *                      BufferPostFlags field of CommandBufferPostBase Request.
 *  02-29-08  02.00.02  Modified various names to make them 32-character unique.
 *  10-02-08  02.00.03  Removed NextCmdBufferOffset from
 *                      MPI2_TARGET_CMD_BUF_POST_BASE_REQUEST.
 *                      Target Status Send Request only takes a single SGE for
 *                      response data.
 *  02-10-10  02.00.04  Added comment to MPI2_TARGET_SSP_RSP_IU structure.
 *  --------------------------------------------------------------------------
 */

#ifndef MPI2_TARG_H
#define MPI2_TARG_H


/******************************************************************************
*
*        SCSI Target Messages
*
*******************************************************************************/

/****************************************************************************
*  Target Command Buffer Post Base Request
****************************************************************************/

typedef struct _MPI2_TARGET_CMD_BUF_POST_BASE_REQUEST
{
    U8                      BufferPostFlags;        /* 0x00 */
    U8                      Reserved1;              /* 0x01 */
    U8                      ChainOffset;            /* 0x02 */
    U8                      Function;               /* 0x03 */
    U16                     TotalCmdBuffers;        /* 0x04 */
    U8                      Reserved;               /* 0x06 */
    U8                      MsgFlags;               /* 0x07 */
    U8                      VP_ID;                  /* 0x08 */
    U8                      VF_ID;                  /* 0x09 */
    U16                     Reserved2;              /* 0x0A */
    U32                     Reserved3;              /* 0x0C */
    U16                     CmdBufferLength;        /* 0x10 */
    U16                     Reserved4;              /* 0x12 */
    U32                     BaseAddressLow;         /* 0x14 */
    U32                     BaseAddressHigh;        /* 0x18 */
} MPI2_TARGET_CMD_BUF_POST_BASE_REQUEST,
  MPI2_POINTER PTR_MPI2_TARGET_CMD_BUF_POST_BASE_REQUEST,
  Mpi2TargetCmdBufferPostBaseRequest_t,
  MPI2_POINTER pMpi2TargetCmdBufferPostBaseRequest_t;

/* values for the BufferPostflags field */
#define MPI2_CMD_BUF_POST_BASE_ADDRESS_SPACE_MASK            (0x0C)
#define MPI2_CMD_BUF_POST_BASE_SYSTEM_ADDRESS_SPACE          (0x00)
#define MPI2_CMD_BUF_POST_BASE_IOCDDR_ADDRESS_SPACE          (0x04)
#define MPI2_CMD_BUF_POST_BASE_IOCPLB_ADDRESS_SPACE          (0x08)
#define MPI2_CMD_BUF_POST_BASE_IOCPLBNTA_ADDRESS_SPACE       (0x0C)

#define MPI2_CMD_BUF_POST_BASE_FLAGS_AUTO_POST_ALL           (0x01)


/****************************************************************************
*  Target Command Buffer Post List Request
****************************************************************************/

typedef struct _MPI2_TARGET_CMD_BUF_POST_LIST_REQUEST
{
    U16                     Reserved;               /* 0x00 */
    U8                      ChainOffset;            /* 0x02 */
    U8                      Function;               /* 0x03 */
    U16                     CmdBufferCount;         /* 0x04 */
    U8                      Reserved1;              /* 0x06 */
    U8                      MsgFlags;               /* 0x07 */
    U8                      VP_ID;                  /* 0x08 */
    U8                      VF_ID;                  /* 0x09 */
    U16                     Reserved2;              /* 0x0A */
    U32                     Reserved3;              /* 0x0C */
    U16                     IoIndex[2];             /* 0x10 */
} MPI2_TARGET_CMD_BUF_POST_LIST_REQUEST,
  MPI2_POINTER PTR_MPI2_TARGET_CMD_BUF_POST_LIST_REQUEST,
  Mpi2TargetCmdBufferPostListRequest_t,
  MPI2_POINTER pMpi2TargetCmdBufferPostListRequest_t;

/****************************************************************************
*  Target Command Buffer Post Base List Reply
****************************************************************************/

typedef struct _MPI2_TARGET_BUF_POST_BASE_LIST_REPLY
{
    U8                      Flags;                  /* 0x00 */
    U8                      Reserved;               /* 0x01 */
    U8                      MsgLength;              /* 0x02 */
    U8                      Function;               /* 0x03 */
    U16                     Reserved1;              /* 0x04 */
    U8                      Reserved2;              /* 0x06 */
    U8                      MsgFlags;               /* 0x07 */
    U8                      VP_ID;                  /* 0x08 */
    U8                      VF_ID;                  /* 0x09 */
    U16                     Reserved3;              /* 0x0A */
    U16                     Reserved4;              /* 0x0C */
    U16                     IOCStatus;              /* 0x0E */
    U32                     IOCLogInfo;             /* 0x10 */
    U16                     IoIndex;                /* 0x14 */
    U16                     Reserved5;              /* 0x16 */
    U32                     Reserved6;              /* 0x18 */
} MPI2_TARGET_BUF_POST_BASE_LIST_REPLY,
  MPI2_POINTER PTR_MPI2_TARGET_BUF_POST_BASE_LIST_REPLY,
  Mpi2TargetCmdBufferPostBaseListReply_t,
  MPI2_POINTER pMpi2TargetCmdBufferPostBaseListReply_t;

/* Flags defines */
#define MPI2_CMD_BUF_POST_REPLY_IOINDEX_VALID       (0x01)


/****************************************************************************
*  Command Buffer Formats (with 16 byte CDB)
****************************************************************************/

typedef struct _MPI2_TARGET_SSP_CMD_BUFFER
{
    U8      FrameType;                                  /* 0x00 */
    U8      Reserved1;                                  /* 0x01 */
    U16     InitiatorConnectionTag;                     /* 0x02 */
    U32     HashedSourceSASAddress;                     /* 0x04 */
    U16     Reserved2;                                  /* 0x08 */
    U16     Flags;                                      /* 0x0A */
    U32     Reserved3;                                  /* 0x0C */
    U16     Tag;                                        /* 0x10 */
    U16     TargetPortTransferTag;                      /* 0x12 */
    U32     DataOffset;                                 /* 0x14 */
    /* COMMAND information unit starts here */
    U8      LogicalUnitNumber[8];                       /* 0x18 */
    U8      Reserved4;                                  /* 0x20 */
    U8      TaskAttribute; /* lower 3 bits */           /* 0x21 */
    U8      Reserved5;                                  /* 0x22 */
    U8      AdditionalCDBLength; /* upper 5 bits */     /* 0x23 */
    U8      CDB[16];                                    /* 0x24 */
    /* Additional CDB bytes extend past the CDB field */
} MPI2_TARGET_SSP_CMD_BUFFER, MPI2_POINTER PTR_MPI2_TARGET_SSP_CMD_BUFFER,
  Mpi2TargetSspCmdBuffer, MPI2_POINTER pMp2iTargetSspCmdBuffer;

typedef struct _MPI2_TARGET_SSP_TASK_BUFFER
{
    U8      FrameType;                                  /* 0x00 */
    U8      Reserved1;                                  /* 0x01 */
    U16     InitiatorConnectionTag;                     /* 0x02 */
    U32     HashedSourceSASAddress;                     /* 0x04 */
    U16     Reserved2;                                  /* 0x08 */
    U16     Flags;                                      /* 0x0A */
    U32     Reserved3;                                  /* 0x0C */
    U16     Tag;                                        /* 0x10 */
    U16     TargetPortTransferTag;                      /* 0x12 */
    U32     DataOffset;                                 /* 0x14 */
    /* TASK information unit starts here */
    U8      LogicalUnitNumber[8];                       /* 0x18 */
    U16     Reserved4;                                  /* 0x20 */
    U8      TaskManagementFunction;                     /* 0x22 */
    U8      Reserved5;                                  /* 0x23 */
    U16     ManagedTaskTag;                             /* 0x24 */
    U16     Reserved6;                                  /* 0x26 */
    U32     Reserved7;                                  /* 0x28 */
    U32     Reserved8;                                  /* 0x2C */
    U32     Reserved9;                                  /* 0x30 */
} MPI2_TARGET_SSP_TASK_BUFFER, MPI2_POINTER PTR_MPI2_TARGET_SSP_TASK_BUFFER,
  Mpi2TargetSspTaskBuffer, MPI2_POINTER pMpi2TargetSspTaskBuffer;

/* mask and shift for HashedSourceSASAddress field */
#define MPI2_TARGET_HASHED_SAS_ADDRESS_MASK     (0xFFFFFF00)
#define MPI2_TARGET_HASHED_SAS_ADDRESS_SHIFT    (8)


/****************************************************************************
*   Target Assist Request
****************************************************************************/

typedef struct _MPI2_TARGET_ASSIST_REQUEST
{
    U8                  Reserved1;                          /* 0x00 */
    U8                  TargetAssistFlags;                  /* 0x01 */
    U8                  ChainOffset;                        /* 0x02 */
    U8                  Function;                           /* 0x03 */
    U16                 QueueTag;                           /* 0x04 */
    U8                  Reserved2;                          /* 0x06 */
    U8                  MsgFlags;                           /* 0x07 */
    U8                  VP_ID;                              /* 0x08 */
    U8                  VF_ID;                              /* 0x09 */
    U16                 Reserved3;                          /* 0x0A */
    U16                 IoIndex;                            /* 0x0C */
    U16                 InitiatorConnectionTag;             /* 0x0E */
    U16                 SGLFlags;                           /* 0x10 */
    U8                  SequenceNumber;                     /* 0x12 */
    U8                  Reserved4;                          /* 0x13 */
    U8                  SGLOffset0;                         /* 0x14 */
    U8                  SGLOffset1;                         /* 0x15 */
    U8                  SGLOffset2;                         /* 0x16 */
    U8                  SGLOffset3;                         /* 0x17 */
    U32                 SkipCount;                          /* 0x18 */
    U32                 DataLength;                         /* 0x1C */
    U32                 BidirectionalDataLength;            /* 0x20 */
    U16                 IoFlags;                            /* 0x24 */
    U16                 EEDPFlags;                          /* 0x26 */
    U32                 EEDPBlockSize;                      /* 0x28 */
    U32                 SecondaryReferenceTag;              /* 0x2C */
    U16                 SecondaryApplicationTag;            /* 0x30 */
    U16                 ApplicationTagTranslationMask;      /* 0x32 */
    U32                 PrimaryReferenceTag;                /* 0x34 */
    U16                 PrimaryApplicationTag;              /* 0x38 */
    U16                 PrimaryApplicationTagMask;          /* 0x3A */
    U32                 RelativeOffset;                     /* 0x3C */
    U32                 Reserved5;                          /* 0x40 */
    U32                 Reserved6;                          /* 0x44 */
    U32                 Reserved7;                          /* 0x48 */
    U32                 Reserved8;                          /* 0x4C */
    MPI2_SGE_IO_UNION   SGL[1];                             /* 0x50 */
} MPI2_TARGET_ASSIST_REQUEST, MPI2_POINTER PTR_MPI2_TARGET_ASSIST_REQUEST,
  Mpi2TargetAssistRequest_t, MPI2_POINTER pMpi2TargetAssistRequest_t;

/* Target Assist TargetAssistFlags bits */

#define MPI2_TARGET_ASSIST_FLAGS_REPOST_CMD_BUFFER      (0x80)
#define MPI2_TARGET_ASSIST_FLAGS_TLR                    (0x10)
#define MPI2_TARGET_ASSIST_FLAGS_RETRANSMIT             (0x04)
#define MPI2_TARGET_ASSIST_FLAGS_AUTO_STATUS            (0x02)
#define MPI2_TARGET_ASSIST_FLAGS_DATA_DIRECTION         (0x01)

/* Target Assist SGLFlags bits */

/* base values for Data Location Address Space */
#define MPI2_TARGET_ASSIST_SGLFLAGS_ADDR_MASK           (0x0C)
#define MPI2_TARGET_ASSIST_SGLFLAGS_SYSTEM_ADDR         (0x00)
#define MPI2_TARGET_ASSIST_SGLFLAGS_IOCDDR_ADDR         (0x04)
#define MPI2_TARGET_ASSIST_SGLFLAGS_IOCPLB_ADDR         (0x08)
#define MPI2_TARGET_ASSIST_SGLFLAGS_PLBNTA_ADDR         (0x0C)

/* base values for Type */
#define MPI2_TARGET_ASSIST_SGLFLAGS_TYPE_MASK           (0x03)
#define MPI2_TARGET_ASSIST_SGLFLAGS_MPI_TYPE            (0x00)
#define MPI2_TARGET_ASSIST_SGLFLAGS_32IEEE_TYPE         (0x01)
#define MPI2_TARGET_ASSIST_SGLFLAGS_64IEEE_TYPE         (0x02)

/* shift values for each sub-field */
#define MPI2_TARGET_ASSIST_SGLFLAGS_SGL3_SHIFT          (12)
#define MPI2_TARGET_ASSIST_SGLFLAGS_SGL2_SHIFT          (8)
#define MPI2_TARGET_ASSIST_SGLFLAGS_SGL1_SHIFT          (4)
#define MPI2_TARGET_ASSIST_SGLFLAGS_SGL0_SHIFT          (0)

/* Target Assist IoFlags bits */

#define MPI2_TARGET_ASSIST_IOFLAGS_BIDIRECTIONAL        (0x0800)
#define MPI2_TARGET_ASSIST_IOFLAGS_MULTICAST            (0x0400)
#define MPI2_TARGET_ASSIST_IOFLAGS_RECEIVE_FIRST        (0x0200)

/* Target Assist EEDPFlags bits */

#define MPI2_TA_EEDPFLAGS_INC_PRI_REFTAG            (0x8000)
#define MPI2_TA_EEDPFLAGS_INC_SEC_REFTAG            (0x4000)
#define MPI2_TA_EEDPFLAGS_INC_PRI_APPTAG            (0x2000)
#define MPI2_TA_EEDPFLAGS_INC_SEC_APPTAG            (0x1000)

#define MPI2_TA_EEDPFLAGS_CHECK_REFTAG              (0x0400)
#define MPI2_TA_EEDPFLAGS_CHECK_APPTAG              (0x0200)
#define MPI2_TA_EEDPFLAGS_CHECK_GUARD               (0x0100)

#define MPI2_TA_EEDPFLAGS_PASSTHRU_REFTAG           (0x0008)

#define MPI2_TA_EEDPFLAGS_MASK_OP                   (0x0007)
#define MPI2_TA_EEDPFLAGS_NOOP_OP                   (0x0000)
#define MPI2_TA_EEDPFLAGS_CHECK_OP                  (0x0001)
#define MPI2_TA_EEDPFLAGS_STRIP_OP                  (0x0002)
#define MPI2_TA_EEDPFLAGS_CHECK_REMOVE_OP           (0x0003)
#define MPI2_TA_EEDPFLAGS_INSERT_OP                 (0x0004)
#define MPI2_TA_EEDPFLAGS_REPLACE_OP                (0x0006)
#define MPI2_TA_EEDPFLAGS_CHECK_REGEN_OP            (0x0007)


/****************************************************************************
*  Target Status Send Request
****************************************************************************/

typedef struct _MPI2_TARGET_STATUS_SEND_REQUEST
{
    U8                      Reserved1;                  /* 0x00 */
    U8                      StatusFlags;                /* 0x01 */
    U8                      ChainOffset;                /* 0x02 */
    U8                      Function;                   /* 0x03 */
    U16                     QueueTag;                   /* 0x04 */
    U8                      Reserved2;                  /* 0x06 */
    U8                      MsgFlags;                   /* 0x07 */
    U8                      VP_ID;                      /* 0x08 */
    U8                      VF_ID;                      /* 0x09 */
    U16                     Reserved3;                  /* 0x0A */
    U16                     IoIndex;                    /* 0x0C */
    U16                     InitiatorConnectionTag;     /* 0x0E */
    U16                     SGLFlags;                   /* 0x10 */
    U16                     Reserved4;                  /* 0x12 */
    U8                      SGLOffset0;                 /* 0x14 */
    U8                      Reserved5;                  /* 0x15 */
    U16                     Reserved6;                  /* 0x16 */
    U32                     Reserved7;                  /* 0x18 */
    U32                     Reserved8;                  /* 0x1C */
    MPI2_SIMPLE_SGE_UNION   StatusDataSGE;              /* 0x20 */
} MPI2_TARGET_STATUS_SEND_REQUEST,
  MPI2_POINTER PTR_MPI2_TARGET_STATUS_SEND_REQUEST,
  Mpi2TargetStatusSendRequest_t, MPI2_POINTER pMpi2TargetStatusSendRequest_t;

/* Target Status Send StatusFlags bits */

#define MPI2_TSS_FLAGS_REPOST_CMD_BUFFER            (0x80)
#define MPI2_TSS_FLAGS_RETRANSMIT                   (0x04)
#define MPI2_TSS_FLAGS_AUTO_GOOD_STATUS             (0x01)

/* Target Status Send SGLFlags bits */
/* Data Location Address Space */
#define MPI2_TSS_SGLFLAGS_ADDR_MASK                 (0x0C)
#define MPI2_TSS_SGLFLAGS_SYSTEM_ADDR               (0x00)
#define MPI2_TSS_SGLFLAGS_IOCDDR_ADDR               (0x04)
#define MPI2_TSS_SGLFLAGS_IOCPLB_ADDR               (0x08)
#define MPI2_TSS_SGLFLAGS_IOCPLBNTA_ADDR            (0x0C)
/* Type */
#define MPI2_TSS_SGLFLAGS_TYPE_MASK                 (0x03)
#define MPI2_TSS_SGLFLAGS_MPI_TYPE                  (0x00)
#define MPI2_TSS_SGLFLAGS_IEEE32_TYPE               (0x01)
#define MPI2_TSS_SGLFLAGS_IEEE64_TYPE               (0x02)



/*
 * NOTE: The SSP status IU is big-endian. When used on a little-endian system,
 * this structure properly orders the bytes.
 */
typedef struct _MPI2_TARGET_SSP_RSP_IU
{
    U32     Reserved0[6]; /* reserved for SSP header */ /* 0x00 */

    /* start of RESPONSE information unit */
    U32     Reserved1;                                  /* 0x18 */
    U32     Reserved2;                                  /* 0x1C */
    U16     Reserved3;                                  /* 0x20 */
    U8      DataPres; /* lower 2 bits */                /* 0x22 */
    U8      Status;                                     /* 0x23 */
    U32     Reserved4;                                  /* 0x24 */
    U32     SenseDataLength;                            /* 0x28 */
    U32     ResponseDataLength;                         /* 0x2C */

    /* start of Response or Sense Data (size may vary dynamically) */
    U8      ResponseSenseData[4];                       /* 0x30 */
} MPI2_TARGET_SSP_RSP_IU, MPI2_POINTER PTR_MPI2_TARGET_SSP_RSP_IU,
  Mpi2TargetSspRspIu_t, MPI2_POINTER pMpi2TargetSspRspIu_t;


/****************************************************************************
*  Target Standard Reply - used with Target Assist or Target Status Send
****************************************************************************/

typedef struct _MPI2_TARGET_STANDARD_REPLY
{
    U16                     Reserved;                   /* 0x00 */
    U8                      MsgLength;                  /* 0x02 */
    U8                      Function;                   /* 0x03 */
    U16                     Reserved1;                  /* 0x04 */
    U8                      Reserved2;                  /* 0x06 */
    U8                      MsgFlags;                   /* 0x07 */
    U8                      VP_ID;                      /* 0x08 */
    U8                      VF_ID;                      /* 0x09 */
    U16                     Reserved3;                  /* 0x0A */
    U16                     Reserved4;                  /* 0x0C */
    U16                     IOCStatus;                  /* 0x0E */
    U32                     IOCLogInfo;                 /* 0x10 */
    U16                     IoIndex;                    /* 0x14 */
    U16                     Reserved5;                  /* 0x16 */
    U32                     TransferCount;              /* 0x18 */
    U32                     BidirectionalTransferCount; /* 0x1C */
} MPI2_TARGET_STANDARD_REPLY, MPI2_POINTER PTR_MPI2_TARGET_STANDARD_REPLY,
  Mpi2TargetErrorReply_t, MPI2_POINTER pMpi2TargetErrorReply_t;


/****************************************************************************
*  Target Mode Abort Request
****************************************************************************/

typedef struct _MPI2_TARGET_MODE_ABORT_REQUEST
{
    U8                      AbortType;                  /* 0x00 */
    U8                      Reserved1;                  /* 0x01 */
    U8                      ChainOffset;                /* 0x02 */
    U8                      Function;                   /* 0x03 */
    U16                     Reserved2;                  /* 0x04 */
    U8                      Reserved3;                  /* 0x06 */
    U8                      MsgFlags;                   /* 0x07 */
    U8                      VP_ID;                      /* 0x08 */
    U8                      VF_ID;                      /* 0x09 */
    U16                     Reserved4;                  /* 0x0A */
    U16                     IoIndexToAbort;             /* 0x0C */
    U16                     Reserved6;                  /* 0x0E */
    U32                     MidToAbort;                 /* 0x10 */
} MPI2_TARGET_MODE_ABORT, MPI2_POINTER PTR_MPI2_TARGET_MODE_ABORT,
  Mpi2TargetModeAbort_t, MPI2_POINTER pMpi2TargetModeAbort_t;

/* Target Mode Abort AbortType values */

#define MPI2_TARGET_MODE_ABORT_ALL_CMD_BUFFERS      (0x00)
#define MPI2_TARGET_MODE_ABORT_ALL_IO               (0x01)
#define MPI2_TARGET_MODE_ABORT_EXACT_IO             (0x02)
#define MPI2_TARGET_MODE_ABORT_EXACT_IO_REQUEST     (0x03)
#define MPI2_TARGET_MODE_ABORT_IO_REQUEST_AND_IO    (0x04)


/****************************************************************************
*  Target Mode Abort Reply
****************************************************************************/

typedef struct _MPI2_TARGET_MODE_ABORT_REPLY
{
    U16                     Reserved;                   /* 0x00 */
    U8                      MsgLength;                  /* 0x02 */
    U8                      Function;                   /* 0x03 */
    U16                     Reserved1;                  /* 0x04 */
    U8                      Reserved2;                  /* 0x06 */
    U8                      MsgFlags;                   /* 0x07 */
    U8                      VP_ID;                      /* 0x08 */
    U8                      VF_ID;                      /* 0x09 */
    U16                     Reserved3;                  /* 0x0A */
    U16                     Reserved4;                  /* 0x0C */
    U16                     IOCStatus;                  /* 0x0E */
    U32                     IOCLogInfo;                 /* 0x10 */
    U32                     AbortCount;                 /* 0x14 */
} MPI2_TARGET_MODE_ABORT_REPLY, MPI2_POINTER PTR_MPI2_TARGET_MODE_ABORT_REPLY,
  Mpi2TargetModeAbortReply_t, MPI2_POINTER pMpi2TargetModeAbortReply_t;


#endif

