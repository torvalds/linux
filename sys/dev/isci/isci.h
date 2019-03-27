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
 *
 * $FreeBSD$
 */

#ifndef _ISCI_H
#define _ISCI_H

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/types.h>
#include <sys/malloc.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_sim.h>
#include <cam/cam_xpt_sim.h>

#include <dev/isci/environment.h>
#include <dev/isci/scil/intel_pci.h>

#include <dev/isci/scil/sci_types.h>
#include <dev/isci/scil/sci_object.h>
#include <dev/isci/scil/sci_status.h>
#include <dev/isci/scil/sci_pool.h>
#include <dev/isci/scil/sci_fast_list.h>

#include <dev/isci/scil/sci_controller_constants.h>

#include <dev/isci/scil/scic_controller.h>
#include <dev/isci/scil/scic_config_parameters.h>

#define DEVICE2SOFTC(dev) ((struct isci_softc *) device_get_softc(dev))

#define DEVICE_TIMEOUT 1000
#define SCI_MAX_TIMERS  32

#define ISCI_NUM_PCI_BARS  2
#define ISCI_MAX_LUN		 8

MALLOC_DECLARE(M_ISCI);

struct ISCI_TIMER {
	struct callout		callout;
	SCI_TIMER_CALLBACK_T	callback;
	void			*cookie;
	BOOL			is_started;
};

struct ISCI_REMOTE_DEVICE {
	uint32_t			index;
	struct ISCI_DOMAIN 		*domain;
	SCI_REMOTE_DEVICE_HANDLE_T	sci_object;
	BOOL				is_resetting;
	uint32_t			frozen_lun_mask;
	SCI_FAST_LIST_ELEMENT_T		pending_device_reset_element;

	/*
	 * This queue maintains CCBs that have been returned with
	 *  SCI_IO_FAILURE_INVALID_STATE from the SCI layer.  These CCBs
	 *  need to be retried, but we cannot return CAM_REQUEUE_REQ because
	 *  this status gets passed all the way back up to users of the pass(4)
	 *  interface and breaks things like smartctl.  So instead, we queue
	 *  these CCBs internally.
	 */
	TAILQ_HEAD(,ccb_hdr)		queued_ccbs;

	/*
	 * Marker denoting this remote device needs its first queued ccb to
	 *  be retried.
	 */
	BOOL				release_queued_ccb;

	/*
	 * Points to a CCB in the queue that is currently being processed by
	 *  SCIL.  This allows us to keep in flight CCBs in the queue so as to
	 *  maintain ordering (i.e. in case we retry an I/O and then find out
	 *  it needs to be retried again - it just keeps its same place in the
	 *  queue.
	 */
	union ccb *			queued_ccb_in_progress;
};

struct ISCI_DOMAIN {
	struct ISCI_CONTROLLER		*controller;
	SCI_DOMAIN_HANDLE_T		sci_object;
	uint8_t				index;
	struct ISCI_REMOTE_DEVICE	*da_remote_device;
};

struct ISCI_MEMORY
{
	bus_addr_t	physical_address;
	bus_dma_tag_t	dma_tag;
	bus_dmamap_t	dma_map;
	POINTER_UINT	virtual_address;
	uint32_t	size;
	int		error;
};

struct ISCI_INTERRUPT_INFO
{
	SCIC_CONTROLLER_HANDLER_METHODS_T 	*handlers;
	void					*interrupt_target_handle;
	struct resource				*res;
	int					rid;
	void					*tag;

};

struct ISCI_PHY
{
	struct cdev		*cdev_fault;
	struct cdev		*cdev_locate;
	SCI_CONTROLLER_HANDLE_T	handle;
	int			index;
	int			led_fault;
	int			led_locate;
};

struct ISCI_CONTROLLER
{
	struct isci_softc 	*isci;
	uint8_t			index;
	SCI_CONTROLLER_HANDLE_T	scif_controller_handle;
	struct ISCI_DOMAIN	domain[SCI_MAX_DOMAINS];
	BOOL			is_started;
	BOOL			has_been_scanned;
	uint32_t		initial_discovery_mask;
	BOOL			is_frozen;
	BOOL			release_queued_ccbs;
	BOOL			fail_on_task_timeout;
	uint8_t			*remote_device_memory;
	struct ISCI_MEMORY	cached_controller_memory;
	struct ISCI_MEMORY	uncached_controller_memory;
	struct ISCI_MEMORY	request_memory;
	bus_dma_tag_t		buffer_dma_tag;
	struct mtx		lock;
	struct cam_sim		*sim;
	struct cam_path		*path;
	struct ISCI_REMOTE_DEVICE *remote_device[SCI_MAX_REMOTE_DEVICES];
	void 			*timer_memory;
	SCIC_OEM_PARAMETERS_T	oem_parameters;
	uint32_t		oem_parameters_version;
	uint32_t		queue_depth;
	uint32_t		sim_queue_depth;
	SCI_FAST_LIST_T		pending_device_reset_list;
	struct ISCI_PHY		phys[SCI_MAX_PHYS];

	SCI_MEMORY_DESCRIPTOR_LIST_HANDLE_T mdl;

	SCI_POOL_CREATE(remote_device_pool, struct ISCI_REMOTE_DEVICE *, SCI_MAX_REMOTE_DEVICES);
	SCI_POOL_CREATE(request_pool, struct ISCI_REQUEST *, SCI_MAX_IO_REQUESTS);
	SCI_POOL_CREATE(timer_pool, struct ISCI_TIMER *, SCI_MAX_TIMERS);
	SCI_POOL_CREATE(unmap_buffer_pool, void *, SCI_MAX_REMOTE_DEVICES);
};

struct ISCI_REQUEST
{
	SCI_CONTROLLER_HANDLE_T		controller_handle;
	SCI_REMOTE_DEVICE_HANDLE_T	remote_device_handle;
	bus_dma_tag_t			dma_tag;
	bus_dmamap_t			dma_map;
	SCI_PHYSICAL_ADDRESS		physical_address;
	struct callout			timer;
};

struct ISCI_IO_REQUEST
{
	struct ISCI_REQUEST	parent;
	SCI_IO_REQUEST_HANDLE_T	sci_object;
	union ccb		*ccb;
	uint32_t		num_segments;
	uint32_t		current_sge_index;
	bus_dma_segment_t	*sge;
};

struct ISCI_TASK_REQUEST
{
	struct ISCI_REQUEST		parent;
	struct scsi_sense_data		sense_data;
	SCI_TASK_REQUEST_HANDLE_T	sci_object;
	union ccb			*ccb;

};

struct ISCI_PCI_BAR {

	bus_space_tag_t		bus_tag;
	bus_space_handle_t	bus_handle;
	int			resource_id;
	struct resource		*resource;

};

/*
 * One of these per allocated PCI device.
 */
struct isci_softc {

	struct ISCI_PCI_BAR			pci_bar[ISCI_NUM_PCI_BARS];
	struct ISCI_CONTROLLER			controllers[SCI_MAX_CONTROLLERS];
	SCI_LIBRARY_HANDLE_T			sci_library_handle;
	void *					sci_library_memory;
	SCIC_CONTROLLER_HANDLER_METHODS_T	handlers[4];
	struct ISCI_INTERRUPT_INFO		interrupt_info[4];
	uint32_t				controller_count;
	uint32_t				num_interrupts;
	uint32_t				coalesce_number;
	uint32_t				coalesce_timeout;
	device_t				device;
	SCI_PCI_COMMON_HEADER_T			pci_common_header;
	BOOL					oem_parameters_found;
	struct intr_config_hook			config_hook;
};

int isci_allocate_resources(device_t device);

int isci_allocate_dma_buffer(device_t device, struct ISCI_CONTROLLER *lock,
    struct ISCI_MEMORY *memory);

void isci_remote_device_reset(struct ISCI_REMOTE_DEVICE *remote_device,
    union ccb *ccb);

/**
 *  Returns the negotiated link rate (in KB/s) for the associated
 *	remote device.  Used to fill out bitrate field for GET_TRANS_SETTINGS.
 *	Will match the negotiated link rate for the lowest numbered local phy
 *	in the port/domain containing this remote device.
 */
uint32_t isci_remote_device_get_bitrate(
    struct ISCI_REMOTE_DEVICE *remote_device);

void isci_remote_device_freeze_lun_queue(
    struct ISCI_REMOTE_DEVICE *remote_device, lun_id_t lun);

void isci_remote_device_release_lun_queue(
    struct ISCI_REMOTE_DEVICE *remote_device, lun_id_t lun);

void isci_remote_device_release_device_queue(
    struct ISCI_REMOTE_DEVICE * remote_device);

void isci_request_construct(struct ISCI_REQUEST *request,
    SCI_CONTROLLER_HANDLE_T scif_controller_handle,
    bus_dma_tag_t io_buffer_dma_tag, bus_addr_t physical_address);

#define isci_io_request_get_max_io_size() \
	((SCI_MAX_SCATTER_GATHER_ELEMENTS - 1) * PAGE_SIZE)

#define isci_task_request_get_object_size() \
	(sizeof(struct ISCI_TASK_REQUEST) + scif_task_request_get_object_size())

#define isci_io_request_get_object_size() \
	(sizeof(struct ISCI_IO_REQUEST) + scif_io_request_get_object_size())

#define isci_request_get_object_size() \
	max( \
	    isci_task_request_get_object_size(), \
	    isci_io_request_get_object_size() \
	)


void isci_io_request_execute_scsi_io(union ccb *ccb,
    struct ISCI_CONTROLLER *controller);

#if __FreeBSD_version >= 900026
void isci_io_request_execute_smp_io(
    union ccb *ccb, struct ISCI_CONTROLLER *controller);
#endif

void isci_io_request_timeout(void *);

void isci_get_oem_parameters(struct isci_softc *isci);

void isci_io_request_complete(
    SCI_CONTROLLER_HANDLE_T scif_controller,
    SCI_REMOTE_DEVICE_HANDLE_T remote_device,
    struct ISCI_IO_REQUEST * isci_request, SCI_IO_STATUS completion_status);

void isci_task_request_complete(
    SCI_CONTROLLER_HANDLE_T scif_controller,
    SCI_REMOTE_DEVICE_HANDLE_T remote_device,
    SCI_TASK_REQUEST_HANDLE_T io_request, SCI_TASK_STATUS completion_status);

void isci_sysctl_initialize(struct isci_softc *isci);

void isci_controller_construct(struct ISCI_CONTROLLER *controller,
    struct isci_softc *isci);

SCI_STATUS isci_controller_initialize(struct ISCI_CONTROLLER *controller);

int isci_controller_allocate_memory(struct ISCI_CONTROLLER *controller);

void isci_controller_domain_discovery_complete(
    struct ISCI_CONTROLLER *isci_controller, struct ISCI_DOMAIN *isci_domain);

int isci_controller_attach_to_cam(struct ISCI_CONTROLLER *controller);

void isci_controller_start(void *controller);

void isci_controller_release_queued_ccbs(struct ISCI_CONTROLLER *controller);

void isci_domain_construct(struct ISCI_DOMAIN *domain, uint32_t domain_index,
    struct ISCI_CONTROLLER *controller);

void isci_interrupt_setup(struct isci_softc *isci);
void isci_interrupt_poll_handler(struct ISCI_CONTROLLER *controller);

void isci_log_message(uint32_t	verbosity, char *log_message_prefix,
    char *log_message, ...);

extern uint32_t g_isci_debug_level;

#endif /* #ifndef _ISCI_H */
