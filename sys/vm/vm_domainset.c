/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017,	Jeffrey Roberson <jeff@freebsd.org>
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
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_vm.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bitset.h>
#include <sys/domainset.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/malloc.h>
#include <sys/rwlock.h>
#include <sys/vmmeter.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_domainset.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_phys.h>

#ifdef NUMA
/*
 * Iterators are written such that the first nowait pass has as short a
 * codepath as possible to eliminate bloat from the allocator.  It is
 * assumed that most allocations are successful.
 */

static int vm_domainset_default_stride = 64;

/*
 * Determine which policy is to be used for this allocation.
 */
static void
vm_domainset_iter_init(struct vm_domainset_iter *di, struct domainset *ds,
    int *iter, struct vm_object *obj, vm_pindex_t pindex)
{

	di->di_domain = ds;
	di->di_iter = iter;
	di->di_policy = ds->ds_policy;
	if (di->di_policy == DOMAINSET_POLICY_INTERLEAVE) {
#if VM_NRESERVLEVEL > 0
		if (vm_object_reserv(obj)) {
			/*
			 * Color the pindex so we end up on the correct
			 * reservation boundary.
			 */
			pindex += obj->pg_color;
			pindex >>= VM_LEVEL_0_ORDER;
		} else
#endif
			pindex /= vm_domainset_default_stride;
		/*
		 * Offset pindex so the first page of each object does
		 * not end up in domain 0.
		 */
		if (obj != NULL)
			pindex += (((uintptr_t)obj) / sizeof(*obj));
		di->di_offset = pindex;
	}
	/* Skip domains below min on the first pass. */
	di->di_minskip = true;
}

static void
vm_domainset_iter_rr(struct vm_domainset_iter *di, int *domain)
{

	*domain = di->di_domain->ds_order[
	    ++(*di->di_iter) % di->di_domain->ds_cnt];
}

static void
vm_domainset_iter_prefer(struct vm_domainset_iter *di, int *domain)
{
	int d;

	do {
		d = di->di_domain->ds_order[
		    ++(*di->di_iter) % di->di_domain->ds_cnt];
	} while (d == di->di_domain->ds_prefer);
	*domain = d;
}

static void
vm_domainset_iter_interleave(struct vm_domainset_iter *di, int *domain)
{
	int d;

	d = di->di_offset % di->di_domain->ds_cnt;
	*di->di_iter = d;
	*domain = di->di_domain->ds_order[d];
}

static void
vm_domainset_iter_next(struct vm_domainset_iter *di, int *domain)
{

	KASSERT(di->di_n > 0,
	    ("vm_domainset_iter_first: Invalid n %d", di->di_n));
	switch (di->di_policy) {
	case DOMAINSET_POLICY_FIRSTTOUCH:
		/*
		 * To prevent impossible allocations we convert an invalid
		 * first-touch to round-robin.
		 */
		/* FALLTHROUGH */
	case DOMAINSET_POLICY_INTERLEAVE:
		/* FALLTHROUGH */
	case DOMAINSET_POLICY_ROUNDROBIN:
		vm_domainset_iter_rr(di, domain);
		break;
	case DOMAINSET_POLICY_PREFER:
		vm_domainset_iter_prefer(di, domain);
		break;
	default:
		panic("vm_domainset_iter_first: Unknown policy %d",
		    di->di_policy);
	}
	KASSERT(*domain < vm_ndomains,
	    ("vm_domainset_iter_next: Invalid domain %d", *domain));
}

static void
vm_domainset_iter_first(struct vm_domainset_iter *di, int *domain)
{

	switch (di->di_policy) {
	case DOMAINSET_POLICY_FIRSTTOUCH:
		*domain = PCPU_GET(domain);
		if (DOMAINSET_ISSET(*domain, &di->di_domain->ds_mask)) {
			/*
			 * Add an extra iteration because we will visit the
			 * current domain a second time in the rr iterator.
			 */
			di->di_n = di->di_domain->ds_cnt + 1;
			break;
		}
		/*
		 * To prevent impossible allocations we convert an invalid
		 * first-touch to round-robin.
		 */
		/* FALLTHROUGH */
	case DOMAINSET_POLICY_ROUNDROBIN:
		di->di_n = di->di_domain->ds_cnt;
		vm_domainset_iter_rr(di, domain);
		break;
	case DOMAINSET_POLICY_PREFER:
		*domain = di->di_domain->ds_prefer;
		di->di_n = di->di_domain->ds_cnt;
		break;
	case DOMAINSET_POLICY_INTERLEAVE:
		vm_domainset_iter_interleave(di, domain);
		di->di_n = di->di_domain->ds_cnt;
		break;
	default:
		panic("vm_domainset_iter_first: Unknown policy %d",
		    di->di_policy);
	}
	KASSERT(di->di_n > 0,
	    ("vm_domainset_iter_first: Invalid n %d", di->di_n));
	KASSERT(*domain < vm_ndomains,
	    ("vm_domainset_iter_first: Invalid domain %d", *domain));
}

void
vm_domainset_iter_page_init(struct vm_domainset_iter *di, struct vm_object *obj,
    vm_pindex_t pindex, int *domain, int *req)
{
	struct domainset_ref *dr;

	/*
	 * Object policy takes precedence over thread policy.  The policies
	 * are immutable and unsynchronized.  Updates can race but pointer
	 * loads are assumed to be atomic.
	 */
	if (obj != NULL && obj->domain.dr_policy != NULL)
		dr = &obj->domain;
	else
		dr = &curthread->td_domain;
	vm_domainset_iter_init(di, dr->dr_policy, &dr->dr_iter, obj, pindex);
	di->di_flags = *req;
	*req = (di->di_flags & ~(VM_ALLOC_WAITOK | VM_ALLOC_WAITFAIL)) |
	    VM_ALLOC_NOWAIT;
	vm_domainset_iter_first(di, domain);
	if (vm_page_count_min_domain(*domain))
		vm_domainset_iter_page(di, obj, domain);
}

int
vm_domainset_iter_page(struct vm_domainset_iter *di, struct vm_object *obj,
    int *domain)
{

	/* If there are more domains to visit we run the iterator. */
	while (--di->di_n != 0) {
		vm_domainset_iter_next(di, domain);
		if (!di->di_minskip || !vm_page_count_min_domain(*domain))
			return (0);
	}

	/* If we skipped domains below min restart the search. */
	if (di->di_minskip) {
		di->di_minskip = false;
		vm_domainset_iter_first(di, domain);
		return (0);
	}

	/* If we visited all domains and this was a NOWAIT we return error. */
	if ((di->di_flags & (VM_ALLOC_WAITOK | VM_ALLOC_WAITFAIL)) == 0)
		return (ENOMEM);

	/* Wait for one of the domains to accumulate some free pages. */
	if (obj != NULL)
		VM_OBJECT_WUNLOCK(obj);
	vm_wait_doms(&di->di_domain->ds_mask);
	if (obj != NULL)
		VM_OBJECT_WLOCK(obj);
	if ((di->di_flags & VM_ALLOC_WAITFAIL) != 0)
		return (ENOMEM);

	/* Restart the search. */
	vm_domainset_iter_first(di, domain);

	return (0);
}

static void
_vm_domainset_iter_policy_init(struct vm_domainset_iter *di, int *domain,
    int *flags)
{

	di->di_flags = *flags;
	*flags = (di->di_flags & ~M_WAITOK) | M_NOWAIT;
	vm_domainset_iter_first(di, domain);
	if (vm_page_count_min_domain(*domain))
		vm_domainset_iter_policy(di, domain);
}

void
vm_domainset_iter_policy_init(struct vm_domainset_iter *di,
    struct domainset *ds, int *domain, int *flags)
{

	vm_domainset_iter_init(di, ds, &curthread->td_domain.dr_iter, NULL, 0);
	_vm_domainset_iter_policy_init(di, domain, flags);
}

void
vm_domainset_iter_policy_ref_init(struct vm_domainset_iter *di,
    struct domainset_ref *dr, int *domain, int *flags)
{

	vm_domainset_iter_init(di, dr->dr_policy, &dr->dr_iter, NULL, 0);
	_vm_domainset_iter_policy_init(di, domain, flags);
}

int
vm_domainset_iter_policy(struct vm_domainset_iter *di, int *domain)
{

	/* If there are more domains to visit we run the iterator. */
	while (--di->di_n != 0) {
		vm_domainset_iter_next(di, domain);
		if (!di->di_minskip || !vm_page_count_min_domain(*domain))
			return (0);
	}

	/* If we skipped domains below min restart the search. */
	if (di->di_minskip) {
		di->di_minskip = false;
		vm_domainset_iter_first(di, domain);
		return (0);
	}

	/* If we visited all domains and this was a NOWAIT we return error. */
	if ((di->di_flags & M_WAITOK) == 0)
		return (ENOMEM);

	/* Wait for one of the domains to accumulate some free pages. */
	vm_wait_doms(&di->di_domain->ds_mask);

	/* Restart the search. */
	vm_domainset_iter_first(di, domain);

	return (0);
}

#else /* !NUMA */

int
vm_domainset_iter_page(struct vm_domainset_iter *di, struct vm_object *obj,
    int *domain)
{

	return (EJUSTRETURN);
}

void
vm_domainset_iter_page_init(struct vm_domainset_iter *di, struct vm_object *obj,
    vm_pindex_t pindex, int *domain, int *flags)
{

	*domain = 0;
}

int
vm_domainset_iter_policy(struct vm_domainset_iter *di, int *domain)
{

	return (EJUSTRETURN);
}

void
vm_domainset_iter_policy_init(struct vm_domainset_iter *di,
    struct domainset *ds, int *domain, int *flags)
{

	*domain = 0;
}

void
vm_domainset_iter_policy_ref_init(struct vm_domainset_iter *di,
    struct domainset_ref *dr, int *domain, int *flags)
{

	*domain = 0;
}

#endif /* NUMA */
