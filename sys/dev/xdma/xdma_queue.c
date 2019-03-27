/*-
 * Copyright (c) 2018 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-10-C-0237
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_platform.h"
#include <sys/param.h>
#include <sys/conf.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/sx.h>

#include <machine/bus.h>

#include <dev/xdma/xdma.h>

int
xdma_dequeue(xdma_channel_t *xchan, void **user,
    xdma_transfer_status_t *status)
{
	struct xdma_request *xr_tmp;
	struct xdma_request *xr;

	QUEUE_OUT_LOCK(xchan);
	TAILQ_FOREACH_SAFE(xr, &xchan->queue_out, xr_next, xr_tmp) {
		TAILQ_REMOVE(&xchan->queue_out, xr, xr_next);
		break;
	}
	QUEUE_OUT_UNLOCK(xchan);

	if (xr == NULL)
		return (-1);

	*user = xr->user;
	status->error = xr->status.error;
	status->transferred = xr->status.transferred;

	xchan_bank_put(xchan, xr);

	return (0);
}

int
xdma_enqueue(xdma_channel_t *xchan, uintptr_t src, uintptr_t dst,
    uint8_t src_width, uint8_t dst_width, bus_size_t len,
    enum xdma_direction dir, void *user)
{
	struct xdma_request *xr;
	xdma_controller_t *xdma;

	xdma = xchan->xdma;
	KASSERT(xdma != NULL, ("xdma is NULL"));

	xr = xchan_bank_get(xchan);
	if (xr == NULL)
		return (-1); /* No space is available. */

	xr->user = user;
	xr->direction = dir;
	xr->m = NULL;
	xr->bp = NULL;
	xr->block_num = 1;
	xr->block_len = len;
	xr->req_type = XR_TYPE_VIRT;
	xr->src_addr = src;
	xr->dst_addr = dst;
	xr->src_width = src_width;
	xr->dst_width = dst_width;

	QUEUE_IN_LOCK(xchan);
	TAILQ_INSERT_TAIL(&xchan->queue_in, xr, xr_next);
	QUEUE_IN_UNLOCK(xchan);

	return (0);
}

int
xdma_queue_submit(xdma_channel_t *xchan)
{
	xdma_controller_t *xdma;
	int ret;

	xdma = xchan->xdma;
	KASSERT(xdma != NULL, ("xdma is NULL"));

	ret = 0;

	XCHAN_LOCK(xchan);

	if (xchan->flags & XCHAN_TYPE_SG)
		ret = xdma_queue_submit_sg(xchan);

	XCHAN_UNLOCK(xchan);

	return (ret);
}
