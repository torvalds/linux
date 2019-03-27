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

#include <machine/tlb.h>

#include "bman.h"

devclass_t bman_devclass;

static struct bman_softc *bman_sc;

extern t_Handle bman_portal_setup(struct bman_softc *bsc);

static void
bman_exception(t_Handle h_App, e_BmExceptions exception)
{
	struct bman_softc *sc;
	const char *message;

	sc = h_App;

	switch (exception) {
    	case e_BM_EX_INVALID_COMMAND:
		message = "Invalid Command Verb";
		break;
	case e_BM_EX_FBPR_THRESHOLD:
		message = "FBPR pool exhaused. Consider increasing "
		    "BMAN_MAX_BUFFERS";
		break;
	case e_BM_EX_SINGLE_ECC:
		message = "Single bit ECC error";
		break;
	case e_BM_EX_MULTI_ECC:
		message = "Multi bit ECC error";
		break;
	default:
		message = "Unknown error";
	}

	device_printf(sc->sc_dev, "BMAN Exception: %s.\n", message);
}

int
bman_attach(device_t dev)
{
	struct bman_softc *sc;
	t_BmRevisionInfo rev;
	t_Error error;
	t_BmParam bp;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	bman_sc = sc;

	/* Check if MallocSmart allocator is ready */
	if (XX_MallocSmartInit() != E_OK)
		return (ENXIO);

	/* Allocate resources */
	sc->sc_rrid = 0;
	sc->sc_rres = bus_alloc_resource_anywhere(dev, SYS_RES_MEMORY,
	    &sc->sc_rrid, BMAN_CCSR_SIZE, RF_ACTIVE);
	if (sc->sc_rres == NULL)
		return (ENXIO);

	sc->sc_irid = 0;
	sc->sc_ires = bus_alloc_resource_any(sc->sc_dev, SYS_RES_IRQ,
	    &sc->sc_irid, RF_ACTIVE | RF_SHAREABLE);
	if (sc->sc_ires == NULL)
		goto err;

	/* Initialize BMAN */
	memset(&bp, 0, sizeof(bp));
	bp.guestId = NCSW_MASTER_ID;
	bp.baseAddress = rman_get_bushandle(sc->sc_rres);
	bp.totalNumOfBuffers = BMAN_MAX_BUFFERS;
	bp.f_Exception = bman_exception;
	bp.h_App = sc;
	bp.errIrq = (uintptr_t)sc->sc_ires;
	bp.partBpidBase = 0;
	bp.partNumOfPools = BM_MAX_NUM_OF_POOLS;

	sc->sc_bh = BM_Config(&bp);
	if (sc->sc_bh == NULL)
		goto err;

	/* Warn if there is less than 5% free FPBR's in pool */
	error = BM_ConfigFbprThreshold(sc->sc_bh, (BMAN_MAX_BUFFERS / 8) / 20);
	if (error != E_OK)
		goto err;

	error = BM_Init(sc->sc_bh);
	if (error != E_OK)
		goto err;

	error = BM_GetRevision(sc->sc_bh, &rev);
	if (error != E_OK)
		goto err;

	device_printf(dev, "Hardware version: %d.%d.\n",
	    rev.majorRev, rev.minorRev);

	return (0);

err:
	bman_detach(dev);
	return (ENXIO);
}

int
bman_detach(device_t dev)
{
	struct bman_softc *sc;

	sc = device_get_softc(dev);

	if (sc->sc_bh != NULL)
		BM_Free(sc->sc_bh);

	if (sc->sc_ires != NULL)
		bus_release_resource(dev, SYS_RES_IRQ,
		    sc->sc_irid, sc->sc_ires);

	if (sc->sc_rres != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY,
		    sc->sc_rrid, sc->sc_rres);

	return (0);
}

int
bman_suspend(device_t dev)
{

	return (0);
}

int
bman_resume(device_t dev)
{

	return (0);
}

int
bman_shutdown(device_t dev)
{

	return (0);
}

/*
 * BMAN API
 */

t_Handle
bman_pool_create(uint8_t *bpid, uint16_t bufferSize, uint16_t maxBuffers,
    uint16_t minBuffers, uint16_t allocBuffers, t_GetBufFunction *f_GetBuf,
    t_PutBufFunction *f_PutBuf, uint32_t dep_sw_entry, uint32_t dep_sw_exit,
    uint32_t dep_hw_entry, uint32_t dep_hw_exit,
    t_BmDepletionCallback *f_Depletion, t_Handle h_BufferPool,
    t_PhysToVirt *f_PhysToVirt, t_VirtToPhys *f_VirtToPhys)
{
	uint32_t thresholds[MAX_DEPLETION_THRESHOLDS];
	struct bman_softc *sc;
	t_Handle pool, portal;
	t_BmPoolParam bpp;
	int error;

	sc = bman_sc;
	pool = NULL;

	sched_pin();

	portal = bman_portal_setup(sc);
	if (portal == NULL)
		goto err;

	memset(&bpp, 0, sizeof(bpp));
	bpp.h_Bm = sc->sc_bh;
	bpp.h_BmPortal = portal;
	bpp.h_App = h_BufferPool;
	bpp.numOfBuffers = allocBuffers;

	bpp.bufferPoolInfo.h_BufferPool = h_BufferPool;
	bpp.bufferPoolInfo.f_GetBuf = f_GetBuf;
	bpp.bufferPoolInfo.f_PutBuf = f_PutBuf;
	bpp.bufferPoolInfo.f_PhysToVirt = f_PhysToVirt;
	bpp.bufferPoolInfo.f_VirtToPhys = f_VirtToPhys;
	bpp.bufferPoolInfo.bufferSize = bufferSize;

	pool = BM_POOL_Config(&bpp);
	if (pool == NULL)
		goto err;

	/*
	 * Buffer context must be disabled on FreeBSD
	 * as it could cause memory corruption.
	 */
	BM_POOL_ConfigBuffContextMode(pool, 0);

	if (minBuffers != 0 || maxBuffers != 0) {
		error = BM_POOL_ConfigStockpile(pool, maxBuffers, minBuffers);
		if (error != E_OK)
			goto err;
	}

	if (f_Depletion != NULL) {
		thresholds[BM_POOL_DEP_THRESH_SW_ENTRY] = dep_sw_entry;
		thresholds[BM_POOL_DEP_THRESH_SW_EXIT] = dep_sw_exit;
		thresholds[BM_POOL_DEP_THRESH_HW_ENTRY] = dep_hw_entry;
		thresholds[BM_POOL_DEP_THRESH_HW_EXIT] = dep_hw_exit;
		error = BM_POOL_ConfigDepletion(pool, f_Depletion, thresholds);
		if (error != E_OK)
			goto err;
	}

	error = BM_POOL_Init(pool);
	if (error != E_OK)
		goto err;

	*bpid = BM_POOL_GetId(pool);
	sc->sc_bpool_cpu[*bpid] = PCPU_GET(cpuid);

	sched_unpin();

	return (pool);

err:
	if (pool != NULL)
		BM_POOL_Free(pool);

	sched_unpin();

	return (NULL);
}

int
bman_pool_destroy(t_Handle pool)
{
	struct bman_softc *sc;

	sc = bman_sc;
	thread_lock(curthread);
	sched_bind(curthread, sc->sc_bpool_cpu[BM_POOL_GetId(pool)]);
	thread_unlock(curthread);

	BM_POOL_Free(pool);

	thread_lock(curthread);
	sched_unbind(curthread);
	thread_unlock(curthread);

	return (0);
}

int
bman_pool_fill(t_Handle pool, uint16_t nbufs)
{
	struct bman_softc *sc;
	t_Handle portal;
	int error;

	sc = bman_sc;
	sched_pin();

	portal = bman_portal_setup(sc);
	if (portal == NULL) {
		sched_unpin();
		return (EIO);
	}

	error = BM_POOL_FillBufs(pool, portal, nbufs);

	sched_unpin();

	return ((error == E_OK) ? 0 : EIO);
}

void *
bman_get_buffer(t_Handle pool)
{
	struct bman_softc *sc;
	t_Handle portal;
	void *buffer;

	sc = bman_sc;
	sched_pin();

	portal = bman_portal_setup(sc);
	if (portal == NULL) {
		sched_unpin();
		return (NULL);
	}

	buffer = BM_POOL_GetBuf(pool, portal);

	sched_unpin();

	return (buffer);
}

int
bman_put_buffer(t_Handle pool, void *buffer)
{
	struct bman_softc *sc;
	t_Handle portal;
	int error;

	sc = bman_sc;
	sched_pin();

	portal = bman_portal_setup(sc);
	if (portal == NULL) {
		sched_unpin();
		return (EIO);
	}

	error = BM_POOL_PutBuf(pool, portal, buffer);

	sched_unpin();

	return ((error == E_OK) ? 0 : EIO);
}

uint32_t
bman_count(t_Handle pool)
{

	return (BM_POOL_GetCounter(pool, e_BM_POOL_COUNTERS_CONTENT));
}
