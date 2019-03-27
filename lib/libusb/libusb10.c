/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Sylvestre Gallon. All rights reserved.
 * Copyright (c) 2009 Hans Petter Selasky. All rights reserved.
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

#define	LIBUSB_NUM_SW_ENDPOINTS	(16 * 4)

static pthread_mutex_t default_context_lock = PTHREAD_MUTEX_INITIALIZER;
struct libusb_context *usbi_default_context = NULL;

/* Prototypes */

static struct libusb20_transfer *libusb10_get_transfer(struct libusb20_device *, uint8_t, uint8_t);
static int libusb10_get_buffsize(struct libusb20_device *, libusb_transfer *);
static int libusb10_convert_error(uint8_t status);
static void libusb10_complete_transfer(struct libusb20_transfer *, struct libusb_super_transfer *, int);
static void libusb10_isoc_proxy(struct libusb20_transfer *);
static void libusb10_bulk_intr_proxy(struct libusb20_transfer *);
static void libusb10_ctrl_proxy(struct libusb20_transfer *);
static void libusb10_submit_transfer_sub(struct libusb20_device *, uint8_t);

/*  Library initialisation / deinitialisation */

static const struct libusb_version libusb_version = {
	.major = 1,
	.minor = 0,
	.micro = 0,
	.nano = 2016,
	.rc = "",
	.describe = "https://www.freebsd.org"
};

const struct libusb_version *
libusb_get_version(void)
{

	return (&libusb_version);
}

void
libusb_set_debug(libusb_context *ctx, int level)
{
	ctx = GET_CONTEXT(ctx);
	if (ctx)
		ctx->debug = level;
}

static void
libusb_set_nonblocking(int f)
{
	int flags;

	/*
	 * We ignore any failures in this function, hence the
	 * non-blocking flag is not critical to the operation of
	 * libUSB. We use F_GETFL and F_SETFL to be compatible with
	 * Linux.
	 */

	flags = fcntl(f, F_GETFL, NULL);
	if (flags == -1)
		return;
	flags |= O_NONBLOCK;
	fcntl(f, F_SETFL, flags);
}

static void
libusb10_wakeup_event_loop(libusb_context *ctx)
{
	uint8_t dummy = 0;
	int err;

	err = write(ctx->ctrl_pipe[1], &dummy, sizeof(dummy));
	if (err < (int)sizeof(dummy)) {
		/* ignore error, if any */
		DPRINTF(ctx, LIBUSB_DEBUG_FUNCTION, "Waking up event loop failed!");
	}
}

int
libusb_init(libusb_context **context)
{
	struct libusb_context *ctx;
	pthread_condattr_t attr;
	char *debug;
	int ret;

	ctx = malloc(sizeof(*ctx));
	if (!ctx)
		return (LIBUSB_ERROR_INVALID_PARAM);

	memset(ctx, 0, sizeof(*ctx));

	debug = getenv("LIBUSB_DEBUG");
	if (debug != NULL) {
		ctx->debug = atoi(debug);
		if (ctx->debug != 0)
			ctx->debug_fixed = 1;
	}
	TAILQ_INIT(&ctx->pollfds);
	TAILQ_INIT(&ctx->tr_done);
	TAILQ_INIT(&ctx->hotplug_cbh);
	TAILQ_INIT(&ctx->hotplug_devs);

	if (pthread_mutex_init(&ctx->ctx_lock, NULL) != 0) {
		free(ctx);
		return (LIBUSB_ERROR_NO_MEM);
	}
	if (pthread_mutex_init(&ctx->hotplug_lock, NULL) != 0) {
		pthread_mutex_destroy(&ctx->ctx_lock);
		free(ctx);
		return (LIBUSB_ERROR_NO_MEM);
	}
	if (pthread_condattr_init(&attr) != 0) {
		pthread_mutex_destroy(&ctx->ctx_lock);
		pthread_mutex_destroy(&ctx->hotplug_lock);
		free(ctx);
		return (LIBUSB_ERROR_NO_MEM);
	}
	if (pthread_condattr_setclock(&attr, CLOCK_MONOTONIC) != 0) {
		pthread_mutex_destroy(&ctx->ctx_lock);
		pthread_mutex_destroy(&ctx->hotplug_lock);
		pthread_condattr_destroy(&attr);
		free(ctx);
		return (LIBUSB_ERROR_OTHER);
	}
	if (pthread_cond_init(&ctx->ctx_cond, &attr) != 0) {
		pthread_mutex_destroy(&ctx->ctx_lock);
		pthread_mutex_destroy(&ctx->hotplug_lock);
		pthread_condattr_destroy(&attr);
		free(ctx);
		return (LIBUSB_ERROR_NO_MEM);
	}
	pthread_condattr_destroy(&attr);

	ctx->ctx_handler = NO_THREAD;
	ctx->hotplug_handler = NO_THREAD;

	ret = pipe(ctx->ctrl_pipe);
	if (ret < 0) {
		pthread_mutex_destroy(&ctx->ctx_lock);
		pthread_mutex_destroy(&ctx->hotplug_lock);
		pthread_cond_destroy(&ctx->ctx_cond);
		free(ctx);
		return (LIBUSB_ERROR_OTHER);
	}
	/* set non-blocking mode on the control pipe to avoid deadlock */
	libusb_set_nonblocking(ctx->ctrl_pipe[0]);
	libusb_set_nonblocking(ctx->ctrl_pipe[1]);

	libusb10_add_pollfd(ctx, &ctx->ctx_poll, NULL, ctx->ctrl_pipe[0], POLLIN);

	pthread_mutex_lock(&default_context_lock);
	if (usbi_default_context == NULL) {
		usbi_default_context = ctx;
	}
	pthread_mutex_unlock(&default_context_lock);

	if (context)
		*context = ctx;

	DPRINTF(ctx, LIBUSB_DEBUG_FUNCTION, "libusb_init complete");

	return (0);
}

void
libusb_exit(libusb_context *ctx)
{
	ctx = GET_CONTEXT(ctx);

	if (ctx == NULL)
		return;

	/* stop hotplug thread, if any */

	if (ctx->hotplug_handler != NO_THREAD) {
		pthread_t td;
		void *ptr;

		HOTPLUG_LOCK(ctx);
		td = ctx->hotplug_handler;
		ctx->hotplug_handler = NO_THREAD;
		HOTPLUG_UNLOCK(ctx);

		pthread_join(td, &ptr);
	}

	/* XXX cleanup devices */

	libusb10_remove_pollfd(ctx, &ctx->ctx_poll);
	close(ctx->ctrl_pipe[0]);
	close(ctx->ctrl_pipe[1]);
	pthread_mutex_destroy(&ctx->ctx_lock);
	pthread_mutex_destroy(&ctx->hotplug_lock);
	pthread_cond_destroy(&ctx->ctx_cond);

	pthread_mutex_lock(&default_context_lock);
	if (ctx == usbi_default_context) {
		usbi_default_context = NULL;
	}
	pthread_mutex_unlock(&default_context_lock);

	free(ctx);
}

/* Device handling and initialisation. */

ssize_t
libusb_get_device_list(libusb_context *ctx, libusb_device ***list)
{
	struct libusb20_backend *usb_backend;
	struct libusb20_device *pdev;
	struct libusb_device *dev;
	int i;

	ctx = GET_CONTEXT(ctx);

	if (ctx == NULL)
		return (LIBUSB_ERROR_INVALID_PARAM);

	if (list == NULL)
		return (LIBUSB_ERROR_INVALID_PARAM);

	usb_backend = libusb20_be_alloc_default();
	if (usb_backend == NULL)
		return (LIBUSB_ERROR_NO_MEM);

	/* figure out how many USB devices are present */
	pdev = NULL;
	i = 0;
	while ((pdev = libusb20_be_device_foreach(usb_backend, pdev)))
		i++;

	/* allocate device pointer list */
	*list = malloc((i + 1) * sizeof(void *));
	if (*list == NULL) {
		libusb20_be_free(usb_backend);
		return (LIBUSB_ERROR_NO_MEM);
	}
	/* create libusb v1.0 compliant devices */
	i = 0;
	while ((pdev = libusb20_be_device_foreach(usb_backend, NULL))) {

		dev = malloc(sizeof(*dev));
		if (dev == NULL) {
			while (i != 0) {
				libusb_unref_device((*list)[i - 1]);
				i--;
			}
			free(*list);
			*list = NULL;
			libusb20_be_free(usb_backend);
			return (LIBUSB_ERROR_NO_MEM);
		}
		/* get device into libUSB v1.0 list */
		libusb20_be_dequeue_device(usb_backend, pdev);

		memset(dev, 0, sizeof(*dev));

		/* init transfer queues */
		TAILQ_INIT(&dev->tr_head);

		/* set context we belong to */
		dev->ctx = ctx;

		/* link together the two structures */
		dev->os_priv = pdev;
		pdev->privLuData = dev;

		(*list)[i] = libusb_ref_device(dev);
		i++;
	}
	(*list)[i] = NULL;

	libusb20_be_free(usb_backend);
	return (i);
}

void
libusb_free_device_list(libusb_device **list, int unref_devices)
{
	int i;

	if (list == NULL)
		return;			/* be NULL safe */

	if (unref_devices) {
		for (i = 0; list[i] != NULL; i++)
			libusb_unref_device(list[i]);
	}
	free(list);
}

uint8_t
libusb_get_bus_number(libusb_device *dev)
{
	if (dev == NULL)
		return (0);		/* should not happen */
	return (libusb20_dev_get_bus_number(dev->os_priv));
}

uint8_t
libusb_get_port_number(libusb_device *dev)
{
	if (dev == NULL)
		return (0);		/* should not happen */
	return (libusb20_dev_get_parent_port(dev->os_priv));
}

int
libusb_get_port_numbers(libusb_device *dev, uint8_t *buf, uint8_t bufsize)
{
	return (libusb20_dev_get_port_path(dev->os_priv, buf, bufsize));
}

int
libusb_get_port_path(libusb_context *ctx, libusb_device *dev, uint8_t *buf,
    uint8_t bufsize)
{
	return (libusb20_dev_get_port_path(dev->os_priv, buf, bufsize));
}

uint8_t
libusb_get_device_address(libusb_device *dev)
{
	if (dev == NULL)
		return (0);		/* should not happen */
	return (libusb20_dev_get_address(dev->os_priv));
}

enum libusb_speed
libusb_get_device_speed(libusb_device *dev)
{
	if (dev == NULL)
		return (LIBUSB_SPEED_UNKNOWN);	/* should not happen */

	switch (libusb20_dev_get_speed(dev->os_priv)) {
	case LIBUSB20_SPEED_LOW:
		return (LIBUSB_SPEED_LOW);
	case LIBUSB20_SPEED_FULL:
		return (LIBUSB_SPEED_FULL);
	case LIBUSB20_SPEED_HIGH:
		return (LIBUSB_SPEED_HIGH);
	case LIBUSB20_SPEED_SUPER:
		return (LIBUSB_SPEED_SUPER);
	default:
		break;
	}
	return (LIBUSB_SPEED_UNKNOWN);
}

int
libusb_get_max_packet_size(libusb_device *dev, uint8_t endpoint)
{
	struct libusb_config_descriptor *pdconf;
	struct libusb_interface *pinf;
	struct libusb_interface_descriptor *pdinf;
	struct libusb_endpoint_descriptor *pdend;
	int i;
	int j;
	int k;
	int ret;

	if (dev == NULL)
		return (LIBUSB_ERROR_NO_DEVICE);

	ret = libusb_get_active_config_descriptor(dev, &pdconf);
	if (ret < 0)
		return (ret);

	ret = LIBUSB_ERROR_NOT_FOUND;
	for (i = 0; i < pdconf->bNumInterfaces; i++) {
		pinf = &pdconf->interface[i];
		for (j = 0; j < pinf->num_altsetting; j++) {
			pdinf = &pinf->altsetting[j];
			for (k = 0; k < pdinf->bNumEndpoints; k++) {
				pdend = &pdinf->endpoint[k];
				if (pdend->bEndpointAddress == endpoint) {
					ret = pdend->wMaxPacketSize;
					goto out;
				}
			}
		}
	}

out:
	libusb_free_config_descriptor(pdconf);
	return (ret);
}

int
libusb_get_max_iso_packet_size(libusb_device *dev, uint8_t endpoint)
{
	int multiplier;
	int ret;

	ret = libusb_get_max_packet_size(dev, endpoint);

	switch (libusb20_dev_get_speed(dev->os_priv)) {
	case LIBUSB20_SPEED_LOW:
	case LIBUSB20_SPEED_FULL:
		break;
	default:
		if (ret > -1) {
			multiplier = (1 + ((ret >> 11) & 3));
			if (multiplier > 3)
				multiplier = 3;
			ret = (ret & 0x7FF) * multiplier;
		}
		break;
	}
	return (ret);
}

libusb_device *
libusb_ref_device(libusb_device *dev)
{
	if (dev == NULL)
		return (NULL);		/* be NULL safe */

	CTX_LOCK(dev->ctx);
	dev->refcnt++;
	CTX_UNLOCK(dev->ctx);

	return (dev);
}

void
libusb_unref_device(libusb_device *dev)
{
	if (dev == NULL)
		return;			/* be NULL safe */

	CTX_LOCK(dev->ctx);
	dev->refcnt--;
	CTX_UNLOCK(dev->ctx);

	if (dev->refcnt == 0) {
		libusb20_dev_free(dev->os_priv);
		free(dev);
	}
}

int
libusb_open(libusb_device *dev, libusb_device_handle **devh)
{
	libusb_context *ctx = dev->ctx;
	struct libusb20_device *pdev = dev->os_priv;
	int err;

	if (devh == NULL)
		return (LIBUSB_ERROR_INVALID_PARAM);

	/* set default device handle value */
	*devh = NULL;

	dev = libusb_ref_device(dev);
	if (dev == NULL)
		return (LIBUSB_ERROR_INVALID_PARAM);

	err = libusb20_dev_open(pdev, LIBUSB_NUM_SW_ENDPOINTS);
	if (err) {
		libusb_unref_device(dev);
		return (LIBUSB_ERROR_NO_MEM);
	}
	libusb10_add_pollfd(ctx, &dev->dev_poll, pdev, libusb20_dev_get_fd(pdev), POLLIN |
	    POLLOUT | POLLRDNORM | POLLWRNORM);

	/* make sure our event loop detects the new device */
	libusb10_wakeup_event_loop(ctx);

	*devh = pdev;

	return (0);
}

libusb_device_handle *
libusb_open_device_with_vid_pid(libusb_context *ctx, uint16_t vendor_id,
    uint16_t product_id)
{
	struct libusb_device **devs;
	struct libusb20_device *pdev;
	struct LIBUSB20_DEVICE_DESC_DECODED *pdesc;
	int i;
	int j;

	ctx = GET_CONTEXT(ctx);
	if (ctx == NULL)
		return (NULL);		/* be NULL safe */

	DPRINTF(ctx, LIBUSB_DEBUG_FUNCTION, "libusb_open_device_with_vid_pid enter");

	if ((i = libusb_get_device_list(ctx, &devs)) < 0)
		return (NULL);

	pdev = NULL;
	for (j = 0; j < i; j++) {
		struct libusb20_device *tdev;

		tdev = devs[j]->os_priv;
		pdesc = libusb20_dev_get_device_desc(tdev);
		/*
		 * NOTE: The USB library will automatically swap the
		 * fields in the device descriptor to be of host
		 * endian type!
		 */
		if (pdesc->idVendor == vendor_id &&
		    pdesc->idProduct == product_id) {
			libusb_open(devs[j], &pdev);
			break;
		}
	}

	libusb_free_device_list(devs, 1);
	DPRINTF(ctx, LIBUSB_DEBUG_FUNCTION, "libusb_open_device_with_vid_pid leave");
	return (pdev);
}

void
libusb_close(struct libusb20_device *pdev)
{
	libusb_context *ctx;
	struct libusb_device *dev;

	if (pdev == NULL)
		return;			/* be NULL safe */

	dev = libusb_get_device(pdev);
	ctx = dev->ctx;

	libusb10_remove_pollfd(ctx, &dev->dev_poll);

	libusb20_dev_close(pdev);

	/* unref will free the "pdev" when the refcount reaches zero */
	libusb_unref_device(dev);

	/* make sure our event loop detects the closed device */
	libusb10_wakeup_event_loop(ctx);
}

libusb_device *
libusb_get_device(struct libusb20_device *pdev)
{
	if (pdev == NULL)
		return (NULL);
	return ((libusb_device *)pdev->privLuData);
}

int
libusb_get_configuration(struct libusb20_device *pdev, int *config)
{
	struct libusb20_config *pconf;

	if (pdev == NULL || config == NULL)
		return (LIBUSB_ERROR_INVALID_PARAM);

	pconf = libusb20_dev_alloc_config(pdev, libusb20_dev_get_config_index(pdev));
	if (pconf == NULL)
		return (LIBUSB_ERROR_NO_MEM);

	*config = pconf->desc.bConfigurationValue;

	free(pconf);

	return (0);
}

int
libusb_set_configuration(struct libusb20_device *pdev, int configuration)
{
	struct libusb20_config *pconf;
	struct libusb_device *dev;
	int err;
	uint8_t i;

	dev = libusb_get_device(pdev);
	if (dev == NULL)
		return (LIBUSB_ERROR_INVALID_PARAM);

	if (configuration < 1) {
		/* unconfigure */
		i = 255;
	} else {
		for (i = 0; i != 255; i++) {
			uint8_t found;

			pconf = libusb20_dev_alloc_config(pdev, i);
			if (pconf == NULL)
				return (LIBUSB_ERROR_INVALID_PARAM);
			found = (pconf->desc.bConfigurationValue
			    == configuration);
			free(pconf);

			if (found)
				goto set_config;
		}
		return (LIBUSB_ERROR_INVALID_PARAM);
	}

set_config:

	libusb10_cancel_all_transfer(dev);

	libusb10_remove_pollfd(dev->ctx, &dev->dev_poll);

	err = libusb20_dev_set_config_index(pdev, i);

	libusb10_add_pollfd(dev->ctx, &dev->dev_poll, pdev, libusb20_dev_get_fd(pdev), POLLIN |
	    POLLOUT | POLLRDNORM | POLLWRNORM);

	return (err ? LIBUSB_ERROR_INVALID_PARAM : 0);
}

int
libusb_claim_interface(struct libusb20_device *pdev, int interface_number)
{
	libusb_device *dev;
	int err = 0;

	dev = libusb_get_device(pdev);
	if (dev == NULL)
		return (LIBUSB_ERROR_INVALID_PARAM);

	if (interface_number < 0 || interface_number > 31)
		return (LIBUSB_ERROR_INVALID_PARAM);

	if (pdev->auto_detach != 0) {
		err = libusb_detach_kernel_driver(pdev, interface_number);
		if (err != 0)
			goto done;
	}

	CTX_LOCK(dev->ctx);
	dev->claimed_interfaces |= (1 << interface_number);
	CTX_UNLOCK(dev->ctx);
done:
	return (err);
}

int
libusb_release_interface(struct libusb20_device *pdev, int interface_number)
{
	libusb_device *dev;
	int err = 0;

	dev = libusb_get_device(pdev);
	if (dev == NULL)
		return (LIBUSB_ERROR_INVALID_PARAM);

	if (interface_number < 0 || interface_number > 31)
		return (LIBUSB_ERROR_INVALID_PARAM);

	if (pdev->auto_detach != 0) {
		err = libusb_attach_kernel_driver(pdev, interface_number);
		if (err != 0)
			goto done;
	}

	CTX_LOCK(dev->ctx);
	if (!(dev->claimed_interfaces & (1 << interface_number)))
		err = LIBUSB_ERROR_NOT_FOUND;
	else
		dev->claimed_interfaces &= ~(1 << interface_number);
	CTX_UNLOCK(dev->ctx);
done:
	return (err);
}

int
libusb_set_interface_alt_setting(struct libusb20_device *pdev,
    int interface_number, int alternate_setting)
{
	libusb_device *dev;
	int err = 0;

	dev = libusb_get_device(pdev);
	if (dev == NULL)
		return (LIBUSB_ERROR_INVALID_PARAM);

	if (interface_number < 0 || interface_number > 31)
		return (LIBUSB_ERROR_INVALID_PARAM);

	CTX_LOCK(dev->ctx);
	if (!(dev->claimed_interfaces & (1 << interface_number)))
		err = LIBUSB_ERROR_NOT_FOUND;
	CTX_UNLOCK(dev->ctx);

	if (err)
		return (err);

	libusb10_cancel_all_transfer(dev);

	libusb10_remove_pollfd(dev->ctx, &dev->dev_poll);

	err = libusb20_dev_set_alt_index(pdev,
	    interface_number, alternate_setting);

	libusb10_add_pollfd(dev->ctx, &dev->dev_poll,
	    pdev, libusb20_dev_get_fd(pdev),
	    POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM);

	return (err ? LIBUSB_ERROR_OTHER : 0);
}

static struct libusb20_transfer *
libusb10_get_transfer(struct libusb20_device *pdev,
    uint8_t endpoint, uint8_t xfer_index)
{
	xfer_index &= 1;	/* double buffering */

	xfer_index |= (endpoint & LIBUSB20_ENDPOINT_ADDRESS_MASK) * 4;

	if (endpoint & LIBUSB20_ENDPOINT_DIR_MASK) {
		/* this is an IN endpoint */
		xfer_index |= 2;
	}
	return (libusb20_tr_get_pointer(pdev, xfer_index));
}

int
libusb_clear_halt(struct libusb20_device *pdev, uint8_t endpoint)
{
	struct libusb20_transfer *xfer;
	struct libusb_device *dev;
	int err;

	xfer = libusb10_get_transfer(pdev, endpoint, 0);
	if (xfer == NULL)
		return (LIBUSB_ERROR_INVALID_PARAM);

	dev = libusb_get_device(pdev);
	if (dev == NULL)
		return (LIBUSB_ERROR_INVALID_PARAM);

	CTX_LOCK(dev->ctx);
	err = libusb20_tr_open(xfer, 0, 1, endpoint);
	CTX_UNLOCK(dev->ctx);

	if (err != 0 && err != LIBUSB20_ERROR_BUSY)
		return (LIBUSB_ERROR_OTHER);

	libusb20_tr_clear_stall_sync(xfer);

	/* check if we opened the transfer */
	if (err == 0) {
		CTX_LOCK(dev->ctx);
		libusb20_tr_close(xfer);
		CTX_UNLOCK(dev->ctx);
	}
	return (0);			/* success */
}

int
libusb_reset_device(struct libusb20_device *pdev)
{
	libusb_device *dev;
	int err;

	dev = libusb_get_device(pdev);
	if (dev == NULL)
		return (LIBUSB_ERROR_INVALID_PARAM);

	libusb10_cancel_all_transfer(dev);

	libusb10_remove_pollfd(dev->ctx, &dev->dev_poll);

	err = libusb20_dev_reset(pdev);

	libusb10_add_pollfd(dev->ctx, &dev->dev_poll,
	    pdev, libusb20_dev_get_fd(pdev),
	    POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM);

	return (err ? LIBUSB_ERROR_OTHER : 0);
}

int
libusb_check_connected(struct libusb20_device *pdev)
{
	libusb_device *dev;
	int err;

	dev = libusb_get_device(pdev);
	if (dev == NULL)
		return (LIBUSB_ERROR_INVALID_PARAM);

	err = libusb20_dev_check_connected(pdev);

	return (err ? LIBUSB_ERROR_NO_DEVICE : 0);
}

int
libusb_kernel_driver_active(struct libusb20_device *pdev, int interface)
{
	if (pdev == NULL)
		return (LIBUSB_ERROR_INVALID_PARAM);

	if (libusb20_dev_kernel_driver_active(pdev, interface))
		return (0);		/* no kernel driver is active */
	else
		return (1);		/* kernel driver is active */
}

int
libusb_get_driver_np(struct libusb20_device *pdev, int interface,
    char *name, int namelen)
{
	return (libusb_get_driver(pdev, interface, name, namelen));
}

int
libusb_get_driver(struct libusb20_device *pdev, int interface,
    char *name, int namelen)
{
	char *ptr;
	int err;

	if (pdev == NULL)
		return (LIBUSB_ERROR_INVALID_PARAM);
	if (namelen < 1)
		return (LIBUSB_ERROR_INVALID_PARAM);
	if (namelen > 255)
		namelen = 255;

	err = libusb20_dev_get_iface_desc(
	    pdev, interface, name, namelen);

	if (err != 0)
		return (LIBUSB_ERROR_OTHER);

	/* we only want the driver name */
	ptr = strstr(name, ":");
	if (ptr != NULL)
		*ptr = 0;

	return (0);
}

int
libusb_detach_kernel_driver_np(struct libusb20_device *pdev, int interface)
{
	return (libusb_detach_kernel_driver(pdev, interface));
}

int
libusb_detach_kernel_driver(struct libusb20_device *pdev, int interface)
{
	int err;

	if (pdev == NULL)
		return (LIBUSB_ERROR_INVALID_PARAM);

	err = libusb20_dev_detach_kernel_driver(
	    pdev, interface);

	return (err ? LIBUSB_ERROR_OTHER : 0);
}

int
libusb_attach_kernel_driver(struct libusb20_device *pdev, int interface)
{
	if (pdev == NULL)
		return (LIBUSB_ERROR_INVALID_PARAM);
	/* stub - currently not supported by libusb20 */
	return (0);
}

int
libusb_set_auto_detach_kernel_driver(libusb_device_handle *dev, int enable)
{
	dev->auto_detach = (enable ? 1 : 0);
	return (0);
}

/* Asynchronous device I/O */

struct libusb_transfer *
libusb_alloc_transfer(int iso_packets)
{
	struct libusb_transfer *uxfer;
	struct libusb_super_transfer *sxfer;
	int len;

	len = sizeof(struct libusb_transfer) +
	    sizeof(struct libusb_super_transfer) +
	    (iso_packets * sizeof(libusb_iso_packet_descriptor));

	sxfer = malloc(len);
	if (sxfer == NULL)
		return (NULL);

	memset(sxfer, 0, len);

	uxfer = (struct libusb_transfer *)(
	    ((uint8_t *)sxfer) + sizeof(*sxfer));

	/* set default value */
	uxfer->num_iso_packets = iso_packets;

	return (uxfer);
}

void
libusb_free_transfer(struct libusb_transfer *uxfer)
{
	struct libusb_super_transfer *sxfer;

	if (uxfer == NULL)
		return;			/* be NULL safe */

	/* check if we should free the transfer buffer */
	if (uxfer->flags & LIBUSB_TRANSFER_FREE_BUFFER)
		free(uxfer->buffer);

	sxfer = (struct libusb_super_transfer *)(
	    (uint8_t *)uxfer - sizeof(*sxfer));

	free(sxfer);
}

static uint32_t
libusb10_get_maxframe(struct libusb20_device *pdev, libusb_transfer *xfer)
{
	uint32_t ret;

	switch (xfer->type) {
	case LIBUSB_TRANSFER_TYPE_ISOCHRONOUS:
		ret = 60 | LIBUSB20_MAX_FRAME_PRE_SCALE;	/* 60ms */
		break;
	case LIBUSB_TRANSFER_TYPE_CONTROL:
		ret = 2;
		break;
	default:
		ret = 1;
		break;
	}
	return (ret);
}

static int
libusb10_get_buffsize(struct libusb20_device *pdev, libusb_transfer *xfer)
{
	int ret;
	int usb_speed;

	usb_speed = libusb20_dev_get_speed(pdev);

	switch (xfer->type) {
	case LIBUSB_TRANSFER_TYPE_ISOCHRONOUS:
		ret = 0;		/* kernel will auto-select */
		break;
	case LIBUSB_TRANSFER_TYPE_CONTROL:
		ret = 1024;
		break;
	default:
		switch (usb_speed) {
		case LIBUSB20_SPEED_LOW:
			ret = 256;
			break;
		case LIBUSB20_SPEED_FULL:
			ret = 4096;
			break;
		case LIBUSB20_SPEED_SUPER:
			ret = 65536;
			break;
		default:
			ret = 16384;
			break;
		}
		break;
	}
	return (ret);
}

static int
libusb10_convert_error(uint8_t status)
{
	;				/* indent fix */

	switch (status) {
	case LIBUSB20_TRANSFER_START:
	case LIBUSB20_TRANSFER_COMPLETED:
		return (LIBUSB_TRANSFER_COMPLETED);
	case LIBUSB20_TRANSFER_OVERFLOW:
		return (LIBUSB_TRANSFER_OVERFLOW);
	case LIBUSB20_TRANSFER_NO_DEVICE:
		return (LIBUSB_TRANSFER_NO_DEVICE);
	case LIBUSB20_TRANSFER_STALL:
		return (LIBUSB_TRANSFER_STALL);
	case LIBUSB20_TRANSFER_CANCELLED:
		return (LIBUSB_TRANSFER_CANCELLED);
	case LIBUSB20_TRANSFER_TIMED_OUT:
		return (LIBUSB_TRANSFER_TIMED_OUT);
	default:
		return (LIBUSB_TRANSFER_ERROR);
	}
}

/* This function must be called locked */

static void
libusb10_complete_transfer(struct libusb20_transfer *pxfer,
    struct libusb_super_transfer *sxfer, int status)
{
	struct libusb_transfer *uxfer;
	struct libusb_device *dev;

	uxfer = (struct libusb_transfer *)(
	    ((uint8_t *)sxfer) + sizeof(*sxfer));

	if (pxfer != NULL)
		libusb20_tr_set_priv_sc1(pxfer, NULL);

	/* set transfer status */
	uxfer->status = status;

	/* update super transfer state */
	sxfer->state = LIBUSB_SUPER_XFER_ST_NONE;

	dev = libusb_get_device(uxfer->dev_handle);

	TAILQ_INSERT_TAIL(&dev->ctx->tr_done, sxfer, entry);
}

/* This function must be called locked */

static void
libusb10_isoc_proxy(struct libusb20_transfer *pxfer)
{
	struct libusb_super_transfer *sxfer;
	struct libusb_transfer *uxfer;
	uint32_t actlen;
	uint16_t iso_packets;
	uint16_t i;
	uint8_t status;

	status = libusb20_tr_get_status(pxfer);
	sxfer = libusb20_tr_get_priv_sc1(pxfer);
	actlen = libusb20_tr_get_actual_length(pxfer);
	iso_packets = libusb20_tr_get_max_frames(pxfer);

	if (sxfer == NULL)
		return; /* cancelled - nothing to do */

	uxfer = (struct libusb_transfer *)(
	    ((uint8_t *)sxfer) + sizeof(*sxfer));

	if (iso_packets > uxfer->num_iso_packets)
		iso_packets = uxfer->num_iso_packets;

	if (iso_packets == 0)
		return; /* nothing to do */

	/* make sure that the number of ISOCHRONOUS packets is valid */
	uxfer->num_iso_packets = iso_packets;

	switch (status) {
	case LIBUSB20_TRANSFER_COMPLETED:
		/* update actual length */
		uxfer->actual_length = actlen;
		for (i = 0; i != iso_packets; i++) {
			uxfer->iso_packet_desc[i].actual_length =
			    libusb20_tr_get_length(pxfer, i);
		}
		libusb10_complete_transfer(pxfer, sxfer, LIBUSB_TRANSFER_COMPLETED);
		break;
	case LIBUSB20_TRANSFER_START:
		/* setup length(s) */
		actlen = 0;
		for (i = 0; i != iso_packets; i++) {
			libusb20_tr_setup_isoc(pxfer,
			    &uxfer->buffer[actlen],
			    uxfer->iso_packet_desc[i].length, i);
			actlen += uxfer->iso_packet_desc[i].length;
		}

		/* no remainder */
		sxfer->rem_len = 0;

		libusb20_tr_set_total_frames(pxfer, iso_packets);
		libusb20_tr_submit(pxfer);

		/* fork another USB transfer, if any */
		libusb10_submit_transfer_sub(libusb20_tr_get_priv_sc0(pxfer), uxfer->endpoint);
		break;
	default:
		libusb10_complete_transfer(pxfer, sxfer, libusb10_convert_error(status));
		break;
	}
}

/* This function must be called locked */

static void
libusb10_bulk_intr_proxy(struct libusb20_transfer *pxfer)
{
	struct libusb_super_transfer *sxfer;
	struct libusb_transfer *uxfer;
	uint32_t max_bulk;
	uint32_t actlen;
	uint8_t status;
	uint8_t flags;

	status = libusb20_tr_get_status(pxfer);
	sxfer = libusb20_tr_get_priv_sc1(pxfer);
	max_bulk = libusb20_tr_get_max_total_length(pxfer);
	actlen = libusb20_tr_get_actual_length(pxfer);

	if (sxfer == NULL)
		return;			/* cancelled - nothing to do */

	uxfer = (struct libusb_transfer *)(
	    ((uint8_t *)sxfer) + sizeof(*sxfer));

	flags = uxfer->flags;

	switch (status) {
	case LIBUSB20_TRANSFER_COMPLETED:

		uxfer->actual_length += actlen;

		/* check for short packet */
		if (sxfer->last_len != actlen) {
			if (flags & LIBUSB_TRANSFER_SHORT_NOT_OK) {
				libusb10_complete_transfer(pxfer, sxfer, LIBUSB_TRANSFER_ERROR);
			} else {
				libusb10_complete_transfer(pxfer, sxfer, LIBUSB_TRANSFER_COMPLETED);
			}
			break;
		}
		/* check for end of data */
		if (sxfer->rem_len == 0) {
			libusb10_complete_transfer(pxfer, sxfer, LIBUSB_TRANSFER_COMPLETED);
			break;
		}
		/* FALLTHROUGH */

	case LIBUSB20_TRANSFER_START:
		if (max_bulk > sxfer->rem_len) {
			max_bulk = sxfer->rem_len;
		}
		/* setup new BULK or INTERRUPT transaction */
		libusb20_tr_setup_bulk(pxfer,
		    sxfer->curr_data, max_bulk, uxfer->timeout);

		/* update counters */
		sxfer->last_len = max_bulk;
		sxfer->curr_data += max_bulk;
		sxfer->rem_len -= max_bulk;

		libusb20_tr_submit(pxfer);

		/* check if we can fork another USB transfer */
		if (sxfer->rem_len == 0)
			libusb10_submit_transfer_sub(libusb20_tr_get_priv_sc0(pxfer), uxfer->endpoint);
		break;

	default:
		libusb10_complete_transfer(pxfer, sxfer, libusb10_convert_error(status));
		break;
	}
}

/* This function must be called locked */

static void
libusb10_ctrl_proxy(struct libusb20_transfer *pxfer)
{
	struct libusb_super_transfer *sxfer;
	struct libusb_transfer *uxfer;
	uint32_t max_bulk;
	uint32_t actlen;
	uint8_t status;
	uint8_t flags;

	status = libusb20_tr_get_status(pxfer);
	sxfer = libusb20_tr_get_priv_sc1(pxfer);
	max_bulk = libusb20_tr_get_max_total_length(pxfer);
	actlen = libusb20_tr_get_actual_length(pxfer);

	if (sxfer == NULL)
		return;			/* cancelled - nothing to do */

	uxfer = (struct libusb_transfer *)(
	    ((uint8_t *)sxfer) + sizeof(*sxfer));

	flags = uxfer->flags;

	switch (status) {
	case LIBUSB20_TRANSFER_COMPLETED:

		uxfer->actual_length += actlen;

		/* subtract length of SETUP packet, if any */
		actlen -= libusb20_tr_get_length(pxfer, 0);

		/* check for short packet */
		if (sxfer->last_len != actlen) {
			if (flags & LIBUSB_TRANSFER_SHORT_NOT_OK) {
				libusb10_complete_transfer(pxfer, sxfer, LIBUSB_TRANSFER_ERROR);
			} else {
				libusb10_complete_transfer(pxfer, sxfer, LIBUSB_TRANSFER_COMPLETED);
			}
			break;
		}
		/* check for end of data */
		if (sxfer->rem_len == 0) {
			libusb10_complete_transfer(pxfer, sxfer, LIBUSB_TRANSFER_COMPLETED);
			break;
		}
		/* FALLTHROUGH */

	case LIBUSB20_TRANSFER_START:
		if (max_bulk > sxfer->rem_len) {
			max_bulk = sxfer->rem_len;
		}
		/* setup new CONTROL transaction */
		if (status == LIBUSB20_TRANSFER_COMPLETED) {
			/* next fragment - don't send SETUP packet */
			libusb20_tr_set_length(pxfer, 0, 0);
		} else {
			/* first fragment - send SETUP packet */
			libusb20_tr_set_length(pxfer, 8, 0);
			libusb20_tr_set_buffer(pxfer, uxfer->buffer, 0);
		}

		if (max_bulk != 0) {
			libusb20_tr_set_length(pxfer, max_bulk, 1);
			libusb20_tr_set_buffer(pxfer, sxfer->curr_data, 1);
			libusb20_tr_set_total_frames(pxfer, 2);
		} else {
			libusb20_tr_set_total_frames(pxfer, 1);
		}

		/* update counters */
		sxfer->last_len = max_bulk;
		sxfer->curr_data += max_bulk;
		sxfer->rem_len -= max_bulk;

		libusb20_tr_submit(pxfer);

		/* check if we can fork another USB transfer */
		if (sxfer->rem_len == 0)
			libusb10_submit_transfer_sub(libusb20_tr_get_priv_sc0(pxfer), uxfer->endpoint);
		break;

	default:
		libusb10_complete_transfer(pxfer, sxfer, libusb10_convert_error(status));
		break;
	}
}

/* The following function must be called locked */

static void
libusb10_submit_transfer_sub(struct libusb20_device *pdev, uint8_t endpoint)
{
	struct libusb20_transfer *pxfer0;
	struct libusb20_transfer *pxfer1;
	struct libusb_super_transfer *sxfer;
	struct libusb_transfer *uxfer;
	struct libusb_device *dev;
	int err;
	int buffsize;
	int maxframe;
	int temp;

	dev = libusb_get_device(pdev);

	pxfer0 = libusb10_get_transfer(pdev, endpoint, 0);
	pxfer1 = libusb10_get_transfer(pdev, endpoint, 1);

	if (pxfer0 == NULL || pxfer1 == NULL)
		return;			/* shouldn't happen */

	temp = 0;
	if (libusb20_tr_pending(pxfer0))
		temp |= 1;
	if (libusb20_tr_pending(pxfer1))
		temp |= 2;

	switch (temp) {
	case 3:
		/* wait till one of the transfers complete */
		return;
	case 2:
		sxfer = libusb20_tr_get_priv_sc1(pxfer1);
		if (sxfer == NULL)
			return;		/* cancelling */
		if (sxfer->rem_len)
			return;		/* cannot queue another one */
		/* swap transfers */
		pxfer1 = pxfer0;
		break;
	case 1:
		sxfer = libusb20_tr_get_priv_sc1(pxfer0);
		if (sxfer == NULL)
			return;		/* cancelling */
		if (sxfer->rem_len)
			return;		/* cannot queue another one */
		/* swap transfers */
		pxfer0 = pxfer1;
		break;
	default:
		break;
	}

	/* find next transfer on same endpoint */
	TAILQ_FOREACH(sxfer, &dev->tr_head, entry) {

		uxfer = (struct libusb_transfer *)(
		    ((uint8_t *)sxfer) + sizeof(*sxfer));

		if (uxfer->endpoint == endpoint) {
			TAILQ_REMOVE(&dev->tr_head, sxfer, entry);
			sxfer->entry.tqe_prev = NULL;
			goto found;
		}
	}
	return;				/* success */

found:

	libusb20_tr_set_priv_sc0(pxfer0, pdev);
	libusb20_tr_set_priv_sc1(pxfer0, sxfer);

	/* reset super transfer state */
	sxfer->rem_len = uxfer->length;
	sxfer->curr_data = uxfer->buffer;
	uxfer->actual_length = 0;

	switch (uxfer->type) {
	case LIBUSB_TRANSFER_TYPE_ISOCHRONOUS:
		libusb20_tr_set_callback(pxfer0, libusb10_isoc_proxy);
		break;
	case LIBUSB_TRANSFER_TYPE_BULK:
	case LIBUSB_TRANSFER_TYPE_INTERRUPT:
		libusb20_tr_set_callback(pxfer0, libusb10_bulk_intr_proxy);
		break;
	case LIBUSB_TRANSFER_TYPE_CONTROL:
		libusb20_tr_set_callback(pxfer0, libusb10_ctrl_proxy);
		if (sxfer->rem_len < 8)
			goto failure;

		/* remove SETUP packet from data */
		sxfer->rem_len -= 8;
		sxfer->curr_data += 8;
		break;
	default:
		goto failure;
	}

	buffsize = libusb10_get_buffsize(pdev, uxfer);
	maxframe = libusb10_get_maxframe(pdev, uxfer);

	/* make sure the transfer is opened */
	err = libusb20_tr_open_stream(pxfer0, buffsize, maxframe,
	    endpoint, sxfer->stream_id);
	if (err && (err != LIBUSB20_ERROR_BUSY)) {
		goto failure;
	}
	libusb20_tr_start(pxfer0);
	return;

failure:
	libusb10_complete_transfer(pxfer0, sxfer, LIBUSB_TRANSFER_ERROR);
	/* make sure our event loop spins the done handler */
	libusb10_wakeup_event_loop(dev->ctx);
}

/* The following function must be called unlocked */

int
libusb_submit_transfer(struct libusb_transfer *uxfer)
{
	struct libusb20_transfer *pxfer0;
	struct libusb20_transfer *pxfer1;
	struct libusb_super_transfer *sxfer;
	struct libusb_device *dev;
	uint8_t endpoint;
	int err;

	if (uxfer == NULL)
		return (LIBUSB_ERROR_INVALID_PARAM);

	if (uxfer->dev_handle == NULL)
		return (LIBUSB_ERROR_INVALID_PARAM);

	endpoint = uxfer->endpoint;

	dev = libusb_get_device(uxfer->dev_handle);

	DPRINTF(dev->ctx, LIBUSB_DEBUG_FUNCTION, "libusb_submit_transfer enter");

	sxfer = (struct libusb_super_transfer *)(
	    (uint8_t *)uxfer - sizeof(*sxfer));

	CTX_LOCK(dev->ctx);

	pxfer0 = libusb10_get_transfer(uxfer->dev_handle, endpoint, 0);
	pxfer1 = libusb10_get_transfer(uxfer->dev_handle, endpoint, 1);

	if (pxfer0 == NULL || pxfer1 == NULL) {
		err = LIBUSB_ERROR_OTHER;
	} else if ((sxfer->entry.tqe_prev != NULL) ||
	    (libusb20_tr_get_priv_sc1(pxfer0) == sxfer) ||
	    (libusb20_tr_get_priv_sc1(pxfer1) == sxfer)) {
		err = LIBUSB_ERROR_BUSY;
	} else if (dev->device_is_gone != 0) {
		err = LIBUSB_ERROR_NO_DEVICE;
	} else {

		/* set pending state */
		sxfer->state = LIBUSB_SUPER_XFER_ST_PEND;

		/* insert transfer into transfer head list */
		TAILQ_INSERT_TAIL(&dev->tr_head, sxfer, entry);

		/* start work transfers */
		libusb10_submit_transfer_sub(
		    uxfer->dev_handle, endpoint);

		err = 0;		/* success */
	}

	CTX_UNLOCK(dev->ctx);

	DPRINTF(dev->ctx, LIBUSB_DEBUG_FUNCTION, "libusb_submit_transfer leave %d", err);

	return (err);
}

/* Asynchronous transfer cancel */

int
libusb_cancel_transfer(struct libusb_transfer *uxfer)
{
	struct libusb20_transfer *pxfer0;
	struct libusb20_transfer *pxfer1;
	struct libusb_super_transfer *sxfer;
	struct libusb_device *dev;
	struct libusb_device_handle *devh;
	uint8_t endpoint;
	int retval;

	if (uxfer == NULL)
		return (LIBUSB_ERROR_INVALID_PARAM);

	/* check if not initialised */
	if ((devh = uxfer->dev_handle) == NULL)
		return (LIBUSB_ERROR_NOT_FOUND);

	endpoint = uxfer->endpoint;

	dev = libusb_get_device(devh);

	DPRINTF(dev->ctx, LIBUSB_DEBUG_FUNCTION, "libusb_cancel_transfer enter");

	sxfer = (struct libusb_super_transfer *)(
	    (uint8_t *)uxfer - sizeof(*sxfer));

	retval = 0;

	CTX_LOCK(dev->ctx);

	pxfer0 = libusb10_get_transfer(devh, endpoint, 0);
	pxfer1 = libusb10_get_transfer(devh, endpoint, 1);

	if (sxfer->state != LIBUSB_SUPER_XFER_ST_PEND) {
		/* only update the transfer status */
		uxfer->status = LIBUSB_TRANSFER_CANCELLED;
		retval = LIBUSB_ERROR_NOT_FOUND;
	} else if (sxfer->entry.tqe_prev != NULL) {
		/* we are lucky - transfer is on a queue */
		TAILQ_REMOVE(&dev->tr_head, sxfer, entry);
		sxfer->entry.tqe_prev = NULL;
		libusb10_complete_transfer(NULL,
		    sxfer, LIBUSB_TRANSFER_CANCELLED);
		/* make sure our event loop spins the done handler */
		libusb10_wakeup_event_loop(dev->ctx);
	} else if (pxfer0 == NULL || pxfer1 == NULL) {
		/* not started */
		retval = LIBUSB_ERROR_NOT_FOUND;
	} else if (libusb20_tr_get_priv_sc1(pxfer0) == sxfer) {
		libusb10_complete_transfer(pxfer0,
		    sxfer, LIBUSB_TRANSFER_CANCELLED);
		if (dev->device_is_gone != 0) {
			/* clear transfer pointer */
			libusb20_tr_set_priv_sc1(pxfer0, NULL);
			/* make sure our event loop spins the done handler */
			libusb10_wakeup_event_loop(dev->ctx);
		} else {
			libusb20_tr_stop(pxfer0);
			/* make sure the queue doesn't stall */
			libusb10_submit_transfer_sub(devh, endpoint);
		}
	} else if (libusb20_tr_get_priv_sc1(pxfer1) == sxfer) {
		libusb10_complete_transfer(pxfer1,
		    sxfer, LIBUSB_TRANSFER_CANCELLED);
		/* check if handle is still active */
		if (dev->device_is_gone != 0) {
			/* clear transfer pointer */
			libusb20_tr_set_priv_sc1(pxfer1, NULL);
			/* make sure our event loop spins the done handler */
			libusb10_wakeup_event_loop(dev->ctx);
		} else {
			libusb20_tr_stop(pxfer1);
			/* make sure the queue doesn't stall */
			libusb10_submit_transfer_sub(devh, endpoint);
		}
	} else {
		/* not started */
		retval = LIBUSB_ERROR_NOT_FOUND;
	}

	CTX_UNLOCK(dev->ctx);

	DPRINTF(dev->ctx, LIBUSB_DEBUG_FUNCTION, "libusb_cancel_transfer leave");

	return (retval);
}

UNEXPORTED void
libusb10_cancel_all_transfer(libusb_device *dev)
{
	struct libusb20_device *pdev = dev->os_priv;
	unsigned x;

	for (x = 0; x != LIBUSB_NUM_SW_ENDPOINTS; x++) {
		struct libusb20_transfer *xfer;

		xfer = libusb20_tr_get_pointer(pdev, x);
		if (xfer == NULL)
			continue;
		libusb20_tr_close(xfer);
	}
}

UNEXPORTED void
libusb10_cancel_all_transfer_locked(struct libusb20_device *pdev, struct libusb_device *dev)
{
	struct libusb_super_transfer *sxfer;
	unsigned x;

	for (x = 0; x != LIBUSB_NUM_SW_ENDPOINTS; x++) {
		struct libusb20_transfer *xfer;

		xfer = libusb20_tr_get_pointer(pdev, x);
		if (xfer == NULL)
			continue;
		if (libusb20_tr_pending(xfer) == 0)
			continue;
		sxfer = libusb20_tr_get_priv_sc1(xfer);
		if (sxfer == NULL)
			continue;
		/* complete pending transfer */
		libusb10_complete_transfer(xfer, sxfer, LIBUSB_TRANSFER_ERROR);
	}

	while ((sxfer = TAILQ_FIRST(&dev->tr_head))) {
		TAILQ_REMOVE(&dev->tr_head, sxfer, entry);

		/* complete pending transfer */
		libusb10_complete_transfer(NULL, sxfer, LIBUSB_TRANSFER_ERROR);
	}
}

uint16_t
libusb_cpu_to_le16(uint16_t x)
{
	return (htole16(x));
}

uint16_t
libusb_le16_to_cpu(uint16_t x)
{
	return (le16toh(x));
}

const char *
libusb_strerror(int code)
{
	switch (code) {
	case LIBUSB_SUCCESS:
		return ("Success");
	case LIBUSB_ERROR_IO:
		return ("I/O error");
	case LIBUSB_ERROR_INVALID_PARAM:
		return ("Invalid parameter");
	case LIBUSB_ERROR_ACCESS:
		return ("Permissions error");
	case LIBUSB_ERROR_NO_DEVICE:
		return ("No device");
	case LIBUSB_ERROR_NOT_FOUND:
		return ("Not found");
	case LIBUSB_ERROR_BUSY:
		return ("Device busy");
	case LIBUSB_ERROR_TIMEOUT:
		return ("Timeout");
	case LIBUSB_ERROR_OVERFLOW:
		return ("Overflow");
	case LIBUSB_ERROR_PIPE:
		return ("Pipe error");
	case LIBUSB_ERROR_INTERRUPTED:
		return ("Interrupted");
	case LIBUSB_ERROR_NO_MEM:
		return ("Out of memory");
	case LIBUSB_ERROR_NOT_SUPPORTED:
		return ("Not supported");
	case LIBUSB_ERROR_OTHER:
		return ("Other error");
	default:
		return ("Unknown error");
	}
}

const char *
libusb_error_name(int code)
{
	switch (code) {
	case LIBUSB_SUCCESS:
		return ("LIBUSB_SUCCESS");
	case LIBUSB_ERROR_IO:
		return ("LIBUSB_ERROR_IO");
	case LIBUSB_ERROR_INVALID_PARAM:
		return ("LIBUSB_ERROR_INVALID_PARAM");
	case LIBUSB_ERROR_ACCESS:
		return ("LIBUSB_ERROR_ACCESS");
	case LIBUSB_ERROR_NO_DEVICE:
		return ("LIBUSB_ERROR_NO_DEVICE");
	case LIBUSB_ERROR_NOT_FOUND:
		return ("LIBUSB_ERROR_NOT_FOUND");
	case LIBUSB_ERROR_BUSY:
		return ("LIBUSB_ERROR_BUSY");
	case LIBUSB_ERROR_TIMEOUT:
		return ("LIBUSB_ERROR_TIMEOUT");
	case LIBUSB_ERROR_OVERFLOW:
		return ("LIBUSB_ERROR_OVERFLOW");
	case LIBUSB_ERROR_PIPE:
		return ("LIBUSB_ERROR_PIPE");
	case LIBUSB_ERROR_INTERRUPTED:
		return ("LIBUSB_ERROR_INTERRUPTED");
	case LIBUSB_ERROR_NO_MEM:
		return ("LIBUSB_ERROR_NO_MEM");
	case LIBUSB_ERROR_NOT_SUPPORTED:
		return ("LIBUSB_ERROR_NOT_SUPPORTED");
	case LIBUSB_ERROR_OTHER:
		return ("LIBUSB_ERROR_OTHER");
	default:
		return ("LIBUSB_ERROR_UNKNOWN");
	}
}
