/*	$OpenBSD: dt_prov_syscall.c,v 1.10 2025/07/25 05:18:05 jsg Exp $ */

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
#include <sys/malloc.h>
#include <sys/atomic.h>
#include <sys/syscall.h>

#include <dev/dt/dtvar.h>

/* Arrays of probes per syscall. */
struct dt_probe	**dtps_entry;
struct dt_probe	**dtps_return;
unsigned int	  dtps_nsysent = SYS_MAXSYSCALL;

/* Flags that make sense for this provider */
#define DTEVT_PROV_SYSCALL	(DTEVT_COMMON|DTEVT_FUNCARGS)

int	dt_prov_syscall_alloc(struct dt_probe *, struct dt_softc *,
	    struct dt_pcb_list *, struct dtioc_req *);
int	dt_prov_syscall_entry(struct dt_provider *, ...);
void	dt_prov_syscall_return(struct dt_provider *, ...);

struct dt_provider dt_prov_syscall = {
	.dtpv_name	= "syscall",
	.dtpv_alloc	= dt_prov_syscall_alloc,
	.dtpv_enter	= dt_prov_syscall_entry,
	.dtpv_leave	= dt_prov_syscall_return,
	.dtpv_dealloc	= NULL,
};

int
dt_prov_syscall_init(void)
{
	struct dt_probe *dtp;
	int i, len, nprobes = 0;
	char *sysnb;

	dtps_entry = mallocarray(dtps_nsysent, sizeof(dtp), M_DT,
	    M_NOWAIT|M_ZERO);
	if (dtps_entry == NULL)
		return 0;
	dtps_return = mallocarray(dtps_nsysent, sizeof(dtp), M_DT,
	    M_NOWAIT|M_ZERO);
	if (dtps_return == NULL) {
		free(dtps_entry, M_DT, dtps_nsysent * sizeof(dtp));
		return 0;
	}

	for (i = 0; i < dtps_nsysent; i++) {
		if (sysent[i].sy_call == sys_nosys)
			continue;

		len = snprintf(NULL, 0, "sys%%%u", i);
		sysnb = malloc(len + 1, M_DT, M_NOWAIT);
		if (sysnb == NULL)
			break;
		snprintf(sysnb, len + 1, "sys%%%u", i);
		dtp = dt_dev_alloc_probe(sysnb, "entry", &dt_prov_syscall);
		if (dtp == NULL) {
			free(sysnb, M_DT, len + 1);
			break;
		}
		dtp->dtp_nargs = sysent[i].sy_narg;
		dtp->dtp_sysnum = i;
		dtps_entry[i] = dtp;
		dt_dev_register_probe(dtp);
		nprobes++;
		dtp = dt_dev_alloc_probe(sysnb, "return", &dt_prov_syscall);
		if (dtp == NULL)
			break;
		dtp->dtp_sysnum = i;
		dtps_return[i] = dtp;
		dt_dev_register_probe(dtp);
		nprobes++;
	}

	return nprobes;
}

int
dt_prov_syscall_alloc(struct dt_probe *dtp, struct dt_softc *sc,
    struct dt_pcb_list *plist, struct dtioc_req *dtrq)
{
	struct dt_pcb *dp;

	KASSERT(TAILQ_EMPTY(plist));
	KASSERT(dtp->dtp_prov == &dt_prov_syscall);
	KASSERT((dtp->dtp_sysnum >= 0) && (dtp->dtp_sysnum < dtps_nsysent));

	dp = dt_pcb_alloc(dtp, sc);
	if (dp == NULL)
		return ENOMEM;

	dp->dp_evtflags = dtrq->dtrq_evtflags & DTEVT_PROV_SYSCALL;
	TAILQ_INSERT_HEAD(plist, dp, dp_snext);


	return 0;
}

int
dt_prov_syscall_entry(struct dt_provider *dtpv, ...)
{
	struct dt_probe *dtp;
	struct dt_pcb *dp;
	register_t sysnum;
	size_t argsize;
	register_t *args;
	va_list ap;

	KASSERT(dtpv == &dt_prov_syscall);
	va_start(ap, dtpv);
	sysnum = va_arg(ap, register_t);
	argsize = va_arg(ap, size_t);
	args = va_arg(ap, register_t*);
	va_end(ap);

	KASSERT((argsize / sizeof(register_t)) <= DTMAXFUNCARGS);

	if (sysnum < 0 || sysnum >= dtps_nsysent)
		return 0;

	dtp = dtps_entry[sysnum];
	if (!dtp->dtp_recording)
		return 0;

	smr_read_enter();
	SMR_SLIST_FOREACH(dp, &dtp->dtp_pcbs, dp_pnext) {
		struct dt_evt *dtev;

		dtev = dt_pcb_ring_get(dp, 0);
		if (dtev == NULL)
			continue;

		if (ISSET(dp->dp_evtflags, DTEVT_FUNCARGS))
			memcpy(dtev->dtev_args, args, argsize);

		dt_pcb_ring_consume(dp, dtev);
	}
	smr_read_leave();
	return 0;
}

void
dt_prov_syscall_return(struct dt_provider *dtpv, ...)
{
	struct dt_probe *dtp;
	struct dt_pcb *dp;
	register_t sysnum;
	int error;
	register_t retval[2];
	va_list ap;

	KASSERT(dtpv == &dt_prov_syscall);

	va_start(ap, dtpv);
	sysnum = va_arg(ap, register_t);
	error = va_arg(ap, int);
	retval[0] = va_arg(ap, register_t);
	retval[1] = va_arg(ap, register_t);
	va_end(ap);

	if (sysnum < 0 || sysnum >= dtps_nsysent)
		return;

	dtp = dtps_return[sysnum];
	if (!dtp->dtp_recording)
		return;

	smr_read_enter();
	SMR_SLIST_FOREACH(dp, &dtp->dtp_pcbs, dp_pnext) {
		struct dt_evt *dtev;

		dtev = dt_pcb_ring_get(dp, 0);
		if (dtev == NULL)
			continue;

		if (error) {
			dtev->dtev_retval[0] = -1;
			dtev->dtev_retval[1] = 0;
			dtev->dtev_error = error;
		} else {
			dtev->dtev_retval[0] = retval[0];
			dtev->dtev_retval[1] = retval[1];
			dtev->dtev_error = 0;
		}

		dt_pcb_ring_consume(dp, dtev);
	}
	smr_read_leave();
}
