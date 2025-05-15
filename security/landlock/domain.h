/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Landlock - Domain management
 *
 * Copyright © 2016-2020 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2018-2020 ANSSI
 * Copyright © 2024-2025 Microsoft Corporation
 */

#ifndef _SECURITY_LANDLOCK_DOMAIN_H
#define _SECURITY_LANDLOCK_DOMAIN_H

#include <linux/limits.h>
#include <linux/mm.h>
#include <linux/path.h>
#include <linux/pid.h>
#include <linux/refcount.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include "access.h"
#include "audit.h"

enum landlock_log_status {
	LANDLOCK_LOG_PENDING = 0,
	LANDLOCK_LOG_RECORDED,
	LANDLOCK_LOG_DISABLED,
};

/**
 * struct landlock_details - Domain's creation information
 *
 * Rarely accessed, mainly when logging the first domain's denial.
 *
 * The contained pointers are initialized at the domain creation time and never
 * changed again.  Contrary to most other Landlock object types, this one is
 * not allocated with GFP_KERNEL_ACCOUNT because its size may not be under the
 * caller's control (e.g. unknown exe_path) and the data is not explicitly
 * requested nor used by tasks.
 */
struct landlock_details {
	/**
	 * @pid: PID of the task that initially restricted itself.  It still
	 * identifies the same task.  Keeping a reference to this PID ensures that
	 * it will not be recycled.
	 */
	struct pid *pid;
	/**
	 * @uid: UID of the task that initially restricted itself, at creation time.
	 */
	uid_t uid;
	/**
	 * @comm: Command line of the task that initially restricted itself, at
	 * creation time.  Always NULL terminated.
	 */
	char comm[TASK_COMM_LEN];
	/**
	 * @exe_path: Executable path of the task that initially restricted
	 * itself, at creation time.  Always NULL terminated, and never greater
	 * than LANDLOCK_PATH_MAX_SIZE.
	 */
	char exe_path[];
};

/* Adds 11 extra characters for the potential " (deleted)" suffix. */
#define LANDLOCK_PATH_MAX_SIZE (PATH_MAX + 11)

/* Makes sure the greatest landlock_details can be allocated. */
static_assert(struct_size_t(struct landlock_details, exe_path,
			    LANDLOCK_PATH_MAX_SIZE) <= KMALLOC_MAX_SIZE);

/**
 * struct landlock_hierarchy - Node in a domain hierarchy
 */
struct landlock_hierarchy {
	/**
	 * @parent: Pointer to the parent node, or NULL if it is a root
	 * Landlock domain.
	 */
	struct landlock_hierarchy *parent;
	/**
	 * @usage: Number of potential children domains plus their parent
	 * domain.
	 */
	refcount_t usage;

#ifdef CONFIG_AUDIT
	/**
	 * @log_status: Whether this domain should be logged or not.  Because
	 * concurrent log entries may be created at the same time, it is still
	 * possible to have several domain records of the same domain.
	 */
	enum landlock_log_status log_status;
	/**
	 * @num_denials: Number of access requests denied by this domain.
	 * Masked (i.e. never logged) denials are still counted.
	 */
	atomic64_t num_denials;
	/**
	 * @id: Landlock domain ID, sets once at domain creation time.
	 */
	u64 id;
	/**
	 * @details: Information about the related domain.
	 */
	const struct landlock_details *details;
	/**
	 * @log_same_exec: Set if the domain is *not* configured with
	 * %LANDLOCK_RESTRICT_SELF_LOG_SAME_EXEC_OFF.  Set to true by default.
	 */
	u32 log_same_exec : 1,
		/**
		 * @log_new_exec: Set if the domain is configured with
		 * %LANDLOCK_RESTRICT_SELF_LOG_NEW_EXEC_ON.  Set to false by default.
		 */
		log_new_exec : 1;
#endif /* CONFIG_AUDIT */
};

#ifdef CONFIG_AUDIT

deny_masks_t
landlock_get_deny_masks(const access_mask_t all_existing_optional_access,
			const access_mask_t optional_access,
			const layer_mask_t (*const layer_masks)[],
			size_t layer_masks_size);

int landlock_init_hierarchy_log(struct landlock_hierarchy *const hierarchy);

static inline void
landlock_free_hierarchy_details(struct landlock_hierarchy *const hierarchy)
{
	if (!hierarchy || !hierarchy->details)
		return;

	put_pid(hierarchy->details->pid);
	kfree(hierarchy->details);
}

#else /* CONFIG_AUDIT */

static inline int
landlock_init_hierarchy_log(struct landlock_hierarchy *const hierarchy)
{
	return 0;
}

static inline void
landlock_free_hierarchy_details(struct landlock_hierarchy *const hierarchy)
{
}

#endif /* CONFIG_AUDIT */

static inline void
landlock_get_hierarchy(struct landlock_hierarchy *const hierarchy)
{
	if (hierarchy)
		refcount_inc(&hierarchy->usage);
}

static inline void landlock_put_hierarchy(struct landlock_hierarchy *hierarchy)
{
	while (hierarchy && refcount_dec_and_test(&hierarchy->usage)) {
		const struct landlock_hierarchy *const freeme = hierarchy;

		landlock_log_drop_domain(hierarchy);
		landlock_free_hierarchy_details(hierarchy);
		hierarchy = hierarchy->parent;
		kfree(freeme);
	}
}

#endif /* _SECURITY_LANDLOCK_DOMAIN_H */
