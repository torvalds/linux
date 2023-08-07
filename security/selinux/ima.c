// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2021 Microsoft Corporation
 *
 * Author: Lakshmi Ramasubramanian (nramas@linux.microsoft.com)
 *
 * Measure critical data structures maintained by SELinux
 * using IMA subsystem.
 */
#include <linux/vmalloc.h>
#include <linux/ima.h>
#include "security.h"
#include "ima.h"

/*
 * selinux_ima_collect_state - Read selinux configuration settings
 *
 * On success returns the configuration settings string.
 * On error, returns NULL.
 */
static char *selinux_ima_collect_state(void)
{
	const char *on = "=1;", *off = "=0;";
	char *buf;
	int buf_len, len, i, rc;

	buf_len = strlen("initialized=0;enforcing=0;checkreqprot=0;") + 1;

	len = strlen(on);
	for (i = 0; i < __POLICYDB_CAP_MAX; i++)
		buf_len += strlen(selinux_policycap_names[i]) + len;

	buf = kzalloc(buf_len, GFP_KERNEL);
	if (!buf)
		return NULL;

	rc = strscpy(buf, "initialized", buf_len);
	WARN_ON(rc < 0);

	rc = strlcat(buf, selinux_initialized() ? on : off, buf_len);
	WARN_ON(rc >= buf_len);

	rc = strlcat(buf, "enforcing", buf_len);
	WARN_ON(rc >= buf_len);

	rc = strlcat(buf, enforcing_enabled() ? on : off, buf_len);
	WARN_ON(rc >= buf_len);

	rc = strlcat(buf, "checkreqprot", buf_len);
	WARN_ON(rc >= buf_len);

	rc = strlcat(buf, checkreqprot_get() ? on : off, buf_len);
	WARN_ON(rc >= buf_len);

	for (i = 0; i < __POLICYDB_CAP_MAX; i++) {
		rc = strlcat(buf, selinux_policycap_names[i], buf_len);
		WARN_ON(rc >= buf_len);

		rc = strlcat(buf, selinux_state.policycap[i] ? on : off,
			buf_len);
		WARN_ON(rc >= buf_len);
	}

	return buf;
}

/*
 * selinux_ima_measure_state_locked - Measure SELinux state and hash of policy
 */
void selinux_ima_measure_state_locked(void)
{
	char *state_str = NULL;
	void *policy = NULL;
	size_t policy_len;
	int rc = 0;

	lockdep_assert_held(&selinux_state.policy_mutex);

	state_str = selinux_ima_collect_state();
	if (!state_str) {
		pr_err("SELinux: %s: failed to read state.\n", __func__);
		return;
	}

	ima_measure_critical_data("selinux", "selinux-state",
				  state_str, strlen(state_str), false,
				  NULL, 0);

	kfree(state_str);

	/*
	 * Measure SELinux policy only after initialization is completed.
	 */
	if (!selinux_initialized())
		return;

	rc = security_read_state_kernel(&policy, &policy_len);
	if (rc) {
		pr_err("SELinux: %s: failed to read policy %d.\n", __func__, rc);
		return;
	}

	ima_measure_critical_data("selinux", "selinux-policy-hash",
				  policy, policy_len, true,
				  NULL, 0);

	vfree(policy);
}

/*
 * selinux_ima_measure_state - Measure SELinux state and hash of policy
 */
void selinux_ima_measure_state(void)
{
	lockdep_assert_not_held(&selinux_state.policy_mutex);

	mutex_lock(&selinux_state.policy_mutex);
	selinux_ima_measure_state_locked();
	mutex_unlock(&selinux_state.policy_mutex);
}
