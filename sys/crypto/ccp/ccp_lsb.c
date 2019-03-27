/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017 Conrad Meyer <cem@FreeBSD.org>
 * All rights reserved.
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

#include <sys/types.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>

#include <opencrypto/xform.h>

#include "ccp.h"
#include "ccp_lsb.h"

void
ccp_queue_decode_lsb_regions(struct ccp_softc *sc, uint64_t lsbmask,
    unsigned queue)
{
	struct ccp_queue *qp;
	unsigned i;

	qp = &sc->queues[queue];

	qp->lsb_mask = 0;

	for (i = 0; i < MAX_LSB_REGIONS; i++) {
		if (((1 << queue) & lsbmask) != 0)
			qp->lsb_mask |= (1 << i);
		lsbmask >>= MAX_HW_QUEUES;
	}

	/*
	 * Ignore region 0, which has special entries that cannot be used
	 * generally.
	 */
	qp->lsb_mask &= ~(1 << 0);
}

/*
 * Look for a private LSB for each queue.  There are 7 general purpose LSBs
 * total and 5 queues.  PSP will reserve some of both.  Firmware limits some
 * queues' access to some LSBs; we hope it is fairly sane and just use a dumb
 * greedy algorithm to assign LSBs to queues.
 */
void
ccp_assign_lsb_regions(struct ccp_softc *sc, uint64_t lsbmask)
{
	unsigned q, i;

	for (q = 0; q < nitems(sc->queues); q++) {
		if (((1 << q) & sc->valid_queues) == 0)
			continue;

		sc->queues[q].private_lsb = -1;

		/* Intentionally skip specialized 0th LSB */
		for (i = 1; i < MAX_LSB_REGIONS; i++) {
			if ((lsbmask &
			    (1ull << (q + (MAX_HW_QUEUES * i)))) != 0) {
				sc->queues[q].private_lsb = i;
				lsbmask &= ~(0x1Full << (MAX_HW_QUEUES * i));
				break;
			}
		}

		if (i == MAX_LSB_REGIONS) {
			device_printf(sc->dev,
			    "Ignoring queue %u with no private LSB\n", q);
			sc->valid_queues &= ~(1 << q);
		}
	}
}
