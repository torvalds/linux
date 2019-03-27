/*-
 * Copyright (c) 2016, 2018 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by BAE Systems, the University of Cambridge
 * Computer Laboratory, and Memorial University under DARPA/AFRL contract
 * FA8650-15-C-7558 ("CADETS"), as part of the DARPA Transparent Computing
 * (TC) research program.
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

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/ctype.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/queue.h>
#include <sys/refcount.h>

#include <sys/dtrace.h>
#include <sys/dtrace_bsd.h>

#include <bsm/audit.h>
#include <bsm/audit_internal.h>
#include <bsm/audit_kevents.h>

#include <security/audit/audit.h>
#include <security/audit/audit_private.h>

/*-
 * Audit DTrace provider: allow DTrace to request that audit records be
 * generated for various audit events, and then expose those records (in
 * various forms) to probes.  The model is that each event type has two
 * probes, which use the event's name to create the probe:
 *
 * - "commit" passes the kernel-internal (unserialised) kaudit_record
 *   synchronously (from the originating thread) of the record as we prepare
 *   to "commit" the record to the audit queue.
 *
 * - "bsm" also passes generated BSM, and executes asynchronously in the audit
 *   worker thread, once it has been extracted from the audit queue.  This is
 *   the point at which an audit record would be enqueued to the trail on
 *   disk, or to pipes.
 *
 * These probes support very different goals.  The former executes in the
 * thread originating the record, making it easier to correlate other DTrace
 * probe activity with the event described in the record.  The latter gives
 * access to BSM-formatted events (at a cost) allowing DTrace to extract BSM
 * directly an alternative mechanism to the formal audit trail and audit
 * pipes.
 *
 * To generate names for numeric event IDs, userspace will push the contents
 * of /etc/security/audit_event into the kernel during audit setup, much as it
 * does /etc/security/audit_class.  We then create the probes for each of
 * those mappings.  If one (or both) of the probes are enabled, then we cause
 * a record to be generated (as both normal audit preselection and audit pipes
 * do), and catch it on the way out during commit.  There are suitable hook
 * functions in the audit code that this provider can register to catch
 * various events in the audit-record life cycle.
 *
 * Further ponderings:
 *
 * - How do we want to handle events for which there are not names -- perhaps
 *   a catch-all probe for those events without mappings?
 *
 * - Should the evname code really be present even if DTrace isn't loaded...?
 *   Right now, we arrange that it is so that userspace can usefully maintain
 *   the list in case DTrace is later loaded (and to prevent userspace
 *   confusion).
 *
 * - Should we add an additional set of audit:class::commit probes that use
 *   event class names to match broader categories of events as specified in
 *   /etc/security/event_class?
 *
 * - If we pursue that last point, we will want to pass the name of the event
 *   into the probe explicitly (e.g., as arg0), since it would no longer be
 *   available as the probe function name.
 */

static int	dtaudit_unload(void);
static void	dtaudit_getargdesc(void *, dtrace_id_t, void *,
		    dtrace_argdesc_t *);
static void	dtaudit_provide(void *, dtrace_probedesc_t *);
static void	dtaudit_destroy(void *, dtrace_id_t, void *);
static void	dtaudit_enable(void *, dtrace_id_t, void *);
static void	dtaudit_disable(void *, dtrace_id_t, void *);
static void	dtaudit_load(void *);

static dtrace_pattr_t dtaudit_attr = {
{ DTRACE_STABILITY_EVOLVING, DTRACE_STABILITY_EVOLVING, DTRACE_CLASS_COMMON },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_EVOLVING, DTRACE_STABILITY_EVOLVING, DTRACE_CLASS_COMMON },
{ DTRACE_STABILITY_EVOLVING, DTRACE_STABILITY_EVOLVING, DTRACE_CLASS_COMMON },
};

/*
 * Strings for the "module" and "name" portions of the probe.  The name of the
 * audit event will be the "function" portion of the probe.  All dtaudit
 * probes therefore take the form audit:event:<event name>:commit.
 */
static char	*dtaudit_module_str = "event";
static char	*dtaudit_name_commit_str = "commit";
static char	*dtaudit_name_bsm_str = "bsm";

static dtrace_pops_t dtaudit_pops = {
	.dtps_provide =		dtaudit_provide,
	.dtps_provide_module =	NULL,
	.dtps_enable =		dtaudit_enable,
	.dtps_disable =		dtaudit_disable,
	.dtps_suspend =		NULL,
	.dtps_resume =		NULL,
	.dtps_getargdesc =	dtaudit_getargdesc,
	.dtps_getargval =	NULL,
	.dtps_usermode =	NULL,
	.dtps_destroy =		dtaudit_destroy
};

static dtrace_provider_id_t	dtaudit_id;

/*
 * Because looking up entries in the event-to-name mapping is quite expensive,
 * maintain a global flag tracking whether any dtaudit probes are enabled.  If
 * not, don't bother doing all that work whenever potential queries about
 * events turn up during preselection or commit.
 *
 * NB: We used to maintain our own variable in dtaudit, but now use the
 * centralized audit_dtrace_enabled variable imported from the audit code.
 *
 * static uint_t		dtaudit_probes_enabled;
 */

/*
 * Check dtaudit policy for the event to see whether this is an event we would
 * like to preselect (i.e., cause an audit record to be generated for).  To
 * minimise probe effect when not used at all, we not only check for the probe
 * on the individual event, but also a global flag indicating that at least
 * one probe is enabled, before acquiring locks, searching lists, etc.
 *
 * If the event is selected, return an evname_elem reference to be stored in
 * the audit record, which we can use later to avoid further lookups.  The
 * contents of the evname_elem must be sufficiently stable so as to not risk
 * race conditions here.
 *
 * Currently, we take an interest only in the 'event' argument, but in the
 * future might want to support other types of record selection tied to
 * additional probe types (e.g., event clases).
 *
 * XXXRW: Should we have a catch-all probe here for events without registered
 * names?
 */
static void *
dtaudit_preselect(au_id_t auid, au_event_t event, au_class_t class)
{
	struct evname_elem *ene;
	int probe_enabled;

	/*
	 * NB: Lockless reads here may return a slightly stale value; this is
	 * considered better than acquiring a lock, however.
	 */
	if (!audit_dtrace_enabled)
		return (NULL);
	ene = au_evnamemap_lookup(event);
	if (ene == NULL)
		return (NULL);

	/*
	 * See if either of the two probes for the audit event are enabled.
	 *
	 * NB: Lock also not acquired here -- but perhaps it wouldn't matter
	 * given that we've already used the list lock above?
	 *
	 * XXXRW: Alternatively, au_evnamemap_lookup() could return these
	 * values while holding the list lock...?
	 */
	probe_enabled = ene->ene_commit_probe_enabled ||
	    ene->ene_bsm_probe_enabled;
	if (!probe_enabled)
		return (NULL);
	return ((void *)ene);
}

/*
 * Commit probe pre-BSM.  Fires the probe but also checks to see if we should
 * ask the audit framework to call us again with BSM arguments in the audit
 * worker thread.
 *
 * XXXRW: Should we have a catch-all probe here for events without registered
 * names?
 */
static int
dtaudit_commit(struct kaudit_record *kar, au_id_t auid, au_event_t event,
    au_class_t class, int sorf)
{
	char ene_name_lower[EVNAMEMAP_NAME_SIZE];
	struct evname_elem *ene;
	int i;

	ene = (struct evname_elem *)kar->k_dtaudit_state;
	if (ene == NULL)
		return (0);

	/*
	 * Process a possibly registered commit probe.
	 */
	if (ene->ene_commit_probe_enabled) {
		/*
		 * XXXRW: Lock ene to provide stability to the name string.  A
		 * bit undesirable!  We may want another locking strategy
		 * here.  At least we don't run the DTrace probe under the
		 * lock.
		 *
		 * XXXRW: We provide the struct audit_record pointer -- but
		 * perhaps should provide the kaudit_record pointer?
		 */
		EVNAME_LOCK(ene);
		for (i = 0; i < sizeof(ene_name_lower); i++)
			ene_name_lower[i] = tolower(ene->ene_name[i]);
		EVNAME_UNLOCK(ene);
		dtrace_probe(ene->ene_commit_probe_id,
		    (uintptr_t)ene_name_lower, (uintptr_t)&kar->k_ar, 0, 0, 0);
	}

	/*
	 * Return the state of the BSM probe to the caller.
	 */
	return (ene->ene_bsm_probe_enabled);
}

/*
 * Commit probe post-BSM.
 *
 * XXXRW: Should we have a catch-all probe here for events without registered
 * names?
 */
static void
dtaudit_bsm(struct kaudit_record *kar, au_id_t auid, au_event_t event,
    au_class_t class, int sorf, void *bsm_data, size_t bsm_len)
{
	char ene_name_lower[EVNAMEMAP_NAME_SIZE];
	struct evname_elem *ene;
	int i;

	ene = (struct evname_elem *)kar->k_dtaudit_state;
	if (ene == NULL)
		return;
	if (!(ene->ene_bsm_probe_enabled))
		return;

	/*
	 * XXXRW: Lock ene to provide stability to the name string.  A bit
	 * undesirable!  We may want another locking strategy here.  At least
	 * we don't run the DTrace probe under the lock.
	 *
	 * XXXRW: We provide the struct audit_record pointer -- but perhaps
	 * should provide the kaudit_record pointer?
	 */
	EVNAME_LOCK(ene);
	for (i = 0; i < sizeof(ene_name_lower); i++)
		ene_name_lower[i] = tolower(ene->ene_name[i]);
	EVNAME_UNLOCK(ene);
	dtrace_probe(ene->ene_bsm_probe_id, (uintptr_t)ene_name_lower,
	    (uintptr_t)&kar->k_ar, (uintptr_t)bsm_data, (uintptr_t)bsm_len,
	    0);
}

/*
 * A very simple provider: argument types are identical across all probes: the
 * kaudit_record, plus a BSM pointer and length.
 */
static void
dtaudit_getargdesc(void *arg, dtrace_id_t id, void *parg,
    dtrace_argdesc_t *desc)
{
	struct evname_elem *ene;
	const char *p;

	ene = (struct evname_elem *)parg;
	p = NULL;
	switch (desc->dtargd_ndx) {
	case 0:
		/* Audit event name. */
		p = "char *";
		break;

	case 1:
		/* In-kernel audit record. */
		p = "struct audit_record *";
		break;

	case 2:
		/* BSM data, if present. */
		if (id == ene->ene_bsm_probe_id)
			p = "const void *";
		else
			desc->dtargd_ndx = DTRACE_ARGNONE;
		break;

	case 3:
		/* BSM length, if present. */
		if (id == ene->ene_bsm_probe_id)
			p = "size_t";
		else
			desc->dtargd_ndx = DTRACE_ARGNONE;
		break;

	default:
		desc->dtargd_ndx = DTRACE_ARGNONE;
		break;
	}
	if (p != NULL)
		strlcpy(desc->dtargd_native, p, sizeof(desc->dtargd_native));
}

/*
 * Callback from the event-to-name mapping code when performing
 * evname_foreach().  Note that we may update the entry, so the foreach code
 * must have a write lock.  However, as the synchronisation model is private
 * to the evname code, we cannot easily assert it here.
 *
 * XXXRW: How do we want to handle event rename / collision issues here --
 * e.g., if userspace was using a name to point to one event number, and then
 * changes it so that the name points at another?  For now, paper over this by
 * skipping event numbers that are already registered, and likewise skipping
 * names that are already registered.  However, this could lead to confusing
 * behaviour so possibly needs to be resolved in the longer term.
 */
static void
dtaudit_au_evnamemap_callback(struct evname_elem *ene)
{
	char ene_name_lower[EVNAMEMAP_NAME_SIZE];
	int i;

	/*
	 * DTrace, by convention, has lower-case probe names.  However, the
	 * in-kernel event-to-name mapping table must maintain event-name case
	 * as submitted by userspace.  Create a temporary lower-case version
	 * here, away from the fast path, to use when exposing the event name
	 * to DTrace as part of the name of a probe.
	 *
	 * NB: Convert the entire array, including the terminating nul,
	 * because these strings are short and it's more work not to.  If they
	 * become long, we might feel more guilty about this sloppiness!
	 */
	for (i = 0; i < sizeof(ene_name_lower); i++)
		ene_name_lower[i] = tolower(ene->ene_name[i]);

	/*
	 * Don't register a new probe if this event number already has an
	 * associated commit probe -- or if another event has already
	 * registered this name.
	 *
	 * XXXRW: There is an argument that if multiple numeric events match
	 * a single name, they should all be exposed to the same named probe.
	 * In particular, we should perhaps use a probe ID returned by this
	 * lookup and just stick that in the saved probe ID?
	 */
	if ((ene->ene_commit_probe_id == 0) &&
	    (dtrace_probe_lookup(dtaudit_id, dtaudit_module_str,
	    ene_name_lower, dtaudit_name_commit_str) == 0)) {

		/*
		 * Create the commit probe.
		 *
		 * NB: We don't declare any extra stack frames because stack()
		 * will just return the path to the audit commit code, which
		 * is not really interesting anyway.
		 *
		 * We pass in the pointer to the evnam_elem entry so that we
		 * can easily change its enabled flag in the probe
		 * enable/disable interface.
		 */
		ene->ene_commit_probe_id = dtrace_probe_create(dtaudit_id,
		    dtaudit_module_str, ene_name_lower,
		    dtaudit_name_commit_str, 0, ene);
	}

	/*
	 * Don't register a new probe if this event number already has an
	 * associated bsm probe -- or if another event has already
	 * registered this name.
	 *
	 * XXXRW: There is an argument that if multiple numeric events match
	 * a single name, they should all be exposed to the same named probe.
	 * In particular, we should perhaps use a probe ID returned by this
	 * lookup and just stick that in the saved probe ID?
	 */
	if ((ene->ene_bsm_probe_id == 0) &&
	    (dtrace_probe_lookup(dtaudit_id, dtaudit_module_str,
	    ene_name_lower, dtaudit_name_bsm_str) == 0)) {

		/*
		 * Create the bsm probe.
		 *
		 * NB: We don't declare any extra stack frames because stack()
		 * will just return the path to the audit commit code, which
		 * is not really interesting anyway.
		 *
		 * We pass in the pointer to the evnam_elem entry so that we
		 * can easily change its enabled flag in the probe
		 * enable/disable interface.
		 */
		ene->ene_bsm_probe_id = dtrace_probe_create(dtaudit_id,
		    dtaudit_module_str, ene_name_lower, dtaudit_name_bsm_str,
		    0, ene);
	}
}

static void
dtaudit_provide(void *arg, dtrace_probedesc_t *desc)
{

	/*
	 * Walk all registered number-to-name mapping entries, and ensure each
	 * is properly registered.
	 */
	au_evnamemap_foreach(dtaudit_au_evnamemap_callback);
}

static void
dtaudit_destroy(void *arg, dtrace_id_t id, void *parg)
{
}

static void
dtaudit_enable(void *arg, dtrace_id_t id, void *parg)
{
	struct evname_elem *ene;

	ene = parg;
	KASSERT(ene->ene_commit_probe_id == id || ene->ene_bsm_probe_id == id,
	    ("%s: probe ID mismatch (%u, %u != %u)", __func__,
	    ene->ene_commit_probe_id, ene->ene_bsm_probe_id, id));

	if (id == ene->ene_commit_probe_id)
		ene->ene_commit_probe_enabled = 1;
	else
		ene->ene_bsm_probe_enabled = 1;
	refcount_acquire(&audit_dtrace_enabled);
	audit_syscalls_enabled_update();
}

static void
dtaudit_disable(void *arg, dtrace_id_t id, void *parg)
{
	struct evname_elem *ene;

	ene = parg;
	KASSERT(ene->ene_commit_probe_id == id || ene->ene_bsm_probe_id == id,
	    ("%s: probe ID mismatch (%u, %u != %u)", __func__,
	    ene->ene_commit_probe_id, ene->ene_bsm_probe_id, id));

	if (id == ene->ene_commit_probe_id)
		ene->ene_commit_probe_enabled = 0;
	else
		ene->ene_bsm_probe_enabled = 0;
	(void)refcount_release(&audit_dtrace_enabled);
	audit_syscalls_enabled_update();
}

static void
dtaudit_load(void *dummy)
{

	if (dtrace_register("audit", &dtaudit_attr, DTRACE_PRIV_USER, NULL,
	    &dtaudit_pops, NULL, &dtaudit_id) != 0)
		return;
	dtaudit_hook_preselect = dtaudit_preselect;
	dtaudit_hook_commit = dtaudit_commit;
	dtaudit_hook_bsm = dtaudit_bsm;
}

static int
dtaudit_unload(void)
{
	int error;

	dtaudit_hook_preselect = NULL;
	dtaudit_hook_commit = NULL;
	dtaudit_hook_bsm = NULL;
	if ((error = dtrace_unregister(dtaudit_id)) != 0)
		return (error);
	return (0);
}

static int
dtaudit_modevent(module_t mod __unused, int type, void *data __unused)
{
	int error = 0;

	switch (type) {
	case MOD_LOAD:
	case MOD_UNLOAD:
	case MOD_SHUTDOWN:
		break;

	default:
		error = EOPNOTSUPP;
		break;
	}

	return (error);
}

SYSINIT(dtaudit_load, SI_SUB_DTRACE_PROVIDER, SI_ORDER_ANY, dtaudit_load,
    NULL);
SYSUNINIT(dtaudit_unload, SI_SUB_DTRACE_PROVIDER, SI_ORDER_ANY,
    dtaudit_unload, NULL);

DEV_MODULE(dtaudit, dtaudit_modevent, NULL);
MODULE_VERSION(dtaudit, 1);
MODULE_DEPEND(dtaudit, dtrace, 1, 1, 1);
MODULE_DEPEND(dtaudit, opensolaris, 1, 1, 1);
