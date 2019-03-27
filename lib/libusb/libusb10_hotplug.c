/* $FreeBSD$ */
/*-
 * Copyright (c) 2016 Hans Petter Selasky. All rights reserved.
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

#ifdef LIBUSB_GLOBAL_INCLUDE_FILE
#include LIBUSB_GLOBAL_INCLUDE_FILE
#else
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/queue.h>
#include <sys/endian.h>
#endif

#define	libusb_device_handle libusb20_device

#include "libusb20.h"
#include "libusb20_desc.h"
#include "libusb20_int.h"
#include "libusb.h"
#include "libusb10.h"

static int
libusb_hotplug_equal(libusb_device *_adev, libusb_device *_bdev)
{
	struct libusb20_device *adev = _adev->os_priv;
	struct libusb20_device *bdev = _bdev->os_priv;

	if (adev->bus_number != bdev->bus_number)
		return (0);
	if (adev->device_address != bdev->device_address)
		return (0);
	if (memcmp(&adev->ddesc, &bdev->ddesc, sizeof(adev->ddesc)))
		return (0);
	if (memcmp(&adev->session_data, &bdev->session_data, sizeof(adev->session_data)))
		return (0);
	return (1);
}

static int
libusb_hotplug_filter(libusb_context *ctx, libusb_hotplug_callback_handle pcbh,
    libusb_device *dev, libusb_hotplug_event event)
{
	if (!(pcbh->events & event))
		return (0);
	if (pcbh->vendor != LIBUSB_HOTPLUG_MATCH_ANY &&
	    pcbh->vendor != libusb20_dev_get_device_desc(dev->os_priv)->idVendor)
		return (0);
	if (pcbh->product != LIBUSB_HOTPLUG_MATCH_ANY &&
	    pcbh->product != libusb20_dev_get_device_desc(dev->os_priv)->idProduct)
		return (0);
	if (pcbh->devclass != LIBUSB_HOTPLUG_MATCH_ANY &&
	    pcbh->devclass != libusb20_dev_get_device_desc(dev->os_priv)->bDeviceClass)
		return (0);
	return (pcbh->fn(ctx, dev, event, pcbh->user_data));
}

static void *
libusb_hotplug_scan(void *arg)
{
	TAILQ_HEAD(, libusb_device) hotplug_devs;
	libusb_hotplug_callback_handle acbh;
	libusb_hotplug_callback_handle bcbh;
	libusb_context *ctx = arg;
	libusb_device **ppdev;
	libusb_device *temp;
	libusb_device *adev;
	libusb_device *bdev;
	unsigned do_loop = 1;
	ssize_t count;
	ssize_t x;

	while (do_loop) {
		usleep(4000000);

		HOTPLUG_LOCK(ctx);

		TAILQ_INIT(&hotplug_devs);

		if (ctx->hotplug_handler != NO_THREAD) {
			count = libusb_get_device_list(ctx, &ppdev);
			if (count < 0)
				continue;
			for (x = 0; x != count; x++) {
				TAILQ_INSERT_TAIL(&hotplug_devs, ppdev[x],
				    hotplug_entry);
			}
			libusb_free_device_list(ppdev, 0);
		} else {
			do_loop = 0;
		}

		/* figure out which devices are gone */
		TAILQ_FOREACH_SAFE(adev, &ctx->hotplug_devs, hotplug_entry, temp) {
			TAILQ_FOREACH(bdev, &hotplug_devs, hotplug_entry) {
				if (libusb_hotplug_equal(adev, bdev))
					break;
			}
			if (bdev == NULL) {
				TAILQ_REMOVE(&ctx->hotplug_devs, adev, hotplug_entry);
				TAILQ_FOREACH_SAFE(acbh, &ctx->hotplug_cbh, entry, bcbh) {
					if (libusb_hotplug_filter(ctx, acbh, adev,
					    LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT) == 0)
						continue;
					TAILQ_REMOVE(&ctx->hotplug_cbh, acbh, entry);
					free(acbh);
				}
				libusb_unref_device(adev);
			}
		}

		/* figure out which devices are new */
		TAILQ_FOREACH_SAFE(adev, &hotplug_devs, hotplug_entry, temp) {
			TAILQ_FOREACH(bdev, &ctx->hotplug_devs, hotplug_entry) {
				if (libusb_hotplug_equal(adev, bdev))
					break;
			}
			if (bdev == NULL) {
				TAILQ_REMOVE(&hotplug_devs, adev, hotplug_entry);
				TAILQ_INSERT_TAIL(&ctx->hotplug_devs, adev, hotplug_entry);
				TAILQ_FOREACH_SAFE(acbh, &ctx->hotplug_cbh, entry, bcbh) {
					if (libusb_hotplug_filter(ctx, acbh, adev,
					    LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED) == 0)
						continue;
					TAILQ_REMOVE(&ctx->hotplug_cbh, acbh, entry);
					free(acbh);
				}
			}
		}
		HOTPLUG_UNLOCK(ctx);

		/* unref remaining devices */
		while ((adev = TAILQ_FIRST(&hotplug_devs)) != NULL) {
			TAILQ_REMOVE(&hotplug_devs, adev, hotplug_entry);
			libusb_unref_device(adev);
		}
	}
	return (NULL);
}

int libusb_hotplug_register_callback(libusb_context *ctx,
    libusb_hotplug_event events, libusb_hotplug_flag flags,
    int vendor_id, int product_id, int dev_class,
    libusb_hotplug_callback_fn cb_fn, void *user_data,
    libusb_hotplug_callback_handle *phandle)
{
	libusb_hotplug_callback_handle handle;
	struct libusb_device *adev;

	ctx = GET_CONTEXT(ctx);

	if (ctx == NULL || cb_fn == NULL || events == 0 ||
	    vendor_id < -1 || vendor_id > 0xffff ||
	    product_id < -1 || product_id > 0xffff ||
	    dev_class < -1 || dev_class > 0xff)
		return (LIBUSB_ERROR_INVALID_PARAM);

	handle = malloc(sizeof(*handle));
	if (handle == NULL)
		return (LIBUSB_ERROR_NO_MEM);

	HOTPLUG_LOCK(ctx);
	if (ctx->hotplug_handler == NO_THREAD) {
		if (pthread_create(&ctx->hotplug_handler, NULL,
		    &libusb_hotplug_scan, ctx) != 0)
			ctx->hotplug_handler = NO_THREAD;
	}
	handle->events = events;
	handle->vendor = vendor_id;
	handle->product = product_id;
	handle->devclass = dev_class;
	handle->fn = cb_fn;
	handle->user_data = user_data;

	if (flags & LIBUSB_HOTPLUG_ENUMERATE) {
		TAILQ_FOREACH(adev, &ctx->hotplug_devs, hotplug_entry) {
			if (libusb_hotplug_filter(ctx, handle, adev,
			    LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED) == 0)
				continue;
			free(handle);
			handle = NULL;
			break;
		}
	}
	if (handle != NULL)
		TAILQ_INSERT_TAIL(&ctx->hotplug_cbh, handle, entry);
	HOTPLUG_UNLOCK(ctx);

	if (phandle != NULL)
		*phandle = handle;
	return (LIBUSB_SUCCESS);
}

void libusb_hotplug_deregister_callback(libusb_context *ctx,
    libusb_hotplug_callback_handle handle)
{
  	ctx = GET_CONTEXT(ctx);

	if (ctx == NULL || handle == NULL)
		return;

	HOTPLUG_LOCK(ctx);
	TAILQ_REMOVE(&ctx->hotplug_cbh, handle, entry);
	HOTPLUG_UNLOCK(ctx);

	free(handle);
}
