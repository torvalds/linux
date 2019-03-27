/*-
 * Copyright (c) 2018 Microsemi Corporation.
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

/* $FreeBSD$ */

#include "smartpqi_includes.h"


/*
 * Function to get processor count
 */
int os_get_processor_config(pqisrc_softstate_t *softs)
{
	DBG_FUNC("IN\n");
	softs->num_cpus_online = mp_ncpus;
	DBG_FUNC("OUT\n");
	
	return PQI_STATUS_SUCCESS;
}

/*
 * Function to get interrupt count and type supported
 */
int os_get_intr_config(pqisrc_softstate_t *softs)
{
	device_t dev;
	int msi_count = 0;
	int error = 0;
	int ret = PQI_STATUS_SUCCESS;
	dev = softs->os_specific.pqi_dev;

	DBG_FUNC("IN\n");

	msi_count = pci_msix_count(dev);

	if (msi_count > softs->num_cpus_online)
		msi_count = softs->num_cpus_online;
	if (msi_count > PQI_MAX_MSIX)
		msi_count = PQI_MAX_MSIX;
	if (msi_count == 0 || (error = pci_alloc_msix(dev, &msi_count)) != 0) {
		device_printf(dev, "alloc msix failed - msi_count=%d, err=%d; "
                                   "will try MSI\n", msi_count, error);
		pci_release_msi(dev);
	} else {
		softs->intr_count = msi_count;
		softs->intr_type = INTR_TYPE_MSIX;
		softs->os_specific.msi_enabled = TRUE;
		device_printf(dev, "using MSI-X interrupts (%d vectors)\n",
			msi_count);
	}
	if (!softs->intr_type) {
		msi_count = 1;
		if ((error = pci_alloc_msi(dev, &msi_count)) != 0) {
			device_printf(dev, "alloc msi failed - err=%d; "
				"will use INTx\n", error);
			pci_release_msi(dev);
		} else {
			softs->os_specific.msi_enabled = TRUE;
			softs->intr_count = msi_count;
			softs->intr_type = INTR_TYPE_MSI;
			device_printf(dev, "using MSI interrupts\n");
		}
	}

	if (!softs->intr_type) {
		device_printf(dev, "using legacy interrupts\n");
		softs->intr_type = INTR_TYPE_FIXED;
		softs->intr_count = 1;
	}

	if(!softs->intr_type) {
		DBG_FUNC("OUT failed\n");
		ret =  PQI_STATUS_FAILURE;
		return ret;
	}
	DBG_FUNC("OUT\n");
	return ret;
}

void os_eventtaskqueue_enqueue(pqisrc_softstate_t *sc)
{
	taskqueue_enqueue(taskqueue_swi, &sc->os_specific.event_task);
}

void pqisrc_event_worker(void *arg1, int arg2)
{
	pqisrc_ack_all_events(arg1);
}

/*
 * ithread routine to handle uniprocessor systems
 */
static void shared_ithread_routine(void *arg)
{
	pqi_intr_ctx_t *intr_ctx = (pqi_intr_ctx_t *)arg;
	pqisrc_softstate_t *softs = device_get_softc(intr_ctx->pqi_dev);
	int oq_id  = intr_ctx->oq_id;
	
	DBG_FUNC("IN\n");

	pqisrc_process_response_queue(softs, oq_id);
	pqisrc_process_event_intr_src(softs, oq_id - 1);

	DBG_FUNC("OUT\n");
}

/*
 * ithread routine to process non event response
 */
static void common_ithread_routine(void *arg)
{
	pqi_intr_ctx_t *intr_ctx = (pqi_intr_ctx_t *)arg;
	pqisrc_softstate_t *softs = device_get_softc(intr_ctx->pqi_dev);
	int oq_id  = intr_ctx->oq_id;

	DBG_FUNC("IN\n");
	
	pqisrc_process_response_queue(softs, oq_id);

	DBG_FUNC("OUT\n");
}

static void event_ithread_routine(void *arg)
{
	pqi_intr_ctx_t *intr_ctx = (pqi_intr_ctx_t *)arg;
	pqisrc_softstate_t *softs = device_get_softc(intr_ctx->pqi_dev);
	int oq_id  = intr_ctx->oq_id;

	DBG_FUNC("IN\n");

	pqisrc_process_event_intr_src(softs, oq_id);

	DBG_FUNC("OUT\n");
}

/*
 * Registration of legacy interrupt in case MSI is unsupported
 */
int register_legacy_intr(pqisrc_softstate_t *softs)
{
	int error = 0;
	device_t dev;

	DBG_FUNC("IN\n");

	dev = softs->os_specific.pqi_dev;

	softs->os_specific.pqi_irq_rid[0] = 0;
	softs->os_specific.pqi_irq[0] = bus_alloc_resource_any(dev, \
		SYS_RES_IRQ, &softs->os_specific.pqi_irq_rid[0],
		RF_ACTIVE | RF_SHAREABLE);
	if (NULL == softs->os_specific.pqi_irq[0]) {
		DBG_ERR("Failed to allocate resource for interrupt\n");
		return PQI_STATUS_FAILURE; 
	}
	if ((softs->os_specific.msi_ctx = os_mem_alloc(softs,sizeof(pqi_intr_ctx_t))) == NULL) {
		DBG_ERR("Failed to allocate memory for msi_ctx\n");
		return PQI_STATUS_FAILURE;
	}
	softs->os_specific.msi_ctx[0].pqi_dev = dev;
	softs->os_specific.msi_ctx[0].oq_id = 1;

	error = bus_setup_intr(dev, softs->os_specific.pqi_irq[0],
				INTR_TYPE_CAM | INTR_MPSAFE, \
				NULL, shared_ithread_routine,
				&softs->os_specific.msi_ctx[0], 
				&softs->os_specific.intrcookie[0]);
	if (error) {
		DBG_ERR("Failed to setup legacy interrupt err = %d\n", error);
		return error;
	}
	softs->os_specific.intr_registered[0] = TRUE;

	DBG_FUNC("OUT error = %d\n", error);

	return error;
}

/*
 * Registration of MSIx 
 */
int register_msix_intr(pqisrc_softstate_t *softs)
{
	int error = 0;
	int i = 0;
	device_t dev;
	dev = softs->os_specific.pqi_dev;
	int msix_count = softs->intr_count;

	DBG_FUNC("IN\n");

	softs->os_specific.msi_ctx = os_mem_alloc(softs, sizeof(pqi_intr_ctx_t) * msix_count);
	/*Add shared handler */
	if (softs->share_opq_and_eventq) {
		softs->os_specific.pqi_irq_rid[i] = i+1;
		softs->os_specific.pqi_irq[i] = bus_alloc_resource_any(dev, \
						SYS_RES_IRQ,
						&softs->os_specific.pqi_irq_rid[i],
						RF_SHAREABLE |  RF_ACTIVE);
		if (NULL == softs->os_specific.pqi_irq[i]) {
			DBG_ERR("Failed to allocate \
				event interrupt resource\n");
			return PQI_STATUS_FAILURE;
		}
				
		softs->os_specific.msi_ctx[i].pqi_dev = dev;
		softs->os_specific.msi_ctx[i].oq_id = i+1;
		
		error = bus_setup_intr(dev,softs->os_specific.pqi_irq[i],
					INTR_TYPE_CAM | INTR_MPSAFE,\
					NULL,
					shared_ithread_routine,
					&softs->os_specific.msi_ctx[i],
					&softs->os_specific.intrcookie[i]);

		if (error) {
			DBG_ERR("Failed to setup interrupt for events r=%d\n", 
				error);
			return error;
		}
		softs->os_specific.intr_registered[i] = TRUE;
	}
	else {
		/* Add event handler */
		softs->os_specific.pqi_irq_rid[i] = i+1;
		softs->os_specific.pqi_irq[i] = bus_alloc_resource_any(dev, \
						SYS_RES_IRQ,
						&softs->os_specific.pqi_irq_rid[i],
						RF_SHAREABLE |  RF_ACTIVE);
		if (NULL == softs->os_specific.pqi_irq[i]) {
			DBG_ERR("ERR : Failed to allocate \
				event interrupt resource\n");
			return PQI_STATUS_FAILURE;
		}
		
		
		softs->os_specific.msi_ctx[i].pqi_dev = dev;
		softs->os_specific.msi_ctx[i].oq_id = i;
		

		error = bus_setup_intr(dev,softs->os_specific.pqi_irq[i],
					INTR_TYPE_CAM | INTR_MPSAFE,\
                       			NULL,
					event_ithread_routine,
					&softs->os_specific.msi_ctx[i],
					&softs->os_specific.intrcookie[i]);
		if (error) {
			DBG_ERR("Failed to setup interrupt for events err=%d\n",
				error);
			return error;
		}
		softs->os_specific.intr_registered[i] = TRUE;
		/* Add interrupt handlers*/	
		for (i = 1; i < msix_count; ++i) {
			softs->os_specific.pqi_irq_rid[i] = i+1;
			softs->os_specific.pqi_irq[i] = \
					bus_alloc_resource_any(dev,
					SYS_RES_IRQ,
					&softs->os_specific.pqi_irq_rid[i],
					RF_SHAREABLE | RF_ACTIVE);
			if (NULL == softs->os_specific.pqi_irq[i]) {
				DBG_ERR("Failed to allocate \
					msi/x interrupt resource\n");
				return PQI_STATUS_FAILURE;
			}
			softs->os_specific.msi_ctx[i].pqi_dev = dev;
			softs->os_specific.msi_ctx[i].oq_id = i;
			error = bus_setup_intr(dev,
					softs->os_specific.pqi_irq[i],
					INTR_TYPE_CAM | INTR_MPSAFE,\
					NULL,
					common_ithread_routine,
					&softs->os_specific.msi_ctx[i],
					&softs->os_specific.intrcookie[i]);
			if (error) {
				DBG_ERR("Failed to setup \
					msi/x interrupt error = %d\n", error);
				return error;
			}
			softs->os_specific.intr_registered[i] = TRUE;
		}
	}

	DBG_FUNC("OUT error = %d\n", error);

	return error;
}

/*
 * Setup interrupt depending on the configuration
 */
int os_setup_intr(pqisrc_softstate_t *softs)
{
	int error = 0;

	DBG_FUNC("IN\n");

	if (softs->intr_type == INTR_TYPE_FIXED) {
		error = register_legacy_intr(softs);
	}
	else {
		error = register_msix_intr(softs);
	}
	if (error) {
		DBG_FUNC("OUT failed error = %d\n", error);
		return error;
	}

	DBG_FUNC("OUT error = %d\n", error);

	return error;
}

/*
 * Deregistration of legacy interrupt
 */
void deregister_pqi_intx(pqisrc_softstate_t *softs)
{
	device_t dev;

	DBG_FUNC("IN\n");

	dev = softs->os_specific.pqi_dev;
	if (softs->os_specific.pqi_irq[0] != NULL) {
		if (softs->os_specific.intr_registered[0]) {
			bus_teardown_intr(dev, softs->os_specific.pqi_irq[0],
					softs->os_specific.intrcookie[0]);
			softs->os_specific.intr_registered[0] = FALSE;
		}
		bus_release_resource(dev, SYS_RES_IRQ,
			softs->os_specific.pqi_irq_rid[0],
			softs->os_specific.pqi_irq[0]);
		softs->os_specific.pqi_irq[0] = NULL;
		os_mem_free(softs, (char*)softs->os_specific.msi_ctx, sizeof(pqi_intr_ctx_t));
	}

	DBG_FUNC("OUT\n");
}

/*
 * Deregistration of MSIx interrupt
 */
void deregister_pqi_msix(pqisrc_softstate_t *softs)
{
	device_t dev;
	dev = softs->os_specific.pqi_dev;
	int msix_count = softs->intr_count;
	int i = 0;

	DBG_FUNC("IN\n");
	
	os_mem_free(softs, (char*)softs->os_specific.msi_ctx, sizeof(pqi_intr_ctx_t) * msix_count);
	softs->os_specific.msi_ctx = NULL;

	for (; i < msix_count; ++i) {
		if (softs->os_specific.pqi_irq[i] != NULL) {
			if (softs->os_specific.intr_registered[i]) {
				bus_teardown_intr(dev,
					softs->os_specific.pqi_irq[i],
					softs->os_specific.intrcookie[i]);
				softs->os_specific.intr_registered[i] = FALSE;
			}
			bus_release_resource(dev, SYS_RES_IRQ,
				softs->os_specific.pqi_irq_rid[i],
			softs->os_specific.pqi_irq[i]);
			softs->os_specific.pqi_irq[i] = NULL;
		}
	}

	DBG_FUNC("OUT\n");
}

/*
 * Function to destroy interrupts registered
 */
int os_destroy_intr(pqisrc_softstate_t *softs)
{
	device_t dev;
	dev = softs->os_specific.pqi_dev;

	DBG_FUNC("IN\n");

	if (softs->intr_type == INTR_TYPE_FIXED) {
		deregister_pqi_intx(softs);
	} else if (softs->intr_type == INTR_TYPE_MSIX) {
		deregister_pqi_msix(softs);
	}
	if (softs->os_specific.msi_enabled) {
		pci_release_msi(dev);
		softs->os_specific.msi_enabled = FALSE;
	} 
	
	DBG_FUNC("OUT\n");

	return PQI_STATUS_SUCCESS;
}

/*
 * Free interrupt related resources for the adapter
 */
void os_free_intr_config(pqisrc_softstate_t *softs)
{
	device_t dev;
	dev = softs->os_specific.pqi_dev;

	DBG_FUNC("IN\n");

        if (softs->os_specific.msi_enabled) {
                pci_release_msi(dev);
                softs->os_specific.msi_enabled = FALSE;
        }

	DBG_FUNC("OUT\n");
}
