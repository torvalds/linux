/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Sylvestre Gallon. All rights reserved.
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
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/queue.h>
#include <sys/endian.h>
#endif

#define	libusb_device_handle libusb20_device

#include "libusb20.h"
#include "libusb20_desc.h"
#include "libusb20_int.h"
#include "libusb.h"
#include "libusb10.h"

UNEXPORTED void
libusb10_add_pollfd(libusb_context *ctx, struct libusb_super_pollfd *pollfd,
    struct libusb20_device *pdev, int fd, short events)
{
	if (ctx == NULL)
		return;			/* invalid */

	if (pollfd->entry.tqe_prev != NULL)
		return;			/* already queued */

	if (fd < 0)
		return;			/* invalid */

	pollfd->pdev = pdev;
	pollfd->pollfd.fd = fd;
	pollfd->pollfd.events = events;

	CTX_LOCK(ctx);
	TAILQ_INSERT_TAIL(&ctx->pollfds, pollfd, entry);
	CTX_UNLOCK(ctx);

	if (ctx->fd_added_cb)
		ctx->fd_added_cb(fd, events, ctx->fd_cb_user_data);
}

UNEXPORTED void
libusb10_remove_pollfd(libusb_context *ctx, struct libusb_super_pollfd *pollfd)
{
	if (ctx == NULL)
		return;			/* invalid */

	if (pollfd->entry.tqe_prev == NULL)
		return;			/* already dequeued */

	CTX_LOCK(ctx);
	TAILQ_REMOVE(&ctx->pollfds, pollfd, entry);
	pollfd->entry.tqe_prev = NULL;
	CTX_UNLOCK(ctx);

	if (ctx->fd_removed_cb)
		ctx->fd_removed_cb(pollfd->pollfd.fd, ctx->fd_cb_user_data);
}

/* This function must be called locked */

static int
libusb10_handle_events_sub(struct libusb_context *ctx, struct timeval *tv)
{
	struct libusb_device *dev;
	struct libusb20_device **ppdev;
	struct libusb_super_pollfd *pfd;
	struct pollfd *fds;
	struct libusb_super_transfer *sxfer;
	struct libusb_transfer *uxfer;
	nfds_t nfds;
	int timeout;
	int i;
	int err;

	DPRINTF(ctx, LIBUSB_DEBUG_FUNCTION, "libusb10_handle_events_sub enter");

	nfds = 0;
	i = 0;
	TAILQ_FOREACH(pfd, &ctx->pollfds, entry)
	    nfds++;

	fds = alloca(sizeof(*fds) * nfds);
	if (fds == NULL)
		return (LIBUSB_ERROR_NO_MEM);

	ppdev = alloca(sizeof(*ppdev) * nfds);
	if (ppdev == NULL)
		return (LIBUSB_ERROR_NO_MEM);

	TAILQ_FOREACH(pfd, &ctx->pollfds, entry) {
		fds[i].fd = pfd->pollfd.fd;
		fds[i].events = pfd->pollfd.events;
		fds[i].revents = 0;
		ppdev[i] = pfd->pdev;
		if (pfd->pdev != NULL)
			libusb_get_device(pfd->pdev)->refcnt++;
		i++;
	}

	if (tv == NULL)
		timeout = -1;
	else
		timeout = (tv->tv_sec * 1000) + ((tv->tv_usec + 999) / 1000);

	CTX_UNLOCK(ctx);
	err = poll(fds, nfds, timeout);
	CTX_LOCK(ctx);

	if ((err == -1) && (errno == EINTR))
		err = LIBUSB_ERROR_INTERRUPTED;
	else if (err < 0)
		err = LIBUSB_ERROR_IO;

	if (err < 1) {
		for (i = 0; i != (int)nfds; i++) {
			if (ppdev[i] != NULL) {
				CTX_UNLOCK(ctx);
				libusb_unref_device(libusb_get_device(ppdev[i]));
				CTX_LOCK(ctx);
			}
		}
		goto do_done;
	}
	for (i = 0; i != (int)nfds; i++) {
		if (ppdev[i] != NULL) {
			dev = libusb_get_device(ppdev[i]);

			if (fds[i].revents != 0) {
				err = libusb20_dev_process(ppdev[i]);

				if (err) {
					/* set device is gone */
					dev->device_is_gone = 1;

					/* remove USB device from polling loop */
					libusb10_remove_pollfd(dev->ctx, &dev->dev_poll);

					/* cancel all pending transfers */
					libusb10_cancel_all_transfer_locked(ppdev[i], dev);
				}
			}
			CTX_UNLOCK(ctx);
			libusb_unref_device(dev);
			CTX_LOCK(ctx);

		} else {
			uint8_t dummy;

			while (read(fds[i].fd, &dummy, 1) == 1)
				;
		}
	}

	err = 0;

do_done:

	/* Do all done callbacks */

	while ((sxfer = TAILQ_FIRST(&ctx->tr_done))) {
		uint8_t flags;

		TAILQ_REMOVE(&ctx->tr_done, sxfer, entry);
		sxfer->entry.tqe_prev = NULL;

		ctx->tr_done_ref++;

		CTX_UNLOCK(ctx);

		uxfer = (struct libusb_transfer *)(
		    ((uint8_t *)sxfer) + sizeof(*sxfer));

		/* Allow the callback to free the transfer itself. */
		flags = uxfer->flags;

		if (uxfer->callback != NULL)
			(uxfer->callback) (uxfer);

		/* Check if the USB transfer should be automatically freed. */
		if (flags & LIBUSB_TRANSFER_FREE_TRANSFER)
			libusb_free_transfer(uxfer);

		CTX_LOCK(ctx);

		ctx->tr_done_ref--;
		ctx->tr_done_gen++;
	}

	/* Wakeup other waiters */
	pthread_cond_broadcast(&ctx->ctx_cond);

	return (err);
}

/* Polling and timing */

int
libusb_try_lock_events(libusb_context *ctx)
{
	int err;

	ctx = GET_CONTEXT(ctx);
	if (ctx == NULL)
		return (1);

	err = CTX_TRYLOCK(ctx);
	if (err)
		return (1);

	err = (ctx->ctx_handler != NO_THREAD);
	if (err)
		CTX_UNLOCK(ctx);
	else
		ctx->ctx_handler = pthread_self();

	return (err);
}

void
libusb_lock_events(libusb_context *ctx)
{
	ctx = GET_CONTEXT(ctx);
	CTX_LOCK(ctx);
	if (ctx->ctx_handler == NO_THREAD)
		ctx->ctx_handler = pthread_self();
}

void
libusb_unlock_events(libusb_context *ctx)
{
	ctx = GET_CONTEXT(ctx);
	if (ctx->ctx_handler == pthread_self()) {
		ctx->ctx_handler = NO_THREAD;
		pthread_cond_broadcast(&ctx->ctx_cond);
	}
	CTX_UNLOCK(ctx);
}

int
libusb_event_handling_ok(libusb_context *ctx)
{
	ctx = GET_CONTEXT(ctx);
	return (ctx->ctx_handler == pthread_self());
}

int
libusb_event_handler_active(libusb_context *ctx)
{
	ctx = GET_CONTEXT(ctx);
	return (ctx->ctx_handler != NO_THREAD);
}

void
libusb_lock_event_waiters(libusb_context *ctx)
{
	ctx = GET_CONTEXT(ctx);
	CTX_LOCK(ctx);
}

void
libusb_unlock_event_waiters(libusb_context *ctx)
{
	ctx = GET_CONTEXT(ctx);
	CTX_UNLOCK(ctx);
}

int
libusb_wait_for_event(libusb_context *ctx, struct timeval *tv)
{
	struct timespec ts;
	int err;

	ctx = GET_CONTEXT(ctx);
	DPRINTF(ctx, LIBUSB_DEBUG_FUNCTION, "libusb_wait_for_event enter");

	if (tv == NULL) {
		pthread_cond_wait(&ctx->ctx_cond,
		    &ctx->ctx_lock);
		/* try to grab polling of actual events, if any */
		if (ctx->ctx_handler == NO_THREAD)
			ctx->ctx_handler = pthread_self();
		return (0);
	}
	err = clock_gettime(CLOCK_MONOTONIC, &ts);
	if (err < 0)
		return (LIBUSB_ERROR_OTHER);

	/*
	 * The "tv" arguments points to a relative time structure and
	 * not an absolute time structure.
	 */
	ts.tv_sec += tv->tv_sec;
	ts.tv_nsec += tv->tv_usec * 1000;
	if (ts.tv_nsec >= 1000000000) {
		ts.tv_nsec -= 1000000000;
		ts.tv_sec++;
	}
	err = pthread_cond_timedwait(&ctx->ctx_cond,
	    &ctx->ctx_lock, &ts);
	/* try to grab polling of actual events, if any */
	if (ctx->ctx_handler == NO_THREAD)
		ctx->ctx_handler = pthread_self();

	if (err == ETIMEDOUT)
		return (1);

	return (0);
}

int
libusb_handle_events_timeout_completed(libusb_context *ctx,
    struct timeval *tv, int *completed)
{
	int err = 0;

	ctx = GET_CONTEXT(ctx);

	DPRINTF(ctx, LIBUSB_DEBUG_FUNCTION, "libusb_handle_events_timeout_completed enter");

	libusb_lock_events(ctx);

	while (1) {
		if (completed != NULL) {
			if (*completed != 0 || err != 0)
				break;
		}
		err = libusb_handle_events_locked(ctx, tv);
		if (completed == NULL)
			break;
	}

	libusb_unlock_events(ctx);

	DPRINTF(ctx, LIBUSB_DEBUG_FUNCTION, "libusb_handle_events_timeout_completed exit");

	return (err);
}

int
libusb_handle_events_completed(libusb_context *ctx, int *completed)
{
	return (libusb_handle_events_timeout_completed(ctx, NULL, completed));
}

int
libusb_handle_events_timeout(libusb_context *ctx, struct timeval *tv)
{
	return (libusb_handle_events_timeout_completed(ctx, tv, NULL));
}

int
libusb_handle_events(libusb_context *ctx)
{
	return (libusb_handle_events_timeout_completed(ctx, NULL, NULL));
}

int
libusb_handle_events_locked(libusb_context *ctx, struct timeval *tv)
{
	int err;

	ctx = GET_CONTEXT(ctx);

	if (libusb_event_handling_ok(ctx)) {
		err = libusb10_handle_events_sub(ctx, tv);
	} else {
		err = libusb_wait_for_event(ctx, tv);
		if (err != 0)
			err = LIBUSB_ERROR_TIMEOUT;
	}
	return (err);
}

int
libusb_get_next_timeout(libusb_context *ctx, struct timeval *tv)
{
	/* all timeouts are currently being done by the kernel */
	timerclear(tv);
	return (0);
}

void
libusb_set_pollfd_notifiers(libusb_context *ctx,
    libusb_pollfd_added_cb added_cb, libusb_pollfd_removed_cb removed_cb,
    void *user_data)
{
	ctx = GET_CONTEXT(ctx);

	ctx->fd_added_cb = added_cb;
	ctx->fd_removed_cb = removed_cb;
	ctx->fd_cb_user_data = user_data;
}

const struct libusb_pollfd **
libusb_get_pollfds(libusb_context *ctx)
{
	struct libusb_super_pollfd *pollfd;
	libusb_pollfd **ret;
	int i;

	ctx = GET_CONTEXT(ctx);

	CTX_LOCK(ctx);

	i = 0;
	TAILQ_FOREACH(pollfd, &ctx->pollfds, entry)
	    i++;

	ret = calloc(i + 1, sizeof(struct libusb_pollfd *));
	if (ret == NULL)
		goto done;

	i = 0;
	TAILQ_FOREACH(pollfd, &ctx->pollfds, entry)
	    ret[i++] = &pollfd->pollfd;
	ret[i] = NULL;

done:
	CTX_UNLOCK(ctx);
	return ((const struct libusb_pollfd **)ret);
}


/* Synchronous device I/O */

int
libusb_control_transfer(libusb_device_handle *devh,
    uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
    uint8_t *data, uint16_t wLength, unsigned int timeout)
{
	struct LIBUSB20_CONTROL_SETUP_DECODED req;
	int err;
	uint16_t actlen;

	if (devh == NULL)
		return (LIBUSB_ERROR_INVALID_PARAM);

	if ((wLength != 0) && (data == NULL))
		return (LIBUSB_ERROR_INVALID_PARAM);

	LIBUSB20_INIT(LIBUSB20_CONTROL_SETUP, &req);

	req.bmRequestType = bmRequestType;
	req.bRequest = bRequest;
	req.wValue = wValue;
	req.wIndex = wIndex;
	req.wLength = wLength;

	err = libusb20_dev_request_sync(devh, &req, data,
	    &actlen, timeout, 0);

	if (err == LIBUSB20_ERROR_PIPE)
		return (LIBUSB_ERROR_PIPE);
	else if (err == LIBUSB20_ERROR_TIMEOUT)
		return (LIBUSB_ERROR_TIMEOUT);
	else if (err)
		return (LIBUSB_ERROR_NO_DEVICE);

	return (actlen);
}

static libusb_context *
libusb10_get_context_by_device_handle(libusb_device_handle *devh)
{
	libusb_context *ctx;

	if (devh != NULL)
		ctx = libusb_get_device(devh)->ctx;
	else
		ctx = NULL;

	return (GET_CONTEXT(ctx));
}

static void
libusb10_do_transfer_cb(struct libusb_transfer *transfer)
{
	libusb_context *ctx;
	int *pdone;

	ctx = libusb10_get_context_by_device_handle(transfer->dev_handle);

	DPRINTF(ctx, LIBUSB_DEBUG_TRANSFER, "sync I/O done");

	pdone = transfer->user_data;
	*pdone = 1;
}

/*
 * TODO: Replace the following function. Allocating and freeing on a
 * per-transfer basis is slow.  --HPS
 */
static int
libusb10_do_transfer(libusb_device_handle *devh,
    uint8_t endpoint, uint8_t *data, int length,
    int *transferred, unsigned int timeout, int type)
{
	libusb_context *ctx;
	struct libusb_transfer *xfer;
	int done;
	int ret;

	if (devh == NULL)
		return (LIBUSB_ERROR_INVALID_PARAM);

	if ((length != 0) && (data == NULL))
		return (LIBUSB_ERROR_INVALID_PARAM);

	xfer = libusb_alloc_transfer(0);
	if (xfer == NULL)
		return (LIBUSB_ERROR_NO_MEM);

	ctx = libusb_get_device(devh)->ctx;

	xfer->dev_handle = devh;
	xfer->endpoint = endpoint;
	xfer->type = type;
	xfer->timeout = timeout;
	xfer->buffer = data;
	xfer->length = length;
	xfer->user_data = (void *)&done;
	xfer->callback = libusb10_do_transfer_cb;
	done = 0;

	if ((ret = libusb_submit_transfer(xfer)) < 0) {
		libusb_free_transfer(xfer);
		return (ret);
	}
	while (done == 0) {
		if ((ret = libusb_handle_events(ctx)) < 0) {
			libusb_cancel_transfer(xfer);
			usleep(1000);	/* nice it */
		}
	}

	*transferred = xfer->actual_length;

	switch (xfer->status) {
	case LIBUSB_TRANSFER_COMPLETED:
		ret = 0;
		break;
	case LIBUSB_TRANSFER_TIMED_OUT:
		ret = LIBUSB_ERROR_TIMEOUT;
		break;
	case LIBUSB_TRANSFER_OVERFLOW:
		ret = LIBUSB_ERROR_OVERFLOW;
		break;
	case LIBUSB_TRANSFER_STALL:
		ret = LIBUSB_ERROR_PIPE;
		break;
	case LIBUSB_TRANSFER_NO_DEVICE:
		ret = LIBUSB_ERROR_NO_DEVICE;
		break;
	default:
		ret = LIBUSB_ERROR_OTHER;
		break;
	}

	libusb_free_transfer(xfer);
	return (ret);
}

int
libusb_bulk_transfer(libusb_device_handle *devh,
    uint8_t endpoint, uint8_t *data, int length,
    int *transferred, unsigned int timeout)
{
	libusb_context *ctx;
	int ret;

	ctx = libusb10_get_context_by_device_handle(devh);

	DPRINTF(ctx, LIBUSB_DEBUG_FUNCTION, "libusb_bulk_transfer enter");

	ret = libusb10_do_transfer(devh, endpoint, data, length, transferred,
	    timeout, LIBUSB_TRANSFER_TYPE_BULK);

	DPRINTF(ctx, LIBUSB_DEBUG_FUNCTION, "libusb_bulk_transfer leave");
	return (ret);
}

int
libusb_interrupt_transfer(libusb_device_handle *devh,
    uint8_t endpoint, uint8_t *data, int length,
    int *transferred, unsigned int timeout)
{
	libusb_context *ctx;
	int ret;

	ctx = libusb10_get_context_by_device_handle(devh);

	DPRINTF(ctx, LIBUSB_DEBUG_FUNCTION, "libusb_interrupt_transfer enter");

	ret = libusb10_do_transfer(devh, endpoint, data, length, transferred,
	    timeout, LIBUSB_TRANSFER_TYPE_INTERRUPT);

	DPRINTF(ctx, LIBUSB_DEBUG_FUNCTION, "libusb_interrupt_transfer leave");
	return (ret);
}

uint8_t *
libusb_get_iso_packet_buffer(struct libusb_transfer *transfer, uint32_t off)
{
	uint8_t *ptr;
	uint32_t n;

	if (transfer->num_iso_packets < 0)
		return (NULL);

	if (off >= (uint32_t)transfer->num_iso_packets)
		return (NULL);

	ptr = transfer->buffer;
	if (ptr == NULL)
		return (NULL);

	for (n = 0; n != off; n++) {
		ptr += transfer->iso_packet_desc[n].length;
	}
	return (ptr);
}

uint8_t *
libusb_get_iso_packet_buffer_simple(struct libusb_transfer *transfer, uint32_t off)
{
	uint8_t *ptr;

	if (transfer->num_iso_packets < 0)
		return (NULL);

	if (off >= (uint32_t)transfer->num_iso_packets)
		return (NULL);

	ptr = transfer->buffer;
	if (ptr == NULL)
		return (NULL);

	ptr += transfer->iso_packet_desc[0].length * off;

	return (ptr);
}

void
libusb_set_iso_packet_lengths(struct libusb_transfer *transfer, uint32_t length)
{
	int n;

	if (transfer->num_iso_packets < 0)
		return;

	for (n = 0; n != transfer->num_iso_packets; n++)
		transfer->iso_packet_desc[n].length = length;
}

uint8_t *
libusb_control_transfer_get_data(struct libusb_transfer *transfer)
{
	if (transfer->buffer == NULL)
		return (NULL);

	return (transfer->buffer + LIBUSB_CONTROL_SETUP_SIZE);
}

struct libusb_control_setup *
libusb_control_transfer_get_setup(struct libusb_transfer *transfer)
{
	return ((struct libusb_control_setup *)transfer->buffer);
}

void
libusb_fill_control_setup(uint8_t *buf, uint8_t bmRequestType,
    uint8_t bRequest, uint16_t wValue,
    uint16_t wIndex, uint16_t wLength)
{
	struct libusb_control_setup *req = (struct libusb_control_setup *)buf;

	/* The alignment is OK for all fields below. */
	req->bmRequestType = bmRequestType;
	req->bRequest = bRequest;
	req->wValue = htole16(wValue);
	req->wIndex = htole16(wIndex);
	req->wLength = htole16(wLength);
}

void
libusb_fill_control_transfer(struct libusb_transfer *transfer, 
    libusb_device_handle *devh, uint8_t *buf,
    libusb_transfer_cb_fn callback, void *user_data,
    uint32_t timeout)
{
	struct libusb_control_setup *setup = (struct libusb_control_setup *)buf;

	transfer->dev_handle = devh;
	transfer->endpoint = 0;
	transfer->type = LIBUSB_TRANSFER_TYPE_CONTROL;
	transfer->timeout = timeout;
	transfer->buffer = buf;
	if (setup != NULL)
		transfer->length = LIBUSB_CONTROL_SETUP_SIZE
			+ le16toh(setup->wLength);
	else
		transfer->length = 0;
	transfer->user_data = user_data;
	transfer->callback = callback;

}

void
libusb_fill_bulk_transfer(struct libusb_transfer *transfer, 
    libusb_device_handle *devh, uint8_t endpoint, uint8_t *buf, 
    int length, libusb_transfer_cb_fn callback, void *user_data,
    uint32_t timeout)
{
	transfer->dev_handle = devh;
	transfer->endpoint = endpoint;
	transfer->type = LIBUSB_TRANSFER_TYPE_BULK;
	transfer->timeout = timeout;
	transfer->buffer = buf;
	transfer->length = length;
	transfer->user_data = user_data;
	transfer->callback = callback;
}

void
libusb_fill_interrupt_transfer(struct libusb_transfer *transfer,
    libusb_device_handle *devh, uint8_t endpoint, uint8_t *buf,
    int length, libusb_transfer_cb_fn callback, void *user_data,
    uint32_t timeout)
{
	transfer->dev_handle = devh;
	transfer->endpoint = endpoint;
	transfer->type = LIBUSB_TRANSFER_TYPE_INTERRUPT;
	transfer->timeout = timeout;
	transfer->buffer = buf;
	transfer->length = length;
	transfer->user_data = user_data;
	transfer->callback = callback;
}

void
libusb_fill_iso_transfer(struct libusb_transfer *transfer, 
    libusb_device_handle *devh, uint8_t endpoint, uint8_t *buf,
    int length, int npacket, libusb_transfer_cb_fn callback,
    void *user_data, uint32_t timeout)
{
	transfer->dev_handle = devh;
	transfer->endpoint = endpoint;
	transfer->type = LIBUSB_TRANSFER_TYPE_ISOCHRONOUS;
	transfer->timeout = timeout;
	transfer->buffer = buf;
	transfer->length = length;
	transfer->num_iso_packets = npacket;
	transfer->user_data = user_data;
	transfer->callback = callback;
}

int
libusb_alloc_streams(libusb_device_handle *dev, uint32_t num_streams,
    unsigned char *endpoints, int num_endpoints)
{
	if (num_streams > 1)
		return (LIBUSB_ERROR_INVALID_PARAM);
	return (0);
}

int
libusb_free_streams(libusb_device_handle *dev, unsigned char *endpoints, int num_endpoints)
{

	return (0);
}

void
libusb_transfer_set_stream_id(struct libusb_transfer *transfer, uint32_t stream_id)
{
	struct libusb_super_transfer *sxfer;

	if (transfer == NULL)
		return;

	sxfer = (struct libusb_super_transfer *)(
	    ((uint8_t *)transfer) - sizeof(*sxfer));

	/* set stream ID */
	sxfer->stream_id = stream_id;
}

uint32_t
libusb_transfer_get_stream_id(struct libusb_transfer *transfer)
{
	struct libusb_super_transfer *sxfer;

	if (transfer == NULL)
		return (0);

	sxfer = (struct libusb_super_transfer *)(
	    ((uint8_t *)transfer) - sizeof(*sxfer));

	/* get stream ID */
	return (sxfer->stream_id);
}
