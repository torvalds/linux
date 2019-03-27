/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014, Neel Natu (neel@freebsd.org)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <machine/vmm.h>

#include "vpmtmr.h"

/*
 * The ACPI Power Management timer is a free-running 24- or 32-bit
 * timer with a frequency of 3.579545MHz
 *
 * This implementation will be 32-bits
 */

#define PMTMR_FREQ	3579545  /* 3.579545MHz */

struct vpmtmr {
	sbintime_t	freq_sbt;
	sbintime_t	baseuptime;
	uint32_t	baseval;
};

static MALLOC_DEFINE(M_VPMTMR, "vpmtmr", "bhyve virtual acpi timer");

struct vpmtmr *
vpmtmr_init(struct vm *vm)
{
	struct vpmtmr *vpmtmr;
	struct bintime bt;

	vpmtmr = malloc(sizeof(struct vpmtmr), M_VPMTMR, M_WAITOK | M_ZERO);
	vpmtmr->baseuptime = sbinuptime();
	vpmtmr->baseval = 0;

	FREQ2BT(PMTMR_FREQ, &bt);
	vpmtmr->freq_sbt = bttosbt(bt);

	return (vpmtmr);
}

void
vpmtmr_cleanup(struct vpmtmr *vpmtmr)
{

	free(vpmtmr, M_VPMTMR);
}

int
vpmtmr_handler(struct vm *vm, int vcpuid, bool in, int port, int bytes,
    uint32_t *val)
{
	struct vpmtmr *vpmtmr;
	sbintime_t now, delta;

	if (!in || bytes != 4)
		return (-1);

	vpmtmr = vm_pmtmr(vm);

	/*
	 * No locking needed because 'baseuptime' and 'baseval' are
	 * written only during initialization.
	 */
	now = sbinuptime();
	delta = now - vpmtmr->baseuptime;
	KASSERT(delta >= 0, ("vpmtmr_handler: uptime went backwards: "
	    "%#lx to %#lx", vpmtmr->baseuptime, now));
	*val = vpmtmr->baseval + delta / vpmtmr->freq_sbt;

	return (0);
}
