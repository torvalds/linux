/*-
 * Copyright (c) 2017-2018 Ruslan Bukin <br@bsdpad.com>
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
#include <sys/sx.h>
#include <sys/mbuf.h>

#include <machine/bus.h>

#ifdef FDT
#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#endif

#include <dev/xdma/xdma.h>

int
xdma_dequeue_mbuf(xdma_channel_t *xchan, struct mbuf **mp,
    xdma_transfer_status_t *status)
{
	struct xdma_request *xr;
	struct xdma_request *xr_tmp;

	QUEUE_OUT_LOCK(xchan);
	TAILQ_FOREACH_SAFE(xr, &xchan->queue_out, xr_next, xr_tmp) {
		TAILQ_REMOVE(&xchan->queue_out, xr, xr_next);
		break;
	}
	QUEUE_OUT_UNLOCK(xchan);

	if (xr == NULL)
		return (-1);

	*mp = xr->m;
	status->error = xr->status.error;
	status->transferred = xr->status.transferred;

	xchan_bank_put(xchan, xr);

	return (0);
}

int
xdma_enqueue_mbuf(xdma_channel_t *xchan, struct mbuf **mp,
    uintptr_t addr, uint8_t src_width, uint8_t dst_width,
    enum xdma_direction dir)
{
	struct xdma_request *xr;
	xdma_controller_t *xdma;

	xdma = xchan->xdma;

	xr = xchan_bank_get(xchan);
	if (xr == NULL)
		return (-1); /* No space is available yet. */

	xr->direction = dir;
	xr->m = *mp;
	xr->req_type = XR_TYPE_MBUF;
	if (dir == XDMA_MEM_TO_DEV) {
		xr->dst_addr = addr;
		xr->src_addr = 0;
	} else {
		xr->src_addr = addr;
		xr->dst_addr = 0;
	}
	xr->src_width = src_width;
	xr->dst_width = dst_width;

	QUEUE_IN_LOCK(xchan);
	TAILQ_INSERT_TAIL(&xchan->queue_in, xr, xr_next);
	QUEUE_IN_UNLOCK(xchan);

	return (0);
}

uint32_t
xdma_mbuf_chain_count(struct mbuf *m0)
{
	struct mbuf *m;
	uint32_t c;

	c = 0;

	for (m = m0; m != NULL; m = m->m_next)
		c++;

	return (c);
}

uint32_t
xdma_mbuf_defrag(xdma_channel_t *xchan, struct xdma_request *xr)
{
	xdma_controller_t *xdma;
	struct mbuf *m;
	uint32_t c;

	xdma = xchan->xdma;

	c = xdma_mbuf_chain_count(xr->m);
	if (c == 1)
		return (c); /* Nothing to do. */

	if (xchan->caps & XCHAN_CAP_BUSDMA) {
		if ((xchan->caps & XCHAN_CAP_BUSDMA_NOSEG) || \
		    (c > xchan->maxnsegs)) {
			if ((m = m_defrag(xr->m, M_NOWAIT)) == NULL) {
				device_printf(xdma->dma_dev,
				    "%s: Can't defrag mbuf\n",
				    __func__);
				return (c);
			}
			xr->m = m;
			c = 1;
		}
	}

	return (c);
}
