/******************************************************************************
 * Copyright (C) 2010 Spectra Logic Corporation
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
 * \file xenbusb.c
 *
 * \brief Shared support functions for managing the NewBus buses that contain
 *        Xen front and back end device instances.
 *
 * The NewBus implementation of XenBus attaches a xenbusb_front and xenbusb_back
 * child bus to the xenstore device.  This strategy allows the small differences
 * in the handling of XenBus operations for front and back devices to be handled
 * as overrides in xenbusb_front/back.c.  Front and back specific device
 * classes are also provided so device drivers can register for the devices they
 * can handle without the need to filter within their probe routines.  The
 * net result is a device hierarchy that might look like this:
 *
 * xenstore0/
 *           xenbusb_front0/
 *                         xn0
 *                         xbd0
 *                         xbd1
 *           xenbusb_back0/
 *                        xbbd0
 *                        xnb0
 *                        xnb1
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
#include <xen/xenstore/xenstorevar.h>
#include <xen/xenbus/xenbusb.h>
#include <xen/xenbus/xenbusvar.h>

/*------------------------- Private Functions --------------------------------*/
/**
 * \brief Deallocate XenBus device instance variables.
 *
 * \param ivars  The instance variable block to free.
 */
static void
xenbusb_free_child_ivars(struct xenbus_device_ivars *ivars)
{
	if (ivars->xd_otherend_watch.node != NULL) {
		xs_unregister_watch(&ivars->xd_otherend_watch);
		free(ivars->xd_otherend_watch.node, M_XENBUS);
		ivars->xd_otherend_watch.node = NULL;
	}

	if (ivars->xd_local_watch.node != NULL) {
		xs_unregister_watch(&ivars->xd_local_watch);
		ivars->xd_local_watch.node = NULL;
	}

	if (ivars->xd_node != NULL) {
		free(ivars->xd_node, M_XENBUS);
		ivars->xd_node = NULL;
	}
	ivars->xd_node_len = 0;

	if (ivars->xd_type != NULL) {
		free(ivars->xd_type, M_XENBUS);
		ivars->xd_type = NULL;
	}

	if (ivars->xd_otherend_path != NULL) {
		free(ivars->xd_otherend_path, M_XENBUS);
		ivars->xd_otherend_path = NULL;
	}
	ivars->xd_otherend_path_len = 0;

	free(ivars, M_XENBUS);
}

/**
 * XenBus watch callback registered against the "state" XenStore
 * node of the other-end of a split device connection.
 *
 * This callback is invoked whenever the state of a device instance's
 * peer changes.
 *
 * \param watch      The xs_watch object used to register this callback
 *                   function.
 * \param vec        An array of pointers to NUL terminated strings containing
 *                   watch event data.  The vector should be indexed via the
 *                   xs_watch_type enum in xs_wire.h.
 * \param vec_size   The number of elements in vec.
 */
static void
xenbusb_otherend_watch_cb(struct xs_watch *watch, const char **vec,
    unsigned int vec_size __unused)
{
	struct xenbus_device_ivars *ivars;
	device_t child;
	device_t bus;
	const char *path;
	enum xenbus_state newstate;

	ivars = (struct xenbus_device_ivars *)watch->callback_data;
	child = ivars->xd_dev;
	bus = device_get_parent(child);

	path = vec[XS_WATCH_PATH];
	if (ivars->xd_otherend_path == NULL
	 || strncmp(ivars->xd_otherend_path, path, ivars->xd_otherend_path_len))
		return;

	newstate = xenbus_read_driver_state(ivars->xd_otherend_path);
	XENBUSB_OTHEREND_CHANGED(bus, child, newstate);
}

/**
 * XenBus watch callback registered against the XenStore sub-tree
 * represnting the local half of a split device connection.
 *
 * This callback is invoked whenever any XenStore data in the subtree
 * is modified, either by us or another privledged domain.
 *
 * \param watch      The xs_watch object used to register this callback
 *                   function.
 * \param vec        An array of pointers to NUL terminated strings containing
 *                   watch event data.  The vector should be indexed via the
 *                   xs_watch_type enum in xs_wire.h.
 * \param vec_size   The number of elements in vec.
 *
 */
static void
xenbusb_local_watch_cb(struct xs_watch *watch, const char **vec,
    unsigned int vec_size __unused)
{
	struct xenbus_device_ivars *ivars;
	device_t child;
	device_t bus;
	const char *path;

	ivars = (struct xenbus_device_ivars *)watch->callback_data;
	child = ivars->xd_dev;
	bus = device_get_parent(child);

	path = vec[XS_WATCH_PATH];
	if (ivars->xd_node == NULL
	 || strncmp(ivars->xd_node, path, ivars->xd_node_len))
		return;

	XENBUSB_LOCALEND_CHANGED(bus, child, &path[ivars->xd_node_len]);
}

/**
 * Search our internal record of configured devices (not the XenStore)
 * to determine if the XenBus device indicated by \a node is known to
 * the system.
 *
 * \param dev   The XenBus bus instance to search for device children.
 * \param node  The XenStore node path for the device to find.
 *
 * \return  The device_t of the found device if any, or NULL.
 *
 * \note device_t is a pointer type, so it can be compared against
 *       NULL for validity. 
 */
static device_t
xenbusb_device_exists(device_t dev, const char *node)
{
	device_t *kids;
	device_t result;
	struct xenbus_device_ivars *ivars;
	int i, count;

	if (device_get_children(dev, &kids, &count))
		return (FALSE);

	result = NULL;
	for (i = 0; i < count; i++) {
		ivars = device_get_ivars(kids[i]);
		if (!strcmp(ivars->xd_node, node)) {
			result = kids[i];
			break;
		}
	}
	free(kids, M_TEMP);

	return (result);
}

static void
xenbusb_delete_child(device_t dev, device_t child)
{
	struct xenbus_device_ivars *ivars;

	ivars = device_get_ivars(child);

	/*
	 * We no longer care about the otherend of the
	 * connection.  Cancel the watches now so that we
	 * don't try to handle an event for a partially
	 * detached child.
	 */
	if (ivars->xd_otherend_watch.node != NULL)
		xs_unregister_watch(&ivars->xd_otherend_watch);
	if (ivars->xd_local_watch.node != NULL)
		xs_unregister_watch(&ivars->xd_local_watch);
	
	device_delete_child(dev, child);
	xenbusb_free_child_ivars(ivars);
}

/**
 * \param dev    The NewBus device representing this XenBus bus.
 * \param child	 The NewBus device representing a child of dev%'s XenBus bus.
 */
static void
xenbusb_verify_device(device_t dev, device_t child)
{
	if (xs_exists(XST_NIL, xenbus_get_node(child), "") == 0) {

		/*
		 * Device tree has been removed from Xenbus.
		 * Tear down the device.
		 */
		xenbusb_delete_child(dev, child);
	}
}

/**
 * \brief Enumerate the devices on a XenBus bus and register them with
 *        the NewBus device tree.
 *
 * xenbusb_enumerate_bus() will create entries (in state DS_NOTPRESENT)
 * for nodes that appear in the XenStore, but will not invoke probe/attach
 * operations on drivers.  Probe/Attach processing must be separately
 * performed via an invocation of xenbusb_probe_children().  This is usually
 * done via the xbs_probe_children task.
 *
 * \param xbs  XenBus Bus device softc of the owner of the bus to enumerate.
 *
 * \return  On success, 0. Otherwise an errno value indicating the
 *          type of failure.
 */
static int
xenbusb_enumerate_bus(struct xenbusb_softc *xbs)
{
	const char **types;
	u_int type_idx;
	u_int type_count;
	int error;

	error = xs_directory(XST_NIL, xbs->xbs_node, "", &type_count, &types);
	if (error)
		return (error);

	for (type_idx = 0; type_idx < type_count; type_idx++)
		XENBUSB_ENUMERATE_TYPE(xbs->xbs_dev, types[type_idx]);

	free(types, M_XENSTORE);

	return (0);
}

/**
 * Handler for all generic XenBus device systcl nodes.
 */
static int
xenbusb_device_sysctl_handler(SYSCTL_HANDLER_ARGS)  
{
	device_t dev;
        const char *value;

	dev = (device_t)arg1;
        switch (arg2) {
	case XENBUS_IVAR_NODE:
		value = xenbus_get_node(dev);
		break;
	case XENBUS_IVAR_TYPE:
		value = xenbus_get_type(dev);
		break;
	case XENBUS_IVAR_STATE:
		value = xenbus_strstate(xenbus_get_state(dev));
		break;
	case XENBUS_IVAR_OTHEREND_ID:
		return (sysctl_handle_int(oidp, NULL,
					  xenbus_get_otherend_id(dev),
					  req));
		/* NOTREACHED */
	case XENBUS_IVAR_OTHEREND_PATH:
		value = xenbus_get_otherend_path(dev);
                break;
	default:
		return (EINVAL);
	}
	return (SYSCTL_OUT_STR(req, value));
}

/**
 * Create read-only systcl nodes for xenbusb device ivar data.
 *
 * \param dev  The XenBus device instance to register with sysctl.
 */
static void
xenbusb_device_sysctl_init(device_t dev)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid      *tree;

	ctx  = device_get_sysctl_ctx(dev);
	tree = device_get_sysctl_tree(dev);

        SYSCTL_ADD_PROC(ctx,
			SYSCTL_CHILDREN(tree),
			OID_AUTO,
			"xenstore_path",
			CTLTYPE_STRING | CTLFLAG_RD,
			dev,
			XENBUS_IVAR_NODE,
			xenbusb_device_sysctl_handler,
			"A",
			"XenStore path to device");

        SYSCTL_ADD_PROC(ctx,
			SYSCTL_CHILDREN(tree),
			OID_AUTO,
			"xenbus_dev_type",
			CTLTYPE_STRING | CTLFLAG_RD,
			dev,
			XENBUS_IVAR_TYPE,
			xenbusb_device_sysctl_handler,
			"A",
			"XenBus device type");

        SYSCTL_ADD_PROC(ctx,
			SYSCTL_CHILDREN(tree),
			OID_AUTO,
			"xenbus_connection_state",
			CTLTYPE_STRING | CTLFLAG_RD,
			dev,
			XENBUS_IVAR_STATE,
			xenbusb_device_sysctl_handler,
			"A",
			"XenBus state of peer connection");

        SYSCTL_ADD_PROC(ctx,
			SYSCTL_CHILDREN(tree),
			OID_AUTO,
			"xenbus_peer_domid",
			CTLTYPE_INT | CTLFLAG_RD,
			dev,
			XENBUS_IVAR_OTHEREND_ID,
			xenbusb_device_sysctl_handler,
			"I",
			"Xen domain ID of peer");

        SYSCTL_ADD_PROC(ctx,
			SYSCTL_CHILDREN(tree),
			OID_AUTO,
			"xenstore_peer_path",
			CTLTYPE_STRING | CTLFLAG_RD,
			dev,
			XENBUS_IVAR_OTHEREND_PATH,
			xenbusb_device_sysctl_handler,
			"A",
			"XenStore path to peer device");
}

/**
 * \brief Decrement the number of XenBus child devices in the
 *        connecting state by one and release the xbs_attch_ch
 *        interrupt configuration hook if the connecting count
 *        drops to zero.
 *
 * \param xbs  XenBus Bus device softc of the owner of the bus to enumerate.
 */
static void
xenbusb_release_confighook(struct xenbusb_softc *xbs)
{
	mtx_lock(&xbs->xbs_lock);
	KASSERT(xbs->xbs_connecting_children > 0,
		("Connecting device count error\n"));
	xbs->xbs_connecting_children--;
	if (xbs->xbs_connecting_children == 0
	 && (xbs->xbs_flags & XBS_ATTACH_CH_ACTIVE) != 0) {
		xbs->xbs_flags &= ~XBS_ATTACH_CH_ACTIVE;
		mtx_unlock(&xbs->xbs_lock);
		config_intrhook_disestablish(&xbs->xbs_attach_ch);
	} else {
		mtx_unlock(&xbs->xbs_lock);
	}
}

/**
 * \brief Verify the existance of attached device instances and perform
 *        probe/attach processing for newly arrived devices.
 *
 * \param dev  The NewBus device representing this XenBus bus.
 *
 * \return  On success, 0. Otherwise an errno value indicating the
 *          type of failure.
 */
static int
xenbusb_probe_children(device_t dev)
{
	device_t *kids;
	struct xenbus_device_ivars *ivars;
	int i, count, error;

	if (device_get_children(dev, &kids, &count) == 0) {
		for (i = 0; i < count; i++) {
			if (device_get_state(kids[i]) != DS_NOTPRESENT) {
				/*
				 * We already know about this one.
				 * Make sure it's still here.
				 */
				xenbusb_verify_device(dev, kids[i]);
				continue;
			}

			error = device_probe_and_attach(kids[i]);
			if (error == ENXIO) {
				struct xenbusb_softc *xbs;

				/*
				 * We don't have a PV driver for this device.
				 * However, an emulated device we do support
				 * may share this backend.  Hide the node from
				 * XenBus until the next rescan, but leave it's
				 * state unchanged so we don't inadvertently
				 * prevent attachment of any emulated device.
				 */
				xenbusb_delete_child(dev, kids[i]);

				/*
				 * Since the XenStore state of this device
				 * still indicates a pending attach, manually
				 * release it's hold on the boot process.
				 */
				xbs = device_get_softc(dev);
				xenbusb_release_confighook(xbs);

				continue;
			} else if (error) {
				/*
				 * Transition device to the closed state
				 * so the world knows that attachment will
				 * not occur.
				 */
				xenbus_set_state(kids[i], XenbusStateClosed);

				/*
				 * Remove our record of this device.
				 * So long as it remains in the closed
				 * state in the XenStore, we will not find
				 * it again.  The state will only change
				 * if the control domain actively reconfigures
				 * this device.
				 */
				xenbusb_delete_child(dev, kids[i]);

				continue;
			}
			/*
			 * Augment default newbus provided dynamic sysctl
			 * variables with the standard ivar contents of
			 * XenBus devices.
			 */
			xenbusb_device_sysctl_init(kids[i]);

			/*
			 * Now that we have a driver managing this device
			 * that can receive otherend state change events,
			 * hook up a watch for them.
			 */
			ivars = device_get_ivars(kids[i]);
			xs_register_watch(&ivars->xd_otherend_watch);
			xs_register_watch(&ivars->xd_local_watch);
		}
		free(kids, M_TEMP);
	}

	return (0);
}

/**
 * \brief Task callback function to perform XenBus probe operations
 *        from a known safe context.
 *
 * \param arg      The NewBus device_t representing the bus instance to
 *                 on which to perform probe processing.
 * \param pending  The number of times this task was queued before it could
 *                 be run.
 */
static void
xenbusb_probe_children_cb(void *arg, int pending __unused)
{
	device_t dev = (device_t)arg;

	/*
	 * Hold Giant until the Giant free newbus changes are committed.
	 */
	mtx_lock(&Giant);
	xenbusb_probe_children(dev);
	mtx_unlock(&Giant);
}

/**
 * \brief XenStore watch callback for the root node of the XenStore
 *        subtree representing a XenBus.
 *
 * This callback performs, or delegates to the xbs_probe_children task,
 * all processing necessary to handle dynmaic device arrival and departure
 * events from a XenBus.
 *
 * \param watch  The XenStore watch object associated with this callback.
 * \param vec    The XenStore watch event data.
 * \param len	 The number of fields in the event data stream.
 */
static void
xenbusb_devices_changed(struct xs_watch *watch, const char **vec,
			unsigned int len)
{
	struct xenbusb_softc *xbs;
	device_t dev;
	char *node;
	char *type;
	char *id;
	char *p;
	u_int component;

	xbs = (struct xenbusb_softc *)watch->callback_data;
	dev = xbs->xbs_dev;

	if (len <= XS_WATCH_PATH) {
		device_printf(dev, "xenbusb_devices_changed: "
			      "Short Event Data.\n");
		return;
	}

	node = strdup(vec[XS_WATCH_PATH], M_XENBUS);
	p = strchr(node, '/');
	if (p == NULL)
		goto out;
	*p = 0;
	type = p + 1;

	p = strchr(type, '/');
	if (p == NULL)
		goto out;
	*p++ = 0;

	/*
	 * Extract the device ID.  A device ID has one or more path
	 * components separated by the '/' character.
	 *
	 * e.g. "<frontend vm id>/<frontend dev id>" for backend devices.
	 */
	id = p;
	for (component = 0; component < xbs->xbs_id_components; component++) {
		p = strchr(p, '/');
		if (p == NULL)
			break;
		p++;
	}
	if (p != NULL)
		*p = 0;

	if (*id != 0 && component >= xbs->xbs_id_components - 1) {
		xenbusb_add_device(xbs->xbs_dev, type, id);
		taskqueue_enqueue(taskqueue_thread, &xbs->xbs_probe_children);
	}
out:
	free(node, M_XENBUS);
}

/**
 * \brief Interrupt configuration hook callback associated with xbs_attch_ch.
 *
 * Since interrupts are always functional at the time of XenBus configuration,
 * there is nothing to be done when the callback occurs.  This hook is only
 * registered to hold up boot processing while XenBus devices come online.
 * 
 * \param arg  Unused configuration hook callback argument.
 */
static void
xenbusb_nop_confighook_cb(void *arg __unused)
{
}

/*--------------------------- Public Functions -------------------------------*/
/*--------- API comments for these methods can be found in xenbusb.h ---------*/
void
xenbusb_identify(driver_t *driver __unused, device_t parent)
{
	/*
	 * A single instance of each bus type for which we have a driver
	 * is always present in a system operating under Xen.
	 */
	BUS_ADD_CHILD(parent, 0, driver->name, 0);
}

int
xenbusb_add_device(device_t dev, const char *type, const char *id)
{
	struct xenbusb_softc *xbs;
	struct sbuf *devpath_sbuf;
	char *devpath;
	struct xenbus_device_ivars *ivars;
	int error;

	xbs = device_get_softc(dev);
	devpath_sbuf = sbuf_new_auto();
	sbuf_printf(devpath_sbuf, "%s/%s/%s", xbs->xbs_node, type, id);
	sbuf_finish(devpath_sbuf);
	devpath = sbuf_data(devpath_sbuf);

	ivars = malloc(sizeof(*ivars), M_XENBUS, M_ZERO|M_WAITOK);
	error = ENXIO;

	if (xs_exists(XST_NIL, devpath, "") != 0) {
		device_t child;
		enum xenbus_state state;
		char *statepath;

		child = xenbusb_device_exists(dev, devpath);
		if (child != NULL) {
			/*
			 * We are already tracking this node
			 */
			error = 0;
			goto out;
		}
			
		state = xenbus_read_driver_state(devpath);
		if (state != XenbusStateInitialising) {
			/*
			 * Device is not new, so ignore it. This can
			 * happen if a device is going away after
			 * switching to Closed.
			 */
			printf("xenbusb_add_device: Device %s ignored. "
			       "State %d\n", devpath, state);
			error = 0;
			goto out;
		}

		sx_init(&ivars->xd_lock, "xdlock");
		ivars->xd_flags = XDF_CONNECTING;
		ivars->xd_node = strdup(devpath, M_XENBUS);
		ivars->xd_node_len = strlen(devpath);
		ivars->xd_type  = strdup(type, M_XENBUS);
		ivars->xd_state = XenbusStateInitialising;

		error = XENBUSB_GET_OTHEREND_NODE(dev, ivars);
		if (error) {
			printf("xenbus_update_device: %s no otherend id\n",
			    devpath); 
			goto out;
		}

		statepath = malloc(ivars->xd_otherend_path_len
		    + strlen("/state") + 1, M_XENBUS, M_WAITOK);
		sprintf(statepath, "%s/state", ivars->xd_otherend_path);
		ivars->xd_otherend_watch.node = statepath;
		ivars->xd_otherend_watch.callback = xenbusb_otherend_watch_cb;
		ivars->xd_otherend_watch.callback_data = (uintptr_t)ivars;

		ivars->xd_local_watch.node = ivars->xd_node;
		ivars->xd_local_watch.callback = xenbusb_local_watch_cb;
		ivars->xd_local_watch.callback_data = (uintptr_t)ivars;

		mtx_lock(&xbs->xbs_lock);
		xbs->xbs_connecting_children++;
		mtx_unlock(&xbs->xbs_lock);

		child = device_add_child(dev, NULL, -1);
		ivars->xd_dev = child;
		device_set_ivars(child, ivars);
	}

out:
	sbuf_delete(devpath_sbuf);
	if (error != 0)
		xenbusb_free_child_ivars(ivars);

	return (error);
}

int
xenbusb_attach(device_t dev, char *bus_node, u_int id_components)
{
	struct xenbusb_softc *xbs;

	xbs = device_get_softc(dev);
	mtx_init(&xbs->xbs_lock, "xenbusb softc lock", NULL, MTX_DEF);
	xbs->xbs_node = bus_node;
	xbs->xbs_id_components = id_components;
	xbs->xbs_dev = dev;

	/*
	 * Since XenBus buses are attached to the XenStore, and
	 * the XenStore does not probe children until after interrupt
	 * services are available, this config hook is used solely
	 * to ensure that the remainder of the boot process (e.g.
	 * mount root) is deferred until child devices are adequately
	 * probed.  We unblock the boot process as soon as the
	 * connecting child count in our softc goes to 0.
	 */
	xbs->xbs_attach_ch.ich_func = xenbusb_nop_confighook_cb;
	xbs->xbs_attach_ch.ich_arg = dev;
	config_intrhook_establish(&xbs->xbs_attach_ch);
	xbs->xbs_flags |= XBS_ATTACH_CH_ACTIVE;
	xbs->xbs_connecting_children = 1;

	/*
	 * The subtree for this bus type may not yet exist
	 * causing initial enumeration to fail.  We still
	 * want to return success from our attach though
	 * so that we are ready to handle devices for this
	 * bus when they are dynamically attached to us
	 * by a Xen management action.
	 */
	(void)xenbusb_enumerate_bus(xbs);
	xenbusb_probe_children(dev);

	xbs->xbs_device_watch.node = bus_node;
	xbs->xbs_device_watch.callback = xenbusb_devices_changed;
	xbs->xbs_device_watch.callback_data = (uintptr_t)xbs;

	TASK_INIT(&xbs->xbs_probe_children, 0, xenbusb_probe_children_cb, dev);

	xs_register_watch(&xbs->xbs_device_watch);

	xenbusb_release_confighook(xbs);

	return (0);
}

int
xenbusb_resume(device_t dev)
{
	device_t *kids;
	struct xenbus_device_ivars *ivars;
	int i, count, error;
	char *statepath;

	/*
	 * We must re-examine each device and find the new path for
	 * its backend.
	 */
	if (device_get_children(dev, &kids, &count) == 0) {
		for (i = 0; i < count; i++) {
			if (device_get_state(kids[i]) == DS_NOTPRESENT)
				continue;

			if (xen_suspend_cancelled) {
				DEVICE_RESUME(kids[i]);
				continue;
			}

			ivars = device_get_ivars(kids[i]);

			xs_unregister_watch(&ivars->xd_otherend_watch);
			xenbus_set_state(kids[i], XenbusStateInitialising);

			/*
			 * Find the new backend details and
			 * re-register our watch.
			 */
			error = XENBUSB_GET_OTHEREND_NODE(dev, ivars);
			if (error)
				return (error);

			statepath = malloc(ivars->xd_otherend_path_len
			    + strlen("/state") + 1, M_XENBUS, M_WAITOK);
			sprintf(statepath, "%s/state", ivars->xd_otherend_path);

			free(ivars->xd_otherend_watch.node, M_XENBUS);
			ivars->xd_otherend_watch.node = statepath;

			DEVICE_RESUME(kids[i]);

			xs_register_watch(&ivars->xd_otherend_watch);
#if 0
			/*
			 * Can't do this yet since we are running in
			 * the xenwatch thread and if we sleep here,
			 * we will stop delivering watch notifications
			 * and the device will never come back online.
			 */
			sx_xlock(&ivars->xd_lock);
			while (ivars->xd_state != XenbusStateClosed
			    && ivars->xd_state != XenbusStateConnected)
				sx_sleep(&ivars->xd_state, &ivars->xd_lock,
				    0, "xdresume", 0);
			sx_xunlock(&ivars->xd_lock);
#endif
		}
		free(kids, M_TEMP);
	}

	return (0);
}

int
xenbusb_print_child(device_t dev, device_t child)
{
	struct xenbus_device_ivars *ivars = device_get_ivars(child);
	int	retval = 0;

	retval += bus_print_child_header(dev, child);
	retval += printf(" at %s", ivars->xd_node);
	retval += bus_print_child_footer(dev, child);

	return (retval);
}

int
xenbusb_read_ivar(device_t dev, device_t child, int index, uintptr_t *result)
{
	struct xenbus_device_ivars *ivars = device_get_ivars(child);

	switch (index) {
	case XENBUS_IVAR_NODE:
		*result = (uintptr_t) ivars->xd_node;
		return (0);

	case XENBUS_IVAR_TYPE:
		*result = (uintptr_t) ivars->xd_type;
		return (0);

	case XENBUS_IVAR_STATE:
		*result = (uintptr_t) ivars->xd_state;
		return (0);

	case XENBUS_IVAR_OTHEREND_ID:
		*result = (uintptr_t) ivars->xd_otherend_id;
		return (0);

	case XENBUS_IVAR_OTHEREND_PATH:
		*result = (uintptr_t) ivars->xd_otherend_path;
		return (0);
	}

	return (ENOENT);
}

int
xenbusb_write_ivar(device_t dev, device_t child, int index, uintptr_t value)
{
	struct xenbus_device_ivars *ivars = device_get_ivars(child);
	enum xenbus_state newstate;
	int currstate;

	switch (index) {
	case XENBUS_IVAR_STATE:
	{
		int error;

		newstate = (enum xenbus_state)value;
		sx_xlock(&ivars->xd_lock);
		if (ivars->xd_state == newstate) {
			error = 0;
			goto out;
		}

		error = xs_scanf(XST_NIL, ivars->xd_node, "state",
		    NULL, "%d", &currstate);
		if (error)
			goto out;

		do {
			error = xs_printf(XST_NIL, ivars->xd_node, "state",
			    "%d", newstate);
		} while (error == EAGAIN);
		if (error) {
			/*
			 * Avoid looping through xenbus_dev_fatal()
			 * which calls xenbus_write_ivar to set the
			 * state to closing.
			 */
			if (newstate != XenbusStateClosing)
				xenbus_dev_fatal(dev, error,
						 "writing new state");
			goto out;
		}
		ivars->xd_state = newstate;

		if ((ivars->xd_flags & XDF_CONNECTING) != 0
		 && (newstate == XenbusStateClosed
		  || newstate == XenbusStateConnected)) {
			struct xenbusb_softc *xbs;

			ivars->xd_flags &= ~XDF_CONNECTING;
			xbs = device_get_softc(dev);
			xenbusb_release_confighook(xbs);
		}

		wakeup(&ivars->xd_state);
	out:
		sx_xunlock(&ivars->xd_lock);
		return (error);
	}

	case XENBUS_IVAR_NODE:
	case XENBUS_IVAR_TYPE:
	case XENBUS_IVAR_OTHEREND_ID:
	case XENBUS_IVAR_OTHEREND_PATH:
		/*
		 * These variables are read-only.
		 */
		return (EINVAL);
	}

	return (ENOENT);
}

void
xenbusb_otherend_changed(device_t bus, device_t child, enum xenbus_state state)
{
	XENBUS_OTHEREND_CHANGED(child, state);
}

void
xenbusb_localend_changed(device_t bus, device_t child, const char *path)
{

	if (strcmp(path, "/state") != 0) {
		struct xenbus_device_ivars *ivars;

		ivars = device_get_ivars(child);
		sx_xlock(&ivars->xd_lock);
		ivars->xd_state = xenbus_read_driver_state(ivars->xd_node);
		sx_xunlock(&ivars->xd_lock);
	}
	XENBUS_LOCALEND_CHANGED(child, path);
}
