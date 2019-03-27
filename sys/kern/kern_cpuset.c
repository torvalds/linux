/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008,  Jeffrey Roberson <jeff@freebsd.org>
 * All rights reserved.
 * 
 * Copyright (c) 2008 Nokia Corporation
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

#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/ctype.h>
#include <sys/sysproto.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/refcount.h>
#include <sys/sched.h>
#include <sys/smp.h>
#include <sys/syscallsubr.h>
#include <sys/capsicum.h>
#include <sys/cpuset.h>
#include <sys/domainset.h>
#include <sys/sx.h>
#include <sys/queue.h>
#include <sys/libkern.h>
#include <sys/limits.h>
#include <sys/bus.h>
#include <sys/interrupt.h>
#include <sys/vmmeter.h>

#include <vm/uma.h>
#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pageout.h>
#include <vm/vm_extern.h>
#include <vm/vm_param.h>
#include <vm/vm_phys.h>
#include <vm/vm_pagequeue.h>

#ifdef DDB
#include <ddb/ddb.h>
#endif /* DDB */

/*
 * cpusets provide a mechanism for creating and manipulating sets of
 * processors for the purpose of constraining the scheduling of threads to
 * specific processors.
 *
 * Each process belongs to an identified set, by default this is set 1.  Each
 * thread may further restrict the cpus it may run on to a subset of this
 * named set.  This creates an anonymous set which other threads and processes
 * may not join by number.
 *
 * The named set is referred to herein as the 'base' set to avoid ambiguity.
 * This set is usually a child of a 'root' set while the anonymous set may
 * simply be referred to as a mask.  In the syscall api these are referred to
 * as the ROOT, CPUSET, and MASK levels where CPUSET is called 'base' here.
 *
 * Threads inherit their set from their creator whether it be anonymous or
 * not.  This means that anonymous sets are immutable because they may be
 * shared.  To modify an anonymous set a new set is created with the desired
 * mask and the same parent as the existing anonymous set.  This gives the
 * illusion of each thread having a private mask.
 *
 * Via the syscall apis a user may ask to retrieve or modify the root, base,
 * or mask that is discovered via a pid, tid, or setid.  Modifying a set
 * modifies all numbered and anonymous child sets to comply with the new mask.
 * Modifying a pid or tid's mask applies only to that tid but must still
 * exist within the assigned parent set.
 *
 * A thread may not be assigned to a group separate from other threads in
 * the process.  This is to remove ambiguity when the setid is queried with
 * a pid argument.  There is no other technical limitation.
 *
 * This somewhat complex arrangement is intended to make it easy for
 * applications to query available processors and bind their threads to
 * specific processors while also allowing administrators to dynamically
 * reprovision by changing sets which apply to groups of processes.
 *
 * A simple application should not concern itself with sets at all and
 * rather apply masks to its own threads via CPU_WHICH_TID and a -1 id
 * meaning 'curthread'.  It may query available cpus for that tid with a
 * getaffinity call using (CPU_LEVEL_CPUSET, CPU_WHICH_PID, -1, ...).
 */

LIST_HEAD(domainlist, domainset);
struct domainset __read_mostly domainset_fixed[MAXMEMDOM];
struct domainset __read_mostly domainset_prefer[MAXMEMDOM];
struct domainset __read_mostly domainset_roundrobin;

static uma_zone_t cpuset_zone;
static uma_zone_t domainset_zone;
static struct mtx cpuset_lock;
static struct setlist cpuset_ids;
static struct domainlist cpuset_domains;
static struct unrhdr *cpuset_unr;
static struct cpuset *cpuset_zero, *cpuset_default, *cpuset_kernel;
static struct domainset domainset0, domainset2;

/* Return the size of cpuset_t at the kernel level */
SYSCTL_INT(_kern_sched, OID_AUTO, cpusetsize, CTLFLAG_RD | CTLFLAG_CAPRD,
    SYSCTL_NULL_INT_PTR, sizeof(cpuset_t), "sizeof(cpuset_t)");

cpuset_t *cpuset_root;
cpuset_t cpuset_domain[MAXMEMDOM];

static int domainset_valid(const struct domainset *, const struct domainset *);

/*
 * Find the first non-anonymous set starting from 'set'.
 */
static struct cpuset *
cpuset_getbase(struct cpuset *set)
{

	if (set->cs_id == CPUSET_INVALID)
		set = set->cs_parent;
	return (set);
}

/*
 * Walks up the tree from 'set' to find the root.
 */
static struct cpuset *
cpuset_getroot(struct cpuset *set)
{

	while ((set->cs_flags & CPU_SET_ROOT) == 0 && set->cs_parent != NULL)
		set = set->cs_parent;
	return (set);
}

/*
 * Acquire a reference to a cpuset, all pointers must be tracked with refs.
 */
struct cpuset *
cpuset_ref(struct cpuset *set)
{

	refcount_acquire(&set->cs_ref);
	return (set);
}

/*
 * Walks up the tree from 'set' to find the root.  Returns the root
 * referenced.
 */
static struct cpuset *
cpuset_refroot(struct cpuset *set)
{

	return (cpuset_ref(cpuset_getroot(set)));
}

/*
 * Find the first non-anonymous set starting from 'set'.  Returns this set
 * referenced.  May return the passed in set with an extra ref if it is
 * not anonymous. 
 */
static struct cpuset *
cpuset_refbase(struct cpuset *set)
{

	return (cpuset_ref(cpuset_getbase(set)));
}

/*
 * Release a reference in a context where it is safe to allocate.
 */
void
cpuset_rel(struct cpuset *set)
{
	cpusetid_t id;

	if (refcount_release(&set->cs_ref) == 0)
		return;
	mtx_lock_spin(&cpuset_lock);
	LIST_REMOVE(set, cs_siblings);
	id = set->cs_id;
	if (id != CPUSET_INVALID)
		LIST_REMOVE(set, cs_link);
	mtx_unlock_spin(&cpuset_lock);
	cpuset_rel(set->cs_parent);
	uma_zfree(cpuset_zone, set);
	if (id != CPUSET_INVALID)
		free_unr(cpuset_unr, id);
}

/*
 * Deferred release must be used when in a context that is not safe to
 * allocate/free.  This places any unreferenced sets on the list 'head'.
 */
static void
cpuset_rel_defer(struct setlist *head, struct cpuset *set)
{

	if (refcount_release(&set->cs_ref) == 0)
		return;
	mtx_lock_spin(&cpuset_lock);
	LIST_REMOVE(set, cs_siblings);
	if (set->cs_id != CPUSET_INVALID)
		LIST_REMOVE(set, cs_link);
	LIST_INSERT_HEAD(head, set, cs_link);
	mtx_unlock_spin(&cpuset_lock);
}

/*
 * Complete a deferred release.  Removes the set from the list provided to
 * cpuset_rel_defer.
 */
static void
cpuset_rel_complete(struct cpuset *set)
{
	LIST_REMOVE(set, cs_link);
	cpuset_rel(set->cs_parent);
	uma_zfree(cpuset_zone, set);
}

/*
 * Find a set based on an id.  Returns it with a ref.
 */
static struct cpuset *
cpuset_lookup(cpusetid_t setid, struct thread *td)
{
	struct cpuset *set;

	if (setid == CPUSET_INVALID)
		return (NULL);
	mtx_lock_spin(&cpuset_lock);
	LIST_FOREACH(set, &cpuset_ids, cs_link)
		if (set->cs_id == setid)
			break;
	if (set)
		cpuset_ref(set);
	mtx_unlock_spin(&cpuset_lock);

	KASSERT(td != NULL, ("[%s:%d] td is NULL", __func__, __LINE__));
	if (set != NULL && jailed(td->td_ucred)) {
		struct cpuset *jset, *tset;

		jset = td->td_ucred->cr_prison->pr_cpuset;
		for (tset = set; tset != NULL; tset = tset->cs_parent)
			if (tset == jset)
				break;
		if (tset == NULL) {
			cpuset_rel(set);
			set = NULL;
		}
	}

	return (set);
}

/*
 * Create a set in the space provided in 'set' with the provided parameters.
 * The set is returned with a single ref.  May return EDEADLK if the set
 * will have no valid cpu based on restrictions from the parent.
 */
static int
_cpuset_create(struct cpuset *set, struct cpuset *parent,
    const cpuset_t *mask, struct domainset *domain, cpusetid_t id)
{

	if (domain == NULL)
		domain = parent->cs_domain;
	if (mask == NULL)
		mask = &parent->cs_mask;
	if (!CPU_OVERLAP(&parent->cs_mask, mask))
		return (EDEADLK);
	/* The domain must be prepared ahead of time. */
	if (!domainset_valid(parent->cs_domain, domain))
		return (EDEADLK);
	CPU_COPY(mask, &set->cs_mask);
	LIST_INIT(&set->cs_children);
	refcount_init(&set->cs_ref, 1);
	set->cs_flags = 0;
	mtx_lock_spin(&cpuset_lock);
	set->cs_domain = domain;
	CPU_AND(&set->cs_mask, &parent->cs_mask);
	set->cs_id = id;
	set->cs_parent = cpuset_ref(parent);
	LIST_INSERT_HEAD(&parent->cs_children, set, cs_siblings);
	if (set->cs_id != CPUSET_INVALID)
		LIST_INSERT_HEAD(&cpuset_ids, set, cs_link);
	mtx_unlock_spin(&cpuset_lock);

	return (0);
}

/*
 * Create a new non-anonymous set with the requested parent and mask.  May
 * return failures if the mask is invalid or a new number can not be
 * allocated.
 */
static int
cpuset_create(struct cpuset **setp, struct cpuset *parent, const cpuset_t *mask)
{
	struct cpuset *set;
	cpusetid_t id;
	int error;

	id = alloc_unr(cpuset_unr);
	if (id == -1)
		return (ENFILE);
	*setp = set = uma_zalloc(cpuset_zone, M_WAITOK | M_ZERO);
	error = _cpuset_create(set, parent, mask, NULL, id);
	if (error == 0)
		return (0);
	free_unr(cpuset_unr, id);
	uma_zfree(cpuset_zone, set);

	return (error);
}

static void
cpuset_freelist_add(struct setlist *list, int count)
{
	struct cpuset *set;
	int i;

	for (i = 0; i < count; i++) {
		set = uma_zalloc(cpuset_zone, M_ZERO | M_WAITOK);
		LIST_INSERT_HEAD(list, set, cs_link);
	}
}

static void
cpuset_freelist_init(struct setlist *list, int count)
{

	LIST_INIT(list);
	cpuset_freelist_add(list, count);
}

static void
cpuset_freelist_free(struct setlist *list)
{
	struct cpuset *set;

	while ((set = LIST_FIRST(list)) != NULL) {
		LIST_REMOVE(set, cs_link);
		uma_zfree(cpuset_zone, set);
	}
}

static void
domainset_freelist_add(struct domainlist *list, int count)
{
	struct domainset *set;
	int i;

	for (i = 0; i < count; i++) {
		set = uma_zalloc(domainset_zone, M_ZERO | M_WAITOK);
		LIST_INSERT_HEAD(list, set, ds_link);
	}
}

static void
domainset_freelist_init(struct domainlist *list, int count)
{

	LIST_INIT(list);
	domainset_freelist_add(list, count);
}

static void
domainset_freelist_free(struct domainlist *list)
{
	struct domainset *set;

	while ((set = LIST_FIRST(list)) != NULL) {
		LIST_REMOVE(set, ds_link);
		uma_zfree(domainset_zone, set);
	}
}

/* Copy a domainset preserving mask and policy. */
static void
domainset_copy(const struct domainset *from, struct domainset *to)
{

	DOMAINSET_COPY(&from->ds_mask, &to->ds_mask);
	to->ds_policy = from->ds_policy;
	to->ds_prefer = from->ds_prefer;
}

/* Return 1 if mask and policy are equal, otherwise 0. */
static int
domainset_equal(const struct domainset *one, const struct domainset *two)
{

	return (DOMAINSET_CMP(&one->ds_mask, &two->ds_mask) == 0 &&
	    one->ds_policy == two->ds_policy &&
	    one->ds_prefer == two->ds_prefer);
}

/* Return 1 if child is a valid subset of parent. */
static int
domainset_valid(const struct domainset *parent, const struct domainset *child)
{
	if (child->ds_policy != DOMAINSET_POLICY_PREFER)
		return (DOMAINSET_SUBSET(&parent->ds_mask, &child->ds_mask));
	return (DOMAINSET_ISSET(child->ds_prefer, &parent->ds_mask));
}

static int
domainset_restrict(const struct domainset *parent,
    const struct domainset *child)
{
	if (child->ds_policy != DOMAINSET_POLICY_PREFER)
		return (DOMAINSET_OVERLAP(&parent->ds_mask, &child->ds_mask));
	return (DOMAINSET_ISSET(child->ds_prefer, &parent->ds_mask));
}

/*
 * Lookup or create a domainset.  The key is provided in ds_mask and
 * ds_policy.  If the domainset does not yet exist the storage in
 * 'domain' is used to insert.  Otherwise this storage is freed to the
 * domainset_zone and the existing domainset is returned.
 */
static struct domainset *
_domainset_create(struct domainset *domain, struct domainlist *freelist)
{
	struct domainset *ndomain;
	int i, j, max;

	KASSERT(domain->ds_cnt <= vm_ndomains,
	    ("invalid domain count in domainset %p", domain));
	KASSERT(domain->ds_policy != DOMAINSET_POLICY_PREFER ||
	    domain->ds_prefer < vm_ndomains,
	    ("invalid preferred domain in domains %p", domain));

	mtx_lock_spin(&cpuset_lock);
	LIST_FOREACH(ndomain, &cpuset_domains, ds_link)
		if (domainset_equal(ndomain, domain))
			break;
	/*
	 * If the domain does not yet exist we insert it and initialize
	 * various iteration helpers which are not part of the key.
	 */
	if (ndomain == NULL) {
		LIST_INSERT_HEAD(&cpuset_domains, domain, ds_link);
		domain->ds_cnt = DOMAINSET_COUNT(&domain->ds_mask);
		max = DOMAINSET_FLS(&domain->ds_mask) + 1;
		for (i = 0, j = 0; i < max; i++)
			if (DOMAINSET_ISSET(i, &domain->ds_mask))
				domain->ds_order[j++] = i;
	}
	mtx_unlock_spin(&cpuset_lock);
	if (ndomain == NULL)
		return (domain);
	if (freelist != NULL)
		LIST_INSERT_HEAD(freelist, domain, ds_link);
	else
		uma_zfree(domainset_zone, domain);
	return (ndomain);
	
}

/*
 * Are any of the domains in the mask empty?  If so, silently
 * remove them and update the domainset accordingly.  If only empty
 * domains are present, we must return failure.
 */
static bool
domainset_empty_vm(struct domainset *domain)
{
	int i, j, max;

	max = DOMAINSET_FLS(&domain->ds_mask) + 1;
	for (i = 0; i < max; i++)
		if (DOMAINSET_ISSET(i, &domain->ds_mask) && VM_DOMAIN_EMPTY(i))
			DOMAINSET_CLR(i, &domain->ds_mask);
	domain->ds_cnt = DOMAINSET_COUNT(&domain->ds_mask);
	max = DOMAINSET_FLS(&domain->ds_mask) + 1;
	for (i = j = 0; i < max; i++) {
		if (DOMAINSET_ISSET(i, &domain->ds_mask))
			domain->ds_order[j++] = i;
		else if (domain->ds_policy == DOMAINSET_POLICY_PREFER &&
		    domain->ds_prefer == i && domain->ds_cnt > 1) {
			domain->ds_policy = DOMAINSET_POLICY_ROUNDROBIN;
			domain->ds_prefer = -1;
		}
	}

	return (DOMAINSET_EMPTY(&domain->ds_mask));
}

/*
 * Create or lookup a domainset based on the key held in 'domain'.
 */
struct domainset *
domainset_create(const struct domainset *domain)
{
	struct domainset *ndomain;

	/*
	 * Validate the policy.  It must specify a useable policy number with
	 * only valid domains.  Preferred must include the preferred domain
	 * in the mask.
	 */
	if (domain->ds_policy <= DOMAINSET_POLICY_INVALID ||
	    domain->ds_policy > DOMAINSET_POLICY_MAX)
		return (NULL);
	if (domain->ds_policy == DOMAINSET_POLICY_PREFER &&
	    !DOMAINSET_ISSET(domain->ds_prefer, &domain->ds_mask))
		return (NULL);
	if (!DOMAINSET_SUBSET(&domainset0.ds_mask, &domain->ds_mask))
		return (NULL);
	ndomain = uma_zalloc(domainset_zone, M_WAITOK | M_ZERO);
	domainset_copy(domain, ndomain);
	return _domainset_create(ndomain, NULL);
}

/*
 * Update thread domainset pointers.
 */
static void
domainset_notify(void)
{
	struct thread *td;
	struct proc *p;

	sx_slock(&allproc_lock);
	FOREACH_PROC_IN_SYSTEM(p) {
		PROC_LOCK(p);
		if (p->p_state == PRS_NEW) {
			PROC_UNLOCK(p);
			continue;
		}
		FOREACH_THREAD_IN_PROC(p, td) {
			thread_lock(td);
			td->td_domain.dr_policy = td->td_cpuset->cs_domain;
			thread_unlock(td);
		}
		PROC_UNLOCK(p);
	}
	sx_sunlock(&allproc_lock);
	kernel_object->domain.dr_policy = cpuset_kernel->cs_domain;
}

/*
 * Create a new set that is a subset of a parent.
 */
static struct domainset *
domainset_shadow(const struct domainset *pdomain,
    const struct domainset *domain, struct domainlist *freelist)
{
	struct domainset *ndomain;

	ndomain = LIST_FIRST(freelist);
	LIST_REMOVE(ndomain, ds_link);

	/*
	 * Initialize the key from the request.
	 */
	domainset_copy(domain, ndomain);

	/*
	 * Restrict the key by the parent.
	 */
	DOMAINSET_AND(&ndomain->ds_mask, &pdomain->ds_mask);

	return _domainset_create(ndomain, freelist);
}

/*
 * Recursively check for errors that would occur from applying mask to
 * the tree of sets starting at 'set'.  Checks for sets that would become
 * empty as well as RDONLY flags.
 */
static int
cpuset_testupdate(struct cpuset *set, cpuset_t *mask, int check_mask)
{
	struct cpuset *nset;
	cpuset_t newmask;
	int error;

	mtx_assert(&cpuset_lock, MA_OWNED);
	if (set->cs_flags & CPU_SET_RDONLY)
		return (EPERM);
	if (check_mask) {
		if (!CPU_OVERLAP(&set->cs_mask, mask))
			return (EDEADLK);
		CPU_COPY(&set->cs_mask, &newmask);
		CPU_AND(&newmask, mask);
	} else
		CPU_COPY(mask, &newmask);
	error = 0;
	LIST_FOREACH(nset, &set->cs_children, cs_siblings) 
		if ((error = cpuset_testupdate(nset, &newmask, 1)) != 0)
			break;
	return (error);
}

/*
 * Applies the mask 'mask' without checking for empty sets or permissions.
 */
static void
cpuset_update(struct cpuset *set, cpuset_t *mask)
{
	struct cpuset *nset;

	mtx_assert(&cpuset_lock, MA_OWNED);
	CPU_AND(&set->cs_mask, mask);
	LIST_FOREACH(nset, &set->cs_children, cs_siblings) 
		cpuset_update(nset, &set->cs_mask);

	return;
}

/*
 * Modify the set 'set' to use a copy of the mask provided.  Apply this new
 * mask to restrict all children in the tree.  Checks for validity before
 * applying the changes.
 */
static int
cpuset_modify(struct cpuset *set, cpuset_t *mask)
{
	struct cpuset *root;
	int error;

	error = priv_check(curthread, PRIV_SCHED_CPUSET);
	if (error)
		return (error);
	/*
	 * In case we are called from within the jail
	 * we do not allow modifying the dedicated root
	 * cpuset of the jail but may still allow to
	 * change child sets.
	 */
	if (jailed(curthread->td_ucred) &&
	    set->cs_flags & CPU_SET_ROOT)
		return (EPERM);
	/*
	 * Verify that we have access to this set of
	 * cpus.
	 */
	root = cpuset_getroot(set);
	mtx_lock_spin(&cpuset_lock);
	if (root && !CPU_SUBSET(&root->cs_mask, mask)) {
		error = EINVAL;
		goto out;
	}
	error = cpuset_testupdate(set, mask, 0);
	if (error)
		goto out;
	CPU_COPY(mask, &set->cs_mask);
	cpuset_update(set, mask);
out:
	mtx_unlock_spin(&cpuset_lock);

	return (error);
}

/*
 * Recursively check for errors that would occur from applying mask to
 * the tree of sets starting at 'set'.  Checks for sets that would become
 * empty as well as RDONLY flags.
 */
static int
cpuset_testupdate_domain(struct cpuset *set, struct domainset *dset,
    struct domainset *orig, int *count, int check_mask)
{
	struct cpuset *nset;
	struct domainset *domain;
	struct domainset newset;
	int error;

	mtx_assert(&cpuset_lock, MA_OWNED);
	if (set->cs_flags & CPU_SET_RDONLY)
		return (EPERM);
	domain = set->cs_domain;
	domainset_copy(domain, &newset);
	if (!domainset_equal(domain, orig)) {
		if (!domainset_restrict(domain, dset))
			return (EDEADLK);
		DOMAINSET_AND(&newset.ds_mask, &dset->ds_mask);
		/* Count the number of domains that are changing. */
		(*count)++;
	}
	error = 0;
	LIST_FOREACH(nset, &set->cs_children, cs_siblings) 
		if ((error = cpuset_testupdate_domain(nset, &newset, domain,
		    count, 1)) != 0)
			break;
	return (error);
}

/*
 * Applies the mask 'mask' without checking for empty sets or permissions.
 */
static void
cpuset_update_domain(struct cpuset *set, struct domainset *domain,
    struct domainset *orig, struct domainlist *domains)
{
	struct cpuset *nset;

	mtx_assert(&cpuset_lock, MA_OWNED);
	/*
	 * If this domainset has changed from the parent we must calculate
	 * a new set.  Otherwise it simply inherits from the parent.  When
	 * we inherit from the parent we get a new mask and policy.  If the
	 * set is modified from the parent we keep the policy and only
	 * update the mask.
	 */
	if (set->cs_domain != orig) {
		orig = set->cs_domain;
		set->cs_domain = domainset_shadow(domain, orig, domains);
	} else
		set->cs_domain = domain;
	LIST_FOREACH(nset, &set->cs_children, cs_siblings) 
		cpuset_update_domain(nset, set->cs_domain, orig, domains);

	return;
}

/*
 * Modify the set 'set' to use a copy the domainset provided.  Apply this new
 * mask to restrict all children in the tree.  Checks for validity before
 * applying the changes.
 */
static int
cpuset_modify_domain(struct cpuset *set, struct domainset *domain)
{
	struct domainlist domains;
	struct domainset temp;
	struct domainset *dset;
	struct cpuset *root;
	int ndomains, needed;
	int error;

	error = priv_check(curthread, PRIV_SCHED_CPUSET);
	if (error)
		return (error);
	/*
	 * In case we are called from within the jail
	 * we do not allow modifying the dedicated root
	 * cpuset of the jail but may still allow to
	 * change child sets.
	 */
	if (jailed(curthread->td_ucred) &&
	    set->cs_flags & CPU_SET_ROOT)
		return (EPERM);
	domainset_freelist_init(&domains, 0);
	domain = domainset_create(domain);
	ndomains = needed = 0;
	do {
		if (ndomains < needed) {
			domainset_freelist_add(&domains, needed - ndomains);
			ndomains = needed;
		}
		root = cpuset_getroot(set);
		mtx_lock_spin(&cpuset_lock);
		dset = root->cs_domain;
		/*
		 * Verify that we have access to this set of domains.
		 */
		if (root && !domainset_valid(dset, domain)) {
			error = EINVAL;
			goto out;
		}
		/*
		 * If applying prefer we keep the current set as the fallback.
		 */
		if (domain->ds_policy == DOMAINSET_POLICY_PREFER)
			DOMAINSET_COPY(&set->cs_domain->ds_mask,
			    &domain->ds_mask);
		/*
		 * Determine whether we can apply this set of domains and
		 * how many new domain structures it will require.
		 */
		domainset_copy(domain, &temp);
		needed = 0;
		error = cpuset_testupdate_domain(set, &temp, set->cs_domain,
		    &needed, 0);
		if (error)
			goto out;
	} while (ndomains < needed);
	dset = set->cs_domain;
	cpuset_update_domain(set, domain, dset, &domains);
out:
	mtx_unlock_spin(&cpuset_lock);
	domainset_freelist_free(&domains);
	if (error == 0)
		domainset_notify();

	return (error);
}

/*
 * Resolve the 'which' parameter of several cpuset apis.
 *
 * For WHICH_PID and WHICH_TID return a locked proc and valid proc/tid.  Also
 * checks for permission via p_cansched().
 *
 * For WHICH_SET returns a valid set with a new reference.
 *
 * -1 may be supplied for any argument to mean the current proc/thread or
 * the base set of the current thread.  May fail with ESRCH/EPERM.
 */
int
cpuset_which(cpuwhich_t which, id_t id, struct proc **pp, struct thread **tdp,
    struct cpuset **setp)
{
	struct cpuset *set;
	struct thread *td;
	struct proc *p;
	int error;

	*pp = p = NULL;
	*tdp = td = NULL;
	*setp = set = NULL;
	switch (which) {
	case CPU_WHICH_PID:
		if (id == -1) {
			PROC_LOCK(curproc);
			p = curproc;
			break;
		}
		if ((p = pfind(id)) == NULL)
			return (ESRCH);
		break;
	case CPU_WHICH_TID:
		if (id == -1) {
			PROC_LOCK(curproc);
			p = curproc;
			td = curthread;
			break;
		}
		td = tdfind(id, -1);
		if (td == NULL)
			return (ESRCH);
		p = td->td_proc;
		break;
	case CPU_WHICH_CPUSET:
		if (id == -1) {
			thread_lock(curthread);
			set = cpuset_refbase(curthread->td_cpuset);
			thread_unlock(curthread);
		} else
			set = cpuset_lookup(id, curthread);
		if (set) {
			*setp = set;
			return (0);
		}
		return (ESRCH);
	case CPU_WHICH_JAIL:
	{
		/* Find `set' for prison with given id. */
		struct prison *pr;

		sx_slock(&allprison_lock);
		pr = prison_find_child(curthread->td_ucred->cr_prison, id);
		sx_sunlock(&allprison_lock);
		if (pr == NULL)
			return (ESRCH);
		cpuset_ref(pr->pr_cpuset);
		*setp = pr->pr_cpuset;
		mtx_unlock(&pr->pr_mtx);
		return (0);
	}
	case CPU_WHICH_IRQ:
	case CPU_WHICH_DOMAIN:
		return (0);
	default:
		return (EINVAL);
	}
	error = p_cansched(curthread, p);
	if (error) {
		PROC_UNLOCK(p);
		return (error);
	}
	if (td == NULL)
		td = FIRST_THREAD_IN_PROC(p);
	*pp = p;
	*tdp = td;
	return (0);
}

static int
cpuset_testshadow(struct cpuset *set, const cpuset_t *mask,
    const struct domainset *domain)
{
	struct cpuset *parent;
	struct domainset *dset;

	parent = cpuset_getbase(set);
	/*
	 * If we are restricting a cpu mask it must be a subset of the
	 * parent or invalid CPUs have been specified.
	 */
	if (mask != NULL && !CPU_SUBSET(&parent->cs_mask, mask))
		return (EINVAL);

	/*
	 * If we are restricting a domain mask it must be a subset of the
	 * parent or invalid domains have been specified.
	 */
	dset = parent->cs_domain;
	if (domain != NULL && !domainset_valid(dset, domain))
		return (EINVAL);

	return (0);
}

/*
 * Create an anonymous set with the provided mask in the space provided by
 * 'nset'.  If the passed in set is anonymous we use its parent otherwise
 * the new set is a child of 'set'.
 */
static int
cpuset_shadow(struct cpuset *set, struct cpuset **nsetp,
   const cpuset_t *mask, const struct domainset *domain,
   struct setlist *cpusets, struct domainlist *domains)
{
	struct cpuset *parent;
	struct cpuset *nset;
	struct domainset *dset;
	struct domainset *d;
	int error;

	error = cpuset_testshadow(set, mask, domain);
	if (error)
		return (error);

	parent = cpuset_getbase(set);
	dset = parent->cs_domain;
	if (mask == NULL)
		mask = &set->cs_mask;
	if (domain != NULL)
		d = domainset_shadow(dset, domain, domains);
	else
		d = set->cs_domain;
	nset = LIST_FIRST(cpusets);
	error = _cpuset_create(nset, parent, mask, d, CPUSET_INVALID);
	if (error == 0) {
		LIST_REMOVE(nset, cs_link);
		*nsetp = nset;
	}
	return (error);
}

static struct cpuset *
cpuset_update_thread(struct thread *td, struct cpuset *nset)
{
	struct cpuset *tdset;

	tdset = td->td_cpuset;
	td->td_cpuset = nset;
	td->td_domain.dr_policy = nset->cs_domain;
	sched_affinity(td);

	return (tdset);
}

static int
cpuset_setproc_test_maskthread(struct cpuset *tdset, cpuset_t *mask,
    struct domainset *domain)
{
	struct cpuset *parent;

	parent = cpuset_getbase(tdset);
	if (mask == NULL)
		mask = &tdset->cs_mask;
	if (domain == NULL)
		domain = tdset->cs_domain;
	return cpuset_testshadow(parent, mask, domain);
}

static int
cpuset_setproc_maskthread(struct cpuset *tdset, cpuset_t *mask,
    struct domainset *domain, struct cpuset **nsetp,
    struct setlist *freelist, struct domainlist *domainlist)
{
	struct cpuset *parent;

	parent = cpuset_getbase(tdset);
	if (mask == NULL)
		mask = &tdset->cs_mask;
	if (domain == NULL)
		domain = tdset->cs_domain;
	return cpuset_shadow(parent, nsetp, mask, domain, freelist,
	    domainlist);
}

static int
cpuset_setproc_setthread_mask(struct cpuset *tdset, struct cpuset *set,
    cpuset_t *mask, struct domainset *domain)
{
	struct cpuset *parent;

	parent = cpuset_getbase(tdset);

	/*
	 * If the thread restricted its mask then apply that same
	 * restriction to the new set, otherwise take it wholesale.
	 */
	if (CPU_CMP(&tdset->cs_mask, &parent->cs_mask) != 0) {
		CPU_COPY(&tdset->cs_mask, mask);
		CPU_AND(mask, &set->cs_mask);
	} else
		CPU_COPY(&set->cs_mask, mask);

	/*
	 * If the thread restricted the domain then we apply the
	 * restriction to the new set but retain the policy.
	 */
	if (tdset->cs_domain != parent->cs_domain) {
		domainset_copy(tdset->cs_domain, domain);
		DOMAINSET_AND(&domain->ds_mask, &set->cs_domain->ds_mask);
	} else
		domainset_copy(set->cs_domain, domain);

	if (CPU_EMPTY(mask) || DOMAINSET_EMPTY(&domain->ds_mask))
		return (EDEADLK);

	return (0);
}

static int
cpuset_setproc_test_setthread(struct cpuset *tdset, struct cpuset *set)
{
	struct domainset domain;
	cpuset_t mask;

	if (tdset->cs_id != CPUSET_INVALID)
		return (0);
	return cpuset_setproc_setthread_mask(tdset, set, &mask, &domain);
}

static int
cpuset_setproc_setthread(struct cpuset *tdset, struct cpuset *set,
    struct cpuset **nsetp, struct setlist *freelist,
    struct domainlist *domainlist)
{
	struct domainset domain;
	cpuset_t mask;
	int error;

	/*
	 * If we're replacing on a thread that has not constrained the
	 * original set we can simply accept the new set.
	 */
	if (tdset->cs_id != CPUSET_INVALID) {
		*nsetp = cpuset_ref(set);
		return (0);
	}
	error = cpuset_setproc_setthread_mask(tdset, set, &mask, &domain);
	if (error)
		return (error);

	return cpuset_shadow(tdset, nsetp, &mask, &domain, freelist,
	    domainlist);
}

/*
 * Handle three cases for updating an entire process.
 *
 * 1) Set is non-null.  This reparents all anonymous sets to the provided
 *    set and replaces all non-anonymous td_cpusets with the provided set.
 * 2) Mask is non-null.  This replaces or creates anonymous sets for every
 *    thread with the existing base as a parent.
 * 3) domain is non-null.  This creates anonymous sets for every thread
 *    and replaces the domain set.
 *
 * This is overly complicated because we can't allocate while holding a 
 * spinlock and spinlocks must be held while changing and examining thread
 * state.
 */
static int
cpuset_setproc(pid_t pid, struct cpuset *set, cpuset_t *mask,
    struct domainset *domain)
{
	struct setlist freelist;
	struct setlist droplist;
	struct domainlist domainlist;
	struct cpuset *nset;
	struct thread *td;
	struct proc *p;
	int threads;
	int nfree;
	int error;

	/*
	 * The algorithm requires two passes due to locking considerations.
	 * 
	 * 1) Lookup the process and acquire the locks in the required order.
	 * 2) If enough cpusets have not been allocated release the locks and
	 *    allocate them.  Loop.
	 */
	cpuset_freelist_init(&freelist, 1);
	domainset_freelist_init(&domainlist, 1);
	nfree = 1;
	LIST_INIT(&droplist);
	nfree = 0;
	for (;;) {
		error = cpuset_which(CPU_WHICH_PID, pid, &p, &td, &nset);
		if (error)
			goto out;
		if (nfree >= p->p_numthreads)
			break;
		threads = p->p_numthreads;
		PROC_UNLOCK(p);
		if (nfree < threads) {
			cpuset_freelist_add(&freelist, threads - nfree);
			domainset_freelist_add(&domainlist, threads - nfree);
			nfree = threads;
		}
	}
	PROC_LOCK_ASSERT(p, MA_OWNED);
	/*
	 * Now that the appropriate locks are held and we have enough cpusets,
	 * make sure the operation will succeed before applying changes. The
	 * proc lock prevents td_cpuset from changing between calls.
	 */
	error = 0;
	FOREACH_THREAD_IN_PROC(p, td) {
		thread_lock(td);
		if (set != NULL)
			error = cpuset_setproc_test_setthread(td->td_cpuset,
			    set);
		else
			error = cpuset_setproc_test_maskthread(td->td_cpuset,
			    mask, domain);
		thread_unlock(td);
		if (error)
			goto unlock_out;
	}
	/*
	 * Replace each thread's cpuset while using deferred release.  We
	 * must do this because the thread lock must be held while operating
	 * on the thread and this limits the type of operations allowed.
	 */
	FOREACH_THREAD_IN_PROC(p, td) {
		thread_lock(td);
		if (set != NULL)
			error = cpuset_setproc_setthread(td->td_cpuset, set,
			    &nset, &freelist, &domainlist);
		else
			error = cpuset_setproc_maskthread(td->td_cpuset, mask,
			    domain, &nset, &freelist, &domainlist);
		if (error) {
			thread_unlock(td);
			break;
		}
		cpuset_rel_defer(&droplist, cpuset_update_thread(td, nset));
		thread_unlock(td);
	}
unlock_out:
	PROC_UNLOCK(p);
out:
	while ((nset = LIST_FIRST(&droplist)) != NULL)
		cpuset_rel_complete(nset);
	cpuset_freelist_free(&freelist);
	domainset_freelist_free(&domainlist);
	return (error);
}

static int
bitset_strprint(char *buf, size_t bufsiz, const struct bitset *set, int setlen)
{
	size_t bytes;
	int i, once;
	char *p;

	once = 0;
	p = buf;
	for (i = 0; i < __bitset_words(setlen); i++) {
		if (once != 0) {
			if (bufsiz < 1)
				return (0);
			*p = ',';
			p++;
			bufsiz--;
		} else
			once = 1;
		if (bufsiz < sizeof(__STRING(ULONG_MAX)))
			return (0);
		bytes = snprintf(p, bufsiz, "%lx", set->__bits[i]);
		p += bytes;
		bufsiz -= bytes;
	}
	return (p - buf);
}

static int
bitset_strscan(struct bitset *set, int setlen, const char *buf)
{
	int i, ret;
	const char *p;

	BIT_ZERO(setlen, set);
	p = buf;
	for (i = 0; i < __bitset_words(setlen); i++) {
		if (*p == ',') {
			p++;
			continue;
		}
		ret = sscanf(p, "%lx", &set->__bits[i]);
		if (ret == 0 || ret == -1)
			break;
		while (isxdigit(*p))
			p++;
	}
	return (p - buf);
}

/*
 * Return a string representing a valid layout for a cpuset_t object.
 * It expects an incoming buffer at least sized as CPUSETBUFSIZ.
 */
char *
cpusetobj_strprint(char *buf, const cpuset_t *set)
{

	bitset_strprint(buf, CPUSETBUFSIZ, (const struct bitset *)set,
	    CPU_SETSIZE);
	return (buf);
}

/*
 * Build a valid cpuset_t object from a string representation.
 * It expects an incoming buffer at least sized as CPUSETBUFSIZ.
 */
int
cpusetobj_strscan(cpuset_t *set, const char *buf)
{
	char p;

	if (strlen(buf) > CPUSETBUFSIZ - 1)
		return (-1);

	p = buf[bitset_strscan((struct bitset *)set, CPU_SETSIZE, buf)];
	if (p != '\0')
		return (-1);

	return (0);
}

/*
 * Handle a domainset specifier in the sysctl tree.  A poiner to a pointer to
 * a domainset is in arg1.  If the user specifies a valid domainset the
 * pointer is updated.
 *
 * Format is:
 * hex mask word 0,hex mask word 1,...:decimal policy:decimal preferred
 */
int
sysctl_handle_domainset(SYSCTL_HANDLER_ARGS)
{
	char buf[DOMAINSETBUFSIZ];
	struct domainset *dset;
	struct domainset key;
	int policy, prefer, error;
	char *p;

	dset = *(struct domainset **)arg1;
	error = 0;

	if (dset != NULL) {
		p = buf + bitset_strprint(buf, DOMAINSETBUFSIZ,
		    (const struct bitset *)&dset->ds_mask, DOMAINSET_SETSIZE);
		sprintf(p, ":%d:%d", dset->ds_policy, dset->ds_prefer);
	} else
		sprintf(buf, "<NULL>");
	error = sysctl_handle_string(oidp, buf, sizeof(buf), req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	/*
	 * Read in and validate the string.
	 */
	memset(&key, 0, sizeof(key));
	p = &buf[bitset_strscan((struct bitset *)&key.ds_mask,
	    DOMAINSET_SETSIZE, buf)];
	if (p == buf)
		return (EINVAL);
	if (sscanf(p, ":%d:%d", &policy, &prefer) != 2)
		return (EINVAL);
	key.ds_policy = policy;
	key.ds_prefer = prefer;

	/* Domainset_create() validates the policy.*/
	dset = domainset_create(&key);
	if (dset == NULL)
		return (EINVAL);
	*(struct domainset **)arg1 = dset;

	return (error);
}

/*
 * Apply an anonymous mask or a domain to a single thread.
 */
static int
_cpuset_setthread(lwpid_t id, cpuset_t *mask, struct domainset *domain)
{
	struct setlist cpusets;
	struct domainlist domainlist;
	struct cpuset *nset;
	struct cpuset *set;
	struct thread *td;
	struct proc *p;
	int error;

	cpuset_freelist_init(&cpusets, 1);
	domainset_freelist_init(&domainlist, domain != NULL);
	error = cpuset_which(CPU_WHICH_TID, id, &p, &td, &set);
	if (error)
		goto out;
	set = NULL;
	thread_lock(td);
	error = cpuset_shadow(td->td_cpuset, &nset, mask, domain,
	    &cpusets, &domainlist);
	if (error == 0)
		set = cpuset_update_thread(td, nset);
	thread_unlock(td);
	PROC_UNLOCK(p);
	if (set)
		cpuset_rel(set);
out:
	cpuset_freelist_free(&cpusets);
	domainset_freelist_free(&domainlist);
	return (error);
}

/*
 * Apply an anonymous mask to a single thread.
 */
int
cpuset_setthread(lwpid_t id, cpuset_t *mask)
{

	return _cpuset_setthread(id, mask, NULL);
}

/*
 * Apply new cpumask to the ithread.
 */
int
cpuset_setithread(lwpid_t id, int cpu)
{
	cpuset_t mask;

	CPU_ZERO(&mask);
	if (cpu == NOCPU)
		CPU_COPY(cpuset_root, &mask);
	else
		CPU_SET(cpu, &mask);
	return _cpuset_setthread(id, &mask, NULL);
}

/*
 * Initialize static domainsets after NUMA information is available.  This is
 * called before memory allocators are initialized.
 */
void
domainset_init(void)
{
	struct domainset *dset;
	int i;

	dset = &domainset_roundrobin;
	DOMAINSET_COPY(&all_domains, &dset->ds_mask);
	dset->ds_policy = DOMAINSET_POLICY_ROUNDROBIN;
	dset->ds_prefer = -1;
	_domainset_create(dset, NULL);

	for (i = 0; i < vm_ndomains; i++) {
		dset = &domainset_fixed[i];
		DOMAINSET_ZERO(&dset->ds_mask);
		DOMAINSET_SET(i, &dset->ds_mask);
		dset->ds_policy = DOMAINSET_POLICY_ROUNDROBIN;
		_domainset_create(dset, NULL);

		dset = &domainset_prefer[i];
		DOMAINSET_COPY(&all_domains, &dset->ds_mask);
		dset->ds_policy = DOMAINSET_POLICY_PREFER;
		dset->ds_prefer = i;
		_domainset_create(dset, NULL);
	}
}

/*
 * Create the domainset for cpuset 0, 1 and cpuset 2.
 */
void
domainset_zero(void)
{
	struct domainset *dset, *tmp;

	mtx_init(&cpuset_lock, "cpuset", NULL, MTX_SPIN | MTX_RECURSE);

	dset = &domainset0;
	DOMAINSET_COPY(&all_domains, &dset->ds_mask);
	dset->ds_policy = DOMAINSET_POLICY_FIRSTTOUCH;
	dset->ds_prefer = -1;
	curthread->td_domain.dr_policy = _domainset_create(dset, NULL);

	domainset_copy(dset, &domainset2);
	domainset2.ds_policy = DOMAINSET_POLICY_INTERLEAVE;
	kernel_object->domain.dr_policy = _domainset_create(&domainset2, NULL);

	/* Remove empty domains from the global policies. */
	LIST_FOREACH_SAFE(dset, &cpuset_domains, ds_link, tmp)
		if (domainset_empty_vm(dset))
			LIST_REMOVE(dset, ds_link);
}

/*
 * Creates system-wide cpusets and the cpuset for thread0 including three
 * sets:
 * 
 * 0 - The root set which should represent all valid processors in the
 *     system.  It is initially created with a mask of all processors
 *     because we don't know what processors are valid until cpuset_init()
 *     runs.  This set is immutable.
 * 1 - The default set which all processes are a member of until changed.
 *     This allows an administrator to move all threads off of given cpus to
 *     dedicate them to high priority tasks or save power etc.
 * 2 - The kernel set which allows restriction and policy to be applied only
 *     to kernel threads and the kernel_object.
 */
struct cpuset *
cpuset_thread0(void)
{
	struct cpuset *set;
	int i;
	int error __unused;

	cpuset_zone = uma_zcreate("cpuset", sizeof(struct cpuset), NULL, NULL,
	    NULL, NULL, UMA_ALIGN_CACHE, 0);
	domainset_zone = uma_zcreate("domainset", sizeof(struct domainset),
	    NULL, NULL, NULL, NULL, UMA_ALIGN_CACHE, 0);

	/*
	 * Create the root system set (0) for the whole machine.  Doesn't use
	 * cpuset_create() due to NULL parent.
	 */
	set = uma_zalloc(cpuset_zone, M_WAITOK | M_ZERO);
	CPU_COPY(&all_cpus, &set->cs_mask);
	LIST_INIT(&set->cs_children);
	LIST_INSERT_HEAD(&cpuset_ids, set, cs_link);
	set->cs_ref = 1;
	set->cs_flags = CPU_SET_ROOT | CPU_SET_RDONLY;
	set->cs_domain = &domainset0;
	cpuset_zero = set;
	cpuset_root = &set->cs_mask;

	/*
	 * Now derive a default (1), modifiable set from that to give out.
	 */
	set = uma_zalloc(cpuset_zone, M_WAITOK | M_ZERO);
	error = _cpuset_create(set, cpuset_zero, NULL, NULL, 1);
	KASSERT(error == 0, ("Error creating default set: %d\n", error));
	cpuset_default = set;
	/*
	 * Create the kernel set (2).
	 */
	set = uma_zalloc(cpuset_zone, M_WAITOK | M_ZERO);
	error = _cpuset_create(set, cpuset_zero, NULL, NULL, 2);
	KASSERT(error == 0, ("Error creating kernel set: %d\n", error));
	set->cs_domain = &domainset2;
	cpuset_kernel = set;

	/*
	 * Initialize the unit allocator. 0 and 1 are allocated above.
	 */
	cpuset_unr = new_unrhdr(2, INT_MAX, NULL);

	/*
	 * If MD code has not initialized per-domain cpusets, place all
	 * CPUs in domain 0.
	 */
	for (i = 0; i < MAXMEMDOM; i++)
		if (!CPU_EMPTY(&cpuset_domain[i]))
			goto domains_set;
	CPU_COPY(&all_cpus, &cpuset_domain[0]);
domains_set:

	return (cpuset_default);
}

void
cpuset_kernthread(struct thread *td)
{
	struct cpuset *set;

	thread_lock(td);
	set = td->td_cpuset;
	td->td_cpuset = cpuset_ref(cpuset_kernel);
	thread_unlock(td);
	cpuset_rel(set);
}

/*
 * Create a cpuset, which would be cpuset_create() but
 * mark the new 'set' as root.
 *
 * We are not going to reparent the td to it.  Use cpuset_setproc_update_set()
 * for that.
 *
 * In case of no error, returns the set in *setp locked with a reference.
 */
int
cpuset_create_root(struct prison *pr, struct cpuset **setp)
{
	struct cpuset *set;
	int error;

	KASSERT(pr != NULL, ("[%s:%d] invalid pr", __func__, __LINE__));
	KASSERT(setp != NULL, ("[%s:%d] invalid setp", __func__, __LINE__));

	error = cpuset_create(setp, pr->pr_cpuset, &pr->pr_cpuset->cs_mask);
	if (error)
		return (error);

	KASSERT(*setp != NULL, ("[%s:%d] cpuset_create returned invalid data",
	    __func__, __LINE__));

	/* Mark the set as root. */
	set = *setp;
	set->cs_flags |= CPU_SET_ROOT;

	return (0);
}

int
cpuset_setproc_update_set(struct proc *p, struct cpuset *set)
{
	int error;

	KASSERT(p != NULL, ("[%s:%d] invalid proc", __func__, __LINE__));
	KASSERT(set != NULL, ("[%s:%d] invalid set", __func__, __LINE__));

	cpuset_ref(set);
	error = cpuset_setproc(p->p_pid, set, NULL, NULL);
	if (error)
		return (error);
	cpuset_rel(set);
	return (0);
}

#ifndef _SYS_SYSPROTO_H_
struct cpuset_args {
	cpusetid_t	*setid;
};
#endif
int
sys_cpuset(struct thread *td, struct cpuset_args *uap)
{
	struct cpuset *root;
	struct cpuset *set;
	int error;

	thread_lock(td);
	root = cpuset_refroot(td->td_cpuset);
	thread_unlock(td);
	error = cpuset_create(&set, root, &root->cs_mask);
	cpuset_rel(root);
	if (error)
		return (error);
	error = copyout(&set->cs_id, uap->setid, sizeof(set->cs_id));
	if (error == 0)
		error = cpuset_setproc(-1, set, NULL, NULL);
	cpuset_rel(set);
	return (error);
}

#ifndef _SYS_SYSPROTO_H_
struct cpuset_setid_args {
	cpuwhich_t	which;
	id_t		id;
	cpusetid_t	setid;
};
#endif
int
sys_cpuset_setid(struct thread *td, struct cpuset_setid_args *uap)
{

	return (kern_cpuset_setid(td, uap->which, uap->id, uap->setid));
}

int
kern_cpuset_setid(struct thread *td, cpuwhich_t which,
    id_t id, cpusetid_t setid)
{
	struct cpuset *set;
	int error;

	/*
	 * Presently we only support per-process sets.
	 */
	if (which != CPU_WHICH_PID)
		return (EINVAL);
	set = cpuset_lookup(setid, td);
	if (set == NULL)
		return (ESRCH);
	error = cpuset_setproc(id, set, NULL, NULL);
	cpuset_rel(set);
	return (error);
}

#ifndef _SYS_SYSPROTO_H_
struct cpuset_getid_args {
	cpulevel_t	level;
	cpuwhich_t	which;
	id_t		id;
	cpusetid_t	*setid;
};
#endif
int
sys_cpuset_getid(struct thread *td, struct cpuset_getid_args *uap)
{

	return (kern_cpuset_getid(td, uap->level, uap->which, uap->id,
	    uap->setid));
}

int
kern_cpuset_getid(struct thread *td, cpulevel_t level, cpuwhich_t which,
    id_t id, cpusetid_t *setid)
{
	struct cpuset *nset;
	struct cpuset *set;
	struct thread *ttd;
	struct proc *p;
	cpusetid_t tmpid;
	int error;

	if (level == CPU_LEVEL_WHICH && which != CPU_WHICH_CPUSET)
		return (EINVAL);
	error = cpuset_which(which, id, &p, &ttd, &set);
	if (error)
		return (error);
	switch (which) {
	case CPU_WHICH_TID:
	case CPU_WHICH_PID:
		thread_lock(ttd);
		set = cpuset_refbase(ttd->td_cpuset);
		thread_unlock(ttd);
		PROC_UNLOCK(p);
		break;
	case CPU_WHICH_CPUSET:
	case CPU_WHICH_JAIL:
		break;
	case CPU_WHICH_IRQ:
	case CPU_WHICH_DOMAIN:
		return (EINVAL);
	}
	switch (level) {
	case CPU_LEVEL_ROOT:
		nset = cpuset_refroot(set);
		cpuset_rel(set);
		set = nset;
		break;
	case CPU_LEVEL_CPUSET:
		break;
	case CPU_LEVEL_WHICH:
		break;
	}
	tmpid = set->cs_id;
	cpuset_rel(set);
	if (error == 0)
		error = copyout(&tmpid, setid, sizeof(tmpid));

	return (error);
}

#ifndef _SYS_SYSPROTO_H_
struct cpuset_getaffinity_args {
	cpulevel_t	level;
	cpuwhich_t	which;
	id_t		id;
	size_t		cpusetsize;
	cpuset_t	*mask;
};
#endif
int
sys_cpuset_getaffinity(struct thread *td, struct cpuset_getaffinity_args *uap)
{

	return (kern_cpuset_getaffinity(td, uap->level, uap->which,
	    uap->id, uap->cpusetsize, uap->mask));
}

int
kern_cpuset_getaffinity(struct thread *td, cpulevel_t level, cpuwhich_t which,
    id_t id, size_t cpusetsize, cpuset_t *maskp)
{
	struct thread *ttd;
	struct cpuset *nset;
	struct cpuset *set;
	struct proc *p;
	cpuset_t *mask;
	int error;
	size_t size;

	if (cpusetsize < sizeof(cpuset_t) || cpusetsize > CPU_MAXSIZE / NBBY)
		return (ERANGE);
	/* In Capability mode, you can only get your own CPU set. */
	if (IN_CAPABILITY_MODE(td)) {
		if (level != CPU_LEVEL_WHICH)
			return (ECAPMODE);
		if (which != CPU_WHICH_TID && which != CPU_WHICH_PID)
			return (ECAPMODE);
		if (id != -1)
			return (ECAPMODE);
	}
	size = cpusetsize;
	mask = malloc(size, M_TEMP, M_WAITOK | M_ZERO);
	error = cpuset_which(which, id, &p, &ttd, &set);
	if (error)
		goto out;
	switch (level) {
	case CPU_LEVEL_ROOT:
	case CPU_LEVEL_CPUSET:
		switch (which) {
		case CPU_WHICH_TID:
		case CPU_WHICH_PID:
			thread_lock(ttd);
			set = cpuset_ref(ttd->td_cpuset);
			thread_unlock(ttd);
			break;
		case CPU_WHICH_CPUSET:
		case CPU_WHICH_JAIL:
			break;
		case CPU_WHICH_IRQ:
		case CPU_WHICH_INTRHANDLER:
		case CPU_WHICH_ITHREAD:
		case CPU_WHICH_DOMAIN:
			error = EINVAL;
			goto out;
		}
		if (level == CPU_LEVEL_ROOT)
			nset = cpuset_refroot(set);
		else
			nset = cpuset_refbase(set);
		CPU_COPY(&nset->cs_mask, mask);
		cpuset_rel(nset);
		break;
	case CPU_LEVEL_WHICH:
		switch (which) {
		case CPU_WHICH_TID:
			thread_lock(ttd);
			CPU_COPY(&ttd->td_cpuset->cs_mask, mask);
			thread_unlock(ttd);
			break;
		case CPU_WHICH_PID:
			FOREACH_THREAD_IN_PROC(p, ttd) {
				thread_lock(ttd);
				CPU_OR(mask, &ttd->td_cpuset->cs_mask);
				thread_unlock(ttd);
			}
			break;
		case CPU_WHICH_CPUSET:
		case CPU_WHICH_JAIL:
			CPU_COPY(&set->cs_mask, mask);
			break;
		case CPU_WHICH_IRQ:
		case CPU_WHICH_INTRHANDLER:
		case CPU_WHICH_ITHREAD:
			error = intr_getaffinity(id, which, mask);
			break;
		case CPU_WHICH_DOMAIN:
			if (id < 0 || id >= MAXMEMDOM)
				error = ESRCH;
			else
				CPU_COPY(&cpuset_domain[id], mask);
			break;
		}
		break;
	default:
		error = EINVAL;
		break;
	}
	if (set)
		cpuset_rel(set);
	if (p)
		PROC_UNLOCK(p);
	if (error == 0)
		error = copyout(mask, maskp, size);
out:
	free(mask, M_TEMP);
	return (error);
}

#ifndef _SYS_SYSPROTO_H_
struct cpuset_setaffinity_args {
	cpulevel_t	level;
	cpuwhich_t	which;
	id_t		id;
	size_t		cpusetsize;
	const cpuset_t	*mask;
};
#endif
int
sys_cpuset_setaffinity(struct thread *td, struct cpuset_setaffinity_args *uap)
{

	return (kern_cpuset_setaffinity(td, uap->level, uap->which,
	    uap->id, uap->cpusetsize, uap->mask));
}

int
kern_cpuset_setaffinity(struct thread *td, cpulevel_t level, cpuwhich_t which,
    id_t id, size_t cpusetsize, const cpuset_t *maskp)
{
	struct cpuset *nset;
	struct cpuset *set;
	struct thread *ttd;
	struct proc *p;
	cpuset_t *mask;
	int error;

	if (cpusetsize < sizeof(cpuset_t) || cpusetsize > CPU_MAXSIZE / NBBY)
		return (ERANGE);
	/* In Capability mode, you can only set your own CPU set. */
	if (IN_CAPABILITY_MODE(td)) {
		if (level != CPU_LEVEL_WHICH)
			return (ECAPMODE);
		if (which != CPU_WHICH_TID && which != CPU_WHICH_PID)
			return (ECAPMODE);
		if (id != -1)
			return (ECAPMODE);
	}
	mask = malloc(cpusetsize, M_TEMP, M_WAITOK | M_ZERO);
	error = copyin(maskp, mask, cpusetsize);
	if (error)
		goto out;
	/*
	 * Verify that no high bits are set.
	 */
	if (cpusetsize > sizeof(cpuset_t)) {
		char *end;
		char *cp;

		end = cp = (char *)&mask->__bits;
		end += cpusetsize;
		cp += sizeof(cpuset_t);
		while (cp != end)
			if (*cp++ != 0) {
				error = EINVAL;
				goto out;
			}

	}
	switch (level) {
	case CPU_LEVEL_ROOT:
	case CPU_LEVEL_CPUSET:
		error = cpuset_which(which, id, &p, &ttd, &set);
		if (error)
			break;
		switch (which) {
		case CPU_WHICH_TID:
		case CPU_WHICH_PID:
			thread_lock(ttd);
			set = cpuset_ref(ttd->td_cpuset);
			thread_unlock(ttd);
			PROC_UNLOCK(p);
			break;
		case CPU_WHICH_CPUSET:
		case CPU_WHICH_JAIL:
			break;
		case CPU_WHICH_IRQ:
		case CPU_WHICH_INTRHANDLER:
		case CPU_WHICH_ITHREAD:
		case CPU_WHICH_DOMAIN:
			error = EINVAL;
			goto out;
		}
		if (level == CPU_LEVEL_ROOT)
			nset = cpuset_refroot(set);
		else
			nset = cpuset_refbase(set);
		error = cpuset_modify(nset, mask);
		cpuset_rel(nset);
		cpuset_rel(set);
		break;
	case CPU_LEVEL_WHICH:
		switch (which) {
		case CPU_WHICH_TID:
			error = cpuset_setthread(id, mask);
			break;
		case CPU_WHICH_PID:
			error = cpuset_setproc(id, NULL, mask, NULL);
			break;
		case CPU_WHICH_CPUSET:
		case CPU_WHICH_JAIL:
			error = cpuset_which(which, id, &p, &ttd, &set);
			if (error == 0) {
				error = cpuset_modify(set, mask);
				cpuset_rel(set);
			}
			break;
		case CPU_WHICH_IRQ:
		case CPU_WHICH_INTRHANDLER:
		case CPU_WHICH_ITHREAD:
			error = intr_setaffinity(id, which, mask);
			break;
		default:
			error = EINVAL;
			break;
		}
		break;
	default:
		error = EINVAL;
		break;
	}
out:
	free(mask, M_TEMP);
	return (error);
}

#ifndef _SYS_SYSPROTO_H_
struct cpuset_getdomain_args {
	cpulevel_t	level;
	cpuwhich_t	which;
	id_t		id;
	size_t		domainsetsize;
	domainset_t	*mask;
	int 		*policy;
};
#endif
int
sys_cpuset_getdomain(struct thread *td, struct cpuset_getdomain_args *uap)
{

	return (kern_cpuset_getdomain(td, uap->level, uap->which,
	    uap->id, uap->domainsetsize, uap->mask, uap->policy));
}

int
kern_cpuset_getdomain(struct thread *td, cpulevel_t level, cpuwhich_t which,
    id_t id, size_t domainsetsize, domainset_t *maskp, int *policyp)
{
	struct domainset outset;
	struct thread *ttd;
	struct cpuset *nset;
	struct cpuset *set;
	struct domainset *dset;
	struct proc *p;
	domainset_t *mask;
	int error;

	if (domainsetsize < sizeof(domainset_t) ||
	    domainsetsize > DOMAINSET_MAXSIZE / NBBY)
		return (ERANGE);
	/* In Capability mode, you can only get your own domain set. */
	if (IN_CAPABILITY_MODE(td)) {
		if (level != CPU_LEVEL_WHICH)
			return (ECAPMODE);
		if (which != CPU_WHICH_TID && which != CPU_WHICH_PID)
			return (ECAPMODE);
		if (id != -1)
			return (ECAPMODE);
	}
	mask = malloc(domainsetsize, M_TEMP, M_WAITOK | M_ZERO);
	bzero(&outset, sizeof(outset));
	error = cpuset_which(which, id, &p, &ttd, &set);
	if (error)
		goto out;
	switch (level) {
	case CPU_LEVEL_ROOT:
	case CPU_LEVEL_CPUSET:
		switch (which) {
		case CPU_WHICH_TID:
		case CPU_WHICH_PID:
			thread_lock(ttd);
			set = cpuset_ref(ttd->td_cpuset);
			thread_unlock(ttd);
			break;
		case CPU_WHICH_CPUSET:
		case CPU_WHICH_JAIL:
			break;
		case CPU_WHICH_IRQ:
		case CPU_WHICH_INTRHANDLER:
		case CPU_WHICH_ITHREAD:
		case CPU_WHICH_DOMAIN:
			error = EINVAL;
			goto out;
		}
		if (level == CPU_LEVEL_ROOT)
			nset = cpuset_refroot(set);
		else
			nset = cpuset_refbase(set);
		domainset_copy(nset->cs_domain, &outset);
		cpuset_rel(nset);
		break;
	case CPU_LEVEL_WHICH:
		switch (which) {
		case CPU_WHICH_TID:
			thread_lock(ttd);
			domainset_copy(ttd->td_cpuset->cs_domain, &outset);
			thread_unlock(ttd);
			break;
		case CPU_WHICH_PID:
			FOREACH_THREAD_IN_PROC(p, ttd) {
				thread_lock(ttd);
				dset = ttd->td_cpuset->cs_domain;
				/* Show all domains in the proc. */
				DOMAINSET_OR(&outset.ds_mask, &dset->ds_mask);
				/* Last policy wins. */
				outset.ds_policy = dset->ds_policy;
				outset.ds_prefer = dset->ds_prefer;
				thread_unlock(ttd);
			}
			break;
		case CPU_WHICH_CPUSET:
		case CPU_WHICH_JAIL:
			domainset_copy(set->cs_domain, &outset);
			break;
		case CPU_WHICH_IRQ:
		case CPU_WHICH_INTRHANDLER:
		case CPU_WHICH_ITHREAD:
		case CPU_WHICH_DOMAIN:
			error = EINVAL;
			break;
		}
		break;
	default:
		error = EINVAL;
		break;
	}
	if (set)
		cpuset_rel(set);
	if (p)
		PROC_UNLOCK(p);
	/*
	 * Translate prefer into a set containing only the preferred domain,
	 * not the entire fallback set.
	 */
	if (outset.ds_policy == DOMAINSET_POLICY_PREFER) {
		DOMAINSET_ZERO(&outset.ds_mask);
		DOMAINSET_SET(outset.ds_prefer, &outset.ds_mask);
	}
	DOMAINSET_COPY(&outset.ds_mask, mask);
	if (error == 0)
		error = copyout(mask, maskp, domainsetsize);
	if (error == 0)
		if (suword32(policyp, outset.ds_policy) != 0)
			error = EFAULT;
out:
	free(mask, M_TEMP);
	return (error);
}

#ifndef _SYS_SYSPROTO_H_
struct cpuset_setdomain_args {
	cpulevel_t	level;
	cpuwhich_t	which;
	id_t		id;
	size_t		domainsetsize;
	domainset_t	*mask;
	int 		policy;
};
#endif
int
sys_cpuset_setdomain(struct thread *td, struct cpuset_setdomain_args *uap)
{

	return (kern_cpuset_setdomain(td, uap->level, uap->which,
	    uap->id, uap->domainsetsize, uap->mask, uap->policy));
}

int
kern_cpuset_setdomain(struct thread *td, cpulevel_t level, cpuwhich_t which,
    id_t id, size_t domainsetsize, const domainset_t *maskp, int policy)
{
	struct cpuset *nset;
	struct cpuset *set;
	struct thread *ttd;
	struct proc *p;
	struct domainset domain;
	domainset_t *mask;
	int error;

	if (domainsetsize < sizeof(domainset_t) ||
	    domainsetsize > DOMAINSET_MAXSIZE / NBBY)
		return (ERANGE);
	if (policy <= DOMAINSET_POLICY_INVALID ||
	    policy > DOMAINSET_POLICY_MAX)
		return (EINVAL);
	/* In Capability mode, you can only set your own CPU set. */
	if (IN_CAPABILITY_MODE(td)) {
		if (level != CPU_LEVEL_WHICH)
			return (ECAPMODE);
		if (which != CPU_WHICH_TID && which != CPU_WHICH_PID)
			return (ECAPMODE);
		if (id != -1)
			return (ECAPMODE);
	}
	memset(&domain, 0, sizeof(domain));
	mask = malloc(domainsetsize, M_TEMP, M_WAITOK | M_ZERO);
	error = copyin(maskp, mask, domainsetsize);
	if (error)
		goto out;
	/*
	 * Verify that no high bits are set.
	 */
	if (domainsetsize > sizeof(domainset_t)) {
		char *end;
		char *cp;

		end = cp = (char *)&mask->__bits;
		end += domainsetsize;
		cp += sizeof(domainset_t);
		while (cp != end)
			if (*cp++ != 0) {
				error = EINVAL;
				goto out;
			}

	}
	DOMAINSET_COPY(mask, &domain.ds_mask);
	domain.ds_policy = policy;

	/* Translate preferred policy into a mask and fallback. */
	if (policy == DOMAINSET_POLICY_PREFER) {
		/* Only support a single preferred domain. */
		if (DOMAINSET_COUNT(&domain.ds_mask) != 1) {
			error = EINVAL;
			goto out;
		}
		domain.ds_prefer = DOMAINSET_FFS(&domain.ds_mask) - 1;
		/* This will be constrained by domainset_shadow(). */
		DOMAINSET_FILL(&domain.ds_mask);
	}

	/*
	 *  When given an impossible policy, fall back to interleaving
	 *  across all domains
	 */
	if (domainset_empty_vm(&domain))
		domainset_copy(&domainset2, &domain);

	switch (level) {
	case CPU_LEVEL_ROOT:
	case CPU_LEVEL_CPUSET:
		error = cpuset_which(which, id, &p, &ttd, &set);
		if (error)
			break;
		switch (which) {
		case CPU_WHICH_TID:
		case CPU_WHICH_PID:
			thread_lock(ttd);
			set = cpuset_ref(ttd->td_cpuset);
			thread_unlock(ttd);
			PROC_UNLOCK(p);
			break;
		case CPU_WHICH_CPUSET:
		case CPU_WHICH_JAIL:
			break;
		case CPU_WHICH_IRQ:
		case CPU_WHICH_INTRHANDLER:
		case CPU_WHICH_ITHREAD:
		case CPU_WHICH_DOMAIN:
			error = EINVAL;
			goto out;
		}
		if (level == CPU_LEVEL_ROOT)
			nset = cpuset_refroot(set);
		else
			nset = cpuset_refbase(set);
		error = cpuset_modify_domain(nset, &domain);
		cpuset_rel(nset);
		cpuset_rel(set);
		break;
	case CPU_LEVEL_WHICH:
		switch (which) {
		case CPU_WHICH_TID:
			error = _cpuset_setthread(id, NULL, &domain);
			break;
		case CPU_WHICH_PID:
			error = cpuset_setproc(id, NULL, NULL, &domain);
			break;
		case CPU_WHICH_CPUSET:
		case CPU_WHICH_JAIL:
			error = cpuset_which(which, id, &p, &ttd, &set);
			if (error == 0) {
				error = cpuset_modify_domain(set, &domain);
				cpuset_rel(set);
			}
			break;
		case CPU_WHICH_IRQ:
		case CPU_WHICH_INTRHANDLER:
		case CPU_WHICH_ITHREAD:
		default:
			error = EINVAL;
			break;
		}
		break;
	default:
		error = EINVAL;
		break;
	}
out:
	free(mask, M_TEMP);
	return (error);
}

#ifdef DDB

static void
ddb_display_bitset(const struct bitset *set, int size)
{
	int bit, once;

	for (once = 0, bit = 0; bit < size; bit++) {
		if (CPU_ISSET(bit, set)) {
			if (once == 0) {
				db_printf("%d", bit);
				once = 1;
			} else  
				db_printf(",%d", bit);
		}
	}
	if (once == 0)
		db_printf("<none>");
}

void
ddb_display_cpuset(const cpuset_t *set)
{
	ddb_display_bitset((const struct bitset *)set, CPU_SETSIZE);
}

static void
ddb_display_domainset(const domainset_t *set)
{
	ddb_display_bitset((const struct bitset *)set, DOMAINSET_SETSIZE);
}

DB_SHOW_COMMAND(cpusets, db_show_cpusets)
{
	struct cpuset *set;

	LIST_FOREACH(set, &cpuset_ids, cs_link) {
		db_printf("set=%p id=%-6u ref=%-6d flags=0x%04x parent id=%d\n",
		    set, set->cs_id, set->cs_ref, set->cs_flags,
		    (set->cs_parent != NULL) ? set->cs_parent->cs_id : 0);
		db_printf("  cpu mask=");
		ddb_display_cpuset(&set->cs_mask);
		db_printf("\n");
		db_printf("  domain policy %d prefer %d mask=",
		    set->cs_domain->ds_policy, set->cs_domain->ds_prefer);
		ddb_display_domainset(&set->cs_domain->ds_mask);
		db_printf("\n");
		if (db_pager_quit)
			break;
	}
}

DB_SHOW_COMMAND(domainsets, db_show_domainsets)
{
	struct domainset *set;

	LIST_FOREACH(set, &cpuset_domains, ds_link) {
		db_printf("set=%p policy %d prefer %d cnt %d\n",
		    set, set->ds_policy, set->ds_prefer, set->ds_cnt);
		db_printf("  mask =");
		ddb_display_domainset(&set->ds_mask);
		db_printf("\n");
	}
}
#endif /* DDB */
