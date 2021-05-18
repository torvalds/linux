// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2021 Microsoft Corporation
 *
 * Author: Lakshmi Ramasubramanian (nramas@linux.microsoft.com)
 *
 * Measure critical data structures maintainted by SELinux
 * using IMA subsystem.
 */
#include <linux/vmalloc.h>
#include <linux/ima.h>
#include "security.h"
#include "ima.h"

/*
 * selinux_ima_collect_state - Read selinux configuration settings
 *
 * @state: selinux_state
 *
 * On success returns the configuration settings string.
 * On error, returns NULL.
 */
static char *selinux_ima_collect_state(struct selinux_state *state)
{
	const char *on = "=1;", *off = "=0;";
	char *buf;
	int buf_len, len, i, rc;

	buf_len = strlen("initialized=0;enforcing=0;checkreqprot=0;") + 1;

	len = strlen(on);
	for (i = 0; i < __POLICYDB_CAPABILITY_MAX; i++)
		buf_len += strlen(selinux_policycap_names[i]) + len;

	buf = kzalloc(buf_len, GFP_KERNEL);
	if (!buf)
		return NULL;

	rc = strscpy(buf, "initialized", buf_len);
	WARN_ON(rc < 0);

	rc = strlcat(buf, selinux_initialized(state) ? on : off, buf_len);
	WARN_ON(rc >= buf_len);

	rc = strlcat(buf, "enforcing", buf_len);
	WARN_ON(rc >= buf_len);

	rc = strlcat(buf, enforcing_enabled(state) ? on : off, buf_len);
	WARN_ON(rc >= buf_len);

	rc = strlcat(buf, "checkreqprot", buf_len);
	WARN_ON(rc >= buf_len);

	rc = strlcat(buf, checkreqprot_get(state) ? on : off, buf_len);
	WARN_ON(rc >= buf_len);

	for (i = 0; i < __POLICYDB_CAPABILITY_MAX; i++) {
		rc = strlcat(buf, selinux_policycap_names[i], buf_len);
		WARN_ON(rc >= buf_len);

		rc = strlcat(buf, state->policycap[i] ? on : off, buf_len);
		WARN_ON(rc >= buf_len);
	}

	return buf;
}

/*
 * selinux_ima_measure_state_locked - Measure SELinux state and hash of policy
 *
 * @state: selinux state struct
 */
void selinux_ima_measure_state_locked(struct selinux_state *state)
{
	char *state_str = NULL;
	void *policy = NULL;
	size_t policy_len;
	int rc = 0;

	WARN_ON(!mutex_is_locked(&state->policy_mutex));

	state_str = selinux_ima_collect_state(state);
	if (!state_str) {
		pr_err("SELinux: %s: failed to read state.\n", __func__);
		return;
	}

	ima_measure_critical_data("selinux", "selinux-state",
				  state_str, strlen(state_str), false);

	kfree(state_str);

	/*
	 * Measure SELinux policy only after initialization is completed.
	 */
	if (!selinux_initialized(state))
		return;

	rc = security_read_state_kernel(state, &policy, &policy_len);
	if (rc) {
		pr_err("SELinux: %s: failed to read policy %d.\n", __func__, rc);
		return;
	}

	ima_measure_critical_data("selinux", "selinux-policy-hash",
				  policy, policy_len, true);

	vfree(policy);
}

/*
 * selinux_ima_measure_state - Measure SELinux state and hash of policy
 *
 * @state: selinux state struct
 */
void selinux_ima_measure_state(struct selinux_state *state)
{
	WARN_ON(mutex_is_locked(&state->policy_mutex));

	mutex_lock(&state->policy_mutex);
	selinux_ima_measure_state_locked(state);
	mutex_unlock(&state->policy_mutex);
}
