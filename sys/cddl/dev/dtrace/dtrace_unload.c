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

static int
dtrace_unload()
{
	dtrace_state_t *state;
	int error = 0;

	destroy_dev(dtrace_dev);
	destroy_dev(helper_dev);

	mutex_enter(&dtrace_provider_lock);
	mutex_enter(&dtrace_lock);
	mutex_enter(&cpu_lock);

	ASSERT(dtrace_opens == 0);

	if (dtrace_helpers > 0) {
		mutex_exit(&cpu_lock);
		mutex_exit(&dtrace_lock);
		mutex_exit(&dtrace_provider_lock);
		return (EBUSY);
	}

	if (dtrace_unregister((dtrace_provider_id_t)dtrace_provider) != 0) {
		mutex_exit(&cpu_lock);
		mutex_exit(&dtrace_lock);
		mutex_exit(&dtrace_provider_lock);
		return (EBUSY);
	}

	dtrace_provider = NULL;
	EVENTHANDLER_DEREGISTER(kld_load, dtrace_kld_load_tag);
	EVENTHANDLER_DEREGISTER(kld_unload_try, dtrace_kld_unload_try_tag);

	if ((state = dtrace_anon_grab()) != NULL) {
		/*
		 * If there were ECBs on this state, the provider should
		 * have not been allowed to detach; assert that there is
		 * none.
		 */
		ASSERT(state->dts_necbs == 0);
		dtrace_state_destroy(state);
	}

	bzero(&dtrace_anon, sizeof (dtrace_anon_t));

	mutex_exit(&cpu_lock);

	if (dtrace_probes != NULL) {
		kmem_free(dtrace_probes, 0);
		dtrace_probes = NULL;
		dtrace_nprobes = 0;
	}

	dtrace_hash_destroy(dtrace_bymod);
	dtrace_hash_destroy(dtrace_byfunc);
	dtrace_hash_destroy(dtrace_byname);
	dtrace_bymod = NULL;
	dtrace_byfunc = NULL;
	dtrace_byname = NULL;

	kmem_cache_destroy(dtrace_state_cache);

	delete_unrhdr(dtrace_arena);

	if (dtrace_toxrange != NULL) {
		kmem_free(dtrace_toxrange, 0);
		dtrace_toxrange = NULL;
		dtrace_toxranges = 0;
		dtrace_toxranges_max = 0;
	}

	ASSERT(dtrace_vtime_references == 0);
	ASSERT(dtrace_opens == 0);
	ASSERT(dtrace_retained == NULL);

	mutex_exit(&dtrace_lock);
	mutex_exit(&dtrace_provider_lock);

	mutex_destroy(&dtrace_meta_lock);
	mutex_destroy(&dtrace_provider_lock);
	mutex_destroy(&dtrace_lock);
#ifdef DEBUG
	mutex_destroy(&dtrace_errlock);
#endif

	taskq_destroy(dtrace_taskq);

	/* Reset our hook for exceptions. */
	dtrace_invop_uninit();

	/*
	 * Reset our hook for thread switches, but ensure that vtime isn't
	 * active first.
	 */
	dtrace_vtime_active = 0;
	dtrace_vtime_switch_func = NULL;

	/* Unhook from the trap handler. */
	dtrace_trap_func = NULL;

	return (error);
}
