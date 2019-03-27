/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Hans Petter Selasky. All rights reserved.
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

#ifdef USB_GLOBAL_INCLUDE_FILE
#include USB_GLOBAL_INCLUDE_FILE
#else
#include "opt_ddb.h"

#include <sys/stdint.h>
#include <sys/stddef.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/sysctl.h>
#include <sys/sx.h>
#include <sys/unistd.h>
#include <sys/callout.h>
#include <sys/malloc.h>
#include <sys/priv.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>

#define	USB_DEBUG_VAR usb_ctrl_debug

#include <dev/usb/usb_core.h>
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_process.h>
#include <dev/usb/usb_busdma.h>
#include <dev/usb/usb_dynamic.h>
#include <dev/usb/usb_device.h>
#include <dev/usb/usb_dev.h>
#include <dev/usb/usb_hub.h>

#include <dev/usb/usb_controller.h>
#include <dev/usb/usb_bus.h>
#include <dev/usb/usb_pf.h>
#include "usb_if.h"
#endif			/* USB_GLOBAL_INCLUDE_FILE */

/* function prototypes  */

static device_probe_t usb_probe;
static device_attach_t usb_attach;
static device_detach_t usb_detach;
static device_suspend_t usb_suspend;
static device_resume_t usb_resume;
static device_shutdown_t usb_shutdown;

static void	usb_attach_sub(device_t, struct usb_bus *);

/* static variables */

#ifdef USB_DEBUG
static int usb_ctrl_debug = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, ctrl, CTLFLAG_RW, 0, "USB controller");
SYSCTL_INT(_hw_usb_ctrl, OID_AUTO, debug, CTLFLAG_RWTUN, &usb_ctrl_debug, 0,
    "Debug level");
#endif

#if USB_HAVE_ROOT_MOUNT_HOLD
static int usb_no_boot_wait = 0;
SYSCTL_INT(_hw_usb, OID_AUTO, no_boot_wait, CTLFLAG_RDTUN, &usb_no_boot_wait, 0,
    "No USB device enumerate waiting at boot.");
#endif

static int usb_no_suspend_wait = 0;
SYSCTL_INT(_hw_usb, OID_AUTO, no_suspend_wait, CTLFLAG_RWTUN,
    &usb_no_suspend_wait, 0, "No USB device waiting at system suspend.");

static int usb_no_shutdown_wait = 0;
SYSCTL_INT(_hw_usb, OID_AUTO, no_shutdown_wait, CTLFLAG_RWTUN,
    &usb_no_shutdown_wait, 0, "No USB device waiting at system shutdown.");

static devclass_t usb_devclass;

static device_method_t usb_methods[] = {
	DEVMETHOD(device_probe, usb_probe),
	DEVMETHOD(device_attach, usb_attach),
	DEVMETHOD(device_detach, usb_detach),
	DEVMETHOD(device_suspend, usb_suspend),
	DEVMETHOD(device_resume, usb_resume),
	DEVMETHOD(device_shutdown, usb_shutdown),

	DEVMETHOD_END
};

static driver_t usb_driver = {
	.name = "usbus",
	.methods = usb_methods,
	.size = 0,
};

/* Host Only Drivers */
DRIVER_MODULE(usbus, ohci, usb_driver, usb_devclass, 0, 0);
DRIVER_MODULE(usbus, uhci, usb_driver, usb_devclass, 0, 0);
DRIVER_MODULE(usbus, ehci, usb_driver, usb_devclass, 0, 0);
DRIVER_MODULE(usbus, xhci, usb_driver, usb_devclass, 0, 0);

/* Device Only Drivers */
DRIVER_MODULE(usbus, musbotg, usb_driver, usb_devclass, 0, 0);
DRIVER_MODULE(usbus, uss820dci, usb_driver, usb_devclass, 0, 0);
DRIVER_MODULE(usbus, octusb, usb_driver, usb_devclass, 0, 0);

/* Dual Mode Drivers */
DRIVER_MODULE(usbus, dwcotg, usb_driver, usb_devclass, 0, 0);
DRIVER_MODULE(usbus, saf1761otg, usb_driver, usb_devclass, 0, 0);

/*------------------------------------------------------------------------*
 *	usb_probe
 *
 * This function is called from "{ehci,ohci,uhci}_pci_attach()".
 *------------------------------------------------------------------------*/
static int
usb_probe(device_t dev)
{
	DPRINTF("\n");
	return (0);
}

#if USB_HAVE_ROOT_MOUNT_HOLD
static void
usb_root_mount_rel(struct usb_bus *bus)
{
	if (bus->bus_roothold != NULL) {
		DPRINTF("Releasing root mount hold %p\n", bus->bus_roothold);
		root_mount_rel(bus->bus_roothold);
		bus->bus_roothold = NULL;
	}
}
#endif

/*------------------------------------------------------------------------*
 *	usb_attach
 *------------------------------------------------------------------------*/
static int
usb_attach(device_t dev)
{
	struct usb_bus *bus = device_get_ivars(dev);

	DPRINTF("\n");

	if (bus == NULL) {
		device_printf(dev, "USB device has no ivars\n");
		return (ENXIO);
	}

#if USB_HAVE_ROOT_MOUNT_HOLD
	if (usb_no_boot_wait == 0) {
		/* delay vfs_mountroot until the bus is explored */
		bus->bus_roothold = root_mount_hold(device_get_nameunit(dev));
	}
#endif
	usb_attach_sub(dev, bus);

	return (0);			/* return success */
}

/*------------------------------------------------------------------------*
 *	usb_detach
 *------------------------------------------------------------------------*/
static int
usb_detach(device_t dev)
{
	struct usb_bus *bus = device_get_softc(dev);

	DPRINTF("\n");

	if (bus == NULL) {
		/* was never setup properly */
		return (0);
	}
	/* Stop power watchdog */
	usb_callout_drain(&bus->power_wdog);

#if USB_HAVE_ROOT_MOUNT_HOLD
	/* Let the USB explore process detach all devices. */
	usb_root_mount_rel(bus);
#endif

	USB_BUS_LOCK(bus);

	/* Queue detach job */
	usb_proc_msignal(USB_BUS_EXPLORE_PROC(bus),
	    &bus->detach_msg[0], &bus->detach_msg[1]);

	/* Wait for detach to complete */
	usb_proc_mwait(USB_BUS_EXPLORE_PROC(bus),
	    &bus->detach_msg[0], &bus->detach_msg[1]);

#if USB_HAVE_UGEN
	/* Wait for cleanup to complete */
	usb_proc_mwait(USB_BUS_EXPLORE_PROC(bus),
	    &bus->cleanup_msg[0], &bus->cleanup_msg[1]);
#endif
	USB_BUS_UNLOCK(bus);

#if USB_HAVE_PER_BUS_PROCESS
	/* Get rid of USB callback processes */

	usb_proc_free(USB_BUS_GIANT_PROC(bus));
	usb_proc_free(USB_BUS_NON_GIANT_ISOC_PROC(bus));
	usb_proc_free(USB_BUS_NON_GIANT_BULK_PROC(bus));

	/* Get rid of USB explore process */

	usb_proc_free(USB_BUS_EXPLORE_PROC(bus));

	/* Get rid of control transfer process */

	usb_proc_free(USB_BUS_CONTROL_XFER_PROC(bus));
#endif

#if USB_HAVE_PF
	usbpf_detach(bus);
#endif
	return (0);
}

/*------------------------------------------------------------------------*
 *	usb_suspend
 *------------------------------------------------------------------------*/
static int
usb_suspend(device_t dev)
{
	struct usb_bus *bus = device_get_softc(dev);

	DPRINTF("\n");

	if (bus == NULL) {
		/* was never setup properly */
		return (0);
	}

	USB_BUS_LOCK(bus);
	usb_proc_msignal(USB_BUS_EXPLORE_PROC(bus),
	    &bus->suspend_msg[0], &bus->suspend_msg[1]);
	if (usb_no_suspend_wait == 0) {
		/* wait for suspend callback to be executed */
		usb_proc_mwait(USB_BUS_EXPLORE_PROC(bus),
		    &bus->suspend_msg[0], &bus->suspend_msg[1]);
	}
	USB_BUS_UNLOCK(bus);

	return (0);
}

/*------------------------------------------------------------------------*
 *	usb_resume
 *------------------------------------------------------------------------*/
static int
usb_resume(device_t dev)
{
	struct usb_bus *bus = device_get_softc(dev);

	DPRINTF("\n");

	if (bus == NULL) {
		/* was never setup properly */
		return (0);
	}

	USB_BUS_LOCK(bus);
	usb_proc_msignal(USB_BUS_EXPLORE_PROC(bus),
	    &bus->resume_msg[0], &bus->resume_msg[1]);
	USB_BUS_UNLOCK(bus);

	return (0);
}

/*------------------------------------------------------------------------*
 *	usb_bus_reset_async_locked
 *------------------------------------------------------------------------*/
void
usb_bus_reset_async_locked(struct usb_bus *bus)
{
	USB_BUS_LOCK_ASSERT(bus, MA_OWNED);

	DPRINTF("\n");

	if (bus->reset_msg[0].hdr.pm_qentry.tqe_prev != NULL ||
	    bus->reset_msg[1].hdr.pm_qentry.tqe_prev != NULL) {
		DPRINTF("Reset already pending\n");
		return;
	}

	device_printf(bus->parent, "Resetting controller\n");

	usb_proc_msignal(USB_BUS_EXPLORE_PROC(bus),
	    &bus->reset_msg[0], &bus->reset_msg[1]);
}

/*------------------------------------------------------------------------*
 *	usb_shutdown
 *------------------------------------------------------------------------*/
static int
usb_shutdown(device_t dev)
{
	struct usb_bus *bus = device_get_softc(dev);

	DPRINTF("\n");

	if (bus == NULL) {
		/* was never setup properly */
		return (0);
	}

	DPRINTF("%s: Controller shutdown\n", device_get_nameunit(bus->bdev));

	USB_BUS_LOCK(bus);
	usb_proc_msignal(USB_BUS_EXPLORE_PROC(bus),
	    &bus->shutdown_msg[0], &bus->shutdown_msg[1]);
	if (usb_no_shutdown_wait == 0) {
		/* wait for shutdown callback to be executed */
		usb_proc_mwait(USB_BUS_EXPLORE_PROC(bus),
		    &bus->shutdown_msg[0], &bus->shutdown_msg[1]);
	}
	USB_BUS_UNLOCK(bus);

	DPRINTF("%s: Controller shutdown complete\n",
	    device_get_nameunit(bus->bdev));

	return (0);
}

/*------------------------------------------------------------------------*
 *	usb_bus_explore
 *
 * This function is used to explore the device tree from the root.
 *------------------------------------------------------------------------*/
static void
usb_bus_explore(struct usb_proc_msg *pm)
{
	struct usb_bus *bus;
	struct usb_device *udev;

	bus = ((struct usb_bus_msg *)pm)->bus;
	udev = bus->devices[USB_ROOT_HUB_ADDR];

	if (bus->no_explore != 0)
		return;

	if (udev != NULL) {
		USB_BUS_UNLOCK(bus);
		uhub_explore_handle_re_enumerate(udev);
		USB_BUS_LOCK(bus);
	}

	if (udev != NULL && udev->hub != NULL) {

		if (bus->do_probe) {
			bus->do_probe = 0;
			bus->driver_added_refcount++;
		}
		if (bus->driver_added_refcount == 0) {
			/* avoid zero, hence that is memory default */
			bus->driver_added_refcount = 1;
		}

#ifdef DDB
		/*
		 * The following three lines of code are only here to
		 * recover from DDB:
		 */
		usb_proc_rewakeup(USB_BUS_CONTROL_XFER_PROC(bus));
		usb_proc_rewakeup(USB_BUS_GIANT_PROC(bus));
		usb_proc_rewakeup(USB_BUS_NON_GIANT_ISOC_PROC(bus));
		usb_proc_rewakeup(USB_BUS_NON_GIANT_BULK_PROC(bus));
#endif

		USB_BUS_UNLOCK(bus);

#if USB_HAVE_POWERD
		/*
		 * First update the USB power state!
		 */
		usb_bus_powerd(bus);
#endif
		 /* Explore the Root USB HUB. */
		(udev->hub->explore) (udev);
		USB_BUS_LOCK(bus);
	}
#if USB_HAVE_ROOT_MOUNT_HOLD
	usb_root_mount_rel(bus);
#endif
}

/*------------------------------------------------------------------------*
 *	usb_bus_detach
 *
 * This function is used to detach the device tree from the root.
 *------------------------------------------------------------------------*/
static void
usb_bus_detach(struct usb_proc_msg *pm)
{
	struct usb_bus *bus;
	struct usb_device *udev;
	device_t dev;

	bus = ((struct usb_bus_msg *)pm)->bus;
	udev = bus->devices[USB_ROOT_HUB_ADDR];
	dev = bus->bdev;
	/* clear the softc */
	device_set_softc(dev, NULL);
	USB_BUS_UNLOCK(bus);

	/* detach children first */
	mtx_lock(&Giant);
	bus_generic_detach(dev);
	mtx_unlock(&Giant);

	/*
	 * Free USB device and all subdevices, if any.
	 */
	usb_free_device(udev, 0);

	USB_BUS_LOCK(bus);
	/* clear bdev variable last */
	bus->bdev = NULL;
}

/*------------------------------------------------------------------------*
 *	usb_bus_suspend
 *
 * This function is used to suspend the USB controller.
 *------------------------------------------------------------------------*/
static void
usb_bus_suspend(struct usb_proc_msg *pm)
{
	struct usb_bus *bus;
	struct usb_device *udev;
	usb_error_t err;
	uint8_t do_unlock;

	DPRINTF("\n");

	bus = ((struct usb_bus_msg *)pm)->bus;
	udev = bus->devices[USB_ROOT_HUB_ADDR];

	if (udev == NULL || bus->bdev == NULL)
		return;

	USB_BUS_UNLOCK(bus);

	/*
	 * We use the shutdown event here because the suspend and
	 * resume events are reserved for the USB port suspend and
	 * resume. The USB system suspend is implemented like full
	 * shutdown and all connected USB devices will be disconnected
	 * subsequently. At resume all USB devices will be
	 * re-connected again.
	 */

	bus_generic_shutdown(bus->bdev);

	do_unlock = usbd_enum_lock(udev);

	err = usbd_set_config_index(udev, USB_UNCONFIG_INDEX);
	if (err)
		device_printf(bus->bdev, "Could not unconfigure root HUB\n");

	USB_BUS_LOCK(bus);
	bus->hw_power_state = 0;
	bus->no_explore = 1;
	USB_BUS_UNLOCK(bus);

	if (bus->methods->set_hw_power != NULL)
		(bus->methods->set_hw_power) (bus);

	if (bus->methods->set_hw_power_sleep != NULL)
		(bus->methods->set_hw_power_sleep) (bus, USB_HW_POWER_SUSPEND);

	if (do_unlock)
		usbd_enum_unlock(udev);

	USB_BUS_LOCK(bus);
}

/*------------------------------------------------------------------------*
 *	usb_bus_resume
 *
 * This function is used to resume the USB controller.
 *------------------------------------------------------------------------*/
static void
usb_bus_resume(struct usb_proc_msg *pm)
{
	struct usb_bus *bus;
	struct usb_device *udev;
	usb_error_t err;
	uint8_t do_unlock;

	DPRINTF("\n");

	bus = ((struct usb_bus_msg *)pm)->bus;
	udev = bus->devices[USB_ROOT_HUB_ADDR];

	if (udev == NULL || bus->bdev == NULL)
		return;

	USB_BUS_UNLOCK(bus);

	do_unlock = usbd_enum_lock(udev);
#if 0
	DEVMETHOD(usb_take_controller, NULL);	/* dummy */
#endif
	USB_TAKE_CONTROLLER(device_get_parent(bus->bdev));

	USB_BUS_LOCK(bus);
 	bus->hw_power_state =
	  USB_HW_POWER_CONTROL |
	  USB_HW_POWER_BULK |
	  USB_HW_POWER_INTERRUPT |
	  USB_HW_POWER_ISOC |
	  USB_HW_POWER_NON_ROOT_HUB;
	bus->no_explore = 0;
	USB_BUS_UNLOCK(bus);

	if (bus->methods->set_hw_power_sleep != NULL)
		(bus->methods->set_hw_power_sleep) (bus, USB_HW_POWER_RESUME);

	if (bus->methods->set_hw_power != NULL)
		(bus->methods->set_hw_power) (bus);

	/* restore USB configuration to index 0 */
	err = usbd_set_config_index(udev, 0);
	if (err)
		device_printf(bus->bdev, "Could not configure root HUB\n");

	/* probe and attach */
	err = usb_probe_and_attach(udev, USB_IFACE_INDEX_ANY);
	if (err) {
		device_printf(bus->bdev, "Could not probe and "
		    "attach root HUB\n");
	}

	if (do_unlock)
		usbd_enum_unlock(udev);

	USB_BUS_LOCK(bus);
}

/*------------------------------------------------------------------------*
 *	usb_bus_reset
 *
 * This function is used to reset the USB controller.
 *------------------------------------------------------------------------*/
static void
usb_bus_reset(struct usb_proc_msg *pm)
{
	struct usb_bus *bus;

	DPRINTF("\n");

	bus = ((struct usb_bus_msg *)pm)->bus;

	if (bus->bdev == NULL || bus->no_explore != 0)
		return;

	/* a suspend and resume will reset the USB controller */
	usb_bus_suspend(pm);
	usb_bus_resume(pm);
}

/*------------------------------------------------------------------------*
 *	usb_bus_shutdown
 *
 * This function is used to shutdown the USB controller.
 *------------------------------------------------------------------------*/
static void
usb_bus_shutdown(struct usb_proc_msg *pm)
{
	struct usb_bus *bus;
	struct usb_device *udev;
	usb_error_t err;
	uint8_t do_unlock;

	bus = ((struct usb_bus_msg *)pm)->bus;
	udev = bus->devices[USB_ROOT_HUB_ADDR];

	if (udev == NULL || bus->bdev == NULL)
		return;

	USB_BUS_UNLOCK(bus);

	bus_generic_shutdown(bus->bdev);

	do_unlock = usbd_enum_lock(udev);

	err = usbd_set_config_index(udev, USB_UNCONFIG_INDEX);
	if (err)
		device_printf(bus->bdev, "Could not unconfigure root HUB\n");

	USB_BUS_LOCK(bus);
	bus->hw_power_state = 0;
	bus->no_explore = 1;
	USB_BUS_UNLOCK(bus);

	if (bus->methods->set_hw_power != NULL)
		(bus->methods->set_hw_power) (bus);

	if (bus->methods->set_hw_power_sleep != NULL)
		(bus->methods->set_hw_power_sleep) (bus, USB_HW_POWER_SHUTDOWN);

	if (do_unlock)
		usbd_enum_unlock(udev);

	USB_BUS_LOCK(bus);
}

/*------------------------------------------------------------------------*
 *	usb_bus_cleanup
 *
 * This function is used to cleanup leftover USB character devices.
 *------------------------------------------------------------------------*/
#if USB_HAVE_UGEN
static void
usb_bus_cleanup(struct usb_proc_msg *pm)
{
	struct usb_bus *bus;
	struct usb_fs_privdata *pd;

	bus = ((struct usb_bus_msg *)pm)->bus;

	while ((pd = LIST_FIRST(&bus->pd_cleanup_list)) != NULL) {

		LIST_REMOVE(pd, pd_next);
		USB_BUS_UNLOCK(bus);

		usb_destroy_dev_sync(pd);

		USB_BUS_LOCK(bus);
	}
}
#endif

static void
usb_power_wdog(void *arg)
{
	struct usb_bus *bus = arg;

	USB_BUS_LOCK_ASSERT(bus, MA_OWNED);

	usb_callout_reset(&bus->power_wdog,
	    4 * hz, usb_power_wdog, arg);

#ifdef DDB
	/*
	 * The following line of code is only here to recover from
	 * DDB:
	 */
	usb_proc_rewakeup(USB_BUS_EXPLORE_PROC(bus));	/* recover from DDB */
#endif

#if USB_HAVE_POWERD
	USB_BUS_UNLOCK(bus);

	usb_bus_power_update(bus);

	USB_BUS_LOCK(bus);
#endif
}

/*------------------------------------------------------------------------*
 *	usb_bus_attach
 *
 * This function attaches USB in context of the explore thread.
 *------------------------------------------------------------------------*/
static void
usb_bus_attach(struct usb_proc_msg *pm)
{
	struct usb_bus *bus;
	struct usb_device *child;
	device_t dev;
	usb_error_t err;
	enum usb_dev_speed speed;

	bus = ((struct usb_bus_msg *)pm)->bus;
	dev = bus->bdev;

	DPRINTF("\n");

	switch (bus->usbrev) {
	case USB_REV_1_0:
		speed = USB_SPEED_FULL;
		device_printf(bus->bdev, "12Mbps Full Speed USB v1.0\n");
		break;

	case USB_REV_1_1:
		speed = USB_SPEED_FULL;
		device_printf(bus->bdev, "12Mbps Full Speed USB v1.1\n");
		break;

	case USB_REV_2_0:
		speed = USB_SPEED_HIGH;
		device_printf(bus->bdev, "480Mbps High Speed USB v2.0\n");
		break;

	case USB_REV_2_5:
		speed = USB_SPEED_VARIABLE;
		device_printf(bus->bdev, "480Mbps Wireless USB v2.5\n");
		break;

	case USB_REV_3_0:
		speed = USB_SPEED_SUPER;
		device_printf(bus->bdev, "5.0Gbps Super Speed USB v3.0\n");
		break;

	default:
		device_printf(bus->bdev, "Unsupported USB revision\n");
#if USB_HAVE_ROOT_MOUNT_HOLD
		usb_root_mount_rel(bus);
#endif
		return;
	}

	/* default power_mask value */
	bus->hw_power_state =
	  USB_HW_POWER_CONTROL |
	  USB_HW_POWER_BULK |
	  USB_HW_POWER_INTERRUPT |
	  USB_HW_POWER_ISOC |
	  USB_HW_POWER_NON_ROOT_HUB;

	USB_BUS_UNLOCK(bus);

	/* make sure power is set at least once */

	if (bus->methods->set_hw_power != NULL) {
		(bus->methods->set_hw_power) (bus);
	}

	/* allocate the Root USB device */

	child = usb_alloc_device(bus->bdev, bus, NULL, 0, 0, 1,
	    speed, USB_MODE_HOST);
	if (child) {
		err = usb_probe_and_attach(child,
		    USB_IFACE_INDEX_ANY);
		if (!err) {
			if ((bus->devices[USB_ROOT_HUB_ADDR] == NULL) ||
			    (bus->devices[USB_ROOT_HUB_ADDR]->hub == NULL)) {
				err = USB_ERR_NO_ROOT_HUB;
			}
		}
	} else {
		err = USB_ERR_NOMEM;
	}

	USB_BUS_LOCK(bus);

	if (err) {
		device_printf(bus->bdev, "Root HUB problem, error=%s\n",
		    usbd_errstr(err));
#if USB_HAVE_ROOT_MOUNT_HOLD
		usb_root_mount_rel(bus);
#endif
	}

	/* set softc - we are ready */
	device_set_softc(dev, bus);

	/* start watchdog */
	usb_power_wdog(bus);
}

/*------------------------------------------------------------------------*
 *	usb_attach_sub
 *
 * This function creates a thread which runs the USB attach code.
 *------------------------------------------------------------------------*/
static void
usb_attach_sub(device_t dev, struct usb_bus *bus)
{
	mtx_lock(&Giant);
	if (usb_devclass_ptr == NULL)
		usb_devclass_ptr = devclass_find("usbus");
	mtx_unlock(&Giant);

#if USB_HAVE_PF
	usbpf_attach(bus);
#endif
	/* Initialise USB process messages */
	bus->explore_msg[0].hdr.pm_callback = &usb_bus_explore;
	bus->explore_msg[0].bus = bus;
	bus->explore_msg[1].hdr.pm_callback = &usb_bus_explore;
	bus->explore_msg[1].bus = bus;

	bus->detach_msg[0].hdr.pm_callback = &usb_bus_detach;
	bus->detach_msg[0].bus = bus;
	bus->detach_msg[1].hdr.pm_callback = &usb_bus_detach;
	bus->detach_msg[1].bus = bus;

	bus->attach_msg[0].hdr.pm_callback = &usb_bus_attach;
	bus->attach_msg[0].bus = bus;
	bus->attach_msg[1].hdr.pm_callback = &usb_bus_attach;
	bus->attach_msg[1].bus = bus;

	bus->suspend_msg[0].hdr.pm_callback = &usb_bus_suspend;
	bus->suspend_msg[0].bus = bus;
	bus->suspend_msg[1].hdr.pm_callback = &usb_bus_suspend;
	bus->suspend_msg[1].bus = bus;

	bus->resume_msg[0].hdr.pm_callback = &usb_bus_resume;
	bus->resume_msg[0].bus = bus;
	bus->resume_msg[1].hdr.pm_callback = &usb_bus_resume;
	bus->resume_msg[1].bus = bus;

	bus->reset_msg[0].hdr.pm_callback = &usb_bus_reset;
	bus->reset_msg[0].bus = bus;
	bus->reset_msg[1].hdr.pm_callback = &usb_bus_reset;
	bus->reset_msg[1].bus = bus;

	bus->shutdown_msg[0].hdr.pm_callback = &usb_bus_shutdown;
	bus->shutdown_msg[0].bus = bus;
	bus->shutdown_msg[1].hdr.pm_callback = &usb_bus_shutdown;
	bus->shutdown_msg[1].bus = bus;

#if USB_HAVE_UGEN
	LIST_INIT(&bus->pd_cleanup_list);
	bus->cleanup_msg[0].hdr.pm_callback = &usb_bus_cleanup;
	bus->cleanup_msg[0].bus = bus;
	bus->cleanup_msg[1].hdr.pm_callback = &usb_bus_cleanup;
	bus->cleanup_msg[1].bus = bus;
#endif

#if USB_HAVE_PER_BUS_PROCESS
	/* Create USB explore and callback processes */

	if (usb_proc_create(USB_BUS_GIANT_PROC(bus),
	    &bus->bus_mtx, device_get_nameunit(dev), USB_PRI_MED)) {
		device_printf(dev, "WARNING: Creation of USB Giant "
		    "callback process failed.\n");
	} else if (usb_proc_create(USB_BUS_NON_GIANT_ISOC_PROC(bus),
	    &bus->bus_mtx, device_get_nameunit(dev), USB_PRI_HIGHEST)) {
		device_printf(dev, "WARNING: Creation of USB non-Giant ISOC "
		    "callback process failed.\n");
	} else if (usb_proc_create(USB_BUS_NON_GIANT_BULK_PROC(bus),
	    &bus->bus_mtx, device_get_nameunit(dev), USB_PRI_HIGH)) {
		device_printf(dev, "WARNING: Creation of USB non-Giant BULK "
		    "callback process failed.\n");
	} else if (usb_proc_create(USB_BUS_EXPLORE_PROC(bus),
	    &bus->bus_mtx, device_get_nameunit(dev), USB_PRI_MED)) {
		device_printf(dev, "WARNING: Creation of USB explore "
		    "process failed.\n");
	} else if (usb_proc_create(USB_BUS_CONTROL_XFER_PROC(bus),
	    &bus->bus_mtx, device_get_nameunit(dev), USB_PRI_MED)) {
		device_printf(dev, "WARNING: Creation of USB control transfer "
		    "process failed.\n");
	} else
#endif
	{
		/* Get final attach going */
		USB_BUS_LOCK(bus);
		usb_proc_msignal(USB_BUS_EXPLORE_PROC(bus),
		    &bus->attach_msg[0], &bus->attach_msg[1]);
		USB_BUS_UNLOCK(bus);

		/* Do initial explore */
		usb_needs_explore(bus, 1);
	}
}
SYSUNINIT(usb_bus_unload, SI_SUB_KLD, SI_ORDER_ANY, usb_bus_unload, NULL);

/*------------------------------------------------------------------------*
 *	usb_bus_mem_flush_all_cb
 *------------------------------------------------------------------------*/
#if USB_HAVE_BUSDMA
static void
usb_bus_mem_flush_all_cb(struct usb_bus *bus, struct usb_page_cache *pc,
    struct usb_page *pg, usb_size_t size, usb_size_t align)
{
	usb_pc_cpu_flush(pc);
}
#endif

/*------------------------------------------------------------------------*
 *	usb_bus_mem_flush_all - factored out code
 *------------------------------------------------------------------------*/
#if USB_HAVE_BUSDMA
void
usb_bus_mem_flush_all(struct usb_bus *bus, usb_bus_mem_cb_t *cb)
{
	if (cb) {
		cb(bus, &usb_bus_mem_flush_all_cb);
	}
}
#endif

/*------------------------------------------------------------------------*
 *	usb_bus_mem_alloc_all_cb
 *------------------------------------------------------------------------*/
#if USB_HAVE_BUSDMA
static void
usb_bus_mem_alloc_all_cb(struct usb_bus *bus, struct usb_page_cache *pc,
    struct usb_page *pg, usb_size_t size, usb_size_t align)
{
	/* need to initialize the page cache */
	pc->tag_parent = bus->dma_parent_tag;

	if (usb_pc_alloc_mem(pc, pg, size, align)) {
		bus->alloc_failed = 1;
	}
}
#endif

/*------------------------------------------------------------------------*
 *	usb_bus_mem_alloc_all - factored out code
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
uint8_t
usb_bus_mem_alloc_all(struct usb_bus *bus, bus_dma_tag_t dmat,
    usb_bus_mem_cb_t *cb)
{
	bus->alloc_failed = 0;

	mtx_init(&bus->bus_mtx, device_get_nameunit(bus->parent),
	    "usb_def_mtx", MTX_DEF | MTX_RECURSE);

	mtx_init(&bus->bus_spin_lock, device_get_nameunit(bus->parent),
	    "usb_spin_mtx", MTX_SPIN | MTX_RECURSE);

	usb_callout_init_mtx(&bus->power_wdog,
	    &bus->bus_mtx, 0);

	TAILQ_INIT(&bus->intr_q.head);

#if USB_HAVE_BUSDMA
	usb_dma_tag_setup(bus->dma_parent_tag, bus->dma_tags,
	    dmat, &bus->bus_mtx, NULL, bus->dma_bits, USB_BUS_DMA_TAG_MAX);
#endif
	if ((bus->devices_max > USB_MAX_DEVICES) ||
	    (bus->devices_max < USB_MIN_DEVICES) ||
	    (bus->devices == NULL)) {
		DPRINTFN(0, "Devices field has not been "
		    "initialised properly\n");
		bus->alloc_failed = 1;		/* failure */
	}
#if USB_HAVE_BUSDMA
	if (cb) {
		cb(bus, &usb_bus_mem_alloc_all_cb);
	}
#endif
	if (bus->alloc_failed) {
		usb_bus_mem_free_all(bus, cb);
	}
	return (bus->alloc_failed);
}

/*------------------------------------------------------------------------*
 *	usb_bus_mem_free_all_cb
 *------------------------------------------------------------------------*/
#if USB_HAVE_BUSDMA
static void
usb_bus_mem_free_all_cb(struct usb_bus *bus, struct usb_page_cache *pc,
    struct usb_page *pg, usb_size_t size, usb_size_t align)
{
	usb_pc_free_mem(pc);
}
#endif

/*------------------------------------------------------------------------*
 *	usb_bus_mem_free_all - factored out code
 *------------------------------------------------------------------------*/
void
usb_bus_mem_free_all(struct usb_bus *bus, usb_bus_mem_cb_t *cb)
{
#if USB_HAVE_BUSDMA
	if (cb) {
		cb(bus, &usb_bus_mem_free_all_cb);
	}
	usb_dma_tag_unsetup(bus->dma_parent_tag);
#endif

	mtx_destroy(&bus->bus_mtx);
	mtx_destroy(&bus->bus_spin_lock);
}

/* convenience wrappers */
void
usb_proc_explore_mwait(struct usb_device *udev, void *pm1, void *pm2)
{
	usb_proc_mwait(USB_BUS_EXPLORE_PROC(udev->bus), pm1, pm2);
}

void	*
usb_proc_explore_msignal(struct usb_device *udev, void *pm1, void *pm2)
{
	return (usb_proc_msignal(USB_BUS_EXPLORE_PROC(udev->bus), pm1, pm2));
}

void
usb_proc_explore_lock(struct usb_device *udev)
{
	USB_BUS_LOCK(udev->bus);
}

void
usb_proc_explore_unlock(struct usb_device *udev)
{
	USB_BUS_UNLOCK(udev->bus);
}
