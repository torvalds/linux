/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Michael Smith
 * Copyright (c) 2001 Scott Long
 * Copyright (c) 2000 BSDi
 * Copyright (c) 2001 Adaptec, Inc.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Debugging support.
 */
#include "opt_aac.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/conf.h>

#include <sys/bus.h>

#include <machine/resource.h>
#include <machine/bus.h>

#include <dev/aac/aacreg.h>
#include <sys/aac_ioctl.h>
#include <dev/aac/aacvar.h>

#ifdef AAC_DEBUG
int	aac_debug_enable = 0;
void	aac_printstate0(void);

/*
 * Dump the command queue indices
 */
void
aac_print_queues(struct aac_softc *sc)
{
	device_printf(sc->aac_dev, "FIB queue header at %p  queues at %p\n",
	    &sc->aac_queues->qt_qindex[AAC_HOST_NORM_CMD_QUEUE][0],
	    &sc->aac_queues->qt_HostNormCmdQueue[0]);
	device_printf(sc->aac_dev, "HOST_NORM_CMD  %d/%d (%d)\n",
	    sc->aac_queues->qt_qindex[AAC_HOST_NORM_CMD_QUEUE][
				      AAC_PRODUCER_INDEX],
	    sc->aac_queues->qt_qindex[AAC_HOST_NORM_CMD_QUEUE][
				      AAC_CONSUMER_INDEX],
	    AAC_HOST_NORM_CMD_ENTRIES);
	device_printf(sc->aac_dev, "HOST_HIGH_CMD  %d/%d (%d)\n",
	    sc->aac_queues->qt_qindex[AAC_HOST_HIGH_CMD_QUEUE][
				      AAC_PRODUCER_INDEX],
	    sc->aac_queues->qt_qindex[AAC_HOST_HIGH_CMD_QUEUE][
				      AAC_CONSUMER_INDEX],
	    AAC_HOST_HIGH_CMD_ENTRIES);
	device_printf(sc->aac_dev, "ADAP_NORM_CMD  %d/%d (%d)\n",
	    sc->aac_queues->qt_qindex[AAC_ADAP_NORM_CMD_QUEUE][
				      AAC_PRODUCER_INDEX],
	    sc->aac_queues->qt_qindex[AAC_ADAP_NORM_CMD_QUEUE][
				      AAC_CONSUMER_INDEX],
	    AAC_ADAP_NORM_CMD_ENTRIES);
	device_printf(sc->aac_dev, "ADAP_HIGH_CMD  %d/%d (%d)\n",
	    sc->aac_queues->qt_qindex[AAC_ADAP_HIGH_CMD_QUEUE][
				      AAC_PRODUCER_INDEX],
	    sc->aac_queues->qt_qindex[AAC_ADAP_HIGH_CMD_QUEUE][
				      AAC_CONSUMER_INDEX],
	    AAC_ADAP_HIGH_CMD_ENTRIES);
	device_printf(sc->aac_dev, "HOST_NORM_RESP %d/%d (%d)\n",
	    sc->aac_queues->qt_qindex[AAC_HOST_NORM_RESP_QUEUE][
				      AAC_PRODUCER_INDEX],
	    sc->aac_queues->qt_qindex[AAC_HOST_NORM_RESP_QUEUE][
				      AAC_CONSUMER_INDEX],
	    AAC_HOST_NORM_RESP_ENTRIES);
	device_printf(sc->aac_dev, "HOST_HIGH_RESP %d/%d (%d)\n",
	    sc->aac_queues->qt_qindex[AAC_HOST_HIGH_RESP_QUEUE][
				      AAC_PRODUCER_INDEX],
	    sc->aac_queues->qt_qindex[AAC_HOST_HIGH_RESP_QUEUE][
				      AAC_CONSUMER_INDEX],
	    AAC_HOST_HIGH_RESP_ENTRIES);
	device_printf(sc->aac_dev, "ADAP_NORM_RESP %d/%d (%d)\n",
	    sc->aac_queues->qt_qindex[AAC_ADAP_NORM_RESP_QUEUE][
				      AAC_PRODUCER_INDEX],
	    sc->aac_queues->qt_qindex[AAC_ADAP_NORM_RESP_QUEUE][
				      AAC_CONSUMER_INDEX],
	    AAC_ADAP_NORM_RESP_ENTRIES);
	device_printf(sc->aac_dev, "ADAP_HIGH_RESP %d/%d (%d)\n",
	    sc->aac_queues->qt_qindex[AAC_ADAP_HIGH_RESP_QUEUE][
				      AAC_PRODUCER_INDEX],
	    sc->aac_queues->qt_qindex[AAC_ADAP_HIGH_RESP_QUEUE][
				      AAC_CONSUMER_INDEX],
	    AAC_ADAP_HIGH_RESP_ENTRIES);
	device_printf(sc->aac_dev, "AACQ_FREE      %d/%d\n",
	    sc->aac_qstat[AACQ_FREE].q_length, sc->aac_qstat[AACQ_FREE].q_max);
	device_printf(sc->aac_dev, "AACQ_BIO       %d/%d\n",
	    sc->aac_qstat[AACQ_BIO].q_length, sc->aac_qstat[AACQ_BIO].q_max);
	device_printf(sc->aac_dev, "AACQ_READY     %d/%d\n",
	    sc->aac_qstat[AACQ_READY].q_length,
	    sc->aac_qstat[AACQ_READY].q_max);
	device_printf(sc->aac_dev, "AACQ_BUSY      %d/%d\n",
	    sc->aac_qstat[AACQ_BUSY].q_length, sc->aac_qstat[AACQ_BUSY].q_max);
}

/*
 * Print the command queue states for controller 0 (callable from DDB)
 */
void
aac_printstate0(void)
{
	struct aac_softc *sc;

	sc = devclass_get_softc(devclass_find("aac"), 0);

	aac_print_queues(sc);
	switch (sc->aac_hwif) {
	case AAC_HWIF_I960RX:
	case AAC_HWIF_NARK:
		device_printf(sc->aac_dev, "IDBR 0x%08x  IIMR 0x%08x  "
		    "IISR 0x%08x\n", AAC_MEM0_GETREG4(sc, AAC_RX_IDBR),
		    AAC_MEM0_GETREG4(sc, AAC_RX_IIMR), AAC_MEM0_GETREG4(sc, AAC_RX_IISR));
		device_printf(sc->aac_dev, "ODBR 0x%08x  OIMR 0x%08x  "
		    "OISR 0x%08x\n", AAC_MEM0_GETREG4(sc, AAC_RX_ODBR),
		    AAC_MEM0_GETREG4(sc, AAC_RX_OIMR), AAC_MEM0_GETREG4(sc, AAC_RX_OISR));
		AAC_MEM0_SETREG4(sc, AAC_RX_OIMR, 0/*~(AAC_DB_COMMAND_READY |
			    AAC_DB_RESPONSE_READY | AAC_DB_PRINTF)*/);
		device_printf(sc->aac_dev, "ODBR 0x%08x  OIMR 0x%08x  "
		    "OISR 0x%08x\n", AAC_MEM0_GETREG4(sc, AAC_RX_ODBR),
		    AAC_MEM0_GETREG4(sc, AAC_RX_OIMR), AAC_MEM0_GETREG4(sc, AAC_RX_OISR));
		break;
	case AAC_HWIF_STRONGARM:
		/* XXX implement */
		break;
	}
}

/*
 * Panic in a slightly informative fashion
 */
void
aac_panic(struct aac_softc *sc, char *reason)
{
	aac_print_queues(sc);
	panic("%s", reason);
}

/*
 * Print a FIB
 */
void
aac_print_fib(struct aac_softc *sc, struct aac_fib *fib, const char *caller)
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
	device_printf(sc->aac_dev, "  Flags         0x%x\n", fib->Header.Flags);
	device_printf(sc->aac_dev, "  Size          %d\n", fib->Header.Size);
	device_printf(sc->aac_dev, "  SenderSize    %d\n",
		      fib->Header.SenderSize);
	device_printf(sc->aac_dev, "  SenderAddress 0x%x\n",
		      fib->Header.SenderFibAddress);
	device_printf(sc->aac_dev, "  RcvrAddress   0x%x\n",
		      fib->Header.ReceiverFibAddress);
	device_printf(sc->aac_dev, "  SenderData    0x%x\n",
		      fib->Header.SenderData);
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
aac_print_aif(struct aac_softc *sc, struct aac_aif_command *aif)
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
#endif /* AAC_DEBUG */
