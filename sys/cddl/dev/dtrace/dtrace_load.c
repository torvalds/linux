/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 *
 * $FreeBSD$
 *
 */

#ifndef EARLY_AP_STARTUP
static void
dtrace_ap_start(void *dummy)
{
	int i;

	mutex_enter(&cpu_lock);

	/* Setup the rest of the CPUs. */
	CPU_FOREACH(i) {
		if (i == 0)
			continue;

		(void) dtrace_cpu_setup(CPU_CONFIG, i);
	}

	mutex_exit(&cpu_lock);
}

SYSINIT(dtrace_ap_start, SI_SUB_SMP, SI_ORDER_ANY, dtrace_ap_start, NULL);
#endif

static void
dtrace_load(void *dummy)
{
	dtrace_provider_id_t id;
#ifdef EARLY_AP_STARTUP
	int i;
#endif

#ifndef illumos
	/*
	 * DTrace uses negative logic for the destructive mode switch, so it
	 * is required to translate from the sysctl which uses positive logic.
	 */ 
	if (dtrace_allow_destructive)
		dtrace_destructive_disallow = 0;
	else
		dtrace_destructive_disallow = 1;
#endif

	/* Hook into the trap handler. */
	dtrace_trap_func = dtrace_trap;

	/* Hang our hook for thread switches. */
	dtrace_vtime_switch_func = dtrace_vtime_switch;

	/* Hang our hook for exceptions. */
	dtrace_invop_init();

	dtrace_taskq = taskq_create("dtrace_taskq", 1, maxclsyspri, 0, 0, 0);

	dtrace_arena = new_unrhdr(1, INT_MAX, &dtrace_unr_mtx);

	/* Register callbacks for linker file load and unload events. */
	dtrace_kld_load_tag = EVENTHANDLER_REGISTER(kld_load,
	    dtrace_kld_load, NULL, EVENTHANDLER_PRI_ANY);
	dtrace_kld_unload_try_tag = EVENTHANDLER_REGISTER(kld_unload_try,
	    dtrace_kld_unload_try, NULL, EVENTHANDLER_PRI_ANY);

	/*
	 * Initialise the mutexes without 'witness' because the dtrace
	 * code is mostly written to wait for memory. To have the
	 * witness code change a malloc() from M_WAITOK to M_NOWAIT
	 * because a lock is held would surely create a panic in a
	 * low memory situation. And that low memory situation might be
	 * the very problem we are trying to trace.
	 */
	mutex_init(&dtrace_lock,"dtrace probe state", MUTEX_DEFAULT, NULL);
	mutex_init(&dtrace_provider_lock,"dtrace provider state", MUTEX_DEFAULT, NULL);
	mutex_init(&dtrace_meta_lock,"dtrace meta-provider state", MUTEX_DEFAULT, NULL);
#ifdef DEBUG
	mutex_init(&dtrace_errlock,"dtrace error lock", MUTEX_DEFAULT, NULL);
#endif

	mutex_enter(&cpu_lock);
	mutex_enter(&dtrace_provider_lock);
	mutex_enter(&dtrace_lock);

	dtrace_state_cache = kmem_cache_create("dtrace_state_cache",
	    sizeof (dtrace_dstate_percpu_t) * NCPU, DTRACE_STATE_ALIGN,
	    NULL, NULL, NULL, NULL, NULL, 0);

	ASSERT(MUTEX_HELD(&cpu_lock));
	dtrace_bymod = dtrace_hash_create(offsetof(dtrace_probe_t, dtpr_mod),
	    offsetof(dtrace_probe_t, dtpr_nextmod),
	    offsetof(dtrace_probe_t, dtpr_prevmod));

	dtrace_byfunc = dtrace_hash_create(offsetof(dtrace_probe_t, dtpr_func),
	    offsetof(dtrace_probe_t, dtpr_nextfunc),
	    offsetof(dtrace_probe_t, dtpr_prevfunc));

	dtrace_byname = dtrace_hash_create(offsetof(dtrace_probe_t, dtpr_name),
	    offsetof(dtrace_probe_t, dtpr_nextname),
	    offsetof(dtrace_probe_t, dtpr_prevname));

	if (dtrace_retain_max < 1) {
		cmn_err(CE_WARN, "illegal value (%lu) for dtrace_retain_max; "
		    "setting to 1", dtrace_retain_max);
		dtrace_retain_max = 1;
	}

	/*
	 * Now discover our toxic ranges.
	 */
	dtrace_toxic_ranges(dtrace_toxrange_add);

	/*
	 * Before we register ourselves as a provider to our own framework,
	 * we would like to assert that dtrace_provider is NULL -- but that's
	 * not true if we were loaded as a dependency of a DTrace provider.
	 * Once we've registered, we can assert that dtrace_provider is our
	 * pseudo provider.
	 */
	(void) dtrace_register("dtrace", &dtrace_provider_attr,
	    DTRACE_PRIV_NONE, 0, &dtrace_provider_ops, NULL, &id);

	ASSERT(dtrace_provider != NULL);
	ASSERT((dtrace_provider_id_t)dtrace_provider == id);

	dtrace_probeid_begin = dtrace_probe_create((dtrace_provider_id_t)
	    dtrace_provider, NULL, NULL, "BEGIN", 0, NULL);
	dtrace_probeid_end = dtrace_probe_create((dtrace_provider_id_t)
	    dtrace_provider, NULL, NULL, "END", 0, NULL);
	dtrace_probeid_error = dtrace_probe_create((dtrace_provider_id_t)
	    dtrace_provider, NULL, NULL, "ERROR", 1, NULL);

	mutex_exit(&dtrace_lock);
	mutex_exit(&dtrace_provider_lock);

#ifdef EARLY_AP_STARTUP
	CPU_FOREACH(i) {
		(void) dtrace_cpu_setup(CPU_CONFIG, i);
	}
#else
	/* Setup the boot CPU */
	(void) dtrace_cpu_setup(CPU_CONFIG, 0);
#endif

	mutex_exit(&cpu_lock);

	dtrace_dev = make_dev(&dtrace_cdevsw, 0, UID_ROOT, GID_WHEEL, 0600,
	    "dtrace/dtrace");
	helper_dev = make_dev(&helper_cdevsw, 0, UID_ROOT, GID_WHEEL, 0660,
	    "dtrace/helper");
}
