/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * BSD LICENSE
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <dev/isci/isci.h>

#include <sys/conf.h>
#include <sys/malloc.h>

#include <cam/cam_periph.h>
#include <cam/cam_xpt_periph.h>

#include <dev/isci/scil/sci_memory_descriptor_list.h>
#include <dev/isci/scil/sci_memory_descriptor_list_decorator.h>

#include <dev/isci/scil/scif_controller.h>
#include <dev/isci/scil/scif_library.h>
#include <dev/isci/scil/scif_io_request.h>
#include <dev/isci/scil/scif_task_request.h>
#include <dev/isci/scil/scif_remote_device.h>
#include <dev/isci/scil/scif_domain.h>
#include <dev/isci/scil/scif_user_callback.h>
#include <dev/isci/scil/scic_sgpio.h>

#include <dev/led/led.h>

void isci_action(struct cam_sim *sim, union ccb *ccb);
void isci_poll(struct cam_sim *sim);

#define ccb_sim_ptr sim_priv.entries[0].ptr

/**
 * @brief This user callback will inform the user that the controller has
 *        had a serious unexpected error.  The user should not the error,
 *        disable interrupts, and wait for current ongoing processing to
 *        complete.  Subsequently, the user should reset the controller.
 *
 * @param[in]  controller This parameter specifies the controller that had
 *                        an error.
 *
 * @return none
 */
void scif_cb_controller_error(SCI_CONTROLLER_HANDLE_T controller,
    SCI_CONTROLLER_ERROR error)
{

	isci_log_message(0, "ISCI", "scif_cb_controller_error: 0x%x\n",
	    error);
}

/**
 * @brief This user callback will inform the user that the controller has
 *        finished the start process.
 *
 * @param[in]  controller This parameter specifies the controller that was
 *             started.
 * @param[in]  completion_status This parameter specifies the results of
 *             the start operation.  SCI_SUCCESS indicates successful
 *             completion.
 *
 * @return none
 */
void scif_cb_controller_start_complete(SCI_CONTROLLER_HANDLE_T controller,
    SCI_STATUS completion_status)
{
	uint32_t index;
	struct ISCI_CONTROLLER *isci_controller = (struct ISCI_CONTROLLER *)
	    sci_object_get_association(controller);

	isci_controller->is_started = TRUE;

	/* Set bits for all domains.  We will clear them one-by-one once
	 *  the domains complete discovery, or return error when calling
	 *  scif_domain_discover.  Once all bits are clear, we will register
	 *  the controller with CAM.
	 */
	isci_controller->initial_discovery_mask = (1 << SCI_MAX_DOMAINS) - 1;

	for(index = 0; index < SCI_MAX_DOMAINS; index++) {
		SCI_STATUS status;
		SCI_DOMAIN_HANDLE_T domain =
		    isci_controller->domain[index].sci_object;

		status = scif_domain_discover(
			domain,
			scif_domain_get_suggested_discover_timeout(domain),
			DEVICE_TIMEOUT
		);

		if (status != SCI_SUCCESS)
		{
			isci_controller_domain_discovery_complete(
			    isci_controller, &isci_controller->domain[index]);
		}
	}
}

/**
 * @brief This user callback will inform the user that the controller has
 *        finished the stop process. Note, after user calls
 *        scif_controller_stop(), before user receives this controller stop
 *        complete callback, user should not expect any callback from
 *        framework, such like scif_cb_domain_change_notification().
 *
 * @param[in]  controller This parameter specifies the controller that was
 *             stopped.
 * @param[in]  completion_status This parameter specifies the results of
 *             the stop operation.  SCI_SUCCESS indicates successful
 *             completion.
 *
 * @return none
 */
void scif_cb_controller_stop_complete(SCI_CONTROLLER_HANDLE_T controller,
    SCI_STATUS completion_status)
{
	struct ISCI_CONTROLLER *isci_controller = (struct ISCI_CONTROLLER *)
	    sci_object_get_association(controller);

	isci_controller->is_started = FALSE;
}

static void
isci_single_map(void *arg, bus_dma_segment_t *seg, int nseg, int error)
{
	SCI_PHYSICAL_ADDRESS *phys_addr = arg;

	*phys_addr = seg[0].ds_addr;
}

/**
 * @brief This method will be invoked to allocate memory dynamically.
 *
 * @param[in]  controller This parameter represents the controller
 *             object for which to allocate memory.
 * @param[out] mde This parameter represents the memory descriptor to
 *             be filled in by the user that will reference the newly
 *             allocated memory.
 *
 * @return none
 */
void scif_cb_controller_allocate_memory(SCI_CONTROLLER_HANDLE_T controller,
    SCI_PHYSICAL_MEMORY_DESCRIPTOR_T *mde)
{
	struct ISCI_CONTROLLER *isci_controller = (struct ISCI_CONTROLLER *)
	    sci_object_get_association(controller);

	/*
	 * Note this routine is only used for buffers needed to translate
	 * SCSI UNMAP commands to ATA DSM commands for SATA disks.
	 *
	 * We first try to pull a buffer from the controller's pool, and only
	 * call contigmalloc if one isn't there.
	 */
	if (!sci_pool_empty(isci_controller->unmap_buffer_pool)) {
		sci_pool_get(isci_controller->unmap_buffer_pool,
		    mde->virtual_address);
	} else
		mde->virtual_address = contigmalloc(PAGE_SIZE,
		    M_ISCI, M_NOWAIT, 0, BUS_SPACE_MAXADDR,
		    mde->constant_memory_alignment, 0);

	if (mde->virtual_address != NULL)
		bus_dmamap_load(isci_controller->buffer_dma_tag,
		    NULL, mde->virtual_address, PAGE_SIZE,
		    isci_single_map, &mde->physical_address,
		    BUS_DMA_NOWAIT);
}

/**
 * @brief This method will be invoked to allocate memory dynamically.
 *
 * @param[in]  controller This parameter represents the controller
 *             object for which to allocate memory.
 * @param[out] mde This parameter represents the memory descriptor to
 *             be filled in by the user that will reference the newly
 *             allocated memory.
 *
 * @return none
 */
void scif_cb_controller_free_memory(SCI_CONTROLLER_HANDLE_T controller,
    SCI_PHYSICAL_MEMORY_DESCRIPTOR_T * mde)
{
	struct ISCI_CONTROLLER *isci_controller = (struct ISCI_CONTROLLER *)
	    sci_object_get_association(controller);

	/*
	 * Put the buffer back into the controller's buffer pool, rather
	 * than invoking configfree.  This helps reduce chance we won't
	 * have buffers available when system is under memory pressure.
	 */ 
	sci_pool_put(isci_controller->unmap_buffer_pool,
	    mde->virtual_address);
}

void isci_controller_construct(struct ISCI_CONTROLLER *controller,
    struct isci_softc *isci)
{
	SCI_CONTROLLER_HANDLE_T scif_controller_handle;

	scif_library_allocate_controller(isci->sci_library_handle,
	    &scif_controller_handle);

	scif_controller_construct(isci->sci_library_handle,
	    scif_controller_handle, NULL);

	controller->isci = isci;
	controller->scif_controller_handle = scif_controller_handle;

	/* This allows us to later use
	 *  sci_object_get_association(scif_controller_handle)
	 * inside of a callback routine to get our struct ISCI_CONTROLLER object
	 */
	sci_object_set_association(scif_controller_handle, (void *)controller);

	controller->is_started = FALSE;
	controller->is_frozen = FALSE;
	controller->release_queued_ccbs = FALSE;
	controller->sim = NULL;
	controller->initial_discovery_mask = 0;

	sci_fast_list_init(&controller->pending_device_reset_list);

	mtx_init(&controller->lock, "isci", NULL, MTX_DEF);

	uint32_t domain_index;

	for(domain_index = 0; domain_index < SCI_MAX_DOMAINS; domain_index++) {
		isci_domain_construct( &controller->domain[domain_index],
		    domain_index, controller);
	}

	controller->timer_memory = malloc(
	    sizeof(struct ISCI_TIMER) * SCI_MAX_TIMERS, M_ISCI,
	    M_NOWAIT | M_ZERO);

	sci_pool_initialize(controller->timer_pool);

	struct ISCI_TIMER *timer = (struct ISCI_TIMER *)
	    controller->timer_memory;

	for ( int i = 0; i < SCI_MAX_TIMERS; i++ ) {
		sci_pool_put(controller->timer_pool, timer++);
	}

	sci_pool_initialize(controller->unmap_buffer_pool);
}

static void isci_led_fault_func(void *priv, int onoff)
{
	struct ISCI_PHY *phy = priv;

	/* map onoff to the fault LED */
	phy->led_fault = onoff;
	scic_sgpio_update_led_state(phy->handle, 1 << phy->index, 
		phy->led_fault, phy->led_locate, 0);
}

static void isci_led_locate_func(void *priv, int onoff)
{
	struct ISCI_PHY *phy = priv;

	/* map onoff to the locate LED */
	phy->led_locate = onoff;
	scic_sgpio_update_led_state(phy->handle, 1 << phy->index, 
		phy->led_fault, phy->led_locate, 0);
}

SCI_STATUS isci_controller_initialize(struct ISCI_CONTROLLER *controller)
{
	SCIC_USER_PARAMETERS_T scic_user_parameters;
	SCI_CONTROLLER_HANDLE_T scic_controller_handle;
	char led_name[64];
	unsigned long tunable;
	uint32_t io_shortage;
	uint32_t fail_on_timeout;
	int i;

	scic_controller_handle =
	    scif_controller_get_scic_handle(controller->scif_controller_handle);

	if (controller->isci->oem_parameters_found == TRUE)
	{
		scic_oem_parameters_set(
		    scic_controller_handle,
		    &controller->oem_parameters,
		    (uint8_t)(controller->oem_parameters_version));
	}

	scic_user_parameters_get(scic_controller_handle, &scic_user_parameters);

	if (TUNABLE_ULONG_FETCH("hw.isci.no_outbound_task_timeout", &tunable))
		scic_user_parameters.sds1.no_outbound_task_timeout =
		    (uint8_t)tunable;

	if (TUNABLE_ULONG_FETCH("hw.isci.ssp_max_occupancy_timeout", &tunable))
		scic_user_parameters.sds1.ssp_max_occupancy_timeout =
		    (uint16_t)tunable;

	if (TUNABLE_ULONG_FETCH("hw.isci.stp_max_occupancy_timeout", &tunable))
		scic_user_parameters.sds1.stp_max_occupancy_timeout =
		    (uint16_t)tunable;

	if (TUNABLE_ULONG_FETCH("hw.isci.ssp_inactivity_timeout", &tunable))
		scic_user_parameters.sds1.ssp_inactivity_timeout =
		    (uint16_t)tunable;

	if (TUNABLE_ULONG_FETCH("hw.isci.stp_inactivity_timeout", &tunable))
		scic_user_parameters.sds1.stp_inactivity_timeout =
		    (uint16_t)tunable;

	if (TUNABLE_ULONG_FETCH("hw.isci.max_speed_generation", &tunable))
		for (i = 0; i < SCI_MAX_PHYS; i++)
			scic_user_parameters.sds1.phys[i].max_speed_generation =
			    (uint8_t)tunable;

	scic_user_parameters_set(scic_controller_handle, &scic_user_parameters);

	/* Scheduler bug in SCU requires SCIL to reserve some task contexts as a
	 *  a workaround - one per domain.
	 */
	controller->queue_depth = SCI_MAX_IO_REQUESTS - SCI_MAX_DOMAINS;

	if (TUNABLE_INT_FETCH("hw.isci.controller_queue_depth",
	    &controller->queue_depth)) {
		controller->queue_depth = max(1, min(controller->queue_depth,
		    SCI_MAX_IO_REQUESTS - SCI_MAX_DOMAINS));
	}

	/* Reserve one request so that we can ensure we have one available TC
	 *  to do internal device resets.
	 */
	controller->sim_queue_depth = controller->queue_depth - 1;

	/* Although we save one TC to do internal device resets, it is possible
	 *  we could end up using several TCs for simultaneous device resets
	 *  while at the same time having CAM fill our controller queue.  To
	 *  simulate this condition, and how our driver handles it, we can set
	 *  this io_shortage parameter, which will tell CAM that we have a
	 *  large queue depth than we really do.
	 */
	io_shortage = 0;
	TUNABLE_INT_FETCH("hw.isci.io_shortage", &io_shortage);
	controller->sim_queue_depth += io_shortage;

	fail_on_timeout = 1;
	TUNABLE_INT_FETCH("hw.isci.fail_on_task_timeout", &fail_on_timeout);
	controller->fail_on_task_timeout = fail_on_timeout;

	/* Attach to CAM using xpt_bus_register now, then immediately freeze
	 *  the simq.  It will get released later when initial domain discovery
	 *  is complete.
	 */
	controller->has_been_scanned = FALSE;
	mtx_lock(&controller->lock);
	isci_controller_attach_to_cam(controller);
	xpt_freeze_simq(controller->sim, 1);
	mtx_unlock(&controller->lock);

	for (i = 0; i < SCI_MAX_PHYS; i++) {
		controller->phys[i].handle = scic_controller_handle;
		controller->phys[i].index = i;

		/* fault */
		controller->phys[i].led_fault = 0;
		sprintf(led_name, "isci.bus%d.port%d.fault", controller->index, i);
		controller->phys[i].cdev_fault = led_create(isci_led_fault_func,
		    &controller->phys[i], led_name);
			
		/* locate */
		controller->phys[i].led_locate = 0;
		sprintf(led_name, "isci.bus%d.port%d.locate", controller->index, i);
		controller->phys[i].cdev_locate = led_create(isci_led_locate_func,
		    &controller->phys[i], led_name);
	}

	return (scif_controller_initialize(controller->scif_controller_handle));
}

int isci_controller_allocate_memory(struct ISCI_CONTROLLER *controller)
{
	int error;
	device_t device =  controller->isci->device;
	uint32_t max_segment_size = isci_io_request_get_max_io_size();
	uint32_t status = 0;
	struct ISCI_MEMORY *uncached_controller_memory =
	    &controller->uncached_controller_memory;
	struct ISCI_MEMORY *cached_controller_memory =
	    &controller->cached_controller_memory;
	struct ISCI_MEMORY *request_memory =
	    &controller->request_memory;
	POINTER_UINT virtual_address;
	bus_addr_t physical_address;

	controller->mdl = sci_controller_get_memory_descriptor_list_handle(
	    controller->scif_controller_handle);

	uncached_controller_memory->size = sci_mdl_decorator_get_memory_size(
	    controller->mdl, SCI_MDE_ATTRIBUTE_PHYSICALLY_CONTIGUOUS);

	error = isci_allocate_dma_buffer(device, controller,
	    uncached_controller_memory);

	if (error != 0)
	    return (error);

	sci_mdl_decorator_assign_memory( controller->mdl,
	    SCI_MDE_ATTRIBUTE_PHYSICALLY_CONTIGUOUS,
	    uncached_controller_memory->virtual_address,
	    uncached_controller_memory->physical_address);

	cached_controller_memory->size = sci_mdl_decorator_get_memory_size(
	    controller->mdl,
	    SCI_MDE_ATTRIBUTE_CACHEABLE | SCI_MDE_ATTRIBUTE_PHYSICALLY_CONTIGUOUS
	);

	error = isci_allocate_dma_buffer(device, controller,
	    cached_controller_memory);

	if (error != 0)
	    return (error);

	sci_mdl_decorator_assign_memory(controller->mdl,
	    SCI_MDE_ATTRIBUTE_CACHEABLE | SCI_MDE_ATTRIBUTE_PHYSICALLY_CONTIGUOUS,
	    cached_controller_memory->virtual_address,
	    cached_controller_memory->physical_address);

	request_memory->size =
	    controller->queue_depth * isci_io_request_get_object_size();

	error = isci_allocate_dma_buffer(device, controller, request_memory);

	if (error != 0)
	    return (error);

	/* For STP PIO testing, we want to ensure we can force multiple SGLs
	 *  since this has been a problem area in SCIL.  This tunable parameter
	 *  will allow us to force DMA segments to a smaller size, ensuring
	 *  that even if a physically contiguous buffer is attached to this
	 *  I/O, the DMA subsystem will pass us multiple segments in our DMA
	 *  load callback.
	 */
	TUNABLE_INT_FETCH("hw.isci.max_segment_size", &max_segment_size);

	/* Create DMA tag for our I/O requests.  Then we can create DMA maps based off
	 *  of this tag and store them in each of our ISCI_IO_REQUEST objects.  This
	 *  will enable better performance than creating the DMA maps every time we get
	 *  an I/O.
	 */
	status = bus_dma_tag_create(bus_get_dma_tag(device), 0x1, 0x0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL,
	    isci_io_request_get_max_io_size(),
	    SCI_MAX_SCATTER_GATHER_ELEMENTS, max_segment_size, 0,
	    busdma_lock_mutex, &controller->lock,
	    &controller->buffer_dma_tag);

	sci_pool_initialize(controller->request_pool);

	virtual_address = request_memory->virtual_address;
	physical_address = request_memory->physical_address;

	for (int i = 0; i < controller->queue_depth; i++) {
		struct ISCI_REQUEST *request =
		    (struct ISCI_REQUEST *)virtual_address;

		isci_request_construct(request,
		    controller->scif_controller_handle,
		    controller->buffer_dma_tag, physical_address);

		sci_pool_put(controller->request_pool, request);

		virtual_address += isci_request_get_object_size();
		physical_address += isci_request_get_object_size();
	}

	uint32_t remote_device_size = sizeof(struct ISCI_REMOTE_DEVICE) +
	    scif_remote_device_get_object_size();

	controller->remote_device_memory = (uint8_t *) malloc(
	    remote_device_size * SCI_MAX_REMOTE_DEVICES, M_ISCI,
	    M_NOWAIT | M_ZERO);

	sci_pool_initialize(controller->remote_device_pool);

	uint8_t *remote_device_memory_ptr = controller->remote_device_memory;

	for (int i = 0; i < SCI_MAX_REMOTE_DEVICES; i++) {
		struct ISCI_REMOTE_DEVICE *remote_device =
		    (struct ISCI_REMOTE_DEVICE *)remote_device_memory_ptr;

		controller->remote_device[i] = NULL;
		remote_device->index = i;
		remote_device->is_resetting = FALSE;
		remote_device->frozen_lun_mask = 0;
		sci_fast_list_element_init(remote_device,
		    &remote_device->pending_device_reset_element);
		TAILQ_INIT(&remote_device->queued_ccbs);
		remote_device->release_queued_ccb = FALSE;
		remote_device->queued_ccb_in_progress = NULL;

		/*
		 * For the first SCI_MAX_DOMAINS device objects, do not put
		 *  them in the pool, rather assign them to each domain.  This
		 *  ensures that any device attached directly to port "i" will
		 *  always get CAM target id "i".
		 */
		if (i < SCI_MAX_DOMAINS)
			controller->domain[i].da_remote_device = remote_device;
		else
			sci_pool_put(controller->remote_device_pool,
			    remote_device);
		remote_device_memory_ptr += remote_device_size;
	}

	return (0);
}

void isci_controller_start(void *controller_handle)
{
	struct ISCI_CONTROLLER *controller =
	    (struct ISCI_CONTROLLER *)controller_handle;
	SCI_CONTROLLER_HANDLE_T scif_controller_handle =
	    controller->scif_controller_handle;

	scif_controller_start(scif_controller_handle,
	    scif_controller_get_suggested_start_timeout(scif_controller_handle));

	scic_controller_enable_interrupts(
	    scif_controller_get_scic_handle(controller->scif_controller_handle));
}

void isci_controller_domain_discovery_complete(
    struct ISCI_CONTROLLER *isci_controller, struct ISCI_DOMAIN *isci_domain)
{
	if (!isci_controller->has_been_scanned)
	{
		/* Controller has not been scanned yet.  We'll clear
		 *  the discovery bit for this domain, then check if all bits
		 *  are now clear.  That would indicate that all domains are
		 *  done with discovery and we can then proceed with initial
		 *  scan.
		 */

		isci_controller->initial_discovery_mask &=
		    ~(1 << isci_domain->index);

		if (isci_controller->initial_discovery_mask == 0) {
			struct isci_softc *driver = isci_controller->isci;
			uint8_t next_index = isci_controller->index + 1;

			isci_controller->has_been_scanned = TRUE;

			/* Unfreeze simq to allow initial scan to proceed. */
			xpt_release_simq(isci_controller->sim, TRUE);

#if __FreeBSD_version < 800000
			/* When driver is loaded after boot, we need to
			 *  explicitly rescan here for versions <8.0, because
			 *  CAM only automatically scans new buses at boot
			 *  time.
			 */
			union ccb *ccb = xpt_alloc_ccb_nowait();

			xpt_create_path(&ccb->ccb_h.path, NULL,
			    cam_sim_path(isci_controller->sim),
			    CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD);

			xpt_rescan(ccb);
#endif

			if (next_index < driver->controller_count) {
				/*  There are more controllers that need to
				 *   start.  So start the next one.
				 */
				isci_controller_start(
				    &driver->controllers[next_index]);
			}
			else
			{
				/* All controllers have been started and completed discovery.
				 *  Disestablish the config hook while will signal to the
				 *  kernel during boot that it is safe to try to find and
				 *  mount the root partition.
				 */
				config_intrhook_disestablish(
				    &driver->config_hook);
			}
		}
	}
}

int isci_controller_attach_to_cam(struct ISCI_CONTROLLER *controller)
{
	struct isci_softc *isci = controller->isci;
	device_t parent = device_get_parent(isci->device);
	int unit = device_get_unit(isci->device);
	struct cam_devq *isci_devq = cam_simq_alloc(controller->sim_queue_depth);

	if(isci_devq == NULL) {
		isci_log_message(0, "ISCI", "isci_devq is NULL \n");
		return (-1);
	}

	controller->sim = cam_sim_alloc(isci_action, isci_poll, "isci",
	    controller, unit, &controller->lock, controller->sim_queue_depth,
	    controller->sim_queue_depth, isci_devq);

	if(controller->sim == NULL) {
		isci_log_message(0, "ISCI", "cam_sim_alloc... fails\n");
		cam_simq_free(isci_devq);
		return (-1);
	}

	if(xpt_bus_register(controller->sim, parent, controller->index)
	    != CAM_SUCCESS) {
		isci_log_message(0, "ISCI", "xpt_bus_register...fails \n");
		cam_sim_free(controller->sim, TRUE);
		mtx_unlock(&controller->lock);
		return (-1);
	}

	if(xpt_create_path(&controller->path, NULL,
	    cam_sim_path(controller->sim), CAM_TARGET_WILDCARD,
	    CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
		isci_log_message(0, "ISCI", "xpt_create_path....fails\n");
		xpt_bus_deregister(cam_sim_path(controller->sim));
		cam_sim_free(controller->sim, TRUE);
		mtx_unlock(&controller->lock);
		return (-1);
	}

	return (0);
}

void isci_poll(struct cam_sim *sim)
{
	struct ISCI_CONTROLLER *controller =
	    (struct ISCI_CONTROLLER *)cam_sim_softc(sim);

	isci_interrupt_poll_handler(controller);
}

void isci_action(struct cam_sim *sim, union ccb *ccb)
{
	struct ISCI_CONTROLLER *controller =
	    (struct ISCI_CONTROLLER *)cam_sim_softc(sim);

	switch ( ccb->ccb_h.func_code ) {
	case XPT_PATH_INQ:
		{
			struct ccb_pathinq *cpi = &ccb->cpi;
			int bus = cam_sim_bus(sim);
			ccb->ccb_h.ccb_sim_ptr = sim;
			cpi->version_num = 1;
			cpi->hba_inquiry = PI_TAG_ABLE;
			cpi->target_sprt = 0;
			cpi->hba_misc = PIM_NOBUSRESET | PIM_SEQSCAN |
			    PIM_UNMAPPED;
			cpi->hba_eng_cnt = 0;
			cpi->max_target = SCI_MAX_REMOTE_DEVICES - 1;
			cpi->max_lun = ISCI_MAX_LUN;
#if __FreeBSD_version >= 800102
			cpi->maxio = isci_io_request_get_max_io_size();
#endif
			cpi->unit_number = cam_sim_unit(sim);
			cpi->bus_id = bus;
			cpi->initiator_id = SCI_MAX_REMOTE_DEVICES;
			cpi->base_transfer_speed = 300000;
			strlcpy(cpi->sim_vid, "FreeBSD", SIM_IDLEN);
			strlcpy(cpi->hba_vid, "Intel Corp.", HBA_IDLEN);
			strlcpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
			cpi->transport = XPORT_SAS;
			cpi->transport_version = 0;
			cpi->protocol = PROTO_SCSI;
			cpi->protocol_version = SCSI_REV_SPC2;
			cpi->ccb_h.status = CAM_REQ_CMP;
			xpt_done(ccb);
		}
		break;
	case XPT_GET_TRAN_SETTINGS:
		{
			struct ccb_trans_settings *general_settings = &ccb->cts;
			struct ccb_trans_settings_sas *sas_settings =
			    &general_settings->xport_specific.sas;
			struct ccb_trans_settings_scsi *scsi_settings =
			    &general_settings->proto_specific.scsi;
			struct ISCI_REMOTE_DEVICE *remote_device;

			remote_device = controller->remote_device[ccb->ccb_h.target_id];

			if (remote_device == NULL) {
				ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
				ccb->ccb_h.status &= ~CAM_STATUS_MASK;
				ccb->ccb_h.status |= CAM_DEV_NOT_THERE;
				xpt_done(ccb);
				break;
			}

			general_settings->protocol = PROTO_SCSI;
			general_settings->transport = XPORT_SAS;
			general_settings->protocol_version = SCSI_REV_SPC2;
			general_settings->transport_version = 0;
			scsi_settings->valid = CTS_SCSI_VALID_TQ;
			scsi_settings->flags = CTS_SCSI_FLAGS_TAG_ENB;
			ccb->ccb_h.status &= ~CAM_STATUS_MASK;
			ccb->ccb_h.status |= CAM_REQ_CMP;

			sas_settings->bitrate =
			    isci_remote_device_get_bitrate(remote_device);

			if (sas_settings->bitrate != 0)
				sas_settings->valid = CTS_SAS_VALID_SPEED;

			xpt_done(ccb);
		}
		break;
	case XPT_SCSI_IO:
		if (ccb->ccb_h.flags & CAM_CDB_PHYS) {
			ccb->ccb_h.status = CAM_REQ_INVALID;
			xpt_done(ccb);
			break;
		}
		isci_io_request_execute_scsi_io(ccb, controller);
		break;
#if __FreeBSD_version >= 900026
	case XPT_SMP_IO:
		isci_io_request_execute_smp_io(ccb, controller);
		break;
#endif
	case XPT_SET_TRAN_SETTINGS:
		ccb->ccb_h.status &= ~CAM_STATUS_MASK;
		ccb->ccb_h.status |= CAM_REQ_CMP;
		xpt_done(ccb);
		break;
	case XPT_CALC_GEOMETRY:
		cam_calc_geometry(&ccb->ccg, /*extended*/1);
		xpt_done(ccb);
		break;
	case XPT_RESET_DEV:
		{
			struct ISCI_REMOTE_DEVICE *remote_device =
			    controller->remote_device[ccb->ccb_h.target_id];

			if (remote_device != NULL)
				isci_remote_device_reset(remote_device, ccb);
			else {
				ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
				ccb->ccb_h.status &= ~CAM_STATUS_MASK;
				ccb->ccb_h.status |= CAM_DEV_NOT_THERE;
				xpt_done(ccb);
			}
		}
		break;
	case XPT_RESET_BUS:
		ccb->ccb_h.status = CAM_REQ_CMP;
		xpt_done(ccb);
		break;
	default:
		isci_log_message(0, "ISCI", "Unhandled func_code 0x%x\n",
		    ccb->ccb_h.func_code);
		ccb->ccb_h.status &= ~CAM_SIM_QUEUED;
		ccb->ccb_h.status &= ~CAM_STATUS_MASK;
		ccb->ccb_h.status |= CAM_REQ_INVALID;
		xpt_done(ccb);
		break;
	}
}

/*
 * Unfortunately, SCIL doesn't cleanly handle retry conditions.
 *  CAM_REQUEUE_REQ works only when no one is using the pass(4) interface.  So
 *  when SCIL denotes an I/O needs to be retried (typically because of mixing
 *  tagged/non-tagged ATA commands, or running out of NCQ slots), we queue
 *  these I/O internally.  Once SCIL completes an I/O to this device, or we get
 *  a ready notification, we will retry the first I/O on the queue.
 *  Unfortunately, SCIL also doesn't cleanly handle starting the new I/O within
 *  the context of the completion handler, so we need to retry these I/O after
 *  the completion handler is done executing.
 */
void
isci_controller_release_queued_ccbs(struct ISCI_CONTROLLER *controller)
{
	struct ISCI_REMOTE_DEVICE *dev;
	struct ccb_hdr *ccb_h;
	uint8_t *ptr;
	int dev_idx;

	KASSERT(mtx_owned(&controller->lock), ("controller lock not owned"));

	controller->release_queued_ccbs = FALSE;
	for (dev_idx = 0;
	     dev_idx < SCI_MAX_REMOTE_DEVICES;
	     dev_idx++) {

		dev = controller->remote_device[dev_idx];
		if (dev != NULL &&
		    dev->release_queued_ccb == TRUE &&
		    dev->queued_ccb_in_progress == NULL) {
			dev->release_queued_ccb = FALSE;
			ccb_h = TAILQ_FIRST(&dev->queued_ccbs);

			if (ccb_h == NULL)
				continue;

			ptr = scsiio_cdb_ptr(&((union ccb *)ccb_h)->csio);
			isci_log_message(1, "ISCI", "release %p %x\n", ccb_h, *ptr);

			dev->queued_ccb_in_progress = (union ccb *)ccb_h;
			isci_io_request_execute_scsi_io(
			    (union ccb *)ccb_h, controller);
		}
	}
}
