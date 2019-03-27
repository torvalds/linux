/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2016 Solarflare Communications Inc.
 * All rights reserved.
 *
 * This software was developed in part by Philip Paeps under contract for
 * Solarflare Communications, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing official
 * policies, either expressed or implied, of the FreeBSD Project.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_rss.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/rman.h>
#include <sys/syslog.h>
#include <sys/taskqueue.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#ifdef RSS
#include <net/rss_config.h>
#endif

#include "common/efx.h"

#include "sfxge.h"

static int
sfxge_intr_line_filter(void *arg)
{
	struct sfxge_evq *evq;
	struct sfxge_softc *sc;
	efx_nic_t *enp;
	struct sfxge_intr *intr;
	boolean_t fatal;
	uint32_t qmask;

	evq = (struct sfxge_evq *)arg;
	sc = evq->sc;
	enp = sc->enp;
	intr = &sc->intr;

	KASSERT(intr != NULL, ("intr == NULL"));
	KASSERT(intr->type == EFX_INTR_LINE,
	    ("intr->type != EFX_INTR_LINE"));

	if (intr->state != SFXGE_INTR_STARTED)
		return (FILTER_STRAY);

	(void)efx_intr_status_line(enp, &fatal, &qmask);

	if (fatal) {
		(void) efx_intr_disable(enp);
		(void) efx_intr_fatal(enp);
		return (FILTER_HANDLED);
	}

	if (qmask != 0) {
		intr->zero_count = 0;
		return (FILTER_SCHEDULE_THREAD);
	}

	/* SF bug 15783: If the function is not asserting its IRQ and
	 * we read the queue mask on the cycle before a flag is added
	 * to the mask, this inhibits the function from asserting the
	 * IRQ even though we don't see the flag set.  To work around
	 * this, we must re-prime all event queues and report the IRQ
	 * as handled when we see a mask of zero.  To allow for shared
	 * IRQs, we don't repeat this if we see a mask of zero twice
	 * or more in a row.
	 */
	if (intr->zero_count++ == 0) {
		if (evq->init_state == SFXGE_EVQ_STARTED) {
			if (efx_ev_qpending(evq->common, evq->read_ptr))
				return (FILTER_SCHEDULE_THREAD);
			efx_ev_qprime(evq->common, evq->read_ptr);
			return (FILTER_HANDLED);
		}
	}

	return (FILTER_STRAY);
}

static void
sfxge_intr_line(void *arg)
{
	struct sfxge_evq *evq = arg;

	(void)sfxge_ev_qpoll(evq);
}

static void
sfxge_intr_message(void *arg)
{
	struct sfxge_evq *evq;
	struct sfxge_softc *sc;
	efx_nic_t *enp;
	struct sfxge_intr *intr;
	unsigned int index;
	boolean_t fatal;

	evq = (struct sfxge_evq *)arg;
	sc = evq->sc;
	enp = sc->enp;
	intr = &sc->intr;
	index = evq->index;

	KASSERT(intr != NULL, ("intr == NULL"));
	KASSERT(intr->type == EFX_INTR_MESSAGE,
	    ("intr->type != EFX_INTR_MESSAGE"));

	if (__predict_false(intr->state != SFXGE_INTR_STARTED))
		return;

	(void)efx_intr_status_message(enp, index, &fatal);

	if (fatal) {
		(void)efx_intr_disable(enp);
		(void)efx_intr_fatal(enp);
		return;
	}

	(void)sfxge_ev_qpoll(evq);
}

static int
sfxge_intr_bus_enable(struct sfxge_softc *sc)
{
	struct sfxge_intr *intr;
	struct sfxge_intr_hdl *table;
	driver_filter_t *filter;
	driver_intr_t *handler;
	int index;
	int err;

	intr = &sc->intr;
	table = intr->table;

	switch (intr->type) {
	case EFX_INTR_MESSAGE:
		filter = NULL; /* not shared */
		handler = sfxge_intr_message;
		break;

	case EFX_INTR_LINE:
		filter = sfxge_intr_line_filter;
		handler = sfxge_intr_line;
		break;

	default:
		KASSERT(0, ("Invalid interrupt type"));
		return (EINVAL);
	}

	/* Try to add the handlers */
	for (index = 0; index < intr->n_alloc; index++) {
		if ((err = bus_setup_intr(sc->dev, table[index].eih_res,
			    INTR_MPSAFE|INTR_TYPE_NET, filter, handler,
			    sc->evq[index], &table[index].eih_tag)) != 0) {
			goto fail;
		}
#ifdef SFXGE_HAVE_DESCRIBE_INTR
		if (intr->n_alloc > 1)
			bus_describe_intr(sc->dev, table[index].eih_res,
			    table[index].eih_tag, "%d", index);
#endif
#ifdef RSS
		bus_bind_intr(sc->dev, table[index].eih_res,
			      rss_getcpu(index));
#else
		bus_bind_intr(sc->dev, table[index].eih_res, index);
#endif

	}

	return (0);

fail:
	/* Remove remaining handlers */
	while (--index >= 0)
		bus_teardown_intr(sc->dev, table[index].eih_res,
		    table[index].eih_tag);

	return (err);
}

static void
sfxge_intr_bus_disable(struct sfxge_softc *sc)
{
	struct sfxge_intr *intr;
	struct sfxge_intr_hdl *table;
	int i;

	intr = &sc->intr;
	table = intr->table;

	/* Remove all handlers */
	for (i = 0; i < intr->n_alloc; i++)
		bus_teardown_intr(sc->dev, table[i].eih_res,
		    table[i].eih_tag);
}

static int
sfxge_intr_alloc(struct sfxge_softc *sc, int count)
{
	device_t dev;
	struct sfxge_intr_hdl *table;
	struct sfxge_intr *intr;
	struct resource *res;
	int rid;
	int error;
	int i;

	dev = sc->dev;
	intr = &sc->intr;
	error = 0;

	table = malloc(count * sizeof(struct sfxge_intr_hdl),
	    M_SFXGE, M_WAITOK);
	intr->table = table;

	for (i = 0; i < count; i++) {
		rid = i + 1;
		res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
		    RF_SHAREABLE | RF_ACTIVE);
		if (res == NULL) {
			device_printf(dev, "Couldn't allocate interrupts for "
			    "message %d\n", rid);
			error = ENOMEM;
			break;
		}
		table[i].eih_rid = rid;
		table[i].eih_res = res;
	}

	if (error != 0) {
		count = i - 1;
		for (i = 0; i < count; i++)
			bus_release_resource(dev, SYS_RES_IRQ,
			    table[i].eih_rid, table[i].eih_res);
	}

	return (error);
}

static void
sfxge_intr_teardown_msix(struct sfxge_softc *sc)
{
	device_t dev;
	struct resource *resp;
	int rid;

	dev = sc->dev;
	resp = sc->intr.msix_res;

	rid = rman_get_rid(resp);
	bus_release_resource(dev, SYS_RES_MEMORY, rid, resp);
}

static int
sfxge_intr_setup_msix(struct sfxge_softc *sc)
{
	struct sfxge_intr *intr;
	struct resource *resp;
	device_t dev;
	int count;
	int rid;

	dev = sc->dev;
	intr = &sc->intr;

	/* Check if MSI-X is available. */
	count = pci_msix_count(dev);
	if (count == 0)
		return (EINVAL);

	/* Do not try to allocate more than already estimated EVQ maximum */
	KASSERT(sc->evq_max > 0, ("evq_max is zero"));
	count = MIN(count, sc->evq_max);

	rid = PCIR_BAR(4);
	resp = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (resp == NULL)
		return (ENOMEM);

	if (pci_alloc_msix(dev, &count) != 0) {
		bus_release_resource(dev, SYS_RES_MEMORY, rid, resp);
		return (ENOMEM);
	}

	/* Allocate interrupt handlers. */
	if (sfxge_intr_alloc(sc, count) != 0) {
		bus_release_resource(dev, SYS_RES_MEMORY, rid, resp);
		pci_release_msi(dev);
		return (ENOMEM);
	}

	intr->type = EFX_INTR_MESSAGE;
	intr->n_alloc = count;
	intr->msix_res = resp;

	return (0);
}

static int
sfxge_intr_setup_msi(struct sfxge_softc *sc)
{
	struct sfxge_intr_hdl *table;
	struct sfxge_intr *intr;
	device_t dev;
	int count;
	int error;

	dev = sc->dev;
	intr = &sc->intr;
	table = intr->table;

	/*
	 * Check if MSI is available.  All messages must be written to
	 * the same address and on x86 this means the IRQs have the
	 * same CPU affinity.  So we only ever allocate 1.
	 */
	count = pci_msi_count(dev) ? 1 : 0;
	if (count == 0)
		return (EINVAL);

	if ((error = pci_alloc_msi(dev, &count)) != 0)
		return (ENOMEM);

	/* Allocate interrupt handler. */
	if (sfxge_intr_alloc(sc, count) != 0) {
		pci_release_msi(dev);
		return (ENOMEM);
	}

	intr->type = EFX_INTR_MESSAGE;
	intr->n_alloc = count;

	return (0);
}

static int
sfxge_intr_setup_fixed(struct sfxge_softc *sc)
{
	struct sfxge_intr_hdl *table;
	struct sfxge_intr *intr;
	struct resource *res;
	device_t dev;
	int rid;

	dev = sc->dev;
	intr = &sc->intr;

	rid = 0;
	res =  bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_SHAREABLE | RF_ACTIVE);
	if (res == NULL)
		return (ENOMEM);

	table = malloc(sizeof(struct sfxge_intr_hdl), M_SFXGE, M_WAITOK);
	table[0].eih_rid = rid;
	table[0].eih_res = res;

	intr->type = EFX_INTR_LINE;
	intr->n_alloc = 1;
	intr->table = table;

	return (0);
}

static const char *const __sfxge_err[] = {
	"",
	"SRAM out-of-bounds",
	"Buffer ID out-of-bounds",
	"Internal memory parity",
	"Receive buffer ownership",
	"Transmit buffer ownership",
	"Receive descriptor ownership",
	"Transmit descriptor ownership",
	"Event queue ownership",
	"Event queue FIFO overflow",
	"Illegal address",
	"SRAM parity"
};

void
sfxge_err(efsys_identifier_t *arg, unsigned int code, uint32_t dword0,
	  uint32_t dword1)
{
	struct sfxge_softc *sc = (struct sfxge_softc *)arg;
	device_t dev = sc->dev;

	log(LOG_WARNING, "[%s%d] FATAL ERROR: %s (0x%08x%08x)",
	    device_get_name(dev), device_get_unit(dev),
		__sfxge_err[code], dword1, dword0);
}

void
sfxge_intr_stop(struct sfxge_softc *sc)
{
	struct sfxge_intr *intr;

	intr = &sc->intr;

	KASSERT(intr->state == SFXGE_INTR_STARTED,
	    ("Interrupts not started"));

	intr->state = SFXGE_INTR_INITIALIZED;

	/* Disable interrupts at the NIC */
	efx_intr_disable(sc->enp);

	/* Disable interrupts at the bus */
	sfxge_intr_bus_disable(sc);

	/* Tear down common code interrupt bits. */
	efx_intr_fini(sc->enp);
}

int
sfxge_intr_start(struct sfxge_softc *sc)
{
	struct sfxge_intr *intr;
	efsys_mem_t *esmp;
	int rc;

	intr = &sc->intr;
	esmp = &intr->status;

	KASSERT(intr->state == SFXGE_INTR_INITIALIZED,
	    ("Interrupts not initialized"));

	/* Zero the memory. */
	(void)memset(esmp->esm_base, 0, EFX_INTR_SIZE);

	/* Initialize common code interrupt bits. */
	(void)efx_intr_init(sc->enp, intr->type, esmp);

	/* Enable interrupts at the bus */
	if ((rc = sfxge_intr_bus_enable(sc)) != 0)
		goto fail;

	intr->state = SFXGE_INTR_STARTED;

	/* Enable interrupts at the NIC */
	efx_intr_enable(sc->enp);

	return (0);

fail:
	/* Tear down common code interrupt bits. */
	efx_intr_fini(sc->enp);

	intr->state = SFXGE_INTR_INITIALIZED;

	return (rc);
}

void
sfxge_intr_fini(struct sfxge_softc *sc)
{
	struct sfxge_intr_hdl *table;
	struct sfxge_intr *intr;
	efsys_mem_t *esmp;
	device_t dev;
	int i;

	dev = sc->dev;
	intr = &sc->intr;
	esmp = &intr->status;
	table = intr->table;

	KASSERT(intr->state == SFXGE_INTR_INITIALIZED,
	    ("intr->state != SFXGE_INTR_INITIALIZED"));

	/* Free DMA memory. */
	sfxge_dma_free(esmp);

	/* Free interrupt handles. */
	for (i = 0; i < intr->n_alloc; i++)
		bus_release_resource(dev, SYS_RES_IRQ,
		    table[i].eih_rid, table[i].eih_res);

	if (table[0].eih_rid != 0)
		pci_release_msi(dev);

	if (intr->msix_res != NULL)
		sfxge_intr_teardown_msix(sc);

	/* Free the handle table */
	free(table, M_SFXGE);
	intr->table = NULL;
	intr->n_alloc = 0;

	/* Clear the interrupt type */
	intr->type = EFX_INTR_INVALID;

	intr->state = SFXGE_INTR_UNINITIALIZED;
}

int
sfxge_intr_init(struct sfxge_softc *sc)
{
	device_t dev;
	struct sfxge_intr *intr;
	efsys_mem_t *esmp;
	int rc;

	dev = sc->dev;
	intr = &sc->intr;
	esmp = &intr->status;

	KASSERT(intr->state == SFXGE_INTR_UNINITIALIZED,
	    ("Interrupts already initialized"));

	/* Try to setup MSI-X or MSI interrupts if available. */
	if ((rc = sfxge_intr_setup_msix(sc)) == 0)
		device_printf(dev, "Using MSI-X interrupts\n");
	else if ((rc = sfxge_intr_setup_msi(sc)) == 0)
		device_printf(dev, "Using MSI interrupts\n");
	else if ((rc = sfxge_intr_setup_fixed(sc)) == 0) {
		device_printf(dev, "Using fixed interrupts\n");
	} else {
		device_printf(dev, "Couldn't setup interrupts\n");
		return (ENOMEM);
	}

	/* Set up DMA for interrupts. */
	if ((rc = sfxge_dma_alloc(sc, EFX_INTR_SIZE, esmp)) != 0)
		return (ENOMEM);

	intr->state = SFXGE_INTR_INITIALIZED;

	return (0);
}
