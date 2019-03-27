/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006-2010 Adaptec, Inc.
 * Copyright (c) 2010-2012 PMC-Sierra, Inc.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Debugging support.
 */
#include "opt_aacraid.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/conf.h>

#include <sys/bus.h>

#include <machine/resource.h>
#include <machine/bus.h>

#include <dev/aacraid/aacraid_reg.h>
#include <sys/aac_ioctl.h>
#include <dev/aacraid/aacraid_var.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/conf.h>

#include <sys/bus.h>
#include <sys/rman.h>

#include <machine/resource.h>
#include <machine/bus.h>
#include <machine/stdarg.h>

#include <dev/aacraid/aacraid_debug.h>

#ifdef AACRAID_DEBUG
/*
 * Dump the command queue indices
 */
void
aacraid_print_queues(struct aac_softc *sc)
{
	device_printf(sc->aac_dev, "AACQ_FREE      %d/%d\n",
	    sc->aac_qstat[AACQ_FREE].q_length, sc->aac_qstat[AACQ_FREE].q_max);
	device_printf(sc->aac_dev, "AACQ_READY     %d/%d\n",
	    sc->aac_qstat[AACQ_READY].q_length,
	    sc->aac_qstat[AACQ_READY].q_max);
	device_printf(sc->aac_dev, "AACQ_BUSY      %d/%d\n",
	    sc->aac_qstat[AACQ_BUSY].q_length, sc->aac_qstat[AACQ_BUSY].q_max);
}

/*
 * Print a FIB
 */
void
aacraid_print_fib(struct aac_softc *sc, struct aac_fib *fib, const char *caller)
{
	if (fib == NULL) {
		device_printf(sc->aac_dev,
			      "aac_print_fib called with NULL fib\n");
		return;
	}
	device_printf(sc->aac_dev, "%s: FIB @ %p\n", caller, fib);
	device_printf(sc->aac_dev, "  XferState %b\n", fib->Header.XferState,
		      "\20"
		      "\1HOSTOWNED"
		      "\2ADAPTEROWNED"
		      "\3INITIALISED"
		      "\4EMPTY"
		      "\5FROMPOOL"
		      "\6FROMHOST"
		      "\7FROMADAP"
		      "\10REXPECTED"
		      "\11RNOTEXPECTED"
		      "\12DONEADAP"
		      "\13DONEHOST"
		      "\14HIGH"
		      "\15NORM"
		      "\16ASYNC"
		      "\17PAGEFILEIO"
		      "\20SHUTDOWN"
		      "\21LAZYWRITE"
		      "\22ADAPMICROFIB"
		      "\23BIOSFIB"
		      "\24FAST_RESPONSE"
		      "\25APIFIB\n");
	device_printf(sc->aac_dev, "  Command       %d\n", fib->Header.Command);
	device_printf(sc->aac_dev, "  StructType    %d\n",
		      fib->Header.StructType);
	device_printf(sc->aac_dev, "  Size          %d\n", fib->Header.Size);
	device_printf(sc->aac_dev, "  SenderSize    %d\n",
		      fib->Header.SenderSize);
	device_printf(sc->aac_dev, "  SenderAddress 0x%x\n",
		      fib->Header.SenderFibAddress);
	device_printf(sc->aac_dev, "  RcvrAddress   0x%x\n",
		      fib->Header.u.ReceiverFibAddress);
	device_printf(sc->aac_dev, "  Handle    0x%x\n",
		      fib->Header.Handle);
	switch(fib->Header.Command) {
	case ContainerCommand:
	{
		struct aac_blockread *br;
		struct aac_blockwrite *bw;
		struct aac_sg_table *sg;
		int i;

		br = (struct aac_blockread*)fib->data;
		bw = (struct aac_blockwrite*)fib->data;
		sg = NULL;

		if (br->Command == VM_CtBlockRead) {
			device_printf(sc->aac_dev,
				      "  BlockRead: container %d  0x%x/%d\n",
				      br->ContainerId, br->BlockNumber,
				      br->ByteCount);
			sg = &br->SgMap;
		}
		if (bw->Command == VM_CtBlockWrite) {
			device_printf(sc->aac_dev,
				      "  BlockWrite: container %d  0x%x/%d "
				      "(%s)\n", bw->ContainerId,
				      bw->BlockNumber, bw->ByteCount,
				      bw->Stable == CSTABLE ? "stable" :
				      "unstable");
			sg = &bw->SgMap;
		}
		if (sg != NULL) {
			device_printf(sc->aac_dev,
				      "  %d s/g entries\n", sg->SgCount);
			for (i = 0; i < sg->SgCount; i++)
				device_printf(sc->aac_dev, "  0x%08x/%d\n",
					      sg->SgEntry[i].SgAddress,
					      sg->SgEntry[i].SgByteCount);
		}
		break;
	}
	default:
		device_printf(sc->aac_dev, "   %16D\n", fib->data, " ");
		device_printf(sc->aac_dev, "   %16D\n", fib->data + 16, " ");
		break;
	}
}

/*
 * Describe an AIF we have received.
 */
void
aacraid_print_aif(struct aac_softc *sc, struct aac_aif_command *aif)
{
	switch(aif->command) {
	case AifCmdEventNotify:
		device_printf(sc->aac_dev, "EventNotify(%d)\n", aif->seqNumber);
		switch(aif->data.EN.type) {
		case AifEnGeneric:		/* Generic notification */
			device_printf(sc->aac_dev, "(Generic) %.*s\n",
				  (int)sizeof(aif->data.EN.data.EG),
				  aif->data.EN.data.EG.text);
			break;
		case AifEnTaskComplete:		/* Task has completed */
			device_printf(sc->aac_dev, "(TaskComplete)\n");
			break;
		case AifEnConfigChange:		/* Adapter configuration change
						 * occurred */
			device_printf(sc->aac_dev, "(ConfigChange)\n");
			break;
		case AifEnContainerChange:	/* Adapter specific container
						 * configuration change */
			device_printf(sc->aac_dev, "(ContainerChange) "
				      "container %d,%d\n",
				      aif->data.EN.data.ECC.container[0],
				      aif->data.EN.data.ECC.container[1]);
			break;
		case AifEnDeviceFailure:	/* SCSI device failed */
			device_printf(sc->aac_dev, "(DeviceFailure) "
				      "handle %d\n",
				      aif->data.EN.data.EDF.deviceHandle);
			break;
		case AifEnMirrorFailover:	/* Mirror failover started */
			device_printf(sc->aac_dev, "(MirrorFailover) "
				      "container %d failed, "
				      "migrating from slice %d to %d\n",
				      aif->data.EN.data.EMF.container,
				      aif->data.EN.data.EMF.failedSlice,
				      aif->data.EN.data.EMF.creatingSlice);
			break;
		case AifEnContainerEvent:	/* Significant container
						 * event */
			device_printf(sc->aac_dev, "(ContainerEvent) "
				      "container %d event "
				      "%d\n", aif->data.EN.data.ECE.container,
				      aif->data.EN.data.ECE.eventType);	
			break;
		case AifEnFileSystemChange:	/* File system changed */
			device_printf(sc->aac_dev, "(FileSystemChange)\n");
			break;
		case AifEnConfigPause:		/* Container pause event */
			device_printf(sc->aac_dev, "(ConfigPause)\n");
			break;
		case AifEnConfigResume:		/* Container resume event */
			device_printf(sc->aac_dev, "(ConfigResume)\n");
			break;
		case AifEnFailoverChange:	/* Failover space assignment
						 * changed */
			device_printf(sc->aac_dev, "(FailoverChange)\n");
			break;
		case AifEnRAID5RebuildDone:	/* RAID5 rebuild finished */
			device_printf(sc->aac_dev, "(RAID5RebuildDone)\n");
			break;
		case AifEnEnclosureManagement:	/* Enclosure management event */
			device_printf(sc->aac_dev, "(EnclosureManagement) "
				      "EMPID %d unit %d "
				      "event %d\n", aif->data.EN.data.EEE.empID,
				      aif->data.EN.data.EEE.unitID,
				      aif->data.EN.data.EEE.eventType);
			break;
		case AifEnBatteryEvent:		/* Significant NV battery
						 * event */
			device_printf(sc->aac_dev, "(BatteryEvent) %d "
				      "(state was %d, is %d\n",
				      aif->data.EN.data.EBE.transition_type,
				      aif->data.EN.data.EBE.current_state,
				      aif->data.EN.data.EBE.prior_state);
			break;
		case AifEnAddContainer:		/* A new container was
						 * created. */
			device_printf(sc->aac_dev, "(AddContainer)\n");
			break;		
		case AifEnDeleteContainer:	/* A container was deleted. */
			device_printf(sc->aac_dev, "(DeleteContainer)\n");
			break;
		case AifEnBatteryNeedsRecond:	/* The battery needs
						 * reconditioning */
			device_printf(sc->aac_dev, "(BatteryNeedsRecond)\n");
			break;
		case AifEnClusterEvent:		/* Some cluster event */
			device_printf(sc->aac_dev, "(ClusterEvent) event %d\n",
				      aif->data.EN.data.ECLE.eventType);
			break;
		case AifEnDiskSetEvent:		/* A disk set event occurred. */
			device_printf(sc->aac_dev, "(DiskSetEvent) event %d "
				      "diskset %jd creator %jd\n",
				      aif->data.EN.data.EDS.eventType,
				      (intmax_t)aif->data.EN.data.EDS.DsNum,
				      (intmax_t)aif->data.EN.data.EDS.CreatorId);
			break;
		case AifDenMorphComplete: 	/* A morph operation
						 * completed */
			device_printf(sc->aac_dev, "(MorphComplete)\n");
			break;
		case AifDenVolumeExtendComplete: /* A volume expand operation
						  * completed */
			device_printf(sc->aac_dev, "(VolumeExtendComplete)\n");
			break;
		default:
			device_printf(sc->aac_dev, "(%d)\n", aif->data.EN.type);
			break;
		}
		break;
	case AifCmdJobProgress:
	{
		char	*status;
		switch(aif->data.PR[0].status) {
		case AifJobStsSuccess:
			status = "success"; break;
		case AifJobStsFinished:
			status = "finished"; break;
		case AifJobStsAborted:
			status = "aborted"; break;
		case AifJobStsFailed:
			status = "failed"; break;
		case AifJobStsSuspended:
			status = "suspended"; break;
		case AifJobStsRunning:
			status = "running"; break;
		default:
			status = "unknown status"; break;
		}		
	
		device_printf(sc->aac_dev, "JobProgress (%d) - %s (%d, %d)\n",
			      aif->seqNumber, status,
			      aif->data.PR[0].currentTick,
			      aif->data.PR[0].finalTick);
		switch(aif->data.PR[0].jd.type) {
		case AifJobScsiZero:		/* SCSI dev clear operation */
			device_printf(sc->aac_dev, "(ScsiZero) handle %d\n",
				      aif->data.PR[0].jd.client.scsi_dh);
			break;
		case AifJobScsiVerify:		/* SCSI device Verify operation
						 * NO REPAIR */
			device_printf(sc->aac_dev, "(ScsiVerify) handle %d\n",
				      aif->data.PR[0].jd.client.scsi_dh);
			break;
		case AifJobScsiExercise:	/* SCSI device Exercise
						 * operation */
			device_printf(sc->aac_dev, "(ScsiExercise) handle %d\n",
				      aif->data.PR[0].jd.client.scsi_dh);
			break;
		case AifJobScsiVerifyRepair:	/* SCSI device Verify operation
						 * WITH repair */
			device_printf(sc->aac_dev,
				      "(ScsiVerifyRepair) handle %d\n",
				      aif->data.PR[0].jd.client.scsi_dh);
			break;
		case AifJobCtrZero:		/* Container clear operation */
			device_printf(sc->aac_dev,
				      "(ContainerZero) container %d\n",
				      aif->data.PR[0].jd.client.container.src);
			break;
		case AifJobCtrCopy:		/* Container copy operation */
			device_printf(sc->aac_dev,
				      "(ContainerCopy) container %d to %d\n",
				      aif->data.PR[0].jd.client.container.src,
				      aif->data.PR[0].jd.client.container.dst);
			break;
		case AifJobCtrCreateMirror:	/* Container Create Mirror
						 * operation */
			device_printf(sc->aac_dev,
				      "(ContainerCreateMirror) container %d\n",
				      aif->data.PR[0].jd.client.container.src);
				      /* XXX two containers? */
			break;
		case AifJobCtrMergeMirror:	/* Container Merge Mirror
						 * operation */
			device_printf(sc->aac_dev,
				      "(ContainerMergeMirror) container %d\n",
				      aif->data.PR[0].jd.client.container.src);
				      /* XXX two containers? */
			break;
		case AifJobCtrScrubMirror:	/* Container Scrub Mirror
						 * operation */
			device_printf(sc->aac_dev,
				      "(ContainerScrubMirror) container %d\n",
				      aif->data.PR[0].jd.client.container.src);
			break;
		case AifJobCtrRebuildRaid5:	/* Container Rebuild Raid5
						 * operation */
			device_printf(sc->aac_dev,
				      "(ContainerRebuildRaid5) container %d\n",
				      aif->data.PR[0].jd.client.container.src);
			break;
		case AifJobCtrScrubRaid5:	/* Container Scrub Raid5
						 * operation */
			device_printf(sc->aac_dev,
				      "(ContainerScrubRaid5) container %d\n",
				      aif->data.PR[0].jd.client.container.src);
			break;
		case AifJobCtrMorph:		/* Container morph operation */
			device_printf(sc->aac_dev,
				      "(ContainerMorph) container %d\n",
				      aif->data.PR[0].jd.client.container.src);
				      /* XXX two containers? */
			break;
		case AifJobCtrPartCopy:		/* Container Partition copy
						 * operation */
			device_printf(sc->aac_dev,
				      "(ContainerPartCopy) container %d to "
				      "%d\n",
				      aif->data.PR[0].jd.client.container.src,
				      aif->data.PR[0].jd.client.container.dst);
			break;
		case AifJobCtrRebuildMirror:	/* Container Rebuild Mirror
						 * operation */
			device_printf(sc->aac_dev,
				      "(ContainerRebuildMirror) container "
				      "%d\n",
				      aif->data.PR[0].jd.client.container.src);
			break;
		case AifJobCtrCrazyCache:	/* crazy cache */
			device_printf(sc->aac_dev,
				      "(ContainerCrazyCache) container %d\n",
				      aif->data.PR[0].jd.client.container.src);
				      /* XXX two containers? */
			break;
		case AifJobFsCreate:		/* File System Create
						 * operation */
			device_printf(sc->aac_dev, "(FsCreate)\n");
			break;
		case AifJobFsVerify:		/* File System Verify
						 * operation */
			device_printf(sc->aac_dev, "(FsVerivy)\n");
			break;
		case AifJobFsExtend:		/* File System Extend
						 * operation */
			device_printf(sc->aac_dev, "(FsExtend)\n");
			break;
		case AifJobApiFormatNTFS:	/* Format a drive to NTFS */
			device_printf(sc->aac_dev, "(FormatNTFS)\n");
			break;
		case AifJobApiFormatFAT:	/* Format a drive to FAT */
			device_printf(sc->aac_dev, "(FormatFAT)\n");
			break;
		case AifJobApiUpdateSnapshot:	/* update the read/write half
						 * of a snapshot */
			device_printf(sc->aac_dev, "(UpdateSnapshot)\n");
			break;
		case AifJobApiFormatFAT32:	/* Format a drive to FAT32 */
			device_printf(sc->aac_dev, "(FormatFAT32)\n");
			break;
		case AifJobCtlContinuousCtrVerify: /* Adapter operation */
			device_printf(sc->aac_dev, "(ContinuousCtrVerify)\n");
			break;
		default:
			device_printf(sc->aac_dev, "(%d)\n",
				      aif->data.PR[0].jd.type);
			break;
		}
		break;
	}
	case AifCmdAPIReport:
		device_printf(sc->aac_dev, "APIReport (%d)\n", aif->seqNumber);
		break;
	case AifCmdDriverNotify:
		device_printf(sc->aac_dev, "DriverNotify (%d)\n",
			      aif->seqNumber);
		break;
	default:
		device_printf(sc->aac_dev, "AIF %d (%d)\n", aif->command,
			      aif->seqNumber);
		break;
	}
}
#endif /* AACRAID_DEBUG */

/*
 * Debug flags to be put into the HBA flags field when initialized
 */
const unsigned long aacraid_debug_flags = /* Variable to setup with above flags. */
/*			HBA_FLAGS_DBG_KERNEL_PRINT_B |		*/
			HBA_FLAGS_DBG_FW_PRINT_B |
/*			HBA_FLAGS_DBG_FUNCTION_ENTRY_B |	*/
			HBA_FLAGS_DBG_FUNCTION_EXIT_B |
			HBA_FLAGS_DBG_ERROR_B |
			HBA_FLAGS_DBG_INIT_B |			
/*			HBA_FLAGS_DBG_OS_COMMANDS_B |		*/
/*			HBA_FLAGS_DBG_SCAN_B |			*/
/*			HBA_FLAGS_DBG_COALESCE_B |		*/
/*			HBA_FLAGS_DBG_IOCTL_COMMANDS_B |	*/
/*			HBA_FLAGS_DBG_SYNC_COMMANDS_B |		*/
			HBA_FLAGS_DBG_COMM_B |			
/*			HBA_FLAGS_DBG_AIF_B |			*/
/*			HBA_FLAGS_DBG_CSMI_COMMANDS_B | 	*/
			HBA_FLAGS_DBG_DEBUG_B | 		
/*			HBA_FLAGS_DBG_FLAGS_MASK | 		*/
0;

int aacraid_get_fw_debug_buffer(struct aac_softc *sc)
{
	u_int32_t MonDriverBufferPhysAddrLow = 0;
	u_int32_t MonDriverBufferPhysAddrHigh = 0;
	u_int32_t MonDriverBufferSize = 0;
	u_int32_t MonDriverHeaderSize = 0;

	/*
	 * Get the firmware print buffer parameters from the firmware
	 * If the command was successful map in the address.
	 */
	if (!aacraid_sync_command(sc, AAC_MONKER_GETDRVPROP, 0, 0, 0, 0, NULL, NULL)) {
		MonDriverBufferPhysAddrLow = AAC_GET_MAILBOX(sc, 1);
		MonDriverBufferPhysAddrHigh = AAC_GET_MAILBOX(sc, 2);
		MonDriverBufferSize = AAC_GET_MAILBOX(sc, 3);
		MonDriverHeaderSize = AAC_GET_MAILBOX(sc, 4); 
		if (MonDriverBufferSize) {
			unsigned long Offset = MonDriverBufferPhysAddrLow
				- rman_get_start(sc->aac_regs_res1);

			/*
			 * See if the address is already mapped in and if so set it up
			 * from the base address
			 */
			if ((MonDriverBufferPhysAddrHigh == 0) && 
				(Offset + MonDriverBufferSize < 
				rman_get_size(sc->aac_regs_res1))) {
				sc->DebugOffset = Offset;
				sc->DebugHeaderSize = MonDriverHeaderSize;
				sc->FwDebugBufferSize = MonDriverBufferSize;
				sc->FwDebugFlags = 0;
				sc->DebugFlags = aacraid_debug_flags;
				return 1;
			}
		}
	}

	/*
	 * The GET_DRIVER_BUFFER_PROPERTIES command failed
	 */
	return 0;
}

#define PRINT_TIMEOUT 250000 /* 1/4 second */

void aacraid_fw_printf(struct aac_softc *sc, unsigned long PrintFlags, const char * fmt, ...)
{
	va_list args;
	u_int32_t Count, i;
	char PrintBuffer_P[PRINT_BUFFER_SIZE];
	unsigned long PrintType;

	PrintType = PrintFlags & 
		~(HBA_FLAGS_DBG_KERNEL_PRINT_B|HBA_FLAGS_DBG_FW_PRINT_B);
	if (((PrintType!=0) && (sc!=NULL) && ((sc->DebugFlags & PrintType)==0))
		|| ((sc!=NULL) && (sc->DebugFlags
		& (HBA_FLAGS_DBG_KERNEL_PRINT_B|HBA_FLAGS_DBG_FW_PRINT_B)) == 0))
		return;

	/*
	 * Set up parameters and call sprintf function to format the data
	 */
	va_start(args, fmt);
	vsprintf(PrintBuffer_P, fmt, args);
	va_end(args);

	/*
	 * Make sure the HBA structure has been passed in for this section
	 */
	if ((sc != NULL) && (sc->FwDebugBufferSize)) {
		/*
		 * If we are set up for a Firmware print
		 */
		if ((sc->DebugFlags & HBA_FLAGS_DBG_FW_PRINT_B)
			&& ((PrintFlags
			& (HBA_FLAGS_DBG_KERNEL_PRINT_B|HBA_FLAGS_DBG_FW_PRINT_B))
			!= HBA_FLAGS_DBG_KERNEL_PRINT_B)) {
			/*
			 * Make sure the string size is within boundaries
			 */
			Count = strlen(PrintBuffer_P);
			if (Count > sc->FwDebugBufferSize)
				Count = (u_int16_t)sc->FwDebugBufferSize;

			/*
			 * Wait for no more than PRINT_TIMEOUT for the previous
			 * message length to clear (the handshake).
			 */
			for (i = 0; i < PRINT_TIMEOUT; ++i) {
				if (!AAC_MEM1_GETREG4(sc,
					sc->DebugOffset + FW_DEBUG_STR_LENGTH_OFFSET)) {
					break;
				}
				DELAY(1);
            }

			/*
			 * If the Length is clear, copy over the message, the
			 * flags, and the length. Make sure the length is the
			 * last because that is the signal for the Firmware to
			 * pick it up.
			 */
			if (!AAC_MEM1_GETREG4(sc,
				sc->DebugOffset + FW_DEBUG_STR_LENGTH_OFFSET)) {
				for (i = 0; i < Count; ++i) {
					AAC_MEM1_SETREG1(sc, sc->DebugOffset + sc->DebugHeaderSize + i,
								PrintBuffer_P[i]);
				}
				AAC_MEM1_SETREG4(sc, sc->DebugOffset + FW_DEBUG_FLAGS_OFFSET,
							sc->FwDebugFlags);
				AAC_MEM1_SETREG4(sc, sc->DebugOffset + FW_DEBUG_STR_LENGTH_OFFSET,
                            Count);
			} else
				sc->DebugFlags &= ~HBA_FLAGS_DBG_FW_PRINT_B;
		}

		/*
		 * If the Kernel Debug Print flag is set, send it off to the
		 * Kernel debugger
		 */
		if ((sc->DebugFlags & HBA_FLAGS_DBG_KERNEL_PRINT_B)
			&& ((PrintFlags
			& (HBA_FLAGS_DBG_KERNEL_PRINT_B|HBA_FLAGS_DBG_FW_PRINT_B))
			!= HBA_FLAGS_DBG_FW_PRINT_B)) {
			if (sc->FwDebugFlags & FW_DEBUG_FLAGS_NO_HEADERS_B)
				printf ("%s\n", PrintBuffer_P);
			else
				device_printf (sc->aac_dev, "%s\n", PrintBuffer_P);
		}

	} else {
		/*
		 * No HBA structure passed in so it has to be for the Kernel Debugger
		 */
		if ((sc != NULL) && (sc->FwDebugFlags & FW_DEBUG_FLAGS_NO_HEADERS_B))
			printf ("%s\n", PrintBuffer_P);
		else if (sc != NULL)
			device_printf (sc->aac_dev, "%s\n", PrintBuffer_P);
		else
			printf("%s\n", PrintBuffer_P);
	}
}

void aacraid_fw_print_mem(struct aac_softc *sc, unsigned long PrintFlags, u_int8_t *Addr, int Count)
{
	int Offset, i;
	u_int32_t DebugFlags = 0;
	char Buffer[100];
	char *LineBuffer_P;

	/*
	 * If we have an HBA structure, save off the flags and set the no
	 * headers flag so we don't have garbage between our lines of data
	 */
	if (sc != NULL) {
		DebugFlags = sc->FwDebugFlags;
		sc->FwDebugFlags |= FW_DEBUG_FLAGS_NO_HEADERS_B;
	}

	Offset = 0;

	/*
	 * Loop through all the data
	 */
	while (Offset < Count) {
		/*
		 * We will format each line into a buffer and then print out
		 * the entire line so set the pointer to the beginning of the
		 * buffer
		 */
		LineBuffer_P = Buffer;

		/*
		 * Set up the address in HEX
		 */
		sprintf(LineBuffer_P, "\n%04x  ", Offset);
		LineBuffer_P += 6;

		/*
		 * Set up 16 bytes in HEX format
		 */
		for (i = 0; i < 16; ++i) {
			/*
			 * If we are past the count of data bytes to output,
			 * pad with blanks
			 */
			if ((Offset + i) >= Count)
				sprintf (LineBuffer_P, "   ");
			else
			  	sprintf (LineBuffer_P, "%02x ", Addr[Offset+i]);
			LineBuffer_P += 3;

			/*
			 * At the mid point we will put in a divider
			 */
			if (i == 7) {
				sprintf (LineBuffer_P, "- ");
				LineBuffer_P += 2;
			}
		}
		/*
		 * Now do the same 16 bytes at the end of the line in ASCII
		 * format
		 */
		sprintf (LineBuffer_P, "  ");
		LineBuffer_P += 2;
		for (i = 0; i < 16; ++i) {
			/*
			 * If all data processed, OUT-O-HERE
			 */
			if ((Offset + i) >= Count)
				break;

			/*
			 * If this is a printable ASCII character, convert it
			 */
			if ((Addr[Offset+i] > 0x1F) && (Addr[Offset+i] < 0x7F))
				sprintf (LineBuffer_P, "%c", Addr[Offset+i]);
			else
				sprintf (LineBuffer_P, ".");
			++LineBuffer_P;
		}
		/*
		 * The line is now formatted, so print it out
		 */
		aacraid_fw_printf(sc, PrintFlags, "%s", Buffer);

		/*
		 * Bump the offset by 16 for the next line
		 */
		Offset += 16;

	}

	/*
	 * Restore the saved off flags
	 */
	if (sc != NULL)
		sc->FwDebugFlags = DebugFlags;
}

