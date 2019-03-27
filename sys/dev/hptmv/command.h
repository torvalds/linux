/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2005 HighPoint Technologies, Inc.
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
 * $FreeBSD$
 */
#ifndef _COMMAND_H_
#define _COMMAND_H_

/***************************************************************************
 * Description: Command
 ***************************************************************************/
typedef struct _AtaCommand
{
	LBA_T            Lba;          /* Current Logic Disk command: LBA   */
	USHORT           nSectors;     /* sector count. May great than 0x80 */	
	UCHAR            Command;      /* IDE_COMMAND_READ, _WRITE, _VERIFY */
	UCHAR            QueueTag;
} AtaComm, *PAtaComm;

typedef struct _PassthroughCmd {
	BYTE     bFeaturesReg;     /* feature register */
	BYTE     bSectorCountReg;  /* IDE sector count register. */
	BYTE     bLbaLowReg; /* IDE sector number register. */
	BYTE     bLbaMidReg;       /* IDE low order cylinder value. */
	BYTE     bLbaHighReg;      /* IDE high order cylinder value. */
	BYTE     bDriveHeadReg;    /* IDE drive/head register. */
	BYTE     bCommandReg;      /* Actual IDE command. Checked for validity by driver. */
	BYTE     nSectors;         /* data transfer */
	ADDRESS  pDataBuffer;      /* data buffer */
}
PassthroughCmd;

/* control commands */
#define CTRL_CMD_REBUILD 1
#define CTRL_CMD_VERIFY  2
#define CTRL_CMD_INIT    3

/* 
 * RAID5 rebuild/verify
 *   Rebuild/verify one stripe line.
 *   The caller needn't supply a buffer for rebuild.
 *   RebuildSectors member will be updated if its previous location is the 
 *   begin of this stripe line.
 */
typedef struct _R5ControlCmd {
	LBA_T  StripeLine;   /* _physical_ stripe line on array */
	USHORT Offset;       /* internal use, don't set */
	UCHAR  Command;      /* CTRL_CMD_XXX */
	UCHAR  reserve1;
}
R5ControlCmd, *PR5ControlCmd;

/* 
 * RAID1 rebuild/verify 
 *   Rebuild/verify specified sectors.
 *   The caller must supply a valid buffer and a physical SG table (or a
 *   pfnBuildSgl routine).
 *   For rebuild/initialize, the buffer size should be nSectors<<9;
 *   For verify, the buffer size should be (nSectors*2)<<9.
 *   RebuildSectors member will be updated if its previous value equals Lba.
 */
typedef struct _R1ControlCmd {
	LBA_T  Lba;
	USHORT nSectors;
	UCHAR  Command;      /* CTRL_CMD_XXX */
	UCHAR  reserve1;
	ADDRESS Buffer;  /* buffer logical address */
#ifdef _MACOSX_
	ADDRESS PhysicalAddress;
#endif
}
R1ControlCmd, *PR1ControlCmd;

typedef struct _Command
{
	PVDevice pVDevice;
	union{
		/* Ide Command */
		AtaComm Ide;
		PassthroughCmd Passthrough;
		/* Atapi Command */
		UCHAR Atapi[12];
		/* Control command */
		R5ControlCmd R5Control;
		R1ControlCmd R1Control;
	} uCmd;
	
	USHORT	cf_physical_sg: 1;
	USHORT	cf_data_in: 1;
	USHORT	cf_data_out: 1;
	USHORT	cf_atapi: 1;
	USHORT	cf_ide_passthrough: 1;
	USHORT  cf_control: 1;

	/* return status */
	UCHAR	Result;
	/* retry count */
	UCHAR   RetryCount;

	/* S/G table address, if already prepared */
	FPSCAT_GATH pSgTable;
	
	/* called if pSgTable is invalid. */
	int (* HPTLIBAPI pfnBuildSgl)(_VBUS_ARG PCommand pCmd, FPSCAT_GATH pSgTable, int logical);
	
	/* called when this command is finished */
	void (* HPTLIBAPI pfnCompletion)(_VBUS_ARG PCommand pCmd);
	
	/* pointer to original command */
	void *pOrgCommand;


	/* scratch data area */
	union {
		struct {
			LBA_T      StartLBA;
			UCHAR      FirstMember;    /* the sequence number of the first member */
			UCHAR      LastMember;     /* the sequence number of the last member */
			USHORT     LastSectors;    /* the number of sectors for the last member */
			USHORT     FirstSectors;   /* the number of sectors for the first member */
			USHORT     FirstOffset;    /* the offset from the StartLBA for the first member */
			USHORT     AllMemberBlocks;/* the number of sectors for all member */
			USHORT     WaitInterrupt;  /* bit map the members who wait interrupt */
			UCHAR      InSameLine;     /* if the start and end on the same line */
			UCHAR      pad1;
		} array;
		struct {
			LBA_T      StartLBA;
			USHORT     FirstSectors;   /* the number of sectors for the first member */
			USHORT     FirstOffset;    /* the offset from the StartLBA for the first member */
			USHORT     WaitInterrupt;  /* bit map the members who wait interrupt */
			USHORT     r5_gap;         /* see raid5.c */
			UCHAR      ParDiskNo;      /* parity for startLba */
			UCHAR      BadDiskNo;
			UCHAR      FirstMember;
			UCHAR      pad1;
		} r5;
		struct {
			PCommand pCmd1;
			PCommand pCmd2;
		} r5split;
#ifdef _RAID5N_
		struct {
			ULONG dummy[2]; /* uScratch.wait shall be moved out uScratch.
							   now just fix it thisway */
			struct range_lock *range_lock;
			struct stripe *stripes[5]; 
			UCHAR nstripes;
			UCHAR finished_stripes;
			USHORT pad2;
			/* for direct-read: */
			struct {
				UCHAR  cmds;
				UCHAR  finished;
				UCHAR  first;
				UCHAR  parity;
				LBA_T  base;
				USHORT firstoffset;
				USHORT firstsectors;
			} dr;
		} r5n2;
#endif
		struct {
			ULONG WordsLeft;
			FPSCAT_GATH pPIOSg;
			void (* HPTLIBAPI pfnOrgDone)(_VBUS_ARG PCommand pCmd);
#ifdef SUPPORT_HPT584
			UCHAR cmd;
#endif
		} disk;
		struct {
			PCommand pNext;
			void (* HPTLIBAPI WaitEntry)(_VBUS_ARG PCommand pCmd);
		} wait;
		
		struct {
			PVOID prdAddr;
			ULONG cmd_priv;
			USHORT responseFlags;
			UCHAR  bIdeStatus;
			UCHAR  errorRegister;
		} sata_param;
	} uScratch;
} Command;

/***************************************************************************
 * command return value
 ***************************************************************************/
#define   RETURN_PENDING             0
#define   RETURN_SUCCESS             1
#define   RETURN_BAD_DEVICE          2
#define   RETURN_BAD_PARAMETER       3
#define   RETURN_WRITE_NO_DRQ        4
#define   RETURN_DEVICE_BUSY         5
#define   RETURN_INVALID_REQUEST     6
#define   RETURN_SELECTION_TIMEOUT   7
#define   RETURN_IDE_ERROR           8
#define   RETURN_NEED_LOGICAL_SG     9
#define   RETURN_NEED_PHYSICAL_SG    10
#define   RETURN_RETRY               11
#define   RETURN_DATA_ERROR          12
#define   RETURN_BUS_RESET           13
#define   RETURN_BAD_TRANSFER_LENGTH 14

typedef void (* HPTLIBAPI DPC_PROC)(_VBUS_ARG void *);
typedef struct _dpc_routine {
	DPC_PROC proc;
	void *arg;
}
DPC_ROUTINE;

/*
 * MAX_QUEUE_COMM is defined in platform related compiler.h
 * to specify the maximum requests allowed (for each VBus) from system.
 *
 * Maximum command blocks needed for each VBus:
 *   Each OS command requests 1+MAX_MEMBERS*2 command blocks (RAID1/0 case)
 *   This space is allocated by platform dependent part, either static or 
 *   dynamic, continuous or non-continous.
 *   The code only needs _vbus_(pFreeCommands) to be set.
 *
 * PendingRoutines[] size:
 *   Each command may invoke CallAfterReturn once.
 *
 * IdleRoutines[] size:
 *   Each command may invoke CallWhenIdle once.
 */
#define MAX_COMMAND_BLOCKS_FOR_EACH_VBUS (MAX_QUEUE_COMM * (1+MAX_MEMBERS*2) + 1)
#define MAX_PENDING_ROUTINES  (MAX_COMMAND_BLOCKS_FOR_EACH_VBUS+1)
#define MAX_IDLE_ROUTINES     (MAX_COMMAND_BLOCKS_FOR_EACH_VBUS+1)

#define mWaitingForIdle(pVBus) ((pVBus)->IdleRoutinesFirst!=(pVBus)->IdleRoutinesLast)

PCommand HPTLIBAPI AllocateCommand(_VBUS_ARG0);
void FASTCALL FreeCommand(_VBUS_ARG PCommand pCmd);

void FASTCALL CallAfterReturn(_VBUS_ARG DPC_PROC proc, void *arg);
void HPTLIBAPI CheckPendingCall(_VBUS_ARG0);
void FASTCALL CallWhenIdle(_VBUS_ARG DPC_PROC proc, void *arg);
void HPTLIBAPI CheckIdleCall(_VBUS_ARG0);

void HPTLIBAPI AddToWaitingList(PCommand *ppList, PCommand pCmd);
void HPTLIBAPI DoWaitingList(_VBUS_ARG PCommand *ppList);

#endif
