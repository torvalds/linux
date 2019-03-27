/******************************************************************************
 * Copyright (C) 2005 Rusty Russell, IBM Corporation
 * Copyright (C) 2005 XenSource Ltd.
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
 *
 * $FreeBSD$
 */

/**
 * \file xenbusvar.h
 *
 * \brief Datastructures and function declarations for usedby device
 *        drivers operating on the XenBus.
 */

#ifndef _XEN_XENBUS_XENBUSVAR_H
#define _XEN_XENBUS_XENBUSVAR_H

#include <sys/queue.h>
#include <sys/bus.h>
#include <sys/eventhandler.h>
#include <sys/malloc.h>
#include <sys/sbuf.h>

#include <machine/stdarg.h>

#include <xen/xen-os.h>
#include <xen/interface/grant_table.h>
#include <xen/interface/io/xenbus.h>
#include <xen/interface/io/xs_wire.h>

#include <xen/xenstore/xenstorevar.h>

/* XenBus allocations including XenStore data returned to clients. */
MALLOC_DECLARE(M_XENBUS);

enum {
	/**
	 * Path of this device node.
	 */
	XENBUS_IVAR_NODE,

	/**
	 * The device type (e.g. vif, vbd).
	 */
	XENBUS_IVAR_TYPE,

	/**
	 * The state of this device (not the otherend's state).
	 */
	XENBUS_IVAR_STATE,

	/**
	 * Domain ID of the other end device.
	 */
	XENBUS_IVAR_OTHEREND_ID,

	/**
	 * Path of the other end device.
	 */
	XENBUS_IVAR_OTHEREND_PATH
};

/**
 * Simplified accessors for xenbus devices:
 *
 * xenbus_get_node
 * xenbus_get_type
 * xenbus_get_state
 * xenbus_get_otherend_id
 * xenbus_get_otherend_path
 */
#define	XENBUS_ACCESSOR(var, ivar, type) \
	__BUS_ACCESSOR(xenbus, var, XENBUS, ivar, type)

XENBUS_ACCESSOR(node,		NODE,			const char *)
XENBUS_ACCESSOR(type,		TYPE,			const char *)
XENBUS_ACCESSOR(state,		STATE,			enum xenbus_state)
XENBUS_ACCESSOR(otherend_id,	OTHEREND_ID,		int)
XENBUS_ACCESSOR(otherend_path,	OTHEREND_PATH,		const char *)

/**
 * Return the state of a XenBus device.
 *
 * \param path  The root XenStore path for the device.
 *
 * \return  The current state of the device or XenbusStateClosed if no
 *	    state can be read.
 */
XenbusState xenbus_read_driver_state(const char *path);

/**
 * Return the state of the "other end" (peer) of a XenBus device.
 *
 * \param dev   The XenBus device whose peer to query.
 *
 * \return  The current state of the peer device or XenbusStateClosed if no
 *          state can be read.
 */
static inline XenbusState
xenbus_get_otherend_state(device_t dev)
{
	return (xenbus_read_driver_state(xenbus_get_otherend_path(dev)));
}

/**
 * Initialize and register a watch on the given path (client suplied storage).
 *
 * \param dev       The XenBus device requesting the watch service.
 * \param path      The XenStore path of the object to be watched.  The
 *                  storage for this string must be stable for the lifetime
 *                  of the watch.
 * \param watch     The watch object to use for this request.  This object
 *                  must be stable for the lifetime of the watch.
 * \param callback  The function to call when XenStore objects at or below
 *                  path are modified.
 * \param cb_data   Client data that can be retrieved from the watch object
 *                  during the callback.
 *
 * \return  On success, 0. Otherwise an errno value indicating the
 *          type of failure.
 *
 * \note  On error, the device 'dev' will be switched to the XenbusStateClosing
 *        state and the returned error is saved in the per-device error node
 *        for dev in the XenStore.
 */
int xenbus_watch_path(device_t dev, char *path,
		      struct xs_watch *watch, 
		      xs_watch_cb_t *callback,
		      uintptr_t cb_data);

/**
 * Initialize and register a watch at path/path2 in the XenStore.
 *
 * \param dev       The XenBus device requesting the watch service.
 * \param path      The base XenStore path of the object to be watched.
 * \param path2     The tail XenStore path of the object to be watched.
 * \param watch     The watch object to use for this request.  This object
 *                  must be stable for the lifetime of the watch.
 * \param callback  The function to call when XenStore objects at or below
 *                  path are modified.
 * \param cb_data   Client data that can be retrieved from the watch object
 *                  during the callback.
 *
 * \return  On success, 0. Otherwise an errno value indicating the
 *          type of failure.
 *
 * \note  On error, \a dev will be switched to the XenbusStateClosing
 *        state and the returned error is saved in the per-device error node
 *        for \a dev in the XenStore.
 *
 * Similar to xenbus_watch_path, however the storage for the path to the
 * watched object is allocated from the heap and filled with "path '/' path2".
 * Should a call to this function succeed, it is the callers responsibility
 * to free watch->node using the M_XENBUS malloc type.
 */
int xenbus_watch_path2(device_t dev, const char *path,
		       const char *path2, struct xs_watch *watch, 
		       xs_watch_cb_t *callback,
		       uintptr_t cb_data);

/**
 * Grant access to the given ring_mfn to the peer of the given device.
 *
 * \param dev        The device granting access to the ring page.
 * \param ring_mfn   The guest machine page number of the page to grant
 *                   peer access rights.
 * \param refp[out]  The grant reference for the page.
 *
 * \return  On success, 0. Otherwise an errno value indicating the
 *          type of failure.
 *
 * A successful call to xenbus_grant_ring should be paired with a call
 * to gnttab_end_foreign_access() when foregn access to this page is no
 * longer requried.
 * 
 * \note  On error, \a dev will be switched to the XenbusStateClosing
 *        state and the returned error is saved in the per-device error node
 *        for \a dev in the XenStore.
 */
int xenbus_grant_ring(device_t dev, unsigned long ring_mfn, grant_ref_t *refp);

/**
 * Record the given errno, along with the given, printf-style, formatted
 * message in dev's device specific error node in the XenStore.
 *
 * \param dev  The device which encountered the error.
 * \param err  The errno value corresponding to the error.
 * \param fmt  Printf format string followed by a variable number of
 *             printf arguments.
 */
void xenbus_dev_error(device_t dev, int err, const char *fmt, ...)
	__attribute__((format(printf, 3, 4)));

/**
 * va_list version of xenbus_dev_error().
 *
 * \param dev  The device which encountered the error.
 * \param err  The errno value corresponding to the error.
 * \param fmt  Printf format string.
 * \param ap   Va_list of printf arguments.
 */
void xenbus_dev_verror(device_t dev, int err, const char *fmt, va_list ap)
	__attribute__((format(printf, 3, 0)));

/**
 * Equivalent to xenbus_dev_error(), followed by
 * xenbus_set_state(dev, XenbusStateClosing).
 *
 * \param dev  The device which encountered the error.
 * \param err  The errno value corresponding to the error.
 * \param fmt  Printf format string followed by a variable number of
 *             printf arguments.
 */
void xenbus_dev_fatal(device_t dev, int err, const char *fmt, ...)
	__attribute__((format(printf, 3, 4)));

/**
 * va_list version of xenbus_dev_fatal().
 *
 * \param dev  The device which encountered the error.
 * \param err  The errno value corresponding to the error.
 * \param fmt  Printf format string.
 * \param ap   Va_list of printf arguments.
 */
void xenbus_dev_vfatal(device_t dev, int err, const char *fmt, va_list)
	__attribute__((format(printf, 3, 0)));

/**
 * Convert a member of the xenbus_state enum into an ASCII string.
 *
 * /param state  The XenBus state to lookup.
 *
 * /return  A string representing state or, for unrecognized states,
 *	    the string "Unknown".
 */
const char *xenbus_strstate(enum xenbus_state state);

/**
 * Return the value of a XenBus device's "online" node within the XenStore.
 *
 * \param dev  The XenBus device to query.
 *
 * \return  The value of the "online" node for the device.  If the node
 *          does not exist, 0 (offline) is returned.
 */
int xenbus_dev_is_online(device_t dev);

/**
 * Default callback invoked when a change to the local XenStore sub-tree
 * for a device is modified.
 * 
 * \param dev   The XenBus device whose tree was modified.
 * \param path  The tree relative sub-path to the modified node.  The empty
 *              string indicates the root of the tree was destroyed.
 */
void xenbus_localend_changed(device_t dev, const char *path);

#include "xenbus_if.h"

#endif /* _XEN_XENBUS_XENBUSVAR_H */
