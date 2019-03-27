/******************************************************************************
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
 * \file xenbus.c
 *
 * \brief Client-facing interface for the Xenbus driver.
 *
 * In other words, the interface between the Xenbus and the device-specific
 * code, be it the frontend or the backend of that driver.
 */

#if 0
#define DPRINTK(fmt, args...) \
    printk("xenbus_client (%s:%d) " fmt ".\n", __FUNCTION__, __LINE__, ##args)
#else
#define DPRINTK(fmt, args...) ((void)0)
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/types.h>
#include <sys/malloc.h>
#include <sys/libkern.h>
#include <sys/sbuf.h>

#include <xen/xen-os.h>
#include <xen/hypervisor.h>
#include <xen/evtchn.h>
#include <xen/gnttab.h>
#include <xen/xenbus/xenbusvar.h>

#include <machine/stdarg.h>

MALLOC_DEFINE(M_XENBUS, "xenbus", "XenBus Support");

/*------------------------- Private Functions --------------------------------*/
/**
 * \brief Construct the error path corresponding to the given XenBus
 *        device.
 *
 * \param dev  The XenBus device for which we are constructing an error path.
 *
 * \return  On success, the contructed error path.  Otherwise NULL.
 *
 * It is the caller's responsibility to free any returned error path
 * node using the M_XENBUS malloc type.
 */
static char *
error_path(device_t dev)
{
	char *path_buffer = malloc(strlen("error/")
	    + strlen(xenbus_get_node(dev)) + 1,M_XENBUS, M_WAITOK);

	strcpy(path_buffer, "error/");
	strcpy(path_buffer + strlen("error/"), xenbus_get_node(dev));

	return (path_buffer);
}

/*--------------------------- Public Functions -------------------------------*/
/*-------- API comments for these methods can be found in xenbusvar.h --------*/
const char *
xenbus_strstate(XenbusState state)
{
	static const char *const name[] = {
		[ XenbusStateUnknown      ] = "Unknown",
		[ XenbusStateInitialising ] = "Initialising",
		[ XenbusStateInitWait     ] = "InitWait",
		[ XenbusStateInitialised  ] = "Initialised",
		[ XenbusStateConnected    ] = "Connected",
		[ XenbusStateClosing      ] = "Closing",
		[ XenbusStateClosed	  ] = "Closed",
	};

	return ((state < (XenbusStateClosed + 1)) ? name[state] : "INVALID");
}

int 
xenbus_watch_path(device_t dev, char *path, struct xs_watch *watch, 
    xs_watch_cb_t *callback, uintptr_t callback_data)
{
	int error;

	watch->node = path;
	watch->callback = callback;
	watch->callback_data = callback_data;

	error = xs_register_watch(watch);

	if (error) {
		watch->node = NULL;
		watch->callback = NULL;
		xenbus_dev_fatal(dev, error, "adding watch on %s", path);
	}

	return (error);
}

int
xenbus_watch_path2(device_t dev, const char *path,
    const char *path2, struct xs_watch *watch, 
    xs_watch_cb_t *callback, uintptr_t callback_data)
{
	int error;
	char *state = malloc(strlen(path) + 1 + strlen(path2) + 1,
	   M_XENBUS, M_WAITOK);

	strcpy(state, path);
	strcat(state, "/");
	strcat(state, path2);

	error = xenbus_watch_path(dev, state, watch, callback, callback_data);
	if (error) {
		free(state,M_XENBUS);
	}

	return (error);
}

void
xenbus_dev_verror(device_t dev, int err, const char *fmt, va_list ap)
{
	int ret;
	unsigned int len;
	char *printf_buffer = NULL, *path_buffer = NULL;

#define PRINTF_BUFFER_SIZE 4096
	printf_buffer = malloc(PRINTF_BUFFER_SIZE,M_XENBUS, M_WAITOK);

	len = sprintf(printf_buffer, "%i ", err);
	ret = vsnprintf(printf_buffer+len, PRINTF_BUFFER_SIZE-len, fmt, ap);

	KASSERT(len + ret <= PRINTF_BUFFER_SIZE-1, ("xenbus error message too big"));
	device_printf(dev, "Error %s\n", printf_buffer);
	path_buffer = error_path(dev);

	if (path_buffer == NULL) {
		printf("xenbus: failed to write error node for %s (%s)\n",
		       xenbus_get_node(dev), printf_buffer);
		goto fail;
	}

	if (xs_write(XST_NIL, path_buffer, "error", printf_buffer) != 0) {
		printf("xenbus: failed to write error node for %s (%s)\n",
		       xenbus_get_node(dev), printf_buffer);
		goto fail;
	}

 fail:
	if (printf_buffer)
		free(printf_buffer,M_XENBUS);
	if (path_buffer)
		free(path_buffer,M_XENBUS);
}

void
xenbus_dev_error(device_t dev, int err, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	xenbus_dev_verror(dev, err, fmt, ap);
	va_end(ap);
}

void
xenbus_dev_vfatal(device_t dev, int err, const char *fmt, va_list ap)
{
	xenbus_dev_verror(dev, err, fmt, ap);
	device_printf(dev, "Fatal error. Transitioning to Closing State\n");
	xenbus_set_state(dev, XenbusStateClosing);
}

void
xenbus_dev_fatal(device_t dev, int err, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	xenbus_dev_vfatal(dev, err, fmt, ap);
	va_end(ap);
}

int
xenbus_grant_ring(device_t dev, unsigned long ring_mfn, grant_ref_t *refp)
{
	int error;

	error = gnttab_grant_foreign_access(
		xenbus_get_otherend_id(dev), ring_mfn, 0, refp);
	if (error) {
		xenbus_dev_fatal(dev, error, "granting access to ring page");
		return (error);
	}

	return (0);
}

XenbusState
xenbus_read_driver_state(const char *path)
{
        XenbusState result;
        int error;

        error = xs_gather(XST_NIL, path, "state", "%d", &result, NULL);
        if (error)
                result = XenbusStateClosed;

        return (result);
}

int
xenbus_dev_is_online(device_t dev)
{
	const char *path;
	int error;
	int value;

	path = xenbus_get_node(dev);
	error = xs_gather(XST_NIL, path, "online", "%d", &value, NULL);
	if (error != 0) {
		/* Default to not online. */
		value = 0;
	}

	return (value);
}

void
xenbus_localend_changed(device_t dev, const char *path)
{
}
