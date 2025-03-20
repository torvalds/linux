/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Landlock - Credential hooks
 *
 * Copyright © 2019-2020 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2019-2020 ANSSI
 * Copyright © 2021-2025 Microsoft Corporation
 */

#ifndef _SECURITY_LANDLOCK_CRED_H
#define _SECURITY_LANDLOCK_CRED_H

#include <linux/container_of.h>
#include <linux/cred.h>
#include <linux/init.h>
#include <linux/rcupdate.h>

#include "access.h"
#include "limits.h"
#include "ruleset.h"
#include "setup.h"

/**
 * struct landlock_cred_security - Credential security blob
 *
 * This structure is packed to minimize the size of struct
 * landlock_file_security.  However, it is always aligned in the LSM cred blob,
 * see lsm_set_blob_size().
 */
struct landlock_cred_security {
	/**
	 * @domain: Immutable ruleset enforced on a task.
	 */
	struct landlock_ruleset *domain;

#ifdef CONFIG_AUDIT
	/**
	 * @domain_exec: Bitmask identifying the domain layers that were enforced by
	 * the current task's executed file (i.e. no new execve(2) since
	 * landlock_restrict_self(2)).
	 */
	u16 domain_exec;
	/**
	 * @log_subdomains_off: Set if the domain descendants's log_status should be
	 * set to %LANDLOCK_LOG_DISABLED.  This is not a landlock_hierarchy
	 * configuration because it applies to future descendant domains and it does
	 * not require a current domain.
	 */
	u8 log_subdomains_off : 1;
#endif /* CONFIG_AUDIT */
} __packed;

#ifdef CONFIG_AUDIT

/* Makes sure all layer executions can be stored. */
static_assert(BITS_PER_TYPE(typeof_member(struct landlock_cred_security,
					  domain_exec)) >=
	      LANDLOCK_MAX_NUM_LAYERS);

#endif /* CONFIG_AUDIT */

static inline struct landlock_cred_security *
landlock_cred(const struct cred *cred)
{
	return cred->security + landlock_blob_sizes.lbs_cred;
}

static inline struct landlock_ruleset *landlock_get_current_domain(void)
{
	return landlock_cred(current_cred())->domain;
}

/*
 * The call needs to come from an RCU read-side critical section.
 */
static inline const struct landlock_ruleset *
landlock_get_task_domain(const struct task_struct *const task)
{
	return landlock_cred(__task_cred(task))->domain;
}

static inline bool landlocked(const struct task_struct *const task)
{
	bool has_dom;

	if (task == current)
		return !!landlock_get_current_domain();

	rcu_read_lock();
	has_dom = !!landlock_get_task_domain(task);
	rcu_read_unlock();
	return has_dom;
}

/**
 * landlock_get_applicable_subject - Return the subject's Landlock credential
 *                                   if its enforced domain applies to (i.e.
 *                                   handles) at least one of the access rights
 *                                   specified in @masks
 *
 * @cred: credential
 * @masks: access masks
 * @handle_layer: returned youngest layer handling a subset of @masks.  Not set
 *                if the function returns NULL.
 *
 * Returns: landlock_cred(@cred) if any access rights specified in @masks is
 * handled, or NULL otherwise.
 */
static inline const struct landlock_cred_security *
landlock_get_applicable_subject(const struct cred *const cred,
				const struct access_masks masks,
				size_t *const handle_layer)
{
	const union access_masks_all masks_all = {
		.masks = masks,
	};
	const struct landlock_ruleset *domain;
	ssize_t layer_level;

	if (!cred)
		return NULL;

	domain = landlock_cred(cred)->domain;
	if (!domain)
		return NULL;

	for (layer_level = domain->num_layers - 1; layer_level >= 0;
	     layer_level--) {
		union access_masks_all layer = {
			.masks = domain->access_masks[layer_level],
		};

		if (layer.all & masks_all.all) {
			if (handle_layer)
				*handle_layer = layer_level;

			return landlock_cred(cred);
		}
	}

	return NULL;
}

__init void landlock_add_cred_hooks(void);

#endif /* _SECURITY_LANDLOCK_CRED_H */
