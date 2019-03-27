/*-
 * Copyright (c) 2018 VMware, Inc.
 *
 * SPDX-License-Identifier: (BSD-2-Clause OR GPL-2.0)
 */

/* Driver for VMware Virtual Machine Communication Interface (VMCI) device. */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/systm.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <machine/bus.h>

#include "vmci.h"
#include "vmci_doorbell.h"
#include "vmci_driver.h"
#include "vmci_kernel_defs.h"
#include "vmci_queue_pair.h"

static int	vmci_probe(device_t);
static int	vmci_attach(device_t);
static int	vmci_detach(device_t);
static int	vmci_shutdown(device_t);

static int	vmci_map_bars(struct vmci_softc *);
static void	vmci_unmap_bars(struct vmci_softc *);

static int	vmci_config_capabilities(struct vmci_softc *);

static int	vmci_dma_malloc_int(struct vmci_softc *, bus_size_t,
		    bus_size_t, struct vmci_dma_alloc *);
static void	vmci_dma_free_int(struct vmci_softc *,
		    struct vmci_dma_alloc *);

static int	vmci_config_interrupts(struct vmci_softc *);
static int	vmci_config_interrupt(struct vmci_softc *);
static int	vmci_check_intr_cnt(struct vmci_softc *);
static int	vmci_allocate_interrupt_resources(struct vmci_softc *);
static int	vmci_setup_interrupts(struct vmci_softc *);
static void	vmci_dismantle_interrupts(struct vmci_softc *);
static void	vmci_interrupt(void *);
static void	vmci_interrupt_bm(void *);
static void	dispatch_datagrams(void *, int);
static void	process_bitmap(void *, int);

static void	vmci_delayed_work_fn_cb(void *context, int data);

static device_method_t vmci_methods[] = {
	/* Device interface. */
	DEVMETHOD(device_probe,		vmci_probe),
	DEVMETHOD(device_attach,	vmci_attach),
	DEVMETHOD(device_detach,	vmci_detach),
	DEVMETHOD(device_shutdown,	vmci_shutdown),

	DEVMETHOD_END
};

static driver_t vmci_driver = {
	"vmci", vmci_methods, sizeof(struct vmci_softc)
};

static devclass_t vmci_devclass;
DRIVER_MODULE(vmci, pci, vmci_driver, vmci_devclass, 0, 0);
MODULE_VERSION(vmci, VMCI_VERSION);

MODULE_DEPEND(vmci, pci, 1, 1, 1);

static struct vmci_softc *vmci_sc;

#define LGPFX	"vmci: "
/*
 * Allocate a buffer for incoming datagrams globally to avoid repeated
 * allocation in the interrupt handler's atomic context.
 */
static uint8_t *data_buffer = NULL;
static uint32_t data_buffer_size = VMCI_MAX_DG_SIZE;

struct vmci_delayed_work_info {
	vmci_work_fn	*work_fn;
	void		*data;
	vmci_list_item(vmci_delayed_work_info) entry;
};

/*
 *------------------------------------------------------------------------------
 *
 * vmci_probe --
 *
 *     Probe to see if the VMCI device is present.
 *
 * Results:
 *     BUS_PROBE_DEFAULT if device exists, ENXIO otherwise.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static int
vmci_probe(device_t dev)
{

	if (pci_get_vendor(dev) == VMCI_VMWARE_VENDOR_ID &&
	    pci_get_device(dev) == VMCI_VMWARE_DEVICE_ID) {
		device_set_desc(dev,
		    "VMware Virtual Machine Communication Interface");

		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_attach --
 *
 *     Attach VMCI device to the system after vmci_probe() has been called and
 *     the device has been detected.
 *
 * Results:
 *     0 if success, ENXIO otherwise.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static int
vmci_attach(device_t dev)
{
	struct vmci_softc *sc;
	int error, i;

	sc = device_get_softc(dev);
	sc->vmci_dev = dev;
	vmci_sc = sc;

	data_buffer = NULL;
	sc->vmci_num_intr = 0;
	for (i = 0; i < VMCI_MAX_INTRS; i++) {
		sc->vmci_intrs[i].vmci_irq = NULL;
		sc->vmci_intrs[i].vmci_handler = NULL;
	}

	TASK_INIT(&sc->vmci_interrupt_dq_task, 0, dispatch_datagrams, sc);
	TASK_INIT(&sc->vmci_interrupt_bm_task, 0, process_bitmap, sc);

	TASK_INIT(&sc->vmci_delayed_work_task, 0, vmci_delayed_work_fn_cb, sc);

	pci_enable_busmaster(dev);

	mtx_init(&sc->vmci_spinlock, "VMCI Spinlock", NULL, MTX_SPIN);
	mtx_init(&sc->vmci_delayed_work_lock, "VMCI Delayed Work Lock",
	    NULL, MTX_DEF);

	error = vmci_map_bars(sc);
	if (error) {
		VMCI_LOG_ERROR(LGPFX"Failed to map PCI BARs.\n");
		goto fail;
	}

	error = vmci_config_capabilities(sc);
	if (error) {
		VMCI_LOG_ERROR(LGPFX"Failed to configure capabilities.\n");
		goto fail;
	}

	vmci_list_init(&sc->vmci_delayed_work_infos);

	vmci_components_init();
	vmci_util_init();
	error = vmci_qp_guest_endpoints_init();
	if (error) {
		VMCI_LOG_ERROR(LGPFX"vmci_qp_guest_endpoints_init failed.\n");
		goto fail;
	}

	error = vmci_config_interrupts(sc);
	if (error)
		VMCI_LOG_ERROR(LGPFX"Failed to enable interrupts.\n");

fail:
	if (error) {
		vmci_detach(dev);
		return (ENXIO);
	}

	return (0);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_detach --
 *
 *     Detach the VMCI device.
 *
 * Results:
 *     0
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static int
vmci_detach(device_t dev)
{
	struct vmci_softc *sc;

	sc = device_get_softc(dev);

	vmci_qp_guest_endpoints_exit();
	vmci_util_exit();

	vmci_dismantle_interrupts(sc);

	vmci_components_cleanup();

	taskqueue_drain(taskqueue_thread, &sc->vmci_delayed_work_task);
	mtx_destroy(&sc->vmci_delayed_work_lock);

	if (sc->vmci_res0 != NULL)
		bus_space_write_4(sc->vmci_iot0, sc->vmci_ioh0,
		    VMCI_CONTROL_ADDR, VMCI_CONTROL_RESET);

	if (sc->vmci_notifications_bitmap.dma_vaddr != NULL)
		vmci_dma_free(&sc->vmci_notifications_bitmap);

	vmci_unmap_bars(sc);

	mtx_destroy(&sc->vmci_spinlock);

	pci_disable_busmaster(dev);

	return (0);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_shutdown --
 *
 *     This function is called during system shutdown. We don't do anything.
 *
 * Results:
 *     0
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static int
vmci_shutdown(device_t dev)
{

	return (0);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_map_bars --
 *
 *     Maps the PCI I/O and MMIO BARs.
 *
 * Results:
 *     0 on success, ENXIO otherwise.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static int
vmci_map_bars(struct vmci_softc *sc)
{
	int rid;

	/* Map the PCI I/O BAR: BAR0 */
	rid = PCIR_BAR(0);
	sc->vmci_res0 = bus_alloc_resource_any(sc->vmci_dev, SYS_RES_IOPORT,
	    &rid, RF_ACTIVE);
	if (sc->vmci_res0 == NULL) {
		VMCI_LOG_ERROR(LGPFX"Could not map: BAR0\n");
		return (ENXIO);
	}

	sc->vmci_iot0 = rman_get_bustag(sc->vmci_res0);
	sc->vmci_ioh0 = rman_get_bushandle(sc->vmci_res0);
	sc->vmci_ioaddr = rman_get_start(sc->vmci_res0);

	/* Map the PCI MMIO BAR: BAR1 */
	rid = PCIR_BAR(1);
	sc->vmci_res1 = bus_alloc_resource_any(sc->vmci_dev, SYS_RES_MEMORY,
	    &rid, RF_ACTIVE);
	if (sc->vmci_res1 == NULL) {
		VMCI_LOG_ERROR(LGPFX"Could not map: BAR1\n");
		return (ENXIO);
	}

	sc->vmci_iot1 = rman_get_bustag(sc->vmci_res1);
	sc->vmci_ioh1 = rman_get_bushandle(sc->vmci_res1);

	return (0);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_unmap_bars --
 *
 *     Unmaps the VMCI PCI I/O and MMIO BARs.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static void
vmci_unmap_bars(struct vmci_softc *sc)
{
	int rid;

	if (sc->vmci_res0 != NULL) {
		rid = PCIR_BAR(0);
		bus_release_resource(sc->vmci_dev, SYS_RES_IOPORT, rid,
		    sc->vmci_res0);
		sc->vmci_res0 = NULL;
	}

	if (sc->vmci_res1 != NULL) {
		rid = PCIR_BAR(1);
		bus_release_resource(sc->vmci_dev, SYS_RES_MEMORY, rid,
		    sc->vmci_res1);
		sc->vmci_res1 = NULL;
	}
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_config_capabilities --
 *
 *     Check the VMCI device capabilities and configure the device accordingly.
 *
 * Results:
 *     0 if success, ENODEV otherwise.
 *
 * Side effects:
 *     Device capabilities are enabled.
 *
 *------------------------------------------------------------------------------
 */

static int
vmci_config_capabilities(struct vmci_softc *sc)
{
	unsigned long bitmap_PPN;
	int error;

	/*
	 * Verify that the VMCI device supports the capabilities that we
	 * need. Datagrams are necessary and notifications will be used
	 * if the device supports it.
	 */
	sc->capabilities = bus_space_read_4(sc->vmci_iot0, sc->vmci_ioh0,
	    VMCI_CAPS_ADDR);

	if ((sc->capabilities & VMCI_CAPS_DATAGRAM) == 0) {
		VMCI_LOG_ERROR(LGPFX"VMCI device does not support "
		    "datagrams.\n");
		return (ENODEV);
	}

	if (sc->capabilities & VMCI_CAPS_NOTIFICATIONS) {
		sc->capabilities = VMCI_CAPS_DATAGRAM;
		error = vmci_dma_malloc(PAGE_SIZE, 1,
		    &sc->vmci_notifications_bitmap);
		if (error)
			VMCI_LOG_ERROR(LGPFX"Failed to alloc memory for "
			    "notification bitmap.\n");
		else {
			memset(sc->vmci_notifications_bitmap.dma_vaddr, 0,
			    PAGE_SIZE);
			sc->capabilities |= VMCI_CAPS_NOTIFICATIONS;
		}
	} else
		sc->capabilities = VMCI_CAPS_DATAGRAM;

	/* Let the host know which capabilities we intend to use. */
	bus_space_write_4(sc->vmci_iot0, sc->vmci_ioh0,
	    VMCI_CAPS_ADDR, sc->capabilities);

	/*
	 * Register notification bitmap with device if that capability is
	 * used.
	 */
	if (sc->capabilities & VMCI_CAPS_NOTIFICATIONS) {
		bitmap_PPN =
		    sc->vmci_notifications_bitmap.dma_paddr >> PAGE_SHIFT;
		vmci_register_notification_bitmap(bitmap_PPN);
	}

	/* Check host capabilities. */
	if (!vmci_check_host_capabilities())
		return (ENODEV);

	return (0);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_dmamap_cb --
 *
 *     Callback to receive mapping information resulting from the load of a
 *     bus_dmamap_t via bus_dmamap_load()
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static void
vmci_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	bus_addr_t *baddr = arg;

	if (error == 0)
		*baddr = segs->ds_addr;
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_dma_malloc_int --
 *
 *     Internal function that allocates DMA memory.
 *
 * Results:
 *     0 if success.
 *     ENOMEM if insufficient memory.
 *     EINPROGRESS if mapping is deferred.
 *     EINVAL if the request was invalid.
 *
 * Side effects:
 *     DMA memory is allocated.
 *
 *------------------------------------------------------------------------------
 */

static int
vmci_dma_malloc_int(struct vmci_softc *sc, bus_size_t size, bus_size_t align,
    struct vmci_dma_alloc *dma)
{
	int error;

	bzero(dma, sizeof(struct vmci_dma_alloc));

	error = bus_dma_tag_create(bus_get_dma_tag(vmci_sc->vmci_dev),
	    align, 0,		/* alignment, bounds */
	    BUS_SPACE_MAXADDR,	/* lowaddr */
	    BUS_SPACE_MAXADDR,	/* highaddr */
	    NULL, NULL,		/* filter, filterarg */
	    size,		/* maxsize */
	    1,			/* nsegments */
	    size,		/* maxsegsize */
	    BUS_DMA_ALLOCNOW,	/* flags */
	    NULL,		/* lockfunc */
	    NULL,		/* lockfuncarg */
	    &dma->dma_tag);
	if (error) {
		VMCI_LOG_ERROR(LGPFX"bus_dma_tag_create failed: %d\n", error);
		goto fail;
	}

	error = bus_dmamem_alloc(dma->dma_tag, (void **)&dma->dma_vaddr,
	    BUS_DMA_ZERO | BUS_DMA_NOWAIT, &dma->dma_map);
	if (error) {
		VMCI_LOG_ERROR(LGPFX"bus_dmamem_alloc failed: %d\n", error);
		goto fail;
	}

	error = bus_dmamap_load(dma->dma_tag, dma->dma_map, dma->dma_vaddr,
	    size, vmci_dmamap_cb, &dma->dma_paddr, BUS_DMA_NOWAIT);
	if (error) {
		VMCI_LOG_ERROR(LGPFX"bus_dmamap_load failed: %d\n", error);
		goto fail;
	}

	dma->dma_size = size;

fail:
	if (error)
		vmci_dma_free(dma);

	return (error);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_dma_malloc --
 *
 *     This function is a wrapper around vmci_dma_malloc_int for callers
 *     outside of this module. Since we only support a single VMCI device, this
 *     wrapper provides access to the device softc structure.
 *
 * Results:
 *     0 if success.
 *     ENOMEM if insufficient memory.
 *     EINPROGRESS if mapping is deferred.
 *     EINVAL if the request was invalid.
 *
 * Side effects:
 *     DMA memory is allocated.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_dma_malloc(bus_size_t size, bus_size_t align, struct vmci_dma_alloc *dma)
{

	return (vmci_dma_malloc_int(vmci_sc, size, align, dma));
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_dma_free_int --
 *
 *     Internal function that frees DMA memory.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Frees DMA memory.
 *
 *------------------------------------------------------------------------------
 */

static void
vmci_dma_free_int(struct vmci_softc *sc, struct vmci_dma_alloc *dma)
{

	if (dma->dma_tag != NULL) {
		if (dma->dma_paddr != 0) {
			bus_dmamap_sync(dma->dma_tag, dma->dma_map,
			    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(dma->dma_tag, dma->dma_map);
		}

		if (dma->dma_vaddr != NULL)
			bus_dmamem_free(dma->dma_tag, dma->dma_vaddr,
			    dma->dma_map);

		bus_dma_tag_destroy(dma->dma_tag);
	}
	bzero(dma, sizeof(struct vmci_dma_alloc));
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_dma_free --
 *
 *     This function is a wrapper around vmci_dma_free_int for callers outside
 *     of this module. Since we only support a single VMCI device, this wrapper
 *     provides access to the device softc structure.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Frees DMA memory.
 *
 *------------------------------------------------------------------------------
 */

void
vmci_dma_free(struct vmci_dma_alloc *dma)
{

	vmci_dma_free_int(vmci_sc, dma);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_config_interrupts --
 *
 *     Configures and enables interrupts. Try to configure MSI-X. If this fails,
 *     try to configure MSI. If even this fails, try legacy interrupts.
 *
 * Results:
 *     0 if success.
 *     ENOMEM if insufficient memory.
 *     ENODEV if the device doesn't support interrupts.
 *     ENXIO if the device configuration failed.
 *
 * Side effects:
 *     Interrupts get enabled if successful.
 *
 *------------------------------------------------------------------------------
 */

static int
vmci_config_interrupts(struct vmci_softc *sc)
{
	int error;

	data_buffer = malloc(data_buffer_size, M_DEVBUF, M_ZERO | M_NOWAIT);
	if (data_buffer == NULL)
		return (ENOMEM);

	sc->vmci_intr_type = VMCI_INTR_TYPE_MSIX;
	error = vmci_config_interrupt(sc);
	if (error) {
		sc->vmci_intr_type = VMCI_INTR_TYPE_MSI;
		error = vmci_config_interrupt(sc);
	}
	if (error) {
		sc->vmci_intr_type = VMCI_INTR_TYPE_INTX;
		error = vmci_config_interrupt(sc);
	}
	if (error)
		return (error);

	/* Enable specific interrupt bits. */
	if (sc->capabilities & VMCI_CAPS_NOTIFICATIONS)
		bus_space_write_4(sc->vmci_iot0, sc->vmci_ioh0,
		    VMCI_IMR_ADDR, VMCI_IMR_DATAGRAM | VMCI_IMR_NOTIFICATION);
	else
		bus_space_write_4(sc->vmci_iot0, sc->vmci_ioh0,
		    VMCI_IMR_ADDR, VMCI_IMR_DATAGRAM);

	/* Enable interrupts. */
	bus_space_write_4(sc->vmci_iot0, sc->vmci_ioh0,
	    VMCI_CONTROL_ADDR, VMCI_CONTROL_INT_ENABLE);

	return (0);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_config_interrupt --
 *
 *     Check the number of interrupts supported, allocate resources and setup
 *     interrupts.
 *
 * Results:
 *     0 if success.
 *     ENOMEM if insufficient memory.
 *     ENODEV if the device doesn't support interrupts.
 *     ENXIO if the device configuration failed.
 *
 * Side effects:
 *     Resources get allocated and interrupts get setup (but not enabled) if
 *     successful.
 *
 *------------------------------------------------------------------------------
 */

static int
vmci_config_interrupt(struct vmci_softc *sc)
{
	int error;

	error = vmci_check_intr_cnt(sc);
	if (error)
		return (error);

	error = vmci_allocate_interrupt_resources(sc);
	if (error)
		return (error);

	error = vmci_setup_interrupts(sc);
	if (error)
		return (error);

	return (0);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_check_intr_cnt --
 *
 *     Check the number of interrupts supported by the device and ask PCI bus
 *     to allocate appropriate number of interrupts.
 *
 * Results:
 *     0 if success.
 *     ENODEV if the device doesn't support any interrupts.
 *     ENXIO if the device configuration failed.
 *
 * Side effects:
 *     Resources get allocated on success.
 *
 *------------------------------------------------------------------------------
 */

static int
vmci_check_intr_cnt(struct vmci_softc *sc)
{

	if (sc->vmci_intr_type == VMCI_INTR_TYPE_INTX) {
		sc->vmci_num_intr = 1;
		return (0);
	}

	/*
	 * Make sure that the device supports the required number of MSI/MSI-X
	 * messages. We try for 2 MSI-X messages but 1 is good too. We need at
	 * least 1 MSI message.
	 */
	sc->vmci_num_intr = (sc->vmci_intr_type == VMCI_INTR_TYPE_MSIX) ?
	    pci_msix_count(sc->vmci_dev) : pci_msi_count(sc->vmci_dev);

	if (!sc->vmci_num_intr) {
		VMCI_LOG_ERROR(LGPFX"Device does not support any interrupt"
		    " messages");
		return (ENODEV);
	}

	sc->vmci_num_intr = (sc->vmci_intr_type == VMCI_INTR_TYPE_MSIX) ?
	    VMCI_MAX_INTRS : 1;
	if (sc->vmci_intr_type == VMCI_INTR_TYPE_MSIX) {
		if (pci_alloc_msix(sc->vmci_dev, &sc->vmci_num_intr))
			return (ENXIO);
	} else if (sc->vmci_intr_type == VMCI_INTR_TYPE_MSI) {
		if (pci_alloc_msi(sc->vmci_dev, &sc->vmci_num_intr))
			return (ENXIO);
	}

	return (0);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_allocate_interrupt_resources --
 *
 *     Allocate resources necessary for interrupts.
 *
 * Results:
 *     0 if success, ENXIO otherwise.
 *
 * Side effects:
 *     Resources get allocated on success.
 *
 *------------------------------------------------------------------------------
 */

static int
vmci_allocate_interrupt_resources(struct vmci_softc *sc)
{
	struct resource *irq;
	int flags, i, rid;

	flags = RF_ACTIVE;
	flags |= (sc->vmci_num_intr == 1) ? RF_SHAREABLE : 0;
	rid = (sc->vmci_intr_type == VMCI_INTR_TYPE_INTX) ? 0 : 1;

	for (i = 0; i < sc->vmci_num_intr; i++, rid++) {
		irq = bus_alloc_resource_any(sc->vmci_dev, SYS_RES_IRQ, &rid,
		    flags);
		if (irq == NULL)
			return (ENXIO);
		sc->vmci_intrs[i].vmci_irq = irq;
		sc->vmci_intrs[i].vmci_rid = rid;
	}

	return (0);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_setup_interrupts --
 *
 *     Sets up the interrupts.
 *
 * Results:
 *     0 if success, appropriate error code from bus_setup_intr otherwise.
 *
 * Side effects:
 *     Interrupt handler gets attached.
 *
 *------------------------------------------------------------------------------
 */

static int
vmci_setup_interrupts(struct vmci_softc *sc)
{
	struct vmci_interrupt *intr;
	int error, flags;

	flags = INTR_TYPE_NET | INTR_MPSAFE;
	if (sc->vmci_num_intr > 1)
		flags |= INTR_EXCL;

	intr = &sc->vmci_intrs[0];
	error = bus_setup_intr(sc->vmci_dev, intr->vmci_irq, flags, NULL,
	    vmci_interrupt, NULL, &intr->vmci_handler);
	if (error)
		return (error);
	bus_describe_intr(sc->vmci_dev, intr->vmci_irq, intr->vmci_handler,
	    "vmci_interrupt");

	if (sc->vmci_num_intr == 2) {
		intr = &sc->vmci_intrs[1];
		error = bus_setup_intr(sc->vmci_dev, intr->vmci_irq, flags,
		    NULL, vmci_interrupt_bm, NULL, &intr->vmci_handler);
		if (error)
			return (error);
		bus_describe_intr(sc->vmci_dev, intr->vmci_irq,
		    intr->vmci_handler, "vmci_interrupt_bm");
	}

	return (0);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_interrupt --
 *
 *     Interrupt handler for legacy or MSI interrupt, or for first MSI-X
 *     interrupt (vector VMCI_INTR_DATAGRAM).
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static void
vmci_interrupt(void *arg)
{

	if (vmci_sc->vmci_num_intr == 2)
		taskqueue_enqueue(taskqueue_swi,
		    &vmci_sc->vmci_interrupt_dq_task);
	else {
		unsigned int icr;

		icr = inl(vmci_sc->vmci_ioaddr + VMCI_ICR_ADDR);
		if (icr == 0 || icr == 0xffffffff)
			return;
		if (icr & VMCI_ICR_DATAGRAM) {
			taskqueue_enqueue(taskqueue_swi,
			    &vmci_sc->vmci_interrupt_dq_task);
			icr &= ~VMCI_ICR_DATAGRAM;
		}
		if (icr & VMCI_ICR_NOTIFICATION) {
			taskqueue_enqueue(taskqueue_swi,
			    &vmci_sc->vmci_interrupt_bm_task);
			icr &= ~VMCI_ICR_NOTIFICATION;
		}
		if (icr != 0)
			VMCI_LOG_INFO(LGPFX"Ignoring unknown interrupt "
			    "cause");
	}
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_interrupt_bm --
 *
 *     Interrupt handler for MSI-X interrupt vector VMCI_INTR_NOTIFICATION,
 *     which is for the notification bitmap. Will only get called if we are
 *     using MSI-X with exclusive vectors.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static void
vmci_interrupt_bm(void *arg)
{

	ASSERT(vmci_sc->vmci_num_intr == 2);
	taskqueue_enqueue(taskqueue_swi, &vmci_sc->vmci_interrupt_bm_task);
}

/*
 *------------------------------------------------------------------------------
 *
 * dispatch_datagrams --
 *
 *     Reads and dispatches incoming datagrams.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Reads data from the device.
 *
 *------------------------------------------------------------------------------
 */

static void
dispatch_datagrams(void *context, int data)
{

	if (data_buffer == NULL)
		VMCI_LOG_INFO(LGPFX"dispatch_datagrams(): no buffer "
		    "present");

	vmci_read_datagrams_from_port((vmci_io_handle) 0,
	    vmci_sc->vmci_ioaddr + VMCI_DATA_IN_ADDR,
	    data_buffer, data_buffer_size);
}

/*
 *------------------------------------------------------------------------------
 *
 * process_bitmap --
 *
 *     Scans the notification bitmap for raised flags, clears them and handles
 *     the notifications.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static void
process_bitmap(void *context, int data)
{

	if (vmci_sc->vmci_notifications_bitmap.dma_vaddr == NULL)
		VMCI_LOG_INFO(LGPFX"process_bitmaps(): no bitmap present");

	vmci_scan_notification_bitmap(
	    vmci_sc->vmci_notifications_bitmap.dma_vaddr);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_dismantle_interrupts --
 *
 *     Releases resources, detaches the interrupt handler and drains the task
 *     queue.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     No more interrupts.
 *
 *------------------------------------------------------------------------------
 */

static void
vmci_dismantle_interrupts(struct vmci_softc *sc)
{
	struct vmci_interrupt *intr;
	int i;

	for (i = 0; i < sc->vmci_num_intr; i++) {
		intr = &sc->vmci_intrs[i];
		if (intr->vmci_handler != NULL) {
			bus_teardown_intr(sc->vmci_dev, intr->vmci_irq,
			    intr->vmci_handler);
			intr->vmci_handler = NULL;
		}
		if (intr->vmci_irq != NULL) {
			bus_release_resource(sc->vmci_dev, SYS_RES_IRQ,
			    intr->vmci_rid, intr->vmci_irq);
			intr->vmci_irq = NULL;
			intr->vmci_rid = -1;
		}
	}

	if ((sc->vmci_intr_type != VMCI_INTR_TYPE_INTX) &&
	    (sc->vmci_num_intr))
		pci_release_msi(sc->vmci_dev);

	taskqueue_drain(taskqueue_swi, &sc->vmci_interrupt_dq_task);
	taskqueue_drain(taskqueue_swi, &sc->vmci_interrupt_bm_task);

	if (data_buffer != NULL)
		free(data_buffer, M_DEVBUF);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_delayed_work_fn_cb --
 *
 *     Callback function that executes the queued up delayed work functions.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

static void
vmci_delayed_work_fn_cb(void *context, int data)
{
	vmci_list(vmci_delayed_work_info) temp_list;

	vmci_list_init(&temp_list);

	/*
	 * Swap vmci_delayed_work_infos list with the empty temp_list while
	 * holding a lock. vmci_delayed_work_infos would then be an empty list
	 * and temp_list would contain the elements from the original
	 * vmci_delayed_work_infos. Finally, iterate through temp_list
	 * executing the delayed callbacks.
	 */

	mtx_lock(&vmci_sc->vmci_delayed_work_lock);
	vmci_list_swap(&temp_list, &vmci_sc->vmci_delayed_work_infos,
	    vmci_delayed_work_info, entry);
	mtx_unlock(&vmci_sc->vmci_delayed_work_lock);

	while (!vmci_list_empty(&temp_list)) {
		struct vmci_delayed_work_info *delayed_work_info =
		    vmci_list_first(&temp_list);

		delayed_work_info->work_fn(delayed_work_info->data);

		vmci_list_remove(delayed_work_info, entry);
		vmci_free_kernel_mem(delayed_work_info,
		    sizeof(*delayed_work_info));
	}
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_schedule_delayed_work_fn --
 *
 *     Schedule the specified callback.
 *
 * Results:
 *     0 if success, error code otherwise.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_schedule_delayed_work_fn(vmci_work_fn *work_fn, void *data)
{
	struct vmci_delayed_work_info *delayed_work_info;

	delayed_work_info = vmci_alloc_kernel_mem(sizeof(*delayed_work_info),
	    VMCI_MEMORY_ATOMIC);

	if (!delayed_work_info)
		return (VMCI_ERROR_NO_MEM);

	delayed_work_info->work_fn = work_fn;
	delayed_work_info->data = data;
	mtx_lock(&vmci_sc->vmci_delayed_work_lock);
	vmci_list_insert(&vmci_sc->vmci_delayed_work_infos,
	    delayed_work_info, entry);
	mtx_unlock(&vmci_sc->vmci_delayed_work_lock);

	taskqueue_enqueue(taskqueue_thread,
	    &vmci_sc->vmci_delayed_work_task);

	return (VMCI_SUCCESS);
}

/*
 *------------------------------------------------------------------------------
 *
 * vmci_send_datagram --
 *
 *     VM to hypervisor call mechanism.
 *
 * Results:
 *     The result of the hypercall.
 *
 * Side effects:
 *     None.
 *
 *------------------------------------------------------------------------------
 */

int
vmci_send_datagram(struct vmci_datagram *dg)
{
	int result;

	if (dg == NULL)
		return (VMCI_ERROR_INVALID_ARGS);

	/*
	 * Need to acquire spinlock on the device because
	 * the datagram data may be spread over multiple pages and the monitor
	 * may interleave device user rpc calls from multiple VCPUs. Acquiring
	 * the spinlock precludes that possibility. Disabling interrupts to
	 * avoid incoming datagrams during a "rep out" and possibly landing up
	 * in this function.
	 */
	mtx_lock_spin(&vmci_sc->vmci_spinlock);

	/*
	 * Send the datagram and retrieve the return value from the result
	 * register.
	 */
	__asm__ __volatile__(
	    "cld\n\t"
	    "rep outsb\n\t"
	    : /* No output. */
	    : "d"(vmci_sc->vmci_ioaddr + VMCI_DATA_OUT_ADDR),
	    "c"(VMCI_DG_SIZE(dg)), "S"(dg)
	    );

	/*
	 * XXX: Should read result high port as well when updating handlers to
	 * return 64bit.
	 */

	result = bus_space_read_4(vmci_sc->vmci_iot0,
	    vmci_sc->vmci_ioh0, VMCI_RESULT_LOW_ADDR);
	mtx_unlock_spin(&vmci_sc->vmci_spinlock);

	return (result);
}
