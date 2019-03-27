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
#include <sys/conf.h>
#include <sys/fcntl.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usb_ioctl.h>

#if USB_HAVE_UGEN
#include <sys/sbuf.h>
#endif

#include "usbdevs.h"

#define	USB_DEBUG_VAR usb_debug

#include <dev/usb/usb_core.h>
#include <dev/usb/usb_debug.h>
#include <dev/usb/usb_process.h>
#include <dev/usb/usb_device.h>
#include <dev/usb/usb_busdma.h>
#include <dev/usb/usb_transfer.h>
#include <dev/usb/usb_request.h>
#include <dev/usb/usb_dynamic.h>
#include <dev/usb/usb_hub.h>
#include <dev/usb/usb_util.h>
#include <dev/usb/usb_msctest.h>
#if USB_HAVE_UGEN
#include <dev/usb/usb_dev.h>
#include <dev/usb/usb_generic.h>
#endif

#include <dev/usb/quirk/usb_quirk.h>

#include <dev/usb/usb_controller.h>
#include <dev/usb/usb_bus.h>
#endif			/* USB_GLOBAL_INCLUDE_FILE */

/* function prototypes  */

static int	sysctl_hw_usb_template(SYSCTL_HANDLER_ARGS);
static void	usb_init_endpoint(struct usb_device *, uint8_t,
		    struct usb_endpoint_descriptor *,
		    struct usb_endpoint_ss_comp_descriptor *,
		    struct usb_endpoint *);
static void	usb_unconfigure(struct usb_device *, uint8_t);
static void	usb_detach_device_sub(struct usb_device *, device_t *,
		    char **, uint8_t);
static uint8_t	usb_probe_and_attach_sub(struct usb_device *,
		    struct usb_attach_arg *);
static void	usb_init_attach_arg(struct usb_device *,
		    struct usb_attach_arg *);
static void	usb_suspend_resume_sub(struct usb_device *, device_t,
		    uint8_t);
static usb_proc_callback_t usbd_clear_stall_proc;
static usb_error_t usb_config_parse(struct usb_device *, uint8_t, uint8_t);
static void	usbd_set_device_strings(struct usb_device *);
#if USB_HAVE_DEVCTL
static void	usb_notify_addq(const char *type, struct usb_device *);
#endif
#if USB_HAVE_UGEN
static void	usb_fifo_free_wrap(struct usb_device *, uint8_t, uint8_t);
static void	usb_cdev_create(struct usb_device *);
static void	usb_cdev_free(struct usb_device *);
#endif

/* This variable is global to allow easy access to it: */

#ifdef	USB_TEMPLATE
int	usb_template = USB_TEMPLATE;
#else
int	usb_template = -1;
#endif

SYSCTL_PROC(_hw_usb, OID_AUTO, template,
    CTLTYPE_INT | CTLFLAG_RWTUN | CTLFLAG_MPSAFE,
    NULL, 0, sysctl_hw_usb_template,
    "I", "Selected USB device side template");

/*------------------------------------------------------------------------*
 *	usb_trigger_reprobe_on_off
 *
 * This function sets the pull up resistors for all ports currently
 * operating in device mode either on (when on_not_off is 1), or off
 * (when it's 0).
 *------------------------------------------------------------------------*/
static void
usb_trigger_reprobe_on_off(int on_not_off)
{
	struct usb_port_status ps;
	struct usb_bus *bus;
	struct usb_device *udev;
	usb_error_t err;
	int do_unlock, max;

	max = devclass_get_maxunit(usb_devclass_ptr);
	while (max >= 0) {
		mtx_lock(&usb_ref_lock);
		bus = devclass_get_softc(usb_devclass_ptr, max);
		max--;

		if (bus == NULL || bus->devices == NULL ||
		    bus->devices[USB_ROOT_HUB_ADDR] == NULL) {
			mtx_unlock(&usb_ref_lock);
			continue;
		}

		udev = bus->devices[USB_ROOT_HUB_ADDR];

		if (udev->refcount == USB_DEV_REF_MAX) {
			mtx_unlock(&usb_ref_lock);
			continue;
		}

		udev->refcount++;
		mtx_unlock(&usb_ref_lock);

		do_unlock = usbd_enum_lock(udev);
		if (do_unlock > 1) {
			do_unlock = 0;
			goto next;
		}

		err = usbd_req_get_port_status(udev, NULL, &ps, 1);
		if (err != 0) {
			DPRINTF("usbd_req_get_port_status() "
			    "failed: %s\n", usbd_errstr(err));
			goto next;
		}

		if ((UGETW(ps.wPortStatus) & UPS_PORT_MODE_DEVICE) == 0)
			goto next;

		if (on_not_off) {
			err = usbd_req_set_port_feature(udev, NULL, 1,
			    UHF_PORT_POWER);
			if (err != 0) {
				DPRINTF("usbd_req_set_port_feature() "
				    "failed: %s\n", usbd_errstr(err));
			}
		} else {
			err = usbd_req_clear_port_feature(udev, NULL, 1,
			    UHF_PORT_POWER);
			if (err != 0) {
				DPRINTF("usbd_req_clear_port_feature() "
				    "failed: %s\n", usbd_errstr(err));
			}
		}

next:
		mtx_lock(&usb_ref_lock);
		if (do_unlock)
			usbd_enum_unlock(udev);
		if (--(udev->refcount) == 0)
			cv_broadcast(&udev->ref_cv);
		mtx_unlock(&usb_ref_lock);
	}
}

/*------------------------------------------------------------------------*
 *	usb_trigger_reprobe_all
 *
 * This function toggles the pull up resistors for all ports currently
 * operating in device mode, causing the host machine to reenumerate them.
 *------------------------------------------------------------------------*/
static void
usb_trigger_reprobe_all(void)
{

	/*
	 * Set the pull up resistors off for all ports in device mode.
	 */
	usb_trigger_reprobe_on_off(0);

	/*
	 * According to the DWC OTG spec this must be at least 3ms.
	 */
	usb_pause_mtx(NULL, USB_MS_TO_TICKS(USB_POWER_DOWN_TIME));

	/*
	 * Set the pull up resistors back on.
	 */
	usb_trigger_reprobe_on_off(1);
}

static int
sysctl_hw_usb_template(SYSCTL_HANDLER_ARGS)
{
	int error, val;

	val = usb_template;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL || usb_template == val)
		return (error);

	usb_template = val;

	if (usb_template < 0) {
		usb_trigger_reprobe_on_off(0);
	} else {
		usb_trigger_reprobe_all();
	}

	return (0);
}

/* English is default language */

static int usb_lang_id = 0x0009;
static int usb_lang_mask = 0x00FF;

SYSCTL_INT(_hw_usb, OID_AUTO, usb_lang_id, CTLFLAG_RWTUN,
    &usb_lang_id, 0, "Preferred USB language ID");

SYSCTL_INT(_hw_usb, OID_AUTO, usb_lang_mask, CTLFLAG_RWTUN,
    &usb_lang_mask, 0, "Preferred USB language mask");

static const char* statestr[USB_STATE_MAX] = {
	[USB_STATE_DETACHED]	= "DETACHED",
	[USB_STATE_ATTACHED]	= "ATTACHED",
	[USB_STATE_POWERED]	= "POWERED",
	[USB_STATE_ADDRESSED]	= "ADDRESSED",
	[USB_STATE_CONFIGURED]	= "CONFIGURED",
};

const char *
usb_statestr(enum usb_dev_state state)
{
	return ((state < USB_STATE_MAX) ? statestr[state] : "UNKNOWN");
}

const char *
usb_get_manufacturer(struct usb_device *udev)
{
	return (udev->manufacturer ? udev->manufacturer : "Unknown");
}

const char *
usb_get_product(struct usb_device *udev)
{
	return (udev->product ? udev->product : "");
}

const char *
usb_get_serial(struct usb_device *udev)
{
	return (udev->serial ? udev->serial : "");
}

/*------------------------------------------------------------------------*
 *	usbd_get_ep_by_addr
 *
 * This function searches for an USB ep by endpoint address and
 * direction.
 *
 * Returns:
 * NULL: Failure
 * Else: Success
 *------------------------------------------------------------------------*/
struct usb_endpoint *
usbd_get_ep_by_addr(struct usb_device *udev, uint8_t ea_val)
{
	struct usb_endpoint *ep = udev->endpoints;
	struct usb_endpoint *ep_end = udev->endpoints + udev->endpoints_max;
	enum {
		EA_MASK = (UE_DIR_IN | UE_DIR_OUT | UE_ADDR),
	};

	/*
	 * According to the USB specification not all bits are used
	 * for the endpoint address. Keep defined bits only:
	 */
	ea_val &= EA_MASK;

	/*
	 * Iterate across all the USB endpoints searching for a match
	 * based on the endpoint address:
	 */
	for (; ep != ep_end; ep++) {

		if (ep->edesc == NULL) {
			continue;
		}
		/* do the mask and check the value */
		if ((ep->edesc->bEndpointAddress & EA_MASK) == ea_val) {
			goto found;
		}
	}

	/*
	 * The default endpoint is always present and is checked separately:
	 */
	if ((udev->ctrl_ep.edesc != NULL) &&
	    ((udev->ctrl_ep.edesc->bEndpointAddress & EA_MASK) == ea_val)) {
		ep = &udev->ctrl_ep;
		goto found;
	}
	return (NULL);

found:
	return (ep);
}

/*------------------------------------------------------------------------*
 *	usbd_get_endpoint
 *
 * This function searches for an USB endpoint based on the information
 * given by the passed "struct usb_config" pointer.
 *
 * Return values:
 * NULL: No match.
 * Else: Pointer to "struct usb_endpoint".
 *------------------------------------------------------------------------*/
struct usb_endpoint *
usbd_get_endpoint(struct usb_device *udev, uint8_t iface_index,
    const struct usb_config *setup)
{
	struct usb_endpoint *ep = udev->endpoints;
	struct usb_endpoint *ep_end = udev->endpoints + udev->endpoints_max;
	uint8_t index = setup->ep_index;
	uint8_t ea_mask;
	uint8_t ea_val;
	uint8_t type_mask;
	uint8_t type_val;

	DPRINTFN(10, "udev=%p iface_index=%d address=0x%x "
	    "type=0x%x dir=0x%x index=%d\n",
	    udev, iface_index, setup->endpoint,
	    setup->type, setup->direction, setup->ep_index);

	/* check USB mode */

	if (setup->usb_mode != USB_MODE_DUAL &&
	    udev->flags.usb_mode != setup->usb_mode) {
		/* wrong mode - no endpoint */
		return (NULL);
	}

	/* setup expected endpoint direction mask and value */

	if (setup->direction == UE_DIR_RX) {
		ea_mask = (UE_DIR_IN | UE_DIR_OUT);
		ea_val = (udev->flags.usb_mode == USB_MODE_DEVICE) ?
		    UE_DIR_OUT : UE_DIR_IN;
	} else if (setup->direction == UE_DIR_TX) {
		ea_mask = (UE_DIR_IN | UE_DIR_OUT);
		ea_val = (udev->flags.usb_mode == USB_MODE_DEVICE) ?
		    UE_DIR_IN : UE_DIR_OUT;
	} else if (setup->direction == UE_DIR_ANY) {
		/* match any endpoint direction */
		ea_mask = 0;
		ea_val = 0;
	} else {
		/* match the given endpoint direction */
		ea_mask = (UE_DIR_IN | UE_DIR_OUT);
		ea_val = (setup->direction & (UE_DIR_IN | UE_DIR_OUT));
	}

	/* setup expected endpoint address */

	if (setup->endpoint == UE_ADDR_ANY) {
		/* match any endpoint address */
	} else {
		/* match the given endpoint address */
		ea_mask |= UE_ADDR;
		ea_val |= (setup->endpoint & UE_ADDR);
	}

	/* setup expected endpoint type */

	if (setup->type == UE_BULK_INTR) {
		/* this will match BULK and INTERRUPT endpoints */
		type_mask = 2;
		type_val = 2;
	} else if (setup->type == UE_TYPE_ANY) {
		/* match any endpoint type */
		type_mask = 0;
		type_val = 0;
	} else {
		/* match the given endpoint type */
		type_mask = UE_XFERTYPE;
		type_val = (setup->type & UE_XFERTYPE);
	}

	/*
	 * Iterate across all the USB endpoints searching for a match
	 * based on the endpoint address. Note that we are searching
	 * the endpoints from the beginning of the "udev->endpoints" array.
	 */
	for (; ep != ep_end; ep++) {

		if ((ep->edesc == NULL) ||
		    (ep->iface_index != iface_index)) {
			continue;
		}
		/* do the masks and check the values */

		if (((ep->edesc->bEndpointAddress & ea_mask) == ea_val) &&
		    ((ep->edesc->bmAttributes & type_mask) == type_val)) {
			if (!index--) {
				goto found;
			}
		}
	}

	/*
	 * Match against default endpoint last, so that "any endpoint", "any
	 * address" and "any direction" returns the first endpoint of the
	 * interface. "iface_index" and "direction" is ignored:
	 */
	if ((udev->ctrl_ep.edesc != NULL) &&
	    ((udev->ctrl_ep.edesc->bEndpointAddress & ea_mask) == ea_val) &&
	    ((udev->ctrl_ep.edesc->bmAttributes & type_mask) == type_val) &&
	    (!index)) {
		ep = &udev->ctrl_ep;
		goto found;
	}
	return (NULL);

found:
	return (ep);
}

/*------------------------------------------------------------------------*
 *	usbd_interface_count
 *
 * This function stores the number of USB interfaces excluding
 * alternate settings, which the USB config descriptor reports into
 * the unsigned 8-bit integer pointed to by "count".
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_interface_count(struct usb_device *udev, uint8_t *count)
{
	if (udev->cdesc == NULL) {
		*count = 0;
		return (USB_ERR_NOT_CONFIGURED);
	}
	*count = udev->ifaces_max;
	return (USB_ERR_NORMAL_COMPLETION);
}

/*------------------------------------------------------------------------*
 *	usb_init_endpoint
 *
 * This function will initialise the USB endpoint structure pointed to by
 * the "endpoint" argument. The structure pointed to by "endpoint" must be
 * zeroed before calling this function.
 *------------------------------------------------------------------------*/
static void
usb_init_endpoint(struct usb_device *udev, uint8_t iface_index,
    struct usb_endpoint_descriptor *edesc,
    struct usb_endpoint_ss_comp_descriptor *ecomp,
    struct usb_endpoint *ep)
{
	const struct usb_bus_methods *methods;
	usb_stream_t x;

	methods = udev->bus->methods;

	(methods->endpoint_init) (udev, edesc, ep);

	/* initialise USB endpoint structure */
	ep->edesc = edesc;
	ep->ecomp = ecomp;
	ep->iface_index = iface_index;

	/* setup USB stream queues */
	for (x = 0; x != USB_MAX_EP_STREAMS; x++) {
		TAILQ_INIT(&ep->endpoint_q[x].head);
		ep->endpoint_q[x].command = &usbd_pipe_start;
	}

	/* the pipe is not supported by the hardware */
 	if (ep->methods == NULL)
		return;

	/* check for SUPER-speed streams mode endpoint */
	if (udev->speed == USB_SPEED_SUPER && ecomp != NULL &&
	    (edesc->bmAttributes & UE_XFERTYPE) == UE_BULK &&
	    (UE_GET_BULK_STREAMS(ecomp->bmAttributes) != 0)) {
		usbd_set_endpoint_mode(udev, ep, USB_EP_MODE_STREAMS);
	} else {
		usbd_set_endpoint_mode(udev, ep, USB_EP_MODE_DEFAULT);
	}

	/* clear stall, if any */
	if (methods->clear_stall != NULL) {
		USB_BUS_LOCK(udev->bus);
		(methods->clear_stall) (udev, ep);
		USB_BUS_UNLOCK(udev->bus);
	}
}

/*-----------------------------------------------------------------------*
 *	usb_endpoint_foreach
 *
 * This function will iterate all the USB endpoints except the control
 * endpoint. This function is NULL safe.
 *
 * Return values:
 * NULL: End of USB endpoints
 * Else: Pointer to next USB endpoint
 *------------------------------------------------------------------------*/
struct usb_endpoint *
usb_endpoint_foreach(struct usb_device *udev, struct usb_endpoint *ep)
{
	struct usb_endpoint *ep_end;

	/* be NULL safe */
	if (udev == NULL)
		return (NULL);

	ep_end = udev->endpoints + udev->endpoints_max;

	/* get next endpoint */
	if (ep == NULL)
		ep = udev->endpoints;
	else
		ep++;

	/* find next allocated ep */
	while (ep != ep_end) {
		if (ep->edesc != NULL)
			return (ep);
		ep++;
	}
	return (NULL);
}

/*------------------------------------------------------------------------*
 *	usb_wait_pending_refs
 *
 * This function will wait for any USB references to go away before
 * returning. This function is used before freeing a USB device.
 *------------------------------------------------------------------------*/
static void
usb_wait_pending_refs(struct usb_device *udev)
{
#if USB_HAVE_UGEN
	DPRINTF("Refcount = %d\n", (int)udev->refcount); 

	mtx_lock(&usb_ref_lock);
	udev->refcount--;
	while (1) {
		/* wait for any pending references to go away */
		if (udev->refcount == 0) {
			/* prevent further refs being taken, if any */
			udev->refcount = USB_DEV_REF_MAX;
			break;
		}
		cv_wait(&udev->ref_cv, &usb_ref_lock);
	}
	mtx_unlock(&usb_ref_lock);
#endif
}

/*------------------------------------------------------------------------*
 *	usb_unconfigure
 *
 * This function will free all USB interfaces and USB endpoints belonging
 * to an USB device.
 *
 * Flag values, see "USB_UNCFG_FLAG_XXX".
 *------------------------------------------------------------------------*/
static void
usb_unconfigure(struct usb_device *udev, uint8_t flag)
{
	uint8_t do_unlock;

	/* Prevent re-enumeration */
	do_unlock = usbd_enum_lock(udev);

	/* detach all interface drivers */
	usb_detach_device(udev, USB_IFACE_INDEX_ANY, flag);

#if USB_HAVE_UGEN
	/* free all FIFOs except control endpoint FIFOs */
	usb_fifo_free_wrap(udev, USB_IFACE_INDEX_ANY, flag);

	/*
	 * Free all cdev's, if any.
	 */
	usb_cdev_free(udev);
#endif

#if USB_HAVE_COMPAT_LINUX
	/* free Linux compat device, if any */
	if (udev->linux_endpoint_start != NULL) {
		usb_linux_free_device_p(udev);
		udev->linux_endpoint_start = NULL;
	}
#endif

	usb_config_parse(udev, USB_IFACE_INDEX_ANY, USB_CFG_FREE);

	/* free "cdesc" after "ifaces" and "endpoints", if any */
	if (udev->cdesc != NULL) {
		if (udev->flags.usb_mode != USB_MODE_DEVICE)
			usbd_free_config_desc(udev, udev->cdesc);
		udev->cdesc = NULL;
	}
	/* set unconfigured state */
	udev->curr_config_no = USB_UNCONFIG_NO;
	udev->curr_config_index = USB_UNCONFIG_INDEX;

	if (do_unlock)
		usbd_enum_unlock(udev);
}

/*------------------------------------------------------------------------*
 *	usbd_set_config_index
 *
 * This function selects configuration by index, independent of the
 * actual configuration number. This function should not be used by
 * USB drivers.
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_set_config_index(struct usb_device *udev, uint8_t index)
{
	struct usb_status ds;
	struct usb_config_descriptor *cdp;
	uint16_t power;
	uint16_t max_power;
	uint8_t selfpowered;
	uint8_t do_unlock;
	usb_error_t err;

	DPRINTFN(6, "udev=%p index=%d\n", udev, index);

	/* Prevent re-enumeration */
	do_unlock = usbd_enum_lock(udev);

	usb_unconfigure(udev, 0);

	if (index == USB_UNCONFIG_INDEX) {
		/*
		 * Leave unallocated when unconfiguring the
		 * device. "usb_unconfigure()" will also reset
		 * the current config number and index.
		 */
		err = usbd_req_set_config(udev, NULL, USB_UNCONFIG_NO);
		if (udev->state == USB_STATE_CONFIGURED)
			usb_set_device_state(udev, USB_STATE_ADDRESSED);
		goto done;
	}
	/* get the full config descriptor */
	if (udev->flags.usb_mode == USB_MODE_DEVICE) {
		/* save some memory */
		err = usbd_req_get_descriptor_ptr(udev, &cdp, 
		    (UDESC_CONFIG << 8) | index);
	} else {
		/* normal request */
		err = usbd_req_get_config_desc_full(udev,
		    NULL, &cdp, index);
	}
	if (err) {
		goto done;
	}
	/* set the new config descriptor */

	udev->cdesc = cdp;

	/* Figure out if the device is self or bus powered. */
	selfpowered = 0;
	if ((!udev->flags.uq_bus_powered) &&
	    (cdp->bmAttributes & UC_SELF_POWERED) &&
	    (udev->flags.usb_mode == USB_MODE_HOST)) {
		/* May be self powered. */
		if (cdp->bmAttributes & UC_BUS_POWERED) {
			/* Must ask device. */
			err = usbd_req_get_device_status(udev, NULL, &ds);
			if (err) {
				DPRINTFN(0, "could not read "
				    "device status: %s\n",
				    usbd_errstr(err));
			} else if (UGETW(ds.wStatus) & UDS_SELF_POWERED) {
				selfpowered = 1;
			}
			DPRINTF("status=0x%04x \n",
				UGETW(ds.wStatus));
		} else
			selfpowered = 1;
	}
	DPRINTF("udev=%p cdesc=%p (addr %d) cno=%d attr=0x%02x, "
	    "selfpowered=%d, power=%d\n",
	    udev, cdp,
	    udev->address, cdp->bConfigurationValue, cdp->bmAttributes,
	    selfpowered, cdp->bMaxPower * 2);

	/* Check if we have enough power. */
	power = cdp->bMaxPower * 2;

	if (udev->parent_hub) {
		max_power = udev->parent_hub->hub->portpower;
	} else {
		max_power = USB_MAX_POWER;
	}

	if (power > max_power) {
		DPRINTFN(0, "power exceeded %d > %d\n", power, max_power);
		err = USB_ERR_NO_POWER;
		goto done;
	}
	/* Only update "self_powered" in USB Host Mode */
	if (udev->flags.usb_mode == USB_MODE_HOST) {
		udev->flags.self_powered = selfpowered;
	}
	udev->power = power;
	udev->curr_config_no = cdp->bConfigurationValue;
	udev->curr_config_index = index;
	usb_set_device_state(udev, USB_STATE_CONFIGURED);

	/* Set the actual configuration value. */
	err = usbd_req_set_config(udev, NULL, cdp->bConfigurationValue);
	if (err) {
		goto done;
	}

	err = usb_config_parse(udev, USB_IFACE_INDEX_ANY, USB_CFG_ALLOC);
	if (err) {
		goto done;
	}

	err = usb_config_parse(udev, USB_IFACE_INDEX_ANY, USB_CFG_INIT);
	if (err) {
		goto done;
	}

#if USB_HAVE_UGEN
	/* create device nodes for each endpoint */
	usb_cdev_create(udev);
#endif

done:
	DPRINTF("error=%s\n", usbd_errstr(err));
	if (err) {
		usb_unconfigure(udev, 0);
	}
	if (do_unlock)
		usbd_enum_unlock(udev);
	return (err);
}

/*------------------------------------------------------------------------*
 *	usb_config_parse
 *
 * This function will allocate and free USB interfaces and USB endpoints,
 * parse the USB configuration structure and initialise the USB endpoints
 * and interfaces. If "iface_index" is not equal to
 * "USB_IFACE_INDEX_ANY" then the "cmd" parameter is the
 * alternate_setting to be selected for the given interface. Else the
 * "cmd" parameter is defined by "USB_CFG_XXX". "iface_index" can be
 * "USB_IFACE_INDEX_ANY" or a valid USB interface index. This function
 * is typically called when setting the configuration or when setting
 * an alternate interface.
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
static usb_error_t
usb_config_parse(struct usb_device *udev, uint8_t iface_index, uint8_t cmd)
{
	struct usb_idesc_parse_state ips;
	struct usb_interface_descriptor *id;
	struct usb_endpoint_descriptor *ed;
	struct usb_interface *iface;
	struct usb_endpoint *ep;
	usb_error_t err;
	uint8_t ep_curr;
	uint8_t ep_max;
	uint8_t temp;
	uint8_t do_init;
	uint8_t alt_index;

	if (iface_index != USB_IFACE_INDEX_ANY) {
		/* parameter overload */
		alt_index = cmd;
		cmd = USB_CFG_INIT;
	} else {
		/* not used */
		alt_index = 0;
	}

	err = 0;

	DPRINTFN(5, "iface_index=%d cmd=%d\n",
	    iface_index, cmd);

	if (cmd == USB_CFG_FREE)
		goto cleanup;

	if (cmd == USB_CFG_INIT) {
		sx_assert(&udev->enum_sx, SA_LOCKED);

		/* check for in-use endpoints */

		ep = udev->endpoints;
		ep_max = udev->endpoints_max;
		while (ep_max--) {
			/* look for matching endpoints */
			if ((iface_index == USB_IFACE_INDEX_ANY) ||
			    (iface_index == ep->iface_index)) {
				if (ep->refcount_alloc != 0) {
					/*
					 * This typically indicates a
					 * more serious error.
					 */
					err = USB_ERR_IN_USE;
				} else {
					/* reset endpoint */
					memset(ep, 0, sizeof(*ep));
					/* make sure we don't zero the endpoint again */
					ep->iface_index = USB_IFACE_INDEX_ANY;
				}
			}
			ep++;
		}

		if (err)
			return (err);
	}

	memset(&ips, 0, sizeof(ips));

	ep_curr = 0;
	ep_max = 0;

	while ((id = usb_idesc_foreach(udev->cdesc, &ips))) {

		iface = udev->ifaces + ips.iface_index;

		/* check for specific interface match */

		if (cmd == USB_CFG_INIT) {
			if ((iface_index != USB_IFACE_INDEX_ANY) && 
			    (iface_index != ips.iface_index)) {
				/* wrong interface */
				do_init = 0;
			} else if (alt_index != ips.iface_index_alt) {
				/* wrong alternate setting */
				do_init = 0;
			} else {
				/* initialise interface */
				do_init = 1;
			}
		} else
			do_init = 0;

		/* check for new interface */
		if (ips.iface_index_alt == 0) {
			/* update current number of endpoints */
			ep_curr = ep_max;
		}
		/* check for init */
		if (do_init) {
			/* setup the USB interface structure */
			iface->idesc = id;
			/* set alternate index */
			iface->alt_index = alt_index;
			/* set default interface parent */
			if (iface_index == USB_IFACE_INDEX_ANY) {
				iface->parent_iface_index =
				    USB_IFACE_INDEX_ANY;
			}
		}

		DPRINTFN(5, "found idesc nendpt=%d\n", id->bNumEndpoints);

		ed = (struct usb_endpoint_descriptor *)id;

		temp = ep_curr;

		/* iterate all the endpoint descriptors */
		while ((ed = usb_edesc_foreach(udev->cdesc, ed))) {

			/* check if endpoint limit has been reached */
			if (temp >= USB_MAX_EP_UNITS) {
				DPRINTF("Endpoint limit reached\n");
				break;
			}

			ep = udev->endpoints + temp;

			if (do_init) {
				void *ecomp;

				ecomp = usb_ed_comp_foreach(udev->cdesc, (void *)ed);
				if (ecomp != NULL)
					DPRINTFN(5, "Found endpoint companion descriptor\n");

				usb_init_endpoint(udev, 
				    ips.iface_index, ed, ecomp, ep);
			}

			temp ++;

			/* find maximum number of endpoints */
			if (ep_max < temp)
				ep_max = temp;
		}
	}

	/* NOTE: It is valid to have no interfaces and no endpoints! */

	if (cmd == USB_CFG_ALLOC) {
		udev->ifaces_max = ips.iface_index;
#if (USB_HAVE_FIXED_IFACE == 0)
		udev->ifaces = NULL;
		if (udev->ifaces_max != 0) {
			udev->ifaces = malloc(sizeof(*iface) * udev->ifaces_max,
			        M_USB, M_WAITOK | M_ZERO);
			if (udev->ifaces == NULL) {
				err = USB_ERR_NOMEM;
				goto done;
			}
		}
#endif
#if (USB_HAVE_FIXED_ENDPOINT == 0)
		if (ep_max != 0) {
			udev->endpoints = malloc(sizeof(*ep) * ep_max,
			        M_USB, M_WAITOK | M_ZERO);
			if (udev->endpoints == NULL) {
				err = USB_ERR_NOMEM;
				goto done;
			}
		} else {
			udev->endpoints = NULL;
		}
#endif
		USB_BUS_LOCK(udev->bus);
		udev->endpoints_max = ep_max;
		/* reset any ongoing clear-stall */
		udev->ep_curr = NULL;
		USB_BUS_UNLOCK(udev->bus);
	}
#if (USB_HAVE_FIXED_IFACE == 0) || (USB_HAVE_FIXED_ENDPOINT == 0) 
done:
#endif
	if (err) {
		if (cmd == USB_CFG_ALLOC) {
cleanup:
			USB_BUS_LOCK(udev->bus);
			udev->endpoints_max = 0;
			/* reset any ongoing clear-stall */
			udev->ep_curr = NULL;
			USB_BUS_UNLOCK(udev->bus);

#if (USB_HAVE_FIXED_IFACE == 0)
			free(udev->ifaces, M_USB);
			udev->ifaces = NULL;
#endif
#if (USB_HAVE_FIXED_ENDPOINT == 0)
			free(udev->endpoints, M_USB);
			udev->endpoints = NULL;
#endif
			udev->ifaces_max = 0;
		}
	}
	return (err);
}

/*------------------------------------------------------------------------*
 *	usbd_set_alt_interface_index
 *
 * This function will select an alternate interface index for the
 * given interface index. The interface should not be in use when this
 * function is called. That means there should not be any open USB
 * transfers. Else an error is returned. If the alternate setting is
 * already set this function will simply return success. This function
 * is called in Host mode and Device mode!
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_set_alt_interface_index(struct usb_device *udev,
    uint8_t iface_index, uint8_t alt_index)
{
	struct usb_interface *iface = usbd_get_iface(udev, iface_index);
	usb_error_t err;
	uint8_t do_unlock;

	/* Prevent re-enumeration */
	do_unlock = usbd_enum_lock(udev);

	if (iface == NULL) {
		err = USB_ERR_INVAL;
		goto done;
	}
	if (iface->alt_index == alt_index) {
		/* 
		 * Optimise away duplicate setting of
		 * alternate setting in USB Host Mode!
		 */
		err = 0;
		goto done;
	}
#if USB_HAVE_UGEN
	/*
	 * Free all generic FIFOs for this interface, except control
	 * endpoint FIFOs:
	 */
	usb_fifo_free_wrap(udev, iface_index, 0);
#endif

	err = usb_config_parse(udev, iface_index, alt_index);
	if (err) {
		goto done;
	}
	if (iface->alt_index != alt_index) {
		/* the alternate setting does not exist */
		err = USB_ERR_INVAL;
		goto done;
	}

	err = usbd_req_set_alt_interface_no(udev, NULL, iface_index,
	    iface->idesc->bAlternateSetting);

done:
	if (do_unlock)
		usbd_enum_unlock(udev);
	return (err);
}

/*------------------------------------------------------------------------*
 *	usbd_set_endpoint_stall
 *
 * This function is used to make a BULK or INTERRUPT endpoint send
 * STALL tokens in USB device mode.
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usbd_set_endpoint_stall(struct usb_device *udev, struct usb_endpoint *ep,
    uint8_t do_stall)
{
	struct usb_xfer *xfer;
	usb_stream_t x;
	uint8_t et;
	uint8_t was_stalled;

	if (ep == NULL) {
		/* nothing to do */
		DPRINTF("Cannot find endpoint\n");
		/*
		 * Pretend that the clear or set stall request is
		 * successful else some USB host stacks can do
		 * strange things, especially when a control endpoint
		 * stalls.
		 */
		return (0);
	}
	et = (ep->edesc->bmAttributes & UE_XFERTYPE);

	if ((et != UE_BULK) &&
	    (et != UE_INTERRUPT)) {
		/*
	         * Should not stall control
	         * nor isochronous endpoints.
	         */
		DPRINTF("Invalid endpoint\n");
		return (0);
	}
	USB_BUS_LOCK(udev->bus);

	/* store current stall state */
	was_stalled = ep->is_stalled;

	/* check for no change */
	if (was_stalled && do_stall) {
		/* if the endpoint is already stalled do nothing */
		USB_BUS_UNLOCK(udev->bus);
		DPRINTF("No change\n");
		return (0);
	}
	/* set stalled state */
	ep->is_stalled = 1;

	if (do_stall || (!was_stalled)) {
		if (!was_stalled) {
			for (x = 0; x != USB_MAX_EP_STREAMS; x++) {
				/* lookup the current USB transfer, if any */
				xfer = ep->endpoint_q[x].curr;
				if (xfer != NULL) {
					/*
					 * The "xfer_stall" method
					 * will complete the USB
					 * transfer like in case of a
					 * timeout setting the error
					 * code "USB_ERR_STALLED".
					 */
					(udev->bus->methods->xfer_stall) (xfer);
				}
			}
		}
		(udev->bus->methods->set_stall) (udev, ep, &do_stall);
	}
	if (!do_stall) {
		ep->toggle_next = 0;	/* reset data toggle */
		ep->is_stalled = 0;	/* clear stalled state */

		(udev->bus->methods->clear_stall) (udev, ep);

		/* start the current or next transfer, if any */
		for (x = 0; x != USB_MAX_EP_STREAMS; x++) {
			usb_command_wrapper(&ep->endpoint_q[x],
			    ep->endpoint_q[x].curr);
		}
	}
	USB_BUS_UNLOCK(udev->bus);
	return (0);
}

/*------------------------------------------------------------------------*
 *	usb_reset_iface_endpoints - used in USB device side mode
 *------------------------------------------------------------------------*/
usb_error_t
usb_reset_iface_endpoints(struct usb_device *udev, uint8_t iface_index)
{
	struct usb_endpoint *ep;
	struct usb_endpoint *ep_end;

	ep = udev->endpoints;
	ep_end = udev->endpoints + udev->endpoints_max;

	for (; ep != ep_end; ep++) {

		if ((ep->edesc == NULL) ||
		    (ep->iface_index != iface_index)) {
			continue;
		}
		/* simulate a clear stall from the peer */
		usbd_set_endpoint_stall(udev, ep, 0);
	}
	return (0);
}

/*------------------------------------------------------------------------*
 *	usb_detach_device_sub
 *
 * This function will try to detach an USB device. If it fails a panic
 * will result.
 *
 * Flag values, see "USB_UNCFG_FLAG_XXX".
 *------------------------------------------------------------------------*/
static void
usb_detach_device_sub(struct usb_device *udev, device_t *ppdev,
    char **ppnpinfo, uint8_t flag)
{
	device_t dev;
	char *pnpinfo;
	int err;

	dev = *ppdev;
	if (dev) {
		/*
		 * NOTE: It is important to clear "*ppdev" before deleting
		 * the child due to some device methods being called late
		 * during the delete process !
		 */
		*ppdev = NULL;

		if (!rebooting) {
			device_printf(dev, "at %s, port %d, addr %d "
			    "(disconnected)\n",
			    device_get_nameunit(udev->parent_dev),
			    udev->port_no, udev->address);
		}

		if (device_is_attached(dev)) {
			if (udev->flags.peer_suspended) {
				err = DEVICE_RESUME(dev);
				if (err) {
					device_printf(dev, "Resume failed\n");
				}
			}
		}
		/* detach and delete child */
		if (device_delete_child(udev->parent_dev, dev)) {
			goto error;
		}
	}

	pnpinfo = *ppnpinfo;
	if (pnpinfo != NULL) {
		*ppnpinfo = NULL;
		free(pnpinfo, M_USBDEV);
	}
	return;

error:
	/* Detach is not allowed to fail in the USB world */
	panic("usb_detach_device_sub: A USB driver would not detach\n");
}

/*------------------------------------------------------------------------*
 *	usb_detach_device
 *
 * The following function will detach the matching interfaces.
 * This function is NULL safe.
 *
 * Flag values, see "USB_UNCFG_FLAG_XXX".
 *------------------------------------------------------------------------*/
void
usb_detach_device(struct usb_device *udev, uint8_t iface_index,
    uint8_t flag)
{
	struct usb_interface *iface;
	uint8_t i;

	if (udev == NULL) {
		/* nothing to do */
		return;
	}
	DPRINTFN(4, "udev=%p\n", udev);

	sx_assert(&udev->enum_sx, SA_LOCKED);

	/*
	 * First detach the child to give the child's detach routine a
	 * chance to detach the sub-devices in the correct order.
	 * Then delete the child using "device_delete_child()" which
	 * will detach all sub-devices from the bottom and upwards!
	 */
	if (iface_index != USB_IFACE_INDEX_ANY) {
		i = iface_index;
		iface_index = i + 1;
	} else {
		i = 0;
		iface_index = USB_IFACE_MAX;
	}

	/* do the detach */

	for (; i != iface_index; i++) {

		iface = usbd_get_iface(udev, i);
		if (iface == NULL) {
			/* looks like the end of the USB interfaces */
			break;
		}
		usb_detach_device_sub(udev, &iface->subdev,
		    &iface->pnpinfo, flag);
	}
}

/*------------------------------------------------------------------------*
 *	usb_probe_and_attach_sub
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
static uint8_t
usb_probe_and_attach_sub(struct usb_device *udev,
    struct usb_attach_arg *uaa)
{
	struct usb_interface *iface;
	device_t dev;
	int err;

	iface = uaa->iface;
	if (iface->parent_iface_index != USB_IFACE_INDEX_ANY) {
		/* leave interface alone */
		return (0);
	}
	dev = iface->subdev;
	if (dev) {

		/* clean up after module unload */

		if (device_is_attached(dev)) {
			/* already a device there */
			return (0);
		}
		/* clear "iface->subdev" as early as possible */

		iface->subdev = NULL;

		if (device_delete_child(udev->parent_dev, dev)) {

			/*
			 * Panic here, else one can get a double call
			 * to device_detach().  USB devices should
			 * never fail on detach!
			 */
			panic("device_delete_child() failed\n");
		}
	}
	if (uaa->temp_dev == NULL) {

		/* create a new child */
		uaa->temp_dev = device_add_child(udev->parent_dev, NULL, -1);
		if (uaa->temp_dev == NULL) {
			device_printf(udev->parent_dev,
			    "Device creation failed\n");
			return (1);	/* failure */
		}
		device_set_ivars(uaa->temp_dev, uaa);
		device_quiet(uaa->temp_dev);
	}
	/*
	 * Set "subdev" before probe and attach so that "devd" gets
	 * the information it needs.
	 */
	iface->subdev = uaa->temp_dev;

	if (device_probe_and_attach(iface->subdev) == 0) {
		/*
		 * The USB attach arguments are only available during probe
		 * and attach !
		 */
		uaa->temp_dev = NULL;
		device_set_ivars(iface->subdev, NULL);

		if (udev->flags.peer_suspended) {
			err = DEVICE_SUSPEND(iface->subdev);
			if (err)
				device_printf(iface->subdev, "Suspend failed\n");
		}
		return (0);		/* success */
	} else {
		/* No USB driver found */
		iface->subdev = NULL;
	}
	return (1);			/* failure */
}

/*------------------------------------------------------------------------*
 *	usbd_set_parent_iface
 *
 * Using this function will lock the alternate interface setting on an
 * interface. It is typically used for multi interface drivers. In USB
 * device side mode it is assumed that the alternate interfaces all
 * have the same endpoint descriptors. The default parent index value
 * is "USB_IFACE_INDEX_ANY". Then the alternate setting value is not
 * locked.
 *------------------------------------------------------------------------*/
void
usbd_set_parent_iface(struct usb_device *udev, uint8_t iface_index,
    uint8_t parent_index)
{
	struct usb_interface *iface;

	if (udev == NULL) {
		/* nothing to do */
		return;
	}
	iface = usbd_get_iface(udev, iface_index);
	if (iface != NULL)
		iface->parent_iface_index = parent_index;
}

static void
usb_init_attach_arg(struct usb_device *udev,
    struct usb_attach_arg *uaa)
{
	memset(uaa, 0, sizeof(*uaa));

	uaa->device = udev;
	uaa->usb_mode = udev->flags.usb_mode;
	uaa->port = udev->port_no;
	uaa->dev_state = UAA_DEV_READY;

	uaa->info.idVendor = UGETW(udev->ddesc.idVendor);
	uaa->info.idProduct = UGETW(udev->ddesc.idProduct);
	uaa->info.bcdDevice = UGETW(udev->ddesc.bcdDevice);
	uaa->info.bDeviceClass = udev->ddesc.bDeviceClass;
	uaa->info.bDeviceSubClass = udev->ddesc.bDeviceSubClass;
	uaa->info.bDeviceProtocol = udev->ddesc.bDeviceProtocol;
	uaa->info.bConfigIndex = udev->curr_config_index;
	uaa->info.bConfigNum = udev->curr_config_no;
}

/*------------------------------------------------------------------------*
 *	usb_probe_and_attach
 *
 * This function is called from "uhub_explore_sub()",
 * "usb_handle_set_config()" and "usb_handle_request()".
 *
 * Returns:
 *    0: Success
 * Else: A control transfer failed
 *------------------------------------------------------------------------*/
usb_error_t
usb_probe_and_attach(struct usb_device *udev, uint8_t iface_index)
{
	struct usb_attach_arg uaa;
	struct usb_interface *iface;
	uint8_t i;
	uint8_t j;
	uint8_t do_unlock;

	if (udev == NULL) {
		DPRINTF("udev == NULL\n");
		return (USB_ERR_INVAL);
	}
	/* Prevent re-enumeration */
	do_unlock = usbd_enum_lock(udev);

	if (udev->curr_config_index == USB_UNCONFIG_INDEX) {
		/* do nothing - no configuration has been set */
		goto done;
	}
	/* setup USB attach arguments */

	usb_init_attach_arg(udev, &uaa);

	/*
	 * If the whole USB device is targeted, invoke the USB event
	 * handler(s):
	 */
	if (iface_index == USB_IFACE_INDEX_ANY) {

		if (usb_test_quirk(&uaa, UQ_MSC_DYMO_EJECT) != 0 &&
		    usb_dymo_eject(udev, 0) == 0) {
			/* success, mark the udev as disappearing */
			uaa.dev_state = UAA_DEV_EJECTING;
		}

		EVENTHANDLER_INVOKE(usb_dev_configured, udev, &uaa);

		if (uaa.dev_state != UAA_DEV_READY) {
			/* leave device unconfigured */
			usb_unconfigure(udev, 0);
			goto done;
		}
	}

	/* Check if only one interface should be probed: */
	if (iface_index != USB_IFACE_INDEX_ANY) {
		i = iface_index;
		j = i + 1;
	} else {
		i = 0;
		j = USB_IFACE_MAX;
	}

	/* Do the probe and attach */
	for (; i != j; i++) {

		iface = usbd_get_iface(udev, i);
		if (iface == NULL) {
			/*
			 * Looks like the end of the USB
			 * interfaces !
			 */
			DPRINTFN(2, "end of interfaces "
			    "at %u\n", i);
			break;
		}
		if (iface->idesc == NULL) {
			/* no interface descriptor */
			continue;
		}
		uaa.iface = iface;

		uaa.info.bInterfaceClass =
		    iface->idesc->bInterfaceClass;
		uaa.info.bInterfaceSubClass =
		    iface->idesc->bInterfaceSubClass;
		uaa.info.bInterfaceProtocol =
		    iface->idesc->bInterfaceProtocol;
		uaa.info.bIfaceIndex = i;
		uaa.info.bIfaceNum =
		    iface->idesc->bInterfaceNumber;
		uaa.driver_info = 0;	/* reset driver_info */

		DPRINTFN(2, "iclass=%u/%u/%u iindex=%u/%u\n",
		    uaa.info.bInterfaceClass,
		    uaa.info.bInterfaceSubClass,
		    uaa.info.bInterfaceProtocol,
		    uaa.info.bIfaceIndex,
		    uaa.info.bIfaceNum);

		usb_probe_and_attach_sub(udev, &uaa);

		/*
		 * Remove the leftover child, if any, to enforce that
		 * a new nomatch devd event is generated for the next
		 * interface if no driver is found:
		 */
		if (uaa.temp_dev == NULL)
			continue;
		if (device_delete_child(udev->parent_dev, uaa.temp_dev))
			DPRINTFN(0, "device delete child failed\n");
		uaa.temp_dev = NULL;
	}
done:
	if (do_unlock)
		usbd_enum_unlock(udev);
	return (0);
}

/*------------------------------------------------------------------------*
 *	usb_suspend_resume_sub
 *
 * This function is called when the suspend or resume methods should
 * be executed on an USB device.
 *------------------------------------------------------------------------*/
static void
usb_suspend_resume_sub(struct usb_device *udev, device_t dev, uint8_t do_suspend)
{
	int err;

	if (dev == NULL) {
		return;
	}
	if (!device_is_attached(dev)) {
		return;
	}
	if (do_suspend) {
		err = DEVICE_SUSPEND(dev);
	} else {
		err = DEVICE_RESUME(dev);
	}
	if (err) {
		device_printf(dev, "%s failed\n",
		    do_suspend ? "Suspend" : "Resume");
	}
}

/*------------------------------------------------------------------------*
 *	usb_suspend_resume
 *
 * The following function will suspend or resume the USB device.
 *
 * Returns:
 *    0: Success
 * Else: Failure
 *------------------------------------------------------------------------*/
usb_error_t
usb_suspend_resume(struct usb_device *udev, uint8_t do_suspend)
{
	struct usb_interface *iface;
	uint8_t i;

	if (udev == NULL) {
		/* nothing to do */
		return (0);
	}
	DPRINTFN(4, "udev=%p do_suspend=%d\n", udev, do_suspend);

	sx_assert(&udev->sr_sx, SA_LOCKED);

	USB_BUS_LOCK(udev->bus);
	/* filter the suspend events */
	if (udev->flags.peer_suspended == do_suspend) {
		USB_BUS_UNLOCK(udev->bus);
		/* nothing to do */
		return (0);
	}
	udev->flags.peer_suspended = do_suspend;
	USB_BUS_UNLOCK(udev->bus);

	/* do the suspend or resume */

	for (i = 0; i != USB_IFACE_MAX; i++) {

		iface = usbd_get_iface(udev, i);
		if (iface == NULL) {
			/* looks like the end of the USB interfaces */
			break;
		}
		usb_suspend_resume_sub(udev, iface->subdev, do_suspend);
	}
	return (0);
}

/*------------------------------------------------------------------------*
 *      usbd_clear_stall_proc
 *
 * This function performs generic USB clear stall operations.
 *------------------------------------------------------------------------*/
static void
usbd_clear_stall_proc(struct usb_proc_msg *_pm)
{
	struct usb_udev_msg *pm = (void *)_pm;
	struct usb_device *udev = pm->udev;

	/* Change lock */
	USB_BUS_UNLOCK(udev->bus);
	USB_MTX_LOCK(&udev->device_mtx);

	/* Start clear stall callback */
	usbd_transfer_start(udev->ctrl_xfer[1]);

	/* Change lock */
	USB_MTX_UNLOCK(&udev->device_mtx);
	USB_BUS_LOCK(udev->bus);
}

/*------------------------------------------------------------------------*
 *	usb_alloc_device
 *
 * This function allocates a new USB device. This function is called
 * when a new device has been put in the powered state, but not yet in
 * the addressed state. Get initial descriptor, set the address, get
 * full descriptor and get strings.
 *
 * Return values:
 *    0: Failure
 * Else: Success
 *------------------------------------------------------------------------*/
struct usb_device *
usb_alloc_device(device_t parent_dev, struct usb_bus *bus,
    struct usb_device *parent_hub, uint8_t depth, uint8_t port_index,
    uint8_t port_no, enum usb_dev_speed speed, enum usb_hc_mode mode)
{
	struct usb_attach_arg uaa;
	struct usb_device *udev;
	struct usb_device *adev;
	struct usb_device *hub;
	uint8_t *scratch_ptr;
	usb_error_t err;
	uint8_t device_index;
	uint8_t config_index;
	uint8_t config_quirk;
	uint8_t set_config_failed;
	uint8_t do_unlock;

	DPRINTF("parent_dev=%p, bus=%p, parent_hub=%p, depth=%u, "
	    "port_index=%u, port_no=%u, speed=%u, usb_mode=%u\n",
	    parent_dev, bus, parent_hub, depth, port_index, port_no,
	    speed, mode);

	/*
	 * Find an unused device index. In USB Host mode this is the
	 * same as the device address.
	 *
	 * Device index zero is not used and device index 1 should
	 * always be the root hub.
	 */
	for (device_index = USB_ROOT_HUB_ADDR;
	    (device_index != bus->devices_max) &&
	    (bus->devices[device_index] != NULL);
	    device_index++) /* nop */;

	if (device_index == bus->devices_max) {
		device_printf(bus->bdev,
		    "No free USB device index for new device\n");
		return (NULL);
	}

	if (depth > 0x10) {
		device_printf(bus->bdev,
		    "Invalid device depth\n");
		return (NULL);
	}
	udev = malloc(sizeof(*udev), M_USB, M_WAITOK | M_ZERO);
	if (udev == NULL) {
		return (NULL);
	}
	/* initialise our SX-lock */
	sx_init_flags(&udev->enum_sx, "USB config SX lock", SX_DUPOK);
	sx_init_flags(&udev->sr_sx, "USB suspend and resume SX lock", SX_NOWITNESS);
	sx_init_flags(&udev->ctrl_sx, "USB control transfer SX lock", SX_DUPOK);

	cv_init(&udev->ctrlreq_cv, "WCTRL");
	cv_init(&udev->ref_cv, "UGONE");

	/* initialise our mutex */
	mtx_init(&udev->device_mtx, "USB device mutex", NULL, MTX_DEF);

	/* initialise generic clear stall */
	udev->cs_msg[0].hdr.pm_callback = &usbd_clear_stall_proc;
	udev->cs_msg[0].udev = udev;
	udev->cs_msg[1].hdr.pm_callback = &usbd_clear_stall_proc;
	udev->cs_msg[1].udev = udev;

	/* initialise some USB device fields */
	udev->parent_hub = parent_hub;
	udev->parent_dev = parent_dev;
	udev->port_index = port_index;
	udev->port_no = port_no;
	udev->depth = depth;
	udev->bus = bus;
	udev->address = USB_START_ADDR;	/* default value */
	udev->plugtime = (usb_ticks_t)ticks;
	/*
	 * We need to force the power mode to "on" because there are plenty
	 * of USB devices out there that do not work very well with
	 * automatic suspend and resume!
	 */
	udev->power_mode = usbd_filter_power_mode(udev, USB_POWER_MODE_ON);
	udev->pwr_save.last_xfer_time = ticks;
	/* we are not ready yet */
	udev->refcount = 1;

	/* set up default endpoint descriptor */
	udev->ctrl_ep_desc.bLength = sizeof(udev->ctrl_ep_desc);
	udev->ctrl_ep_desc.bDescriptorType = UDESC_ENDPOINT;
	udev->ctrl_ep_desc.bEndpointAddress = USB_CONTROL_ENDPOINT;
	udev->ctrl_ep_desc.bmAttributes = UE_CONTROL;
	udev->ctrl_ep_desc.wMaxPacketSize[0] = USB_MAX_IPACKET;
	udev->ctrl_ep_desc.wMaxPacketSize[1] = 0;
	udev->ctrl_ep_desc.bInterval = 0;

	/* set up default endpoint companion descriptor */
	udev->ctrl_ep_comp_desc.bLength = sizeof(udev->ctrl_ep_comp_desc);
	udev->ctrl_ep_comp_desc.bDescriptorType = UDESC_ENDPOINT_SS_COMP;

	udev->ddesc.bMaxPacketSize = USB_MAX_IPACKET;

	udev->speed = speed;
	udev->flags.usb_mode = mode;

	/* search for our High Speed USB HUB, if any */

	adev = udev;
	hub = udev->parent_hub;

	while (hub) {
		if (hub->speed == USB_SPEED_HIGH) {
			udev->hs_hub_addr = hub->address;
			udev->parent_hs_hub = hub;
			udev->hs_port_no = adev->port_no;
			break;
		}
		adev = hub;
		hub = hub->parent_hub;
	}

	/* init the default endpoint */
	usb_init_endpoint(udev, 0,
	    &udev->ctrl_ep_desc,
	    &udev->ctrl_ep_comp_desc,
	    &udev->ctrl_ep);

	/* set device index */
	udev->device_index = device_index;

#if USB_HAVE_UGEN
	/* Create ugen name */
	snprintf(udev->ugen_name, sizeof(udev->ugen_name),
	    USB_GENERIC_NAME "%u.%u", device_get_unit(bus->bdev),
	    device_index);
	LIST_INIT(&udev->pd_list);

	/* Create the control endpoint device */
	udev->ctrl_dev = usb_make_dev(udev, NULL, 0, 0,
	    FREAD|FWRITE, UID_ROOT, GID_OPERATOR, 0600);

	/* Create a link from /dev/ugenX.X to the default endpoint */
	if (udev->ctrl_dev != NULL)
		make_dev_alias(udev->ctrl_dev->cdev, "%s", udev->ugen_name);
#endif
	/* Initialise device */
	if (bus->methods->device_init != NULL) {
		err = (bus->methods->device_init) (udev);
		if (err != 0) {
			DPRINTFN(0, "device init %d failed "
			    "(%s, ignored)\n", device_index, 
			    usbd_errstr(err));
			goto done;
		}
	}
	/* set powered device state after device init is complete */
	usb_set_device_state(udev, USB_STATE_POWERED);

	if (udev->flags.usb_mode == USB_MODE_HOST) {

		err = usbd_req_set_address(udev, NULL, device_index);

		/*
		 * This is the new USB device address from now on, if
		 * the set address request didn't set it already.
		 */
		if (udev->address == USB_START_ADDR)
			udev->address = device_index;

		/*
		 * We ignore any set-address errors, hence there are
		 * buggy USB devices out there that actually receive
		 * the SETUP PID, but manage to set the address before
		 * the STATUS stage is ACK'ed. If the device responds
		 * to the subsequent get-descriptor at the new
		 * address, then we know that the set-address command
		 * was successful.
		 */
		if (err) {
			DPRINTFN(0, "set address %d failed "
			    "(%s, ignored)\n", udev->address, 
			    usbd_errstr(err));
		}
	} else {
		/* We are not self powered */
		udev->flags.self_powered = 0;

		/* Set unconfigured state */
		udev->curr_config_no = USB_UNCONFIG_NO;
		udev->curr_config_index = USB_UNCONFIG_INDEX;

		/* Setup USB descriptors */
		err = (usb_temp_setup_by_index_p) (udev, usb_template);
		if (err) {
			DPRINTFN(0, "setting up USB template failed - "
			    "usb_template(4) not loaded?\n");
			goto done;
		}
	}
	usb_set_device_state(udev, USB_STATE_ADDRESSED);

	/* setup the device descriptor and the initial "wMaxPacketSize" */
	err = usbd_setup_device_desc(udev, NULL);

	if (err != 0) {
		/* try to enumerate two more times */
		err = usbd_req_re_enumerate(udev, NULL);
		if (err != 0) {
			err = usbd_req_re_enumerate(udev, NULL);
			if (err != 0) {
				goto done;
			}
		}
	}

	/*
	 * Setup temporary USB attach args so that we can figure out some
	 * basic quirks for this device.
	 */
	usb_init_attach_arg(udev, &uaa);

	if (usb_test_quirk(&uaa, UQ_BUS_POWERED)) {
		udev->flags.uq_bus_powered = 1;
	}
	if (usb_test_quirk(&uaa, UQ_NO_STRINGS)) {
		udev->flags.no_strings = 1;
	}
	/*
	 * Workaround for buggy USB devices.
	 *
	 * It appears that some string-less USB chips will crash and
	 * disappear if any attempts are made to read any string
	 * descriptors.
	 *
	 * Try to detect such chips by checking the strings in the USB
	 * device descriptor. If no strings are present there we
	 * simply disable all USB strings.
	 */

	/* Protect scratch area */
	do_unlock = usbd_ctrl_lock(udev);

	scratch_ptr = udev->scratch.data;

	if (udev->flags.no_strings) {
		err = USB_ERR_INVAL;
	} else if (udev->ddesc.iManufacturer ||
	    udev->ddesc.iProduct ||
	    udev->ddesc.iSerialNumber) {
		/* read out the language ID string */
		err = usbd_req_get_string_desc(udev, NULL,
		    (char *)scratch_ptr, 4, 0, USB_LANGUAGE_TABLE);
	} else {
		err = USB_ERR_INVAL;
	}

	if (err || (scratch_ptr[0] < 4)) {
		udev->flags.no_strings = 1;
	} else {
		uint16_t langid;
		uint16_t pref;
		uint16_t mask;
		uint8_t x;

		/* load preferred value and mask */
		pref = usb_lang_id;
		mask = usb_lang_mask;

		/* align length correctly */
		scratch_ptr[0] &= ~1U;

		/* fix compiler warning */
		langid = 0;

		/* search for preferred language */
		for (x = 2; (x < scratch_ptr[0]); x += 2) {
			langid = UGETW(scratch_ptr + x);
			if ((langid & mask) == pref)
				break;
		}
		if (x >= scratch_ptr[0]) {
			/* pick the first language as the default */
			DPRINTFN(1, "Using first language\n");
			langid = UGETW(scratch_ptr + 2);
		}

		DPRINTFN(1, "Language selected: 0x%04x\n", langid);
		udev->langid = langid;
	}

	if (do_unlock)
		usbd_ctrl_unlock(udev);

	/* assume 100mA bus powered for now. Changed when configured. */
	udev->power = USB_MIN_POWER;
	/* fetch the vendor and product strings from the device */
	usbd_set_device_strings(udev);

	if (udev->flags.usb_mode == USB_MODE_DEVICE) {
		/* USB device mode setup is complete */
		err = 0;
		goto config_done;
	}

	/*
	 * Most USB devices should attach to config index 0 by
	 * default
	 */
	if (usb_test_quirk(&uaa, UQ_CFG_INDEX_0)) {
		config_index = 0;
		config_quirk = 1;
	} else if (usb_test_quirk(&uaa, UQ_CFG_INDEX_1)) {
		config_index = 1;
		config_quirk = 1;
	} else if (usb_test_quirk(&uaa, UQ_CFG_INDEX_2)) {
		config_index = 2;
		config_quirk = 1;
	} else if (usb_test_quirk(&uaa, UQ_CFG_INDEX_3)) {
		config_index = 3;
		config_quirk = 1;
	} else if (usb_test_quirk(&uaa, UQ_CFG_INDEX_4)) {
		config_index = 4;
		config_quirk = 1;
	} else {
		config_index = 0;
		config_quirk = 0;
	}

	set_config_failed = 0;
repeat_set_config:

	DPRINTF("setting config %u\n", config_index);

	/* get the USB device configured */
	err = usbd_set_config_index(udev, config_index);
	if (err) {
		if (udev->ddesc.bNumConfigurations != 0) {
			if (!set_config_failed) {
				set_config_failed = 1;
				/* XXX try to re-enumerate the device */
				err = usbd_req_re_enumerate(udev, NULL);
				if (err == 0)
					goto repeat_set_config;
			}
			DPRINTFN(0, "Failure selecting configuration index %u:"
			    "%s, port %u, addr %u (ignored)\n",
			    config_index, usbd_errstr(err), udev->port_no,
			    udev->address);
		}
		/*
		 * Some USB devices do not have any configurations. Ignore any
		 * set config failures!
		 */
		err = 0;
		goto config_done;
	}
	if (!config_quirk && config_index + 1 < udev->ddesc.bNumConfigurations) {
		if ((udev->cdesc->bNumInterface < 2) &&
		    usbd_get_no_descriptors(udev->cdesc, UDESC_ENDPOINT) == 0) {
			DPRINTFN(0, "Found no endpoints, trying next config\n");
			config_index++;
			goto repeat_set_config;
		}
#if USB_HAVE_MSCTEST
		if (config_index == 0) {
			/*
			 * Try to figure out if we have an
			 * auto-install disk there:
			 */
			if (usb_iface_is_cdrom(udev, 0)) {
				DPRINTFN(0, "Found possible auto-install "
				    "disk (trying next config)\n");
				config_index++;
				goto repeat_set_config;
			}
		}
#endif
	}
#if USB_HAVE_MSCTEST
	if (set_config_failed == 0 && config_index == 0 &&
	    usb_test_quirk(&uaa, UQ_MSC_NO_SYNC_CACHE) == 0 &&
	    usb_test_quirk(&uaa, UQ_MSC_NO_GETMAXLUN) == 0) {

		/*
		 * Try to figure out if there are any MSC quirks we
		 * should apply automatically:
		 */
		err = usb_msc_auto_quirk(udev, 0);

		if (err != 0) {
			set_config_failed = 1;
			goto repeat_set_config;
		}
	}
#endif

config_done:
	DPRINTF("new dev (addr %d), udev=%p, parent_hub=%p\n",
	    udev->address, udev, udev->parent_hub);

	/* register our device - we are ready */
	usb_bus_port_set_device(bus, parent_hub ?
	    parent_hub->hub->ports + port_index : NULL, udev, device_index);

#if USB_HAVE_UGEN
	/* Symlink the ugen device name */
	udev->ugen_symlink = usb_alloc_symlink(udev->ugen_name);

	/* Announce device */
	printf("%s: <%s %s> at %s\n", udev->ugen_name,
	    usb_get_manufacturer(udev), usb_get_product(udev),
	    device_get_nameunit(udev->bus->bdev));
#endif

#if USB_HAVE_DEVCTL
	usb_notify_addq("ATTACH", udev);
#endif
done:
	if (err) {
		/*
		 * Free USB device and all subdevices, if any.
		 */
		usb_free_device(udev, 0);
		udev = NULL;
	}
	return (udev);
}

#if USB_HAVE_UGEN
struct usb_fs_privdata *
usb_make_dev(struct usb_device *udev, const char *devname, int ep,
    int fi, int rwmode, uid_t uid, gid_t gid, int mode)
{
	struct usb_fs_privdata* pd;
	struct make_dev_args args;
	char buffer[32];

	/* Store information to locate ourselves again later */
	pd = malloc(sizeof(struct usb_fs_privdata), M_USBDEV,
	    M_WAITOK | M_ZERO);
	pd->bus_index = device_get_unit(udev->bus->bdev);
	pd->dev_index = udev->device_index;
	pd->ep_addr = ep;
	pd->fifo_index = fi;
	pd->mode = rwmode;

	/* Now, create the device itself */
	if (devname == NULL) {
		devname = buffer;
		snprintf(buffer, sizeof(buffer), USB_DEVICE_DIR "/%u.%u.%u",
		    pd->bus_index, pd->dev_index, pd->ep_addr);
	}

	/* Setup arguments for make_dev_s() */
	make_dev_args_init(&args);
	args.mda_devsw = &usb_devsw;
	args.mda_uid = uid;
	args.mda_gid = gid;
	args.mda_mode = mode;
	args.mda_si_drv1 = pd;

	if (make_dev_s(&args, &pd->cdev, "%s", devname) != 0) {
		DPRINTFN(0, "Failed to create device %s\n", devname);
		free(pd, M_USBDEV);
		return (NULL);
	}
	return (pd);
}

void
usb_destroy_dev_sync(struct usb_fs_privdata *pd)
{
	DPRINTFN(1, "Destroying device at ugen%d.%d\n",
	    pd->bus_index, pd->dev_index);

	/*
	 * Destroy character device synchronously. After this
	 * all system calls are returned. Can block.
	 */
	destroy_dev(pd->cdev);

	free(pd, M_USBDEV);
}

void
usb_destroy_dev(struct usb_fs_privdata *pd)
{
	struct usb_bus *bus;

	if (pd == NULL)
		return;

	mtx_lock(&usb_ref_lock);
	bus = devclass_get_softc(usb_devclass_ptr, pd->bus_index);
	mtx_unlock(&usb_ref_lock);

	if (bus == NULL) {
		usb_destroy_dev_sync(pd);
		return;
	}

	/* make sure we can re-use the device name */
	delist_dev(pd->cdev);

	USB_BUS_LOCK(bus);
	LIST_INSERT_HEAD(&bus->pd_cleanup_list, pd, pd_next);
	/* get cleanup going */
	usb_proc_msignal(USB_BUS_EXPLORE_PROC(bus),
	    &bus->cleanup_msg[0], &bus->cleanup_msg[1]);
	USB_BUS_UNLOCK(bus);
}

static void
usb_cdev_create(struct usb_device *udev)
{
	struct usb_config_descriptor *cd;
	struct usb_endpoint_descriptor *ed;
	struct usb_descriptor *desc;
	struct usb_fs_privdata* pd;
	int inmode, outmode, inmask, outmask, mode;
	uint8_t ep;

	KASSERT(LIST_FIRST(&udev->pd_list) == NULL, ("stale cdev entries"));

	DPRINTFN(2, "Creating device nodes\n");

	if (usbd_get_mode(udev) == USB_MODE_DEVICE) {
		inmode = FWRITE;
		outmode = FREAD;
	} else {		 /* USB_MODE_HOST */
		inmode = FREAD;
		outmode = FWRITE;
	}

	inmask = 0;
	outmask = 0;
	desc = NULL;

	/*
	 * Collect all used endpoint numbers instead of just
	 * generating 16 static endpoints.
	 */
	cd = usbd_get_config_descriptor(udev);
	while ((desc = usb_desc_foreach(cd, desc))) {
		/* filter out all endpoint descriptors */
		if ((desc->bDescriptorType == UDESC_ENDPOINT) &&
		    (desc->bLength >= sizeof(*ed))) {
			ed = (struct usb_endpoint_descriptor *)desc;

			/* update masks */
			ep = ed->bEndpointAddress;
			if (UE_GET_DIR(ep)  == UE_DIR_OUT)
				outmask |= 1 << UE_GET_ADDR(ep);
			else
				inmask |= 1 << UE_GET_ADDR(ep);
		}
	}

	/* Create all available endpoints except EP0 */
	for (ep = 1; ep < 16; ep++) {
		mode = (inmask & (1 << ep)) ? inmode : 0;
		mode |= (outmask & (1 << ep)) ? outmode : 0;
		if (mode == 0)
			continue;	/* no IN or OUT endpoint */

		pd = usb_make_dev(udev, NULL, ep, 0,
		    mode, UID_ROOT, GID_OPERATOR, 0600);

		if (pd != NULL)
			LIST_INSERT_HEAD(&udev->pd_list, pd, pd_next);
	}
}

static void
usb_cdev_free(struct usb_device *udev)
{
	struct usb_fs_privdata* pd;

	DPRINTFN(2, "Freeing device nodes\n");

	while ((pd = LIST_FIRST(&udev->pd_list)) != NULL) {
		KASSERT(pd->cdev->si_drv1 == pd, ("privdata corrupt"));

		LIST_REMOVE(pd, pd_next);

		usb_destroy_dev(pd);
	}
}
#endif

/*------------------------------------------------------------------------*
 *	usb_free_device
 *
 * This function is NULL safe and will free an USB device and its
 * children devices, if any.
 *
 * Flag values: Reserved, set to zero.
 *------------------------------------------------------------------------*/
void
usb_free_device(struct usb_device *udev, uint8_t flag)
{
	struct usb_bus *bus;

	if (udev == NULL)
		return;		/* already freed */

	DPRINTFN(4, "udev=%p port=%d\n", udev, udev->port_no);

	bus = udev->bus;

	/* set DETACHED state to prevent any further references */
	usb_set_device_state(udev, USB_STATE_DETACHED);

#if USB_HAVE_DEVCTL
	usb_notify_addq("DETACH", udev);
#endif

#if USB_HAVE_UGEN
	if (!rebooting) {
		printf("%s: <%s %s> at %s (disconnected)\n", udev->ugen_name,
		    usb_get_manufacturer(udev), usb_get_product(udev),
		    device_get_nameunit(bus->bdev));
	}

	/* Destroy UGEN symlink, if any */
	if (udev->ugen_symlink) {
		usb_free_symlink(udev->ugen_symlink);
		udev->ugen_symlink = NULL;
	}

	usb_destroy_dev(udev->ctrl_dev);
#endif

	if (udev->flags.usb_mode == USB_MODE_DEVICE) {
		/* stop receiving any control transfers (Device Side Mode) */
		usbd_transfer_unsetup(udev->ctrl_xfer, USB_CTRL_XFER_MAX);
	}

	/* the following will get the device unconfigured in software */
	usb_unconfigure(udev, USB_UNCFG_FLAG_FREE_EP0);

	/* final device unregister after all character devices are closed */
	usb_bus_port_set_device(bus, udev->parent_hub ?
	    udev->parent_hub->hub->ports + udev->port_index : NULL,
	    NULL, USB_ROOT_HUB_ADDR);

	/* unsetup any leftover default USB transfers */
	usbd_transfer_unsetup(udev->ctrl_xfer, USB_CTRL_XFER_MAX);

	/* template unsetup, if any */
	(usb_temp_unsetup_p) (udev);

	/* 
	 * Make sure that our clear-stall messages are not queued
	 * anywhere:
	 */
	USB_BUS_LOCK(udev->bus);
	usb_proc_mwait(USB_BUS_CS_PROC(udev->bus),
	    &udev->cs_msg[0], &udev->cs_msg[1]);
	USB_BUS_UNLOCK(udev->bus);

	/* wait for all references to go away */
	usb_wait_pending_refs(udev);
	
	sx_destroy(&udev->enum_sx);
	sx_destroy(&udev->sr_sx);
	sx_destroy(&udev->ctrl_sx);

	cv_destroy(&udev->ctrlreq_cv);
	cv_destroy(&udev->ref_cv);

	mtx_destroy(&udev->device_mtx);
#if USB_HAVE_UGEN
	KASSERT(LIST_FIRST(&udev->pd_list) == NULL, ("leaked cdev entries"));
#endif

	/* Uninitialise device */
	if (bus->methods->device_uninit != NULL)
		(bus->methods->device_uninit) (udev);

	/* free device */
	free(udev->serial, M_USB);
	free(udev->manufacturer, M_USB);
	free(udev->product, M_USB);
	free(udev, M_USB);
}

/*------------------------------------------------------------------------*
 *	usbd_get_iface
 *
 * This function is the safe way to get the USB interface structure
 * pointer by interface index.
 *
 * Return values:
 *   NULL: Interface not present.
 *   Else: Pointer to USB interface structure.
 *------------------------------------------------------------------------*/
struct usb_interface *
usbd_get_iface(struct usb_device *udev, uint8_t iface_index)
{
	struct usb_interface *iface = udev->ifaces + iface_index;

	if (iface_index >= udev->ifaces_max)
		return (NULL);
	return (iface);
}

/*------------------------------------------------------------------------*
 *	usbd_find_descriptor
 *
 * This function will lookup the first descriptor that matches the
 * criteria given by the arguments "type" and "subtype". Descriptors
 * will only be searched within the interface having the index
 * "iface_index".  If the "id" argument points to an USB descriptor,
 * it will be skipped before the search is started. This allows
 * searching for multiple descriptors using the same criteria. Else
 * the search is started after the interface descriptor.
 *
 * Return values:
 *   NULL: End of descriptors
 *   Else: A descriptor matching the criteria
 *------------------------------------------------------------------------*/
void   *
usbd_find_descriptor(struct usb_device *udev, void *id, uint8_t iface_index,
    uint8_t type, uint8_t type_mask,
    uint8_t subtype, uint8_t subtype_mask)
{
	struct usb_descriptor *desc;
	struct usb_config_descriptor *cd;
	struct usb_interface *iface;

	cd = usbd_get_config_descriptor(udev);
	if (cd == NULL) {
		return (NULL);
	}
	if (id == NULL) {
		iface = usbd_get_iface(udev, iface_index);
		if (iface == NULL) {
			return (NULL);
		}
		id = usbd_get_interface_descriptor(iface);
		if (id == NULL) {
			return (NULL);
		}
	}
	desc = (void *)id;

	while ((desc = usb_desc_foreach(cd, desc))) {

		if (desc->bDescriptorType == UDESC_INTERFACE) {
			break;
		}
		if (((desc->bDescriptorType & type_mask) == type) &&
		    ((desc->bDescriptorSubtype & subtype_mask) == subtype)) {
			return (desc);
		}
	}
	return (NULL);
}

/*------------------------------------------------------------------------*
 *	usb_devinfo
 *
 * This function will dump information from the device descriptor
 * belonging to the USB device pointed to by "udev", to the string
 * pointed to by "dst_ptr" having a maximum length of "dst_len" bytes
 * including the terminating zero.
 *------------------------------------------------------------------------*/
void
usb_devinfo(struct usb_device *udev, char *dst_ptr, uint16_t dst_len)
{
	struct usb_device_descriptor *udd = &udev->ddesc;
	uint16_t bcdDevice;
	uint16_t bcdUSB;

	bcdUSB = UGETW(udd->bcdUSB);
	bcdDevice = UGETW(udd->bcdDevice);

	if (udd->bDeviceClass != 0xFF) {
		snprintf(dst_ptr, dst_len, "%s %s, class %d/%d, rev %x.%02x/"
		    "%x.%02x, addr %d",
		    usb_get_manufacturer(udev),
		    usb_get_product(udev),
		    udd->bDeviceClass, udd->bDeviceSubClass,
		    (bcdUSB >> 8), bcdUSB & 0xFF,
		    (bcdDevice >> 8), bcdDevice & 0xFF,
		    udev->address);
	} else {
		snprintf(dst_ptr, dst_len, "%s %s, rev %x.%02x/"
		    "%x.%02x, addr %d",
		    usb_get_manufacturer(udev),
		    usb_get_product(udev),
		    (bcdUSB >> 8), bcdUSB & 0xFF,
		    (bcdDevice >> 8), bcdDevice & 0xFF,
		    udev->address);
	}
}

#ifdef USB_VERBOSE
/*
 * Descriptions of of known vendors and devices ("products").
 */
struct usb_knowndev {
	uint16_t vendor;
	uint16_t product;
	uint32_t flags;
	const char *vendorname;
	const char *productname;
};

#define	USB_KNOWNDEV_NOPROD	0x01	/* match on vendor only */

#include "usbdevs.h"
#include "usbdevs_data.h"
#endif					/* USB_VERBOSE */

static void
usbd_set_device_strings(struct usb_device *udev)
{
	struct usb_device_descriptor *udd = &udev->ddesc;
#ifdef USB_VERBOSE
	const struct usb_knowndev *kdp;
#endif
	char *temp_ptr;
	size_t temp_size;
	uint16_t vendor_id;
	uint16_t product_id;
	uint8_t do_unlock;

	/* Protect scratch area */
	do_unlock = usbd_ctrl_lock(udev);

	temp_ptr = (char *)udev->scratch.data;
	temp_size = sizeof(udev->scratch.data);

	vendor_id = UGETW(udd->idVendor);
	product_id = UGETW(udd->idProduct);

	/* get serial number string */
	usbd_req_get_string_any(udev, NULL, temp_ptr, temp_size,
	    udev->ddesc.iSerialNumber);
	udev->serial = strdup(temp_ptr, M_USB);

	/* get manufacturer string */
	usbd_req_get_string_any(udev, NULL, temp_ptr, temp_size,
	    udev->ddesc.iManufacturer);
	usb_trim_spaces(temp_ptr);
	if (temp_ptr[0] != '\0')
		udev->manufacturer = strdup(temp_ptr, M_USB);

	/* get product string */
	usbd_req_get_string_any(udev, NULL, temp_ptr, temp_size,
	    udev->ddesc.iProduct);
	usb_trim_spaces(temp_ptr);
	if (temp_ptr[0] != '\0')
		udev->product = strdup(temp_ptr, M_USB);

#ifdef USB_VERBOSE
	if (udev->manufacturer == NULL || udev->product == NULL) {
		for (kdp = usb_knowndevs; kdp->vendorname != NULL; kdp++) {
			if (kdp->vendor == vendor_id &&
			    (kdp->product == product_id ||
			    (kdp->flags & USB_KNOWNDEV_NOPROD) != 0))
				break;
		}
		if (kdp->vendorname != NULL) {
			/* XXX should use pointer to knowndevs string */
			if (udev->manufacturer == NULL) {
				udev->manufacturer = strdup(kdp->vendorname,
				    M_USB);
			}
			if (udev->product == NULL &&
			    (kdp->flags & USB_KNOWNDEV_NOPROD) == 0) {
				udev->product = strdup(kdp->productname,
				    M_USB);
			}
		}
	}
#endif
	/* Provide default strings if none were found */
	if (udev->manufacturer == NULL) {
		snprintf(temp_ptr, temp_size, "vendor 0x%04x", vendor_id);
		udev->manufacturer = strdup(temp_ptr, M_USB);
	}
	if (udev->product == NULL) {
		snprintf(temp_ptr, temp_size, "product 0x%04x", product_id);
		udev->product = strdup(temp_ptr, M_USB);
	}

	if (do_unlock)
		usbd_ctrl_unlock(udev);
}

/*
 * Returns:
 * See: USB_MODE_XXX
 */
enum usb_hc_mode
usbd_get_mode(struct usb_device *udev)
{
	return (udev->flags.usb_mode);
}

/*
 * Returns:
 * See: USB_SPEED_XXX
 */
enum usb_dev_speed
usbd_get_speed(struct usb_device *udev)
{
	return (udev->speed);
}

uint32_t
usbd_get_isoc_fps(struct usb_device *udev)
{
	;				/* indent fix */
	switch (udev->speed) {
	case USB_SPEED_LOW:
	case USB_SPEED_FULL:
		return (1000);
	default:
		return (8000);
	}
}

struct usb_device_descriptor *
usbd_get_device_descriptor(struct usb_device *udev)
{
	if (udev == NULL)
		return (NULL);		/* be NULL safe */
	return (&udev->ddesc);
}

struct usb_config_descriptor *
usbd_get_config_descriptor(struct usb_device *udev)
{
	if (udev == NULL)
		return (NULL);		/* be NULL safe */
	return (udev->cdesc);
}

/*------------------------------------------------------------------------*
 *	usb_test_quirk - test a device for a given quirk
 *
 * Return values:
 * 0: The USB device does not have the given quirk.
 * Else: The USB device has the given quirk.
 *------------------------------------------------------------------------*/
uint8_t
usb_test_quirk(const struct usb_attach_arg *uaa, uint16_t quirk)
{
	uint8_t found;
	uint8_t x;

	if (quirk == UQ_NONE)
		return (0);

	/* search the automatic per device quirks first */

	for (x = 0; x != USB_MAX_AUTO_QUIRK; x++) {
		if (uaa->device->autoQuirk[x] == quirk)
			return (1);
	}

	/* search global quirk table, if any */

	found = (usb_test_quirk_p) (&uaa->info, quirk);

	return (found);
}

struct usb_interface_descriptor *
usbd_get_interface_descriptor(struct usb_interface *iface)
{
	if (iface == NULL)
		return (NULL);		/* be NULL safe */
	return (iface->idesc);
}

uint8_t
usbd_get_interface_altindex(struct usb_interface *iface)
{
	return (iface->alt_index);
}

uint8_t
usbd_get_bus_index(struct usb_device *udev)
{
	return ((uint8_t)device_get_unit(udev->bus->bdev));
}

uint8_t
usbd_get_device_index(struct usb_device *udev)
{
	return (udev->device_index);
}

#if USB_HAVE_DEVCTL
static void
usb_notify_addq(const char *type, struct usb_device *udev)
{
	struct usb_interface *iface;
	struct sbuf *sb;
	int i;

	/* announce the device */
	sb = sbuf_new_auto();
	sbuf_printf(sb,
#if USB_HAVE_UGEN
	    "ugen=%s "
	    "cdev=%s "
#endif
	    "vendor=0x%04x "
	    "product=0x%04x "
	    "devclass=0x%02x "
	    "devsubclass=0x%02x "
	    "sernum=\"%s\" "
	    "release=0x%04x "
	    "mode=%s "
	    "port=%u "
#if USB_HAVE_UGEN
	    "parent=%s"
#endif
	    "",
#if USB_HAVE_UGEN
	    udev->ugen_name,
	    udev->ugen_name,
#endif
	    UGETW(udev->ddesc.idVendor),
	    UGETW(udev->ddesc.idProduct),
	    udev->ddesc.bDeviceClass,
	    udev->ddesc.bDeviceSubClass,
	    usb_get_serial(udev),
	    UGETW(udev->ddesc.bcdDevice),
	    (udev->flags.usb_mode == USB_MODE_HOST) ? "host" : "device",
	    udev->port_no
#if USB_HAVE_UGEN
	    , udev->parent_hub != NULL ?
		udev->parent_hub->ugen_name :
		device_get_nameunit(device_get_parent(udev->bus->bdev))
#endif
	    );
	sbuf_finish(sb);
	devctl_notify("USB", "DEVICE", type, sbuf_data(sb));
	sbuf_delete(sb);

	/* announce each interface */
	for (i = 0; i < USB_IFACE_MAX; i++) {
		iface = usbd_get_iface(udev, i);
		if (iface == NULL)
			break;		/* end of interfaces */
		if (iface->idesc == NULL)
			continue;	/* no interface descriptor */

		sb = sbuf_new_auto();
		sbuf_printf(sb,
#if USB_HAVE_UGEN
		    "ugen=%s "
		    "cdev=%s "
#endif
		    "vendor=0x%04x "
		    "product=0x%04x "
		    "devclass=0x%02x "
		    "devsubclass=0x%02x "
		    "sernum=\"%s\" "
		    "release=0x%04x "
		    "mode=%s "
		    "interface=%d "
		    "endpoints=%d "
		    "intclass=0x%02x "
		    "intsubclass=0x%02x "
		    "intprotocol=0x%02x",
#if USB_HAVE_UGEN
		    udev->ugen_name,
		    udev->ugen_name,
#endif
		    UGETW(udev->ddesc.idVendor),
		    UGETW(udev->ddesc.idProduct),
		    udev->ddesc.bDeviceClass,
		    udev->ddesc.bDeviceSubClass,
		    usb_get_serial(udev),
		    UGETW(udev->ddesc.bcdDevice),
		    (udev->flags.usb_mode == USB_MODE_HOST) ? "host" : "device",
		    iface->idesc->bInterfaceNumber,
		    iface->idesc->bNumEndpoints,
		    iface->idesc->bInterfaceClass,
		    iface->idesc->bInterfaceSubClass,
		    iface->idesc->bInterfaceProtocol);
		sbuf_finish(sb);
		devctl_notify("USB", "INTERFACE", type, sbuf_data(sb));
		sbuf_delete(sb);
	}
}
#endif

#if USB_HAVE_UGEN
/*------------------------------------------------------------------------*
 *	usb_fifo_free_wrap
 *
 * This function will free the FIFOs.
 *
 * Description of "flag" argument: If the USB_UNCFG_FLAG_FREE_EP0 flag
 * is set and "iface_index" is set to "USB_IFACE_INDEX_ANY", we free
 * all FIFOs. If the USB_UNCFG_FLAG_FREE_EP0 flag is not set and
 * "iface_index" is set to "USB_IFACE_INDEX_ANY", we free all non
 * control endpoint FIFOs. If "iface_index" is not set to
 * "USB_IFACE_INDEX_ANY" the flag has no effect.
 *------------------------------------------------------------------------*/
static void
usb_fifo_free_wrap(struct usb_device *udev,
    uint8_t iface_index, uint8_t flag)
{
	struct usb_fifo *f;
	uint16_t i;

	/*
	 * Free any USB FIFOs on the given interface:
	 */
	for (i = 0; i != USB_FIFO_MAX; i++) {
		f = udev->fifo[i];
		if (f == NULL) {
			continue;
		}
		/* Check if the interface index matches */
		if (iface_index == f->iface_index) {
			if (f->methods != &usb_ugen_methods) {
				/*
				 * Don't free any non-generic FIFOs in
				 * this case.
				 */
				continue;
			}
			if ((f->dev_ep_index == 0) &&
			    (f->fs_xfer == NULL)) {
				/* no need to free this FIFO */
				continue;
			}
		} else if (iface_index == USB_IFACE_INDEX_ANY) {
			if ((f->methods == &usb_ugen_methods) &&
			    (f->dev_ep_index == 0) &&
			    (!(flag & USB_UNCFG_FLAG_FREE_EP0)) &&
			    (f->fs_xfer == NULL)) {
				/* no need to free this FIFO */
				continue;
			}
		} else {
			/* no need to free this FIFO */
			continue;
		}
		/* free this FIFO */
		usb_fifo_free(f);
	}
}
#endif

/*------------------------------------------------------------------------*
 *	usb_peer_can_wakeup
 *
 * Return values:
 * 0: Peer cannot do resume signalling.
 * Else: Peer can do resume signalling.
 *------------------------------------------------------------------------*/
uint8_t
usb_peer_can_wakeup(struct usb_device *udev)
{
	const struct usb_config_descriptor *cdp;

	cdp = udev->cdesc;
	if ((cdp != NULL) && (udev->flags.usb_mode == USB_MODE_HOST)) {
		return (cdp->bmAttributes & UC_REMOTE_WAKEUP);
	}
	return (0);			/* not supported */
}

void
usb_set_device_state(struct usb_device *udev, enum usb_dev_state state)
{

	KASSERT(state < USB_STATE_MAX, ("invalid udev state"));

	DPRINTF("udev %p state %s -> %s\n", udev,
	    usb_statestr(udev->state), usb_statestr(state));

#if USB_HAVE_UGEN
	mtx_lock(&usb_ref_lock);
#endif
	udev->state = state;
#if USB_HAVE_UGEN
	mtx_unlock(&usb_ref_lock);
#endif
	if (udev->bus->methods->device_state_change != NULL)
		(udev->bus->methods->device_state_change) (udev);
}

enum usb_dev_state
usb_get_device_state(struct usb_device *udev)
{
	if (udev == NULL)
		return (USB_STATE_DETACHED);
	return (udev->state);
}

uint8_t
usbd_device_attached(struct usb_device *udev)
{
	return (udev->state > USB_STATE_DETACHED);
}

/*
 * The following function locks enumerating the given USB device. If
 * the lock is already grabbed this function returns zero. Else a
 * a value of one is returned.
 */
uint8_t
usbd_enum_lock(struct usb_device *udev)
{
	if (sx_xlocked(&udev->enum_sx))
		return (0);

	sx_xlock(&udev->enum_sx);
	sx_xlock(&udev->sr_sx);
	/* 
	 * NEWBUS LOCK NOTE: We should check if any parent SX locks
	 * are locked before locking Giant. Else the lock can be
	 * locked multiple times.
	 */
	mtx_lock(&Giant);
	return (1);
}

#if USB_HAVE_UGEN
/*
 * This function is the same like usbd_enum_lock() except a value of
 * 255 is returned when a signal is pending:
 */
uint8_t
usbd_enum_lock_sig(struct usb_device *udev)
{
	if (sx_xlocked(&udev->enum_sx))
		return (0);
	if (sx_xlock_sig(&udev->enum_sx))
		return (255);
	if (sx_xlock_sig(&udev->sr_sx)) {
		sx_xunlock(&udev->enum_sx);
		return (255);
	}
	mtx_lock(&Giant);
	return (1);
}
#endif

/* The following function unlocks enumerating the given USB device. */

void
usbd_enum_unlock(struct usb_device *udev)
{
	mtx_unlock(&Giant);
	sx_xunlock(&udev->enum_sx);
	sx_xunlock(&udev->sr_sx);
}

/* The following function locks suspend and resume. */

void
usbd_sr_lock(struct usb_device *udev)
{
	sx_xlock(&udev->sr_sx);
	/* 
	 * NEWBUS LOCK NOTE: We should check if any parent SX locks
	 * are locked before locking Giant. Else the lock can be
	 * locked multiple times.
	 */
	mtx_lock(&Giant);
}

/* The following function unlocks suspend and resume. */

void
usbd_sr_unlock(struct usb_device *udev)
{
	mtx_unlock(&Giant);
	sx_xunlock(&udev->sr_sx);
}

/*
 * The following function checks the enumerating lock for the given
 * USB device.
 */

uint8_t
usbd_enum_is_locked(struct usb_device *udev)
{
	return (sx_xlocked(&udev->enum_sx));
}

/*
 * The following function is used to serialize access to USB control
 * transfers and the USB scratch area. If the lock is already grabbed
 * this function returns zero. Else a value of one is returned.
 */
uint8_t
usbd_ctrl_lock(struct usb_device *udev)
{
	if (sx_xlocked(&udev->ctrl_sx))
		return (0);
	sx_xlock(&udev->ctrl_sx);

	/*
	 * We need to allow suspend and resume at this point, else the
	 * control transfer will timeout if the device is suspended!
	 */
	if (usbd_enum_is_locked(udev))
		usbd_sr_unlock(udev);
	return (1);
}

void
usbd_ctrl_unlock(struct usb_device *udev)
{
	sx_xunlock(&udev->ctrl_sx);

	/*
	 * Restore the suspend and resume lock after we have unlocked
	 * the USB control transfer lock to avoid LOR:
	 */
	if (usbd_enum_is_locked(udev))
		usbd_sr_lock(udev);
}

/*
 * The following function is used to set the per-interface specific
 * plug and play information. The string referred to by the pnpinfo
 * argument can safely be freed after calling this function. The
 * pnpinfo of an interface will be reset at device detach or when
 * passing a NULL argument to this function. This function
 * returns zero on success, else a USB_ERR_XXX failure code.
 */

usb_error_t 
usbd_set_pnpinfo(struct usb_device *udev, uint8_t iface_index, const char *pnpinfo)
{
	struct usb_interface *iface;

	iface = usbd_get_iface(udev, iface_index);
	if (iface == NULL)
		return (USB_ERR_INVAL);

	if (iface->pnpinfo != NULL) {
		free(iface->pnpinfo, M_USBDEV);
		iface->pnpinfo = NULL;
	}

	if (pnpinfo == NULL || pnpinfo[0] == 0)
		return (0);		/* success */

	iface->pnpinfo = strdup(pnpinfo, M_USBDEV);
	if (iface->pnpinfo == NULL)
		return (USB_ERR_NOMEM);

	return (0);			/* success */
}

usb_error_t
usbd_add_dynamic_quirk(struct usb_device *udev, uint16_t quirk)
{
	uint8_t x;

	for (x = 0; x != USB_MAX_AUTO_QUIRK; x++) {
		if (udev->autoQuirk[x] == 0 ||
		    udev->autoQuirk[x] == quirk) {
			udev->autoQuirk[x] = quirk;
			return (0);	/* success */
		}
	}
	return (USB_ERR_NOMEM);
}

/*
 * The following function is used to select the endpoint mode. It
 * should not be called outside enumeration context.
 */

usb_error_t
usbd_set_endpoint_mode(struct usb_device *udev, struct usb_endpoint *ep,
    uint8_t ep_mode)
{   
	usb_error_t error;
	uint8_t do_unlock;

	/* Prevent re-enumeration */
	do_unlock = usbd_enum_lock(udev);

	if (udev->bus->methods->set_endpoint_mode != NULL) {
		error = (udev->bus->methods->set_endpoint_mode) (
		    udev, ep, ep_mode);
	} else if (ep_mode != USB_EP_MODE_DEFAULT) {
		error = USB_ERR_INVAL;
	} else {
		error = 0;
	}

	/* only set new mode regardless of error */
	ep->ep_mode = ep_mode;

	if (do_unlock)
		usbd_enum_unlock(udev);
	return (error);
}

uint8_t
usbd_get_endpoint_mode(struct usb_device *udev, struct usb_endpoint *ep)
{
	return (ep->ep_mode);
}
