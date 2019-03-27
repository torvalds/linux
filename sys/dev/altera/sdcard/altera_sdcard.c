/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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

#include "opt_altera_sdcard.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/condvar.h>
#include <sys/conf.h>
#include <sys/bio.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/systm.h>
#include <sys/taskqueue.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <geom/geom_disk.h>

#include <dev/altera/sdcard/altera_sdcard.h>

/*
 * Device driver for the Altera University Program Secure Data Card IP Core,
 * as described in the similarly named SOPC Builder IP Core specification.
 * This soft core is not a full SD host controller interface (SDHCI) but
 * instead provides a set of memory mapped registers and memory buffer that
 * mildly abstract the SD Card protocol, but without providing DMA or
 * interrupts.  However, it does hide the details of voltage and
 * communications negotiation.  This driver implements disk(9), but due to the
 * lack of interrupt support, must rely on timer-driven polling to determine
 * when I/Os have completed.
 *
 * TODO:
 *
 * 1. Implement DISKFLAG_CANDELETE / SD Card sector erase support.
 * 2. Implement d_ident from SD Card CID serial number field.
 * 3. Handle read-only SD Cards.
 * 4. Tune timeouts based on real-world SD Card speeds.
 */
devclass_t	altera_sdcard_devclass;

void
altera_sdcard_attach(struct altera_sdcard_softc *sc)
{

	ALTERA_SDCARD_LOCK_INIT(sc);
	ALTERA_SDCARD_CONDVAR_INIT(sc);
	sc->as_disk = NULL;
	bioq_init(&sc->as_bioq);
	sc->as_currentbio = NULL;
	sc->as_state = ALTERA_SDCARD_STATE_NOCARD;
	sc->as_taskqueue = taskqueue_create("altera_sdcardc taskq", M_WAITOK,
	    taskqueue_thread_enqueue, &sc->as_taskqueue);
	taskqueue_start_threads(&sc->as_taskqueue, 1, PI_DISK,
	    "altera_sdcardc%d taskqueue", sc->as_unit);
	TIMEOUT_TASK_INIT(sc->as_taskqueue, &sc->as_task, 0,
	    altera_sdcard_task, sc);

	/*
	 * Kick off timer-driven processing with a manual poll so that we
	 * synchronously detect an already-inserted SD Card during the boot or
	 * other driver attach point.
	 */
	altera_sdcard_task(sc, 1);
}

void
altera_sdcard_detach(struct altera_sdcard_softc *sc)
{

	KASSERT(sc->as_taskqueue != NULL, ("%s: taskqueue not present",
	    __func__));

	/*
	 * Winding down the driver on detach is a bit complex.  Update the
	 * flags to indicate that a detach has been requested, and then wait
	 * for in-progress I/O to wind down before continuing.
	 */
	ALTERA_SDCARD_LOCK(sc);
	sc->as_flags |= ALTERA_SDCARD_FLAG_DETACHREQ;
	while (sc->as_state != ALTERA_SDCARD_STATE_DETACHED)
		ALTERA_SDCARD_CONDVAR_WAIT(sc);
	ALTERA_SDCARD_UNLOCK(sc);

	/*
	 * Now wait for the possibly still executing taskqueue to drain.  In
	 * principle no more events will be scheduled as we've transitioned to
	 * a detached state, but there might still be a request in execution.
	 */
	while (taskqueue_cancel_timeout(sc->as_taskqueue, &sc->as_task, NULL))
		taskqueue_drain_timeout(sc->as_taskqueue, &sc->as_task);

	/*
	 * Simulate a disk removal if one is present to deal with any pending
	 * or queued I/O.
	 */
	if (sc->as_disk != NULL)
		altera_sdcard_disk_remove(sc);
	KASSERT(bioq_first(&sc->as_bioq) == NULL,
	    ("%s: non-empty bioq", __func__));

	/*
	 * Free any remaining allocated resources.
	 */
	taskqueue_free(sc->as_taskqueue);
	sc->as_taskqueue = NULL;
	ALTERA_SDCARD_CONDVAR_DESTROY(sc);
	ALTERA_SDCARD_LOCK_DESTROY(sc);
}

/*
 * Set up and start the next I/O.  Transition to the I/O state, but allow the
 * caller to schedule the next timeout, as this may be called either from an
 * initial attach context, or from the task queue, which requires different
 * behaviour.
 */
static void
altera_sdcard_nextio(struct altera_sdcard_softc *sc)
{
	struct bio *bp;

	ALTERA_SDCARD_LOCK_ASSERT(sc);
	KASSERT(sc->as_currentbio == NULL,
	    ("%s: bio already active", __func__));

	bp = bioq_takefirst(&sc->as_bioq);
	if (bp == NULL)
		panic("%s: bioq empty", __func__);
	altera_sdcard_io_start(sc, bp);
	sc->as_state = ALTERA_SDCARD_STATE_IO;
}

static void
altera_sdcard_task_nocard(struct altera_sdcard_softc *sc)
{

	ALTERA_SDCARD_LOCK_ASSERT(sc);

	/*
	 * Handle device driver detach.
	 */
	if (sc->as_flags & ALTERA_SDCARD_FLAG_DETACHREQ) {
		sc->as_state = ALTERA_SDCARD_STATE_DETACHED;
		return;
	}

	/*
	 * If there is no card insertion, remain in NOCARD.
	 */
	if (!(altera_sdcard_read_asr(sc) & ALTERA_SDCARD_ASR_CARDPRESENT))
		return;

	/*
	 * Read the CSD -- it may contain values that the driver can't handle,
	 * either because of an unsupported version/feature, or because the
	 * card is misbehaving.  This triggers a transition to
	 * ALTERA_SDCARD_STATE_BADCARD.  We rely on the CSD read to print a
	 * banner about how the card is problematic, since it has more
	 * information.  The bad card state allows us to print that banner
	 * once rather than each time we notice the card is there, and still
	 * bad.
	 */
	if (altera_sdcard_read_csd(sc) != 0) {
		sc->as_state = ALTERA_SDCARD_STATE_BADCARD;
		return;
	}

	/*
	 * Process card insertion and upgrade to the IDLE state.
	 */
	altera_sdcard_disk_insert(sc);
	sc->as_state = ALTERA_SDCARD_STATE_IDLE;
}

static void
altera_sdcard_task_badcard(struct altera_sdcard_softc *sc)
{

	ALTERA_SDCARD_LOCK_ASSERT(sc);

	/*
	 * Handle device driver detach.
	 */
	if (sc->as_flags & ALTERA_SDCARD_FLAG_DETACHREQ) {
		sc->as_state = ALTERA_SDCARD_STATE_DETACHED;
		return;
	}

	/*
	 * Handle safe card removal -- no teardown is required, just a state
	 * transition.
	 */
	if (!(altera_sdcard_read_asr(sc) & ALTERA_SDCARD_ASR_CARDPRESENT))
		sc->as_state = ALTERA_SDCARD_STATE_NOCARD;
}

static void
altera_sdcard_task_idle(struct altera_sdcard_softc *sc)
{

	ALTERA_SDCARD_LOCK_ASSERT(sc);

	/*
	 * Handle device driver detach.
	 */
	if (sc->as_flags & ALTERA_SDCARD_FLAG_DETACHREQ) {
		sc->as_state = ALTERA_SDCARD_STATE_DETACHED;
		return;
	}

	/*
	 * Handle safe card removal.
	 */
	if (!(altera_sdcard_read_asr(sc) & ALTERA_SDCARD_ASR_CARDPRESENT)) {
		altera_sdcard_disk_remove(sc);
		sc->as_state = ALTERA_SDCARD_STATE_NOCARD;
	}
}

static void
altera_sdcard_task_io(struct altera_sdcard_softc *sc)
{
	uint16_t asr;

	ALTERA_SDCARD_LOCK_ASSERT(sc);
	KASSERT(sc->as_currentbio != NULL, ("%s: no current I/O", __func__));

#ifdef ALTERA_SDCARD_FAST_SIM
recheck:
#endif
	asr = altera_sdcard_read_asr(sc);

	/*
	 * Check for unexpected card removal during an I/O.
	 */
	if (!(asr & ALTERA_SDCARD_ASR_CARDPRESENT)) {
		altera_sdcard_disk_remove(sc);
		if (sc->as_flags & ALTERA_SDCARD_FLAG_DETACHREQ)
			sc->as_state = ALTERA_SDCARD_STATE_DETACHED;
		else
			sc->as_state = ALTERA_SDCARD_STATE_NOCARD;
		return;
	}

	/*
	 * If the I/O isn't complete, remain in the IO state without further
	 * action, even if DETACHREQ is in flight.
	 */
	if (asr & ALTERA_SDCARD_ASR_CMDINPROGRESS)
		return;

	/*
	 * Handle various forms of I/O completion, successful and otherwise.
	 * The I/O layer may restart the transaction if an error occurred, in
	 * which case remain in the IO state and reschedule.
	 */
	if (!altera_sdcard_io_complete(sc, asr))
		return;

	/*
	 * Now that I/O is complete, process detach requests in preference to
	 * starting new I/O.
	 */
	if (sc->as_flags & ALTERA_SDCARD_FLAG_DETACHREQ) {
		sc->as_state = ALTERA_SDCARD_STATE_DETACHED;
		return;
	}

	/*
	 * Finally, either start the next I/O or transition to the IDLE state.
	 */
	if (bioq_first(&sc->as_bioq) != NULL) {
		altera_sdcard_nextio(sc);
#ifdef ALTERA_SDCARD_FAST_SIM
		goto recheck;
#endif
	} else
		sc->as_state = ALTERA_SDCARD_STATE_IDLE;
}

static void
altera_sdcard_task_rechedule(struct altera_sdcard_softc *sc)
{
	int interval;

	/*
	 * Reschedule based on new state.  Or not, if detaching the device
	 * driver.  Treat a bad card as though it were no card at all.
	 */
	switch (sc->as_state) {
	case ALTERA_SDCARD_STATE_NOCARD:
	case ALTERA_SDCARD_STATE_BADCARD:
		interval = ALTERA_SDCARD_TIMEOUT_NOCARD;
		break;

	case ALTERA_SDCARD_STATE_IDLE:
		interval = ALTERA_SDCARD_TIMEOUT_IDLE;
		break;

	case ALTERA_SDCARD_STATE_IO:
		if (sc->as_flags & ALTERA_SDCARD_FLAG_IOERROR)
			interval = ALTERA_SDCARD_TIMEOUT_IOERROR;
		else
			interval = ALTERA_SDCARD_TIMEOUT_IO;
		break;

	default:
		panic("%s: invalid exit state %d", __func__, sc->as_state);
	}
	taskqueue_enqueue_timeout(sc->as_taskqueue, &sc->as_task, interval);
}

/*
 * Because the Altera SD Card IP Core doesn't support interrupts, we do all
 * asynchronous work from a timeout.  Poll at two different rates -- an
 * infrequent check for card insertion status changes, and a frequent one for
 * I/O completion.  The task should never start in DETACHED, as that would
 * imply that a previous instance failed to cancel rather than reschedule.
 */
void
altera_sdcard_task(void *arg, int pending)
{
	struct altera_sdcard_softc *sc;

	sc = arg;
	KASSERT(sc->as_state != ALTERA_SDCARD_STATE_DETACHED,
	    ("%s: already in detached", __func__));

	ALTERA_SDCARD_LOCK(sc);
	switch (sc->as_state) {
	case ALTERA_SDCARD_STATE_NOCARD:
		altera_sdcard_task_nocard(sc);
		break;

	case ALTERA_SDCARD_STATE_BADCARD:
		altera_sdcard_task_badcard(sc);
		break;

	case ALTERA_SDCARD_STATE_IDLE:
		altera_sdcard_task_idle(sc);
		break;

	case ALTERA_SDCARD_STATE_IO:
		altera_sdcard_task_io(sc);
		break;

	default:
		panic("%s: invalid enter state %d", __func__, sc->as_state);
	}

	/*
	 * If we have transitioned to DETACHED, signal the detach thread and
	 * cancel the timeout-driven task.  Otherwise reschedule on an
	 * appropriate timeout.
	 */
	if (sc->as_state == ALTERA_SDCARD_STATE_DETACHED)
		ALTERA_SDCARD_CONDVAR_SIGNAL(sc);
	else
		altera_sdcard_task_rechedule(sc);
	ALTERA_SDCARD_UNLOCK(sc);
}

void
altera_sdcard_start(struct altera_sdcard_softc *sc)
{

	ALTERA_SDCARD_LOCK_ASSERT(sc);

	KASSERT(sc->as_state == ALTERA_SDCARD_STATE_IDLE,
	    ("%s: starting when not IDLE", __func__));

	taskqueue_cancel_timeout(sc->as_taskqueue, &sc->as_task, NULL);
	altera_sdcard_nextio(sc);
#ifdef ALTERA_SDCARD_FAST_SIM
	altera_sdcard_task_io(sc);
#endif
	altera_sdcard_task_rechedule(sc);
}
