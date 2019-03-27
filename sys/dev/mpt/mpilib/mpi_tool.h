/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2000-2010, LSI Logic Corporation and its contributors.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon including
 *    a substantially similar Disclaimer requirement for further binary
 *    redistribution.
 * 3. Neither the name of the LSI Logic Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF THE COPYRIGHT
 * OWNER OR CONTRIBUTOR IS ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 *           Name:  mpi_tool.h
 *          Title:  MPI Toolbox structures and definitions
 *  Creation Date:  July 30, 2001
 *
 *    mpi_tool.h Version:  01.05.03
 *
 *  Version History
 *  ---------------
 *
 *  Date      Version   Description
 *  --------  --------  ------------------------------------------------------
 *  08-08-01  01.02.01  Original release.
 *  08-29-01  01.02.02  Added DIAG_DATA_UPLOAD_HEADER and related defines.
 *  01-16-04  01.02.03  Added defines and structures for new tools
 *.                     MPI_TOOLBOX_ISTWI_READ_WRITE_TOOL and
 *                      MPI_TOOLBOX_FC_MANAGEMENT_TOOL.
 *  04-29-04  01.02.04  Added message structures for Diagnostic Buffer Post and
 *                      Diagnostic Release requests and replies.
 *  05-11-04  01.03.01  Original release for MPI v1.3.
 *  08-19-04  01.05.01  Original release for MPI v1.5.
 *  10-06-04  01.05.02  Added define for MPI_DIAG_BUF_TYPE_COUNT.
 *  02-09-05  01.05.03  Added frame size option to FC management tool.
 *                      Added Beacon tool to the Toolbox.
 *  --------------------------------------------------------------------------
 */

#ifndef MPI_TOOL_H
#define MPI_TOOL_H

#define MPI_TOOLBOX_CLEAN_TOOL                      (0x00)
#define MPI_TOOLBOX_MEMORY_MOVE_TOOL                (0x01)
#define MPI_TOOLBOX_DIAG_DATA_UPLOAD_TOOL           (0x02)
#define MPI_TOOLBOX_ISTWI_READ_WRITE_TOOL           (0x03)
#define MPI_TOOLBOX_FC_MANAGEMENT_TOOL              (0x04)
#define MPI_TOOLBOX_BEACON_TOOL                     (0x05)


/****************************************************************************/
/* Toolbox reply                                                            */
/****************************************************************************/

typedef struct _MSG_TOOLBOX_REPLY
{
    U8                      Tool;                       /* 00h */
    U8                      Reserved;                   /* 01h */
    U8                      MsgLength;                  /* 02h */
    U8                      Function;                   /* 03h */
    U16                     Reserved1;                  /* 04h */
    U8                      Reserved2;                  /* 06h */
    U8                      MsgFlags;                   /* 07h */
    U32                     MsgContext;                 /* 08h */
    U16                     Reserved3;                  /* 0Ch */
    U16                     IOCStatus;                  /* 0Eh */
    U32                     IOCLogInfo;                 /* 10h */
} MSG_TOOLBOX_REPLY, MPI_POINTER PTR_MSG_TOOLBOX_REPLY,
  ToolboxReply_t, MPI_POINTER pToolboxReply_t;


/****************************************************************************/
/* Toolbox Clean Tool request                                               */
/****************************************************************************/

typedef struct _MSG_TOOLBOX_CLEAN_REQUEST
{
    U8                      Tool;                       /* 00h */
    U8                      Reserved;                   /* 01h */
    U8                      ChainOffset;                /* 02h */
    U8                      Function;                   /* 03h */
    U16                     Reserved1;                  /* 04h */
    U8                      Reserved2;                  /* 06h */
    U8                      MsgFlags;                   /* 07h */
    U32                     MsgContext;                 /* 08h */
    U32                     Flags;                      /* 0Ch */
} MSG_TOOLBOX_CLEAN_REQUEST, MPI_POINTER PTR_MSG_TOOLBOX_CLEAN_REQUEST,
  ToolboxCleanRequest_t, MPI_POINTER pToolboxCleanRequest_t;

#define MPI_TOOLBOX_CLEAN_NVSRAM                    (0x00000001)
#define MPI_TOOLBOX_CLEAN_SEEPROM                   (0x00000002)
#define MPI_TOOLBOX_CLEAN_FLASH                     (0x00000004)
#define MPI_TOOLBOX_CLEAN_BOOTLOADER                (0x04000000)
#define MPI_TOOLBOX_CLEAN_FW_BACKUP                 (0x08000000)
#define MPI_TOOLBOX_CLEAN_FW_CURRENT                (0x10000000)
#define MPI_TOOLBOX_CLEAN_OTHER_PERSIST_PAGES       (0x20000000)
#define MPI_TOOLBOX_CLEAN_PERSIST_MANUFACT_PAGES    (0x40000000)
#define MPI_TOOLBOX_CLEAN_BOOT_SERVICES             (0x80000000)


/****************************************************************************/
/* Toolbox Memory Move request                                              */
/****************************************************************************/

typedef struct _MSG_TOOLBOX_MEM_MOVE_REQUEST
{
    U8                      Tool;                       /* 00h */
    U8                      Reserved;                   /* 01h */
    U8                      ChainOffset;                /* 02h */
    U8                      Function;                   /* 03h */
    U16                     Reserved1;                  /* 04h */
    U8                      Reserved2;                  /* 06h */
    U8                      MsgFlags;                   /* 07h */
    U32                     MsgContext;                 /* 08h */
    SGE_SIMPLE_UNION        SGL;                        /* 0Ch */
} MSG_TOOLBOX_MEM_MOVE_REQUEST, MPI_POINTER PTR_MSG_TOOLBOX_MEM_MOVE_REQUEST,
  ToolboxMemMoveRequest_t, MPI_POINTER pToolboxMemMoveRequest_t;


/****************************************************************************/
/* Toolbox Diagnostic Data Upload request                                   */
/****************************************************************************/

typedef struct _MSG_TOOLBOX_DIAG_DATA_UPLOAD_REQUEST
{
    U8                      Tool;                       /* 00h */
    U8                      Reserved;                   /* 01h */
    U8                      ChainOffset;                /* 02h */
    U8                      Function;                   /* 03h */
    U16                     Reserved1;                  /* 04h */
    U8                      Reserved2;                  /* 06h */
    U8                      MsgFlags;                   /* 07h */
    U32                     MsgContext;                 /* 08h */
    U32                     Flags;                      /* 0Ch */
    U32                     Reserved3;                  /* 10h */
    SGE_SIMPLE_UNION        SGL;                        /* 14h */
} MSG_TOOLBOX_DIAG_DATA_UPLOAD_REQUEST, MPI_POINTER PTR_MSG_TOOLBOX_DIAG_DATA_UPLOAD_REQUEST,
  ToolboxDiagDataUploadRequest_t, MPI_POINTER pToolboxDiagDataUploadRequest_t;

typedef struct _DIAG_DATA_UPLOAD_HEADER
{
    U32                     DiagDataLength;             /* 00h */
    U8                      FormatCode;                 /* 04h */
    U8                      Reserved;                   /* 05h */
    U16                     Reserved1;                  /* 06h */
} DIAG_DATA_UPLOAD_HEADER, MPI_POINTER PTR_DIAG_DATA_UPLOAD_HEADER,
  DiagDataUploadHeader_t, MPI_POINTER pDiagDataUploadHeader_t;

#define MPI_TB_DIAG_FORMAT_SCSI_PRINTF_1            (0x01)
#define MPI_TB_DIAG_FORMAT_SCSI_2                   (0x02)
#define MPI_TB_DIAG_FORMAT_SCSI_3                   (0x03)
#define MPI_TB_DIAG_FORMAT_FC_TRACE_1               (0x04)


/****************************************************************************/
/* Toolbox ISTWI Read Write request                                         */
/****************************************************************************/

typedef struct _MSG_TOOLBOX_ISTWI_READ_WRITE_REQUEST
{
    U8                      Tool;                       /* 00h */
    U8                      Reserved;                   /* 01h */
    U8                      ChainOffset;                /* 02h */
    U8                      Function;                   /* 03h */
    U16                     Reserved1;                  /* 04h */
    U8                      Reserved2;                  /* 06h */
    U8                      MsgFlags;                   /* 07h */
    U32                     MsgContext;                 /* 08h */
    U8                      Flags;                      /* 0Ch */
    U8                      BusNum;                     /* 0Dh */
    U16                     Reserved3;                  /* 0Eh */
    U8                      NumAddressBytes;            /* 10h */
    U8                      Reserved4;                  /* 11h */
    U16                     DataLength;                 /* 12h */
    U8                      DeviceAddr;                 /* 14h */
    U8                      Addr1;                      /* 15h */
    U8                      Addr2;                      /* 16h */
    U8                      Addr3;                      /* 17h */
    U32                     Reserved5;                  /* 18h */
    SGE_SIMPLE_UNION        SGL;                        /* 1Ch */
} MSG_TOOLBOX_ISTWI_READ_WRITE_REQUEST, MPI_POINTER PTR_MSG_TOOLBOX_ISTWI_READ_WRITE_REQUEST,
  ToolboxIstwiReadWriteRequest_t, MPI_POINTER pToolboxIstwiReadWriteRequest_t;

#define MPI_TB_ISTWI_FLAGS_WRITE                    (0x00)
#define MPI_TB_ISTWI_FLAGS_READ                     (0x01)


/****************************************************************************/
/* Toolbox FC Management request                                            */
/****************************************************************************/

/* ActionInfo for Bus and TargetId */
typedef struct _MPI_TB_FC_MANAGE_BUS_TID_AI
{
    U16                     Reserved;                   /* 00h */
    U8                      Bus;                        /* 02h */
    U8                      TargetId;                   /* 03h */
} MPI_TB_FC_MANAGE_BUS_TID_AI, MPI_POINTER PTR_MPI_TB_FC_MANAGE_BUS_TID_AI,
  MpiTbFcManageBusTidAi_t, MPI_POINTER pMpiTbFcManageBusTidAi_t;

/* ActionInfo for port identifier */
typedef struct _MPI_TB_FC_MANAGE_PID_AI
{
    U32                     PortIdentifier;             /* 00h */
} MPI_TB_FC_MANAGE_PID_AI, MPI_POINTER PTR_MPI_TB_FC_MANAGE_PID_AI,
  MpiTbFcManagePidAi_t, MPI_POINTER pMpiTbFcManagePidAi_t;

/* ActionInfo for set max frame size */
typedef struct _MPI_TB_FC_MANAGE_FRAME_SIZE_AI
{
    U16                     FrameSize;                  /* 00h */
    U8                      PortNum;                    /* 02h */
    U8                      Reserved1;                  /* 03h */
} MPI_TB_FC_MANAGE_FRAME_SIZE_AI, MPI_POINTER PTR_MPI_TB_FC_MANAGE_FRAME_SIZE_AI,
  MpiTbFcManageFrameSizeAi_t, MPI_POINTER pMpiTbFcManageFrameSizeAi_t;

/* union of ActionInfo */
typedef union _MPI_TB_FC_MANAGE_AI_UNION
{
    MPI_TB_FC_MANAGE_BUS_TID_AI     BusTid;
    MPI_TB_FC_MANAGE_PID_AI         Port;
    MPI_TB_FC_MANAGE_FRAME_SIZE_AI  FrameSize;
} MPI_TB_FC_MANAGE_AI_UNION, MPI_POINTER PTR_MPI_TB_FC_MANAGE_AI_UNION,
  MpiTbFcManageAiUnion_t, MPI_POINTER pMpiTbFcManageAiUnion_t;

typedef struct _MSG_TOOLBOX_FC_MANAGE_REQUEST
{
    U8                          Tool;                   /* 00h */
    U8                          Reserved;               /* 01h */
    U8                          ChainOffset;            /* 02h */
    U8                          Function;               /* 03h */
    U16                         Reserved1;              /* 04h */
    U8                          Reserved2;              /* 06h */
    U8                          MsgFlags;               /* 07h */
    U32                         MsgContext;             /* 08h */
    U8                          Action;                 /* 0Ch */
    U8                          Reserved3;              /* 0Dh */
    U16                         Reserved4;              /* 0Eh */
    MPI_TB_FC_MANAGE_AI_UNION   ActionInfo;             /* 10h */
} MSG_TOOLBOX_FC_MANAGE_REQUEST, MPI_POINTER PTR_MSG_TOOLBOX_FC_MANAGE_REQUEST,
  ToolboxFcManageRequest_t, MPI_POINTER pToolboxFcManageRequest_t;

/* defines for the Action field */
#define MPI_TB_FC_MANAGE_ACTION_DISC_ALL            (0x00)
#define MPI_TB_FC_MANAGE_ACTION_DISC_PID            (0x01)
#define MPI_TB_FC_MANAGE_ACTION_DISC_BUS_TID        (0x02)
#define MPI_TB_FC_MANAGE_ACTION_SET_MAX_FRAME_SIZE  (0x03)


/****************************************************************************/
/* Toolbox Beacon Tool request                                               */
/****************************************************************************/

typedef struct _MSG_TOOLBOX_BEACON_REQUEST
{
    U8                      Tool;                       /* 00h */
    U8                      Reserved;                   /* 01h */
    U8                      ChainOffset;                /* 02h */
    U8                      Function;                   /* 03h */
    U16                     Reserved1;                  /* 04h */
    U8                      Reserved2;                  /* 06h */
    U8                      MsgFlags;                   /* 07h */
    U32                     MsgContext;                 /* 08h */
    U8                      ConnectNum;                 /* 0Ch */
    U8                      PortNum;                    /* 0Dh */
    U8                      Reserved3;                  /* 0Eh */
    U8                      Flags;                      /* 0Fh */
} MSG_TOOLBOX_BEACON_REQUEST, MPI_POINTER PTR_MSG_TOOLBOX_BEACON_REQUEST,
  ToolboxBeaconRequest_t, MPI_POINTER pToolboxBeaconRequest_t;

#define MPI_TOOLBOX_FLAGS_BEACON_MODE_OFF       (0x00)
#define MPI_TOOLBOX_FLAGS_BEACON_MODE_ON        (0x01)


/****************************************************************************/
/* Diagnostic Buffer Post request                                           */
/****************************************************************************/

typedef struct _MSG_DIAG_BUFFER_POST_REQUEST
{
    U8                      TraceLevel;                 /* 00h */
    U8                      BufferType;                 /* 01h */
    U8                      ChainOffset;                /* 02h */
    U8                      Function;                   /* 03h */
    U16                     Reserved1;                  /* 04h */
    U8                      Reserved2;                  /* 06h */
    U8                      MsgFlags;                   /* 07h */
    U32                     MsgContext;                 /* 08h */
    U32                     ExtendedType;               /* 0Ch */
    U32                     BufferLength;               /* 10h */
    U32                     ProductSpecific[4];         /* 14h */
    U32                     Reserved3;                  /* 24h */
    U64                     BufferAddress;              /* 28h */
} MSG_DIAG_BUFFER_POST_REQUEST, MPI_POINTER PTR_MSG_DIAG_BUFFER_POST_REQUEST,
  DiagBufferPostRequest_t, MPI_POINTER pDiagBufferPostRequest_t;

#define MPI_DIAG_BUF_TYPE_TRACE                     (0x00)
#define MPI_DIAG_BUF_TYPE_SNAPSHOT                  (0x01)
#define MPI_DIAG_BUF_TYPE_EXTENDED                  (0x02)
/* count of the number of buffer types */
#define MPI_DIAG_BUF_TYPE_COUNT                     (0x03)

#define MPI_DIAG_EXTENDED_QTAG                      (0x00000001)


/* Diagnostic Buffer Post reply */
typedef struct _MSG_DIAG_BUFFER_POST_REPLY
{
    U8                      Reserved1;                  /* 00h */
    U8                      BufferType;                 /* 01h */
    U8                      MsgLength;                  /* 02h */
    U8                      Function;                   /* 03h */
    U16                     Reserved2;                  /* 04h */
    U8                      Reserved3;                  /* 06h */
    U8                      MsgFlags;                   /* 07h */
    U32                     MsgContext;                 /* 08h */
    U16                     Reserved4;                  /* 0Ch */
    U16                     IOCStatus;                  /* 0Eh */
    U32                     IOCLogInfo;                 /* 10h */
    U32                     TransferLength;             /* 14h */
} MSG_DIAG_BUFFER_POST_REPLY, MPI_POINTER PTR_MSG_DIAG_BUFFER_POST_REPLY,
  DiagBufferPostReply_t, MPI_POINTER pDiagBufferPostReply_t;


/****************************************************************************/
/* Diagnostic Release request                                               */
/****************************************************************************/

typedef struct _MSG_DIAG_RELEASE_REQUEST
{
    U8                      Reserved1;                  /* 00h */
    U8                      BufferType;                 /* 01h */
    U8                      ChainOffset;                /* 02h */
    U8                      Function;                   /* 03h */
    U16                     Reserved2;                  /* 04h */
    U8                      Reserved3;                  /* 06h */
    U8                      MsgFlags;                   /* 07h */
    U32                     MsgContext;                 /* 08h */
} MSG_DIAG_RELEASE_REQUEST, MPI_POINTER PTR_MSG_DIAG_RELEASE_REQUEST,
  DiagReleaseRequest_t, MPI_POINTER pDiagReleaseRequest_t;


/* Diagnostic Release reply */
typedef struct _MSG_DIAG_RELEASE_REPLY
{
    U8                      Reserved1;                  /* 00h */
    U8                      BufferType;                 /* 01h */
    U8                      MsgLength;                  /* 02h */
    U8                      Function;                   /* 03h */
    U16                     Reserved2;                  /* 04h */
    U8                      Reserved3;                  /* 06h */
    U8                      MsgFlags;                   /* 07h */
    U32                     MsgContext;                 /* 08h */
    U16                     Reserved4;                  /* 0Ch */
    U16                     IOCStatus;                  /* 0Eh */
    U32                     IOCLogInfo;                 /* 10h */
} MSG_DIAG_RELEASE_REPLY, MPI_POINTER PTR_MSG_DIAG_RELEASE_REPLY,
  DiagReleaseReply_t, MPI_POINTER pDiagReleaseReply_t;


#endif
