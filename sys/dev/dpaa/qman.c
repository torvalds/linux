/*-
 * Copyright (c) 2011-2012 Semihalf.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/pcpu.h>
#include <sys/rman.h>
#include <sys/sched.h>
#include <sys/smp.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/tlb.h>

#include "qman.h"
#include "portals.h"

extern struct dpaa_portals_softc *qp_sc;
static struct qman_softc *qman_sc;

extern t_Handle qman_portal_setup(struct qman_softc *qsc);

static void
qman_exception(t_Handle app, e_QmExceptions exception)
{
	struct qman_softc *sc;
	const char *message;

	sc = app;

	switch (exception) {
	case e_QM_EX_CORENET_INITIATOR_DATA:
		message = "Initiator Data Error";
		break;
	case e_QM_EX_CORENET_TARGET_DATA:
		message = "CoreNet Target Data Error";
		break;
	case e_QM_EX_CORENET_INVALID_TARGET_TRANSACTION:
		message = "Invalid Target Transaction";
		break;
	case e_QM_EX_PFDR_THRESHOLD:
		message = "PFDR Low Watermark Interrupt";
		break;
	case e_QM_EX_PFDR_ENQUEUE_BLOCKED:
		message = "PFDR Enqueues Blocked Interrupt";
		break;
	case e_QM_EX_SINGLE_ECC:
		message = "Single Bit ECC Error Interrupt";
		break;
	case e_QM_EX_MULTI_ECC:
		message = "Multi Bit ECC Error Interrupt";
		break;
	case e_QM_EX_INVALID_COMMAND:
		message = "Invalid Command Verb Interrupt";
		break;
	case e_QM_EX_DEQUEUE_DCP:
		message = "Invalid Dequeue Direct Connect Portal Interrupt";
		break;
	case e_QM_EX_DEQUEUE_FQ:
		message = "Invalid Dequeue FQ Interrupt";
		break;
	case e_QM_EX_DEQUEUE_SOURCE:
		message = "Invalid Dequeue Source Interrupt";
		break;
	case e_QM_EX_DEQUEUE_QUEUE:
		message = "Invalid Dequeue Queue Interrupt";
		break;
	case e_QM_EX_ENQUEUE_OVERFLOW:
		message = "Invalid Enqueue Overflow Interrupt";
		break;
	case e_QM_EX_ENQUEUE_STATE:
		message = "Invalid Enqueue State Interrupt";
		break;
	case e_QM_EX_ENQUEUE_CHANNEL:
		message = "Invalid Enqueue Channel Interrupt";
		break;
	case e_QM_EX_ENQUEUE_QUEUE:
		message = "Invalid Enqueue Queue Interrupt";
		break;
	case e_QM_EX_CG_STATE_CHANGE:
		message = "CG change state notification";
		break;
	default:
		message = "Unknown error";
	}

	device_printf(sc->sc_dev, "QMan Exception: %s.\n", message);
}

/**
 * General received frame callback.
 * This is called, when user did not register his own callback for a given
 * frame queue range (fqr).
 */
e_RxStoreResponse
qman_received_frame_callback(t_Handle app, t_Handle qm_fqr, t_Handle qm_portal,
    uint32_t fqid_offset, t_DpaaFD *frame)
{
	struct qman_softc *sc;

	sc = app;

	device_printf(sc->sc_dev, "dummy callback for received frame.\n");
	return (e_RX_STORE_RESPONSE_CONTINUE);
}

/**
 * General rejected frame callback.
 * This is called, when user did not register his own callback for a given
 * frame queue range (fqr).
 */
e_RxStoreResponse
qman_rejected_frame_callback(t_Handle app, t_Handle qm_fqr, t_Handle qm_portal,
    uint32_t fqid_offset, t_DpaaFD *frame,
    t_QmRejectedFrameInfo *qm_rejected_frame_info)
{
	struct qman_softc *sc;

	sc = app;

	device_printf(sc->sc_dev, "dummy callback for rejected frame.\n");
	return (e_RX_STORE_RESPONSE_CONTINUE);
}

int
qman_attach(device_t dev)
{
	struct qman_softc *sc;
	t_QmParam qp;
	t_Error error;
	t_QmRevisionInfo rev;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	qman_sc = sc;

	if (XX_MallocSmartInit() != E_OK) {
		device_printf(dev, "could not initialize smart allocator.\n");
		return (ENXIO);
	}

	sched_pin();

	/* Allocate resources */
	sc->sc_rrid = 0;
	sc->sc_rres = bus_alloc_resource(dev, SYS_RES_MEMORY,
	    &sc->sc_rrid, 0, ~0, QMAN_CCSR_SIZE, RF_ACTIVE);
	if (sc->sc_rres == NULL) {
		device_printf(dev, "could not allocate memory.\n");
		goto err;
	}

	sc->sc_irid = 0;
	sc->sc_ires = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &sc->sc_irid, RF_ACTIVE | RF_SHAREABLE);
	if (sc->sc_ires == NULL) {
		device_printf(dev, "could not allocate error interrupt.\n");
		goto err;
	}

	if (qp_sc == NULL)
		goto err;

	dpaa_portal_map_registers(qp_sc);

	/* Initialize QMan */
	qp.guestId = NCSW_MASTER_ID;
	qp.baseAddress = rman_get_bushandle(sc->sc_rres);
	qp.swPortalsBaseAddress = rman_get_bushandle(qp_sc->sc_rres[0]);
	qp.liodn = 0;
	qp.totalNumOfFqids = QMAN_MAX_FQIDS;
	qp.fqdMemPartitionId = NCSW_MASTER_ID;
	qp.pfdrMemPartitionId = NCSW_MASTER_ID;
	qp.f_Exception = qman_exception;
	qp.h_App = sc;
	qp.errIrq = (uintptr_t)sc->sc_ires;
	qp.partFqidBase = QMAN_FQID_BASE;
	qp.partNumOfFqids = QMAN_MAX_FQIDS;
	qp.partCgsBase = 0;
	qp.partNumOfCgs = 0;

	sc->sc_qh = QM_Config(&qp);
	if (sc->sc_qh == NULL) {
		device_printf(dev, "could not be configured\n");
		goto err;
	}

	error = QM_Init(sc->sc_qh);
	if (error != E_OK) {
		device_printf(dev, "could not be initialized\n");
		goto err;
	}

	error = QM_GetRevision(sc->sc_qh, &rev);
	if (error != E_OK) {
		device_printf(dev, "could not get QMan revision\n");
		goto err;
	}

	device_printf(dev, "Hardware version: %d.%d.\n",
	    rev.majorRev, rev.minorRev);

	sched_unpin();

	qman_portal_setup(sc);

	return (0);

err:
	sched_unpin();
	qman_detach(dev);
	return (ENXIO);
}

int
qman_detach(device_t dev)
{
	struct qman_softc *sc;

	sc = device_get_softc(dev);

	if (sc->sc_qh)
		QM_Free(sc->sc_qh);

	if (sc->sc_ires != NULL)
		XX_DeallocIntr((uintptr_t)sc->sc_ires);

	if (sc->sc_ires != NULL)
		bus_release_resource(dev, SYS_RES_IRQ,
		    sc->sc_irid, sc->sc_ires);

	if (sc->sc_rres != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY,
		    sc->sc_rrid, sc->sc_rres);

	return (0);
}

int
qman_suspend(device_t dev)
{

	return (0);
}

int
qman_resume(device_t dev)
{

	return (0);
}

int
qman_shutdown(device_t dev)
{

	return (0);
}


/**
 * @group QMan API functions implementation.
 * @{
 */

t_Handle
qman_fqr_create(uint32_t fqids_num, e_QmFQChannel channel, uint8_t wq,
    bool force_fqid, uint32_t fqid_or_align, bool init_parked,
    bool hold_active, bool prefer_in_cache, bool congst_avoid_ena,
    t_Handle congst_group, int8_t overhead_accounting_len,
    uint32_t tail_drop_threshold)
{
	struct qman_softc *sc;
	t_QmFqrParams fqr;
	unsigned int cpu;
	t_Handle fqrh, portal;

	sc = qman_sc;

	sched_pin();
	cpu = PCPU_GET(cpuid);

	/* Ensure we have got QMan port initialized */
	portal = qman_portal_setup(sc);
	if (portal == NULL) {
		device_printf(sc->sc_dev, "could not setup QMan portal\n");
		goto err;
	}

	fqr.h_Qm = sc->sc_qh;
	fqr.h_QmPortal = portal;
	fqr.initParked = init_parked;
	fqr.holdActive = hold_active;
	fqr.preferInCache = prefer_in_cache;

	/* We do not support stashing */
	fqr.useContextAForStash = FALSE;
	fqr.p_ContextA = 0;
	fqr.p_ContextB = 0;

	fqr.channel = channel;
	fqr.wq = wq;
	fqr.shadowMode = FALSE;
	fqr.numOfFqids = fqids_num;

	/* FQID */
	fqr.useForce = force_fqid;
	if (force_fqid) {
		fqr.qs.frcQ.fqid = fqid_or_align;
	} else {
		fqr.qs.nonFrcQs.align = fqid_or_align;
	}

	/* Congestion Avoidance */
	fqr.congestionAvoidanceEnable = congst_avoid_ena;
	if (congst_avoid_ena) {
		fqr.congestionAvoidanceParams.h_QmCg = congst_group;
		fqr.congestionAvoidanceParams.overheadAccountingLength =
		    overhead_accounting_len;
		fqr.congestionAvoidanceParams.fqTailDropThreshold =
		    tail_drop_threshold;
	} else {
		fqr.congestionAvoidanceParams.h_QmCg = 0;
		fqr.congestionAvoidanceParams.overheadAccountingLength = 0;
		fqr.congestionAvoidanceParams.fqTailDropThreshold = 0;
	}

	fqrh = QM_FQR_Create(&fqr);
	if (fqrh == NULL) {
		device_printf(sc->sc_dev, "could not create Frame Queue Range"
		    "\n");
		goto err;
	}

	sc->sc_fqr_cpu[QM_FQR_GetFqid(fqrh)] = PCPU_GET(cpuid);

	sched_unpin();

	return (fqrh);

err:
	sched_unpin();

	return (NULL);
}

t_Error
qman_fqr_free(t_Handle fqr)
{
	struct qman_softc *sc;
	t_Error error;

	sc = qman_sc;
	thread_lock(curthread);
	sched_bind(curthread, sc->sc_fqr_cpu[QM_FQR_GetFqid(fqr)]);
	thread_unlock(curthread);

	error = QM_FQR_Free(fqr);

	thread_lock(curthread);
	sched_unbind(curthread);
	thread_unlock(curthread);

	return (error);
}

t_Error
qman_fqr_register_cb(t_Handle fqr, t_QmReceivedFrameCallback *callback,
    t_Handle app)
{
	struct qman_softc *sc;
	t_Error error;
	t_Handle portal;

	sc = qman_sc;
	sched_pin();

	/* Ensure we have got QMan port initialized */
	portal = qman_portal_setup(sc);
	if (portal == NULL) {
		device_printf(sc->sc_dev, "could not setup QMan portal\n");
		sched_unpin();
		return (E_NOT_SUPPORTED);
	}

	error = QM_FQR_RegisterCB(fqr, callback, app);

	sched_unpin();

	return (error);
}

t_Error
qman_fqr_enqueue(t_Handle fqr, uint32_t fqid_off, t_DpaaFD *frame)
{
	struct qman_softc *sc;
	t_Error error;
	t_Handle portal;

	sc = qman_sc;
	sched_pin();

	/* Ensure we have got QMan port initialized */
	portal = qman_portal_setup(sc);
	if (portal == NULL) {
		device_printf(sc->sc_dev, "could not setup QMan portal\n");
		sched_unpin();
		return (E_NOT_SUPPORTED);
	}

	error = QM_FQR_Enqueue(fqr, portal, fqid_off, frame);

	sched_unpin();

	return (error);
}

uint32_t
qman_fqr_get_counter(t_Handle fqr, uint32_t fqid_off,
    e_QmFqrCounters counter)
{
	struct qman_softc *sc;
	uint32_t val;
	t_Handle portal;

	sc = qman_sc;
	sched_pin();

	/* Ensure we have got QMan port initialized */
	portal = qman_portal_setup(sc);
	if (portal == NULL) {
		device_printf(sc->sc_dev, "could not setup QMan portal\n");
		sched_unpin();
		return (0);
	}

	val = QM_FQR_GetCounter(fqr, portal, fqid_off, counter);

	sched_unpin();

	return (val);
}

t_Error
qman_fqr_pull_frame(t_Handle fqr, uint32_t fqid_off, t_DpaaFD *frame)
{
	struct qman_softc *sc;
	t_Error error;
	t_Handle portal;

	sc = qman_sc;
	sched_pin();

	/* Ensure we have got QMan port initialized */
	portal = qman_portal_setup(sc);
	if (portal == NULL) {
		device_printf(sc->sc_dev, "could not setup QMan portal\n");
		sched_unpin();
		return (E_NOT_SUPPORTED);
	}

	error = QM_FQR_PullFrame(fqr, portal, fqid_off, frame);

	sched_unpin();

	return (error);
}

uint32_t
qman_fqr_get_base_fqid(t_Handle fqr)
{
	struct qman_softc *sc;
	uint32_t val;
	t_Handle portal;

	sc = qman_sc;
	sched_pin();

	/* Ensure we have got QMan port initialized */
	portal = qman_portal_setup(sc);
	if (portal == NULL) {
		device_printf(sc->sc_dev, "could not setup QMan portal\n");
		sched_unpin();
		return (0);
	}

	val = QM_FQR_GetFqid(fqr);

	sched_unpin();

	return (val);
}

t_Error
qman_poll(e_QmPortalPollSource source)
{
	struct qman_softc *sc;
	t_Error error;
	t_Handle portal;

	sc = qman_sc;
	sched_pin();

	/* Ensure we have got QMan port initialized */
	portal = qman_portal_setup(sc);
	if (portal == NULL) {
		device_printf(sc->sc_dev, "could not setup QMan portal\n");
		sched_unpin();
		return (E_NOT_SUPPORTED);
	}

	error = QM_Poll(sc->sc_qh, source);

	sched_unpin();

	return (error);
}

/*
 * TODO: add polling and/or congestion support.
 */

/** @} */
