/*	$OpenBSD: dt_prov_profile.c,v 1.10 2025/01/23 11:17:32 mpi Exp $ */

/*
 * Copyright (c) 2019 Martin Pieuchot <mpi@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/clockintr.h>

#include <dev/dt/dtvar.h>

struct dt_probe	*dtpp_profile;		/* per-CPU profile probe */
struct dt_probe	*dtpp_interval;		/* global periodic probe */

/* Flags that make sense for this provider */
#define DTEVT_PROV_PROFILE	DTEVT_COMMON

int	dt_prov_profile_alloc(struct dt_probe *, struct dt_softc *,
	    struct dt_pcb_list *, struct dtioc_req *);

struct dt_provider dt_prov_profile = {
	.dtpv_name	= "profile",
	.dtpv_alloc	= dt_prov_profile_alloc,
	.dtpv_enter	= NULL,
	.dtpv_leave	= NULL,
	.dtpv_dealloc	= NULL,
};

struct dt_provider dt_prov_interval = {
	.dtpv_name	= "interval",
	.dtpv_alloc	= dt_prov_profile_alloc,
	.dtpv_enter	= NULL,
	.dtpv_leave	= NULL,
	.dtpv_dealloc	= NULL,
};

int
dt_prov_profile_init(void)
{
	dtpp_profile = dt_dev_alloc_probe("hz", "97", &dt_prov_profile);
	if (dtpp_profile == NULL)
		return 0;
	dt_dev_register_probe(dtpp_profile);
	dtpp_interval = dt_dev_alloc_probe("hz", "1", &dt_prov_interval);
	if (dtpp_interval == NULL)
		return 1;
	dt_dev_register_probe(dtpp_interval);
	return 2;
}

int
dt_prov_profile_alloc(struct dt_probe *dtp, struct dt_softc *sc,
    struct dt_pcb_list *plist, struct dtioc_req *dtrq)
{
	uint64_t nsecs;
	struct dt_pcb *dp;
	struct cpu_info *ci;
	CPU_INFO_ITERATOR cii;

	KASSERT(TAILQ_EMPTY(plist));
	KASSERT(dtp == dtpp_profile || dtp == dtpp_interval);

	nsecs = dtrq->dtrq_nsecs;
	if (nsecs < USEC_TO_NSEC(200) || nsecs > SEC_TO_NSEC(UINT32_MAX))
		return EINVAL;

	CPU_INFO_FOREACH(cii, ci) {
		if (!CPU_IS_PRIMARY(ci) && (dtp == dtpp_interval))
			continue;

		dp = dt_pcb_alloc(dtp, sc);
		if (dp == NULL) {
			dt_pcb_purge(plist);
			return ENOMEM;
		}

		dp->dp_nsecs = nsecs;
		dp->dp_cpu = ci;

		dp->dp_evtflags = dtrq->dtrq_evtflags & DTEVT_PROV_PROFILE;
		TAILQ_INSERT_HEAD(plist, dp, dp_snext);
	}

	return 0;
}

void
dt_clock(struct clockrequest *cr, void *cf, void *arg)
{
	uint64_t count;
	struct dt_evt *dtev;
	struct dt_pcb *dp = arg;

	count = clockrequest_advance(cr, dp->dp_nsecs);
	if (count == 0)
		return;
	else if (count > 1)
		dt_pcb_ring_skiptick(dp, count - 1);

	dtev = dt_pcb_ring_get(dp, 1);
	if (dtev == NULL)
		return;
	dt_pcb_ring_consume(dp, dtev);
}
