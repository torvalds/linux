/*-
 * Copyright (c) 2009 Yahoo! Inc.
 * Copyright (c) 2011-2015 LSI Corp.
 * Copyright (c) 2013-2016 Avago Technologies
 * Copyright 2000-2020 Broadcom Inc.
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
 * Broadcom Inc. (LSI) MPT-Fusion Host Adapter FreeBSD
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/* Communications core for Avago Technologies (LSI) MPT3 */

/* TODO Move headers to mprvar */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/selinfo.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/bio.h>
#include <sys/malloc.h>
#include <sys/uio.h>
#include <sys/sysctl.h>
#include <sys/smp.h>
#include <sys/queue.h>
#include <sys/kthread.h>
#include <sys/taskqueue.h>
#include <sys/endian.h>
#include <sys/eventhandler.h>
#include <sys/sbuf.h>
#include <sys/priv.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>
#include <sys/proc.h>

#include <dev/pci/pcivar.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/scsi/scsi_all.h>

#include <dev/mpr/mpi/mpi2_type.h>
#include <dev/mpr/mpi/mpi2.h>
#include <dev/mpr/mpi/mpi2_ioc.h>
#include <dev/mpr/mpi/mpi2_sas.h>
#include <dev/mpr/mpi/mpi2_pci.h>
#include <dev/mpr/mpi/mpi2_cnfg.h>
#include <dev/mpr/mpi/mpi2_init.h>
#include <dev/mpr/mpi/mpi2_tool.h>
#include <dev/mpr/mpr_ioctl.h>
#include <dev/mpr/mprvar.h>
#include <dev/mpr/mpr_table.h>
#include <dev/mpr/mpr_sas.h>

static int mpr_diag_reset(struct mpr_softc *sc, int sleep_flag);
static int mpr_init_queues(struct mpr_softc *sc);
static void mpr_resize_queues(struct mpr_softc *sc);
static int mpr_message_unit_reset(struct mpr_softc *sc, int sleep_flag);
static int mpr_transition_operational(struct mpr_softc *sc);
static int mpr_iocfacts_allocate(struct mpr_softc *sc, uint8_t attaching);
static void mpr_iocfacts_free(struct mpr_softc *sc);
static void mpr_startup(void *arg);
static int mpr_send_iocinit(struct mpr_softc *sc);
static int mpr_alloc_queues(struct mpr_softc *sc);
static int mpr_alloc_hw_queues(struct mpr_softc *sc);
static int mpr_alloc_replies(struct mpr_softc *sc);
static int mpr_alloc_requests(struct mpr_softc *sc);
static int mpr_alloc_nvme_prp_pages(struct mpr_softc *sc);
static int mpr_attach_log(struct mpr_softc *sc);
static __inline void mpr_complete_command(struct mpr_softc *sc,
    struct mpr_command *cm);
static void mpr_dispatch_event(struct mpr_softc *sc, uintptr_t data,
    MPI2_EVENT_NOTIFICATION_REPLY *reply);
static void mpr_config_complete(struct mpr_softc *sc, struct mpr_command *cm);
static void mpr_periodic(void *);
static int mpr_reregister_events(struct mpr_softc *sc);
static void mpr_enqueue_request(struct mpr_softc *sc, struct mpr_command *cm);
static int mpr_get_iocfacts(struct mpr_softc *sc, MPI2_IOC_FACTS_REPLY *facts);
static int mpr_wait_db_ack(struct mpr_softc *sc, int timeout, int sleep_flag);
static int mpr_debug_sysctl(SYSCTL_HANDLER_ARGS);
static int mpr_dump_reqs(SYSCTL_HANDLER_ARGS);
static void mpr_parse_debug(struct mpr_softc *sc, char *list);

SYSCTL_NODE(_hw, OID_AUTO, mpr, CTLFLAG_RD, 0, "MPR Driver Parameters");

MALLOC_DEFINE(M_MPR, "mpr", "mpr driver memory");

/*
 * Do a "Diagnostic Reset" aka a hard reset.  This should get the chip out of
 * any state and back to its initialization state machine.
 */
static char mpt2_reset_magic[] = { 0x00, 0x0f, 0x04, 0x0b, 0x02, 0x07, 0x0d };

/* 
 * Added this union to smoothly convert le64toh cm->cm_desc.Words.
 * Compiler only supports uint64_t to be passed as an argument.
 * Otherwise it will throw this error:
 * "aggregate value used where an integer was expected"
 */
typedef union _reply_descriptor {
        u64 word;
        struct {
                u32 low;
                u32 high;
        } u;
} reply_descriptor, request_descriptor;

/* Rate limit chain-fail messages to 1 per minute */
static struct timeval mpr_chainfail_interval = { 60, 0 };

/* 
 * sleep_flag can be either CAN_SLEEP or NO_SLEEP.
 * If this function is called from process context, it can sleep
 * and there is no harm to sleep, in case if this fuction is called
 * from Interrupt handler, we can not sleep and need NO_SLEEP flag set.
 * based on sleep flags driver will call either msleep, pause or DELAY.
 * msleep and pause are of same variant, but pause is used when mpr_mtx
 * is not hold by driver.
 */
static int
mpr_diag_reset(struct mpr_softc *sc,int sleep_flag)
{
	uint32_t reg;
	int i, error, tries = 0;
	uint8_t first_wait_done = FALSE;

	mpr_dprint(sc, MPR_INIT, "%s entered\n", __func__);

	/* Clear any pending interrupts */
	mpr_regwrite(sc, MPI2_HOST_INTERRUPT_STATUS_OFFSET, 0x0);

	/*
	 * Force NO_SLEEP for threads prohibited to sleep
 	 * e.a Thread from interrupt handler are prohibited to sleep.
 	 */
#if __FreeBSD_version >= 1000029
	if (curthread->td_no_sleeping)
#else //__FreeBSD_version < 1000029
	if (curthread->td_pflags & TDP_NOSLEEPING)
#endif //__FreeBSD_version >= 1000029
		sleep_flag = NO_SLEEP;

	mpr_dprint(sc, MPR_INIT, "sequence start, sleep_flag=%d\n", sleep_flag);
	/* Push the magic sequence */
	error = ETIMEDOUT;
	while (tries++ < 20) {
		for (i = 0; i < sizeof(mpt2_reset_magic); i++)
			mpr_regwrite(sc, MPI2_WRITE_SEQUENCE_OFFSET,
			    mpt2_reset_magic[i]);

		/* wait 100 msec */
		if (mtx_owned(&sc->mpr_mtx) && sleep_flag == CAN_SLEEP)
			msleep(&sc->msleep_fake_chan, &sc->mpr_mtx, 0,
			    "mprdiag", hz/10);
		else if (sleep_flag == CAN_SLEEP)
			pause("mprdiag", hz/10);
		else
			DELAY(100 * 1000);

		reg = mpr_regread(sc, MPI2_HOST_DIAGNOSTIC_OFFSET);
		if (reg & MPI2_DIAG_DIAG_WRITE_ENABLE) {
			error = 0;
			break;
		}
	}
	if (error) {
		mpr_dprint(sc, MPR_INIT, "sequence failed, error=%d, exit\n",
		    error);
		return (error);
	}

	/* Send the actual reset.  XXX need to refresh the reg? */
	reg |= MPI2_DIAG_RESET_ADAPTER;
	mpr_dprint(sc, MPR_INIT, "sequence success, sending reset, reg= 0x%x\n",
	    reg);
	mpr_regwrite(sc, MPI2_HOST_DIAGNOSTIC_OFFSET, reg);

	/* Wait up to 300 seconds in 50ms intervals */
	error = ETIMEDOUT;
	for (i = 0; i < 6000; i++) {
		/*
		 * Wait 50 msec. If this is the first time through, wait 256
		 * msec to satisfy Diag Reset timing requirements.
		 */
		if (first_wait_done) {
			if (mtx_owned(&sc->mpr_mtx) && sleep_flag == CAN_SLEEP)
				msleep(&sc->msleep_fake_chan, &sc->mpr_mtx, 0,
				    "mprdiag", hz/20);
			else if (sleep_flag == CAN_SLEEP)
				pause("mprdiag", hz/20);
			else
				DELAY(50 * 1000);
		} else {
			DELAY(256 * 1000);
			first_wait_done = TRUE;
		}
		/*
		 * Check for the RESET_ADAPTER bit to be cleared first, then
		 * wait for the RESET state to be cleared, which takes a little
		 * longer.
		 */
		reg = mpr_regread(sc, MPI2_HOST_DIAGNOSTIC_OFFSET);
		if (reg & MPI2_DIAG_RESET_ADAPTER) {
			continue;
		}
		reg = mpr_regread(sc, MPI2_DOORBELL_OFFSET);
		if ((reg & MPI2_IOC_STATE_MASK) != MPI2_IOC_STATE_RESET) {
			error = 0;
			break;
		}
	}
	if (error) {
		mpr_dprint(sc, MPR_INIT, "reset failed, error= %d, exit\n",
		    error);
		return (error);
	}

	mpr_regwrite(sc, MPI2_WRITE_SEQUENCE_OFFSET, 0x0);
	mpr_dprint(sc, MPR_INIT, "diag reset success, exit\n");

	return (0);
}

static int
mpr_message_unit_reset(struct mpr_softc *sc, int sleep_flag)
{
	int error;

	MPR_FUNCTRACE(sc);

	mpr_dprint(sc, MPR_INIT, "%s entered\n", __func__);

	error = 0;
	mpr_regwrite(sc, MPI2_DOORBELL_OFFSET,
	    MPI2_FUNCTION_IOC_MESSAGE_UNIT_RESET <<
	    MPI2_DOORBELL_FUNCTION_SHIFT);

	if (mpr_wait_db_ack(sc, 5, sleep_flag) != 0) {
		mpr_dprint(sc, MPR_INIT|MPR_FAULT,
		    "Doorbell handshake failed\n");
		error = ETIMEDOUT;
	}

	mpr_dprint(sc, MPR_INIT, "%s exit\n", __func__);
	return (error);
}

static int
mpr_transition_ready(struct mpr_softc *sc)
{
	uint32_t reg, state;
	int error, tries = 0;
	int sleep_flags;

	MPR_FUNCTRACE(sc);
	/* If we are in attach call, do not sleep */
	sleep_flags = (sc->mpr_flags & MPR_FLAGS_ATTACH_DONE)
	    ? CAN_SLEEP : NO_SLEEP;

	error = 0;

	mpr_dprint(sc, MPR_INIT, "%s entered, sleep_flags= %d\n",
	    __func__, sleep_flags);

	while (tries++ < 1200) {
		reg = mpr_regread(sc, MPI2_DOORBELL_OFFSET);
		mpr_dprint(sc, MPR_INIT, "  Doorbell= 0x%x\n", reg);

		/*
		 * Ensure the IOC is ready to talk.  If it's not, try
		 * resetting it.
		 */
		if (reg & MPI2_DOORBELL_USED) {
			mpr_dprint(sc, MPR_INIT, "  Not ready, sending diag "
			    "reset\n");
			mpr_diag_reset(sc, sleep_flags);
			DELAY(50000);
			continue;
		}

		/* Is the adapter owned by another peer? */
		if ((reg & MPI2_DOORBELL_WHO_INIT_MASK) ==
		    (MPI2_WHOINIT_PCI_PEER << MPI2_DOORBELL_WHO_INIT_SHIFT)) {
			mpr_dprint(sc, MPR_INIT|MPR_FAULT, "IOC is under the "
			    "control of another peer host, aborting "
			    "initialization.\n");
			error = ENXIO;
			break;
		}
		
		state = reg & MPI2_IOC_STATE_MASK;
		if (state == MPI2_IOC_STATE_READY) {
			/* Ready to go! */
			error = 0;
			break;
		} else if (state == MPI2_IOC_STATE_FAULT) {
			mpr_dprint(sc, MPR_INIT|MPR_FAULT, "IOC in fault "
			    "state 0x%x, resetting\n",
			    state & MPI2_DOORBELL_FAULT_CODE_MASK);
			mpr_diag_reset(sc, sleep_flags);
		} else if (state == MPI2_IOC_STATE_OPERATIONAL) {
			/* Need to take ownership */
			mpr_message_unit_reset(sc, sleep_flags);
		} else if (state == MPI2_IOC_STATE_RESET) {
			/* Wait a bit, IOC might be in transition */
			mpr_dprint(sc, MPR_INIT|MPR_FAULT,
			    "IOC in unexpected reset state\n");
		} else {
			mpr_dprint(sc, MPR_INIT|MPR_FAULT,
			    "IOC in unknown state 0x%x\n", state);
			error = EINVAL;
			break;
		}
	
		/* Wait 50ms for things to settle down. */
		DELAY(50000);
	}

	if (error)
		mpr_dprint(sc, MPR_INIT|MPR_FAULT,
		    "Cannot transition IOC to ready\n");
	mpr_dprint(sc, MPR_INIT, "%s exit\n", __func__);
	return (error);
}

static int
mpr_transition_operational(struct mpr_softc *sc)
{
	uint32_t reg, state;
	int error;

	MPR_FUNCTRACE(sc);

	error = 0;
	reg = mpr_regread(sc, MPI2_DOORBELL_OFFSET);
	mpr_dprint(sc, MPR_INIT, "%s entered, Doorbell= 0x%x\n", __func__, reg);

	state = reg & MPI2_IOC_STATE_MASK;
	if (state != MPI2_IOC_STATE_READY) {
		mpr_dprint(sc, MPR_INIT, "IOC not ready\n");
		if ((error = mpr_transition_ready(sc)) != 0) {
			mpr_dprint(sc, MPR_INIT|MPR_FAULT, 
			    "failed to transition ready, exit\n");
			return (error);
		}
	}

	error = mpr_send_iocinit(sc);
	mpr_dprint(sc, MPR_INIT, "%s exit\n", __func__);

	return (error);
}

static void
mpr_resize_queues(struct mpr_softc *sc)
{
	u_int reqcr, prireqcr, maxio, sges_per_frame, chain_seg_size;

	/*
	 * Size the queues. Since the reply queues always need one free
	 * entry, we'll deduct one reply message here.  The LSI documents
	 * suggest instead to add a count to the request queue, but I think
	 * that it's better to deduct from reply queue.
	 */
	prireqcr = MAX(1, sc->max_prireqframes);
	prireqcr = MIN(prireqcr, sc->facts->HighPriorityCredit);

	reqcr = MAX(2, sc->max_reqframes);
	reqcr = MIN(reqcr, sc->facts->RequestCredit);

	sc->num_reqs = prireqcr + reqcr;
	sc->num_prireqs = prireqcr;
	sc->num_replies = MIN(sc->max_replyframes + sc->max_evtframes,
	    sc->facts->MaxReplyDescriptorPostQueueDepth) - 1;

	/* Store the request frame size in bytes rather than as 32bit words */
	sc->reqframesz = sc->facts->IOCRequestFrameSize * 4;

	/*
	 * Gen3 and beyond uses the IOCMaxChainSegmentSize from IOC Facts to
	 * get the size of a Chain Frame.  Previous versions use the size as a
	 * Request Frame for the Chain Frame size.  If IOCMaxChainSegmentSize
	 * is 0, use the default value.  The IOCMaxChainSegmentSize is the
	 * number of 16-byte elelements that can fit in a Chain Frame, which is
	 * the size of an IEEE Simple SGE.
	 */
	if (sc->facts->MsgVersion >= MPI2_VERSION_02_05) {
		chain_seg_size = htole16(sc->facts->IOCMaxChainSegmentSize);
		if (chain_seg_size == 0)
			chain_seg_size = MPR_DEFAULT_CHAIN_SEG_SIZE;
		sc->chain_frame_size = chain_seg_size *
		    MPR_MAX_CHAIN_ELEMENT_SIZE;
	} else {
		sc->chain_frame_size = sc->reqframesz;
	}

	/*
	 * Max IO Size is Page Size * the following:
	 * ((SGEs per frame - 1 for chain element) * Max Chain Depth)
	 * + 1 for no chain needed in last frame
	 *
	 * If user suggests a Max IO size to use, use the smaller of the
	 * user's value and the calculated value as long as the user's
	 * value is larger than 0. The user's value is in pages.
	 */
	sges_per_frame = sc->chain_frame_size/sizeof(MPI2_IEEE_SGE_SIMPLE64)-1;
	maxio = (sges_per_frame * sc->facts->MaxChainDepth + 1) * PAGE_SIZE;

	/*
	 * If I/O size limitation requested then use it and pass up to CAM.
	 * If not, use MAXPHYS as an optimization hint, but report HW limit.
	 */
	if (sc->max_io_pages > 0) {
		maxio = min(maxio, sc->max_io_pages * PAGE_SIZE);
		sc->maxio = maxio;
	} else {
		sc->maxio = maxio;
		maxio = min(maxio, MAXPHYS);
	}

	sc->num_chains = (maxio / PAGE_SIZE + sges_per_frame - 2) /
	    sges_per_frame * reqcr;
	if (sc->max_chains > 0 && sc->max_chains < sc->num_chains)
		sc->num_chains = sc->max_chains;

	/*
	 * Figure out the number of MSIx-based queues.  If the firmware or
	 * user has done something crazy and not allowed enough credit for
	 * the queues to be useful then don't enable multi-queue.
	 */
	if (sc->facts->MaxMSIxVectors < 2)
		sc->msi_msgs = 1;

	if (sc->msi_msgs > 1) {
		sc->msi_msgs = MIN(sc->msi_msgs, mp_ncpus);
		sc->msi_msgs = MIN(sc->msi_msgs, sc->facts->MaxMSIxVectors);
		if (sc->num_reqs / sc->msi_msgs < 2)
			sc->msi_msgs = 1;
	}

	mpr_dprint(sc, MPR_INIT, "Sized queues to q=%d reqs=%d replies=%d\n",
	    sc->msi_msgs, sc->num_reqs, sc->num_replies);
}

/*
 * This is called during attach and when re-initializing due to a Diag Reset.
 * IOC Facts is used to allocate many of the structures needed by the driver.
 * If called from attach, de-allocation is not required because the driver has
 * not allocated any structures yet, but if called from a Diag Reset, previously
 * allocated structures based on IOC Facts will need to be freed and re-
 * allocated bases on the latest IOC Facts.
 */
static int
mpr_iocfacts_allocate(struct mpr_softc *sc, uint8_t attaching)
{
	int error;
	Mpi2IOCFactsReply_t saved_facts;
	uint8_t saved_mode, reallocating;

	mpr_dprint(sc, MPR_INIT|MPR_TRACE, "%s entered\n", __func__);

	/* Save old IOC Facts and then only reallocate if Facts have changed */
	if (!attaching) {
		bcopy(sc->facts, &saved_facts, sizeof(MPI2_IOC_FACTS_REPLY));
	}

	/*
	 * Get IOC Facts.  In all cases throughout this function, panic if doing
	 * a re-initialization and only return the error if attaching so the OS
	 * can handle it.
	 */
	if ((error = mpr_get_iocfacts(sc, sc->facts)) != 0) {
		if (attaching) {
			mpr_dprint(sc, MPR_INIT|MPR_FAULT, "Failed to get "
			    "IOC Facts with error %d, exit\n", error);
			return (error);
		} else {
			panic("%s failed to get IOC Facts with error %d\n",
			    __func__, error);
		}
	}

	MPR_DPRINT_PAGE(sc, MPR_XINFO, iocfacts, sc->facts);

	snprintf(sc->fw_version, sizeof(sc->fw_version), 
	    "%02d.%02d.%02d.%02d", 
	    sc->facts->FWVersion.Struct.Major,
	    sc->facts->FWVersion.Struct.Minor,
	    sc->facts->FWVersion.Struct.Unit,
	    sc->facts->FWVersion.Struct.Dev);

	mpr_dprint(sc, MPR_INFO, "Firmware: %s, Driver: %s\n", sc->fw_version,
	    MPR_DRIVER_VERSION);
	mpr_dprint(sc, MPR_INFO,
	    "IOCCapabilities: %b\n", sc->facts->IOCCapabilities,
	    "\20" "\3ScsiTaskFull" "\4DiagTrace" "\5SnapBuf" "\6ExtBuf"
	    "\7EEDP" "\10BiDirTarg" "\11Multicast" "\14TransRetry" "\15IR"
	    "\16EventReplay" "\17RaidAccel" "\20MSIXIndex" "\21HostDisc"
	    "\22FastPath" "\23RDPQArray" "\24AtomicReqDesc" "\25PCIeSRIOV");

	/*
	 * If the chip doesn't support event replay then a hard reset will be
	 * required to trigger a full discovery.  Do the reset here then
	 * retransition to Ready.  A hard reset might have already been done,
	 * but it doesn't hurt to do it again.  Only do this if attaching, not
	 * for a Diag Reset.
	 */
	if (attaching && ((sc->facts->IOCCapabilities &
	    MPI2_IOCFACTS_CAPABILITY_EVENT_REPLAY) == 0)) {
		mpr_dprint(sc, MPR_INIT, "No event replay, resetting\n");
		mpr_diag_reset(sc, NO_SLEEP);
		if ((error = mpr_transition_ready(sc)) != 0) {
			mpr_dprint(sc, MPR_INIT|MPR_FAULT, "Failed to "
			    "transition to ready with error %d, exit\n",
			    error);
			return (error);
		}
	}

	/*
	 * Set flag if IR Firmware is loaded.  If the RAID Capability has
	 * changed from the previous IOC Facts, log a warning, but only if
	 * checking this after a Diag Reset and not during attach.
	 */
	saved_mode = sc->ir_firmware;
	if (sc->facts->IOCCapabilities &
	    MPI2_IOCFACTS_CAPABILITY_INTEGRATED_RAID)
		sc->ir_firmware = 1;
	if (!attaching) {
		if (sc->ir_firmware != saved_mode) {
			mpr_dprint(sc, MPR_INIT|MPR_FAULT, "new IR/IT mode "
			    "in IOC Facts does not match previous mode\n");
		}
	}

	/* Only deallocate and reallocate if relevant IOC Facts have changed */
	reallocating = FALSE;
	sc->mpr_flags &= ~MPR_FLAGS_REALLOCATED;

	if ((!attaching) &&
	    ((saved_facts.MsgVersion != sc->facts->MsgVersion) ||
	    (saved_facts.HeaderVersion != sc->facts->HeaderVersion) ||
	    (saved_facts.MaxChainDepth != sc->facts->MaxChainDepth) ||
	    (saved_facts.RequestCredit != sc->facts->RequestCredit) ||
	    (saved_facts.ProductID != sc->facts->ProductID) ||
	    (saved_facts.IOCCapabilities != sc->facts->IOCCapabilities) ||
	    (saved_facts.IOCRequestFrameSize !=
	    sc->facts->IOCRequestFrameSize) ||
	    (saved_facts.IOCMaxChainSegmentSize !=
	    sc->facts->IOCMaxChainSegmentSize) ||
	    (saved_facts.MaxTargets != sc->facts->MaxTargets) ||
	    (saved_facts.MaxSasExpanders != sc->facts->MaxSasExpanders) ||
	    (saved_facts.MaxEnclosures != sc->facts->MaxEnclosures) ||
	    (saved_facts.HighPriorityCredit != sc->facts->HighPriorityCredit) ||
	    (saved_facts.MaxReplyDescriptorPostQueueDepth !=
	    sc->facts->MaxReplyDescriptorPostQueueDepth) ||
	    (saved_facts.ReplyFrameSize != sc->facts->ReplyFrameSize) ||
	    (saved_facts.MaxVolumes != sc->facts->MaxVolumes) ||
	    (saved_facts.MaxPersistentEntries !=
	    sc->facts->MaxPersistentEntries))) {
		reallocating = TRUE;

		/* Record that we reallocated everything */
		sc->mpr_flags |= MPR_FLAGS_REALLOCATED;
	}

	/*
	 * Some things should be done if attaching or re-allocating after a Diag
	 * Reset, but are not needed after a Diag Reset if the FW has not
	 * changed.
	 */
	if (attaching || reallocating) {
		/*
		 * Check if controller supports FW diag buffers and set flag to
		 * enable each type.
		 */
		if (sc->facts->IOCCapabilities &
		    MPI2_IOCFACTS_CAPABILITY_DIAG_TRACE_BUFFER)
			sc->fw_diag_buffer_list[MPI2_DIAG_BUF_TYPE_TRACE].
			    enabled = TRUE;
		if (sc->facts->IOCCapabilities &
		    MPI2_IOCFACTS_CAPABILITY_SNAPSHOT_BUFFER)
			sc->fw_diag_buffer_list[MPI2_DIAG_BUF_TYPE_SNAPSHOT].
			    enabled = TRUE;
		if (sc->facts->IOCCapabilities &
		    MPI2_IOCFACTS_CAPABILITY_EXTENDED_BUFFER)
			sc->fw_diag_buffer_list[MPI2_DIAG_BUF_TYPE_EXTENDED].
			    enabled = TRUE;

		/*
		 * Set flags for some supported items.
		 */
		if (sc->facts->IOCCapabilities & MPI2_IOCFACTS_CAPABILITY_EEDP)
			sc->eedp_enabled = TRUE;
		if (sc->facts->IOCCapabilities & MPI2_IOCFACTS_CAPABILITY_TLR)
			sc->control_TLR = TRUE;
		if ((sc->facts->IOCCapabilities &
		    MPI26_IOCFACTS_CAPABILITY_ATOMIC_REQ) &&
		    (sc->mpr_flags & MPR_FLAGS_SEA_IOC))
			sc->atomic_desc_capable = TRUE;

		mpr_resize_queues(sc);

		/*
		 * Initialize all Tail Queues
		 */
		TAILQ_INIT(&sc->req_list);
		TAILQ_INIT(&sc->high_priority_req_list);
		TAILQ_INIT(&sc->chain_list);
		TAILQ_INIT(&sc->prp_page_list);
		TAILQ_INIT(&sc->tm_list);
	}

	/*
	 * If doing a Diag Reset and the FW is significantly different
	 * (reallocating will be set above in IOC Facts comparison), then all
	 * buffers based on the IOC Facts will need to be freed before they are
	 * reallocated.
	 */
	if (reallocating) {
		mpr_iocfacts_free(sc);
		mprsas_realloc_targets(sc, saved_facts.MaxTargets +
		    saved_facts.MaxVolumes);
	}

	/*
	 * Any deallocation has been completed.  Now start reallocating
	 * if needed.  Will only need to reallocate if attaching or if the new
	 * IOC Facts are different from the previous IOC Facts after a Diag
	 * Reset. Targets have already been allocated above if needed.
	 */
	error = 0;
	while (attaching || reallocating) {
		if ((error = mpr_alloc_hw_queues(sc)) != 0)
			break;
		if ((error = mpr_alloc_replies(sc)) != 0)
			break;
		if ((error = mpr_alloc_requests(sc)) != 0)
			break;
		if ((error = mpr_alloc_queues(sc)) != 0)
			break;
		break;
	}
	if (error) {
		mpr_dprint(sc, MPR_INIT|MPR_ERROR,
		    "Failed to alloc queues with error %d\n", error);
		mpr_free(sc);
		return (error);
	}

	/* Always initialize the queues */
	bzero(sc->free_queue, sc->fqdepth * 4);
	mpr_init_queues(sc);

	/*
	 * Always get the chip out of the reset state, but only panic if not
	 * attaching.  If attaching and there is an error, that is handled by
	 * the OS.
	 */
	error = mpr_transition_operational(sc);
	if (error != 0) {
		mpr_dprint(sc, MPR_INIT|MPR_FAULT, "Failed to "
		    "transition to operational with error %d\n", error);
		mpr_free(sc);
		return (error);
	}

	/*
	 * Finish the queue initialization.
	 * These are set here instead of in mpr_init_queues() because the
	 * IOC resets these values during the state transition in
	 * mpr_transition_operational().  The free index is set to 1
	 * because the corresponding index in the IOC is set to 0, and the
	 * IOC treats the queues as full if both are set to the same value.
	 * Hence the reason that the queue can't hold all of the possible
	 * replies.
	 */
	sc->replypostindex = 0;
	mpr_regwrite(sc, MPI2_REPLY_FREE_HOST_INDEX_OFFSET, sc->replyfreeindex);
	mpr_regwrite(sc, MPI2_REPLY_POST_HOST_INDEX_OFFSET, 0);

	/*
	 * Attach the subsystems so they can prepare their event masks.
	 * XXX Should be dynamic so that IM/IR and user modules can attach
	 */
	error = 0;
	while (attaching) {
		mpr_dprint(sc, MPR_INIT, "Attaching subsystems\n");
		if ((error = mpr_attach_log(sc)) != 0)
			break;
		if ((error = mpr_attach_sas(sc)) != 0)
			break;
		if ((error = mpr_attach_user(sc)) != 0)
			break;
		break;
	}
	if (error) {
		mpr_dprint(sc, MPR_INIT|MPR_ERROR,
		    "Failed to attach all subsystems: error %d\n", error);
		mpr_free(sc);
		return (error);
	}

	/*
	 * XXX If the number of MSI-X vectors changes during re-init, this
	 * won't see it and adjust.
	 */
	if (attaching && (error = mpr_pci_setup_interrupts(sc)) != 0) {
		mpr_dprint(sc, MPR_INIT|MPR_ERROR,
		    "Failed to setup interrupts\n");
		mpr_free(sc);
		return (error);
	}

	return (error);
}

/*
 * This is called if memory is being free (during detach for example) and when
 * buffers need to be reallocated due to a Diag Reset.
 */
static void
mpr_iocfacts_free(struct mpr_softc *sc)
{
	struct mpr_command *cm;
	int i;

	mpr_dprint(sc, MPR_TRACE, "%s\n", __func__);

	if (sc->free_busaddr != 0)
		bus_dmamap_unload(sc->queues_dmat, sc->queues_map);
	if (sc->free_queue != NULL)
		bus_dmamem_free(sc->queues_dmat, sc->free_queue,
		    sc->queues_map);
	if (sc->queues_dmat != NULL)
		bus_dma_tag_destroy(sc->queues_dmat);

	if (sc->chain_frames != NULL) {
		bus_dmamap_unload(sc->chain_dmat, sc->chain_map);
		bus_dmamem_free(sc->chain_dmat, sc->chain_frames,
		    sc->chain_map);
	}
	if (sc->chain_dmat != NULL)
		bus_dma_tag_destroy(sc->chain_dmat);

	if (sc->sense_busaddr != 0)
		bus_dmamap_unload(sc->sense_dmat, sc->sense_map);
	if (sc->sense_frames != NULL)
		bus_dmamem_free(sc->sense_dmat, sc->sense_frames,
		    sc->sense_map);
	if (sc->sense_dmat != NULL)
		bus_dma_tag_destroy(sc->sense_dmat);

	if (sc->prp_page_busaddr != 0)
		bus_dmamap_unload(sc->prp_page_dmat, sc->prp_page_map);
	if (sc->prp_pages != NULL)
		bus_dmamem_free(sc->prp_page_dmat, sc->prp_pages,
		    sc->prp_page_map);
	if (sc->prp_page_dmat != NULL)
		bus_dma_tag_destroy(sc->prp_page_dmat);

	if (sc->reply_busaddr != 0)
		bus_dmamap_unload(sc->reply_dmat, sc->reply_map);
	if (sc->reply_frames != NULL)
		bus_dmamem_free(sc->reply_dmat, sc->reply_frames,
		    sc->reply_map);
	if (sc->reply_dmat != NULL)
		bus_dma_tag_destroy(sc->reply_dmat);

	if (sc->req_busaddr != 0)
		bus_dmamap_unload(sc->req_dmat, sc->req_map);
	if (sc->req_frames != NULL)
		bus_dmamem_free(sc->req_dmat, sc->req_frames, sc->req_map);
	if (sc->req_dmat != NULL)
		bus_dma_tag_destroy(sc->req_dmat);

	if (sc->chains != NULL)
		free(sc->chains, M_MPR);
	if (sc->prps != NULL)
		free(sc->prps, M_MPR);
	if (sc->commands != NULL) {
		for (i = 1; i < sc->num_reqs; i++) {
			cm = &sc->commands[i];
			bus_dmamap_destroy(sc->buffer_dmat, cm->cm_dmamap);
		}
		free(sc->commands, M_MPR);
	}
	if (sc->buffer_dmat != NULL)
		bus_dma_tag_destroy(sc->buffer_dmat);

	mpr_pci_free_interrupts(sc);
	free(sc->queues, M_MPR);
	sc->queues = NULL;
}

/* 
 * The terms diag reset and hard reset are used interchangeably in the MPI
 * docs to mean resetting the controller chip.  In this code diag reset
 * cleans everything up, and the hard reset function just sends the reset
 * sequence to the chip.  This should probably be refactored so that every
 * subsystem gets a reset notification of some sort, and can clean up
 * appropriately.
 */
int
mpr_reinit(struct mpr_softc *sc)
{
	int error;
	struct mprsas_softc *sassc;

	sassc = sc->sassc;

	MPR_FUNCTRACE(sc);

	mtx_assert(&sc->mpr_mtx, MA_OWNED);

	mpr_dprint(sc, MPR_INIT|MPR_INFO, "Reinitializing controller\n");
	if (sc->mpr_flags & MPR_FLAGS_DIAGRESET) {
		mpr_dprint(sc, MPR_INIT, "Reset already in progress\n");
		return 0;
	}

	/*
	 * Make sure the completion callbacks can recognize they're getting
	 * a NULL cm_reply due to a reset.
	 */
	sc->mpr_flags |= MPR_FLAGS_DIAGRESET;

	/*
	 * Mask interrupts here.
	 */
	mpr_dprint(sc, MPR_INIT, "Masking interrupts and resetting\n");
	mpr_mask_intr(sc);

	error = mpr_diag_reset(sc, CAN_SLEEP);
	if (error != 0) {
		panic("%s hard reset failed with error %d\n", __func__, error);
	}

	/* Restore the PCI state, including the MSI-X registers */
	mpr_pci_restore(sc);

	/* Give the I/O subsystem special priority to get itself prepared */
	mprsas_handle_reinit(sc);

	/*
	 * Get IOC Facts and allocate all structures based on this information.
	 * The attach function will also call mpr_iocfacts_allocate at startup.
	 * If relevant values have changed in IOC Facts, this function will free
	 * all of the memory based on IOC Facts and reallocate that memory.
	 */
	if ((error = mpr_iocfacts_allocate(sc, FALSE)) != 0) {
		panic("%s IOC Facts based allocation failed with error %d\n",
		    __func__, error);
	}

	/*
	 * Mapping structures will be re-allocated after getting IOC Page8, so
	 * free these structures here.
	 */
	mpr_mapping_exit(sc);

	/*
	 * The static page function currently read is IOC Page8.  Others can be
	 * added in future.  It's possible that the values in IOC Page8 have
	 * changed after a Diag Reset due to user modification, so always read
	 * these.  Interrupts are masked, so unmask them before getting config
	 * pages.
	 */
	mpr_unmask_intr(sc);
	sc->mpr_flags &= ~MPR_FLAGS_DIAGRESET;
	mpr_base_static_config_pages(sc);

	/*
	 * Some mapping info is based in IOC Page8 data, so re-initialize the
	 * mapping tables.
	 */
	mpr_mapping_initialize(sc);

	/*
	 * Restart will reload the event masks clobbered by the reset, and
	 * then enable the port.
	 */
	mpr_reregister_events(sc);

	/* the end of discovery will release the simq, so we're done. */
	mpr_dprint(sc, MPR_INIT|MPR_XINFO, "Finished sc %p post %u free %u\n", 
	    sc, sc->replypostindex, sc->replyfreeindex);
	mprsas_release_simq_reinit(sassc);
	mpr_dprint(sc, MPR_INIT, "%s exit error= %d\n", __func__, error);

	return 0;
}

/* Wait for the chip to ACK a word that we've put into its FIFO 
 * Wait for <timeout> seconds. In single loop wait for busy loop
 * for 500 microseconds.
 * Total is [ 0.5 * (2000 * <timeout>) ] in miliseconds.
 * */
static int
mpr_wait_db_ack(struct mpr_softc *sc, int timeout, int sleep_flag)
{
	u32 cntdn, count;
	u32 int_status;
	u32 doorbell;

	count = 0;
	cntdn = (sleep_flag == CAN_SLEEP) ? 1000*timeout : 2000*timeout;
	do {
		int_status = mpr_regread(sc, MPI2_HOST_INTERRUPT_STATUS_OFFSET);
		if (!(int_status & MPI2_HIS_SYS2IOC_DB_STATUS)) {
			mpr_dprint(sc, MPR_TRACE, "%s: successful count(%d), "
			    "timeout(%d)\n", __func__, count, timeout);
			return 0;
		} else if (int_status & MPI2_HIS_IOC2SYS_DB_STATUS) {
			doorbell = mpr_regread(sc, MPI2_DOORBELL_OFFSET);
			if ((doorbell & MPI2_IOC_STATE_MASK) ==
			    MPI2_IOC_STATE_FAULT) {
				mpr_dprint(sc, MPR_FAULT,
				    "fault_state(0x%04x)!\n", doorbell);
				return (EFAULT);
			}
		} else if (int_status == 0xFFFFFFFF)
			goto out;
			
		/*
		 * If it can sleep, sleep for 1 milisecond, else busy loop for
 		 * 0.5 milisecond
		 */
		if (mtx_owned(&sc->mpr_mtx) && sleep_flag == CAN_SLEEP)
			msleep(&sc->msleep_fake_chan, &sc->mpr_mtx, 0, "mprdba",
			    hz/1000);
		else if (sleep_flag == CAN_SLEEP)
			pause("mprdba", hz/1000);
		else
			DELAY(500);
		count++;
	} while (--cntdn);

out:
	mpr_dprint(sc, MPR_FAULT, "%s: failed due to timeout count(%d), "
		"int_status(%x)!\n", __func__, count, int_status);
	return (ETIMEDOUT);
}

/* Wait for the chip to signal that the next word in its FIFO can be fetched */
static int
mpr_wait_db_int(struct mpr_softc *sc)
{
	int retry;

	for (retry = 0; retry < MPR_DB_MAX_WAIT; retry++) {
		if ((mpr_regread(sc, MPI2_HOST_INTERRUPT_STATUS_OFFSET) &
		    MPI2_HIS_IOC2SYS_DB_STATUS) != 0)
			return (0);
		DELAY(2000);
	}
	return (ETIMEDOUT);
}

/* Step through the synchronous command state machine, i.e. "Doorbell mode" */
static int
mpr_request_sync(struct mpr_softc *sc, void *req, MPI2_DEFAULT_REPLY *reply,
    int req_sz, int reply_sz, int timeout)
{
	uint32_t *data32;
	uint16_t *data16;
	int i, count, ioc_sz, residual;
	int sleep_flags = CAN_SLEEP;
	
#if __FreeBSD_version >= 1000029
	if (curthread->td_no_sleeping)
#else //__FreeBSD_version < 1000029
	if (curthread->td_pflags & TDP_NOSLEEPING)
#endif //__FreeBSD_version >= 1000029
		sleep_flags = NO_SLEEP;

	/* Step 1 */
	mpr_regwrite(sc, MPI2_HOST_INTERRUPT_STATUS_OFFSET, 0x0);

	/* Step 2 */
	if (mpr_regread(sc, MPI2_DOORBELL_OFFSET) & MPI2_DOORBELL_USED)
		return (EBUSY);

	/* Step 3
	 * Announce that a message is coming through the doorbell.  Messages
	 * are pushed at 32bit words, so round up if needed.
	 */
	count = (req_sz + 3) / 4;
	mpr_regwrite(sc, MPI2_DOORBELL_OFFSET,
	    (MPI2_FUNCTION_HANDSHAKE << MPI2_DOORBELL_FUNCTION_SHIFT) |
	    (count << MPI2_DOORBELL_ADD_DWORDS_SHIFT));

	/* Step 4 */
	if (mpr_wait_db_int(sc) ||
	    (mpr_regread(sc, MPI2_DOORBELL_OFFSET) & MPI2_DOORBELL_USED) == 0) {
		mpr_dprint(sc, MPR_FAULT, "Doorbell failed to activate\n");
		return (ENXIO);
	}
	mpr_regwrite(sc, MPI2_HOST_INTERRUPT_STATUS_OFFSET, 0x0);
	if (mpr_wait_db_ack(sc, 5, sleep_flags) != 0) {
		mpr_dprint(sc, MPR_FAULT, "Doorbell handshake failed\n");
		return (ENXIO);
	}

	/* Step 5 */
	/* Clock out the message data synchronously in 32-bit dwords*/
	data32 = (uint32_t *)req;
	for (i = 0; i < count; i++) {
		mpr_regwrite(sc, MPI2_DOORBELL_OFFSET, htole32(data32[i]));
		if (mpr_wait_db_ack(sc, 5, sleep_flags) != 0) {
			mpr_dprint(sc, MPR_FAULT,
			    "Timeout while writing doorbell\n");
			return (ENXIO);
		}
	}

	/* Step 6 */
	/* Clock in the reply in 16-bit words.  The total length of the
	 * message is always in the 4th byte, so clock out the first 2 words
	 * manually, then loop the rest.
	 */
	data16 = (uint16_t *)reply;
	if (mpr_wait_db_int(sc) != 0) {
		mpr_dprint(sc, MPR_FAULT, "Timeout reading doorbell 0\n");
		return (ENXIO);
	}
	data16[0] =
	    mpr_regread(sc, MPI2_DOORBELL_OFFSET) & MPI2_DOORBELL_DATA_MASK;
	mpr_regwrite(sc, MPI2_HOST_INTERRUPT_STATUS_OFFSET, 0x0);
	if (mpr_wait_db_int(sc) != 0) {
		mpr_dprint(sc, MPR_FAULT, "Timeout reading doorbell 1\n");
		return (ENXIO);
	}
	data16[1] =
	    mpr_regread(sc, MPI2_DOORBELL_OFFSET) & MPI2_DOORBELL_DATA_MASK;
	mpr_regwrite(sc, MPI2_HOST_INTERRUPT_STATUS_OFFSET, 0x0);

	/* Number of 32bit words in the message */
	ioc_sz = reply->MsgLength;

	/*
	 * Figure out how many 16bit words to clock in without overrunning.
	 * The precision loss with dividing reply_sz can safely be
	 * ignored because the messages can only be multiples of 32bits.
	 */
	residual = 0;
	count = MIN((reply_sz / 4), ioc_sz) * 2;
	if (count < ioc_sz * 2) {
		residual = ioc_sz * 2 - count;
		mpr_dprint(sc, MPR_ERROR, "Driver error, throwing away %d "
		    "residual message words\n", residual);
	}

	for (i = 2; i < count; i++) {
		if (mpr_wait_db_int(sc) != 0) {
			mpr_dprint(sc, MPR_FAULT,
			    "Timeout reading doorbell %d\n", i);
			return (ENXIO);
		}
		data16[i] = mpr_regread(sc, MPI2_DOORBELL_OFFSET) &
		    MPI2_DOORBELL_DATA_MASK;
		mpr_regwrite(sc, MPI2_HOST_INTERRUPT_STATUS_OFFSET, 0x0);
	}

	/*
	 * Pull out residual words that won't fit into the provided buffer.
	 * This keeps the chip from hanging due to a driver programming
	 * error.
	 */
	while (residual--) {
		if (mpr_wait_db_int(sc) != 0) {
			mpr_dprint(sc, MPR_FAULT, "Timeout reading doorbell\n");
			return (ENXIO);
		}
		(void)mpr_regread(sc, MPI2_DOORBELL_OFFSET);
		mpr_regwrite(sc, MPI2_HOST_INTERRUPT_STATUS_OFFSET, 0x0);
	}

	/* Step 7 */
	if (mpr_wait_db_int(sc) != 0) {
		mpr_dprint(sc, MPR_FAULT, "Timeout waiting to exit doorbell\n");
		return (ENXIO);
	}
	if (mpr_regread(sc, MPI2_DOORBELL_OFFSET) & MPI2_DOORBELL_USED)
		mpr_dprint(sc, MPR_FAULT, "Warning, doorbell still active\n");
	mpr_regwrite(sc, MPI2_HOST_INTERRUPT_STATUS_OFFSET, 0x0);

	return (0);
}

static void
mpr_enqueue_request(struct mpr_softc *sc, struct mpr_command *cm)
{
	request_descriptor rd;

	MPR_FUNCTRACE(sc);
	mpr_dprint(sc, MPR_TRACE, "SMID %u cm %p ccb %p\n",
	    cm->cm_desc.Default.SMID, cm, cm->cm_ccb);

	if (sc->mpr_flags & MPR_FLAGS_ATTACH_DONE && !(sc->mpr_flags &
	    MPR_FLAGS_SHUTDOWN))
		mtx_assert(&sc->mpr_mtx, MA_OWNED);

	if (++sc->io_cmds_active > sc->io_cmds_highwater)
		sc->io_cmds_highwater++;

	KASSERT(cm->cm_state == MPR_CM_STATE_BUSY, ("command not busy\n"));
	cm->cm_state = MPR_CM_STATE_INQUEUE;

	if (sc->atomic_desc_capable) {
		rd.u.low = cm->cm_desc.Words.Low;
		mpr_regwrite(sc, MPI26_ATOMIC_REQUEST_DESCRIPTOR_POST_OFFSET,
		    rd.u.low);
	} else {
		rd.u.low = cm->cm_desc.Words.Low;
		rd.u.high = cm->cm_desc.Words.High;
		rd.word = htole64(rd.word);
		mpr_regwrite(sc, MPI2_REQUEST_DESCRIPTOR_POST_LOW_OFFSET,
		    rd.u.low);
		mpr_regwrite(sc, MPI2_REQUEST_DESCRIPTOR_POST_HIGH_OFFSET,
		    rd.u.high);
	}
}

/*
 * Just the FACTS, ma'am.
 */
static int
mpr_get_iocfacts(struct mpr_softc *sc, MPI2_IOC_FACTS_REPLY *facts)
{
	MPI2_DEFAULT_REPLY *reply;
	MPI2_IOC_FACTS_REQUEST request;
	int error, req_sz, reply_sz;

	MPR_FUNCTRACE(sc);
	mpr_dprint(sc, MPR_INIT, "%s entered\n", __func__);

	req_sz = sizeof(MPI2_IOC_FACTS_REQUEST);
	reply_sz = sizeof(MPI2_IOC_FACTS_REPLY);
	reply = (MPI2_DEFAULT_REPLY *)facts;

	bzero(&request, req_sz);
	request.Function = MPI2_FUNCTION_IOC_FACTS;
	error = mpr_request_sync(sc, &request, reply, req_sz, reply_sz, 5);

	mpr_dprint(sc, MPR_INIT, "%s exit, error= %d\n", __func__, error);
	return (error);
}

static int
mpr_send_iocinit(struct mpr_softc *sc)
{
	MPI2_IOC_INIT_REQUEST	init;
	MPI2_DEFAULT_REPLY	reply;
	int req_sz, reply_sz, error;
	struct timeval now;
	uint64_t time_in_msec;

	MPR_FUNCTRACE(sc);
	mpr_dprint(sc, MPR_INIT, "%s entered\n", __func__);

	/* Do a quick sanity check on proper initialization */
	if ((sc->pqdepth == 0) || (sc->fqdepth == 0) || (sc->reqframesz == 0)
	    || (sc->replyframesz == 0)) {
		mpr_dprint(sc, MPR_INIT|MPR_ERROR,
		    "Driver not fully initialized for IOCInit\n");
		return (EINVAL);
	}

	req_sz = sizeof(MPI2_IOC_INIT_REQUEST);
	reply_sz = sizeof(MPI2_IOC_INIT_REPLY);
	bzero(&init, req_sz);
	bzero(&reply, reply_sz);

	/*
	 * Fill in the init block.  Note that most addresses are
	 * deliberately in the lower 32bits of memory.  This is a micro-
	 * optimzation for PCI/PCIX, though it's not clear if it helps PCIe.
	 */
	init.Function = MPI2_FUNCTION_IOC_INIT;
	init.WhoInit = MPI2_WHOINIT_HOST_DRIVER;
	init.MsgVersion = htole16(MPI2_VERSION);
	init.HeaderVersion = htole16(MPI2_HEADER_VERSION);
	init.SystemRequestFrameSize = htole16((uint16_t)(sc->reqframesz / 4));
	init.ReplyDescriptorPostQueueDepth = htole16(sc->pqdepth);
	init.ReplyFreeQueueDepth = htole16(sc->fqdepth);
	init.SenseBufferAddressHigh = 0;
	init.SystemReplyAddressHigh = 0;
	init.SystemRequestFrameBaseAddress.High = 0;
	init.SystemRequestFrameBaseAddress.Low =
	    htole32((uint32_t)sc->req_busaddr);
	init.ReplyDescriptorPostQueueAddress.High = 0;
	init.ReplyDescriptorPostQueueAddress.Low =
	    htole32((uint32_t)sc->post_busaddr);
	init.ReplyFreeQueueAddress.High = 0;
	init.ReplyFreeQueueAddress.Low = htole32((uint32_t)sc->free_busaddr);
	getmicrotime(&now);
	time_in_msec = (now.tv_sec * 1000 + now.tv_usec/1000);
	init.TimeStamp.High = htole32((time_in_msec >> 32) & 0xFFFFFFFF);
	init.TimeStamp.Low = htole32(time_in_msec & 0xFFFFFFFF);
	init.HostPageSize = HOST_PAGE_SIZE_4K;

	error = mpr_request_sync(sc, &init, &reply, req_sz, reply_sz, 5);
	if ((reply.IOCStatus & MPI2_IOCSTATUS_MASK) != MPI2_IOCSTATUS_SUCCESS)
		error = ENXIO;

	mpr_dprint(sc, MPR_INIT, "IOCInit status= 0x%x\n", reply.IOCStatus);
	mpr_dprint(sc, MPR_INIT, "%s exit\n", __func__);
	return (error);
}

void
mpr_memaddr_cb(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	bus_addr_t *addr;

	addr = arg;
	*addr = segs[0].ds_addr;
}

void
mpr_memaddr_wait_cb(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	struct mpr_busdma_context *ctx;
	int need_unload, need_free;

	ctx = (struct mpr_busdma_context *)arg;
	need_unload = 0;
	need_free = 0;

	mpr_lock(ctx->softc);
	ctx->error = error;
	ctx->completed = 1;
	if ((error == 0) && (ctx->abandoned == 0)) {
		*ctx->addr = segs[0].ds_addr;
	} else {
		if (nsegs != 0)
			need_unload = 1;
		if (ctx->abandoned != 0)
			need_free = 1;
	}
	if (need_free == 0)
		wakeup(ctx);

	mpr_unlock(ctx->softc);

	if (need_unload != 0) {
		bus_dmamap_unload(ctx->buffer_dmat,
				  ctx->buffer_dmamap);
		*ctx->addr = 0;
	}

	if (need_free != 0)
		free(ctx, M_MPR);
}

static int
mpr_alloc_queues(struct mpr_softc *sc)
{
	struct mpr_queue *q;
	int nq, i;

	nq = sc->msi_msgs;
	mpr_dprint(sc, MPR_INIT|MPR_XINFO, "Allocating %d I/O queues\n", nq);

	sc->queues = malloc(sizeof(struct mpr_queue) * nq, M_MPR,
	     M_NOWAIT|M_ZERO);
	if (sc->queues == NULL)
		return (ENOMEM);

	for (i = 0; i < nq; i++) {
		q = &sc->queues[i];
		mpr_dprint(sc, MPR_INIT, "Configuring queue %d %p\n", i, q);
		q->sc = sc;
		q->qnum = i;
	}
	return (0);
}

static int
mpr_alloc_hw_queues(struct mpr_softc *sc)
{
	bus_addr_t queues_busaddr;
	uint8_t *queues;
	int qsize, fqsize, pqsize;

	/*
	 * The reply free queue contains 4 byte entries in multiples of 16 and
	 * aligned on a 16 byte boundary. There must always be an unused entry.
	 * This queue supplies fresh reply frames for the firmware to use.
	 *
	 * The reply descriptor post queue contains 8 byte entries in
	 * multiples of 16 and aligned on a 16 byte boundary.  This queue
	 * contains filled-in reply frames sent from the firmware to the host.
	 *
	 * These two queues are allocated together for simplicity.
	 */
	sc->fqdepth = roundup2(sc->num_replies + 1, 16);
	sc->pqdepth = roundup2(sc->num_replies + 1, 16);
	fqsize= sc->fqdepth * 4;
	pqsize = sc->pqdepth * 8;
	qsize = fqsize + pqsize;

        if (bus_dma_tag_create( sc->mpr_parent_dmat,    /* parent */
				16, 0,			/* algnmnt, boundary */
				BUS_SPACE_MAXADDR_32BIT,/* lowaddr */
				BUS_SPACE_MAXADDR,	/* highaddr */
				NULL, NULL,		/* filter, filterarg */
                                qsize,			/* maxsize */
                                1,			/* nsegments */
                                qsize,			/* maxsegsize */
                                0,			/* flags */
                                NULL, NULL,		/* lockfunc, lockarg */
                                &sc->queues_dmat)) {
		mpr_dprint(sc, MPR_ERROR, "Cannot allocate queues DMA tag\n");
		return (ENOMEM);
        }
        if (bus_dmamem_alloc(sc->queues_dmat, (void **)&queues, BUS_DMA_NOWAIT,
	    &sc->queues_map)) {
		mpr_dprint(sc, MPR_ERROR, "Cannot allocate queues memory\n");
		return (ENOMEM);
        }
        bzero(queues, qsize);
        bus_dmamap_load(sc->queues_dmat, sc->queues_map, queues, qsize,
	    mpr_memaddr_cb, &queues_busaddr, 0);

	sc->free_queue = (uint32_t *)queues;
	sc->free_busaddr = queues_busaddr;
	sc->post_queue = (MPI2_REPLY_DESCRIPTORS_UNION *)(queues + fqsize);
	sc->post_busaddr = queues_busaddr + fqsize;
	mpr_dprint(sc, MPR_INIT, "free queue busaddr= %#016jx size= %d\n",
	    (uintmax_t)sc->free_busaddr, fqsize);
	mpr_dprint(sc, MPR_INIT, "reply queue busaddr= %#016jx size= %d\n",
	    (uintmax_t)sc->post_busaddr, pqsize);

	return (0);
}

static int
mpr_alloc_replies(struct mpr_softc *sc)
{
	int rsize, num_replies;

	/* Store the reply frame size in bytes rather than as 32bit words */
	sc->replyframesz = sc->facts->ReplyFrameSize * 4;

	/*
	 * sc->num_replies should be one less than sc->fqdepth.  We need to
	 * allocate space for sc->fqdepth replies, but only sc->num_replies
	 * replies can be used at once.
	 */
	num_replies = max(sc->fqdepth, sc->num_replies);

	rsize = sc->replyframesz * num_replies; 
        if (bus_dma_tag_create( sc->mpr_parent_dmat,    /* parent */
				4, 0,			/* algnmnt, boundary */
				BUS_SPACE_MAXADDR_32BIT,/* lowaddr */
				BUS_SPACE_MAXADDR,	/* highaddr */
				NULL, NULL,		/* filter, filterarg */
                                rsize,			/* maxsize */
                                1,			/* nsegments */
                                rsize,			/* maxsegsize */
                                0,			/* flags */
                                NULL, NULL,		/* lockfunc, lockarg */
                                &sc->reply_dmat)) {
		mpr_dprint(sc, MPR_ERROR, "Cannot allocate replies DMA tag\n");
		return (ENOMEM);
        }
        if (bus_dmamem_alloc(sc->reply_dmat, (void **)&sc->reply_frames,
	    BUS_DMA_NOWAIT, &sc->reply_map)) {
		mpr_dprint(sc, MPR_ERROR, "Cannot allocate replies memory\n");
		return (ENOMEM);
        }
        bzero(sc->reply_frames, rsize);
        bus_dmamap_load(sc->reply_dmat, sc->reply_map, sc->reply_frames, rsize,
	    mpr_memaddr_cb, &sc->reply_busaddr, 0);
	mpr_dprint(sc, MPR_INIT, "reply frames busaddr= %#016jx size= %d\n",
	    (uintmax_t)sc->reply_busaddr, rsize);

	return (0);
}

static void
mpr_load_chains_cb(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	struct mpr_softc *sc = arg;
	struct mpr_chain *chain;
	bus_size_t bo;
	int i, o, s;

	if (error != 0)
		return;

	for (i = 0, o = 0, s = 0; s < nsegs; s++) {
		for (bo = 0; bo + sc->chain_frame_size <= segs[s].ds_len;
		    bo += sc->chain_frame_size) {
			chain = &sc->chains[i++];
			chain->chain =(MPI2_SGE_IO_UNION *)(sc->chain_frames+o);
			chain->chain_busaddr = segs[s].ds_addr + bo;
			o += sc->chain_frame_size;
			mpr_free_chain(sc, chain);
		}
		if (bo != segs[s].ds_len)
			o += segs[s].ds_len - bo;
	}
	sc->chain_free_lowwater = i;
}

static int
mpr_alloc_requests(struct mpr_softc *sc)
{
	struct mpr_command *cm;
	int i, rsize, nsegs;

	rsize = sc->reqframesz * sc->num_reqs;
        if (bus_dma_tag_create( sc->mpr_parent_dmat,    /* parent */
				16, 0,			/* algnmnt, boundary */
				BUS_SPACE_MAXADDR_32BIT,/* lowaddr */
				BUS_SPACE_MAXADDR,	/* highaddr */
				NULL, NULL,		/* filter, filterarg */
                                rsize,			/* maxsize */
                                1,			/* nsegments */
                                rsize,			/* maxsegsize */
                                0,			/* flags */
                                NULL, NULL,		/* lockfunc, lockarg */
                                &sc->req_dmat)) {
		mpr_dprint(sc, MPR_ERROR, "Cannot allocate request DMA tag\n");
		return (ENOMEM);
        }
        if (bus_dmamem_alloc(sc->req_dmat, (void **)&sc->req_frames,
	    BUS_DMA_NOWAIT, &sc->req_map)) {
		mpr_dprint(sc, MPR_ERROR, "Cannot allocate request memory\n");
		return (ENOMEM);
        }
        bzero(sc->req_frames, rsize);
        bus_dmamap_load(sc->req_dmat, sc->req_map, sc->req_frames, rsize,
	    mpr_memaddr_cb, &sc->req_busaddr, 0);
	mpr_dprint(sc, MPR_INIT, "request frames busaddr= %#016jx size= %d\n",
	    (uintmax_t)sc->req_busaddr, rsize);

	sc->chains = malloc(sizeof(struct mpr_chain) * sc->num_chains, M_MPR,
	    M_NOWAIT | M_ZERO);
	if (!sc->chains) {
		mpr_dprint(sc, MPR_ERROR, "Cannot allocate chain memory\n");
		return (ENOMEM);
	}
	rsize = sc->chain_frame_size * sc->num_chains;
	if (bus_dma_tag_create( sc->mpr_parent_dmat,	/* parent */
				16, 0,			/* algnmnt, boundary */
				BUS_SPACE_MAXADDR,	/* lowaddr */
				BUS_SPACE_MAXADDR,	/* highaddr */
				NULL, NULL,		/* filter, filterarg */
				rsize,			/* maxsize */
				howmany(rsize, PAGE_SIZE), /* nsegments */
				rsize,			/* maxsegsize */
				0,			/* flags */
				NULL, NULL,		/* lockfunc, lockarg */
				&sc->chain_dmat)) {
		mpr_dprint(sc, MPR_ERROR, "Cannot allocate chain DMA tag\n");
		return (ENOMEM);
	}
	if (bus_dmamem_alloc(sc->chain_dmat, (void **)&sc->chain_frames,
	    BUS_DMA_NOWAIT | BUS_DMA_ZERO, &sc->chain_map)) {
		mpr_dprint(sc, MPR_ERROR, "Cannot allocate chain memory\n");
		return (ENOMEM);
	}
	if (bus_dmamap_load(sc->chain_dmat, sc->chain_map, sc->chain_frames,
	    rsize, mpr_load_chains_cb, sc, BUS_DMA_NOWAIT)) {
		mpr_dprint(sc, MPR_ERROR, "Cannot load chain memory\n");
		bus_dmamem_free(sc->chain_dmat, sc->chain_frames,
		    sc->chain_map);
		return (ENOMEM);
	}

	rsize = MPR_SENSE_LEN * sc->num_reqs;
	if (bus_dma_tag_create( sc->mpr_parent_dmat,    /* parent */
				1, 0,			/* algnmnt, boundary */
				BUS_SPACE_MAXADDR_32BIT,/* lowaddr */
				BUS_SPACE_MAXADDR,	/* highaddr */
				NULL, NULL,		/* filter, filterarg */
                                rsize,			/* maxsize */
                                1,			/* nsegments */
                                rsize,			/* maxsegsize */
                                0,			/* flags */
                                NULL, NULL,		/* lockfunc, lockarg */
                                &sc->sense_dmat)) {
		mpr_dprint(sc, MPR_ERROR, "Cannot allocate sense DMA tag\n");
		return (ENOMEM);
        }
        if (bus_dmamem_alloc(sc->sense_dmat, (void **)&sc->sense_frames,
	    BUS_DMA_NOWAIT, &sc->sense_map)) {
		mpr_dprint(sc, MPR_ERROR, "Cannot allocate sense memory\n");
		return (ENOMEM);
        }
        bzero(sc->sense_frames, rsize);
        bus_dmamap_load(sc->sense_dmat, sc->sense_map, sc->sense_frames, rsize,
	    mpr_memaddr_cb, &sc->sense_busaddr, 0);
	mpr_dprint(sc, MPR_INIT, "sense frames busaddr= %#016jx size= %d\n",
	    (uintmax_t)sc->sense_busaddr, rsize);

	/*
	 * Allocate NVMe PRP Pages for NVMe SGL support only if the FW supports
	 * these devices.
	 */
	if ((sc->facts->MsgVersion >= MPI2_VERSION_02_06) &&
	    (sc->facts->ProtocolFlags & MPI2_IOCFACTS_PROTOCOL_NVME_DEVICES)) {
		if (mpr_alloc_nvme_prp_pages(sc) == ENOMEM)
			return (ENOMEM);
	}

	nsegs = (sc->maxio / PAGE_SIZE) + 1;
        if (bus_dma_tag_create( sc->mpr_parent_dmat,    /* parent */
				1, 0,			/* algnmnt, boundary */
				BUS_SPACE_MAXADDR,	/* lowaddr */
				BUS_SPACE_MAXADDR,	/* highaddr */
				NULL, NULL,		/* filter, filterarg */
                                BUS_SPACE_MAXSIZE_32BIT,/* maxsize */
                                nsegs,			/* nsegments */
                                BUS_SPACE_MAXSIZE_32BIT,/* maxsegsize */
                                BUS_DMA_ALLOCNOW,	/* flags */
                                busdma_lock_mutex,	/* lockfunc */
				&sc->mpr_mtx,		/* lockarg */
                                &sc->buffer_dmat)) {
		mpr_dprint(sc, MPR_ERROR, "Cannot allocate buffer DMA tag\n");
		return (ENOMEM);
        }

	/*
	 * SMID 0 cannot be used as a free command per the firmware spec.
	 * Just drop that command instead of risking accounting bugs.
	 */
	sc->commands = malloc(sizeof(struct mpr_command) * sc->num_reqs,
	    M_MPR, M_WAITOK | M_ZERO);
	if (!sc->commands) {
		mpr_dprint(sc, MPR_ERROR, "Cannot allocate command memory\n");
		return (ENOMEM);
	}
	for (i = 1; i < sc->num_reqs; i++) {
		cm = &sc->commands[i];
		cm->cm_req = sc->req_frames + i * sc->reqframesz;
		cm->cm_req_busaddr = sc->req_busaddr + i * sc->reqframesz;
		cm->cm_sense = &sc->sense_frames[i];
		cm->cm_sense_busaddr = sc->sense_busaddr + i * MPR_SENSE_LEN;
		cm->cm_desc.Default.SMID = i;
		cm->cm_sc = sc;
		cm->cm_state = MPR_CM_STATE_BUSY;
		TAILQ_INIT(&cm->cm_chain_list);
		TAILQ_INIT(&cm->cm_prp_page_list);
		callout_init_mtx(&cm->cm_callout, &sc->mpr_mtx, 0);

		/* XXX Is a failure here a critical problem? */
		if (bus_dmamap_create(sc->buffer_dmat, 0, &cm->cm_dmamap)
		    == 0) {
			if (i <= sc->num_prireqs)
				mpr_free_high_priority_command(sc, cm);
			else
				mpr_free_command(sc, cm);
		} else {
			panic("failed to allocate command %d\n", i);
			sc->num_reqs = i;
			break;
		}
	}

	return (0);
}

/*
 * Allocate contiguous buffers for PCIe NVMe devices for building native PRPs,
 * which are scatter/gather lists for NVMe devices. 
 *
 * This buffer must be contiguous due to the nature of how NVMe PRPs are built
 * and translated by FW.
 *
 * returns ENOMEM if memory could not be allocated, otherwise returns 0.
 */
static int
mpr_alloc_nvme_prp_pages(struct mpr_softc *sc)
{
	int PRPs_per_page, PRPs_required, pages_required;
	int rsize, i;
	struct mpr_prp_page *prp_page;

	/*
	 * Assuming a MAX_IO_SIZE of 1MB and a PAGE_SIZE of 4k, the max number
	 * of PRPs (NVMe's Scatter/Gather Element) needed per I/O is:
	 * MAX_IO_SIZE / PAGE_SIZE = 256
	 * 
	 * 1 PRP entry in main frame for PRP list pointer still leaves 255 PRPs
	 * required for the remainder of the 1MB I/O. 512 PRPs can fit into one
	 * page (4096 / 8 = 512), so only one page is required for each I/O.
	 *
	 * Each of these buffers will need to be contiguous. For simplicity,
	 * only one buffer is allocated here, which has all of the space
	 * required for the NVMe Queue Depth. If there are problems allocating
	 * this one buffer, this function will need to change to allocate
	 * individual, contiguous NVME_QDEPTH buffers.
	 *
	 * The real calculation will use the real max io size. Above is just an
	 * example.
	 *
	 */
	PRPs_required = sc->maxio / PAGE_SIZE;
	PRPs_per_page = (PAGE_SIZE / PRP_ENTRY_SIZE) - 1;
	pages_required = (PRPs_required / PRPs_per_page) + 1;

	sc->prp_buffer_size = PAGE_SIZE * pages_required; 
	rsize = sc->prp_buffer_size * NVME_QDEPTH; 
	if (bus_dma_tag_create( sc->mpr_parent_dmat,	/* parent */
				4, 0,			/* algnmnt, boundary */
				BUS_SPACE_MAXADDR_32BIT,/* lowaddr */
				BUS_SPACE_MAXADDR,	/* highaddr */
				NULL, NULL,		/* filter, filterarg */
				rsize,			/* maxsize */
				1,			/* nsegments */
				rsize,			/* maxsegsize */
				0,			/* flags */
				NULL, NULL,		/* lockfunc, lockarg */
				&sc->prp_page_dmat)) {
		mpr_dprint(sc, MPR_ERROR, "Cannot allocate NVMe PRP DMA "
		    "tag\n");
		return (ENOMEM);
	}
	if (bus_dmamem_alloc(sc->prp_page_dmat, (void **)&sc->prp_pages,
	    BUS_DMA_NOWAIT, &sc->prp_page_map)) {
		mpr_dprint(sc, MPR_ERROR, "Cannot allocate NVMe PRP memory\n");
		return (ENOMEM);
	}
	bzero(sc->prp_pages, rsize);
	bus_dmamap_load(sc->prp_page_dmat, sc->prp_page_map, sc->prp_pages,
	    rsize, mpr_memaddr_cb, &sc->prp_page_busaddr, 0);

	sc->prps = malloc(sizeof(struct mpr_prp_page) * NVME_QDEPTH, M_MPR,
	    M_WAITOK | M_ZERO);
	for (i = 0; i < NVME_QDEPTH; i++) {
		prp_page = &sc->prps[i];
		prp_page->prp_page = (uint64_t *)(sc->prp_pages +
		    i * sc->prp_buffer_size);
		prp_page->prp_page_busaddr = (uint64_t)(sc->prp_page_busaddr +
		    i * sc->prp_buffer_size);
		mpr_free_prp_page(sc, prp_page);
		sc->prp_pages_free_lowwater++;
	}

	return (0);
}

static int
mpr_init_queues(struct mpr_softc *sc)
{
	int i;

	memset((uint8_t *)sc->post_queue, 0xff, sc->pqdepth * 8);

	/*
	 * According to the spec, we need to use one less reply than we
	 * have space for on the queue.  So sc->num_replies (the number we
	 * use) should be less than sc->fqdepth (allocated size).
	 */
	if (sc->num_replies >= sc->fqdepth)
		return (EINVAL);

	/*
	 * Initialize all of the free queue entries.
	 */
	for (i = 0; i < sc->fqdepth; i++) {
		sc->free_queue[i] = sc->reply_busaddr + (i * sc->replyframesz);
	}
	sc->replyfreeindex = sc->num_replies;

	return (0);
}

/* Get the driver parameter tunables.  Lowest priority are the driver defaults.
 * Next are the global settings, if they exist.  Highest are the per-unit
 * settings, if they exist.
 */
void
mpr_get_tunables(struct mpr_softc *sc)
{
	char tmpstr[80], mpr_debug[80];

	/* XXX default to some debugging for now */
	sc->mpr_debug = MPR_INFO | MPR_FAULT;
	sc->disable_msix = 0;
	sc->disable_msi = 0;
	sc->max_msix = MPR_MSIX_MAX;
	sc->max_chains = MPR_CHAIN_FRAMES;
	sc->max_io_pages = MPR_MAXIO_PAGES;
	sc->enable_ssu = MPR_SSU_ENABLE_SSD_DISABLE_HDD;
	sc->spinup_wait_time = DEFAULT_SPINUP_WAIT;
	sc->use_phynum = 1;
	sc->max_reqframes = MPR_REQ_FRAMES;
	sc->max_prireqframes = MPR_PRI_REQ_FRAMES;
	sc->max_replyframes = MPR_REPLY_FRAMES;
	sc->max_evtframes = MPR_EVT_REPLY_FRAMES;

	/*
	 * Grab the global variables.
	 */
	bzero(mpr_debug, 80);
	if (TUNABLE_STR_FETCH("hw.mpr.debug_level", mpr_debug, 80) != 0)
		mpr_parse_debug(sc, mpr_debug);
	TUNABLE_INT_FETCH("hw.mpr.disable_msix", &sc->disable_msix);
	TUNABLE_INT_FETCH("hw.mpr.disable_msi", &sc->disable_msi);
	TUNABLE_INT_FETCH("hw.mpr.max_msix", &sc->max_msix);
	TUNABLE_INT_FETCH("hw.mpr.max_chains", &sc->max_chains);
	TUNABLE_INT_FETCH("hw.mpr.max_io_pages", &sc->max_io_pages);
	TUNABLE_INT_FETCH("hw.mpr.enable_ssu", &sc->enable_ssu);
	TUNABLE_INT_FETCH("hw.mpr.spinup_wait_time", &sc->spinup_wait_time);
	TUNABLE_INT_FETCH("hw.mpr.use_phy_num", &sc->use_phynum);
	TUNABLE_INT_FETCH("hw.mpr.max_reqframes", &sc->max_reqframes);
	TUNABLE_INT_FETCH("hw.mpr.max_prireqframes", &sc->max_prireqframes);
	TUNABLE_INT_FETCH("hw.mpr.max_replyframes", &sc->max_replyframes);
	TUNABLE_INT_FETCH("hw.mpr.max_evtframes", &sc->max_evtframes);

	/* Grab the unit-instance variables */
	snprintf(tmpstr, sizeof(tmpstr), "dev.mpr.%d.debug_level",
	    device_get_unit(sc->mpr_dev));
	bzero(mpr_debug, 80);
	if (TUNABLE_STR_FETCH(tmpstr, mpr_debug, 80) != 0)
		mpr_parse_debug(sc, mpr_debug);

	snprintf(tmpstr, sizeof(tmpstr), "dev.mpr.%d.disable_msix",
	    device_get_unit(sc->mpr_dev));
	TUNABLE_INT_FETCH(tmpstr, &sc->disable_msix);

	snprintf(tmpstr, sizeof(tmpstr), "dev.mpr.%d.disable_msi",
	    device_get_unit(sc->mpr_dev));
	TUNABLE_INT_FETCH(tmpstr, &sc->disable_msi);

	snprintf(tmpstr, sizeof(tmpstr), "dev.mpr.%d.max_msix",
	    device_get_unit(sc->mpr_dev));
	TUNABLE_INT_FETCH(tmpstr, &sc->max_msix);

	snprintf(tmpstr, sizeof(tmpstr), "dev.mpr.%d.max_chains",
	    device_get_unit(sc->mpr_dev));
	TUNABLE_INT_FETCH(tmpstr, &sc->max_chains);

	snprintf(tmpstr, sizeof(tmpstr), "dev.mpr.%d.max_io_pages",
	    device_get_unit(sc->mpr_dev));
	TUNABLE_INT_FETCH(tmpstr, &sc->max_io_pages);

	bzero(sc->exclude_ids, sizeof(sc->exclude_ids));
	snprintf(tmpstr, sizeof(tmpstr), "dev.mpr.%d.exclude_ids",
	    device_get_unit(sc->mpr_dev));
	TUNABLE_STR_FETCH(tmpstr, sc->exclude_ids, sizeof(sc->exclude_ids));

	snprintf(tmpstr, sizeof(tmpstr), "dev.mpr.%d.enable_ssu",
	    device_get_unit(sc->mpr_dev));
	TUNABLE_INT_FETCH(tmpstr, &sc->enable_ssu);

	snprintf(tmpstr, sizeof(tmpstr), "dev.mpr.%d.spinup_wait_time",
	    device_get_unit(sc->mpr_dev));
	TUNABLE_INT_FETCH(tmpstr, &sc->spinup_wait_time);

	snprintf(tmpstr, sizeof(tmpstr), "dev.mpr.%d.use_phy_num",
	    device_get_unit(sc->mpr_dev));
	TUNABLE_INT_FETCH(tmpstr, &sc->use_phynum);

	snprintf(tmpstr, sizeof(tmpstr), "dev.mpr.%d.max_reqframes",
	    device_get_unit(sc->mpr_dev));
	TUNABLE_INT_FETCH(tmpstr, &sc->max_reqframes);

	snprintf(tmpstr, sizeof(tmpstr), "dev.mpr.%d.max_prireqframes",
	    device_get_unit(sc->mpr_dev));
	TUNABLE_INT_FETCH(tmpstr, &sc->max_prireqframes);

	snprintf(tmpstr, sizeof(tmpstr), "dev.mpr.%d.max_replyframes",
	    device_get_unit(sc->mpr_dev));
	TUNABLE_INT_FETCH(tmpstr, &sc->max_replyframes);

	snprintf(tmpstr, sizeof(tmpstr), "dev.mpr.%d.max_evtframes",
	    device_get_unit(sc->mpr_dev));
	TUNABLE_INT_FETCH(tmpstr, &sc->max_evtframes);
}

static void
mpr_setup_sysctl(struct mpr_softc *sc)
{
	struct sysctl_ctx_list	*sysctl_ctx = NULL;
	struct sysctl_oid	*sysctl_tree = NULL;
	char tmpstr[80], tmpstr2[80];

	/*
	 * Setup the sysctl variable so the user can change the debug level
	 * on the fly.
	 */
	snprintf(tmpstr, sizeof(tmpstr), "MPR controller %d",
	    device_get_unit(sc->mpr_dev));
	snprintf(tmpstr2, sizeof(tmpstr2), "%d", device_get_unit(sc->mpr_dev));

	sysctl_ctx = device_get_sysctl_ctx(sc->mpr_dev);
	if (sysctl_ctx != NULL)
		sysctl_tree = device_get_sysctl_tree(sc->mpr_dev);

	if (sysctl_tree == NULL) {
		sysctl_ctx_init(&sc->sysctl_ctx);
		sc->sysctl_tree = SYSCTL_ADD_NODE(&sc->sysctl_ctx,
		    SYSCTL_STATIC_CHILDREN(_hw_mpr), OID_AUTO, tmpstr2,
		    CTLFLAG_RD, 0, tmpstr);
		if (sc->sysctl_tree == NULL)
			return;
		sysctl_ctx = &sc->sysctl_ctx;
		sysctl_tree = sc->sysctl_tree;
	}

	SYSCTL_ADD_PROC(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "debug_level", CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_MPSAFE,
	    sc, 0, mpr_debug_sysctl, "A", "mpr debug level");

	SYSCTL_ADD_INT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "disable_msix", CTLFLAG_RD, &sc->disable_msix, 0,
	    "Disable the use of MSI-X interrupts");

	SYSCTL_ADD_INT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "max_msix", CTLFLAG_RD, &sc->max_msix, 0,
	    "User-defined maximum number of MSIX queues");

	SYSCTL_ADD_INT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "msix_msgs", CTLFLAG_RD, &sc->msi_msgs, 0,
	    "Negotiated number of MSIX queues");

	SYSCTL_ADD_INT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "max_reqframes", CTLFLAG_RD, &sc->max_reqframes, 0,
	    "Total number of allocated request frames");

	SYSCTL_ADD_INT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "max_prireqframes", CTLFLAG_RD, &sc->max_prireqframes, 0,
	    "Total number of allocated high priority request frames");

	SYSCTL_ADD_INT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "max_replyframes", CTLFLAG_RD, &sc->max_replyframes, 0,
	    "Total number of allocated reply frames");

	SYSCTL_ADD_INT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "max_evtframes", CTLFLAG_RD, &sc->max_evtframes, 0,
	    "Total number of event frames allocated");

	SYSCTL_ADD_STRING(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "firmware_version", CTLFLAG_RW, sc->fw_version,
	    strlen(sc->fw_version), "firmware version");

	SYSCTL_ADD_STRING(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "driver_version", CTLFLAG_RW, MPR_DRIVER_VERSION,
	    strlen(MPR_DRIVER_VERSION), "driver version");

	SYSCTL_ADD_INT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "io_cmds_active", CTLFLAG_RD,
	    &sc->io_cmds_active, 0, "number of currently active commands");

	SYSCTL_ADD_INT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "io_cmds_highwater", CTLFLAG_RD,
	    &sc->io_cmds_highwater, 0, "maximum active commands seen");

	SYSCTL_ADD_INT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "chain_free", CTLFLAG_RD,
	    &sc->chain_free, 0, "number of free chain elements");

	SYSCTL_ADD_INT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "chain_free_lowwater", CTLFLAG_RD,
	    &sc->chain_free_lowwater, 0,"lowest number of free chain elements");

	SYSCTL_ADD_INT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "max_chains", CTLFLAG_RD,
	    &sc->max_chains, 0,"maximum chain frames that will be allocated");

	SYSCTL_ADD_INT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "max_io_pages", CTLFLAG_RD,
	    &sc->max_io_pages, 0,"maximum pages to allow per I/O (if <1 use "
	    "IOCFacts)");

	SYSCTL_ADD_INT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "enable_ssu", CTLFLAG_RW, &sc->enable_ssu, 0,
	    "enable SSU to SATA SSD/HDD at shutdown");

	SYSCTL_ADD_UQUAD(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "chain_alloc_fail", CTLFLAG_RD,
	    &sc->chain_alloc_fail, "chain allocation failures");

	SYSCTL_ADD_INT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "spinup_wait_time", CTLFLAG_RD,
	    &sc->spinup_wait_time, DEFAULT_SPINUP_WAIT, "seconds to wait for "
	    "spinup after SATA ID error");

	SYSCTL_ADD_PROC(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "dump_reqs", CTLTYPE_OPAQUE | CTLFLAG_RD | CTLFLAG_SKIP, sc, 0,
	    mpr_dump_reqs, "I", "Dump Active Requests");

	SYSCTL_ADD_INT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "use_phy_num", CTLFLAG_RD, &sc->use_phynum, 0,
	    "Use the phy number for enumeration");

	SYSCTL_ADD_INT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "prp_pages_free", CTLFLAG_RD,
	    &sc->prp_pages_free, 0, "number of free PRP pages");

	SYSCTL_ADD_INT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "prp_pages_free_lowwater", CTLFLAG_RD,
	    &sc->prp_pages_free_lowwater, 0,"lowest number of free PRP pages");

	SYSCTL_ADD_UQUAD(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
	    OID_AUTO, "prp_page_alloc_fail", CTLFLAG_RD,
	    &sc->prp_page_alloc_fail, "PRP page allocation failures");
}

static struct mpr_debug_string {
	char *name;
	int flag;
} mpr_debug_strings[] = {
	{"info", MPR_INFO},
	{"fault", MPR_FAULT},
	{"event", MPR_EVENT},
	{"log", MPR_LOG},
	{"recovery", MPR_RECOVERY},
	{"error", MPR_ERROR},
	{"init", MPR_INIT},
	{"xinfo", MPR_XINFO},
	{"user", MPR_USER},
	{"mapping", MPR_MAPPING},
	{"trace", MPR_TRACE}
};

enum mpr_debug_level_combiner {
	COMB_NONE,
	COMB_ADD,
	COMB_SUB
};

static int
mpr_debug_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct mpr_softc *sc;
	struct mpr_debug_string *string;
	struct sbuf *sbuf;
	char *buffer;
	size_t sz;
	int i, len, debug, error;

	sc = (struct mpr_softc *)arg1;

	error = sysctl_wire_old_buffer(req, 0);
	if (error != 0)
		return (error);

	sbuf = sbuf_new_for_sysctl(NULL, NULL, 128, req);
	debug = sc->mpr_debug;

	sbuf_printf(sbuf, "%#x", debug);

	sz = sizeof(mpr_debug_strings) / sizeof(mpr_debug_strings[0]);
	for (i = 0; i < sz; i++) {
		string = &mpr_debug_strings[i];
		if (debug & string->flag) 
			sbuf_printf(sbuf, ",%s", string->name);
	}

	error = sbuf_finish(sbuf);
	sbuf_delete(sbuf);

	if (error || req->newptr == NULL)
		return (error);

	len = req->newlen - req->newidx;
	if (len == 0)
		return (0);

	buffer = malloc(len, M_MPR, M_ZERO|M_WAITOK);
	error = SYSCTL_IN(req, buffer, len);

	mpr_parse_debug(sc, buffer);

	free(buffer, M_MPR);
	return (error);
}

static void
mpr_parse_debug(struct mpr_softc *sc, char *list)
{
	struct mpr_debug_string *string;
	enum mpr_debug_level_combiner op;
	char *token, *endtoken;
	size_t sz;
	int flags, i;

	if (list == NULL || *list == '\0')
		return;

	if (*list == '+') {
		op = COMB_ADD;
		list++;
	} else if (*list == '-') {
		op = COMB_SUB;
		list++;
	} else
		op = COMB_NONE;
	if (*list == '\0')
		return;

	flags = 0;
	sz = sizeof(mpr_debug_strings) / sizeof(mpr_debug_strings[0]);
	while ((token = strsep(&list, ":,")) != NULL) {

		/* Handle integer flags */
		flags |= strtol(token, &endtoken, 0);
		if (token != endtoken)
			continue;

		/* Handle text flags */
		for (i = 0; i < sz; i++) {
			string = &mpr_debug_strings[i];
			if (strcasecmp(token, string->name) == 0) {
				flags |= string->flag;
				break;
			}
		}
	}

	switch (op) {
	case COMB_NONE:
		sc->mpr_debug = flags;
		break;
	case COMB_ADD:
		sc->mpr_debug |= flags;
		break;
	case COMB_SUB:
		sc->mpr_debug &= (~flags);
		break;
	}
	return;
}

struct mpr_dumpreq_hdr {
	uint32_t	smid;
	uint32_t	state;
	uint32_t	numframes;
	uint32_t	deschi;
	uint32_t	desclo;
};

static int
mpr_dump_reqs(SYSCTL_HANDLER_ARGS)
{
	struct mpr_softc *sc;
	struct mpr_chain *chain, *chain1;
	struct mpr_command *cm;
	struct mpr_dumpreq_hdr hdr;
	struct sbuf *sb;
	uint32_t smid, state;
	int i, numreqs, error = 0;

	sc = (struct mpr_softc *)arg1;

	if ((error = priv_check(curthread, PRIV_DRIVER)) != 0) {
		printf("priv check error %d\n", error);
		return (error);
	}

	state = MPR_CM_STATE_INQUEUE;
	smid = 1;
	numreqs = sc->num_reqs;

	if (req->newptr != NULL)
		return (EINVAL);

	if (smid == 0 || smid > sc->num_reqs)
		return (EINVAL);
	if (numreqs <= 0 || (numreqs + smid > sc->num_reqs))
		numreqs = sc->num_reqs;
	sb = sbuf_new_for_sysctl(NULL, NULL, 4096, req);

	/* Best effort, no locking */
	for (i = smid; i < numreqs; i++) {
		cm = &sc->commands[i];
		if (cm->cm_state != state)
			continue;
		hdr.smid = i;
		hdr.state = cm->cm_state;
		hdr.numframes = 1;
		hdr.deschi = cm->cm_desc.Words.High;
		hdr.desclo = cm->cm_desc.Words.Low;
		TAILQ_FOREACH_SAFE(chain, &cm->cm_chain_list, chain_link,
		   chain1)
			hdr.numframes++;
		sbuf_bcat(sb, &hdr, sizeof(hdr));
		sbuf_bcat(sb, cm->cm_req, 128);
		TAILQ_FOREACH_SAFE(chain, &cm->cm_chain_list, chain_link,
		    chain1)
			sbuf_bcat(sb, chain->chain, 128);
	}

	error = sbuf_finish(sb);
	sbuf_delete(sb);
	return (error);
}

int
mpr_attach(struct mpr_softc *sc)
{
	int error;

	MPR_FUNCTRACE(sc);
	mpr_dprint(sc, MPR_INIT, "%s entered\n", __func__);

	mtx_init(&sc->mpr_mtx, "MPR lock", NULL, MTX_DEF);
	callout_init_mtx(&sc->periodic, &sc->mpr_mtx, 0);
	callout_init_mtx(&sc->device_check_callout, &sc->mpr_mtx, 0);
	TAILQ_INIT(&sc->event_list);
	timevalclear(&sc->lastfail);

	if ((error = mpr_transition_ready(sc)) != 0) {
		mpr_dprint(sc, MPR_INIT|MPR_FAULT,
		    "Failed to transition ready\n");
		return (error);
	}

	sc->facts = malloc(sizeof(MPI2_IOC_FACTS_REPLY), M_MPR,
	    M_ZERO|M_NOWAIT);
	if (!sc->facts) {
		mpr_dprint(sc, MPR_INIT|MPR_FAULT,
		    "Cannot allocate memory, exit\n");
		return (ENOMEM);
	}

	/*
	 * Get IOC Facts and allocate all structures based on this information.
	 * A Diag Reset will also call mpr_iocfacts_allocate and re-read the IOC
	 * Facts. If relevant values have changed in IOC Facts, this function
	 * will free all of the memory based on IOC Facts and reallocate that
	 * memory.  If this fails, any allocated memory should already be freed.
	 */
	if ((error = mpr_iocfacts_allocate(sc, TRUE)) != 0) {
		mpr_dprint(sc, MPR_INIT|MPR_FAULT, "IOC Facts allocation "
		    "failed with error %d\n", error);
		return (error);
	}

	/* Start the periodic watchdog check on the IOC Doorbell */
	mpr_periodic(sc);

	/*
	 * The portenable will kick off discovery events that will drive the
	 * rest of the initialization process.  The CAM/SAS module will
	 * hold up the boot sequence until discovery is complete.
	 */
	sc->mpr_ich.ich_func = mpr_startup;
	sc->mpr_ich.ich_arg = sc;
	if (config_intrhook_establish(&sc->mpr_ich) != 0) {
		mpr_dprint(sc, MPR_INIT|MPR_ERROR,
		    "Cannot establish MPR config hook\n");
		error = EINVAL;
	}

	/*
	 * Allow IR to shutdown gracefully when shutdown occurs.
	 */
	sc->shutdown_eh = EVENTHANDLER_REGISTER(shutdown_final,
	    mprsas_ir_shutdown, sc, SHUTDOWN_PRI_DEFAULT);

	if (sc->shutdown_eh == NULL)
		mpr_dprint(sc, MPR_INIT|MPR_ERROR,
		    "shutdown event registration failed\n");

	mpr_setup_sysctl(sc);

	sc->mpr_flags |= MPR_FLAGS_ATTACH_DONE;
	mpr_dprint(sc, MPR_INIT, "%s exit error= %d\n", __func__, error);

	return (error);
}

/* Run through any late-start handlers. */
static void
mpr_startup(void *arg)
{
	struct mpr_softc *sc;

	sc = (struct mpr_softc *)arg;
	mpr_dprint(sc, MPR_INIT, "%s entered\n", __func__);

	mpr_lock(sc);
	mpr_unmask_intr(sc);

	/* initialize device mapping tables */
	mpr_base_static_config_pages(sc);
	mpr_mapping_initialize(sc);
	mprsas_startup(sc);
	mpr_unlock(sc);

	mpr_dprint(sc, MPR_INIT, "disestablish config intrhook\n");
	config_intrhook_disestablish(&sc->mpr_ich);
	sc->mpr_ich.ich_arg = NULL;

	mpr_dprint(sc, MPR_INIT, "%s exit\n", __func__);
}

/* Periodic watchdog.  Is called with the driver lock already held. */
static void
mpr_periodic(void *arg)
{
	struct mpr_softc *sc;
	uint32_t db;

	sc = (struct mpr_softc *)arg;
	if (sc->mpr_flags & MPR_FLAGS_SHUTDOWN)
		return;

	db = mpr_regread(sc, MPI2_DOORBELL_OFFSET);
	if ((db & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_FAULT) {
		if ((db & MPI2_DOORBELL_FAULT_CODE_MASK) ==
		    IFAULT_IOP_OVER_TEMP_THRESHOLD_EXCEEDED) {
			panic("TEMPERATURE FAULT: STOPPING.");
		}
		mpr_dprint(sc, MPR_FAULT, "IOC Fault 0x%08x, Resetting\n", db);
		mpr_reinit(sc);
	}

	callout_reset(&sc->periodic, MPR_PERIODIC_DELAY * hz, mpr_periodic, sc);
}

static void
mpr_log_evt_handler(struct mpr_softc *sc, uintptr_t data,
    MPI2_EVENT_NOTIFICATION_REPLY *event)
{
	MPI2_EVENT_DATA_LOG_ENTRY_ADDED *entry;

	MPR_DPRINT_EVENT(sc, generic, event);

	switch (event->Event) {
	case MPI2_EVENT_LOG_DATA:
		mpr_dprint(sc, MPR_EVENT, "MPI2_EVENT_LOG_DATA:\n");
		if (sc->mpr_debug & MPR_EVENT)
			hexdump(event->EventData, event->EventDataLength, NULL,
			    0);
		break;
	case MPI2_EVENT_LOG_ENTRY_ADDED:
		entry = (MPI2_EVENT_DATA_LOG_ENTRY_ADDED *)event->EventData;
		mpr_dprint(sc, MPR_EVENT, "MPI2_EVENT_LOG_ENTRY_ADDED event "
		    "0x%x Sequence %d:\n", entry->LogEntryQualifier,
		     entry->LogSequence);
		break;
	default:
		break;
	}
	return;
}

static int
mpr_attach_log(struct mpr_softc *sc)
{
	uint8_t events[16];

	bzero(events, 16);
	setbit(events, MPI2_EVENT_LOG_DATA);
	setbit(events, MPI2_EVENT_LOG_ENTRY_ADDED);

	mpr_register_events(sc, events, mpr_log_evt_handler, NULL,
	    &sc->mpr_log_eh);

	return (0);
}

static int
mpr_detach_log(struct mpr_softc *sc)
{

	if (sc->mpr_log_eh != NULL)
		mpr_deregister_events(sc, sc->mpr_log_eh);
	return (0);
}

/*
 * Free all of the driver resources and detach submodules.  Should be called
 * without the lock held.
 */
int
mpr_free(struct mpr_softc *sc)
{
	int error;

	mpr_dprint(sc, MPR_INIT, "%s entered\n", __func__);
	/* Turn off the watchdog */
	mpr_lock(sc);
	sc->mpr_flags |= MPR_FLAGS_SHUTDOWN;
	mpr_unlock(sc);
	/* Lock must not be held for this */
	callout_drain(&sc->periodic);
	callout_drain(&sc->device_check_callout);

	if (((error = mpr_detach_log(sc)) != 0) ||
	    ((error = mpr_detach_sas(sc)) != 0)) {
		mpr_dprint(sc, MPR_INIT|MPR_FAULT, "failed to detach "
		    "subsystems, error= %d, exit\n", error);
		return (error);
	}

	mpr_detach_user(sc);

	/* Put the IOC back in the READY state. */
	mpr_lock(sc);
	if ((error = mpr_transition_ready(sc)) != 0) {
		mpr_unlock(sc);
		return (error);
	}
	mpr_unlock(sc);

	if (sc->facts != NULL)
		free(sc->facts, M_MPR);

	/*
	 * Free all buffers that are based on IOC Facts.  A Diag Reset may need
	 * to free these buffers too.
	 */
	mpr_iocfacts_free(sc);

	if (sc->sysctl_tree != NULL)
		sysctl_ctx_free(&sc->sysctl_ctx);

	/* Deregister the shutdown function */
	if (sc->shutdown_eh != NULL)
		EVENTHANDLER_DEREGISTER(shutdown_final, sc->shutdown_eh);

	mtx_destroy(&sc->mpr_mtx);
	mpr_dprint(sc, MPR_INIT, "%s exit\n", __func__);

	return (0);
}

static __inline void
mpr_complete_command(struct mpr_softc *sc, struct mpr_command *cm)
{
	MPR_FUNCTRACE(sc);

	if (cm == NULL) {
		mpr_dprint(sc, MPR_ERROR, "Completing NULL command\n");
		return;
	}

	if (cm->cm_flags & MPR_CM_FLAGS_POLLED)
		cm->cm_flags |= MPR_CM_FLAGS_COMPLETE;

	if (cm->cm_complete != NULL) {
		mpr_dprint(sc, MPR_TRACE,
		    "%s cm %p calling cm_complete %p data %p reply %p\n",
		    __func__, cm, cm->cm_complete, cm->cm_complete_data,
		    cm->cm_reply);
		cm->cm_complete(sc, cm);
	}

	if (cm->cm_flags & MPR_CM_FLAGS_WAKEUP) {
		mpr_dprint(sc, MPR_TRACE, "waking up %p\n", cm);
		wakeup(cm);
	}

	if (sc->io_cmds_active != 0) {
		sc->io_cmds_active--;
	} else {
		mpr_dprint(sc, MPR_ERROR, "Warning: io_cmds_active is "
		    "out of sync - resynching to 0\n");
	}
}

static void
mpr_sas_log_info(struct mpr_softc *sc , u32 log_info)
{
	union loginfo_type {
		u32	loginfo;
		struct {
			u32	subcode:16;
			u32	code:8;
			u32	originator:4;
			u32	bus_type:4;
		} dw;
	};
	union loginfo_type sas_loginfo;
	char *originator_str = NULL;
 
	sas_loginfo.loginfo = log_info;
	if (sas_loginfo.dw.bus_type != 3 /*SAS*/)
		return;
 
	/* each nexus loss loginfo */
	if (log_info == 0x31170000)
		return;
 
	/* eat the loginfos associated with task aborts */
	if ((log_info == 30050000) || (log_info == 0x31140000) ||
	    (log_info == 0x31130000))
		return;
 
	switch (sas_loginfo.dw.originator) {
	case 0:
		originator_str = "IOP";
		break;
	case 1:
		originator_str = "PL";
		break;
	case 2:
		originator_str = "IR";
		break;
	}
 
	mpr_dprint(sc, MPR_LOG, "log_info(0x%08x): originator(%s), "
	    "code(0x%02x), sub_code(0x%04x)\n", log_info, originator_str,
	    sas_loginfo.dw.code, sas_loginfo.dw.subcode);
}

static void
mpr_display_reply_info(struct mpr_softc *sc, uint8_t *reply)
{
	MPI2DefaultReply_t *mpi_reply;
	u16 sc_status;
 
	mpi_reply = (MPI2DefaultReply_t*)reply;
	sc_status = le16toh(mpi_reply->IOCStatus);
	if (sc_status & MPI2_IOCSTATUS_FLAG_LOG_INFO_AVAILABLE)
		mpr_sas_log_info(sc, le32toh(mpi_reply->IOCLogInfo));
}

void
mpr_intr(void *data)
{
	struct mpr_softc *sc;
	uint32_t status;

	sc = (struct mpr_softc *)data;
	mpr_dprint(sc, MPR_TRACE, "%s\n", __func__);

	/*
	 * Check interrupt status register to flush the bus.  This is
	 * needed for both INTx interrupts and driver-driven polling
	 */
	status = mpr_regread(sc, MPI2_HOST_INTERRUPT_STATUS_OFFSET);
	if ((status & MPI2_HIS_REPLY_DESCRIPTOR_INTERRUPT) == 0)
		return;

	mpr_lock(sc);
	mpr_intr_locked(data);
	mpr_unlock(sc);
	return;
}

/*
 * In theory, MSI/MSIX interrupts shouldn't need to read any registers on the
 * chip.  Hopefully this theory is correct.
 */
void
mpr_intr_msi(void *data)
{
	struct mpr_softc *sc;

	sc = (struct mpr_softc *)data;
	mpr_dprint(sc, MPR_TRACE, "%s\n", __func__);
	mpr_lock(sc);
	mpr_intr_locked(data);
	mpr_unlock(sc);
	return;
}

/*
 * The locking is overly broad and simplistic, but easy to deal with for now.
 */
void
mpr_intr_locked(void *data)
{
	MPI2_REPLY_DESCRIPTORS_UNION *desc;
	MPI2_DIAG_RELEASE_REPLY *rel_rep;
	mpr_fw_diagnostic_buffer_t *pBuffer;
	struct mpr_softc *sc;
	uint64_t tdesc;
	struct mpr_command *cm = NULL;
	uint8_t flags;
	u_int pq;

	sc = (struct mpr_softc *)data;

	pq = sc->replypostindex;
	mpr_dprint(sc, MPR_TRACE,
	    "%s sc %p starting with replypostindex %u\n", 
	    __func__, sc, sc->replypostindex);

	for ( ;; ) {
		cm = NULL;
		desc = &sc->post_queue[sc->replypostindex];

		/*
		 * Copy and clear out the descriptor so that any reentry will
		 * immediately know that this descriptor has already been
		 * looked at.  There is unfortunate casting magic because the
		 * MPI API doesn't have a cardinal 64bit type.
		 */
		tdesc = 0xffffffffffffffff;
		tdesc = atomic_swap_64((uint64_t *)desc, tdesc);
		desc = (MPI2_REPLY_DESCRIPTORS_UNION *)&tdesc;

		flags = desc->Default.ReplyFlags &
		    MPI2_RPY_DESCRIPT_FLAGS_TYPE_MASK;
		if ((flags == MPI2_RPY_DESCRIPT_FLAGS_UNUSED) ||
		    (le32toh(desc->Words.High) == 0xffffffff))
			break;

		/* increment the replypostindex now, so that event handlers
		 * and cm completion handlers which decide to do a diag
		 * reset can zero it without it getting incremented again
		 * afterwards, and we break out of this loop on the next
		 * iteration since the reply post queue has been cleared to
		 * 0xFF and all descriptors look unused (which they are).
		 */
		if (++sc->replypostindex >= sc->pqdepth)
			sc->replypostindex = 0;

		switch (flags) {
		case MPI2_RPY_DESCRIPT_FLAGS_SCSI_IO_SUCCESS:
		case MPI25_RPY_DESCRIPT_FLAGS_FAST_PATH_SCSI_IO_SUCCESS:
		case MPI26_RPY_DESCRIPT_FLAGS_PCIE_ENCAPSULATED_SUCCESS:
			cm = &sc->commands[le16toh(desc->SCSIIOSuccess.SMID)];
			KASSERT(cm->cm_state == MPR_CM_STATE_INQUEUE,
			    ("command not inqueue\n"));
			cm->cm_state = MPR_CM_STATE_BUSY;
			cm->cm_reply = NULL;
			break;
		case MPI2_RPY_DESCRIPT_FLAGS_ADDRESS_REPLY:
		{
			uint32_t baddr;
			uint8_t *reply;

			/*
			 * Re-compose the reply address from the address
			 * sent back from the chip.  The ReplyFrameAddress
			 * is the lower 32 bits of the physical address of
			 * particular reply frame.  Convert that address to
			 * host format, and then use that to provide the
			 * offset against the virtual address base
			 * (sc->reply_frames).
			 */
			baddr = le32toh(desc->AddressReply.ReplyFrameAddress);
			reply = sc->reply_frames +
				(baddr - ((uint32_t)sc->reply_busaddr));
			/*
			 * Make sure the reply we got back is in a valid
			 * range.  If not, go ahead and panic here, since
			 * we'll probably panic as soon as we deference the
			 * reply pointer anyway.
			 */
			if ((reply < sc->reply_frames)
			 || (reply > (sc->reply_frames +
			     (sc->fqdepth * sc->replyframesz)))) {
				printf("%s: WARNING: reply %p out of range!\n",
				       __func__, reply);
				printf("%s: reply_frames %p, fqdepth %d, "
				       "frame size %d\n", __func__,
				       sc->reply_frames, sc->fqdepth,
				       sc->replyframesz);
				printf("%s: baddr %#x,\n", __func__, baddr);
				/* LSI-TODO. See Linux Code for Graceful exit */
				panic("Reply address out of range");
			}
			if (le16toh(desc->AddressReply.SMID) == 0) {
				if (((MPI2_DEFAULT_REPLY *)reply)->Function ==
				    MPI2_FUNCTION_DIAG_BUFFER_POST) {
					/*
					 * If SMID is 0 for Diag Buffer Post,
					 * this implies that the reply is due to
					 * a release function with a status that
					 * the buffer has been released.  Set
					 * the buffer flags accordingly.
					 */
					rel_rep =
					    (MPI2_DIAG_RELEASE_REPLY *)reply;
					if ((le16toh(rel_rep->IOCStatus) &
					    MPI2_IOCSTATUS_MASK) ==
					    MPI2_IOCSTATUS_DIAGNOSTIC_RELEASED)
					{
						pBuffer =
						    &sc->fw_diag_buffer_list[
						    rel_rep->BufferType];
						pBuffer->valid_data = TRUE;
						pBuffer->owned_by_firmware =
						    FALSE;
						pBuffer->immediate = FALSE;
					}
				} else
					mpr_dispatch_event(sc, baddr,
					    (MPI2_EVENT_NOTIFICATION_REPLY *)
					    reply);
			} else {
				cm = &sc->commands[
				    le16toh(desc->AddressReply.SMID)];
				if (cm->cm_state != MPR_CM_STATE_TIMEDOUT)
					cm->cm_state = MPR_CM_STATE_BUSY;
				cm->cm_reply = reply;
				cm->cm_reply_data =
				    le32toh(desc->AddressReply.
				    ReplyFrameAddress);
			}
			break;
		}
		case MPI2_RPY_DESCRIPT_FLAGS_TARGETASSIST_SUCCESS:
		case MPI2_RPY_DESCRIPT_FLAGS_TARGET_COMMAND_BUFFER:
		case MPI2_RPY_DESCRIPT_FLAGS_RAID_ACCELERATOR_SUCCESS:
		default:
			/* Unhandled */
			mpr_dprint(sc, MPR_ERROR, "Unhandled reply 0x%x\n",
			    desc->Default.ReplyFlags);
			cm = NULL;
			break;
		}

		if (cm != NULL) {
			// Print Error reply frame
			if (cm->cm_reply)
				mpr_display_reply_info(sc,cm->cm_reply);
			mpr_complete_command(sc, cm);
		}
	}

	if (pq != sc->replypostindex) {
		mpr_dprint(sc, MPR_TRACE, "%s sc %p writing postindex %d\n",
		    __func__, sc, sc->replypostindex);
		mpr_regwrite(sc, MPI2_REPLY_POST_HOST_INDEX_OFFSET,
		    sc->replypostindex);
	}

	return;
}

static void
mpr_dispatch_event(struct mpr_softc *sc, uintptr_t data,
    MPI2_EVENT_NOTIFICATION_REPLY *reply)
{
	struct mpr_event_handle *eh;
	int event, handled = 0;

	event = le16toh(reply->Event);
	TAILQ_FOREACH(eh, &sc->event_list, eh_list) {
		if (isset(eh->mask, event)) {
			eh->callback(sc, data, reply);
			handled++;
		}
	}

	if (handled == 0)
		mpr_dprint(sc, MPR_EVENT, "Unhandled event 0x%x\n",
		    le16toh(event));

	/*
	 * This is the only place that the event/reply should be freed.
	 * Anything wanting to hold onto the event data should have
	 * already copied it into their own storage.
	 */
	mpr_free_reply(sc, data);
}

static void
mpr_reregister_events_complete(struct mpr_softc *sc, struct mpr_command *cm)
{
	mpr_dprint(sc, MPR_TRACE, "%s\n", __func__);

	if (cm->cm_reply)
		MPR_DPRINT_EVENT(sc, generic,
			(MPI2_EVENT_NOTIFICATION_REPLY *)cm->cm_reply);

	mpr_free_command(sc, cm);

	/* next, send a port enable */
	mprsas_startup(sc);
}

/*
 * For both register_events and update_events, the caller supplies a bitmap
 * of events that it _wants_.  These functions then turn that into a bitmask
 * suitable for the controller.
 */
int
mpr_register_events(struct mpr_softc *sc, uint8_t *mask,
    mpr_evt_callback_t *cb, void *data, struct mpr_event_handle **handle)
{
	struct mpr_event_handle *eh;
	int error = 0;

	eh = malloc(sizeof(struct mpr_event_handle), M_MPR, M_WAITOK|M_ZERO);
	if (!eh) {
		mpr_dprint(sc, MPR_EVENT|MPR_ERROR,
		    "Cannot allocate event memory\n");
		return (ENOMEM);
	}
	eh->callback = cb;
	eh->data = data;
	TAILQ_INSERT_TAIL(&sc->event_list, eh, eh_list);
	if (mask != NULL)
		error = mpr_update_events(sc, eh, mask);
	*handle = eh;

	return (error);
}

int
mpr_update_events(struct mpr_softc *sc, struct mpr_event_handle *handle,
    uint8_t *mask)
{
	MPI2_EVENT_NOTIFICATION_REQUEST *evtreq;
	MPI2_EVENT_NOTIFICATION_REPLY *reply = NULL;
	struct mpr_command *cm = NULL;
	struct mpr_event_handle *eh;
	int error, i;

	mpr_dprint(sc, MPR_TRACE, "%s\n", __func__);

	if ((mask != NULL) && (handle != NULL))
		bcopy(mask, &handle->mask[0], 16);
	memset(sc->event_mask, 0xff, 16);

	TAILQ_FOREACH(eh, &sc->event_list, eh_list) {
		for (i = 0; i < 16; i++)
			sc->event_mask[i] &= ~eh->mask[i];
	}

	if ((cm = mpr_alloc_command(sc)) == NULL)
		return (EBUSY);
	evtreq = (MPI2_EVENT_NOTIFICATION_REQUEST *)cm->cm_req;
	evtreq->Function = MPI2_FUNCTION_EVENT_NOTIFICATION;
	evtreq->MsgFlags = 0;
	evtreq->SASBroadcastPrimitiveMasks = 0;
#ifdef MPR_DEBUG_ALL_EVENTS
	{
		u_char fullmask[16];
		memset(fullmask, 0x00, 16);
		bcopy(fullmask, (uint8_t *)&evtreq->EventMasks, 16);
	}
#else
		bcopy(sc->event_mask, (uint8_t *)&evtreq->EventMasks, 16);
#endif
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	cm->cm_data = NULL;

	error = mpr_request_polled(sc, &cm);
	if (cm != NULL)
		reply = (MPI2_EVENT_NOTIFICATION_REPLY *)cm->cm_reply;
	if ((reply == NULL) ||
	    (reply->IOCStatus & MPI2_IOCSTATUS_MASK) != MPI2_IOCSTATUS_SUCCESS)
		error = ENXIO;
	
	if (reply)
		MPR_DPRINT_EVENT(sc, generic, reply);

	mpr_dprint(sc, MPR_TRACE, "%s finished error %d\n", __func__, error);

	if (cm != NULL)
		mpr_free_command(sc, cm);
	return (error);
}

static int
mpr_reregister_events(struct mpr_softc *sc)
{
	MPI2_EVENT_NOTIFICATION_REQUEST *evtreq;
	struct mpr_command *cm;
	struct mpr_event_handle *eh;
	int error, i;

	mpr_dprint(sc, MPR_TRACE, "%s\n", __func__);

	/* first, reregister events */

	memset(sc->event_mask, 0xff, 16);

	TAILQ_FOREACH(eh, &sc->event_list, eh_list) {
		for (i = 0; i < 16; i++)
			sc->event_mask[i] &= ~eh->mask[i];
	}

	if ((cm = mpr_alloc_command(sc)) == NULL)
		return (EBUSY);
	evtreq = (MPI2_EVENT_NOTIFICATION_REQUEST *)cm->cm_req;
	evtreq->Function = MPI2_FUNCTION_EVENT_NOTIFICATION;
	evtreq->MsgFlags = 0;
	evtreq->SASBroadcastPrimitiveMasks = 0;
#ifdef MPR_DEBUG_ALL_EVENTS
	{
		u_char fullmask[16];
		memset(fullmask, 0x00, 16);
		bcopy(fullmask, (uint8_t *)&evtreq->EventMasks, 16);
	}
#else
		bcopy(sc->event_mask, (uint8_t *)&evtreq->EventMasks, 16);
#endif
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;
	cm->cm_data = NULL;
	cm->cm_complete = mpr_reregister_events_complete;

	error = mpr_map_command(sc, cm);

	mpr_dprint(sc, MPR_TRACE, "%s finished with error %d\n", __func__,
	    error);
	return (error);
}

int
mpr_deregister_events(struct mpr_softc *sc, struct mpr_event_handle *handle)
{

	TAILQ_REMOVE(&sc->event_list, handle, eh_list);
	free(handle, M_MPR);
	return (mpr_update_events(sc, NULL, NULL));
}

/**
* mpr_build_nvme_prp - This function is called for NVMe end devices to build a
* native SGL (NVMe PRP). The native SGL is built starting in the first PRP entry
* of the NVMe message (PRP1). If the data buffer is small enough to be described
* entirely using PRP1, then PRP2 is not used. If needed, PRP2 is used to
* describe a larger data buffer. If the data buffer is too large to describe
* using the two PRP entriess inside the NVMe message, then PRP1 describes the
* first data memory segment, and PRP2 contains a pointer to a PRP list located
* elsewhere in memory to describe the remaining data memory segments. The PRP
* list will be contiguous.

* The native SGL for NVMe devices is a Physical Region Page (PRP). A PRP
* consists of a list of PRP entries to describe a number of noncontigous
* physical memory segments as a single memory buffer, just as a SGL does. Note
* however, that this function is only used by the IOCTL call, so the memory
* given will be guaranteed to be contiguous. There is no need to translate
* non-contiguous SGL into a PRP in this case. All PRPs will describe contiguous
* space that is one page size each.
*
* Each NVMe message contains two PRP entries. The first (PRP1) either contains
* a PRP list pointer or a PRP element, depending upon the command. PRP2 contains
* the second PRP element if the memory being described fits within 2 PRP
* entries, or a PRP list pointer if the PRP spans more than two entries.
*
* A PRP list pointer contains the address of a PRP list, structured as a linear
* array of PRP entries. Each PRP entry in this list describes a segment of
* physical memory.
*
* Each 64-bit PRP entry comprises an address and an offset field. The address
* always points to the beginning of a PAGE_SIZE physical memory page, and the
* offset describes where within that page the memory segment begins. Only the
* first element in a PRP list may contain a non-zero offest, implying that all
* memory segments following the first begin at the start of a PAGE_SIZE page.
*
* Each PRP element normally describes a chunck of PAGE_SIZE physical memory,
* with exceptions for the first and last elements in the list. If the memory
* being described by the list begins at a non-zero offset within the first page,
* then the first PRP element will contain a non-zero offset indicating where the
* region begins within the page. The last memory segment may end before the end
* of the PAGE_SIZE segment, depending upon the overall size of the memory being
* described by the PRP list. 
*
* Since PRP entries lack any indication of size, the overall data buffer length
* is used to determine where the end of the data memory buffer is located, and
* how many PRP entries are required to describe it.
*
* Returns nothing.
*/
void 
mpr_build_nvme_prp(struct mpr_softc *sc, struct mpr_command *cm,
    Mpi26NVMeEncapsulatedRequest_t *nvme_encap_request, void *data,
    uint32_t data_in_sz, uint32_t data_out_sz)
{
	int			prp_size = PRP_ENTRY_SIZE;
	uint64_t		*prp_entry, *prp1_entry, *prp2_entry;
	uint64_t		*prp_entry_phys, *prp_page, *prp_page_phys;
	uint32_t		offset, entry_len, page_mask_result, page_mask;
	bus_addr_t		paddr;
	size_t			length;
	struct mpr_prp_page	*prp_page_info = NULL;

	/*
	 * Not all commands require a data transfer. If no data, just return
	 * without constructing any PRP.
	 */
	if (!data_in_sz && !data_out_sz)
		return;

	/*
	 * Set pointers to PRP1 and PRP2, which are in the NVMe command. PRP1 is
	 * located at a 24 byte offset from the start of the NVMe command. Then
	 * set the current PRP entry pointer to PRP1.
	 */
	prp1_entry = (uint64_t *)(nvme_encap_request->NVMe_Command +
	    NVME_CMD_PRP1_OFFSET);
	prp2_entry = (uint64_t *)(nvme_encap_request->NVMe_Command +
	    NVME_CMD_PRP2_OFFSET);
	prp_entry = prp1_entry;

	/*
	 * For the PRP entries, use the specially allocated buffer of
	 * contiguous memory. PRP Page allocation failures should not happen
	 * because there should be enough PRP page buffers to account for the
	 * possible NVMe QDepth.
	 */
	prp_page_info = mpr_alloc_prp_page(sc);
	KASSERT(prp_page_info != NULL, ("%s: There are no PRP Pages left to be "
	    "used for building a native NVMe SGL.\n", __func__));
	prp_page = (uint64_t *)prp_page_info->prp_page;
	prp_page_phys = (uint64_t *)(uintptr_t)prp_page_info->prp_page_busaddr;

	/*
	 * Insert the allocated PRP page into the command's PRP page list. This
	 * will be freed when the command is freed.
	 */
	TAILQ_INSERT_TAIL(&cm->cm_prp_page_list, prp_page_info, prp_page_link);

	/*
	 * Check if we are within 1 entry of a page boundary we don't want our
	 * first entry to be a PRP List entry.
	 */
	page_mask = PAGE_SIZE - 1;
	page_mask_result = (uintptr_t)((uint8_t *)prp_page + prp_size) &
	    page_mask;
	if (!page_mask_result)
	{
		/* Bump up to next page boundary. */
		prp_page = (uint64_t *)((uint8_t *)prp_page + prp_size);
		prp_page_phys = (uint64_t *)((uint8_t *)prp_page_phys +
		    prp_size);
	}

	/*
	 * Set PRP physical pointer, which initially points to the current PRP
	 * DMA memory page.
	 */
	prp_entry_phys = prp_page_phys;

	/* Get physical address and length of the data buffer. */
	paddr = (bus_addr_t)(uintptr_t)data;
	if (data_in_sz)
		length = data_in_sz;
	else
		length = data_out_sz;

	/* Loop while the length is not zero. */
	while (length)
	{
		/*
		 * Check if we need to put a list pointer here if we are at page
		 * boundary - prp_size (8 bytes).
		 */
		page_mask_result = (uintptr_t)((uint8_t *)prp_entry_phys +
		    prp_size) & page_mask;
		if (!page_mask_result)
		{
			/*
			 * This is the last entry in a PRP List, so we need to
			 * put a PRP list pointer here. What this does is:
			 *   - bump the current memory pointer to the next
			 *     address, which will be the next full page.
			 *   - set the PRP Entry to point to that page. This is
			 *     now the PRP List pointer.
			 *   - bump the PRP Entry pointer the start of the next
			 *     page. Since all of this PRP memory is contiguous,
			 *     no need to get a new page - it's just the next
			 *     address.
			 */
			prp_entry_phys++;
			*prp_entry =
			    htole64((uint64_t)(uintptr_t)prp_entry_phys);
			prp_entry++;
		}

		/* Need to handle if entry will be part of a page. */
		offset = (uint32_t)paddr & page_mask;
		entry_len = PAGE_SIZE - offset;

		if (prp_entry == prp1_entry)
		{
			/*
			 * Must fill in the first PRP pointer (PRP1) before
			 * moving on.
			 */
			*prp1_entry = htole64((uint64_t)paddr);

			/*
			 * Now point to the second PRP entry within the
			 * command (PRP2).
			 */
			prp_entry = prp2_entry;
		}
		else if (prp_entry == prp2_entry)
		{
			/*
			 * Should the PRP2 entry be a PRP List pointer or just a
			 * regular PRP pointer? If there is more than one more
			 * page of data, must use a PRP List pointer.
			 */
			if (length > PAGE_SIZE)
			{
				/*
				 * PRP2 will contain a PRP List pointer because
				 * more PRP's are needed with this command. The
				 * list will start at the beginning of the
				 * contiguous buffer.
				 */
				*prp2_entry =
				    htole64(
				    (uint64_t)(uintptr_t)prp_entry_phys);

				/*
				 * The next PRP Entry will be the start of the
				 * first PRP List.
				 */
				prp_entry = prp_page;
			}
			else
			{
				/*
				 * After this, the PRP Entries are complete.
				 * This command uses 2 PRP's and no PRP list.
				 */
				*prp2_entry = htole64((uint64_t)paddr);
			}
		}
		else
		{
			/*
			 * Put entry in list and bump the addresses.
			 *
			 * After PRP1 and PRP2 are filled in, this will fill in
			 * all remaining PRP entries in a PRP List, one per each
			 * time through the loop.
			 */
			*prp_entry = htole64((uint64_t)paddr);
			prp_entry++;
			prp_entry_phys++;
		}

		/*
		 * Bump the phys address of the command's data buffer by the
		 * entry_len.
		 */
		paddr += entry_len;

		/* Decrement length accounting for last partial page. */
		if (entry_len > length)
			length = 0;
		else
			length -= entry_len;
	}
}

/*
 * mpr_check_pcie_native_sgl - This function is called for PCIe end devices to
 * determine if the driver needs to build a native SGL. If so, that native SGL
 * is built in the contiguous buffers allocated especially for PCIe SGL
 * creation. If the driver will not build a native SGL, return TRUE and a
 * normal IEEE SGL will be built. Currently this routine supports NVMe devices
 * only.
 *
 * Returns FALSE (0) if native SGL was built, TRUE (1) if no SGL was built.
 */
static int
mpr_check_pcie_native_sgl(struct mpr_softc *sc, struct mpr_command *cm,
    bus_dma_segment_t *segs, int segs_left)
{
	uint32_t		i, sge_dwords, length, offset, entry_len;
	uint32_t		num_entries, buff_len = 0, sges_in_segment;
	uint32_t		page_mask, page_mask_result, *curr_buff;
	uint32_t		*ptr_sgl, *ptr_first_sgl, first_page_offset;
	uint32_t		first_page_data_size, end_residual;
	uint64_t		*msg_phys;
	bus_addr_t		paddr;
	int			build_native_sgl = 0, first_prp_entry;
	int			prp_size = PRP_ENTRY_SIZE;
	Mpi25IeeeSgeChain64_t	*main_chain_element = NULL;
	struct mpr_prp_page	*prp_page_info = NULL;

	mpr_dprint(sc, MPR_TRACE, "%s\n", __func__);

	/*
	 * Add up the sizes of each segment length to get the total transfer
	 * size, which will be checked against the Maximum Data Transfer Size.
	 * If the data transfer length exceeds the MDTS for this device, just
	 * return 1 so a normal IEEE SGL will be built. F/W will break the I/O
	 * up into multiple I/O's. [nvme_mdts = 0 means unlimited]
	 */
	for (i = 0; i < segs_left; i++)
		buff_len += htole32(segs[i].ds_len);
	if ((cm->cm_targ->MDTS > 0) && (buff_len > cm->cm_targ->MDTS))
		return 1;

	/* Create page_mask (to get offset within page) */
	page_mask = PAGE_SIZE - 1;

	/*
	 * Check if the number of elements exceeds the max number that can be
	 * put in the main message frame (H/W can only translate an SGL that
	 * is contained entirely in the main message frame).
	 */
	sges_in_segment = (sc->reqframesz -
	    offsetof(Mpi25SCSIIORequest_t, SGL)) / sizeof(MPI25_SGE_IO_UNION);
	if (segs_left > sges_in_segment)
		build_native_sgl = 1;
	else
	{
		/*
		 * NVMe uses one PRP for each physical page (or part of physical
		 * page).
		 *    if 4 pages or less then IEEE is OK
		 *    if > 5 pages then we need to build a native SGL
		 *    if > 4 and <= 5 pages, then check the physical address of
		 *      the first SG entry, then if this first size in the page
		 *      is >= the residual beyond 4 pages then use IEEE,
		 *      otherwise use native SGL
		 */
		if (buff_len > (PAGE_SIZE * 5))
			build_native_sgl = 1;
		else if ((buff_len > (PAGE_SIZE * 4)) &&
		    (buff_len <= (PAGE_SIZE * 5)) )
		{
			msg_phys = (uint64_t *)(uintptr_t)segs[0].ds_addr;
			first_page_offset =
			    ((uint32_t)(uint64_t)(uintptr_t)msg_phys &
			    page_mask);
			first_page_data_size = PAGE_SIZE - first_page_offset;
			end_residual = buff_len % PAGE_SIZE;

			/*
			 * If offset into first page pushes the end of the data
			 * beyond end of the 5th page, we need the extra PRP
			 * list.
			 */
			if (first_page_data_size < end_residual)
				build_native_sgl = 1;

			/*
			 * Check if first SG entry size is < residual beyond 4
			 * pages.
			 */
			if (htole32(segs[0].ds_len) <
			    (buff_len - (PAGE_SIZE * 4)))
				build_native_sgl = 1;
		}
	}

	/* check if native SGL is needed */
	if (!build_native_sgl)
		return 1;

	/*
	 * Native SGL is needed.
	 * Put a chain element in main message frame that points to the first
	 * chain buffer.
	 *
	 * NOTE:  The ChainOffset field must be 0 when using a chain pointer to
	 *        a native SGL.
	 */

	/* Set main message chain element pointer */
	main_chain_element = (pMpi25IeeeSgeChain64_t)cm->cm_sge;

	/*
	 * For NVMe the chain element needs to be the 2nd SGL entry in the main
	 * message.
	 */
	main_chain_element = (Mpi25IeeeSgeChain64_t *)
	    ((uint8_t *)main_chain_element + sizeof(MPI25_IEEE_SGE_CHAIN64));

	/*
	 * For the PRP entries, use the specially allocated buffer of
	 * contiguous memory. PRP Page allocation failures should not happen
	 * because there should be enough PRP page buffers to account for the
	 * possible NVMe QDepth.
	 */
	prp_page_info = mpr_alloc_prp_page(sc);
	KASSERT(prp_page_info != NULL, ("%s: There are no PRP Pages left to be "
	    "used for building a native NVMe SGL.\n", __func__));
	curr_buff = (uint32_t *)prp_page_info->prp_page;
	msg_phys = (uint64_t *)(uintptr_t)prp_page_info->prp_page_busaddr;

	/*
	 * Insert the allocated PRP page into the command's PRP page list. This
	 * will be freed when the command is freed.
	 */
	TAILQ_INSERT_TAIL(&cm->cm_prp_page_list, prp_page_info, prp_page_link);

	/*
	 * Check if we are within 1 entry of a page boundary we don't want our
	 * first entry to be a PRP List entry.
	 */
	page_mask_result = (uintptr_t)((uint8_t *)curr_buff + prp_size) &
	    page_mask;
	if (!page_mask_result) {
		/* Bump up to next page boundary. */
		curr_buff = (uint32_t *)((uint8_t *)curr_buff + prp_size);
		msg_phys = (uint64_t *)((uint8_t *)msg_phys + prp_size);
	}

	/* Fill in the chain element and make it an NVMe segment type. */
	main_chain_element->Address.High =
	    htole32((uint32_t)((uint64_t)(uintptr_t)msg_phys >> 32));
	main_chain_element->Address.Low =
	    htole32((uint32_t)(uintptr_t)msg_phys);
	main_chain_element->NextChainOffset = 0;
	main_chain_element->Flags = MPI2_IEEE_SGE_FLAGS_CHAIN_ELEMENT |
	    MPI2_IEEE_SGE_FLAGS_SYSTEM_ADDR |
	    MPI26_IEEE_SGE_FLAGS_NSF_NVME_PRP;

	/* Set SGL pointer to start of contiguous PCIe buffer. */
	ptr_sgl = curr_buff;
	sge_dwords = 2;
	num_entries = 0;

	/*
	 * NVMe has a very convoluted PRP format. One PRP is required for each
	 * page or partial page. We need to split up OS SG entries if they are
	 * longer than one page or cross a page boundary. We also have to insert
	 * a PRP list pointer entry as the last entry in each physical page of
	 * the PRP list.
	 *
	 * NOTE: The first PRP "entry" is actually placed in the first SGL entry
	 * in the main message in IEEE 64 format. The 2nd entry in the main
	 * message is the chain element, and the rest of the PRP entries are
	 * built in the contiguous PCIe buffer.
	 */
	first_prp_entry = 1;
	ptr_first_sgl = (uint32_t *)cm->cm_sge;

	for (i = 0; i < segs_left; i++) {
		/* Get physical address and length of this SG entry. */
		paddr = segs[i].ds_addr;
		length = segs[i].ds_len;

		/*
		 * Check whether a given SGE buffer lies on a non-PAGED
		 * boundary if this is not the first page. If so, this is not
		 * expected so have FW build the SGL.
		 */
		if ((i != 0) && (((uint32_t)paddr & page_mask) != 0)) {
			mpr_dprint(sc, MPR_ERROR, "Unaligned SGE while "
			    "building NVMe PRPs, low address is 0x%x\n",
			    (uint32_t)paddr);
			return 1;
		}

		/* Apart from last SGE, if any other SGE boundary is not page
		 * aligned then it means that hole exists. Existence of hole
		 * leads to data corruption. So fallback to IEEE SGEs.
		 */
		if (i != (segs_left - 1)) {
			if (((uint32_t)paddr + length) & page_mask) {
				mpr_dprint(sc, MPR_ERROR, "Unaligned SGE "
				    "boundary while building NVMe PRPs, low "
				    "address: 0x%x and length: %u\n",
				    (uint32_t)paddr, length);
				return 1;
			}
		}

		/* Loop while the length is not zero. */
		while (length) {
			/*
			 * Check if we need to put a list pointer here if we are
			 * at page boundary - prp_size.
			 */
			page_mask_result = (uintptr_t)((uint8_t *)ptr_sgl +
			    prp_size) & page_mask;
			if (!page_mask_result) {
				/*
				 * Need to put a PRP list pointer here.
				 */
				msg_phys = (uint64_t *)((uint8_t *)msg_phys +
				    prp_size);
				*ptr_sgl = htole32((uintptr_t)msg_phys);
				*(ptr_sgl+1) = htole32((uint64_t)(uintptr_t)
				    msg_phys >> 32);
				ptr_sgl += sge_dwords;
				num_entries++;
			}

			/* Need to handle if entry will be part of a page. */
			offset = (uint32_t)paddr & page_mask;
			entry_len = PAGE_SIZE - offset;
			if (first_prp_entry) {
				/*
				 * Put IEEE entry in first SGE in main message.
				 * (Simple element, System addr, not end of
				 * list.)
				 */
				*ptr_first_sgl = htole32((uint32_t)paddr);
				*(ptr_first_sgl + 1) =
				    htole32((uint32_t)((uint64_t)paddr >> 32));
				*(ptr_first_sgl + 2) = htole32(entry_len);
				*(ptr_first_sgl + 3) = 0;

				/* No longer the first PRP entry. */
				first_prp_entry = 0;
			} else {
				/* Put entry in list. */
				*ptr_sgl = htole32((uint32_t)paddr);
				*(ptr_sgl + 1) =
				    htole32((uint32_t)((uint64_t)paddr >> 32));

				/* Bump ptr_sgl, msg_phys, and num_entries. */
				ptr_sgl += sge_dwords;
				msg_phys = (uint64_t *)((uint8_t *)msg_phys +
				    prp_size);
				num_entries++;
			}

			/* Bump the phys address by the entry_len. */
			paddr += entry_len;

			/* Decrement length accounting for last partial page. */
			if (entry_len > length)
				length = 0;
			else
				length -= entry_len;
		}
	}

	/* Set chain element Length. */
	main_chain_element->Length = htole32(num_entries * prp_size);

	/* Return 0, indicating we built a native SGL. */
	return 0;
}

/*
 * Add a chain element as the next SGE for the specified command.
 * Reset cm_sge and cm_sgesize to indicate all the available space. Chains are
 * only required for IEEE commands.  Therefore there is no code for commands
 * that have the MPR_CM_FLAGS_SGE_SIMPLE flag set (and those commands
 * shouldn't be requesting chains).
 */
static int
mpr_add_chain(struct mpr_command *cm, int segsleft)
{
	struct mpr_softc *sc = cm->cm_sc;
	MPI2_REQUEST_HEADER *req;
	MPI25_IEEE_SGE_CHAIN64 *ieee_sgc;
	struct mpr_chain *chain;
	int sgc_size, current_segs, rem_segs, segs_per_frame;
	uint8_t next_chain_offset = 0;

	/*
	 * Fail if a command is requesting a chain for SIMPLE SGE's.  For SAS3
	 * only IEEE commands should be requesting chains.  Return some error
	 * code other than 0.
	 */
	if (cm->cm_flags & MPR_CM_FLAGS_SGE_SIMPLE) {
		mpr_dprint(sc, MPR_ERROR, "A chain element cannot be added to "
		    "an MPI SGL.\n");
		return(ENOBUFS);
	}

	sgc_size = sizeof(MPI25_IEEE_SGE_CHAIN64);
	if (cm->cm_sglsize < sgc_size)
		panic("MPR: Need SGE Error Code\n");

	chain = mpr_alloc_chain(cm->cm_sc);
	if (chain == NULL)
		return (ENOBUFS);

	/*
	 * Note: a double-linked list is used to make it easier to walk for
	 * debugging.
	 */
	TAILQ_INSERT_TAIL(&cm->cm_chain_list, chain, chain_link);

	/*
	 * Need to know if the number of frames left is more than 1 or not.  If
	 * more than 1 frame is required, NextChainOffset will need to be set,
	 * which will just be the last segment of the frame.
	 */
	rem_segs = 0;
	if (cm->cm_sglsize < (sgc_size * segsleft)) {
		/*
		 * rem_segs is the number of segements remaining after the
		 * segments that will go into the current frame.  Since it is
		 * known that at least one more frame is required, account for
		 * the chain element.  To know if more than one more frame is
		 * required, just check if there will be a remainder after using
		 * the current frame (with this chain) and the next frame.  If
		 * so the NextChainOffset must be the last element of the next
		 * frame.
		 */
		current_segs = (cm->cm_sglsize / sgc_size) - 1;
		rem_segs = segsleft - current_segs;
		segs_per_frame = sc->chain_frame_size / sgc_size;
		if (rem_segs > segs_per_frame) {
			next_chain_offset = segs_per_frame - 1;
		}
	}
	ieee_sgc = &((MPI25_SGE_IO_UNION *)cm->cm_sge)->IeeeChain;
	ieee_sgc->Length = next_chain_offset ?
	    htole32((uint32_t)sc->chain_frame_size) :
	    htole32((uint32_t)rem_segs * (uint32_t)sgc_size);
	ieee_sgc->NextChainOffset = next_chain_offset;
	ieee_sgc->Flags = (MPI2_IEEE_SGE_FLAGS_CHAIN_ELEMENT |
	    MPI2_IEEE_SGE_FLAGS_SYSTEM_ADDR);
	ieee_sgc->Address.Low = htole32(chain->chain_busaddr);
	ieee_sgc->Address.High = htole32(chain->chain_busaddr >> 32);
	cm->cm_sge = &((MPI25_SGE_IO_UNION *)chain->chain)->IeeeSimple;
	req = (MPI2_REQUEST_HEADER *)cm->cm_req;
	req->ChainOffset = (sc->chain_frame_size - sgc_size) >> 4;

	cm->cm_sglsize = sc->chain_frame_size;
	return (0);
}

/*
 * Add one scatter-gather element to the scatter-gather list for a command.
 * Maintain cm_sglsize and cm_sge as the remaining size and pointer to the
 * next SGE to fill in, respectively.  In Gen3, the MPI SGL does not have a
 * chain, so don't consider any chain additions.
 */
int
mpr_push_sge(struct mpr_command *cm, MPI2_SGE_SIMPLE64 *sge, size_t len,
    int segsleft)
{
	uint32_t saved_buf_len, saved_address_low, saved_address_high;
	u32 sge_flags;

	/*
	 * case 1: >=1 more segment, no room for anything (error)
	 * case 2: 1 more segment and enough room for it
         */

	if (cm->cm_sglsize < (segsleft * sizeof(MPI2_SGE_SIMPLE64))) {
		mpr_dprint(cm->cm_sc, MPR_ERROR,
		    "%s: warning: Not enough room for MPI SGL in frame.\n",
		    __func__);
		return(ENOBUFS);
	}

	KASSERT(segsleft == 1,
	    ("segsleft cannot be more than 1 for an MPI SGL; segsleft = %d\n",
	    segsleft));

	/*
	 * There is one more segment left to add for the MPI SGL and there is
	 * enough room in the frame to add it.  This is the normal case because
	 * MPI SGL's don't have chains, otherwise something is wrong.
	 *
	 * If this is a bi-directional request, need to account for that
	 * here.  Save the pre-filled sge values.  These will be used
	 * either for the 2nd SGL or for a single direction SGL.  If
	 * cm_out_len is non-zero, this is a bi-directional request, so
	 * fill in the OUT SGL first, then the IN SGL, otherwise just
	 * fill in the IN SGL.  Note that at this time, when filling in
	 * 2 SGL's for a bi-directional request, they both use the same
	 * DMA buffer (same cm command).
	 */
	saved_buf_len = sge->FlagsLength & 0x00FFFFFF;
	saved_address_low = sge->Address.Low;
	saved_address_high = sge->Address.High;
	if (cm->cm_out_len) {
		sge->FlagsLength = cm->cm_out_len |
		    ((uint32_t)(MPI2_SGE_FLAGS_SIMPLE_ELEMENT |
		    MPI2_SGE_FLAGS_END_OF_BUFFER |
		    MPI2_SGE_FLAGS_HOST_TO_IOC |
		    MPI2_SGE_FLAGS_64_BIT_ADDRESSING) <<
		    MPI2_SGE_FLAGS_SHIFT);
		cm->cm_sglsize -= len;
		/* Endian Safe code */
		sge_flags = sge->FlagsLength;
		sge->FlagsLength = htole32(sge_flags);
		sge->Address.High = htole32(sge->Address.High);	
		sge->Address.Low = htole32(sge->Address.Low);
		bcopy(sge, cm->cm_sge, len);
		cm->cm_sge = (MPI2_SGE_IO_UNION *)((uintptr_t)cm->cm_sge + len);
	}
	sge->FlagsLength = saved_buf_len |
	    ((uint32_t)(MPI2_SGE_FLAGS_SIMPLE_ELEMENT |
	    MPI2_SGE_FLAGS_END_OF_BUFFER |
	    MPI2_SGE_FLAGS_LAST_ELEMENT |
	    MPI2_SGE_FLAGS_END_OF_LIST |
	    MPI2_SGE_FLAGS_64_BIT_ADDRESSING) <<
	    MPI2_SGE_FLAGS_SHIFT);
	if (cm->cm_flags & MPR_CM_FLAGS_DATAIN) {
		sge->FlagsLength |=
		    ((uint32_t)(MPI2_SGE_FLAGS_IOC_TO_HOST) <<
		    MPI2_SGE_FLAGS_SHIFT);
	} else {
		sge->FlagsLength |=
		    ((uint32_t)(MPI2_SGE_FLAGS_HOST_TO_IOC) <<
		    MPI2_SGE_FLAGS_SHIFT);
	}
	sge->Address.Low = saved_address_low;
	sge->Address.High = saved_address_high;

	cm->cm_sglsize -= len;
	/* Endian Safe code */
	sge_flags = sge->FlagsLength;
	sge->FlagsLength = htole32(sge_flags);
	sge->Address.High = htole32(sge->Address.High);	
	sge->Address.Low = htole32(sge->Address.Low);
	bcopy(sge, cm->cm_sge, len);
	cm->cm_sge = (MPI2_SGE_IO_UNION *)((uintptr_t)cm->cm_sge + len);
	return (0);
}

/*
 * Add one IEEE scatter-gather element (chain or simple) to the IEEE scatter-
 * gather list for a command.  Maintain cm_sglsize and cm_sge as the
 * remaining size and pointer to the next SGE to fill in, respectively.
 */
int
mpr_push_ieee_sge(struct mpr_command *cm, void *sgep, int segsleft)
{
	MPI2_IEEE_SGE_SIMPLE64 *sge = sgep;
	int error, ieee_sge_size = sizeof(MPI25_SGE_IO_UNION);
	uint32_t saved_buf_len, saved_address_low, saved_address_high;
	uint32_t sge_length;

	/*
	 * case 1: No room for chain or segment (error).
	 * case 2: Two or more segments left but only room for chain.
	 * case 3: Last segment and room for it, so set flags.
	 */

	/*
	 * There should be room for at least one element, or there is a big
	 * problem.
	 */
	if (cm->cm_sglsize < ieee_sge_size)
		panic("MPR: Need SGE Error Code\n");

	if ((segsleft >= 2) && (cm->cm_sglsize < (ieee_sge_size * 2))) {
		if ((error = mpr_add_chain(cm, segsleft)) != 0)
			return (error);
	}

	if (segsleft == 1) {
		/*
		 * If this is a bi-directional request, need to account for that
		 * here.  Save the pre-filled sge values.  These will be used
		 * either for the 2nd SGL or for a single direction SGL.  If
		 * cm_out_len is non-zero, this is a bi-directional request, so
		 * fill in the OUT SGL first, then the IN SGL, otherwise just
		 * fill in the IN SGL.  Note that at this time, when filling in
		 * 2 SGL's for a bi-directional request, they both use the same
		 * DMA buffer (same cm command).
		 */
		saved_buf_len = sge->Length;
		saved_address_low = sge->Address.Low;
		saved_address_high = sge->Address.High;
		if (cm->cm_out_len) {
			sge->Length = cm->cm_out_len;
			sge->Flags = (MPI2_IEEE_SGE_FLAGS_SIMPLE_ELEMENT |
			    MPI2_IEEE_SGE_FLAGS_SYSTEM_ADDR);
			cm->cm_sglsize -= ieee_sge_size;
			/* Endian Safe code */
			sge_length = sge->Length;
			sge->Length = htole32(sge_length);
			sge->Address.High = htole32(sge->Address.High);	
			sge->Address.Low = htole32(sge->Address.Low);
			bcopy(sgep, cm->cm_sge, ieee_sge_size);
			cm->cm_sge =
			    (MPI25_SGE_IO_UNION *)((uintptr_t)cm->cm_sge +
			    ieee_sge_size);
		}
		sge->Length = saved_buf_len;
		sge->Flags = (MPI2_IEEE_SGE_FLAGS_SIMPLE_ELEMENT |
		    MPI2_IEEE_SGE_FLAGS_SYSTEM_ADDR |
		    MPI25_IEEE_SGE_FLAGS_END_OF_LIST);
		sge->Address.Low = saved_address_low;
		sge->Address.High = saved_address_high;
	}

	cm->cm_sglsize -= ieee_sge_size;
	/* Endian Safe code */
	sge_length = sge->Length;
	sge->Length = htole32(sge_length);
	sge->Address.High = htole32(sge->Address.High);	
	sge->Address.Low = htole32(sge->Address.Low);
	bcopy(sgep, cm->cm_sge, ieee_sge_size);
	cm->cm_sge = (MPI25_SGE_IO_UNION *)((uintptr_t)cm->cm_sge +
	    ieee_sge_size);
	return (0);
}

/*
 * Add one dma segment to the scatter-gather list for a command.
 */
int
mpr_add_dmaseg(struct mpr_command *cm, vm_paddr_t pa, size_t len, u_int flags,
    int segsleft)
{
	MPI2_SGE_SIMPLE64 sge;
	MPI2_IEEE_SGE_SIMPLE64 ieee_sge;

	if (!(cm->cm_flags & MPR_CM_FLAGS_SGE_SIMPLE)) {
		ieee_sge.Flags = (MPI2_IEEE_SGE_FLAGS_SIMPLE_ELEMENT |
		    MPI2_IEEE_SGE_FLAGS_SYSTEM_ADDR);
		ieee_sge.Length = len;
		mpr_from_u64(pa, &ieee_sge.Address);

		return (mpr_push_ieee_sge(cm, &ieee_sge, segsleft));
	} else {
		/*
		 * This driver always uses 64-bit address elements for
		 * simplicity.
		 */
		flags |= MPI2_SGE_FLAGS_SIMPLE_ELEMENT |
		    MPI2_SGE_FLAGS_64_BIT_ADDRESSING;
		/* Set Endian safe macro in mpr_push_sge */
		sge.FlagsLength = len | (flags << MPI2_SGE_FLAGS_SHIFT);
		mpr_from_u64(pa, &sge.Address);

		return (mpr_push_sge(cm, &sge, sizeof sge, segsleft));
	}
}

static void
mpr_data_cb(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	struct mpr_softc *sc;
	struct mpr_command *cm;
	u_int i, dir, sflags;

	cm = (struct mpr_command *)arg;
	sc = cm->cm_sc;

	/*
	 * In this case, just print out a warning and let the chip tell the
	 * user they did the wrong thing.
	 */
	if ((cm->cm_max_segs != 0) && (nsegs > cm->cm_max_segs)) {
		mpr_dprint(sc, MPR_ERROR, "%s: warning: busdma returned %d "
		    "segments, more than the %d allowed\n", __func__, nsegs,
		    cm->cm_max_segs);
	}

	/*
	 * Set up DMA direction flags.  Bi-directional requests are also handled
	 * here.  In that case, both direction flags will be set.
	 */
	sflags = 0;
	if (cm->cm_flags & MPR_CM_FLAGS_SMP_PASS) {
		/*
		 * We have to add a special case for SMP passthrough, there
		 * is no easy way to generically handle it.  The first
		 * S/G element is used for the command (therefore the
		 * direction bit needs to be set).  The second one is used
		 * for the reply.  We'll leave it to the caller to make
		 * sure we only have two buffers.
		 */
		/*
		 * Even though the busdma man page says it doesn't make
		 * sense to have both direction flags, it does in this case.
		 * We have one s/g element being accessed in each direction.
		 */
		dir = BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD;

		/*
		 * Set the direction flag on the first buffer in the SMP
		 * passthrough request.  We'll clear it for the second one.
		 */
		sflags |= MPI2_SGE_FLAGS_DIRECTION |
			  MPI2_SGE_FLAGS_END_OF_BUFFER;
	} else if (cm->cm_flags & MPR_CM_FLAGS_DATAOUT) {
		sflags |= MPI2_SGE_FLAGS_HOST_TO_IOC;
		dir = BUS_DMASYNC_PREWRITE;
	} else
		dir = BUS_DMASYNC_PREREAD;

	/* Check if a native SG list is needed for an NVMe PCIe device. */
	if (cm->cm_targ && cm->cm_targ->is_nvme &&
	    mpr_check_pcie_native_sgl(sc, cm, segs, nsegs) == 0) {
		/* A native SG list was built, skip to end. */
		goto out;
	}

	for (i = 0; i < nsegs; i++) {
		if ((cm->cm_flags & MPR_CM_FLAGS_SMP_PASS) && (i != 0)) {
			sflags &= ~MPI2_SGE_FLAGS_DIRECTION;
		}
		error = mpr_add_dmaseg(cm, segs[i].ds_addr, segs[i].ds_len,
		    sflags, nsegs - i);
		if (error != 0) {
			/* Resource shortage, roll back! */
			if (ratecheck(&sc->lastfail, &mpr_chainfail_interval))
				mpr_dprint(sc, MPR_INFO, "Out of chain frames, "
				    "consider increasing hw.mpr.max_chains.\n");
			cm->cm_flags |= MPR_CM_FLAGS_CHAIN_FAILED;
			mpr_complete_command(sc, cm);
			return;
		}
	}

out:
	bus_dmamap_sync(sc->buffer_dmat, cm->cm_dmamap, dir);
	mpr_enqueue_request(sc, cm);

	return;
}

static void
mpr_data_cb2(void *arg, bus_dma_segment_t *segs, int nsegs, bus_size_t mapsize,
	     int error)
{
	mpr_data_cb(arg, segs, nsegs, error);
}

/*
 * This is the routine to enqueue commands ansynchronously.
 * Note that the only error path here is from bus_dmamap_load(), which can
 * return EINPROGRESS if it is waiting for resources.  Other than this, it's
 * assumed that if you have a command in-hand, then you have enough credits
 * to use it.
 */
int
mpr_map_command(struct mpr_softc *sc, struct mpr_command *cm)
{
	int error = 0;

	if (cm->cm_flags & MPR_CM_FLAGS_USE_UIO) {
		error = bus_dmamap_load_uio(sc->buffer_dmat, cm->cm_dmamap,
		    &cm->cm_uio, mpr_data_cb2, cm, 0);
	} else if (cm->cm_flags & MPR_CM_FLAGS_USE_CCB) {
		error = bus_dmamap_load_ccb(sc->buffer_dmat, cm->cm_dmamap,
		    cm->cm_data, mpr_data_cb, cm, 0);
	} else if ((cm->cm_data != NULL) && (cm->cm_length != 0)) {
		error = bus_dmamap_load(sc->buffer_dmat, cm->cm_dmamap,
		    cm->cm_data, cm->cm_length, mpr_data_cb, cm, 0);
	} else {
		/* Add a zero-length element as needed */
		if (cm->cm_sge != NULL)
			mpr_add_dmaseg(cm, 0, 0, 0, 1);
		mpr_enqueue_request(sc, cm);
	}

	return (error);
}

/*
 * This is the routine to enqueue commands synchronously.  An error of
 * EINPROGRESS from mpr_map_command() is ignored since the command will
 * be executed and enqueued automatically.  Other errors come from msleep().
 */
int
mpr_wait_command(struct mpr_softc *sc, struct mpr_command **cmp, int timeout,
    int sleep_flag)
{
	int error, rc;
	struct timeval cur_time, start_time;
	struct mpr_command *cm = *cmp;

	if (sc->mpr_flags & MPR_FLAGS_DIAGRESET) 
		return  EBUSY;

	cm->cm_complete = NULL;
	cm->cm_flags |= (MPR_CM_FLAGS_WAKEUP + MPR_CM_FLAGS_POLLED);
	error = mpr_map_command(sc, cm);
	if ((error != 0) && (error != EINPROGRESS))
		return (error);

	// Check for context and wait for 50 mSec at a time until time has
	// expired or the command has finished.  If msleep can't be used, need
	// to poll.
#if __FreeBSD_version >= 1000029
	if (curthread->td_no_sleeping)
#else //__FreeBSD_version < 1000029
	if (curthread->td_pflags & TDP_NOSLEEPING)
#endif //__FreeBSD_version >= 1000029
		sleep_flag = NO_SLEEP;
	getmicrouptime(&start_time);
	if (mtx_owned(&sc->mpr_mtx) && sleep_flag == CAN_SLEEP) {
		error = msleep(cm, &sc->mpr_mtx, 0, "mprwait", timeout*hz);
		if (error == EWOULDBLOCK) {
			/*
			 * Record the actual elapsed time in the case of a
			 * timeout for the message below.
			 */
			getmicrouptime(&cur_time);
			timevalsub(&cur_time, &start_time);
		}
	} else {
		while ((cm->cm_flags & MPR_CM_FLAGS_COMPLETE) == 0) {
			mpr_intr_locked(sc);
			if (sleep_flag == CAN_SLEEP)
				pause("mprwait", hz/20);
			else
				DELAY(50000);
		
			getmicrouptime(&cur_time);
			timevalsub(&cur_time, &start_time);
			if (cur_time.tv_sec > timeout) {
				error = EWOULDBLOCK;
				break;
			}
		}
	}

	if (error == EWOULDBLOCK) {
		if (cm->cm_timeout_handler == NULL) {
			mpr_dprint(sc, MPR_FAULT, "Calling Reinit from %s, timeout=%d,"
			    " elapsed=%jd\n", __func__, timeout,
			    (intmax_t)cur_time.tv_sec);
			rc = mpr_reinit(sc);
			mpr_dprint(sc, MPR_FAULT, "Reinit %s\n", (rc == 0) ? "success" :
			    "failed");
		} else
			cm->cm_timeout_handler(sc, cm);
		if (sc->mpr_flags & MPR_FLAGS_REALLOCATED) {
			/*
			 * Tell the caller that we freed the command in a
			 * reinit.
			 */
			*cmp = NULL;
		}
		error = ETIMEDOUT;
	}
	return (error);
}

/*
 * This is the routine to enqueue a command synchonously and poll for
 * completion.  Its use should be rare.
 */
int
mpr_request_polled(struct mpr_softc *sc, struct mpr_command **cmp)
{
	int error, rc;
	struct timeval cur_time, start_time;
	struct mpr_command *cm = *cmp;

	error = 0;

	cm->cm_flags |= MPR_CM_FLAGS_POLLED;
	cm->cm_complete = NULL;
	mpr_map_command(sc, cm);

	getmicrouptime(&start_time);
	while ((cm->cm_flags & MPR_CM_FLAGS_COMPLETE) == 0) {
		mpr_intr_locked(sc);

		if (mtx_owned(&sc->mpr_mtx))
			msleep(&sc->msleep_fake_chan, &sc->mpr_mtx, 0,
			    "mprpoll", hz/20);
		else
			pause("mprpoll", hz/20);

		/*
		 * Check for real-time timeout and fail if more than 60 seconds.
		 */
		getmicrouptime(&cur_time);
		timevalsub(&cur_time, &start_time);
		if (cur_time.tv_sec > 60) {
			mpr_dprint(sc, MPR_FAULT, "polling failed\n");
			error = ETIMEDOUT;
			break;
		}
	}

	if (error) {
		mpr_dprint(sc, MPR_FAULT, "Calling Reinit from %s\n", __func__);
		rc = mpr_reinit(sc);
		mpr_dprint(sc, MPR_FAULT, "Reinit %s\n", (rc == 0) ? "success" :
		    "failed");

		if (sc->mpr_flags & MPR_FLAGS_REALLOCATED) {
			/*
			 * Tell the caller that we freed the command in a
			 * reinit.
			 */
			*cmp = NULL;
		}
	}
	return (error);
}

/*
 * The MPT driver had a verbose interface for config pages.  In this driver,
 * reduce it to much simpler terms, similar to the Linux driver.
 */
int
mpr_read_config_page(struct mpr_softc *sc, struct mpr_config_params *params)
{
	MPI2_CONFIG_REQUEST *req;
	struct mpr_command *cm;
	int error;

	if (sc->mpr_flags & MPR_FLAGS_BUSY) {
		return (EBUSY);
	}

	cm = mpr_alloc_command(sc);
	if (cm == NULL) {
		return (EBUSY);
	}

	req = (MPI2_CONFIG_REQUEST *)cm->cm_req;
	req->Function = MPI2_FUNCTION_CONFIG;
	req->Action = params->action;
	req->SGLFlags = 0;
	req->ChainOffset = 0;
	req->PageAddress = params->page_address;
	if (params->hdr.Struct.PageType == MPI2_CONFIG_PAGETYPE_EXTENDED) {
		MPI2_CONFIG_EXTENDED_PAGE_HEADER *hdr;

		hdr = &params->hdr.Ext;
		req->ExtPageType = hdr->ExtPageType;
		req->ExtPageLength = hdr->ExtPageLength;
		req->Header.PageType = MPI2_CONFIG_PAGETYPE_EXTENDED;
		req->Header.PageLength = 0; /* Must be set to zero */
		req->Header.PageNumber = hdr->PageNumber;
		req->Header.PageVersion = hdr->PageVersion;
	} else {
		MPI2_CONFIG_PAGE_HEADER *hdr;

		hdr = &params->hdr.Struct;
		req->Header.PageType = hdr->PageType;
		req->Header.PageNumber = hdr->PageNumber;
		req->Header.PageLength = hdr->PageLength;
		req->Header.PageVersion = hdr->PageVersion;
	}

	cm->cm_data = params->buffer;
	cm->cm_length = params->length;
	if (cm->cm_data != NULL) {
		cm->cm_sge = &req->PageBufferSGE;
		cm->cm_sglsize = sizeof(MPI2_SGE_IO_UNION);
		cm->cm_flags = MPR_CM_FLAGS_SGE_SIMPLE | MPR_CM_FLAGS_DATAIN;
	} else
		cm->cm_sge = NULL;
	cm->cm_desc.Default.RequestFlags = MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE;

	cm->cm_complete_data = params;
	if (params->callback != NULL) {
		cm->cm_complete = mpr_config_complete;
		return (mpr_map_command(sc, cm));
	} else {
		error = mpr_wait_command(sc, &cm, 0, CAN_SLEEP);
		if (error) {
			mpr_dprint(sc, MPR_FAULT,
			    "Error %d reading config page\n", error);
			if (cm != NULL)
				mpr_free_command(sc, cm);
			return (error);
		}
		mpr_config_complete(sc, cm);
	}

	return (0);
}

int
mpr_write_config_page(struct mpr_softc *sc, struct mpr_config_params *params)
{
	return (EINVAL);
}

static void
mpr_config_complete(struct mpr_softc *sc, struct mpr_command *cm)
{
	MPI2_CONFIG_REPLY *reply;
	struct mpr_config_params *params;

	MPR_FUNCTRACE(sc);
	params = cm->cm_complete_data;

	if (cm->cm_data != NULL) {
		bus_dmamap_sync(sc->buffer_dmat, cm->cm_dmamap,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->buffer_dmat, cm->cm_dmamap);
	}

	/*
	 * XXX KDM need to do more error recovery?  This results in the
	 * device in question not getting probed.
	 */
	if ((cm->cm_flags & MPR_CM_FLAGS_ERROR_MASK) != 0) {
		params->status = MPI2_IOCSTATUS_BUSY;
		goto done;
	}

	reply = (MPI2_CONFIG_REPLY *)cm->cm_reply;
	if (reply == NULL) {
		params->status = MPI2_IOCSTATUS_BUSY;
		goto done;
	}
	params->status = reply->IOCStatus;
	if (params->hdr.Struct.PageType == MPI2_CONFIG_PAGETYPE_EXTENDED) {
		params->hdr.Ext.ExtPageType = reply->ExtPageType;
		params->hdr.Ext.ExtPageLength = reply->ExtPageLength;
		params->hdr.Ext.PageType = reply->Header.PageType;
		params->hdr.Ext.PageNumber = reply->Header.PageNumber;
		params->hdr.Ext.PageVersion = reply->Header.PageVersion;
	} else {
		params->hdr.Struct.PageType = reply->Header.PageType;
		params->hdr.Struct.PageNumber = reply->Header.PageNumber;
		params->hdr.Struct.PageLength = reply->Header.PageLength;
		params->hdr.Struct.PageVersion = reply->Header.PageVersion;
	}

done:
	mpr_free_command(sc, cm);
	if (params->callback != NULL)
		params->callback(sc, params);

	return;
}
