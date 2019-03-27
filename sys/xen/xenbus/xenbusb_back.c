/******************************************************************************
 * Talks to Xen Store to figure out what devices we have.
 *
 * Copyright (C) 2009, 2010 Spectra Logic Corporation
 * Copyright (C) 2008 Doug Rabson
 * Copyright (C) 2005 Rusty Russell, IBM Corporation
 * Copyright (C) 2005 Mike Wray, Hewlett-Packard
 * Copyright (C) 2005 XenSource Ltd
 * 
 * This file may be distributed separately from the Linux kernel, or
 * incorporated into other software packages, subject to the following license:
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/**
 * \file xenbusb_back.c
 *
 * XenBus management of the NewBus bus containing the backend instances of
 * Xen split devices.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/sbuf.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/systm.h>
#include <sys/sx.h>
#include <sys/taskqueue.h>

#include <machine/xen/xen-os.h>
#include <machine/stdarg.h>

#include <xen/gnttab.h>
#include <xen/xenbus/xenbusvar.h>
#include <xen/xenbus/xenbusb.h>


/*------------------ Private Device Attachment Functions  --------------------*/
/**
 * \brief Probe for the existance of the XenBus back bus.
 *
 * \param dev  NewBus device_t for this XenBus back bus instance.
 *
 * \return  Always returns 0 indicating success.
 */
static int 
xenbusb_back_probe(device_t dev)
{
	device_set_desc(dev, "Xen Backend Devices");

	return (0);
}

/**
 * \brief Attach the XenBus back bus.
 *
 * \param dev  NewBus device_t for this XenBus back bus instance.
 *
 * \return  On success, 0. Otherwise an errno value indicating the
 *          type of failure.
 */
static int
xenbusb_back_attach(device_t dev)
{
	struct xenbusb_softc *xbs;
	int error;

	xbs = device_get_softc(dev);
	error = xenbusb_attach(dev, "backend", /*id_components*/2);

	/*
	 * Backend devices operate to serve other domains,
	 * so there is no need to hold up boot processing
	 * while connections to foreign domains are made.
	 */
	mtx_lock(&xbs->xbs_lock);
	if ((xbs->xbs_flags & XBS_ATTACH_CH_ACTIVE) != 0) {
		xbs->xbs_flags &= ~XBS_ATTACH_CH_ACTIVE;
		mtx_unlock(&xbs->xbs_lock);
		config_intrhook_disestablish(&xbs->xbs_attach_ch);
	} else {
		mtx_unlock(&xbs->xbs_lock);
	}

	return (error);
}

/**
 * \brief Enumerate all devices of the given type on this bus.
 *
 * \param dev   NewBus device_t for this XenBus backend bus instance.
 * \param type  String indicating the device sub-tree (e.g. "vfb", "vif")
 *              to enumerate. 
 *
 * \return  On success, 0. Otherwise an errno value indicating the
 *          type of failure.
 *
 * Devices that are found are entered into the NewBus hierarchy via
 * xenbusb_add_device().  xenbusb_add_device() ignores duplicate detects
 * and ignores duplicate devices, so it can be called unconditionally
 * for any device found in the XenStore.
 *
 * The backend XenStore hierarchy has the following format:
 *
 *     backend/<device type>/<frontend vm id>/<device id>
 *
 */
static int
xenbusb_back_enumerate_type(device_t dev, const char *type)
{
	struct xenbusb_softc *xbs;
	const char **vms;
	u_int vm_idx;
	u_int vm_count;
	int error;

	xbs = device_get_softc(dev);
	error = xs_directory(XST_NIL, xbs->xbs_node, type, &vm_count, &vms);
	if (error)
		return (error);
	for (vm_idx = 0; vm_idx < vm_count; vm_idx++) {
		struct sbuf *vm_path;
		const char *vm;
		const char **devs;
		u_int dev_idx;
		u_int dev_count;

		vm = vms[vm_idx];

		vm_path = xs_join(type, vm);
		error = xs_directory(XST_NIL, xbs->xbs_node, sbuf_data(vm_path),
		    &dev_count, &devs);
		sbuf_delete(vm_path);
		if (error)
			break;

		for (dev_idx = 0; dev_idx < dev_count; dev_idx++) {
			const char *dev_num;
			struct sbuf *id;
			
			dev_num = devs[dev_idx];
			id = xs_join(vm, dev_num);
			xenbusb_add_device(dev, type, sbuf_data(id));
			sbuf_delete(id);
		}
		free(devs, M_XENSTORE);
	}

	free(vms, M_XENSTORE);

	return (0);
}

/**
 * \brief Determine and store the XenStore path for the other end of
 *        a split device whose local end is represented by ivars.
 *
 * \param dev    NewBus device_t for this XenBus backend bus instance.
 * \param ivars  Instance variables from the XenBus child device for
 *               which to perform this function.
 *
 * \return  On success, 0. Otherwise an errno value indicating the
 *          type of failure.
 *
 * If successful, the xd_otherend_path field of the child's instance
 * variables will be updated.
 *
 */
static int
xenbusb_back_get_otherend_node(device_t dev, struct xenbus_device_ivars *ivars)
{
	char *otherend_path;
	int error;

	if (ivars->xd_otherend_path != NULL) {
		free(ivars->xd_otherend_path, M_XENBUS);
		ivars->xd_otherend_path = NULL;
	}
		
	error = xs_gather(XST_NIL, ivars->xd_node,
	    "frontend-id", "%i", &ivars->xd_otherend_id,
	    "frontend", NULL, &otherend_path,
	    NULL);

	if (error == 0) {
		ivars->xd_otherend_path = strdup(otherend_path, M_XENBUS);
		ivars->xd_otherend_path_len = strlen(otherend_path);
		free(otherend_path, M_XENSTORE);
	}
	return (error);
}

/**
 * \brief Backend XenBus method implementing responses to peer state changes.
 * 
 * \param bus       The XenBus bus parent of child.
 * \param child     The XenBus child whose peer stat has changed.
 * \param state     The current state of the peer.
 */
static void
xenbusb_back_otherend_changed(device_t bus, device_t child,
			      enum xenbus_state peer_state)
{
	/* Perform default processing of state. */
	xenbusb_otherend_changed(bus, child, peer_state);

	/*
	 * "Online" devices are never fully detached in the
	 * newbus sense.  Only the front<->back connection is
	 * torn down.  If the front returns to the initialising
	 * state after closing a previous connection, signal
	 * our willingness to reconnect and that all necessary
	 * XenStore data for feature negotiation is present.
	 */
	if (peer_state == XenbusStateInitialising
	 && xenbus_dev_is_online(child) != 0
	 && xenbus_get_state(child) == XenbusStateClosed)
		xenbus_set_state(child, XenbusStateInitWait);
}

/**
 * \brief Backend XenBus method implementing responses to local
 *        XenStore changes.
 * 
 * \param bus    The XenBus bus parent of child.
 * \param child  The XenBus child whose peer stat has changed.
 * \param_path   The tree relative sub-path to the modified node.  The empty
 *               string indicates the root of the tree was destroyed.
 */
static void
xenbusb_back_localend_changed(device_t bus, device_t child, const char *path)
{

	xenbusb_localend_changed(bus, child, path);

	if (strcmp(path, "/state") != 0
	 && strcmp(path, "/online") != 0)
		return;

	if (xenbus_get_state(child) != XenbusStateClosed
	 || xenbus_dev_is_online(child) != 0)
		return;

	/*
	 * Cleanup the hotplug entry in the XenStore if
	 * present.  The control domain expects any userland
	 * component associated with this device to destroy
	 * this node in order to signify it is safe to 
	 * teardown the device.  However, not all backends
	 * rely on userland components, and those that
	 * do should either use a communication channel
	 * other than the XenStore, or ensure the hotplug
	 * data is already cleaned up.
	 *
	 * This removal ensures that no matter what path
	 * is taken to mark a back-end closed, the control
	 * domain will understand that it is closed.
	 */
	xs_rm(XST_NIL, xenbus_get_node(child), "hotplug-status");
}

/*-------------------- Private Device Attachment Data  -----------------------*/
static device_method_t xenbusb_back_methods[] = { 
	/* Device interface */ 
	DEVMETHOD(device_identify,	xenbusb_identify),
	DEVMETHOD(device_probe,         xenbusb_back_probe), 
	DEVMETHOD(device_attach,        xenbusb_back_attach), 
	DEVMETHOD(device_detach,        bus_generic_detach), 
	DEVMETHOD(device_shutdown,      bus_generic_shutdown), 
	DEVMETHOD(device_suspend,       bus_generic_suspend), 
	DEVMETHOD(device_resume,        xenbusb_resume), 
 
	/* Bus Interface */ 
	DEVMETHOD(bus_print_child,      xenbusb_print_child),
	DEVMETHOD(bus_read_ivar,        xenbusb_read_ivar), 
	DEVMETHOD(bus_write_ivar,       xenbusb_write_ivar), 
	DEVMETHOD(bus_alloc_resource,   bus_generic_alloc_resource),
	DEVMETHOD(bus_release_resource, bus_generic_release_resource),
	DEVMETHOD(bus_activate_resource, bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),
 
	/* XenBus Bus Interface */
	DEVMETHOD(xenbusb_enumerate_type, xenbusb_back_enumerate_type),
	DEVMETHOD(xenbusb_get_otherend_node, xenbusb_back_get_otherend_node),
	DEVMETHOD(xenbusb_otherend_changed, xenbusb_back_otherend_changed),
	DEVMETHOD(xenbusb_localend_changed, xenbusb_back_localend_changed),
	{ 0, 0 } 
}; 

DEFINE_CLASS_0(xenbusb_back, xenbusb_back_driver, xenbusb_back_methods,
	       sizeof(struct xenbusb_softc));
devclass_t xenbusb_back_devclass; 
 
DRIVER_MODULE(xenbusb_back, xenstore, xenbusb_back_driver,
	      xenbusb_back_devclass, 0, 0);
