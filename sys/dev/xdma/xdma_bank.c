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

void
xchan_bank_init(xdma_channel_t *xchan)
{
	struct xdma_request *xr;
	xdma_controller_t *xdma;
	int i;

	xdma = xchan->xdma;
	KASSERT(xdma != NULL, ("xdma is NULL"));

	xchan->xr_mem = malloc(sizeof(struct xdma_request) * xchan->xr_num,
	    M_XDMA, M_WAITOK | M_ZERO);

	for (i = 0; i < xchan->xr_num; i++) {
		xr = &xchan->xr_mem[i];
		TAILQ_INSERT_TAIL(&xchan->bank, xr, xr_next);
	}
}

int
xchan_bank_free(xdma_channel_t *xchan)
{

	free(xchan->xr_mem, M_XDMA);

	return (0);
}

struct xdma_request *
xchan_bank_get(xdma_channel_t *xchan)
{
	struct xdma_request *xr;
	struct xdma_request *xr_tmp;

	QUEUE_BANK_LOCK(xchan);
	TAILQ_FOREACH_SAFE(xr, &xchan->bank, xr_next, xr_tmp) {
		TAILQ_REMOVE(&xchan->bank, xr, xr_next);
		break;
	}
	QUEUE_BANK_UNLOCK(xchan);

	return (xr);
}

int
xchan_bank_put(xdma_channel_t *xchan, struct xdma_request *xr)
{

	QUEUE_BANK_LOCK(xchan);
	TAILQ_INSERT_TAIL(&xchan->bank, xr, xr_next);
	QUEUE_BANK_UNLOCK(xchan);

	return (0);
}
