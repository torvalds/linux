/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Landlock LSM - Credential hooks
 *
 * Copyright © 2019-2020 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2019-2020 ANSSI
 */

#ifndef _SECURITY_LANDLOCK_CRED_H
#define _SECURITY_LANDLOCK_CRED_H

#include <linux/cred.h>
#include <linux/init.h>
#include <linux/rcupdate.h>

#include "ruleset.h"
#include "setup.h"

struct landlock_cred_security {
	struct landlock_ruleset *domain;
};

static inline struct landlock_cred_security *landlock_cred(
		const struct cred *cred)
{
	return cred->security + landlock_blob_sizes.lbs_cred;
}

static inline const struct landlock_ruleset *landlock_get_current_domain(void)
{
	return landlock_cred(current_cred())->domain;
}

/*
 * The call needs to come from an RCU read-side critical section.
 */
static inline const struct landlock_ruleset *landlock_get_task_domain(
		const struct task_struct *const task)
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

__init void landlock_add_cred_hooks(void);

#endif /* _SECURITY_LANDLOCK_CRED_H */
