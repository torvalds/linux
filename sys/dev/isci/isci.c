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

#include <sys/sysctl.h>
#include <sys/malloc.h>

#include <cam/cam_periph.h>

#include <dev/led/led.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/isci/scil/scic_logger.h>
#include <dev/isci/scil/scic_library.h>
#include <dev/isci/scil/scic_sgpio.h>
#include <dev/isci/scil/scic_user_callback.h>

#include <dev/isci/scil/scif_controller.h>
#include <dev/isci/scil/scif_library.h>
#include <dev/isci/scil/scif_logger.h>
#include <dev/isci/scil/scif_user_callback.h>

MALLOC_DEFINE(M_ISCI, "isci", "isci driver memory allocations");

struct isci_softc *g_isci;
uint32_t g_isci_debug_level = 0;

static int isci_probe(device_t);
static int isci_attach(device_t);
static int isci_detach(device_t);

int isci_initialize(struct isci_softc *isci);

void isci_allocate_dma_buffer_callback(void *arg, bus_dma_segment_t *seg,
    int nseg, int error);

static devclass_t isci_devclass;

static device_method_t isci_pci_methods[] = {
	 /* Device interface */
	 DEVMETHOD(device_probe,  isci_probe),
	 DEVMETHOD(device_attach, isci_attach),
	 DEVMETHOD(device_detach, isci_detach),
	 { 0, 0 }
};

static driver_t isci_pci_driver = {
	 "isci",
	 isci_pci_methods,
	 sizeof(struct isci_softc),
};

DRIVER_MODULE(isci, pci, isci_pci_driver, isci_devclass, 0, 0);
MODULE_DEPEND(isci, cam, 1, 1, 1);

static struct _pcsid
{
	 u_int32_t	type;
	 const char	*desc;
} pci_ids[] = {
	 { 0x1d608086,	"Intel(R) C600 Series Chipset SAS Controller"  },
	 { 0x1d618086,	"Intel(R) C600 Series Chipset SAS Controller (SATA mode)"  },
	 { 0x1d628086,	"Intel(R) C600 Series Chipset SAS Controller"  },
	 { 0x1d638086,	"Intel(R) C600 Series Chipset SAS Controller"  },
	 { 0x1d648086,	"Intel(R) C600 Series Chipset SAS Controller"  },
	 { 0x1d658086,	"Intel(R) C600 Series Chipset SAS Controller"  },
	 { 0x1d668086,	"Intel(R) C600 Series Chipset SAS Controller"  },
	 { 0x1d678086,	"Intel(R) C600 Series Chipset SAS Controller"  },
	 { 0x1d688086,	"Intel(R) C600 Series Chipset SAS Controller"  },
	 { 0x1d698086,	"Intel(R) C600 Series Chipset SAS Controller"  },
	 { 0x1d6a8086,	"Intel(R) C600 Series Chipset SAS Controller (SATA mode)"  },
	 { 0x1d6b8086,  "Intel(R) C600 Series Chipset SAS Controller (SATA mode)"  },
	 { 0x1d6c8086,	"Intel(R) C600 Series Chipset SAS Controller"  },
	 { 0x1d6d8086,	"Intel(R) C600 Series Chipset SAS Controller"  },
	 { 0x1d6e8086,	"Intel(R) C600 Series Chipset SAS Controller"  },
	 { 0x1d6f8086,	"Intel(R) C600 Series Chipset SAS Controller (SATA mode)"  },
	 { 0x00000000,	NULL				}
};

static int
isci_probe (device_t device)
{
	u_int32_t	type = pci_get_devid(device);
	struct _pcsid	*ep = pci_ids;

	while (ep->type && ep->type != type)
		++ep;

	if (ep->desc)
	{
		device_set_desc(device, ep->desc);
		return (BUS_PROBE_DEFAULT);
	}
	else
		return (ENXIO);
}

static int
isci_allocate_pci_memory(struct isci_softc *isci)
{
	int i;

	for (i = 0; i < ISCI_NUM_PCI_BARS; i++)
	{
		struct ISCI_PCI_BAR *pci_bar = &isci->pci_bar[i];

		pci_bar->resource_id = PCIR_BAR(i*2);
		pci_bar->resource = bus_alloc_resource_any(isci->device,
		    SYS_RES_MEMORY, &pci_bar->resource_id,
		    RF_ACTIVE);

		if(pci_bar->resource == NULL)
			isci_log_message(0, "ISCI",
			    "unable to allocate pci resource\n");
		else {
			pci_bar->bus_tag = rman_get_bustag(pci_bar->resource);
			pci_bar->bus_handle =
			    rman_get_bushandle(pci_bar->resource);
		}
	}

	return (0);
}

static int
isci_attach(device_t device)
{
	int error;
	struct isci_softc *isci = DEVICE2SOFTC(device);

	g_isci = isci;
	isci->device = device;
	pci_enable_busmaster(device);

	isci_allocate_pci_memory(isci);

	error = isci_initialize(isci);

	if (error)
	{
		isci_detach(device);
		return (error);
	}

	isci_interrupt_setup(isci);
	isci_sysctl_initialize(isci);

	return (0);
}

static int
isci_detach(device_t device)
{
	struct isci_softc *isci = DEVICE2SOFTC(device);
	int i, phy;

	for (i = 0; i < isci->controller_count; i++) {
		struct ISCI_CONTROLLER *controller = &isci->controllers[i];
		SCI_STATUS status;
		void *unmap_buffer;

		if (controller->scif_controller_handle != NULL) {
			scic_controller_disable_interrupts(
			    scif_controller_get_scic_handle(controller->scif_controller_handle));

			mtx_lock(&controller->lock);
			status = scif_controller_stop(controller->scif_controller_handle, 0);
			mtx_unlock(&controller->lock);

			while (controller->is_started == TRUE) {
				/* Now poll for interrupts until the controller stop complete
				 *  callback is received.
				 */
				mtx_lock(&controller->lock);
				isci_interrupt_poll_handler(controller);
				mtx_unlock(&controller->lock);
				pause("isci", 1);
			}

			if(controller->sim != NULL) {
				mtx_lock(&controller->lock);
				xpt_free_path(controller->path);
				xpt_bus_deregister(cam_sim_path(controller->sim));
				cam_sim_free(controller->sim, TRUE);
				mtx_unlock(&controller->lock);
			}
		}

		if (controller->timer_memory != NULL)
			free(controller->timer_memory, M_ISCI);

		if (controller->remote_device_memory != NULL)
			free(controller->remote_device_memory, M_ISCI);

		for (phy = 0; phy < SCI_MAX_PHYS; phy++) {
			if (controller->phys[phy].cdev_fault)
				led_destroy(controller->phys[phy].cdev_fault);

			if (controller->phys[phy].cdev_locate)
				led_destroy(controller->phys[phy].cdev_locate);
		}

		while (1) {
			sci_pool_get(controller->unmap_buffer_pool, unmap_buffer);
			if (unmap_buffer == NULL)
				break;
			contigfree(unmap_buffer, PAGE_SIZE, M_ISCI);
		}
	}

	/* The SCIF controllers have been stopped, so we can now
	 *  free the SCI library memory.
	 */
	if (isci->sci_library_memory != NULL)
		free(isci->sci_library_memory, M_ISCI);

	for (i = 0; i < ISCI_NUM_PCI_BARS; i++)
	{
		struct ISCI_PCI_BAR *pci_bar = &isci->pci_bar[i];

		if (pci_bar->resource != NULL)
			bus_release_resource(device, SYS_RES_MEMORY,
			    pci_bar->resource_id, pci_bar->resource);
	}

	for (i = 0; i < isci->num_interrupts; i++)
	{
		struct ISCI_INTERRUPT_INFO *interrupt_info;

		interrupt_info = &isci->interrupt_info[i];

		if(interrupt_info->tag != NULL)
			bus_teardown_intr(device, interrupt_info->res,
			    interrupt_info->tag);

		if(interrupt_info->res != NULL)
			bus_release_resource(device, SYS_RES_IRQ,
			    rman_get_rid(interrupt_info->res),
			    interrupt_info->res);

		pci_release_msi(device);
	}
	pci_disable_busmaster(device);

	return (0);
}

int
isci_initialize(struct isci_softc *isci)
{
	int error;
	uint32_t status = 0;
	uint32_t library_object_size;
	uint32_t verbosity_mask;
	uint32_t scic_log_object_mask;
	uint32_t scif_log_object_mask;
	uint8_t *header_buffer;

	library_object_size = scif_library_get_object_size(SCI_MAX_CONTROLLERS);

	isci->sci_library_memory =
	    malloc(library_object_size, M_ISCI, M_NOWAIT | M_ZERO );

	isci->sci_library_handle = scif_library_construct(
	    isci->sci_library_memory, SCI_MAX_CONTROLLERS);

	sci_object_set_association( isci->sci_library_handle, (void *)isci);

	verbosity_mask = (1<<SCI_LOG_VERBOSITY_ERROR) |
	    (1<<SCI_LOG_VERBOSITY_WARNING) | (1<<SCI_LOG_VERBOSITY_INFO) |
	    (1<<SCI_LOG_VERBOSITY_TRACE);

	scic_log_object_mask = 0xFFFFFFFF;
	scic_log_object_mask &= ~SCIC_LOG_OBJECT_COMPLETION_QUEUE;
	scic_log_object_mask &= ~SCIC_LOG_OBJECT_SSP_IO_REQUEST;
	scic_log_object_mask &= ~SCIC_LOG_OBJECT_STP_IO_REQUEST;
	scic_log_object_mask &= ~SCIC_LOG_OBJECT_SMP_IO_REQUEST;
	scic_log_object_mask &= ~SCIC_LOG_OBJECT_CONTROLLER;

	scif_log_object_mask = 0xFFFFFFFF;
	scif_log_object_mask &= ~SCIF_LOG_OBJECT_CONTROLLER;
	scif_log_object_mask &= ~SCIF_LOG_OBJECT_IO_REQUEST;

	TUNABLE_INT_FETCH("hw.isci.debug_level", &g_isci_debug_level);

	sci_logger_enable(sci_object_get_logger(isci->sci_library_handle),
	    scif_log_object_mask, verbosity_mask);

	sci_logger_enable(sci_object_get_logger(
	    scif_library_get_scic_handle(isci->sci_library_handle)),
	    scic_log_object_mask, verbosity_mask);

	header_buffer = (uint8_t *)&isci->pci_common_header;
	for (uint8_t i = 0; i < sizeof(isci->pci_common_header); i++)
		header_buffer[i] = pci_read_config(isci->device, i, 1);

	scic_library_set_pci_info(
	    scif_library_get_scic_handle(isci->sci_library_handle),
	    &isci->pci_common_header);

	isci->oem_parameters_found = FALSE;

	isci_get_oem_parameters(isci);

	/* trigger interrupt if 32 completions occur before timeout expires */
	isci->coalesce_number = 32;

	/* trigger interrupt if 2 microseconds elapse after a completion occurs,
	 *  regardless if "coalesce_number" completions have occurred
	 */
	isci->coalesce_timeout = 2;

	isci->controller_count = scic_library_get_pci_device_controller_count(
	    scif_library_get_scic_handle(isci->sci_library_handle));

	for (int index = 0; index < isci->controller_count; index++) {
		struct ISCI_CONTROLLER *controller = &isci->controllers[index];
		SCI_CONTROLLER_HANDLE_T scif_controller_handle;

		controller->index = index;
		isci_controller_construct(controller, isci);

		scif_controller_handle = controller->scif_controller_handle;

		status = isci_controller_initialize(controller);

		if(status != SCI_SUCCESS) {
			isci_log_message(0, "ISCI",
			    "isci_controller_initialize FAILED: %x\n",
			    status);
			return (status);
		}

		error = isci_controller_allocate_memory(controller);

		if (error != 0)
			return (error);

		scif_controller_set_interrupt_coalescence(
		    scif_controller_handle, isci->coalesce_number,
		    isci->coalesce_timeout);
	}

	/* FreeBSD provides us a hook to ensure we get a chance to start
	 *  our controllers and complete initial domain discovery before
	 *  it searches for the boot device.  Once we're done, we'll
	 *  disestablish the hook, signaling the kernel that is can proceed
	 *  with the boot process.
	 */
	isci->config_hook.ich_func = &isci_controller_start;
	isci->config_hook.ich_arg = &isci->controllers[0];

	if (config_intrhook_establish(&isci->config_hook) != 0)
		isci_log_message(0, "ISCI",
		    "config_intrhook_establish failed!\n");

	return (status);
}

void
isci_allocate_dma_buffer_callback(void *arg, bus_dma_segment_t *seg,
    int nseg, int error)
{
	struct ISCI_MEMORY *memory = (struct ISCI_MEMORY *)arg;

	memory->error = error;

	if (nseg != 1 || error != 0)
		isci_log_message(0, "ISCI",
		    "Failed to allocate physically contiguous memory!\n");
	else
		memory->physical_address = seg->ds_addr;
}

int
isci_allocate_dma_buffer(device_t device, struct ISCI_CONTROLLER *controller,
    struct ISCI_MEMORY *memory)
{
	uint32_t status;

	status = bus_dma_tag_create(bus_get_dma_tag(device),
	    0x40 /* cacheline alignment */, 0x0, BUS_SPACE_MAXADDR,
	    BUS_SPACE_MAXADDR, NULL, NULL, memory->size,
	    0x1 /* we want physically contiguous */,
	    memory->size, 0, busdma_lock_mutex, &controller->lock,
	    &memory->dma_tag);

	if(status == ENOMEM) {
		isci_log_message(0, "ISCI", "bus_dma_tag_create failed\n");
		return (status);
	}

	status = bus_dmamem_alloc(memory->dma_tag,
	    (void **)&memory->virtual_address, BUS_DMA_ZERO, &memory->dma_map);

	if(status == ENOMEM)
	{
		isci_log_message(0, "ISCI", "bus_dmamem_alloc failed\n");
		return (status);
	}

	status = bus_dmamap_load(memory->dma_tag, memory->dma_map,
	    (void *)memory->virtual_address, memory->size,
	    isci_allocate_dma_buffer_callback, memory, 0);

	if(status == EINVAL)
	{
		isci_log_message(0, "ISCI", "bus_dmamap_load failed\n");
		return (status);
	}

	return (0);
}

/**
 * @brief This callback method asks the user to associate the supplied
 *        lock with an operating environment specific locking construct.
 *
 * @param[in]  controller This parameter specifies the controller with
 *             which this lock is to be associated.
 * @param[in]  lock This parameter specifies the lock for which the
 *             user should associate an operating environment specific
 *             locking object.
 *
 * @see The SCI_LOCK_LEVEL enumeration for more information.
 *
 * @return none.
 */
void
scif_cb_lock_associate(SCI_CONTROLLER_HANDLE_T controller,
    SCI_LOCK_HANDLE_T lock)
{

}

/**
 * @brief This callback method asks the user to de-associate the supplied
 *        lock with an operating environment specific locking construct.
 *
 * @param[in]  controller This parameter specifies the controller with
 *             which this lock is to be de-associated.
 * @param[in]  lock This parameter specifies the lock for which the
 *             user should de-associate an operating environment specific
 *             locking object.
 *
 * @see The SCI_LOCK_LEVEL enumeration for more information.
 *
 * @return none.
 */
void
scif_cb_lock_disassociate(SCI_CONTROLLER_HANDLE_T controller,
    SCI_LOCK_HANDLE_T lock)
{

}


/**
 * @brief This callback method asks the user to acquire/get the lock.
 *        This method should pend until the lock has been acquired.
 *
 * @param[in]  controller This parameter specifies the controller with
 *             which this lock is associated.
 * @param[in]  lock This parameter specifies the lock to be acquired.
 *
 * @return none
 */
void
scif_cb_lock_acquire(SCI_CONTROLLER_HANDLE_T controller,
    SCI_LOCK_HANDLE_T lock)
{

}

/**
 * @brief This callback method asks the user to release a lock.
 *
 * @param[in]  controller This parameter specifies the controller with
 *             which this lock is associated.
 * @param[in]  lock This parameter specifies the lock to be released.
 *
 * @return none
 */
void
scif_cb_lock_release(SCI_CONTROLLER_HANDLE_T controller,
    SCI_LOCK_HANDLE_T lock)
{
}

/**
 * @brief This callback method creates an OS specific deferred task
 *        for internal usage. The handler to deferred task is stored by OS
 *        driver.
 *
 * @param[in] controller This parameter specifies the controller object
 *            with which this callback is associated.
 *
 * @return none
 */
void
scif_cb_start_internal_io_task_create(SCI_CONTROLLER_HANDLE_T controller)
{

}

/**
 * @brief This callback method schedules a OS specific deferred task.
 *
 * @param[in] controller This parameter specifies the controller
 *            object with which this callback is associated.
 * @param[in] start_internal_io_task_routine This parameter specifies the
 *            sci start_internal_io routine.
 * @param[in] context This parameter specifies a handle to a parameter
 *            that will be passed into the "start_internal_io_task_routine"
 *            when it is invoked.
 *
 * @return none
 */
void
scif_cb_start_internal_io_task_schedule(SCI_CONTROLLER_HANDLE_T scif_controller,
    FUNCPTR start_internal_io_task_routine, void *context)
{
	/** @todo Use FreeBSD tasklet to defer this routine to a later time,
	 *  rather than calling the routine inline.
	 */
	SCI_START_INTERNAL_IO_ROUTINE sci_start_internal_io_routine =
	    (SCI_START_INTERNAL_IO_ROUTINE)start_internal_io_task_routine;

	sci_start_internal_io_routine(context);
}

/**
 * @brief In this method the user must write to PCI memory via access.
 *        This method is used for access to memory space and IO space.
 *
 * @param[in]  controller The controller for which to read a DWORD.
 * @param[in]  address This parameter depicts the address into
 *             which to write.
 * @param[out] write_value This parameter depicts the value being written
 *             into the PCI memory location.
 *
 * @todo These PCI memory access calls likely needs to be optimized into macros?
 */
void
scic_cb_pci_write_dword(SCI_CONTROLLER_HANDLE_T scic_controller,
    void *address, uint32_t write_value)
{
	SCI_CONTROLLER_HANDLE_T scif_controller =
	    (SCI_CONTROLLER_HANDLE_T) sci_object_get_association(scic_controller);
	struct ISCI_CONTROLLER *isci_controller =
	    (struct ISCI_CONTROLLER *) sci_object_get_association(scif_controller);
	struct isci_softc *isci = isci_controller->isci;
	uint32_t bar = (uint32_t)(((POINTER_UINT)address & 0xF0000000) >> 28);
	bus_size_t offset = (bus_size_t)((POINTER_UINT)address & 0x0FFFFFFF);

	bus_space_write_4(isci->pci_bar[bar].bus_tag,
	    isci->pci_bar[bar].bus_handle, offset, write_value);
}

/**
 * @brief In this method the user must read from PCI memory via access.
 *        This method is used for access to memory space and IO space.
 *
 * @param[in]  controller The controller for which to read a DWORD.
 * @param[in]  address This parameter depicts the address from
 *             which to read.
 *
 * @return The value being returned from the PCI memory location.
 *
 * @todo This PCI memory access calls likely need to be optimized into macro?
 */
uint32_t
scic_cb_pci_read_dword(SCI_CONTROLLER_HANDLE_T scic_controller, void *address)
{
	SCI_CONTROLLER_HANDLE_T scif_controller =
		(SCI_CONTROLLER_HANDLE_T)sci_object_get_association(scic_controller);
	struct ISCI_CONTROLLER *isci_controller =
		(struct ISCI_CONTROLLER *)sci_object_get_association(scif_controller);
	struct isci_softc *isci = isci_controller->isci;
	uint32_t bar = (uint32_t)(((POINTER_UINT)address & 0xF0000000) >> 28);
	bus_size_t offset = (bus_size_t)((POINTER_UINT)address & 0x0FFFFFFF);

	return (bus_space_read_4(isci->pci_bar[bar].bus_tag,
	    isci->pci_bar[bar].bus_handle, offset));
}

/**
 * @brief This method is called when the core requires the OS driver
 *        to stall execution.  This method is utilized during initialization
 *        or non-performance paths only.
 *
 * @param[in]  microseconds This parameter specifies the number of
 *             microseconds for which to stall.  The operating system driver
 *             is allowed to round this value up where necessary.
 *
 * @return none.
 */
void
scic_cb_stall_execution(uint32_t microseconds)
{

	DELAY(microseconds);
}

/**
 * @brief In this method the user must return the base address register (BAR)
 *        value for the supplied base address register number.
 *
 * @param[in] controller The controller for which to retrieve the bar number.
 * @param[in] bar_number This parameter depicts the BAR index/number to be read.
 *
 * @return Return a pointer value indicating the contents of the BAR.
 * @retval NULL indicates an invalid BAR index/number was specified.
 * @retval All other values indicate a valid VIRTUAL address from the BAR.
 */
void *
scic_cb_pci_get_bar(SCI_CONTROLLER_HANDLE_T controller,
    uint16_t bar_number)
{

	return ((void *)(POINTER_UINT)((uint32_t)bar_number << 28));
}

/**
 * @brief This method informs the SCI Core user that a phy/link became
 *        ready, but the phy is not allowed in the port.  In some
 *        situations the underlying hardware only allows for certain phy
 *        to port mappings.  If these mappings are violated, then this
 *        API is invoked.
 *
 * @param[in] controller This parameter represents the controller which
 *            contains the port.
 * @param[in] port This parameter specifies the SCI port object for which
 *            the callback is being invoked.
 * @param[in] phy This parameter specifies the phy that came ready, but the
 *            phy can't be a valid member of the port.
 *
 * @return none
 */
void
scic_cb_port_invalid_link_up(SCI_CONTROLLER_HANDLE_T controller,
    SCI_PORT_HANDLE_T port, SCI_PHY_HANDLE_T phy)
{

}
