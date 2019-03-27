/*
 * Copyright (c) 2015, AVAGO Tech. All rights reserved. Authors: Marian Choy
 * Copyright (c) 2014, LSI Corp. All rights reserved. Authors: Marian Choy
 * Support: freebsdraid@avagotech.com
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer. 2. Redistributions
 * in binary form must reproduce the above copyright notice, this list of
 * conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution. 3. Neither the name of the
 * <ORGANIZATION> nor the names of its contributors may be used to endorse or
 * promote products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing
 * official policies,either expressed or implied, of the FreeBSD Project.
 *
 * Send feedback to: <megaraidfbsd@avagotech.com> Mail to: AVAGO TECHNOLOGIES, 1621
 * Barber Lane, Milpitas, CA 95035 ATTN: MegaRaid FreeBSD
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifndef MRSAS_H
#define	MRSAS_H

#include <sys/param.h>			/* defines used in kernel.h */
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/errno.h>
#include <sys/kernel.h>			/* types used in module initialization */
#include <sys/conf.h>			/* cdevsw struct */
#include <sys/uio.h>			/* uio struct */
#include <sys/malloc.h>
#include <sys/bus.h>			/* structs, prototypes for pci bus
					 * stuff */
#include <sys/rman.h>
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/sema.h>
#include <sys/sysctl.h>
#include <sys/stat.h>
#include <sys/taskqueue.h>
#include <sys/poll.h>
#include <sys/selinfo.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/atomic.h>

#include <dev/pci/pcivar.h>		/* For pci_get macros! */
#include <dev/pci/pcireg.h>


#define	IOCTL_SEMA_DESCRIPTION	"mrsas semaphore for MFI pool"

/*
 * Device IDs and PCI
 */
#define	MRSAS_TBOLT			0x005b
#define	MRSAS_INVADER		0x005d
#define	MRSAS_FURY			0x005f
#define	MRSAS_INTRUDER		0x00ce
#define	MRSAS_INTRUDER_24	0x00cf
#define	MRSAS_CUTLASS_52	0x0052
#define	MRSAS_CUTLASS_53	0x0053
/* Gen3.5 Conroller */
#define	MRSAS_VENTURA               0x0014
#define	MRSAS_CRUSADER              0x0015
#define	MRSAS_HARPOON               0x0016
#define	MRSAS_TOMCAT                0x0017
#define	MRSAS_VENTURA_4PORT         0x001B
#define	MRSAS_CRUSADER_4PORT        0x001C
#define	MRSAS_AERO_10E0             0x10E0
#define	MRSAS_AERO_10E1             0x10E1
#define	MRSAS_AERO_10E2             0x10E2
#define	MRSAS_AERO_10E3             0x10E3
#define	MRSAS_AERO_10E4             0x10E4
#define	MRSAS_AERO_10E5             0x10E5
#define	MRSAS_AERO_10E6             0x10E6
#define	MRSAS_AERO_10E7             0x10E7


/*
 * Firmware State Defines
 */
#define	MRSAS_FWSTATE_MAXCMD_MASK		0x0000FFFF
#define	MRSAS_FWSTATE_SGE_MASK			0x00FF0000
#define	MRSAS_FW_STATE_CHNG_INTERRUPT	1

/*
 * Message Frame Defines
 */
#define	MRSAS_SENSE_LEN					96
#define	MRSAS_FUSION_MAX_RESET_TRIES	3

/*
 * Miscellaneous Defines
 */
#define	BYTE_ALIGNMENT					1
#define	MRSAS_MAX_NAME_LENGTH			32
#define	MRSAS_VERSION					"07.709.04.00-fbsd"
#define	MRSAS_ULONG_MAX					0xFFFFFFFFFFFFFFFF
#define	MRSAS_DEFAULT_TIMEOUT			0x14	/* Temporarily set */
#define	DONE							0
#define	MRSAS_PAGE_SIZE					4096
#define	MRSAS_RESET_NOTICE_INTERVAL		5
#define	MRSAS_IO_TIMEOUT				180000	/* 180 second timeout */
#define	MRSAS_LDIO_QUEUE_DEPTH			70	/* 70 percent as default */
#define	THRESHOLD_REPLY_COUNT			50
#define	MAX_MSIX_COUNT					128

#define MAX_STREAMS_TRACKED				8
#define MR_STREAM_BITMAP				0x76543210
#define BITS_PER_INDEX_STREAM			4	/* number of bits per index in U32 TrackStream */
#define STREAM_MASK						((1 << BITS_PER_INDEX_STREAM) - 1)
#define ZERO_LAST_STREAM				0x0fffffff

/*
 * Boolean types
 */
#if (__FreeBSD_version < 901000)
typedef enum _boolean {
	false, true
}	boolean;

#endif
enum err {
	SUCCESS, FAIL
};

MALLOC_DECLARE(M_MRSAS);
SYSCTL_DECL(_hw_mrsas);

#define	MRSAS_INFO		(1 << 0)
#define	MRSAS_TRACE		(1 << 1)
#define	MRSAS_FAULT		(1 << 2)
#define	MRSAS_OCR		(1 << 3)
#define	MRSAS_TOUT		MRSAS_OCR
#define	MRSAS_AEN		(1 << 4)
#define	MRSAS_PRL11		(1 << 5)

#define	mrsas_dprint(sc, level, msg, args...)       \
do {                                                \
    if (sc->mrsas_debug & level)                    \
        device_printf(sc->mrsas_dev, msg, ##args);  \
} while (0)


/****************************************************************************
 * Raid Context structure which describes MegaRAID specific IO Paramenters
 * This resides at offset 0x60 where the SGL normally starts in MPT IO Frames
 ****************************************************************************/

typedef struct _RAID_CONTEXT {
	u_int8_t Type:4;
	u_int8_t nseg:4;
	u_int8_t resvd0;
	u_int16_t timeoutValue;
	u_int8_t regLockFlags;
	u_int8_t resvd1;
	u_int16_t VirtualDiskTgtId;
	u_int64_t regLockRowLBA;
	u_int32_t regLockLength;
	u_int16_t nextLMId;
	u_int8_t exStatus;
	u_int8_t status;
	u_int8_t RAIDFlags;
	u_int8_t numSGE;
	u_int16_t configSeqNum;
	u_int8_t spanArm;
	u_int8_t priority;		/* 0x1D MR_PRIORITY_RANGE */
	u_int8_t numSGEExt;		/* 0x1E 1M IO support */
	u_int8_t resvd2;		/* 0x1F */
}	RAID_CONTEXT;

/*
 * Raid Context structure which describes ventura MegaRAID specific IO Paramenters
 * This resides at offset 0x60 where the SGL normally starts in MPT IO Frames
 */
typedef struct _RAID_CONTEXT_G35 {
	u_int16_t Type:4;
	u_int16_t nseg:4;
	u_int16_t resvd0:8;
	u_int16_t timeoutValue;
	union {
		struct {
			u_int16_t reserved:1;
			u_int16_t sld:1;
			u_int16_t c2f:1;
			u_int16_t fwn:1;
			u_int16_t sqn:1;
			u_int16_t sbs:1;
			u_int16_t rw:1;
			u_int16_t log:1;
			u_int16_t cpuSel:4;
			u_int16_t setDivert:4;
		}	bits;
		u_int16_t s;
	}	routingFlags;
	u_int16_t VirtualDiskTgtId;
	u_int64_t regLockRowLBA;
	u_int32_t regLockLength;
	union {
		u_int16_t nextLMId;
		u_int16_t peerSMID;
	}	smid;
	u_int8_t exStatus;
	u_int8_t status;
	u_int8_t RAIDFlags;
	u_int8_t spanArm;
	u_int16_t configSeqNum;
	u_int16_t numSGE:12;
	u_int16_t reserved:3;
	u_int16_t streamDetected:1;
	u_int8_t resvd2[2];
}	RAID_CONTEXT_G35;

typedef union _RAID_CONTEXT_UNION {
	RAID_CONTEXT raid_context;
	RAID_CONTEXT_G35 raid_context_g35;
}	RAID_CONTEXT_UNION, *PRAID_CONTEXT_UNION;


/*************************************************************************
 * MPI2 Defines
 ************************************************************************/

#define	MPI2_FUNCTION_IOC_INIT					(0x02)	/* IOC Init */
#define	MPI2_WHOINIT_HOST_DRIVER				(0x04)
#define	MPI2_VERSION_MAJOR						(0x02)
#define	MPI2_VERSION_MINOR						(0x00)
#define	MPI2_VERSION_MAJOR_MASK					(0xFF00)
#define	MPI2_VERSION_MAJOR_SHIFT				(8)
#define	MPI2_VERSION_MINOR_MASK					(0x00FF)
#define	MPI2_VERSION_MINOR_SHIFT				(0)
#define	MPI2_VERSION ((MPI2_VERSION_MAJOR << MPI2_VERSION_MAJOR_SHIFT) | \
                      MPI2_VERSION_MINOR)
#define	MPI2_HEADER_VERSION_UNIT				(0x10)
#define	MPI2_HEADER_VERSION_DEV					(0x00)
#define	MPI2_HEADER_VERSION_UNIT_MASK			(0xFF00)
#define	MPI2_HEADER_VERSION_UNIT_SHIFT			(8)
#define	MPI2_HEADER_VERSION_DEV_MASK			(0x00FF)
#define	MPI2_HEADER_VERSION_DEV_SHIFT			(0)
#define	MPI2_HEADER_VERSION ((MPI2_HEADER_VERSION_UNIT << 8) | MPI2_HEADER_VERSION_DEV)
#define	MPI2_IEEE_SGE_FLAGS_IOCPLBNTA_ADDR		(0x03)
#define	MPI2_SCSIIO_EEDPFLAGS_INC_PRI_REFTAG	(0x8000)
#define	MPI2_SCSIIO_EEDPFLAGS_CHECK_REFTAG		(0x0400)
#define	MPI2_SCSIIO_EEDPFLAGS_CHECK_REMOVE_OP	(0x0003)
#define	MPI2_SCSIIO_EEDPFLAGS_CHECK_APPTAG		(0x0200)
#define	MPI2_SCSIIO_EEDPFLAGS_CHECK_GUARD		(0x0100)
#define	MPI2_SCSIIO_EEDPFLAGS_INSERT_OP			(0x0004)
#define	MPI2_FUNCTION_SCSI_IO_REQUEST			(0x00)	/* SCSI IO */
#define	MPI2_FUNCTION_SCSI_TASK_MGMT			(0x01)
#define	MPI2_REQ_DESCRIPT_FLAGS_HIGH_PRIORITY	(0x03)
#define	MPI2_REQ_DESCRIPT_FLAGS_FP_IO			(0x06)
#define	MPI2_REQ_DESCRIPT_FLAGS_SCSI_IO			(0x00)
#define	MPI2_SGE_FLAGS_64_BIT_ADDRESSING		(0x02)
#define	MPI2_SCSIIO_CONTROL_WRITE				(0x01000000)
#define	MPI2_SCSIIO_CONTROL_READ				(0x02000000)
#define	MPI2_REQ_DESCRIPT_FLAGS_TYPE_MASK		(0x0E)
#define	MPI2_RPY_DESCRIPT_FLAGS_UNUSED			(0x0F)
#define	MPI2_RPY_DESCRIPT_FLAGS_SCSI_IO_SUCCESS	(0x00)
#define	MPI2_RPY_DESCRIPT_FLAGS_TYPE_MASK		(0x0F)
#define	MPI2_WRSEQ_FLUSH_KEY_VALUE				(0x0)
#define	MPI2_WRITE_SEQUENCE_OFFSET				(0x00000004)
#define	MPI2_WRSEQ_1ST_KEY_VALUE				(0xF)
#define	MPI2_WRSEQ_2ND_KEY_VALUE				(0x4)
#define	MPI2_WRSEQ_3RD_KEY_VALUE				(0xB)
#define	MPI2_WRSEQ_4TH_KEY_VALUE				(0x2)
#define	MPI2_WRSEQ_5TH_KEY_VALUE				(0x7)
#define	MPI2_WRSEQ_6TH_KEY_VALUE				(0xD)

#ifndef MPI2_POINTER
#define	MPI2_POINTER	*
#endif


/***************************************
 * MPI2 Structures
 ***************************************/

typedef struct _MPI25_IEEE_SGE_CHAIN64 {
	u_int64_t Address;
	u_int32_t Length;
	u_int16_t Reserved1;
	u_int8_t NextChainOffset;
	u_int8_t Flags;
}	MPI25_IEEE_SGE_CHAIN64, MPI2_POINTER PTR_MPI25_IEEE_SGE_CHAIN64,
Mpi25IeeeSgeChain64_t, MPI2_POINTER pMpi25IeeeSgeChain64_t;

typedef struct _MPI2_SGE_SIMPLE_UNION {
	u_int32_t FlagsLength;
	union {
		u_int32_t Address32;
		u_int64_t Address64;
	}	u;
}	MPI2_SGE_SIMPLE_UNION, MPI2_POINTER PTR_MPI2_SGE_SIMPLE_UNION,
Mpi2SGESimpleUnion_t, MPI2_POINTER pMpi2SGESimpleUnion_t;

typedef struct {
	u_int8_t CDB[20];		/* 0x00 */
	u_int32_t PrimaryReferenceTag;	/* 0x14 */
	u_int16_t PrimaryApplicationTag;/* 0x18 */
	u_int16_t PrimaryApplicationTagMask;	/* 0x1A */
	u_int32_t TransferLength;	/* 0x1C */
}	MPI2_SCSI_IO_CDB_EEDP32, MPI2_POINTER PTR_MPI2_SCSI_IO_CDB_EEDP32,
Mpi2ScsiIoCdbEedp32_t, MPI2_POINTER pMpi2ScsiIoCdbEedp32_t;

typedef struct _MPI2_SGE_CHAIN_UNION {
	u_int16_t Length;
	u_int8_t NextChainOffset;
	u_int8_t Flags;
	union {
		u_int32_t Address32;
		u_int64_t Address64;
	}	u;
}	MPI2_SGE_CHAIN_UNION, MPI2_POINTER PTR_MPI2_SGE_CHAIN_UNION,
Mpi2SGEChainUnion_t, MPI2_POINTER pMpi2SGEChainUnion_t;

typedef struct _MPI2_IEEE_SGE_SIMPLE32 {
	u_int32_t Address;
	u_int32_t FlagsLength;
}	MPI2_IEEE_SGE_SIMPLE32, MPI2_POINTER PTR_MPI2_IEEE_SGE_SIMPLE32,
Mpi2IeeeSgeSimple32_t, MPI2_POINTER pMpi2IeeeSgeSimple32_t;
typedef struct _MPI2_IEEE_SGE_SIMPLE64 {
	u_int64_t Address;
	u_int32_t Length;
	u_int16_t Reserved1;
	u_int8_t Reserved2;
	u_int8_t Flags;
}	MPI2_IEEE_SGE_SIMPLE64, MPI2_POINTER PTR_MPI2_IEEE_SGE_SIMPLE64,
Mpi2IeeeSgeSimple64_t, MPI2_POINTER pMpi2IeeeSgeSimple64_t;

typedef union _MPI2_IEEE_SGE_SIMPLE_UNION {
	MPI2_IEEE_SGE_SIMPLE32 Simple32;
	MPI2_IEEE_SGE_SIMPLE64 Simple64;
}	MPI2_IEEE_SGE_SIMPLE_UNION, MPI2_POINTER PTR_MPI2_IEEE_SGE_SIMPLE_UNION,
Mpi2IeeeSgeSimpleUnion_t, MPI2_POINTER pMpi2IeeeSgeSimpleUnion_t;

typedef MPI2_IEEE_SGE_SIMPLE32 MPI2_IEEE_SGE_CHAIN32;
typedef MPI2_IEEE_SGE_SIMPLE64 MPI2_IEEE_SGE_CHAIN64;

typedef union _MPI2_IEEE_SGE_CHAIN_UNION {
	MPI2_IEEE_SGE_CHAIN32 Chain32;
	MPI2_IEEE_SGE_CHAIN64 Chain64;
}	MPI2_IEEE_SGE_CHAIN_UNION, MPI2_POINTER PTR_MPI2_IEEE_SGE_CHAIN_UNION,
Mpi2IeeeSgeChainUnion_t, MPI2_POINTER pMpi2IeeeSgeChainUnion_t;

typedef union _MPI2_SGE_IO_UNION {
	MPI2_SGE_SIMPLE_UNION MpiSimple;
	MPI2_SGE_CHAIN_UNION MpiChain;
	MPI2_IEEE_SGE_SIMPLE_UNION IeeeSimple;
	MPI2_IEEE_SGE_CHAIN_UNION IeeeChain;
}	MPI2_SGE_IO_UNION, MPI2_POINTER PTR_MPI2_SGE_IO_UNION,
Mpi2SGEIOUnion_t, MPI2_POINTER pMpi2SGEIOUnion_t;

typedef union {
	u_int8_t CDB32[32];
	MPI2_SCSI_IO_CDB_EEDP32 EEDP32;
	MPI2_SGE_SIMPLE_UNION SGE;
}	MPI2_SCSI_IO_CDB_UNION, MPI2_POINTER PTR_MPI2_SCSI_IO_CDB_UNION,
Mpi2ScsiIoCdb_t, MPI2_POINTER pMpi2ScsiIoCdb_t;

/****************************************************************************
 *  *  SCSI Task Management messages
 *   ****************************************************************************/

/*SCSI Task Management Request Message */
typedef struct _MPI2_SCSI_TASK_MANAGE_REQUEST {
	u_int16_t DevHandle;        /*0x00 */
	u_int8_t ChainOffset;       /*0x02 */
	u_int8_t Function;      /*0x03 */
	u_int8_t Reserved1;     /*0x04 */
	u_int8_t TaskType;      /*0x05 */
	u_int8_t Reserved2;     /*0x06 */
	u_int8_t MsgFlags;      /*0x07 */
	u_int8_t VP_ID;     /*0x08 */
	u_int8_t VF_ID;     /*0x09 */
	u_int16_t Reserved3;        /*0x0A */
	u_int8_t LUN[8];        /*0x0C */
	u_int32_t Reserved4[7]; /*0x14 */
	u_int16_t TaskMID;      /*0x30 */
	u_int16_t Reserved5;        /*0x32 */
} MPI2_SCSI_TASK_MANAGE_REQUEST;

/*SCSI Task Management Reply Message */
typedef struct _MPI2_SCSI_TASK_MANAGE_REPLY {
	u_int16_t DevHandle;        /*0x00 */
	u_int8_t MsgLength;     /*0x02 */
	u_int8_t Function;      /*0x03 */
	u_int8_t ResponseCode;  /*0x04 */
	u_int8_t TaskType;      /*0x05 */
	u_int8_t Reserved1;     /*0x06 */
	u_int8_t MsgFlags;      /*0x07 */
	u_int8_t VP_ID;     /*0x08 */
	u_int8_t VF_ID;     /*0x09 */
	u_int16_t Reserved2;        /*0x0A */
	u_int16_t Reserved3;        /*0x0C */
	u_int16_t IOCStatus;        /*0x0E */
	u_int32_t IOCLogInfo;       /*0x10 */
	u_int32_t TerminationCount; /*0x14 */
	u_int32_t ResponseInfo; /*0x18 */
} MPI2_SCSI_TASK_MANAGE_REPLY;

typedef struct _MR_TM_REQUEST {
	char request[128];
} MR_TM_REQUEST;

typedef struct _MR_TM_REPLY {
	char reply[128];
} MR_TM_REPLY;

/* SCSI Task Management Request Message */
typedef struct _MR_TASK_MANAGE_REQUEST {
	/*To be type casted to struct MPI2_SCSI_TASK_MANAGE_REQUEST */
	MR_TM_REQUEST        TmRequest;
	union {
		struct {
			u_int32_t isTMForLD:1;
			u_int32_t isTMForPD:1;
			u_int32_t reserved1:30;
			u_int32_t reserved2;
		} tmReqFlags;
		MR_TM_REPLY   TMReply;
	} uTmReqReply;
} MR_TASK_MANAGE_REQUEST;

/* TaskType values */
#define MPI2_SCSITASKMGMT_TASKTYPE_ABORT_TASK           (0x01)
#define MPI2_SCSITASKMGMT_TASKTYPE_ABRT_TASK_SET        (0x02)
#define MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET         (0x03)
#define MPI2_SCSITASKMGMT_TASKTYPE_LOGICAL_UNIT_RESET   (0x05)
#define MPI2_SCSITASKMGMT_TASKTYPE_CLEAR_TASK_SET       (0x06)
#define MPI2_SCSITASKMGMT_TASKTYPE_QUERY_TASK           (0x07)
#define MPI2_SCSITASKMGMT_TASKTYPE_CLR_ACA              (0x08)
#define MPI2_SCSITASKMGMT_TASKTYPE_QRY_TASK_SET         (0x09)
#define MPI2_SCSITASKMGMT_TASKTYPE_QRY_ASYNC_EVENT      (0x0A)

/* ResponseCode values */
#define MPI2_SCSITASKMGMT_RSP_TM_COMPLETE               (0x00)
#define MPI2_SCSITASKMGMT_RSP_INVALID_FRAME             (0x02)
#define MPI2_SCSITASKMGMT_RSP_TM_NOT_SUPPORTED          (0x04)
#define MPI2_SCSITASKMGMT_RSP_TM_FAILED                 (0x05)
#define MPI2_SCSITASKMGMT_RSP_TM_SUCCEEDED              (0x08)
#define MPI2_SCSITASKMGMT_RSP_TM_INVALID_LUN            (0x09)
#define MPI2_SCSITASKMGMT_RSP_TM_OVERLAPPED_TAG         (0x0A)
#define MPI2_SCSITASKMGMT_RSP_IO_QUEUED_ON_IOC          (0x80)

/*
 * RAID SCSI IO Request Message Total SGE count will be one less than
 * _MPI2_SCSI_IO_REQUEST
 */
typedef struct _MPI2_RAID_SCSI_IO_REQUEST {
	u_int16_t DevHandle;		/* 0x00 */
	u_int8_t ChainOffset;		/* 0x02 */
	u_int8_t Function;		/* 0x03 */
	u_int16_t Reserved1;		/* 0x04 */
	u_int8_t Reserved2;		/* 0x06 */
	u_int8_t MsgFlags;		/* 0x07 */
	u_int8_t VP_ID;			/* 0x08 */
	u_int8_t VF_ID;			/* 0x09 */
	u_int16_t Reserved3;		/* 0x0A */
	u_int32_t SenseBufferLowAddress;/* 0x0C */
	u_int16_t SGLFlags;		/* 0x10 */
	u_int8_t SenseBufferLength;	/* 0x12 */
	u_int8_t Reserved4;		/* 0x13 */
	u_int8_t SGLOffset0;		/* 0x14 */
	u_int8_t SGLOffset1;		/* 0x15 */
	u_int8_t SGLOffset2;		/* 0x16 */
	u_int8_t SGLOffset3;		/* 0x17 */
	u_int32_t SkipCount;		/* 0x18 */
	u_int32_t DataLength;		/* 0x1C */
	u_int32_t BidirectionalDataLength;	/* 0x20 */
	u_int16_t IoFlags;		/* 0x24 */
	u_int16_t EEDPFlags;		/* 0x26 */
	u_int32_t EEDPBlockSize;	/* 0x28 */
	u_int32_t SecondaryReferenceTag;/* 0x2C */
	u_int16_t SecondaryApplicationTag;	/* 0x30 */
	u_int16_t ApplicationTagTranslationMask;	/* 0x32 */
	u_int8_t LUN[8];		/* 0x34 */
	u_int32_t Control;		/* 0x3C */
	MPI2_SCSI_IO_CDB_UNION CDB;	/* 0x40 */
	RAID_CONTEXT_UNION RaidContext;	/* 0x60 */
	MPI2_SGE_IO_UNION SGL;		/* 0x80 */
}	MRSAS_RAID_SCSI_IO_REQUEST, MPI2_POINTER PTR_MRSAS_RAID_SCSI_IO_REQUEST,
MRSASRaidSCSIIORequest_t, MPI2_POINTER pMRSASRaidSCSIIORequest_t;

/*
 * MPT RAID MFA IO Descriptor.
 */
typedef struct _MRSAS_RAID_MFA_IO_DESCRIPTOR {
	u_int32_t RequestFlags:8;
	u_int32_t MessageAddress1:24;	/* bits 31:8 */
	u_int32_t MessageAddress2;	/* bits 61:32 */
}	MRSAS_RAID_MFA_IO_REQUEST_DESCRIPTOR, *PMRSAS_RAID_MFA_IO_REQUEST_DESCRIPTOR;

/* Default Request Descriptor */
typedef struct _MPI2_DEFAULT_REQUEST_DESCRIPTOR {
	u_int8_t RequestFlags;		/* 0x00 */
	u_int8_t MSIxIndex;		/* 0x01 */
	u_int16_t SMID;			/* 0x02 */
	u_int16_t LMID;			/* 0x04 */
	u_int16_t DescriptorTypeDependent;	/* 0x06 */
}	MPI2_DEFAULT_REQUEST_DESCRIPTOR,

	MPI2_POINTER PTR_MPI2_DEFAULT_REQUEST_DESCRIPTOR,
Mpi2DefaultRequestDescriptor_t, MPI2_POINTER pMpi2DefaultRequestDescriptor_t;

/* High Priority Request Descriptor */
typedef struct _MPI2_HIGH_PRIORITY_REQUEST_DESCRIPTOR {
	u_int8_t RequestFlags;		/* 0x00 */
	u_int8_t MSIxIndex;		/* 0x01 */
	u_int16_t SMID;			/* 0x02 */
	u_int16_t LMID;			/* 0x04 */
	u_int16_t Reserved1;		/* 0x06 */
}	MPI2_HIGH_PRIORITY_REQUEST_DESCRIPTOR,

	MPI2_POINTER PTR_MPI2_HIGH_PRIORITY_REQUEST_DESCRIPTOR,
Mpi2HighPriorityRequestDescriptor_t, MPI2_POINTER pMpi2HighPriorityRequestDescriptor_t;

/* SCSI IO Request Descriptor */
typedef struct _MPI2_SCSI_IO_REQUEST_DESCRIPTOR {
	u_int8_t RequestFlags;		/* 0x00 */
	u_int8_t MSIxIndex;		/* 0x01 */
	u_int16_t SMID;			/* 0x02 */
	u_int16_t LMID;			/* 0x04 */
	u_int16_t DevHandle;		/* 0x06 */
}	MPI2_SCSI_IO_REQUEST_DESCRIPTOR,

	MPI2_POINTER PTR_MPI2_SCSI_IO_REQUEST_DESCRIPTOR,
Mpi2SCSIIORequestDescriptor_t, MPI2_POINTER pMpi2SCSIIORequestDescriptor_t;

/* SCSI Target Request Descriptor */
typedef struct _MPI2_SCSI_TARGET_REQUEST_DESCRIPTOR {
	u_int8_t RequestFlags;		/* 0x00 */
	u_int8_t MSIxIndex;		/* 0x01 */
	u_int16_t SMID;			/* 0x02 */
	u_int16_t LMID;			/* 0x04 */
	u_int16_t IoIndex;		/* 0x06 */
}	MPI2_SCSI_TARGET_REQUEST_DESCRIPTOR,

	MPI2_POINTER PTR_MPI2_SCSI_TARGET_REQUEST_DESCRIPTOR,
Mpi2SCSITargetRequestDescriptor_t, MPI2_POINTER pMpi2SCSITargetRequestDescriptor_t;

/* RAID Accelerator Request Descriptor */
typedef struct _MPI2_RAID_ACCEL_REQUEST_DESCRIPTOR {
	u_int8_t RequestFlags;		/* 0x00 */
	u_int8_t MSIxIndex;		/* 0x01 */
	u_int16_t SMID;			/* 0x02 */
	u_int16_t LMID;			/* 0x04 */
	u_int16_t Reserved;		/* 0x06 */
}	MPI2_RAID_ACCEL_REQUEST_DESCRIPTOR,

	MPI2_POINTER PTR_MPI2_RAID_ACCEL_REQUEST_DESCRIPTOR,
Mpi2RAIDAcceleratorRequestDescriptor_t, MPI2_POINTER pMpi2RAIDAcceleratorRequestDescriptor_t;

/* union of Request Descriptors */
typedef union _MRSAS_REQUEST_DESCRIPTOR_UNION {
	MPI2_DEFAULT_REQUEST_DESCRIPTOR Default;
	MPI2_HIGH_PRIORITY_REQUEST_DESCRIPTOR HighPriority;
	MPI2_SCSI_IO_REQUEST_DESCRIPTOR SCSIIO;
	MPI2_SCSI_TARGET_REQUEST_DESCRIPTOR SCSITarget;
	MPI2_RAID_ACCEL_REQUEST_DESCRIPTOR RAIDAccelerator;
	MRSAS_RAID_MFA_IO_REQUEST_DESCRIPTOR MFAIo;
	union {
		struct {
			u_int32_t low;
			u_int32_t high;
		}	u;
		u_int64_t Words;
	}	addr;
}	MRSAS_REQUEST_DESCRIPTOR_UNION;

/* Default Reply Descriptor */
typedef struct _MPI2_DEFAULT_REPLY_DESCRIPTOR {
	u_int8_t ReplyFlags;		/* 0x00 */
	u_int8_t MSIxIndex;		/* 0x01 */
	u_int16_t DescriptorTypeDependent1;	/* 0x02 */
	u_int32_t DescriptorTypeDependent2;	/* 0x04 */
}	MPI2_DEFAULT_REPLY_DESCRIPTOR, MPI2_POINTER PTR_MPI2_DEFAULT_REPLY_DESCRIPTOR,
Mpi2DefaultReplyDescriptor_t, MPI2_POINTER pMpi2DefaultReplyDescriptor_t;

/* Address Reply Descriptor */
typedef struct _MPI2_ADDRESS_REPLY_DESCRIPTOR {
	u_int8_t ReplyFlags;		/* 0x00 */
	u_int8_t MSIxIndex;		/* 0x01 */
	u_int16_t SMID;			/* 0x02 */
	u_int32_t ReplyFrameAddress;	/* 0x04 */
}	MPI2_ADDRESS_REPLY_DESCRIPTOR, MPI2_POINTER PTR_MPI2_ADDRESS_REPLY_DESCRIPTOR,
Mpi2AddressReplyDescriptor_t, MPI2_POINTER pMpi2AddressReplyDescriptor_t;

/* SCSI IO Success Reply Descriptor */
typedef struct _MPI2_SCSI_IO_SUCCESS_REPLY_DESCRIPTOR {
	u_int8_t ReplyFlags;		/* 0x00 */
	u_int8_t MSIxIndex;		/* 0x01 */
	u_int16_t SMID;			/* 0x02 */
	u_int16_t TaskTag;		/* 0x04 */
	u_int16_t Reserved1;		/* 0x06 */
}	MPI2_SCSI_IO_SUCCESS_REPLY_DESCRIPTOR,

	MPI2_POINTER PTR_MPI2_SCSI_IO_SUCCESS_REPLY_DESCRIPTOR,
Mpi2SCSIIOSuccessReplyDescriptor_t, MPI2_POINTER pMpi2SCSIIOSuccessReplyDescriptor_t;

/* TargetAssist Success Reply Descriptor */
typedef struct _MPI2_TARGETASSIST_SUCCESS_REPLY_DESCRIPTOR {
	u_int8_t ReplyFlags;		/* 0x00 */
	u_int8_t MSIxIndex;		/* 0x01 */
	u_int16_t SMID;			/* 0x02 */
	u_int8_t SequenceNumber;	/* 0x04 */
	u_int8_t Reserved1;		/* 0x05 */
	u_int16_t IoIndex;		/* 0x06 */
}	MPI2_TARGETASSIST_SUCCESS_REPLY_DESCRIPTOR,

	MPI2_POINTER PTR_MPI2_TARGETASSIST_SUCCESS_REPLY_DESCRIPTOR,
Mpi2TargetAssistSuccessReplyDescriptor_t, MPI2_POINTER pMpi2TargetAssistSuccessReplyDescriptor_t;

/* Target Command Buffer Reply Descriptor */
typedef struct _MPI2_TARGET_COMMAND_BUFFER_REPLY_DESCRIPTOR {
	u_int8_t ReplyFlags;		/* 0x00 */
	u_int8_t MSIxIndex;		/* 0x01 */
	u_int8_t VP_ID;			/* 0x02 */
	u_int8_t Flags;			/* 0x03 */
	u_int16_t InitiatorDevHandle;	/* 0x04 */
	u_int16_t IoIndex;		/* 0x06 */
}	MPI2_TARGET_COMMAND_BUFFER_REPLY_DESCRIPTOR,

	MPI2_POINTER PTR_MPI2_TARGET_COMMAND_BUFFER_REPLY_DESCRIPTOR,
Mpi2TargetCommandBufferReplyDescriptor_t, MPI2_POINTER pMpi2TargetCommandBufferReplyDescriptor_t;

/* RAID Accelerator Success Reply Descriptor */
typedef struct _MPI2_RAID_ACCELERATOR_SUCCESS_REPLY_DESCRIPTOR {
	u_int8_t ReplyFlags;		/* 0x00 */
	u_int8_t MSIxIndex;		/* 0x01 */
	u_int16_t SMID;			/* 0x02 */
	u_int32_t Reserved;		/* 0x04 */
}	MPI2_RAID_ACCELERATOR_SUCCESS_REPLY_DESCRIPTOR,

	MPI2_POINTER PTR_MPI2_RAID_ACCELERATOR_SUCCESS_REPLY_DESCRIPTOR,
Mpi2RAIDAcceleratorSuccessReplyDescriptor_t, MPI2_POINTER pMpi2RAIDAcceleratorSuccessReplyDescriptor_t;

/* union of Reply Descriptors */
typedef union _MPI2_REPLY_DESCRIPTORS_UNION {
	MPI2_DEFAULT_REPLY_DESCRIPTOR Default;
	MPI2_ADDRESS_REPLY_DESCRIPTOR AddressReply;
	MPI2_SCSI_IO_SUCCESS_REPLY_DESCRIPTOR SCSIIOSuccess;
	MPI2_TARGETASSIST_SUCCESS_REPLY_DESCRIPTOR TargetAssistSuccess;
	MPI2_TARGET_COMMAND_BUFFER_REPLY_DESCRIPTOR TargetCommandBuffer;
	MPI2_RAID_ACCELERATOR_SUCCESS_REPLY_DESCRIPTOR RAIDAcceleratorSuccess;
	u_int64_t Words;
}	MPI2_REPLY_DESCRIPTORS_UNION, MPI2_POINTER PTR_MPI2_REPLY_DESCRIPTORS_UNION,
Mpi2ReplyDescriptorsUnion_t, MPI2_POINTER pMpi2ReplyDescriptorsUnion_t;

typedef union {
	volatile unsigned int val;
	unsigned int val_rdonly;
} mrsas_atomic_t;

#define	mrsas_atomic_read(v)	atomic_load_acq_int(&(v)->val)
#define	mrsas_atomic_set(v,i)	atomic_store_rel_int(&(v)->val, i)
#define	mrsas_atomic_dec(v)	atomic_subtract_int(&(v)->val, 1)
#define	mrsas_atomic_inc(v)	atomic_add_int(&(v)->val, 1)

static inline int
mrsas_atomic_inc_return(mrsas_atomic_t *v)
{
	return 1 + atomic_fetchadd_int(&(v)->val, 1);
}

/* IOCInit Request message */
typedef struct _MPI2_IOC_INIT_REQUEST {
	u_int8_t WhoInit;		/* 0x00 */
	u_int8_t Reserved1;		/* 0x01 */
	u_int8_t ChainOffset;		/* 0x02 */
	u_int8_t Function;		/* 0x03 */
	u_int16_t Reserved2;		/* 0x04 */
	u_int8_t Reserved3;		/* 0x06 */
	u_int8_t MsgFlags;		/* 0x07 */
	u_int8_t VP_ID;			/* 0x08 */
	u_int8_t VF_ID;			/* 0x09 */
	u_int16_t Reserved4;		/* 0x0A */
	u_int16_t MsgVersion;		/* 0x0C */
	u_int16_t HeaderVersion;	/* 0x0E */
	u_int32_t Reserved5;		/* 0x10 */
	u_int16_t Reserved6;		/* 0x14 */
	u_int8_t HostPageSize;		/* 0x16 */
	u_int8_t HostMSIxVectors;	/* 0x17 */
	u_int16_t Reserved8;		/* 0x18 */
	u_int16_t SystemRequestFrameSize;	/* 0x1A */
	u_int16_t ReplyDescriptorPostQueueDepth;	/* 0x1C */
	u_int16_t ReplyFreeQueueDepth;	/* 0x1E */
	u_int32_t SenseBufferAddressHigh;	/* 0x20 */
	u_int32_t SystemReplyAddressHigh;	/* 0x24 */
	u_int64_t SystemRequestFrameBaseAddress;	/* 0x28 */
	u_int64_t ReplyDescriptorPostQueueAddress;	/* 0x30 */
	u_int64_t ReplyFreeQueueAddress;/* 0x38 */
	u_int64_t TimeStamp;		/* 0x40 */
}	MPI2_IOC_INIT_REQUEST, MPI2_POINTER PTR_MPI2_IOC_INIT_REQUEST,
Mpi2IOCInitRequest_t, MPI2_POINTER pMpi2IOCInitRequest_t;

/*
 * MR private defines
 */
#define	MR_PD_INVALID			0xFFFF
#define	MR_DEVHANDLE_INVALID	0xFFFF
#define	MAX_SPAN_DEPTH			8
#define	MAX_QUAD_DEPTH			MAX_SPAN_DEPTH
#define	MAX_RAIDMAP_SPAN_DEPTH	(MAX_SPAN_DEPTH)
#define	MAX_ROW_SIZE			32
#define	MAX_RAIDMAP_ROW_SIZE	(MAX_ROW_SIZE)
#define	MAX_LOGICAL_DRIVES		64
#define	MAX_LOGICAL_DRIVES_EXT	256
#define	MAX_LOGICAL_DRIVES_DYN	512

#define	MAX_RAIDMAP_LOGICAL_DRIVES	(MAX_LOGICAL_DRIVES)
#define	MAX_RAIDMAP_VIEWS			(MAX_LOGICAL_DRIVES)

#define	MAX_ARRAYS				128
#define	MAX_RAIDMAP_ARRAYS		(MAX_ARRAYS)

#define	MAX_ARRAYS_EXT			256
#define	MAX_API_ARRAYS_EXT		MAX_ARRAYS_EXT
#define	MAX_API_ARRAYS_DYN		512

#define	MAX_PHYSICAL_DEVICES	256
#define	MAX_RAIDMAP_PHYSICAL_DEVICES	(MAX_PHYSICAL_DEVICES)
#define	MAX_RAIDMAP_PHYSICAL_DEVICES_DYN	512
#define	MR_DCMD_LD_MAP_GET_INFO	0x0300e101
#define	MR_DCMD_SYSTEM_PD_MAP_GET_INFO	0x0200e102
#define MR_DCMD_PD_MFI_TASK_MGMT	0x0200e100

#define MR_DCMD_PD_GET_INFO		0x02020000
#define	MRSAS_MAX_PD_CHANNELS		1
#define	MRSAS_MAX_LD_CHANNELS		1
#define	MRSAS_MAX_DEV_PER_CHANNEL	256
#define	MRSAS_DEFAULT_INIT_ID		-1
#define	MRSAS_MAX_LUN				8
#define	MRSAS_DEFAULT_CMD_PER_LUN	256
#define	MRSAS_MAX_PD				(MRSAS_MAX_PD_CHANNELS * \
			MRSAS_MAX_DEV_PER_CHANNEL)
#define	MRSAS_MAX_LD_IDS			(MRSAS_MAX_LD_CHANNELS * \
			MRSAS_MAX_DEV_PER_CHANNEL)


#define	VD_EXT_DEBUG	0
#define TM_DEBUG		1

/*******************************************************************
 * RAID map related structures
 ********************************************************************/
#pragma pack(1)
typedef struct _MR_DEV_HANDLE_INFO {
	u_int16_t curDevHdl;
	u_int8_t validHandles;
	u_int8_t interfaceType;
	u_int16_t devHandle[2];
}	MR_DEV_HANDLE_INFO;

#pragma pack()

typedef struct _MR_ARRAY_INFO {
	u_int16_t pd[MAX_RAIDMAP_ROW_SIZE];
}	MR_ARRAY_INFO;

typedef struct _MR_QUAD_ELEMENT {
	u_int64_t logStart;
	u_int64_t logEnd;
	u_int64_t offsetInSpan;
	u_int32_t diff;
	u_int32_t reserved1;
}	MR_QUAD_ELEMENT;

typedef struct _MR_SPAN_INFO {
	u_int32_t noElements;
	u_int32_t reserved1;
	MR_QUAD_ELEMENT quad[MAX_RAIDMAP_SPAN_DEPTH];
}	MR_SPAN_INFO;

typedef struct _MR_LD_SPAN_ {
	u_int64_t startBlk;
	u_int64_t numBlks;
	u_int16_t arrayRef;
	u_int8_t spanRowSize;
	u_int8_t spanRowDataSize;
	u_int8_t reserved[4];
}	MR_LD_SPAN;

typedef struct _MR_SPAN_BLOCK_INFO {
	u_int64_t num_rows;
	MR_LD_SPAN span;
	MR_SPAN_INFO block_span_info;
}	MR_SPAN_BLOCK_INFO;

typedef struct _MR_LD_RAID {
	struct {
		u_int32_t fpCapable:1;
		u_int32_t raCapable:1;
		u_int32_t reserved5:2;
		u_int32_t ldPiMode:4;
		u_int32_t pdPiMode:4;
		u_int32_t encryptionType:8;
		u_int32_t fpWriteCapable:1;
		u_int32_t fpReadCapable:1;
		u_int32_t fpWriteAcrossStripe:1;
		u_int32_t fpReadAcrossStripe:1;
		u_int32_t fpNonRWCapable:1;
		u_int32_t tmCapable:1;
		u_int32_t fpCacheBypassCapable:1;
		u_int32_t reserved4:5;
	}	capability;
	u_int32_t reserved6;
	u_int64_t size;

	u_int8_t spanDepth;
	u_int8_t level;
	u_int8_t stripeShift;
	u_int8_t rowSize;

	u_int8_t rowDataSize;
	u_int8_t writeMode;
	u_int8_t PRL;
	u_int8_t SRL;

	u_int16_t targetId;
	u_int8_t ldState;
	u_int8_t regTypeReqOnWrite;
	u_int8_t modFactor;
	u_int8_t regTypeReqOnRead;
	u_int16_t seqNum;

	struct {
		u_int32_t ldSyncRequired:1;
		u_int32_t regTypeReqOnReadLsValid:1;
		u_int32_t reserved:30;
	}	flags;

	u_int8_t LUN[8];
	u_int8_t fpIoTimeoutForLd;
	u_int8_t reserved2[3];
	u_int32_t logicalBlockLength;
	struct {
		u_int32_t LdPiExp:4;
		u_int32_t LdLogicalBlockExp:4;
		u_int32_t reserved1:24;
	}	exponent;
	u_int8_t reserved3[0x80 - 0x38];
}	MR_LD_RAID;

typedef struct _MR_LD_SPAN_MAP {
	MR_LD_RAID ldRaid;
	u_int8_t dataArmMap[MAX_RAIDMAP_ROW_SIZE];
	MR_SPAN_BLOCK_INFO spanBlock[MAX_RAIDMAP_SPAN_DEPTH];
}	MR_LD_SPAN_MAP;

typedef struct _MR_FW_RAID_MAP {
	u_int32_t totalSize;
	union {
		struct {
			u_int32_t maxLd;
			u_int32_t maxSpanDepth;
			u_int32_t maxRowSize;
			u_int32_t maxPdCount;
			u_int32_t maxArrays;
		}	validationInfo;
		u_int32_t version[5];
		u_int32_t reserved1[5];
	}	raid_desc;
	u_int32_t ldCount;
	u_int32_t Reserved1;

	/*
	 * This doesn't correspond to FW Ld Tgt Id to LD, but will purge. For
	 * example: if tgt Id is 4 and FW LD is 2, and there is only one LD,
	 * FW will populate the array like this. [0xFF, 0xFF, 0xFF, 0xFF,
	 * 0x0,.....]. This is to help reduce the entire strcture size if
	 * there are few LDs or driver is looking info for 1 LD only.
	 */
	u_int8_t ldTgtIdToLd[MAX_RAIDMAP_LOGICAL_DRIVES + MAX_RAIDMAP_VIEWS];
	u_int8_t fpPdIoTimeoutSec;
	u_int8_t reserved2[7];
	MR_ARRAY_INFO arMapInfo[MAX_RAIDMAP_ARRAYS];
	MR_DEV_HANDLE_INFO devHndlInfo[MAX_RAIDMAP_PHYSICAL_DEVICES];
	MR_LD_SPAN_MAP ldSpanMap[1];
}	MR_FW_RAID_MAP;


typedef struct _MR_FW_RAID_MAP_EXT {
	/* Not used in new map */
	u_int32_t reserved;

	union {
		struct {
			u_int32_t maxLd;
			u_int32_t maxSpanDepth;
			u_int32_t maxRowSize;
			u_int32_t maxPdCount;
			u_int32_t maxArrays;
		}	validationInfo;
		u_int32_t version[5];
		u_int32_t reserved1[5];
	}	fw_raid_desc;

	u_int8_t fpPdIoTimeoutSec;
	u_int8_t reserved2[7];

	u_int16_t ldCount;
	u_int16_t arCount;
	u_int16_t spanCount;
	u_int16_t reserve3;

	MR_DEV_HANDLE_INFO devHndlInfo[MAX_RAIDMAP_PHYSICAL_DEVICES];
	u_int8_t ldTgtIdToLd[MAX_LOGICAL_DRIVES_EXT];
	MR_ARRAY_INFO arMapInfo[MAX_API_ARRAYS_EXT];
	MR_LD_SPAN_MAP ldSpanMap[MAX_LOGICAL_DRIVES_EXT];
}	MR_FW_RAID_MAP_EXT;


typedef struct _MR_DRV_RAID_MAP {
	/*
	 * Total size of this structure, including this field. This feild
	 * will be manupulated by driver for ext raid map, else pick the
	 * value from firmware raid map.
	 */
	u_int32_t totalSize;

	union {
		struct {
			u_int32_t maxLd;
			u_int32_t maxSpanDepth;
			u_int32_t maxRowSize;
			u_int32_t maxPdCount;
			u_int32_t maxArrays;
		}	validationInfo;
		u_int32_t version[5];
		u_int32_t reserved1[5];
	}	drv_raid_desc;

	/* timeout value used by driver in FP IOs */
	u_int8_t fpPdIoTimeoutSec;
	u_int8_t reserved2[7];

	u_int16_t ldCount;
	u_int16_t arCount;
	u_int16_t spanCount;
	u_int16_t reserve3;

	MR_DEV_HANDLE_INFO devHndlInfo[MAX_RAIDMAP_PHYSICAL_DEVICES_DYN];
	u_int16_t ldTgtIdToLd[MAX_LOGICAL_DRIVES_DYN];
	MR_ARRAY_INFO arMapInfo[MAX_API_ARRAYS_DYN];
	MR_LD_SPAN_MAP ldSpanMap[1];

}	MR_DRV_RAID_MAP;

/*
 * Driver raid map size is same as raid map ext MR_DRV_RAID_MAP_ALL is
 * created to sync with old raid. And it is mainly for code re-use purpose.
 */

#pragma pack(1)
typedef struct _MR_DRV_RAID_MAP_ALL {

	MR_DRV_RAID_MAP raidMap;
	MR_LD_SPAN_MAP ldSpanMap[MAX_LOGICAL_DRIVES_DYN - 1];
}	MR_DRV_RAID_MAP_ALL;

#pragma pack()

typedef struct _LD_LOAD_BALANCE_INFO {
	u_int8_t loadBalanceFlag;
	u_int8_t reserved1;
	mrsas_atomic_t scsi_pending_cmds[MAX_PHYSICAL_DEVICES];
	u_int64_t last_accessed_block[MAX_PHYSICAL_DEVICES];
}	LD_LOAD_BALANCE_INFO, *PLD_LOAD_BALANCE_INFO;

/* SPAN_SET is info caclulated from span info from Raid map per ld */
typedef struct _LD_SPAN_SET {
	u_int64_t log_start_lba;
	u_int64_t log_end_lba;
	u_int64_t span_row_start;
	u_int64_t span_row_end;
	u_int64_t data_strip_start;
	u_int64_t data_strip_end;
	u_int64_t data_row_start;
	u_int64_t data_row_end;
	u_int8_t strip_offset[MAX_SPAN_DEPTH];
	u_int32_t span_row_data_width;
	u_int32_t diff;
	u_int32_t reserved[2];
}	LD_SPAN_SET, *PLD_SPAN_SET;

typedef struct LOG_BLOCK_SPAN_INFO {
	LD_SPAN_SET span_set[MAX_SPAN_DEPTH];
}	LD_SPAN_INFO, *PLD_SPAN_INFO;

#pragma pack(1)
typedef struct _MR_FW_RAID_MAP_ALL {
	MR_FW_RAID_MAP raidMap;
	MR_LD_SPAN_MAP ldSpanMap[MAX_LOGICAL_DRIVES - 1];
}	MR_FW_RAID_MAP_ALL;

#pragma pack()

struct IO_REQUEST_INFO {
	u_int64_t ldStartBlock;
	u_int32_t numBlocks;
	u_int16_t ldTgtId;
	u_int8_t isRead;
	u_int16_t devHandle;
	u_int8_t pdInterface;
	u_int64_t pdBlock;
	u_int8_t fpOkForIo;
	u_int8_t IoforUnevenSpan;
	u_int8_t start_span;
	u_int8_t reserved;
	u_int64_t start_row;
	/* span[7:5], arm[4:0] */
	u_int8_t span_arm;
	u_int8_t pd_after_lb;
	boolean_t raCapable;
	u_int16_t r1_alt_dev_handle;
};

/*
 * define MR_PD_CFG_SEQ structure for system PDs
 */
struct MR_PD_CFG_SEQ {
	u_int16_t seqNum;
	u_int16_t devHandle;
	struct {
		u_int8_t tmCapable:1;
		u_int8_t reserved:7;
	} capability;
	u_int8_t reserved;
	u_int16_t pdTargetId;
} __packed;

struct MR_PD_CFG_SEQ_NUM_SYNC {
	u_int32_t size;
	u_int32_t count;
	struct MR_PD_CFG_SEQ seq[1];
} __packed;

typedef struct _STREAM_DETECT {
	u_int64_t nextSeqLBA;
	struct megasas_cmd_fusion *first_cmd_fusion;
	struct megasas_cmd_fusion *last_cmd_fusion;
	u_int32_t countCmdsInStream;
	u_int16_t numSGEsInGroup;
	u_int8_t isRead;
	u_int8_t groupDepth;
	boolean_t groupFlush;
	u_int8_t reserved[7];
} STREAM_DETECT, *PTR_STREAM_DETECT;

typedef struct _LD_STREAM_DETECT {
	boolean_t writeBack;
	boolean_t FPWriteEnabled;
	boolean_t membersSSDs;
	boolean_t fpCacheBypassCapable;
	u_int32_t mruBitMap;
	volatile long iosToFware;
	volatile long writeBytesOutstanding;
	STREAM_DETECT streamTrack[MAX_STREAMS_TRACKED];
} LD_STREAM_DETECT, *PTR_LD_STREAM_DETECT;


typedef struct _MR_LD_TARGET_SYNC {
	u_int8_t targetId;
	u_int8_t reserved;
	u_int16_t seqNum;
}	MR_LD_TARGET_SYNC;


/*
 * RAID Map descriptor Types.
 * Each element should uniquely idetify one data structure in the RAID map
 */
typedef enum _MR_RAID_MAP_DESC_TYPE {
	RAID_MAP_DESC_TYPE_DEVHDL_INFO = 0,	/* MR_DEV_HANDLE_INFO data */
	RAID_MAP_DESC_TYPE_TGTID_INFO = 1,	/* target to Ld num Index map */
	RAID_MAP_DESC_TYPE_ARRAY_INFO = 2,	/* MR_ARRAY_INFO data */
	RAID_MAP_DESC_TYPE_SPAN_INFO = 3,	/* MR_LD_SPAN_MAP data */
	RAID_MAP_DESC_TYPE_COUNT,
}	MR_RAID_MAP_DESC_TYPE;

/*
 * This table defines the offset, size and num elements  of each descriptor
 * type in the RAID Map buffer
 */
typedef struct _MR_RAID_MAP_DESC_TABLE {
	/* Raid map descriptor type */
	u_int32_t	raidMapDescType;
	/* Offset into the RAID map buffer where descriptor data is saved */
	u_int32_t	raidMapDescOffset;
	/* total size of the descriptor buffer */
	u_int32_t	raidMapDescBufferSize;
	/* Number of elements contained in the descriptor buffer */
	u_int32_t	raidMapDescElements;
}	MR_RAID_MAP_DESC_TABLE;

/*
 * Dynamic Raid Map Structure.
 */
typedef struct _MR_FW_RAID_MAP_DYNAMIC {
	u_int32_t	raidMapSize;
	u_int32_t	descTableOffset;
	u_int32_t	descTableSize;
	u_int32_t	descTableNumElements;
	u_int64_t	PCIThresholdBandwidth;
	u_int32_t	reserved2[3];

	u_int8_t	fpPdIoTimeoutSec;
	u_int8_t	reserved3[3];
	u_int32_t	rmwFPSeqNum;
	u_int16_t	ldCount;
	u_int16_t	arCount;
	u_int16_t	spanCount;
	u_int16_t	reserved4[3];

	/*
	* The below structure of pointers is only to be used by the driver.
	* This is added in the API to reduce the amount of code changes needed in
	* the driver to support dynamic RAID map.
	* Firmware should not update these pointers while preparing the raid map
	*/
	union {
		struct {
			MR_DEV_HANDLE_INFO	*devHndlInfo;
			u_int16_t			*ldTgtIdToLd;
			MR_ARRAY_INFO		*arMapInfo;
			MR_LD_SPAN_MAP		*ldSpanMap;
		} ptrStruct;
		u_int64_t ptrStructureSize[RAID_MAP_DESC_TYPE_COUNT];
	} RaidMapDescPtrs;

	/*
	* RAID Map descriptor table defines the layout of data in the RAID Map.
	* The size of the descriptor table itself could change.
	*/

	/* Variable Size descriptor Table. */
	MR_RAID_MAP_DESC_TABLE raidMapDescTable[RAID_MAP_DESC_TYPE_COUNT];
	/* Variable Size buffer containing all data */
	u_int32_t raidMapDescData[1];

}	MR_FW_RAID_MAP_DYNAMIC;


#define	IEEE_SGE_FLAGS_ADDR_MASK		(0x03)
#define	IEEE_SGE_FLAGS_SYSTEM_ADDR		(0x00)
#define	IEEE_SGE_FLAGS_IOCDDR_ADDR		(0x01)
#define	IEEE_SGE_FLAGS_IOCPLB_ADDR		(0x02)
#define	IEEE_SGE_FLAGS_IOCPLBNTA_ADDR	(0x03)
#define	IEEE_SGE_FLAGS_CHAIN_ELEMENT	(0x80)
#define	IEEE_SGE_FLAGS_END_OF_LIST		(0x40)

/* Few NVME flags defines*/
#define MPI2_SGE_FLAGS_SHIFT                (0x02)
#define IEEE_SGE_FLAGS_FORMAT_MASK          (0xC0)
#define IEEE_SGE_FLAGS_FORMAT_IEEE          (0x00)
#define IEEE_SGE_FLAGS_FORMAT_PQI           (0x01)
#define IEEE_SGE_FLAGS_FORMAT_NVME          (0x02)
#define IEEE_SGE_FLAGS_FORMAT_AHCI          (0x03)


#define MPI26_IEEE_SGE_FLAGS_NSF_MASK           (0x1C)
#define MPI26_IEEE_SGE_FLAGS_NSF_MPI_IEEE       (0x00)
#define MPI26_IEEE_SGE_FLAGS_NSF_PQI            (0x04)
#define MPI26_IEEE_SGE_FLAGS_NSF_NVME_PRP       (0x08)
#define MPI26_IEEE_SGE_FLAGS_NSF_AHCI_PRDT      (0x0C)
#define MPI26_IEEE_SGE_FLAGS_NSF_NVME_SGL       (0x10)

union desc_value {
	u_int64_t word;
	struct {
		u_int32_t low;
		u_int32_t high;
	}	u;
};

/*******************************************************************
 * Temporary command
 ********************************************************************/
struct mrsas_tmp_dcmd {
	bus_dma_tag_t tmp_dcmd_tag;
	bus_dmamap_t tmp_dcmd_dmamap;
	void   *tmp_dcmd_mem;
	bus_addr_t tmp_dcmd_phys_addr;
};

#define	MR_MAX_RAID_MAP_SIZE_OFFSET_SHIFT  16
#define	MR_MAX_RAID_MAP_SIZE_MASK      0x1FF
#define	MR_MIN_MAP_SIZE                0x10000


/*******************************************************************
 * Register set, included legacy controllers 1068 and 1078,
 * structure extended for 1078 registers
 *******************************************************************/
#pragma pack(1)
typedef struct _mrsas_register_set {
	u_int32_t doorbell;		/* 0000h */
	u_int32_t fusion_seq_offset;	/* 0004h */
	u_int32_t fusion_host_diag;	/* 0008h */
	u_int32_t reserved_01;		/* 000Ch */

	u_int32_t inbound_msg_0;	/* 0010h */
	u_int32_t inbound_msg_1;	/* 0014h */
	u_int32_t outbound_msg_0;	/* 0018h */
	u_int32_t outbound_msg_1;	/* 001Ch */

	u_int32_t inbound_doorbell;	/* 0020h */
	u_int32_t inbound_intr_status;	/* 0024h */
	u_int32_t inbound_intr_mask;	/* 0028h */

	u_int32_t outbound_doorbell;	/* 002Ch */
	u_int32_t outbound_intr_status;	/* 0030h */
	u_int32_t outbound_intr_mask;	/* 0034h */

	u_int32_t reserved_1[2];	/* 0038h */

	u_int32_t inbound_queue_port;	/* 0040h */
	u_int32_t outbound_queue_port;	/* 0044h */

	u_int32_t reserved_2[9];	/* 0048h */
	u_int32_t reply_post_host_index;/* 006Ch */
	u_int32_t reserved_2_2[12];	/* 0070h */

	u_int32_t outbound_doorbell_clear;	/* 00A0h */

	u_int32_t reserved_3[3];	/* 00A4h */

	u_int32_t outbound_scratch_pad;	/* 00B0h */
	u_int32_t outbound_scratch_pad_2;	/* 00B4h */
	u_int32_t outbound_scratch_pad_3;	/* 00B8h */
	u_int32_t outbound_scratch_pad_4;	/* 00BCh */

	u_int32_t inbound_low_queue_port;	/* 00C0h */

	u_int32_t inbound_high_queue_port;	/* 00C4h */

	u_int32_t inbound_single_queue_port;	/* 00C8h */
	u_int32_t res_6[11];		/* CCh */
	u_int32_t host_diag;
	u_int32_t seq_offset;
	u_int32_t index_registers[807];	/* 00CCh */
}	mrsas_reg_set;

#pragma pack()

/*******************************************************************
 * Firmware Interface Defines
 *******************************************************************
 * MFI stands for MegaRAID SAS FW Interface. This is just a moniker
 * for protocol between the software and firmware. Commands are
 * issued using "message frames".
 ******************************************************************/
/*
 * FW posts its state in upper 4 bits of outbound_msg_0 register
 */
#define	MFI_STATE_MASK					0xF0000000
#define	MFI_STATE_UNDEFINED				0x00000000
#define	MFI_STATE_BB_INIT				0x10000000
#define	MFI_STATE_FW_INIT				0x40000000
#define	MFI_STATE_WAIT_HANDSHAKE		0x60000000
#define	MFI_STATE_FW_INIT_2				0x70000000
#define	MFI_STATE_DEVICE_SCAN			0x80000000
#define	MFI_STATE_BOOT_MESSAGE_PENDING	0x90000000
#define	MFI_STATE_FLUSH_CACHE			0xA0000000
#define	MFI_STATE_READY					0xB0000000
#define	MFI_STATE_OPERATIONAL			0xC0000000
#define	MFI_STATE_FAULT					0xF0000000
#define	MFI_RESET_REQUIRED				0x00000001
#define	MFI_RESET_ADAPTER				0x00000002
#define	MEGAMFI_FRAME_SIZE				64
#define	MRSAS_MFI_FRAME_SIZE			1024
#define	MRSAS_MFI_SENSE_SIZE			128

/*
 * During FW init, clear pending cmds & reset state using inbound_msg_0
 *
 * ABORT        : Abort all pending cmds READY        : Move from OPERATIONAL to
 * READY state; discard queue info MFIMODE      : Discard (possible) low MFA
 * posted in 64-bit mode (??) CLR_HANDSHAKE: FW is waiting for HANDSHAKE from
 * BIOS or Driver HOTPLUG      : Resume from Hotplug MFI_STOP_ADP : Send
 * signal to FW to stop processing
 */

#define	WRITE_SEQUENCE_OFFSET		(0x0000000FC)
#define	HOST_DIAGNOSTIC_OFFSET		(0x000000F8)
#define	DIAG_WRITE_ENABLE			(0x00000080)
#define	DIAG_RESET_ADAPTER			(0x00000004)

#define	MFI_ADP_RESET				0x00000040
#define	MFI_INIT_ABORT				0x00000001
#define	MFI_INIT_READY				0x00000002
#define	MFI_INIT_MFIMODE			0x00000004
#define	MFI_INIT_CLEAR_HANDSHAKE	0x00000008
#define	MFI_INIT_HOTPLUG			0x00000010
#define	MFI_STOP_ADP				0x00000020
#define	MFI_RESET_FLAGS				MFI_INIT_READY|		\
									MFI_INIT_MFIMODE|	\
									MFI_INIT_ABORT

/*
 * MFI frame flags
 */
#define	MFI_FRAME_POST_IN_REPLY_QUEUE			0x0000
#define	MFI_FRAME_DONT_POST_IN_REPLY_QUEUE		0x0001
#define	MFI_FRAME_SGL32							0x0000
#define	MFI_FRAME_SGL64							0x0002
#define	MFI_FRAME_SENSE32						0x0000
#define	MFI_FRAME_SENSE64						0x0004
#define	MFI_FRAME_DIR_NONE						0x0000
#define	MFI_FRAME_DIR_WRITE						0x0008
#define	MFI_FRAME_DIR_READ						0x0010
#define	MFI_FRAME_DIR_BOTH						0x0018
#define	MFI_FRAME_IEEE							0x0020

/*
 * Definition for cmd_status
 */
#define	MFI_CMD_STATUS_POLL_MODE				0xFF

/*
 * MFI command opcodes
 */
#define	MFI_CMD_INIT							0x00
#define	MFI_CMD_LD_READ							0x01
#define	MFI_CMD_LD_WRITE						0x02
#define	MFI_CMD_LD_SCSI_IO						0x03
#define	MFI_CMD_PD_SCSI_IO						0x04
#define	MFI_CMD_DCMD							0x05
#define	MFI_CMD_ABORT							0x06
#define	MFI_CMD_SMP								0x07
#define	MFI_CMD_STP								0x08
#define	MFI_CMD_INVALID							0xff

#define	MR_DCMD_CTRL_GET_INFO					0x01010000
#define	MR_DCMD_LD_GET_LIST						0x03010000
#define	MR_DCMD_CTRL_CACHE_FLUSH				0x01101000
#define	MR_FLUSH_CTRL_CACHE						0x01
#define	MR_FLUSH_DISK_CACHE						0x02

#define	MR_DCMD_CTRL_SHUTDOWN					0x01050000
#define	MR_DCMD_HIBERNATE_SHUTDOWN				0x01060000
#define	MR_ENABLE_DRIVE_SPINDOWN				0x01

#define	MR_DCMD_CTRL_EVENT_GET_INFO				0x01040100
#define	MR_DCMD_CTRL_EVENT_GET					0x01040300
#define	MR_DCMD_CTRL_EVENT_WAIT					0x01040500
#define	MR_DCMD_LD_GET_PROPERTIES				0x03030000

#define	MR_DCMD_CLUSTER							0x08000000
#define	MR_DCMD_CLUSTER_RESET_ALL				0x08010100
#define	MR_DCMD_CLUSTER_RESET_LD				0x08010200
#define	MR_DCMD_PD_LIST_QUERY					0x02010100

#define	MR_DCMD_CTRL_MISC_CPX					0x0100e200
#define	MR_DCMD_CTRL_MISC_CPX_INIT_DATA_GET		0x0100e201
#define	MR_DCMD_CTRL_MISC_CPX_QUEUE_DATA		0x0100e202
#define	MR_DCMD_CTRL_MISC_CPX_UNREGISTER		0x0100e203
#define	MAX_MR_ROW_SIZE							32
#define	MR_CPX_DIR_WRITE						1
#define	MR_CPX_DIR_READ							0
#define	MR_CPX_VERSION							1

#define	MR_DCMD_CTRL_IO_METRICS_GET				0x01170200

#define	MR_EVT_CFG_CLEARED						0x0004

#define	MR_EVT_LD_STATE_CHANGE					0x0051
#define	MR_EVT_PD_INSERTED						0x005b
#define	MR_EVT_PD_REMOVED						0x0070
#define	MR_EVT_LD_CREATED						0x008a
#define	MR_EVT_LD_DELETED						0x008b
#define	MR_EVT_FOREIGN_CFG_IMPORTED				0x00db
#define	MR_EVT_LD_OFFLINE						0x00fc
#define	MR_EVT_CTRL_HOST_BUS_SCAN_REQUESTED		0x0152
#define	MR_EVT_CTRL_PERF_COLLECTION				0x017e

/*
 * MFI command completion codes
 */
enum MFI_STAT {
	MFI_STAT_OK = 0x00,
	MFI_STAT_INVALID_CMD = 0x01,
	MFI_STAT_INVALID_DCMD = 0x02,
	MFI_STAT_INVALID_PARAMETER = 0x03,
	MFI_STAT_INVALID_SEQUENCE_NUMBER = 0x04,
	MFI_STAT_ABORT_NOT_POSSIBLE = 0x05,
	MFI_STAT_APP_HOST_CODE_NOT_FOUND = 0x06,
	MFI_STAT_APP_IN_USE = 0x07,
	MFI_STAT_APP_NOT_INITIALIZED = 0x08,
	MFI_STAT_ARRAY_INDEX_INVALID = 0x09,
	MFI_STAT_ARRAY_ROW_NOT_EMPTY = 0x0a,
	MFI_STAT_CONFIG_RESOURCE_CONFLICT = 0x0b,
	MFI_STAT_DEVICE_NOT_FOUND = 0x0c,
	MFI_STAT_DRIVE_TOO_SMALL = 0x0d,
	MFI_STAT_FLASH_ALLOC_FAIL = 0x0e,
	MFI_STAT_FLASH_BUSY = 0x0f,
	MFI_STAT_FLASH_ERROR = 0x10,
	MFI_STAT_FLASH_IMAGE_BAD = 0x11,
	MFI_STAT_FLASH_IMAGE_INCOMPLETE = 0x12,
	MFI_STAT_FLASH_NOT_OPEN = 0x13,
	MFI_STAT_FLASH_NOT_STARTED = 0x14,
	MFI_STAT_FLUSH_FAILED = 0x15,
	MFI_STAT_HOST_CODE_NOT_FOUNT = 0x16,
	MFI_STAT_LD_CC_IN_PROGRESS = 0x17,
	MFI_STAT_LD_INIT_IN_PROGRESS = 0x18,
	MFI_STAT_LD_LBA_OUT_OF_RANGE = 0x19,
	MFI_STAT_LD_MAX_CONFIGURED = 0x1a,
	MFI_STAT_LD_NOT_OPTIMAL = 0x1b,
	MFI_STAT_LD_RBLD_IN_PROGRESS = 0x1c,
	MFI_STAT_LD_RECON_IN_PROGRESS = 0x1d,
	MFI_STAT_LD_WRONG_RAID_LEVEL = 0x1e,
	MFI_STAT_MAX_SPARES_EXCEEDED = 0x1f,
	MFI_STAT_MEMORY_NOT_AVAILABLE = 0x20,
	MFI_STAT_MFC_HW_ERROR = 0x21,
	MFI_STAT_NO_HW_PRESENT = 0x22,
	MFI_STAT_NOT_FOUND = 0x23,
	MFI_STAT_NOT_IN_ENCL = 0x24,
	MFI_STAT_PD_CLEAR_IN_PROGRESS = 0x25,
	MFI_STAT_PD_TYPE_WRONG = 0x26,
	MFI_STAT_PR_DISABLED = 0x27,
	MFI_STAT_ROW_INDEX_INVALID = 0x28,
	MFI_STAT_SAS_CONFIG_INVALID_ACTION = 0x29,
	MFI_STAT_SAS_CONFIG_INVALID_DATA = 0x2a,
	MFI_STAT_SAS_CONFIG_INVALID_PAGE = 0x2b,
	MFI_STAT_SAS_CONFIG_INVALID_TYPE = 0x2c,
	MFI_STAT_SCSI_DONE_WITH_ERROR = 0x2d,
	MFI_STAT_SCSI_IO_FAILED = 0x2e,
	MFI_STAT_SCSI_RESERVATION_CONFLICT = 0x2f,
	MFI_STAT_SHUTDOWN_FAILED = 0x30,
	MFI_STAT_TIME_NOT_SET = 0x31,
	MFI_STAT_WRONG_STATE = 0x32,
	MFI_STAT_LD_OFFLINE = 0x33,
	MFI_STAT_PEER_NOTIFICATION_REJECTED = 0x34,
	MFI_STAT_PEER_NOTIFICATION_FAILED = 0x35,
	MFI_STAT_RESERVATION_IN_PROGRESS = 0x36,
	MFI_STAT_I2C_ERRORS_DETECTED = 0x37,
	MFI_STAT_PCI_ERRORS_DETECTED = 0x38,
	MFI_STAT_CONFIG_SEQ_MISMATCH = 0x67,

	MFI_STAT_INVALID_STATUS = 0xFF
};

/*
 * Number of mailbox bytes in DCMD message frame
 */
#define	MFI_MBOX_SIZE	12

enum MR_EVT_CLASS {

	MR_EVT_CLASS_DEBUG = -2,
	MR_EVT_CLASS_PROGRESS = -1,
	MR_EVT_CLASS_INFO = 0,
	MR_EVT_CLASS_WARNING = 1,
	MR_EVT_CLASS_CRITICAL = 2,
	MR_EVT_CLASS_FATAL = 3,
	MR_EVT_CLASS_DEAD = 4,

};

enum MR_EVT_LOCALE {

	MR_EVT_LOCALE_LD = 0x0001,
	MR_EVT_LOCALE_PD = 0x0002,
	MR_EVT_LOCALE_ENCL = 0x0004,
	MR_EVT_LOCALE_BBU = 0x0008,
	MR_EVT_LOCALE_SAS = 0x0010,
	MR_EVT_LOCALE_CTRL = 0x0020,
	MR_EVT_LOCALE_CONFIG = 0x0040,
	MR_EVT_LOCALE_CLUSTER = 0x0080,
	MR_EVT_LOCALE_ALL = 0xffff,

};

enum MR_EVT_ARGS {

	MR_EVT_ARGS_NONE,
	MR_EVT_ARGS_CDB_SENSE,
	MR_EVT_ARGS_LD,
	MR_EVT_ARGS_LD_COUNT,
	MR_EVT_ARGS_LD_LBA,
	MR_EVT_ARGS_LD_OWNER,
	MR_EVT_ARGS_LD_LBA_PD_LBA,
	MR_EVT_ARGS_LD_PROG,
	MR_EVT_ARGS_LD_STATE,
	MR_EVT_ARGS_LD_STRIP,
	MR_EVT_ARGS_PD,
	MR_EVT_ARGS_PD_ERR,
	MR_EVT_ARGS_PD_LBA,
	MR_EVT_ARGS_PD_LBA_LD,
	MR_EVT_ARGS_PD_PROG,
	MR_EVT_ARGS_PD_STATE,
	MR_EVT_ARGS_PCI,
	MR_EVT_ARGS_RATE,
	MR_EVT_ARGS_STR,
	MR_EVT_ARGS_TIME,
	MR_EVT_ARGS_ECC,
	MR_EVT_ARGS_LD_PROP,
	MR_EVT_ARGS_PD_SPARE,
	MR_EVT_ARGS_PD_INDEX,
	MR_EVT_ARGS_DIAG_PASS,
	MR_EVT_ARGS_DIAG_FAIL,
	MR_EVT_ARGS_PD_LBA_LBA,
	MR_EVT_ARGS_PORT_PHY,
	MR_EVT_ARGS_PD_MISSING,
	MR_EVT_ARGS_PD_ADDRESS,
	MR_EVT_ARGS_BITMAP,
	MR_EVT_ARGS_CONNECTOR,
	MR_EVT_ARGS_PD_PD,
	MR_EVT_ARGS_PD_FRU,
	MR_EVT_ARGS_PD_PATHINFO,
	MR_EVT_ARGS_PD_POWER_STATE,
	MR_EVT_ARGS_GENERIC,
};

/*
 * Thunderbolt (and later) Defines
 */
#define	MEGASAS_CHAIN_FRAME_SZ_MIN					1024
#define	MFI_FUSION_ENABLE_INTERRUPT_MASK			(0x00000009)
#define	MRSAS_MPI2_RAID_DEFAULT_IO_FRAME_SIZE		256
#define	MRSAS_MPI2_FUNCTION_PASSTHRU_IO_REQUEST		0xF0
#define	MRSAS_MPI2_FUNCTION_LD_IO_REQUEST			0xF1
#define	MRSAS_LOAD_BALANCE_FLAG						0x1
#define	MRSAS_DCMD_MBOX_PEND_FLAG					0x1
#define	HOST_DIAG_WRITE_ENABLE						0x80
#define	HOST_DIAG_RESET_ADAPTER						0x4
#define	MRSAS_TBOLT_MAX_RESET_TRIES					3
#define MRSAS_MAX_MFI_CMDS                          16
#define MRSAS_MAX_IOCTL_CMDS                        3

/*
 * Invader Defines
 */
#define	MPI2_TYPE_CUDA								0x2
#define	MPI25_SAS_DEVICE0_FLAGS_ENABLED_FAST_PATH	0x4000
#define	MR_RL_FLAGS_GRANT_DESTINATION_CPU0			0x00
#define	MR_RL_FLAGS_GRANT_DESTINATION_CPU1			0x10
#define	MR_RL_FLAGS_GRANT_DESTINATION_CUDA			0x80
#define	MR_RL_FLAGS_SEQ_NUM_ENABLE					0x8
#define	MR_RL_WRITE_THROUGH_MODE					0x00
#define	MR_RL_WRITE_BACK_MODE						0x01

/*
 * T10 PI defines
 */
#define	MR_PROT_INFO_TYPE_CONTROLLER				0x8
#define	MRSAS_SCSI_VARIABLE_LENGTH_CMD				0x7f
#define	MRSAS_SCSI_SERVICE_ACTION_READ32			0x9
#define	MRSAS_SCSI_SERVICE_ACTION_WRITE32			0xB
#define	MRSAS_SCSI_ADDL_CDB_LEN						0x18
#define	MRSAS_RD_WR_PROTECT_CHECK_ALL				0x20
#define	MRSAS_RD_WR_PROTECT_CHECK_NONE				0x60
#define	MRSAS_SCSIBLOCKSIZE							512

/*
 * Raid context flags
 */
#define	MR_RAID_CTX_RAID_FLAGS_IO_SUB_TYPE_SHIFT	0x4
#define	MR_RAID_CTX_RAID_FLAGS_IO_SUB_TYPE_MASK		0x30
typedef enum MR_RAID_FLAGS_IO_SUB_TYPE {
	MR_RAID_FLAGS_IO_SUB_TYPE_NONE = 0,
	MR_RAID_FLAGS_IO_SUB_TYPE_SYSTEM_PD = 1,
	MR_RAID_FLAGS_IO_SUB_TYPE_RMW_DATA = 2,
	MR_RAID_FLAGS_IO_SUB_TYPE_RMW_P = 3,
	MR_RAID_FLAGS_IO_SUB_TYPE_RMW_Q = 4,
	MR_RAID_FLAGS_IO_SUB_TYPE_CACHE_BYPASS = 6,
	MR_RAID_FLAGS_IO_SUB_TYPE_LDIO_BW_LIMIT = 7
} MR_RAID_FLAGS_IO_SUB_TYPE;
/*
 * Request descriptor types
 */
#define	MRSAS_REQ_DESCRIPT_FLAGS_LD_IO		0x7
#define	MRSAS_REQ_DESCRIPT_FLAGS_MFA		0x1
#define	MRSAS_REQ_DESCRIPT_FLAGS_NO_LOCK	0x2
#define	MRSAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT	1
#define	MRSAS_FP_CMD_LEN					16
#define	MRSAS_FUSION_IN_RESET				0

#define	RAID_CTX_SPANARM_ARM_SHIFT			(0)
#define	RAID_CTX_SPANARM_ARM_MASK			(0x1f)
#define	RAID_CTX_SPANARM_SPAN_SHIFT			(5)
#define	RAID_CTX_SPANARM_SPAN_MASK			(0xE0)

/*
 * Define region lock types
 */
typedef enum _REGION_TYPE {
	REGION_TYPE_UNUSED = 0,
	REGION_TYPE_SHARED_READ = 1,
	REGION_TYPE_SHARED_WRITE = 2,
	REGION_TYPE_EXCLUSIVE = 3,
}	REGION_TYPE;


/*
 * SCSI-CAM Related Defines
 */
#define	MRSAS_SCSI_MAX_LUNS				0
#define	MRSAS_SCSI_INITIATOR_ID			255
#define	MRSAS_SCSI_MAX_CMDS				8
#define	MRSAS_SCSI_MAX_CDB_LEN			16
#define	MRSAS_SCSI_SENSE_BUFFERSIZE		96
#define	MRSAS_INTERNAL_CMDS				32
#define	MRSAS_FUSION_INT_CMDS			8

#define	MEGASAS_MAX_CHAIN_SIZE_UNITS_MASK	0x400000
#define	MEGASAS_MAX_CHAIN_SIZE_MASK		0x3E0
#define	MEGASAS_256K_IO					128
#define	MEGASAS_1MB_IO					(MEGASAS_256K_IO * 4)

/* Request types */
#define	MRSAS_REQ_TYPE_INTERNAL_CMD		0x0
#define	MRSAS_REQ_TYPE_AEN_FETCH		0x1
#define	MRSAS_REQ_TYPE_PASSTHRU			0x2
#define	MRSAS_REQ_TYPE_GETSET_PARAM		0x3
#define	MRSAS_REQ_TYPE_SCSI_IO			0x4

/* Request states */
#define	MRSAS_REQ_STATE_FREE			0
#define	MRSAS_REQ_STATE_BUSY			1
#define	MRSAS_REQ_STATE_TRAN			2
#define	MRSAS_REQ_STATE_COMPLETE		3

typedef enum _MR_SCSI_CMD_TYPE {
	READ_WRITE_LDIO = 0,
	NON_READ_WRITE_LDIO = 1,
	READ_WRITE_SYSPDIO = 2,
	NON_READ_WRITE_SYSPDIO = 3,
}	MR_SCSI_CMD_TYPE;

enum mrsas_req_flags {
	MRSAS_DIR_UNKNOWN = 0x1,
	MRSAS_DIR_IN = 0x2,
	MRSAS_DIR_OUT = 0x4,
	MRSAS_DIR_NONE = 0x8,
};

/*
 * Adapter Reset States
 */
enum {
	MRSAS_HBA_OPERATIONAL = 0,
	MRSAS_ADPRESET_SM_INFAULT = 1,
	MRSAS_ADPRESET_SM_FW_RESET_SUCCESS = 2,
	MRSAS_ADPRESET_SM_OPERATIONAL = 3,
	MRSAS_HW_CRITICAL_ERROR = 4,
	MRSAS_ADPRESET_INPROG_SIGN = 0xDEADDEAD,
};

/*
 * MPT Command Structure
 */
struct mrsas_mpt_cmd {
	MRSAS_RAID_SCSI_IO_REQUEST *io_request;
	bus_addr_t io_request_phys_addr;
	MPI2_SGE_IO_UNION *chain_frame;
	bus_addr_t chain_frame_phys_addr;
	u_int32_t sge_count;
	u_int8_t *sense;
	bus_addr_t sense_phys_addr;
	u_int8_t retry_for_fw_reset;
	MRSAS_REQUEST_DESCRIPTOR_UNION *request_desc;
	u_int32_t sync_cmd_idx;
	u_int32_t index;
	u_int8_t flags;
	u_int8_t pd_r1_lb;
	u_int8_t load_balance;
	bus_size_t length;
	u_int32_t error_code;
	bus_dmamap_t data_dmamap;
	void   *data;
	union ccb *ccb_ptr;
	struct callout cm_callout;
	struct mrsas_softc *sc;
	boolean_t tmCapable;
	u_int16_t r1_alt_dev_handle;
	boolean_t cmd_completed;
	struct mrsas_mpt_cmd *peer_cmd;
	bool	callout_owner;
	TAILQ_ENTRY(mrsas_mpt_cmd) next;
	u_int8_t pdInterface;
};

/*
 * MFI Command Structure
 */
struct mrsas_mfi_cmd {
	union mrsas_frame *frame;
	bus_dmamap_t frame_dmamap;
	void   *frame_mem;
	bus_addr_t frame_phys_addr;
	u_int8_t *sense;
	bus_dmamap_t sense_dmamap;
	void   *sense_mem;
	bus_addr_t sense_phys_addr;
	u_int32_t index;
	u_int8_t sync_cmd;
	u_int8_t cmd_status;
	u_int8_t abort_aen;
	u_int8_t retry_for_fw_reset;
	struct mrsas_softc *sc;
	union ccb *ccb_ptr;
	union {
		struct {
			u_int16_t smid;
			u_int16_t resvd;
		}	context;
		u_int32_t frame_count;
	}	cmd_id;
	TAILQ_ENTRY(mrsas_mfi_cmd) next;
};


/*
 * define constants for device list query options
 */
enum MR_PD_QUERY_TYPE {
	MR_PD_QUERY_TYPE_ALL = 0,
	MR_PD_QUERY_TYPE_STATE = 1,
	MR_PD_QUERY_TYPE_POWER_STATE = 2,
	MR_PD_QUERY_TYPE_MEDIA_TYPE = 3,
	MR_PD_QUERY_TYPE_SPEED = 4,
	MR_PD_QUERY_TYPE_EXPOSED_TO_HOST = 5,
};

#define	MR_EVT_CFG_CLEARED						0x0004
#define	MR_EVT_LD_STATE_CHANGE					0x0051
#define	MR_EVT_PD_INSERTED						0x005b
#define	MR_EVT_PD_REMOVED						0x0070
#define	MR_EVT_LD_CREATED						0x008a
#define	MR_EVT_LD_DELETED						0x008b
#define	MR_EVT_FOREIGN_CFG_IMPORTED				0x00db
#define	MR_EVT_LD_OFFLINE						0x00fc
#define	MR_EVT_CTRL_PROP_CHANGED				0x012f
#define	MR_EVT_CTRL_HOST_BUS_SCAN_REQUESTED		0x0152

enum MR_PD_STATE {
	MR_PD_STATE_UNCONFIGURED_GOOD = 0x00,
	MR_PD_STATE_UNCONFIGURED_BAD = 0x01,
	MR_PD_STATE_HOT_SPARE = 0x02,
	MR_PD_STATE_OFFLINE = 0x10,
	MR_PD_STATE_FAILED = 0x11,
	MR_PD_STATE_REBUILD = 0x14,
	MR_PD_STATE_ONLINE = 0x18,
	MR_PD_STATE_COPYBACK = 0x20,
	MR_PD_STATE_SYSTEM = 0x40,
};

/*
 * defines the physical drive address structure
 */
#pragma pack(1)
struct MR_PD_ADDRESS {
	u_int16_t deviceId;
	u_int16_t enclDeviceId;

	union {
		struct {
			u_int8_t enclIndex;
			u_int8_t slotNumber;
		}	mrPdAddress;
		struct {
			u_int8_t enclPosition;
			u_int8_t enclConnectorIndex;
		}	mrEnclAddress;
	}	u1;
	u_int8_t scsiDevType;
	union {
		u_int8_t connectedPortBitmap;
		u_int8_t connectedPortNumbers;
	}	u2;
	u_int64_t sasAddr[2];
};

#pragma pack()

/*
 * defines the physical drive list structure
 */
#pragma pack(1)
struct MR_PD_LIST {
	u_int32_t size;
	u_int32_t count;
	struct MR_PD_ADDRESS addr[1];
};

#pragma pack()

#pragma pack(1)
struct mrsas_pd_list {
	u_int16_t tid;
	u_int8_t driveType;
	u_int8_t driveState;
};

#pragma pack()

/*
 * defines the logical drive reference structure
 */
typedef union _MR_LD_REF {
	struct {
		u_int8_t targetId;
		u_int8_t reserved;
		u_int16_t seqNum;
	}	ld_context;
	u_int32_t ref;
}	MR_LD_REF;


/*
 * defines the logical drive list structure
 */
#pragma pack(1)
struct MR_LD_LIST {
	u_int32_t ldCount;
	u_int32_t reserved;
	struct {
		MR_LD_REF ref;
		u_int8_t state;
		u_int8_t reserved[3];
		u_int64_t size;
	}	ldList[MAX_LOGICAL_DRIVES_EXT];
};

#pragma pack()

/*
 * SAS controller properties
 */
#pragma pack(1)
struct mrsas_ctrl_prop {
	u_int16_t seq_num;
	u_int16_t pred_fail_poll_interval;
	u_int16_t intr_throttle_count;
	u_int16_t intr_throttle_timeouts;
	u_int8_t rebuild_rate;
	u_int8_t patrol_read_rate;
	u_int8_t bgi_rate;
	u_int8_t cc_rate;
	u_int8_t recon_rate;
	u_int8_t cache_flush_interval;
	u_int8_t spinup_drv_count;
	u_int8_t spinup_delay;
	u_int8_t cluster_enable;
	u_int8_t coercion_mode;
	u_int8_t alarm_enable;
	u_int8_t disable_auto_rebuild;
	u_int8_t disable_battery_warn;
	u_int8_t ecc_bucket_size;
	u_int16_t ecc_bucket_leak_rate;
	u_int8_t restore_hotspare_on_insertion;
	u_int8_t expose_encl_devices;
	u_int8_t maintainPdFailHistory;
	u_int8_t disallowHostRequestReordering;
	u_int8_t abortCCOnError;
	u_int8_t loadBalanceMode;
	u_int8_t disableAutoDetectBackplane;
	u_int8_t snapVDSpace;
	/*
	 * Add properties that can be controlled by a bit in the following
	 * structure.
	 */
	struct {
		u_int32_t copyBackDisabled:1;
		u_int32_t SMARTerEnabled:1;
		u_int32_t prCorrectUnconfiguredAreas:1;
		u_int32_t useFdeOnly:1;
		u_int32_t disableNCQ:1;
		u_int32_t SSDSMARTerEnabled:1;
		u_int32_t SSDPatrolReadEnabled:1;
		u_int32_t enableSpinDownUnconfigured:1;
		u_int32_t autoEnhancedImport:1;
		u_int32_t enableSecretKeyControl:1;
		u_int32_t disableOnlineCtrlReset:1;
		u_int32_t allowBootWithPinnedCache:1;
		u_int32_t disableSpinDownHS:1;
		u_int32_t enableJBOD:1;
		u_int32_t disableCacheBypass:1;
		u_int32_t useDiskActivityForLocate:1;
		u_int32_t enablePI:1;
		u_int32_t preventPIImport:1;
		u_int32_t useGlobalSparesForEmergency:1;
		u_int32_t useUnconfGoodForEmergency:1;
		u_int32_t useEmergencySparesforSMARTer:1;
		u_int32_t forceSGPIOForQuadOnly:1;
		u_int32_t enableConfigAutoBalance:1;
		u_int32_t enableVirtualCache:1;
		u_int32_t enableAutoLockRecovery:1;
		u_int32_t disableImmediateIO:1;
		u_int32_t disableT10RebuildAssist:1;
		u_int32_t ignore64ldRestriction:1;
		u_int32_t enableSwZone:1;
		u_int32_t limitMaxRateSATA3G:1;
		u_int32_t reserved:2;
	}	OnOffProperties;
	u_int8_t autoSnapVDSpace;
	u_int8_t viewSpace;
	u_int16_t spinDownTime;
	u_int8_t reserved[24];

};

#pragma pack()


/*
 * SAS controller information
 */
struct mrsas_ctrl_info {
	/*
	 * PCI device information
	 */
	struct {
		u_int16_t vendor_id;
		u_int16_t device_id;
		u_int16_t sub_vendor_id;
		u_int16_t sub_device_id;
		u_int8_t reserved[24];
	} __packed pci;
	/*
	 * Host interface information
	 */
	struct {
		u_int8_t PCIX:1;
		u_int8_t PCIE:1;
		u_int8_t iSCSI:1;
		u_int8_t SAS_3G:1;
		u_int8_t reserved_0:4;
		u_int8_t reserved_1[6];
		u_int8_t port_count;
		u_int64_t port_addr[8];
	} __packed host_interface;
	/*
	 * Device (backend) interface information
	 */
	struct {
		u_int8_t SPI:1;
		u_int8_t SAS_3G:1;
		u_int8_t SATA_1_5G:1;
		u_int8_t SATA_3G:1;
		u_int8_t reserved_0:4;
		u_int8_t reserved_1[6];
		u_int8_t port_count;
		u_int64_t port_addr[8];
	} __packed device_interface;

	u_int32_t image_check_word;
	u_int32_t image_component_count;

	struct {
		char	name[8];
		char	version[32];
		char	build_date[16];
		char	built_time[16];
	} __packed image_component[8];

	u_int32_t pending_image_component_count;

	struct {
		char	name[8];
		char	version[32];
		char	build_date[16];
		char	build_time[16];
	} __packed pending_image_component[8];

	u_int8_t max_arms;
	u_int8_t max_spans;
	u_int8_t max_arrays;
	u_int8_t max_lds;
	char	product_name[80];
	char	serial_no[32];

	/*
	 * Other physical/controller/operation information. Indicates the
	 * presence of the hardware
	 */
	struct {
		u_int32_t bbu:1;
		u_int32_t alarm:1;
		u_int32_t nvram:1;
		u_int32_t uart:1;
		u_int32_t reserved:28;
	} __packed hw_present;

	u_int32_t current_fw_time;

	/*
	 * Maximum data transfer sizes
	 */
	u_int16_t max_concurrent_cmds;
	u_int16_t max_sge_count;
	u_int32_t max_request_size;

	/*
	 * Logical and physical device counts
	 */
	u_int16_t ld_present_count;
	u_int16_t ld_degraded_count;
	u_int16_t ld_offline_count;

	u_int16_t pd_present_count;
	u_int16_t pd_disk_present_count;
	u_int16_t pd_disk_pred_failure_count;
	u_int16_t pd_disk_failed_count;

	/*
	 * Memory size information
	 */
	u_int16_t nvram_size;
	u_int16_t memory_size;
	u_int16_t flash_size;

	/*
	 * Error counters
	 */
	u_int16_t mem_correctable_error_count;
	u_int16_t mem_uncorrectable_error_count;

	/*
	 * Cluster information
	 */
	u_int8_t cluster_permitted;
	u_int8_t cluster_active;

	/*
	 * Additional max data transfer sizes
	 */
	u_int16_t max_strips_per_io;

	/*
	 * Controller capabilities structures
	 */
	struct {
		u_int32_t raid_level_0:1;
		u_int32_t raid_level_1:1;
		u_int32_t raid_level_5:1;
		u_int32_t raid_level_1E:1;
		u_int32_t raid_level_6:1;
		u_int32_t reserved:27;
	} __packed raid_levels;

	struct {
		u_int32_t rbld_rate:1;
		u_int32_t cc_rate:1;
		u_int32_t bgi_rate:1;
		u_int32_t recon_rate:1;
		u_int32_t patrol_rate:1;
		u_int32_t alarm_control:1;
		u_int32_t cluster_supported:1;
		u_int32_t bbu:1;
		u_int32_t spanning_allowed:1;
		u_int32_t dedicated_hotspares:1;
		u_int32_t revertible_hotspares:1;
		u_int32_t foreign_config_import:1;
		u_int32_t self_diagnostic:1;
		u_int32_t mixed_redundancy_arr:1;
		u_int32_t global_hot_spares:1;
		u_int32_t reserved:17;
	} __packed adapter_operations;

	struct {
		u_int32_t read_policy:1;
		u_int32_t write_policy:1;
		u_int32_t io_policy:1;
		u_int32_t access_policy:1;
		u_int32_t disk_cache_policy:1;
		u_int32_t reserved:27;
	} __packed ld_operations;

	struct {
		u_int8_t min;
		u_int8_t max;
		u_int8_t reserved[2];
	} __packed stripe_sz_ops;

	struct {
		u_int32_t force_online:1;
		u_int32_t force_offline:1;
		u_int32_t force_rebuild:1;
		u_int32_t reserved:29;
	} __packed pd_operations;

	struct {
		u_int32_t ctrl_supports_sas:1;
		u_int32_t ctrl_supports_sata:1;
		u_int32_t allow_mix_in_encl:1;
		u_int32_t allow_mix_in_ld:1;
		u_int32_t allow_sata_in_cluster:1;
		u_int32_t reserved:27;
	} __packed pd_mix_support;

	/*
	 * Define ECC single-bit-error bucket information
	 */
	u_int8_t ecc_bucket_count;
	u_int8_t reserved_2[11];

	/*
	 * Include the controller properties (changeable items)
	 */
	struct mrsas_ctrl_prop properties;

	/*
	 * Define FW pkg version (set in envt v'bles on OEM basis)
	 */
	char	package_version[0x60];

	u_int64_t deviceInterfacePortAddr2[8];
	u_int8_t reserved3[128];

	struct {
		u_int16_t minPdRaidLevel_0:4;
		u_int16_t maxPdRaidLevel_0:12;

		u_int16_t minPdRaidLevel_1:4;
		u_int16_t maxPdRaidLevel_1:12;

		u_int16_t minPdRaidLevel_5:4;
		u_int16_t maxPdRaidLevel_5:12;

		u_int16_t minPdRaidLevel_1E:4;
		u_int16_t maxPdRaidLevel_1E:12;

		u_int16_t minPdRaidLevel_6:4;
		u_int16_t maxPdRaidLevel_6:12;

		u_int16_t minPdRaidLevel_10:4;
		u_int16_t maxPdRaidLevel_10:12;

		u_int16_t minPdRaidLevel_50:4;
		u_int16_t maxPdRaidLevel_50:12;

		u_int16_t minPdRaidLevel_60:4;
		u_int16_t maxPdRaidLevel_60:12;

		u_int16_t minPdRaidLevel_1E_RLQ0:4;
		u_int16_t maxPdRaidLevel_1E_RLQ0:12;

		u_int16_t minPdRaidLevel_1E0_RLQ0:4;
		u_int16_t maxPdRaidLevel_1E0_RLQ0:12;

		u_int16_t reserved[6];
	}	pdsForRaidLevels;

	u_int16_t maxPds;		/* 0x780 */
	u_int16_t maxDedHSPs;		/* 0x782 */
	u_int16_t maxGlobalHSPs;	/* 0x784 */
	u_int16_t ddfSize;		/* 0x786 */
	u_int8_t maxLdsPerArray;	/* 0x788 */
	u_int8_t partitionsInDDF;	/* 0x789 */
	u_int8_t lockKeyBinding;	/* 0x78a */
	u_int8_t maxPITsPerLd;		/* 0x78b */
	u_int8_t maxViewsPerLd;		/* 0x78c */
	u_int8_t maxTargetId;		/* 0x78d */
	u_int16_t maxBvlVdSize;		/* 0x78e */

	u_int16_t maxConfigurableSSCSize;	/* 0x790 */
	u_int16_t currentSSCsize;	/* 0x792 */

	char	expanderFwVersion[12];	/* 0x794 */

	u_int16_t PFKTrialTimeRemaining;/* 0x7A0 */

	u_int16_t cacheMemorySize;	/* 0x7A2 */

	struct {			/* 0x7A4 */
		u_int32_t supportPIcontroller:1;
		u_int32_t supportLdPIType1:1;
		u_int32_t supportLdPIType2:1;
		u_int32_t supportLdPIType3:1;
		u_int32_t supportLdBBMInfo:1;
		u_int32_t supportShieldState:1;
		u_int32_t blockSSDWriteCacheChange:1;
		u_int32_t supportSuspendResumeBGops:1;
		u_int32_t supportEmergencySpares:1;
		u_int32_t supportSetLinkSpeed:1;
		u_int32_t supportBootTimePFKChange:1;
		u_int32_t supportJBOD:1;
		u_int32_t disableOnlinePFKChange:1;
		u_int32_t supportPerfTuning:1;
		u_int32_t supportSSDPatrolRead:1;
		u_int32_t realTimeScheduler:1;

		u_int32_t supportResetNow:1;
		u_int32_t supportEmulatedDrives:1;
		u_int32_t headlessMode:1;
		u_int32_t dedicatedHotSparesLimited:1;


		u_int32_t supportUnevenSpans:1;
		u_int32_t reserved:11;
	}	adapterOperations2;

	u_int8_t driverVersion[32];	/* 0x7A8 */
	u_int8_t maxDAPdCountSpinup60;	/* 0x7C8 */
	u_int8_t temperatureROC;	/* 0x7C9 */
	u_int8_t temperatureCtrl;	/* 0x7CA */
	u_int8_t reserved4;		/* 0x7CB */
	u_int16_t maxConfigurablePds;	/* 0x7CC */


	u_int8_t reserved5[2];		/* 0x7CD reserved */

	struct {
		u_int32_t peerIsPresent:1;
		u_int32_t peerIsIncompatible:1;

		u_int32_t hwIncompatible:1;
		u_int32_t fwVersionMismatch:1;
		u_int32_t ctrlPropIncompatible:1;
		u_int32_t premiumFeatureMismatch:1;
		u_int32_t reserved:26;
	}	cluster;

	char	clusterId[16];		/* 0x7D4 */

	char	reserved6[4];		/* 0x7E4 RESERVED FOR IOV */

	struct {			/* 0x7E8 */
		u_int32_t supportPersonalityChange:2;
		u_int32_t supportThermalPollInterval:1;
		u_int32_t supportDisableImmediateIO:1;
		u_int32_t supportT10RebuildAssist:1;
		u_int32_t supportMaxExtLDs:1;
		u_int32_t supportCrashDump:1;
		u_int32_t supportSwZone:1;
		u_int32_t supportDebugQueue:1;
		u_int32_t supportNVCacheErase:1;
		u_int32_t supportForceTo512e:1;
		u_int32_t supportHOQRebuild:1;
		u_int32_t supportAllowedOpsforDrvRemoval:1;
		u_int32_t supportDrvActivityLEDSetting:1;
		u_int32_t supportNVDRAM:1;
		u_int32_t supportForceFlash:1;
		u_int32_t supportDisableSESMonitoring:1;
		u_int32_t supportCacheBypassModes:1;
		u_int32_t supportSecurityonJBOD:1;
		u_int32_t discardCacheDuringLDDelete:1;
		u_int32_t supportTTYLogCompression:1;
		u_int32_t supportCPLDUpdate:1;
		u_int32_t supportDiskCacheSettingForSysPDs:1;
		u_int32_t supportExtendedSSCSize:1;
		u_int32_t useSeqNumJbodFP:1;
		u_int32_t reserved:7;
	}	adapterOperations3;

	u_int8_t pad_cpld[16];

	struct {
		u_int16_t ctrlInfoExtSupported:1;
		u_int16_t supportIbuttonLess:1;
		u_int16_t supportedEncAlgo:1;
		u_int16_t supportEncryptedMfc:1;
		u_int16_t imageUploadSupported:1;
		u_int16_t supportSESCtrlInMultipathCfg:1;
		u_int16_t supportPdMapTargetId:1;
		u_int16_t FWSwapsBBUVPDInfo:1;
		u_int16_t reserved:8;
	}	adapterOperations4;

	u_int8_t pad[0x800 - 0x7FE];	/* 0x7FE */
} __packed;

/*
 * When SCSI mid-layer calls driver's reset routine, driver waits for
 * MRSAS_RESET_WAIT_TIME seconds for all outstanding IO to complete. Note
 * that the driver cannot _actually_ abort or reset pending commands. While
 * it is waiting for the commands to complete, it prints a diagnostic message
 * every MRSAS_RESET_NOTICE_INTERVAL seconds
 */
#define	MRSAS_RESET_WAIT_TIME			180
#define	MRSAS_INTERNAL_CMD_WAIT_TIME	180
#define	MRSAS_RESET_NOTICE_INTERVAL		5
#define	MRSAS_IOCTL_CMD					0
#define	MRSAS_DEFAULT_CMD_TIMEOUT		90
#define	MRSAS_THROTTLE_QUEUE_DEPTH		16

/*
 * MSI-x regsiters offset defines
 */
#define	MPI2_SUP_REPLY_POST_HOST_INDEX_OFFSET	(0x0000030C)
#define	MPI2_REPLY_POST_HOST_INDEX_OFFSET		(0x0000006C)
#define	MR_MAX_REPLY_QUEUES_OFFSET				(0x0000001F)
#define	MR_MAX_REPLY_QUEUES_EXT_OFFSET			(0x003FC000)
#define	MR_MAX_REPLY_QUEUES_EXT_OFFSET_SHIFT	14
#define	MR_MAX_MSIX_REG_ARRAY					16

/*
 * SYNC CACHE offset define
 */
#define MR_CAN_HANDLE_SYNC_CACHE_OFFSET     0X01000000

#define MR_ATOMIC_DESCRIPTOR_SUPPORT_OFFSET (1 << 24)

/*
 * FW reports the maximum of number of commands that it can accept (maximum
 * commands that can be outstanding) at any time. The driver must report a
 * lower number to the mid layer because it can issue a few internal commands
 * itself (E.g, AEN, abort cmd, IOCTLs etc). The number of commands it needs
 * is shown below
 */
#define	MRSAS_INT_CMDS			32
#define	MRSAS_SKINNY_INT_CMDS	5
#define	MRSAS_MAX_MSIX_QUEUES	128

/*
 * FW can accept both 32 and 64 bit SGLs. We want to allocate 32/64 bit SGLs
 * based on the size of bus_addr_t
 */
#define	IS_DMA64							(sizeof(bus_addr_t) == 8)

#define	MFI_XSCALE_OMR0_CHANGE_INTERRUPT	0x00000001
#define	MFI_INTR_FLAG_REPLY_MESSAGE			0x00000001
#define	MFI_INTR_FLAG_FIRMWARE_STATE_CHANGE	0x00000002
#define	MFI_G2_OUTBOUND_DOORBELL_CHANGE_INTERRUPT	0x00000004

#define	MFI_OB_INTR_STATUS_MASK				0x00000002
#define	MFI_POLL_TIMEOUT_SECS				60

#define	MFI_REPLY_1078_MESSAGE_INTERRUPT	0x80000000
#define	MFI_REPLY_GEN2_MESSAGE_INTERRUPT	0x00000001
#define	MFI_GEN2_ENABLE_INTERRUPT_MASK		0x00000001
#define	MFI_REPLY_SKINNY_MESSAGE_INTERRUPT	0x40000000
#define	MFI_SKINNY_ENABLE_INTERRUPT_MASK	(0x00000001)
#define	MFI_1068_PCSR_OFFSET				0x84
#define	MFI_1068_FW_HANDSHAKE_OFFSET		0x64
#define	MFI_1068_FW_READY					0xDDDD0000

typedef union _MFI_CAPABILITIES {
	struct {
		u_int32_t support_fp_remote_lun:1;
		u_int32_t support_additional_msix:1;
		u_int32_t support_fastpath_wb:1;
		u_int32_t support_max_255lds:1;
		u_int32_t support_ndrive_r1_lb:1;
		u_int32_t support_core_affinity:1;
		u_int32_t security_protocol_cmds_fw:1;
		u_int32_t support_ext_queue_depth:1;
		u_int32_t support_ext_io_size:1;
		u_int32_t reserved:23;
	}	mfi_capabilities;
	u_int32_t reg;
}	MFI_CAPABILITIES;

#pragma pack(1)
struct mrsas_sge32 {
	u_int32_t phys_addr;
	u_int32_t length;
};

#pragma pack()

#pragma pack(1)
struct mrsas_sge64 {
	u_int64_t phys_addr;
	u_int32_t length;
};

#pragma pack()

#pragma pack()
union mrsas_sgl {
	struct mrsas_sge32 sge32[1];
	struct mrsas_sge64 sge64[1];
};

#pragma pack()

#pragma pack(1)
struct mrsas_header {
	u_int8_t cmd;			/* 00e */
	u_int8_t sense_len;		/* 01h */
	u_int8_t cmd_status;		/* 02h */
	u_int8_t scsi_status;		/* 03h */

	u_int8_t target_id;		/* 04h */
	u_int8_t lun;			/* 05h */
	u_int8_t cdb_len;		/* 06h */
	u_int8_t sge_count;		/* 07h */

	u_int32_t context;		/* 08h */
	u_int32_t pad_0;		/* 0Ch */

	u_int16_t flags;		/* 10h */
	u_int16_t timeout;		/* 12h */
	u_int32_t data_xferlen;		/* 14h */
};

#pragma pack()

#pragma pack(1)
struct mrsas_init_frame {
	u_int8_t cmd;			/* 00h */
	u_int8_t reserved_0;		/* 01h */
	u_int8_t cmd_status;		/* 02h */

	u_int8_t reserved_1;		/* 03h */
	MFI_CAPABILITIES driver_operations;	/* 04h */
	u_int32_t context;		/* 08h */
	u_int32_t pad_0;		/* 0Ch */

	u_int16_t flags;		/* 10h */
	u_int16_t reserved_3;		/* 12h */
	u_int32_t data_xfer_len;	/* 14h */

	u_int32_t queue_info_new_phys_addr_lo;	/* 18h */
	u_int32_t queue_info_new_phys_addr_hi;	/* 1Ch */
	u_int32_t queue_info_old_phys_addr_lo;	/* 20h */
	u_int32_t queue_info_old_phys_addr_hi;	/* 24h */
	u_int32_t driver_ver_lo;	/* 28h */
	u_int32_t driver_ver_hi;	/* 2Ch */
	u_int32_t reserved_4[4];	/* 30h */
};

#pragma pack()

#pragma pack(1)
struct mrsas_io_frame {
	u_int8_t cmd;			/* 00h */
	u_int8_t sense_len;		/* 01h */
	u_int8_t cmd_status;		/* 02h */
	u_int8_t scsi_status;		/* 03h */

	u_int8_t target_id;		/* 04h */
	u_int8_t access_byte;		/* 05h */
	u_int8_t reserved_0;		/* 06h */
	u_int8_t sge_count;		/* 07h */

	u_int32_t context;		/* 08h */
	u_int32_t pad_0;		/* 0Ch */

	u_int16_t flags;		/* 10h */
	u_int16_t timeout;		/* 12h */
	u_int32_t lba_count;		/* 14h */

	u_int32_t sense_buf_phys_addr_lo;	/* 18h */
	u_int32_t sense_buf_phys_addr_hi;	/* 1Ch */

	u_int32_t start_lba_lo;		/* 20h */
	u_int32_t start_lba_hi;		/* 24h */

	union mrsas_sgl sgl;		/* 28h */
};

#pragma pack()

#pragma pack(1)
struct mrsas_pthru_frame {
	u_int8_t cmd;			/* 00h */
	u_int8_t sense_len;		/* 01h */
	u_int8_t cmd_status;		/* 02h */
	u_int8_t scsi_status;		/* 03h */

	u_int8_t target_id;		/* 04h */
	u_int8_t lun;			/* 05h */
	u_int8_t cdb_len;		/* 06h */
	u_int8_t sge_count;		/* 07h */

	u_int32_t context;		/* 08h */
	u_int32_t pad_0;		/* 0Ch */

	u_int16_t flags;		/* 10h */
	u_int16_t timeout;		/* 12h */
	u_int32_t data_xfer_len;	/* 14h */

	u_int32_t sense_buf_phys_addr_lo;	/* 18h */
	u_int32_t sense_buf_phys_addr_hi;	/* 1Ch */

	u_int8_t cdb[16];		/* 20h */
	union mrsas_sgl sgl;		/* 30h */
};

#pragma pack()

#pragma pack(1)
struct mrsas_dcmd_frame {
	u_int8_t cmd;			/* 00h */
	u_int8_t reserved_0;		/* 01h */
	u_int8_t cmd_status;		/* 02h */
	u_int8_t reserved_1[4];		/* 03h */
	u_int8_t sge_count;		/* 07h */

	u_int32_t context;		/* 08h */
	u_int32_t pad_0;		/* 0Ch */

	u_int16_t flags;		/* 10h */
	u_int16_t timeout;		/* 12h */

	u_int32_t data_xfer_len;	/* 14h */
	u_int32_t opcode;		/* 18h */

	union {				/* 1Ch */
		u_int8_t b[12];
		u_int16_t s[6];
		u_int32_t w[3];
	}	mbox;

	union mrsas_sgl sgl;		/* 28h */
};

#pragma pack()

#pragma pack(1)
struct mrsas_abort_frame {
	u_int8_t cmd;			/* 00h */
	u_int8_t reserved_0;		/* 01h */
	u_int8_t cmd_status;		/* 02h */

	u_int8_t reserved_1;		/* 03h */
	MFI_CAPABILITIES driver_operations;	/* 04h */
	u_int32_t context;		/* 08h */
	u_int32_t pad_0;		/* 0Ch */

	u_int16_t flags;		/* 10h */
	u_int16_t reserved_3;		/* 12h */
	u_int32_t reserved_4;		/* 14h */

	u_int32_t abort_context;	/* 18h */
	u_int32_t pad_1;		/* 1Ch */

	u_int32_t abort_mfi_phys_addr_lo;	/* 20h */
	u_int32_t abort_mfi_phys_addr_hi;	/* 24h */

	u_int32_t reserved_5[6];	/* 28h */
};

#pragma pack()

#pragma pack(1)
struct mrsas_smp_frame {
	u_int8_t cmd;			/* 00h */
	u_int8_t reserved_1;		/* 01h */
	u_int8_t cmd_status;		/* 02h */
	u_int8_t connection_status;	/* 03h */

	u_int8_t reserved_2[3];		/* 04h */
	u_int8_t sge_count;		/* 07h */

	u_int32_t context;		/* 08h */
	u_int32_t pad_0;		/* 0Ch */

	u_int16_t flags;		/* 10h */
	u_int16_t timeout;		/* 12h */

	u_int32_t data_xfer_len;	/* 14h */
	u_int64_t sas_addr;		/* 18h */

	union {
		struct mrsas_sge32 sge32[2];	/* [0]: resp [1]: req */
		struct mrsas_sge64 sge64[2];	/* [0]: resp [1]: req */
	}	sgl;
};

#pragma pack()


#pragma pack(1)
struct mrsas_stp_frame {
	u_int8_t cmd;			/* 00h */
	u_int8_t reserved_1;		/* 01h */
	u_int8_t cmd_status;		/* 02h */
	u_int8_t reserved_2;		/* 03h */

	u_int8_t target_id;		/* 04h */
	u_int8_t reserved_3[2];		/* 05h */
	u_int8_t sge_count;		/* 07h */

	u_int32_t context;		/* 08h */
	u_int32_t pad_0;		/* 0Ch */

	u_int16_t flags;		/* 10h */
	u_int16_t timeout;		/* 12h */

	u_int32_t data_xfer_len;	/* 14h */

	u_int16_t fis[10];		/* 18h */
	u_int32_t stp_flags;

	union {
		struct mrsas_sge32 sge32[2];	/* [0]: resp [1]: data */
		struct mrsas_sge64 sge64[2];	/* [0]: resp [1]: data */
	}	sgl;
};

#pragma pack()

union mrsas_frame {
	struct mrsas_header hdr;
	struct mrsas_init_frame init;
	struct mrsas_io_frame io;
	struct mrsas_pthru_frame pthru;
	struct mrsas_dcmd_frame dcmd;
	struct mrsas_abort_frame abort;
	struct mrsas_smp_frame smp;
	struct mrsas_stp_frame stp;
	u_int8_t raw_bytes[64];
};

#pragma pack(1)
union mrsas_evt_class_locale {

	struct {
		u_int16_t locale;
		u_int8_t reserved;
		int8_t	class;
	} __packed members;

	u_int32_t word;

} __packed;

#pragma pack()


#pragma pack(1)
struct mrsas_evt_log_info {
	u_int32_t newest_seq_num;
	u_int32_t oldest_seq_num;
	u_int32_t clear_seq_num;
	u_int32_t shutdown_seq_num;
	u_int32_t boot_seq_num;

} __packed;

#pragma pack()

struct mrsas_progress {

	u_int16_t progress;
	u_int16_t elapsed_seconds;

} __packed;

struct mrsas_evtarg_ld {

	u_int16_t target_id;
	u_int8_t ld_index;
	u_int8_t reserved;

} __packed;

struct mrsas_evtarg_pd {
	u_int16_t device_id;
	u_int8_t encl_index;
	u_int8_t slot_number;

} __packed;

struct mrsas_evt_detail {

	u_int32_t seq_num;
	u_int32_t time_stamp;
	u_int32_t code;
	union mrsas_evt_class_locale cl;
	u_int8_t arg_type;
	u_int8_t reserved1[15];

	union {
		struct {
			struct mrsas_evtarg_pd pd;
			u_int8_t cdb_length;
			u_int8_t sense_length;
			u_int8_t reserved[2];
			u_int8_t cdb[16];
			u_int8_t sense[64];
		} __packed cdbSense;

		struct mrsas_evtarg_ld ld;

		struct {
			struct mrsas_evtarg_ld ld;
			u_int64_t count;
		} __packed ld_count;

		struct {
			u_int64_t lba;
			struct mrsas_evtarg_ld ld;
		} __packed ld_lba;

		struct {
			struct mrsas_evtarg_ld ld;
			u_int32_t prevOwner;
			u_int32_t newOwner;
		} __packed ld_owner;

		struct {
			u_int64_t ld_lba;
			u_int64_t pd_lba;
			struct mrsas_evtarg_ld ld;
			struct mrsas_evtarg_pd pd;
		} __packed ld_lba_pd_lba;

		struct {
			struct mrsas_evtarg_ld ld;
			struct mrsas_progress prog;
		} __packed ld_prog;

		struct {
			struct mrsas_evtarg_ld ld;
			u_int32_t prev_state;
			u_int32_t new_state;
		} __packed ld_state;

		struct {
			u_int64_t strip;
			struct mrsas_evtarg_ld ld;
		} __packed ld_strip;

		struct mrsas_evtarg_pd pd;

		struct {
			struct mrsas_evtarg_pd pd;
			u_int32_t err;
		} __packed pd_err;

		struct {
			u_int64_t lba;
			struct mrsas_evtarg_pd pd;
		} __packed pd_lba;

		struct {
			u_int64_t lba;
			struct mrsas_evtarg_pd pd;
			struct mrsas_evtarg_ld ld;
		} __packed pd_lba_ld;

		struct {
			struct mrsas_evtarg_pd pd;
			struct mrsas_progress prog;
		} __packed pd_prog;

		struct {
			struct mrsas_evtarg_pd pd;
			u_int32_t prevState;
			u_int32_t newState;
		} __packed pd_state;

		struct {
			u_int16_t vendorId;
			u_int16_t deviceId;
			u_int16_t subVendorId;
			u_int16_t subDeviceId;
		} __packed pci;

		u_int32_t rate;
		char	str[96];

		struct {
			u_int32_t rtc;
			u_int32_t elapsedSeconds;
		} __packed time;

		struct {
			u_int32_t ecar;
			u_int32_t elog;
			char	str[64];
		} __packed ecc;

		u_int8_t b[96];
		u_int16_t s[48];
		u_int32_t w[24];
		u_int64_t d[12];
	}	args;

	char	description[128];

} __packed;

struct mrsas_irq_context {
	struct mrsas_softc *sc;
	uint32_t MSIxIndex;
};

enum MEGASAS_OCR_REASON {
	FW_FAULT_OCR = 0,
	MFI_DCMD_TIMEOUT_OCR = 1,
};

/* Controller management info added to support Linux Emulator */
#define	MAX_MGMT_ADAPTERS               1024

struct mrsas_mgmt_info {
	u_int16_t count;
	struct mrsas_softc *sc_ptr[MAX_MGMT_ADAPTERS];
	int	max_index;
};

#define	PCI_TYPE0_ADDRESSES             6
#define	PCI_TYPE1_ADDRESSES             2
#define	PCI_TYPE2_ADDRESSES             5

typedef struct _MRSAS_DRV_PCI_COMMON_HEADER {
	u_int16_t vendorID;
	      //(ro)
	u_int16_t deviceID;
	      //(ro)
	u_int16_t command;
	      //Device control
	u_int16_t status;
	u_int8_t revisionID;
	      //(ro)
	u_int8_t progIf;
	      //(ro)
	u_int8_t subClass;
	      //(ro)
	u_int8_t baseClass;
	      //(ro)
	u_int8_t cacheLineSize;
	      //(ro +)
	u_int8_t latencyTimer;
	      //(ro +)
	u_int8_t headerType;
	      //(ro)
	u_int8_t bist;
	      //Built in self test

	union {
		struct _MRSAS_DRV_PCI_HEADER_TYPE_0 {
			u_int32_t baseAddresses[PCI_TYPE0_ADDRESSES];
			u_int32_t cis;
			u_int16_t subVendorID;
			u_int16_t subSystemID;
			u_int32_t romBaseAddress;
			u_int8_t capabilitiesPtr;
			u_int8_t reserved1[3];
			u_int32_t reserved2;
			u_int8_t interruptLine;
			u_int8_t interruptPin;
			      //(ro)
			u_int8_t minimumGrant;
			      //(ro)
			u_int8_t maximumLatency;
			      //(ro)
		}	type0;

		/*
	         * PCI to PCI Bridge
	         */

		struct _MRSAS_DRV_PCI_HEADER_TYPE_1 {
			u_int32_t baseAddresses[PCI_TYPE1_ADDRESSES];
			u_int8_t primaryBus;
			u_int8_t secondaryBus;
			u_int8_t subordinateBus;
			u_int8_t secondaryLatency;
			u_int8_t ioBase;
			u_int8_t ioLimit;
			u_int16_t secondaryStatus;
			u_int16_t memoryBase;
			u_int16_t memoryLimit;
			u_int16_t prefetchBase;
			u_int16_t prefetchLimit;
			u_int32_t prefetchBaseUpper32;
			u_int32_t prefetchLimitUpper32;
			u_int16_t ioBaseUpper16;
			u_int16_t ioLimitUpper16;
			u_int8_t capabilitiesPtr;
			u_int8_t reserved1[3];
			u_int32_t romBaseAddress;
			u_int8_t interruptLine;
			u_int8_t interruptPin;
			u_int16_t bridgeControl;
		}	type1;

		/*
	         * PCI to CARDBUS Bridge
	         */

		struct _MRSAS_DRV_PCI_HEADER_TYPE_2 {
			u_int32_t socketRegistersBaseAddress;
			u_int8_t capabilitiesPtr;
			u_int8_t reserved;
			u_int16_t secondaryStatus;
			u_int8_t primaryBus;
			u_int8_t secondaryBus;
			u_int8_t subordinateBus;
			u_int8_t secondaryLatency;
			struct {
				u_int32_t base;
				u_int32_t limit;
			}	range [PCI_TYPE2_ADDRESSES - 1];
			u_int8_t interruptLine;
			u_int8_t interruptPin;
			u_int16_t bridgeControl;
		}	type2;
	}	u;

}	MRSAS_DRV_PCI_COMMON_HEADER, *PMRSAS_DRV_PCI_COMMON_HEADER;

#define	MRSAS_DRV_PCI_COMMON_HEADER_SIZE sizeof(MRSAS_DRV_PCI_COMMON_HEADER)   //64 bytes

typedef struct _MRSAS_DRV_PCI_LINK_CAPABILITY {
	union {
		struct {
			u_int32_t linkSpeed:4;
			u_int32_t linkWidth:6;
			u_int32_t aspmSupport:2;
			u_int32_t losExitLatency:3;
			u_int32_t l1ExitLatency:3;
			u_int32_t rsvdp:6;
			u_int32_t portNumber:8;
		}	bits;

		u_int32_t asUlong;
	}	u;
}	MRSAS_DRV_PCI_LINK_CAPABILITY, *PMRSAS_DRV_PCI_LINK_CAPABILITY;

#define	MRSAS_DRV_PCI_LINK_CAPABILITY_SIZE sizeof(MRSAS_DRV_PCI_LINK_CAPABILITY)

typedef struct _MRSAS_DRV_PCI_LINK_STATUS_CAPABILITY {
	union {
		struct {
			u_int16_t linkSpeed:4;
			u_int16_t negotiatedLinkWidth:6;
			u_int16_t linkTrainingError:1;
			u_int16_t linkTraning:1;
			u_int16_t slotClockConfig:1;
			u_int16_t rsvdZ:3;
		}	bits;

		u_int16_t asUshort;
	}	u;
	u_int16_t reserved;
}	MRSAS_DRV_PCI_LINK_STATUS_CAPABILITY, *PMRSAS_DRV_PCI_LINK_STATUS_CAPABILITY;

#define	MRSAS_DRV_PCI_LINK_STATUS_CAPABILITY_SIZE sizeof(MRSAS_DRV_PCI_LINK_STATUS_CAPABILITY)


typedef struct _MRSAS_DRV_PCI_CAPABILITIES {
	MRSAS_DRV_PCI_LINK_CAPABILITY linkCapability;
	MRSAS_DRV_PCI_LINK_STATUS_CAPABILITY linkStatusCapability;
}	MRSAS_DRV_PCI_CAPABILITIES, *PMRSAS_DRV_PCI_CAPABILITIES;

#define	MRSAS_DRV_PCI_CAPABILITIES_SIZE sizeof(MRSAS_DRV_PCI_CAPABILITIES)

/* PCI information */
typedef struct _MRSAS_DRV_PCI_INFORMATION {
	u_int32_t busNumber;
	u_int8_t deviceNumber;
	u_int8_t functionNumber;
	u_int8_t interruptVector;
	u_int8_t reserved1;
	MRSAS_DRV_PCI_COMMON_HEADER pciHeaderInfo;
	MRSAS_DRV_PCI_CAPABILITIES capability;
	u_int32_t domainID;
	u_int8_t reserved2[28];
}	MRSAS_DRV_PCI_INFORMATION, *PMRSAS_DRV_PCI_INFORMATION;

typedef enum _MR_PD_TYPE {
	UNKNOWN_DRIVE = 0,
	PARALLEL_SCSI = 1,
	SAS_PD = 2,
	SATA_PD = 3,
	FC_PD = 4,
	NVME_PD = 5,
} MR_PD_TYPE;

typedef union	_MR_PD_REF {
	struct {
		u_int16_t	 deviceId;
		u_int16_t	 seqNum;
	} mrPdRef;
	u_int32_t	 ref;
} MR_PD_REF;

/*
 * define the DDF Type bit structure
 */
union MR_PD_DDF_TYPE {
	struct {
		union {
			struct {
				u_int16_t forcedPDGUID:1;
				u_int16_t inVD:1;
				u_int16_t isGlobalSpare:1;
				u_int16_t isSpare:1;
				u_int16_t isForeign:1;
				u_int16_t reserved:7;
				u_int16_t intf:4;
			} pdType;
			u_int16_t type;
		};
		u_int16_t reserved;
	} ddf;
	struct {
		u_int32_t reserved;
	} nonDisk;
	u_int32_t type;
} __packed;

/*
 * defines the progress structure
 */
union MR_PROGRESS {
	struct  {
		u_int16_t progress;
		union {
			u_int16_t elapsedSecs;
			u_int16_t elapsedSecsForLastPercent;
		};
	} mrProgress;
	u_int32_t w;
} __packed;

/*
 * defines the physical drive progress structure
 */
struct MR_PD_PROGRESS {
    struct {
        u_int32_t     rbld:1;
        u_int32_t     patrol:1;
        u_int32_t     clear:1;
        u_int32_t     copyBack:1;
        u_int32_t     erase:1;
        u_int32_t     locate:1;
        u_int32_t     reserved:26;
    } active;
    union MR_PROGRESS     rbld;
    union MR_PROGRESS     patrol;
    union {
        union MR_PROGRESS     clear;
        union MR_PROGRESS     erase;
    };

    struct {
        u_int32_t     rbld:1;
        u_int32_t     patrol:1;
        u_int32_t     clear:1;
        u_int32_t     copyBack:1;
        u_int32_t     erase:1;
        u_int32_t     reserved:27;
    } pause;

    union MR_PROGRESS     reserved[3];
} __packed;


struct  mrsas_pd_info {
	 MR_PD_REF	 ref;
	 u_int8_t		 inquiryData[96];
	 u_int8_t		 vpdPage83[64];

	 u_int8_t		 notSupported;
	 u_int8_t		 scsiDevType;

	 union {
		 u_int8_t		 connectedPortBitmap;
		 u_int8_t		 connectedPortNumbers;
	 };

	 u_int8_t		 deviceSpeed;
	 u_int32_t	 mediaErrCount;
	 u_int32_t	 otherErrCount;
	 u_int32_t	 predFailCount;
	 u_int32_t	 lastPredFailEventSeqNum;

	 u_int16_t	 fwState;
	 u_int8_t		 disabledForRemoval;
	 u_int8_t		 linkSpeed;
	 union MR_PD_DDF_TYPE  state;

	 struct {
		 u_int8_t		 count;
		 u_int8_t		 isPathBroken:4;
		 u_int8_t		 reserved3:3;
		 u_int8_t		 widePortCapable:1;

		 u_int8_t		 connectorIndex[2];
		 u_int8_t		 reserved[4];
		 u_int64_t	 sasAddr[2];
		 u_int8_t		 reserved2[16];
	 } pathInfo;

	 u_int64_t	 rawSize;
	 u_int64_t	 nonCoercedSize;
	 u_int64_t	 coercedSize;
	 u_int16_t	 enclDeviceId;
	 u_int8_t		 enclIndex;

	 union {
		 u_int8_t		 slotNumber;
		 u_int8_t		 enclConnectorIndex;
	 };

	struct MR_PD_PROGRESS progInfo;
	 u_int8_t		 badBlockTableFull;
	 u_int8_t		 unusableInCurrentConfig;
	 u_int8_t		 vpdPage83Ext[64];
	 u_int8_t		 powerState;
	 u_int8_t		 enclPosition;
	 u_int32_t		allowedOps;
	 u_int16_t	 copyBackPartnerId;
	 u_int16_t	 enclPartnerDeviceId;
	struct {
		 u_int16_t fdeCapable:1;
		 u_int16_t fdeEnabled:1;
		 u_int16_t secured:1;
		 u_int16_t locked:1;
		 u_int16_t foreign:1;
		 u_int16_t needsEKM:1;
		 u_int16_t reserved:10;
	 } security;
	 u_int8_t		 mediaType;
	 u_int8_t		 notCertified;
	 u_int8_t		 bridgeVendor[8];
	 u_int8_t		 bridgeProductIdentification[16];
	 u_int8_t		 bridgeProductRevisionLevel[4];
	 u_int8_t		 satBridgeExists;

	 u_int8_t		 interfaceType;
	 u_int8_t		 temperature;
	 u_int8_t		 emulatedBlockSize;
	 u_int16_t	 userDataBlockSize;
	 u_int16_t	 reserved2;

	 struct {
		 u_int32_t piType:3;
		 u_int32_t piFormatted:1;
		 u_int32_t piEligible:1;
		 u_int32_t NCQ:1;
		 u_int32_t WCE:1;
		 u_int32_t commissionedSpare:1;
		 u_int32_t emergencySpare:1;
		 u_int32_t ineligibleForSSCD:1;
		 u_int32_t ineligibleForLd:1;
		 u_int32_t useSSEraseType:1;
		 u_int32_t wceUnchanged:1;
		 u_int32_t supportScsiUnmap:1;
		 u_int32_t reserved:18;
	 } properties;

	 u_int64_t   shieldDiagCompletionTime;
	 u_int8_t    shieldCounter;

	 u_int8_t linkSpeedOther;
	 u_int8_t reserved4[2];

	 struct {
		u_int32_t bbmErrCountSupported:1;
		u_int32_t bbmErrCount:31;
	 } bbmErr;

	 u_int8_t reserved1[512-428];
} __packed;

struct mrsas_target {
	u_int16_t target_id;
	u_int32_t queue_depth;
	u_int8_t interface_type;
	u_int32_t max_io_size_kb;
} __packed;

#define MR_NVME_PAGE_SIZE_MASK		0x000000FF
#define MR_DEFAULT_NVME_PAGE_SIZE	4096
#define MR_DEFAULT_NVME_PAGE_SHIFT	12

/*******************************************************************
 * per-instance data
 ********************************************************************/
struct mrsas_softc {
	device_t mrsas_dev;
	struct cdev *mrsas_cdev;
	struct intr_config_hook mrsas_ich;
	struct cdev *mrsas_linux_emulator_cdev;
	uint16_t device_id;
	struct resource *reg_res;
	int	reg_res_id;
	bus_space_tag_t bus_tag;
	bus_space_handle_t bus_handle;
	bus_dma_tag_t mrsas_parent_tag;
	bus_dma_tag_t verbuf_tag;
	bus_dmamap_t verbuf_dmamap;
	void   *verbuf_mem;
	bus_addr_t verbuf_phys_addr;
	bus_dma_tag_t sense_tag;
	bus_dmamap_t sense_dmamap;
	void   *sense_mem;
	bus_addr_t sense_phys_addr;
	bus_dma_tag_t io_request_tag;
	bus_dmamap_t io_request_dmamap;
	void   *io_request_mem;
	bus_addr_t io_request_phys_addr;
	bus_dma_tag_t chain_frame_tag;
	bus_dmamap_t chain_frame_dmamap;
	void   *chain_frame_mem;
	bus_addr_t chain_frame_phys_addr;
	bus_dma_tag_t reply_desc_tag;
	bus_dmamap_t reply_desc_dmamap;
	void   *reply_desc_mem;
	bus_addr_t reply_desc_phys_addr;
	bus_dma_tag_t ioc_init_tag;
	bus_dmamap_t ioc_init_dmamap;
	void   *ioc_init_mem;
	bus_addr_t ioc_init_phys_mem;
	bus_dma_tag_t data_tag;
	struct cam_sim *sim_0;
	struct cam_sim *sim_1;
	struct cam_path *path_0;
	struct cam_path *path_1;
	struct mtx sim_lock;
	struct mtx pci_lock;
	struct mtx io_lock;
	struct mtx ioctl_lock;
	struct mtx mpt_cmd_pool_lock;
	struct mtx mfi_cmd_pool_lock;
	struct mtx raidmap_lock;
	struct mtx aen_lock;
	struct mtx stream_lock;
	struct selinfo mrsas_select;
	uint32_t mrsas_aen_triggered;
	uint32_t mrsas_poll_waiting;

	struct sema ioctl_count_sema;
	uint32_t max_fw_cmds;
	uint16_t max_scsi_cmds;
	uint32_t max_num_sge;
	struct resource *mrsas_irq[MAX_MSIX_COUNT];
	void   *intr_handle[MAX_MSIX_COUNT];
	int	irq_id[MAX_MSIX_COUNT];
	struct mrsas_irq_context irq_context[MAX_MSIX_COUNT];
	int	msix_vectors;
	int	msix_enable;
	uint32_t msix_reg_offset[16];
	uint8_t	mask_interrupts;
	uint16_t max_chain_frame_sz;
	struct mrsas_mpt_cmd **mpt_cmd_list;
	struct mrsas_mfi_cmd **mfi_cmd_list;
	TAILQ_HEAD(, mrsas_mpt_cmd) mrsas_mpt_cmd_list_head;
	TAILQ_HEAD(, mrsas_mfi_cmd) mrsas_mfi_cmd_list_head;
	bus_addr_t req_frames_desc_phys;
	u_int8_t *req_frames_desc;
	u_int8_t *req_desc;
	bus_addr_t io_request_frames_phys;
	u_int8_t *io_request_frames;
	bus_addr_t reply_frames_desc_phys;
	u_int16_t last_reply_idx[MAX_MSIX_COUNT];
	u_int32_t reply_q_depth;
	u_int32_t request_alloc_sz;
	u_int32_t reply_alloc_sz;
	u_int32_t io_frames_alloc_sz;
	u_int32_t chain_frames_alloc_sz;
	u_int16_t max_sge_in_main_msg;
	u_int16_t max_sge_in_chain;
	u_int8_t chain_offset_io_request;
	u_int8_t chain_offset_mfi_pthru;
	u_int32_t map_sz;
	u_int64_t map_id;
	u_int64_t pd_seq_map_id;
	struct mrsas_mfi_cmd *map_update_cmd;
	struct mrsas_mfi_cmd *jbod_seq_cmd;
	struct mrsas_mfi_cmd *aen_cmd;
	u_int8_t fast_path_io;
	void   *chan;
	void   *ocr_chan;
	u_int8_t adprecovery;
	u_int8_t remove_in_progress;
	u_int8_t ocr_thread_active;
	u_int8_t do_timedout_reset;
	u_int32_t reset_in_progress;
	u_int32_t reset_count;
	u_int32_t block_sync_cache;
	u_int32_t drv_stream_detection;
	u_int8_t fw_sync_cache_support;
	mrsas_atomic_t target_reset_outstanding;
#define MRSAS_MAX_TM_TARGETS (MRSAS_MAX_PD + MRSAS_MAX_LD_IDS)
    struct mrsas_mpt_cmd *target_reset_pool[MRSAS_MAX_TM_TARGETS];

	bus_dma_tag_t jbodmap_tag[2];
	bus_dmamap_t jbodmap_dmamap[2];
	void   *jbodmap_mem[2];
	bus_addr_t jbodmap_phys_addr[2];

	bus_dma_tag_t raidmap_tag[2];
	bus_dmamap_t raidmap_dmamap[2];
	void   *raidmap_mem[2];
	bus_addr_t raidmap_phys_addr[2];
	bus_dma_tag_t mficmd_frame_tag;
	bus_dma_tag_t mficmd_sense_tag;
	bus_addr_t evt_detail_phys_addr;
	bus_dma_tag_t evt_detail_tag;
	bus_dmamap_t evt_detail_dmamap;
	struct mrsas_evt_detail *evt_detail_mem;
	bus_addr_t pd_info_phys_addr;
	bus_dma_tag_t pd_info_tag;
	bus_dmamap_t pd_info_dmamap;
	struct mrsas_pd_info *pd_info_mem;
	struct mrsas_ctrl_info *ctrl_info;
	bus_dma_tag_t ctlr_info_tag;
	bus_dmamap_t ctlr_info_dmamap;
	void   *ctlr_info_mem;
	bus_addr_t ctlr_info_phys_addr;
	u_int32_t max_sectors_per_req;
	u_int32_t disableOnlineCtrlReset;
	mrsas_atomic_t fw_outstanding;
	mrsas_atomic_t prp_count;
	mrsas_atomic_t sge_holes;

	u_int32_t mrsas_debug;
	u_int32_t mrsas_io_timeout;
	u_int32_t mrsas_fw_fault_check_delay;
	u_int32_t io_cmds_highwater;
	u_int8_t UnevenSpanSupport;
	struct sysctl_ctx_list sysctl_ctx;
	struct sysctl_oid *sysctl_tree;
	struct proc *ocr_thread;
	u_int32_t last_seq_num;
	bus_dma_tag_t el_info_tag;
	bus_dmamap_t el_info_dmamap;
	void   *el_info_mem;
	bus_addr_t el_info_phys_addr;
	struct mrsas_pd_list pd_list[MRSAS_MAX_PD];
	struct mrsas_pd_list local_pd_list[MRSAS_MAX_PD];
	struct mrsas_target target_list[MRSAS_MAX_TM_TARGETS];
	u_int8_t ld_ids[MRSAS_MAX_LD_IDS];
	struct taskqueue *ev_tq;
	struct task ev_task;
	u_int32_t CurLdCount;
	u_int64_t reset_flags;
	int	lb_pending_cmds;
	LD_LOAD_BALANCE_INFO load_balance_info[MAX_LOGICAL_DRIVES_EXT];
	LD_SPAN_INFO log_to_span[MAX_LOGICAL_DRIVES_EXT];

	u_int8_t mrsas_gen3_ctrl;
	u_int8_t secure_jbod_support;
	u_int8_t use_seqnum_jbod_fp;
	/* FW suport for more than 256 PD/JBOD */
	u_int32_t support_morethan256jbod;
	u_int8_t max256vdSupport;
	u_int16_t fw_supported_vd_count;
	u_int16_t fw_supported_pd_count;

	u_int16_t drv_supported_vd_count;
	u_int16_t drv_supported_pd_count;

	u_int32_t max_map_sz;
	u_int32_t current_map_sz;
	u_int32_t old_map_sz;
	u_int32_t new_map_sz;
	u_int32_t drv_map_sz;

	u_int32_t nvme_page_size;
	boolean_t is_ventura;
	boolean_t is_aero;
	boolean_t msix_combined;
	boolean_t atomic_desc_support;
	u_int16_t maxRaidMapSize;

	/* Non dma-able memory. Driver local copy. */
	MR_DRV_RAID_MAP_ALL *ld_drv_map[2];
	PTR_LD_STREAM_DETECT  *streamDetectByLD;
};

/* Compatibility shims for different OS versions */
#if __FreeBSD_version >= 800001
#define	mrsas_kproc_create(func, farg, proc_ptr, flags, stackpgs, fmtstr, arg) \
    kproc_create(func, farg, proc_ptr, flags, stackpgs, fmtstr, arg)
#define	mrsas_kproc_exit(arg)   kproc_exit(arg)
#else
#define	mrsas_kproc_create(func, farg, proc_ptr, flags, stackpgs, fmtstr, arg) \
    kthread_create(func, farg, proc_ptr, flags, stackpgs, fmtstr, arg)
#define	mrsas_kproc_exit(arg)   kthread_exit(arg)
#endif

static __inline void
mrsas_clear_bit(int b, volatile void *p)
{
	atomic_clear_int(((volatile int *)p) + (b >> 5), 1 << (b & 0x1f));
}

static __inline void
mrsas_set_bit(int b, volatile void *p)
{
	atomic_set_int(((volatile int *)p) + (b >> 5), 1 << (b & 0x1f));
}

static __inline int
mrsas_test_bit(int b, volatile void *p)
{
	return ((volatile int *)p)[b >> 5] & (1 << (b & 0x1f));
}

#endif					/* MRSAS_H */
