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
 * selinux_ima_measure_state - Measure hash of the SELinux policy
 *
 * @state: selinux state struct
 *
 * NOTE: This function must be called with policy_mutex held.
 */
void selinux_ima_measure_state(struct selinux_state *state)
{
	void *policy = NULL;
	size_t policy_len;
	int rc = 0;

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
