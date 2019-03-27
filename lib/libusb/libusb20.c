/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008-2009 Hans Petter Selasky. All rights reserved.
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
#include <ctype.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/queue.h>
#endif

#include "libusb20.h"
#include "libusb20_desc.h"
#include "libusb20_int.h"

static int
dummy_int(void)
{
	return (LIBUSB20_ERROR_NOT_SUPPORTED);
}

static void
dummy_void(void)
{
	return;
}

static void
dummy_callback(struct libusb20_transfer *xfer)
{
	;				/* style fix */
	switch (libusb20_tr_get_status(xfer)) {
	case LIBUSB20_TRANSFER_START:
		libusb20_tr_submit(xfer);
		break;
	default:
		/* complete or error */
		break;
	}
	return;
}

#define	dummy_get_config_desc_full (void *)dummy_int
#define	dummy_get_config_index (void *)dummy_int
#define	dummy_set_config_index (void *)dummy_int
#define	dummy_set_alt_index (void *)dummy_int
#define	dummy_reset_device (void *)dummy_int
#define	dummy_check_connected (void *)dummy_int
#define	dummy_set_power_mode (void *)dummy_int
#define	dummy_get_power_mode (void *)dummy_int
#define	dummy_get_power_usage (void *)dummy_int
#define	dummy_kernel_driver_active (void *)dummy_int
#define	dummy_detach_kernel_driver (void *)dummy_int
#define	dummy_do_request_sync (void *)dummy_int
#define	dummy_tr_open (void *)dummy_int
#define	dummy_tr_close (void *)dummy_int
#define	dummy_tr_clear_stall_sync (void *)dummy_int
#define	dummy_process (void *)dummy_int
#define	dummy_dev_info (void *)dummy_int
#define	dummy_dev_get_iface_driver (void *)dummy_int

#define	dummy_tr_submit (void *)dummy_void
#define	dummy_tr_cancel_async (void *)dummy_void

static const struct libusb20_device_methods libusb20_dummy_methods = {
	LIBUSB20_DEVICE(LIBUSB20_DECLARE, dummy)
};

void
libusb20_tr_callback_wrapper(struct libusb20_transfer *xfer)
{
	;				/* style fix */

repeat:

	if (!xfer->is_pending) {
		xfer->status = LIBUSB20_TRANSFER_START;
	} else {
		xfer->is_pending = 0;
	}

	xfer->callback(xfer);

	if (xfer->is_restart) {
		xfer->is_restart = 0;
		goto repeat;
	}
	if (xfer->is_draining &&
	    (!xfer->is_pending)) {
		xfer->is_draining = 0;
		xfer->status = LIBUSB20_TRANSFER_DRAINED;
		xfer->callback(xfer);
	}
	return;
}

int
libusb20_tr_close(struct libusb20_transfer *xfer)
{
	int error;

	if (!xfer->is_opened) {
		return (LIBUSB20_ERROR_OTHER);
	}
	error = xfer->pdev->methods->tr_close(xfer);

	if (xfer->pLength) {
		free(xfer->pLength);
	}
	if (xfer->ppBuffer) {
		free(xfer->ppBuffer);
	}
	/* reset variable fields in case the transfer is opened again */
	xfer->priv_sc0 = NULL;
	xfer->priv_sc1 = NULL;
	xfer->is_opened = 0;
	xfer->is_pending = 0;
	xfer->is_cancel = 0;
	xfer->is_draining = 0;
	xfer->is_restart = 0;
	xfer->status = 0;
	xfer->flags = 0;
	xfer->nFrames = 0;
	xfer->aFrames = 0;
	xfer->timeout = 0;
	xfer->maxFrames = 0;
	xfer->maxTotalLength = 0;
	xfer->maxPacketLen = 0;
	return (error);
}

int
libusb20_tr_open(struct libusb20_transfer *xfer, uint32_t MaxBufSize,
    uint32_t MaxFrameCount, uint8_t ep_no)
{
	return (libusb20_tr_open_stream(xfer, MaxBufSize, MaxFrameCount, ep_no, 0));
}

int
libusb20_tr_open_stream(struct libusb20_transfer *xfer, uint32_t MaxBufSize,
    uint32_t MaxFrameCount, uint8_t ep_no, uint16_t stream_id)
{
	uint32_t size;
	uint8_t pre_scale;
	int error;

	if (xfer->is_opened)
		return (LIBUSB20_ERROR_BUSY);
	if (MaxFrameCount & LIBUSB20_MAX_FRAME_PRE_SCALE) {
		MaxFrameCount &= ~LIBUSB20_MAX_FRAME_PRE_SCALE;
		/*
		 * The kernel can setup 8 times more frames when
		 * pre-scaling ISOCHRONOUS transfers. Make sure the
		 * length and pointer buffers are big enough:
		 */
		MaxFrameCount *= 8;
		pre_scale = 1;
	} else {
		pre_scale = 0;
	}
	if (MaxFrameCount == 0)
		return (LIBUSB20_ERROR_INVALID_PARAM);

	xfer->maxFrames = MaxFrameCount;

	size = MaxFrameCount * sizeof(xfer->pLength[0]);
	xfer->pLength = malloc(size);
	if (xfer->pLength == NULL) {
		return (LIBUSB20_ERROR_NO_MEM);
	}
	memset(xfer->pLength, 0, size);

	size = MaxFrameCount * sizeof(xfer->ppBuffer[0]);
	xfer->ppBuffer = malloc(size);
	if (xfer->ppBuffer == NULL) {
		free(xfer->pLength);
		return (LIBUSB20_ERROR_NO_MEM);
	}
	memset(xfer->ppBuffer, 0, size);

	if (pre_scale) {
		error = xfer->pdev->methods->tr_open(xfer, MaxBufSize,
		    MaxFrameCount / 8, ep_no, stream_id, 1);
	} else {
		error = xfer->pdev->methods->tr_open(xfer, MaxBufSize,
		    MaxFrameCount, ep_no, stream_id, 0);
	}

	if (error) {
		free(xfer->ppBuffer);
		free(xfer->pLength);
	} else {
		xfer->is_opened = 1;
	}
	return (error);
}

struct libusb20_transfer *
libusb20_tr_get_pointer(struct libusb20_device *pdev, uint16_t trIndex)
{
	if (trIndex >= pdev->nTransfer) {
		return (NULL);
	}
	return (pdev->pTransfer + trIndex);
}

uint32_t
libusb20_tr_get_actual_frames(struct libusb20_transfer *xfer)
{
	return (xfer->aFrames);
}

uint16_t
libusb20_tr_get_time_complete(struct libusb20_transfer *xfer)
{
	return (xfer->timeComplete);
}

uint32_t
libusb20_tr_get_actual_length(struct libusb20_transfer *xfer)
{
	uint32_t x;
	uint32_t actlen = 0;

	for (x = 0; x != xfer->aFrames; x++) {
		actlen += xfer->pLength[x];
	}
	return (actlen);
}

uint32_t
libusb20_tr_get_max_frames(struct libusb20_transfer *xfer)
{
	return (xfer->maxFrames);
}

uint32_t
libusb20_tr_get_max_packet_length(struct libusb20_transfer *xfer)
{
	/*
	 * Special Case NOTE: If the packet multiplier is non-zero for
	 * High Speed USB, the value returned is equal to
	 * "wMaxPacketSize * multiplier" !
	 */
	return (xfer->maxPacketLen);
}

uint32_t
libusb20_tr_get_max_total_length(struct libusb20_transfer *xfer)
{
	return (xfer->maxTotalLength);
}

uint8_t
libusb20_tr_get_status(struct libusb20_transfer *xfer)
{
	return (xfer->status);
}

uint8_t
libusb20_tr_pending(struct libusb20_transfer *xfer)
{
	return (xfer->is_pending);
}

void   *
libusb20_tr_get_priv_sc0(struct libusb20_transfer *xfer)
{
	return (xfer->priv_sc0);
}

void   *
libusb20_tr_get_priv_sc1(struct libusb20_transfer *xfer)
{
	return (xfer->priv_sc1);
}

void
libusb20_tr_stop(struct libusb20_transfer *xfer)
{
	if (!xfer->is_opened) {
		/* transfer is not opened */
		return;
	}
	if (!xfer->is_pending) {
		/* transfer not pending */
		return;
	}
	if (xfer->is_cancel) {
		/* already cancelling */
		return;
	}
	xfer->is_cancel = 1;		/* we are cancelling */

	xfer->pdev->methods->tr_cancel_async(xfer);
	return;
}

void
libusb20_tr_drain(struct libusb20_transfer *xfer)
{
	if (!xfer->is_opened) {
		/* transfer is not opened */
		return;
	}
	/* make sure that we are cancelling */
	libusb20_tr_stop(xfer);

	if (xfer->is_pending) {
		xfer->is_draining = 1;
	}
	return;
}

void
libusb20_tr_clear_stall_sync(struct libusb20_transfer *xfer)
{
	xfer->pdev->methods->tr_clear_stall_sync(xfer);
	return;
}

void
libusb20_tr_set_buffer(struct libusb20_transfer *xfer, void *buffer, uint16_t frIndex)
{
	xfer->ppBuffer[frIndex] = libusb20_pass_ptr(buffer);
	return;
}

void
libusb20_tr_set_callback(struct libusb20_transfer *xfer, libusb20_tr_callback_t *cb)
{
	xfer->callback = cb;
	return;
}

void
libusb20_tr_set_flags(struct libusb20_transfer *xfer, uint8_t flags)
{
	xfer->flags = flags;
	return;
}

uint32_t
libusb20_tr_get_length(struct libusb20_transfer *xfer, uint16_t frIndex)
{
	return (xfer->pLength[frIndex]);
}

void
libusb20_tr_set_length(struct libusb20_transfer *xfer, uint32_t length, uint16_t frIndex)
{
	xfer->pLength[frIndex] = length;
	return;
}

void
libusb20_tr_set_priv_sc0(struct libusb20_transfer *xfer, void *sc0)
{
	xfer->priv_sc0 = sc0;
	return;
}

void
libusb20_tr_set_priv_sc1(struct libusb20_transfer *xfer, void *sc1)
{
	xfer->priv_sc1 = sc1;
	return;
}

void
libusb20_tr_set_timeout(struct libusb20_transfer *xfer, uint32_t timeout)
{
	xfer->timeout = timeout;
	return;
}

void
libusb20_tr_set_total_frames(struct libusb20_transfer *xfer, uint32_t nFrames)
{
	if (nFrames > xfer->maxFrames) {
		/* should not happen */
		nFrames = xfer->maxFrames;
	}
	xfer->nFrames = nFrames;
	return;
}

void
libusb20_tr_setup_bulk(struct libusb20_transfer *xfer, void *pBuf, uint32_t length, uint32_t timeout)
{
	xfer->ppBuffer[0] = libusb20_pass_ptr(pBuf);
	xfer->pLength[0] = length;
	xfer->timeout = timeout;
	xfer->nFrames = 1;
	return;
}

void
libusb20_tr_setup_control(struct libusb20_transfer *xfer, void *psetup, void *pBuf, uint32_t timeout)
{
	uint16_t len;

	xfer->ppBuffer[0] = libusb20_pass_ptr(psetup);
	xfer->pLength[0] = 8;		/* fixed */
	xfer->timeout = timeout;

	len = ((uint8_t *)psetup)[6] | (((uint8_t *)psetup)[7] << 8);

	if (len != 0) {
		xfer->nFrames = 2;
		xfer->ppBuffer[1] = libusb20_pass_ptr(pBuf);
		xfer->pLength[1] = len;
	} else {
		xfer->nFrames = 1;
	}
	return;
}

void
libusb20_tr_setup_intr(struct libusb20_transfer *xfer, void *pBuf, uint32_t length, uint32_t timeout)
{
	xfer->ppBuffer[0] = libusb20_pass_ptr(pBuf);
	xfer->pLength[0] = length;
	xfer->timeout = timeout;
	xfer->nFrames = 1;
	return;
}

void
libusb20_tr_setup_isoc(struct libusb20_transfer *xfer, void *pBuf, uint32_t length, uint16_t frIndex)
{
	if (frIndex >= xfer->maxFrames) {
		/* should not happen */
		return;
	}
	xfer->ppBuffer[frIndex] = libusb20_pass_ptr(pBuf);
	xfer->pLength[frIndex] = length;
	return;
}

uint8_t
libusb20_tr_bulk_intr_sync(struct libusb20_transfer *xfer,
    void *pbuf, uint32_t length, uint32_t *pactlen,
    uint32_t timeout)
{
	struct libusb20_device *pdev = xfer->pdev;
	uint32_t transfer_max;
	uint32_t transfer_act;
	uint8_t retval;

	/* set some sensible default value */
	if (pactlen != NULL)
		*pactlen = 0;

	/* check for error condition */
	if (libusb20_tr_pending(xfer))
		return (LIBUSB20_ERROR_OTHER);

	do {
		/* compute maximum transfer length */
		transfer_max = 
		    libusb20_tr_get_max_total_length(xfer);

		if (transfer_max > length)
			transfer_max = length;

		/* setup bulk or interrupt transfer */
		libusb20_tr_setup_bulk(xfer, pbuf, 
		    transfer_max, timeout);

		/* start the transfer */
		libusb20_tr_start(xfer);

		/* wait for transfer completion */
		while (libusb20_dev_process(pdev) == 0) {

			if (libusb20_tr_pending(xfer) == 0)
				break;

			libusb20_dev_wait_process(pdev, -1);
		}

		transfer_act = libusb20_tr_get_actual_length(xfer);

		/* update actual length, if any */
		if (pactlen != NULL)
			pactlen[0] += transfer_act;

		/* check transfer status */
		retval = libusb20_tr_get_status(xfer);
		if (retval)
			break;

		/* check for short transfer */
		if (transfer_act != transfer_max)
			break;

		/* update buffer pointer and length */
		pbuf = ((uint8_t *)pbuf) + transfer_max;
		length = length - transfer_max;

	} while (length != 0);

	return (retval);
}

void
libusb20_tr_submit(struct libusb20_transfer *xfer)
{
	if (!xfer->is_opened) {
		/* transfer is not opened */
		return;
	}
	if (xfer->is_pending) {
		/* should not happen */
		return;
	}
	xfer->is_pending = 1;		/* we are pending */
	xfer->is_cancel = 0;		/* not cancelling */
	xfer->is_restart = 0;		/* not restarting */

	xfer->pdev->methods->tr_submit(xfer);
	return;
}

void
libusb20_tr_start(struct libusb20_transfer *xfer)
{
	if (!xfer->is_opened) {
		/* transfer is not opened */
		return;
	}
	if (xfer->is_pending) {
		if (xfer->is_cancel) {
			/* cancelling - restart */
			xfer->is_restart = 1;
		}
		/* transfer not pending */
		return;
	}
	/* get into the callback */
	libusb20_tr_callback_wrapper(xfer);
	return;
}

/* USB device operations */

int
libusb20_dev_close(struct libusb20_device *pdev)
{
	struct libusb20_transfer *xfer;
	uint16_t x;
	int error = 0;

	if (!pdev->is_opened) {
		return (LIBUSB20_ERROR_OTHER);
	}
	for (x = 0; x != pdev->nTransfer; x++) {
		xfer = pdev->pTransfer + x;

		if (!xfer->is_opened) {
			/* transfer is not opened */
			continue;
		}

		libusb20_tr_drain(xfer);

		libusb20_tr_close(xfer);
	}

	if (pdev->pTransfer != NULL) {
		free(pdev->pTransfer);
		pdev->pTransfer = NULL;
	}
	error = pdev->beMethods->close_device(pdev);

	pdev->methods = &libusb20_dummy_methods;

	pdev->is_opened = 0;

	/* 
	 * The following variable is only used by the libusb v0.1
	 * compat layer:
	 */
	pdev->claimed_interface = 0;

	/*
	 * The following variable is only used by the libusb v1.0
	 * compat layer:
	 */
	pdev->auto_detach = 0;

	return (error);
}

int
libusb20_dev_detach_kernel_driver(struct libusb20_device *pdev, uint8_t ifaceIndex)
{
	int error;

	error = pdev->methods->detach_kernel_driver(pdev, ifaceIndex);
	return (error);
}

struct LIBUSB20_DEVICE_DESC_DECODED *
libusb20_dev_get_device_desc(struct libusb20_device *pdev)
{
	return (&(pdev->ddesc));
}

int
libusb20_dev_get_fd(struct libusb20_device *pdev)
{
	return (pdev->file);
}

int
libusb20_dev_kernel_driver_active(struct libusb20_device *pdev, uint8_t ifaceIndex)
{
	int error;

	error = pdev->methods->kernel_driver_active(pdev, ifaceIndex);
	return (error);
}

int
libusb20_dev_open(struct libusb20_device *pdev, uint16_t nTransferMax)
{
	struct libusb20_transfer *xfer;
	uint32_t size;
	uint16_t x;
	int error;

	if (pdev->is_opened) {
		return (LIBUSB20_ERROR_BUSY);
	}
	if (nTransferMax >= 256) {
		return (LIBUSB20_ERROR_INVALID_PARAM);
	} else if (nTransferMax != 0) {
		size = sizeof(pdev->pTransfer[0]) * nTransferMax;
		pdev->pTransfer = malloc(size);
		if (pdev->pTransfer == NULL) {
			return (LIBUSB20_ERROR_NO_MEM);
		}
		memset(pdev->pTransfer, 0, size);
	}
	/* initialise all transfers */
	for (x = 0; x != nTransferMax; x++) {

		xfer = pdev->pTransfer + x;

		xfer->pdev = pdev;
		xfer->trIndex = x;
		xfer->callback = &dummy_callback;
	}

	/* set "nTransfer" early */
	pdev->nTransfer = nTransferMax;

	error = pdev->beMethods->open_device(pdev, nTransferMax);

	if (error) {
		if (pdev->pTransfer != NULL) {
			free(pdev->pTransfer);
			pdev->pTransfer = NULL;
		}
		pdev->file = -1;
		pdev->file_ctrl = -1;
		pdev->nTransfer = 0;
	} else {
		pdev->is_opened = 1;
	}
	return (error);
}

int
libusb20_dev_reset(struct libusb20_device *pdev)
{
	int error;

	error = pdev->methods->reset_device(pdev);
	return (error);
}

int
libusb20_dev_check_connected(struct libusb20_device *pdev)
{
	int error;

	error = pdev->methods->check_connected(pdev);
	return (error);
}

int
libusb20_dev_set_power_mode(struct libusb20_device *pdev, uint8_t power_mode)
{
	int error;

	error = pdev->methods->set_power_mode(pdev, power_mode);
	return (error);
}

uint8_t
libusb20_dev_get_power_mode(struct libusb20_device *pdev)
{
	int error;
	uint8_t power_mode;

	error = pdev->methods->get_power_mode(pdev, &power_mode);
	if (error)
		power_mode = LIBUSB20_POWER_ON;	/* fake power mode */
	return (power_mode);
}

int
libusb20_dev_get_port_path(struct libusb20_device *pdev, uint8_t *buf, uint8_t bufsize)
{

	if (pdev->port_level == 0) {
		/*
		 * Fallback for backends without port path:
		 */
		if (bufsize < 2)
			return (LIBUSB20_ERROR_OVERFLOW);
		buf[0] = pdev->parent_address;
		buf[1] = pdev->parent_port;
		return (2);
	}

	/* check if client buffer is too small */
	if (pdev->port_level > bufsize)
		return (LIBUSB20_ERROR_OVERFLOW);

	/* copy port number information */
	memcpy(buf, pdev->port_path, pdev->port_level);

	return (pdev->port_level);	/* success */
}

uint16_t
libusb20_dev_get_power_usage(struct libusb20_device *pdev)
{
	int error;
	uint16_t power_usage;

	error = pdev->methods->get_power_usage(pdev, &power_usage);
	if (error)
		power_usage = 0;
	return (power_usage);
}

int
libusb20_dev_set_alt_index(struct libusb20_device *pdev, uint8_t ifaceIndex, uint8_t altIndex)
{
	int error;

	error = pdev->methods->set_alt_index(pdev, ifaceIndex, altIndex);
	return (error);
}

int
libusb20_dev_set_config_index(struct libusb20_device *pdev, uint8_t configIndex)
{
	int error;

	error = pdev->methods->set_config_index(pdev, configIndex);
	return (error);
}

int
libusb20_dev_request_sync(struct libusb20_device *pdev,
    struct LIBUSB20_CONTROL_SETUP_DECODED *setup, void *data,
    uint16_t *pactlen, uint32_t timeout, uint8_t flags)
{
	int error;

	error = pdev->methods->do_request_sync(pdev,
	    setup, data, pactlen, timeout, flags);
	return (error);
}

int
libusb20_dev_req_string_sync(struct libusb20_device *pdev,
    uint8_t str_index, uint16_t langid, void *ptr, uint16_t len)
{
	struct LIBUSB20_CONTROL_SETUP_DECODED req;
	int error;
	int flags;

	/* make sure memory is initialised */
	memset(ptr, 0, len);

	if (len < 4) {
		/* invalid length */
		return (LIBUSB20_ERROR_INVALID_PARAM);
	}
	LIBUSB20_INIT(LIBUSB20_CONTROL_SETUP, &req);

	/*
	 * We need to read the USB string in two steps else some USB
	 * devices will complain.
	 */
	req.bmRequestType =
	    LIBUSB20_REQUEST_TYPE_STANDARD |
	    LIBUSB20_RECIPIENT_DEVICE |
	    LIBUSB20_ENDPOINT_IN;
	req.bRequest = LIBUSB20_REQUEST_GET_DESCRIPTOR;
	req.wValue = (LIBUSB20_DT_STRING << 8) | str_index;
	req.wIndex = langid;
	req.wLength = 4;		/* bytes */

	error = libusb20_dev_request_sync(pdev, &req,
	    ptr, NULL, 1000, LIBUSB20_TRANSFER_SINGLE_SHORT_NOT_OK);
	if (error) {
		/* try to request full string */
		req.wLength = 255;
		flags = 0;
	} else {
		/* extract length and request full string */
		req.wLength = *(uint8_t *)ptr;
		flags = LIBUSB20_TRANSFER_SINGLE_SHORT_NOT_OK;
	}
	if (req.wLength > len) {
		/* partial string read */
		req.wLength = len;
	}
	error = libusb20_dev_request_sync(pdev, &req, ptr, NULL, 1000, flags);
	if (error)
		return (error);

	if (((uint8_t *)ptr)[1] != LIBUSB20_DT_STRING)
		return (LIBUSB20_ERROR_OTHER);
	return (0);			/* success */
}

int
libusb20_dev_req_string_simple_sync(struct libusb20_device *pdev,
    uint8_t str_index, void *ptr, uint16_t len)
{
	char *buf;
	int error;
	uint16_t langid;
	uint16_t n;
	uint16_t i;
	uint16_t c;
	uint8_t temp[255];
	uint8_t swap;

	/* the following code derives from the FreeBSD USB kernel */

	if ((len < 1) || (ptr == NULL)) {
		/* too short buffer */
		return (LIBUSB20_ERROR_INVALID_PARAM);
	}
	error = libusb20_dev_req_string_sync(pdev,
	    0, 0, temp, sizeof(temp));
	if (error < 0) {
		*(uint8_t *)ptr = 0;	/* zero terminate */
		return (error);
	}
	langid = temp[2] | (temp[3] << 8);

	error = libusb20_dev_req_string_sync(pdev, str_index,
	    langid, temp, sizeof(temp));
	if (error < 0) {
		*(uint8_t *)ptr = 0;	/* zero terminate */
		return (error);
	}
	if (temp[0] < 2) {
		/* string length is too short */
		*(uint8_t *)ptr = 0;	/* zero terminate */
		return (LIBUSB20_ERROR_OTHER);
	}
	/* reserve one byte for terminating zero */
	len--;

	/* find maximum length */
	n = (temp[0] / 2) - 1;
	if (n > len) {
		n = len;
	}
	/* reset swap state */
	swap = 3;

	/* setup output buffer pointer */
	buf = ptr;

	/* convert and filter */
	for (i = 0; (i != n); i++) {
		c = temp[(2 * i) + 2] | (temp[(2 * i) + 3] << 8);

		/* convert from Unicode, handle buggy strings */
		if (((c & 0xff00) == 0) && (swap & 1)) {
			/* Little Endian, default */
			*buf = c;
			swap = 1;
		} else if (((c & 0x00ff) == 0) && (swap & 2)) {
			/* Big Endian */
			*buf = c >> 8;
			swap = 2;
		} else {
			/* skip invalid character */
			continue;
		}
		/*
		 * Filter by default - we don't allow greater and less than
		 * signs because they might confuse the dmesg printouts!
		 */
		if ((*buf == '<') || (*buf == '>') || (!isprint(*buf))) {
			/* skip invalid character */
			continue;
		}
		buf++;
	}
	*buf = 0;			/* zero terminate string */

	return (0);
}

struct libusb20_config *
libusb20_dev_alloc_config(struct libusb20_device *pdev, uint8_t configIndex)
{
	struct libusb20_config *retval = NULL;
	uint8_t *ptr;
	uint16_t len;
	uint8_t do_close;
	int error;

	if (!pdev->is_opened) {
		error = libusb20_dev_open(pdev, 0);
		if (error) {
			return (NULL);
		}
		do_close = 1;
	} else {
		do_close = 0;
	}
	error = pdev->methods->get_config_desc_full(pdev,
	    &ptr, &len, configIndex);

	if (error) {
		goto done;
	}
	/* parse new config descriptor */
	retval = libusb20_parse_config_desc(ptr);

	/* free config descriptor */
	free(ptr);

done:
	if (do_close) {
		error = libusb20_dev_close(pdev);
	}
	return (retval);
}

struct libusb20_device *
libusb20_dev_alloc(void)
{
	struct libusb20_device *pdev;

	pdev = malloc(sizeof(*pdev));
	if (pdev == NULL) {
		return (NULL);
	}
	memset(pdev, 0, sizeof(*pdev));

	pdev->file = -1;
	pdev->file_ctrl = -1;
	pdev->methods = &libusb20_dummy_methods;
	return (pdev);
}

uint8_t
libusb20_dev_get_config_index(struct libusb20_device *pdev)
{
	int error;
	uint8_t cfg_index;
	uint8_t do_close;

	if (!pdev->is_opened) {
		error = libusb20_dev_open(pdev, 0);
		if (error == 0) {
			do_close = 1;
		} else {
			do_close = 0;
		}
	} else {
		do_close = 0;
	}

	error = pdev->methods->get_config_index(pdev, &cfg_index);
	if (error)
		cfg_index = 0xFF;	/* current config index */
	if (do_close) {
		if (libusb20_dev_close(pdev)) {
			/* ignore */
		}
	}
	return (cfg_index);
}

uint8_t
libusb20_dev_get_mode(struct libusb20_device *pdev)
{
	return (pdev->usb_mode);
}

uint8_t
libusb20_dev_get_speed(struct libusb20_device *pdev)
{
	return (pdev->usb_speed);
}

/* if this function returns an error, the device is gone */
int
libusb20_dev_process(struct libusb20_device *pdev)
{
	int error;

	error = pdev->methods->process(pdev);
	return (error);
}

void
libusb20_dev_wait_process(struct libusb20_device *pdev, int timeout)
{
	struct pollfd pfd[1];

	if (!pdev->is_opened) {
		return;
	}
	pfd[0].fd = pdev->file;
	pfd[0].events = (POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM);
	pfd[0].revents = 0;

	if (poll(pfd, 1, timeout)) {
		/* ignore any error */
	}
	return;
}

void
libusb20_dev_free(struct libusb20_device *pdev)
{
	if (pdev == NULL) {
		/* be NULL safe */
		return;
	}
	if (pdev->is_opened) {
		if (libusb20_dev_close(pdev)) {
			/* ignore any errors */
		}
	}
	free(pdev);
	return;
}

int
libusb20_dev_get_info(struct libusb20_device *pdev,
    struct usb_device_info *pinfo)
{
	if (pinfo == NULL)
		return (LIBUSB20_ERROR_INVALID_PARAM);

	return (pdev->beMethods->dev_get_info(pdev, pinfo));
}

const char *
libusb20_dev_get_backend_name(struct libusb20_device *pdev)
{
	return (pdev->beMethods->get_backend_name());
}

const char *
libusb20_dev_get_desc(struct libusb20_device *pdev)
{
	return (pdev->usb_desc);
}

void
libusb20_dev_set_debug(struct libusb20_device *pdev, int debug)
{
	pdev->debug = debug;
	return;
}

int
libusb20_dev_get_debug(struct libusb20_device *pdev)
{
	return (pdev->debug);
}

uint8_t
libusb20_dev_get_address(struct libusb20_device *pdev)
{
	return (pdev->device_address);
}

uint8_t
libusb20_dev_get_parent_address(struct libusb20_device *pdev)
{
	return (pdev->parent_address);
}

uint8_t
libusb20_dev_get_parent_port(struct libusb20_device *pdev)
{
	return (pdev->parent_port);
}

uint8_t
libusb20_dev_get_bus_number(struct libusb20_device *pdev)
{
	return (pdev->bus_number);
}

int
libusb20_dev_get_iface_desc(struct libusb20_device *pdev, 
    uint8_t iface_index, char *buf, uint8_t len)
{
	if ((buf == NULL) || (len == 0))
		return (LIBUSB20_ERROR_INVALID_PARAM);

	buf[0] = 0;		/* set default string value */

	return (pdev->beMethods->dev_get_iface_desc(
	    pdev, iface_index, buf, len));
}

/* USB backend operations */

int
libusb20_be_get_dev_quirk(struct libusb20_backend *pbe,
    uint16_t quirk_index, struct libusb20_quirk *pq)
{
	return (pbe->methods->root_get_dev_quirk(pbe, quirk_index, pq));
}

int
libusb20_be_get_quirk_name(struct libusb20_backend *pbe,
    uint16_t quirk_index, struct libusb20_quirk *pq)
{
	return (pbe->methods->root_get_quirk_name(pbe, quirk_index, pq));
}

int
libusb20_be_add_dev_quirk(struct libusb20_backend *pbe,
    struct libusb20_quirk *pq)
{
	return (pbe->methods->root_add_dev_quirk(pbe, pq));
}

int
libusb20_be_remove_dev_quirk(struct libusb20_backend *pbe,
    struct libusb20_quirk *pq)
{
	return (pbe->methods->root_remove_dev_quirk(pbe, pq));
}

int
libusb20_be_set_template(struct libusb20_backend *pbe, int temp)
{
	return (pbe->methods->root_set_template(pbe, temp));
}

int
libusb20_be_get_template(struct libusb20_backend *pbe, int *ptemp)
{
	int temp;

	if (ptemp == NULL)
		ptemp = &temp;

	return (pbe->methods->root_get_template(pbe, ptemp));
}

struct libusb20_device *
libusb20_be_device_foreach(struct libusb20_backend *pbe, struct libusb20_device *pdev)
{
	if (pbe == NULL) {
		pdev = NULL;
	} else if (pdev == NULL) {
		pdev = TAILQ_FIRST(&(pbe->usb_devs));
	} else {
		pdev = TAILQ_NEXT(pdev, dev_entry);
	}
	return (pdev);
}

struct libusb20_backend *
libusb20_be_alloc(const struct libusb20_backend_methods *methods)
{
	struct libusb20_backend *pbe;

	pbe = malloc(sizeof(*pbe));
	if (pbe == NULL) {
		return (NULL);
	}
	memset(pbe, 0, sizeof(*pbe));

	TAILQ_INIT(&(pbe->usb_devs));

	pbe->methods = methods;		/* set backend methods */

	/* do the initial device scan */
	if (pbe->methods->init_backend) {
		pbe->methods->init_backend(pbe);
	}
	return (pbe);
}

struct libusb20_backend *
libusb20_be_alloc_linux(void)
{
	return (NULL);
}

struct libusb20_backend *
libusb20_be_alloc_ugen20(void)
{
	return (libusb20_be_alloc(&libusb20_ugen20_backend));
}

struct libusb20_backend *
libusb20_be_alloc_default(void)
{
	struct libusb20_backend *pbe;

#ifdef __linux__
	pbe = libusb20_be_alloc_linux();
	if (pbe) {
		return (pbe);
	}
#endif
	pbe = libusb20_be_alloc_ugen20();
	if (pbe) {
		return (pbe);
	}
	return (NULL);			/* no backend found */
}

void
libusb20_be_free(struct libusb20_backend *pbe)
{
	struct libusb20_device *pdev;

	if (pbe == NULL) {
		/* be NULL safe */
		return;
	}
	while ((pdev = libusb20_be_device_foreach(pbe, NULL))) {
		libusb20_be_dequeue_device(pbe, pdev);
		libusb20_dev_free(pdev);
	}
	if (pbe->methods->exit_backend) {
		pbe->methods->exit_backend(pbe);
	}
	/* free backend */
	free(pbe);
}

void
libusb20_be_enqueue_device(struct libusb20_backend *pbe, struct libusb20_device *pdev)
{
	pdev->beMethods = pbe->methods;	/* copy backend methods */
	TAILQ_INSERT_TAIL(&(pbe->usb_devs), pdev, dev_entry);
}

void
libusb20_be_dequeue_device(struct libusb20_backend *pbe,
    struct libusb20_device *pdev)
{
	TAILQ_REMOVE(&(pbe->usb_devs), pdev, dev_entry);
}

const char *
libusb20_strerror(int code)
{
	switch (code) {
	case LIBUSB20_SUCCESS:
		return ("Success");
	case LIBUSB20_ERROR_IO:
		return ("I/O error");
	case LIBUSB20_ERROR_INVALID_PARAM:
		return ("Invalid parameter");
	case LIBUSB20_ERROR_ACCESS:
		return ("Permissions error");
	case LIBUSB20_ERROR_NO_DEVICE:
		return ("No device");
	case LIBUSB20_ERROR_NOT_FOUND:
		return ("Not found");
	case LIBUSB20_ERROR_BUSY:
		return ("Device busy");
	case LIBUSB20_ERROR_TIMEOUT:
		return ("Timeout");
	case LIBUSB20_ERROR_OVERFLOW:
		return ("Overflow");
	case LIBUSB20_ERROR_PIPE:
		return ("Pipe error");
	case LIBUSB20_ERROR_INTERRUPTED:
		return ("Interrupted");
	case LIBUSB20_ERROR_NO_MEM:
		return ("Out of memory");
	case LIBUSB20_ERROR_NOT_SUPPORTED:
		return ("Not supported");
	case LIBUSB20_ERROR_OTHER:
		return ("Other error");
	default:
		return ("Unknown error");
	}
}

const char *
libusb20_error_name(int code)
{
	switch (code) {
	case LIBUSB20_SUCCESS:
		return ("LIBUSB20_SUCCESS");
	case LIBUSB20_ERROR_IO:
		return ("LIBUSB20_ERROR_IO");
	case LIBUSB20_ERROR_INVALID_PARAM:
		return ("LIBUSB20_ERROR_INVALID_PARAM");
	case LIBUSB20_ERROR_ACCESS:
		return ("LIBUSB20_ERROR_ACCESS");
	case LIBUSB20_ERROR_NO_DEVICE:
		return ("LIBUSB20_ERROR_NO_DEVICE");
	case LIBUSB20_ERROR_NOT_FOUND:
		return ("LIBUSB20_ERROR_NOT_FOUND");
	case LIBUSB20_ERROR_BUSY:
		return ("LIBUSB20_ERROR_BUSY");
	case LIBUSB20_ERROR_TIMEOUT:
		return ("LIBUSB20_ERROR_TIMEOUT");
	case LIBUSB20_ERROR_OVERFLOW:
		return ("LIBUSB20_ERROR_OVERFLOW");
	case LIBUSB20_ERROR_PIPE:
		return ("LIBUSB20_ERROR_PIPE");
	case LIBUSB20_ERROR_INTERRUPTED:
		return ("LIBUSB20_ERROR_INTERRUPTED");
	case LIBUSB20_ERROR_NO_MEM:
		return ("LIBUSB20_ERROR_NO_MEM");
	case LIBUSB20_ERROR_NOT_SUPPORTED:
		return ("LIBUSB20_ERROR_NOT_SUPPORTED");
	case LIBUSB20_ERROR_OTHER:
		return ("LIBUSB20_ERROR_OTHER");
	default:
		return ("LIBUSB20_ERROR_UNKNOWN");
	}
}
