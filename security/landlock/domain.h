/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Landlock - Domain management
 *
 * Copyright © 2016-2020 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2018-2020 ANSSI
 * Copyright © 2024-2025 Microsoft Corporation
 * Copyright © 2026 Cloudflare, Inc.
 */

#ifndef _SECURITY_LANDLOCK_DOMAIN_H
#define _SECURITY_LANDLOCK_DOMAIN_H

#include <linux/cleanup.h>
#include <linux/limits.h>
#include <linux/mm.h>
#include <linux/path.h>
#include <linux/pid.h>
#include <linux/refcount.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include "access.h"
#include "log.h"
#include "ruleset.h"

enum landlock_log_status {
	/*
	 * Hierarchy whose creation event has not been emitted, so it is not yet
	 * observable from user space.  A hierarchy is born in this state (the
	 * zero value, so a partially initialized hierarchy defaults to "not
	 * observable") and leaves it when landlock_restrict_self() emits its
	 * creation event, right after the merge and before the thread-sync
	 * wait.  No trace free_domain event (and no audit deallocation record)
	 * fires while a hierarchy is in this state, so a hierarchy that never
	 * became observable (e.g. its initialization failed) is freed silently.
	 * A domain aborted by a thread-sync failure already emitted its
	 * creation event, so it is no longer UNCOMMITTED and does fire
	 * free_domain.
	 */
	LANDLOCK_LOG_UNCOMMITTED = 0,
	LANDLOCK_LOG_PENDING,
	LANDLOCK_LOG_RECORDED,
	LANDLOCK_LOG_DISABLED,
};

/**
 * struct landlock_details - Domain's creation information
 *
 * Rarely accessed, mainly when logging the first domain's denial.
 *
 * The contained pointers are initialized at the domain creation time and never
 * changed again.
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

#ifdef CONFIG_SECURITY_LANDLOCK_LOG
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
	 * @id: Landlock domain ID, set once at domain creation time.
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
	/**
	 * @quiet_masks: Bitmasks of access that should be quieted (i.e. not
	 * logged) if the related object is marked as quiet.
	 */
	struct access_masks quiet_masks;
#endif /* CONFIG_SECURITY_LANDLOCK_LOG */
};

#ifdef CONFIG_SECURITY_LANDLOCK_LOG

deny_masks_t
landlock_get_deny_masks(const access_mask_t all_existing_optional_access,
			const access_mask_t optional_access,
			const struct layer_masks *const masks);

optional_access_t landlock_get_quiet_optional_accesses(
	const access_mask_t all_existing_optional_access,
	const deny_masks_t deny_masks, const struct layer_masks *const masks);

int landlock_init_hierarchy_log(struct landlock_hierarchy *const hierarchy);

static inline void
landlock_free_hierarchy_details(struct landlock_hierarchy *const hierarchy)
{
	if (!hierarchy || !hierarchy->details)
		return;

	put_pid(hierarchy->details->pid);
	kfree(hierarchy->details);
}

#else /* CONFIG_SECURITY_LANDLOCK_LOG */

static inline int
landlock_init_hierarchy_log(struct landlock_hierarchy *const hierarchy)
{
	return 0;
}

static inline void
landlock_free_hierarchy_details(struct landlock_hierarchy *const hierarchy)
{
}

#endif /* CONFIG_SECURITY_LANDLOCK_LOG */

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

		landlock_log_free_domain(hierarchy);
		landlock_free_hierarchy_details(hierarchy);
		hierarchy = hierarchy->parent;
		kfree(freeme);
	}
}

/**
 * struct landlock_domain - Immutable Landlock domain
 *
 * A domain is created from a ruleset by landlock_merge_ruleset() and enforced
 * on a task.  Once created, its rules and access masks are immutable.  Unlike
 * &struct landlock_ruleset, a domain has no lock field.
 */
struct landlock_domain {
	/**
	 * @rules: Red-black tree storage for rules.
	 */
	struct landlock_rules rules;
	/**
	 * @hierarchy: Enables hierarchy identification even when a parent
	 * domain vanishes.  This is needed for the ptrace and scope
	 * restrictions.
	 */
	struct landlock_hierarchy *hierarchy;
	union {
		/**
		 * @work_free: Enables to free a domain within a lockless
		 * section.  This is only used by landlock_put_domain_deferred()
		 * when @usage reaches zero.  The fields @usage, @num_layers and
		 * @handled_masks are then unused.
		 */
		struct work_struct work_free;
		struct {
			/**
			 * @usage: Number of credentials referencing this
			 * domain.
			 */
			refcount_t usage;
			/**
			 * @num_layers: Number of layers that are used in this
			 * domain.  This enables to check that all the layers
			 * allow an access request.
			 */
			u32 num_layers;
			/**
			 * @handled_masks: Contains the subset of filesystem and
			 * network actions that are restricted by a domain.  A
			 * domain saves all layers of merged rulesets in a stack
			 * (FAM), starting from the first layer to the last one.
			 * These layers are used when merging rulesets, for user
			 * space backward compatibility (i.e. future-proof), and
			 * to properly handle merged rulesets without
			 * overlapping access rights.  These layers are set once
			 * and never changed for the lifetime of the domain.
			 */
			struct access_masks handled_masks[];
		};
	};
};

static inline access_mask_t
landlock_get_fs_access_mask(const struct landlock_domain *const domain,
			    const u16 layer_level)
{
	/* Handles all initially denied by default access rights. */
	return domain->handled_masks[layer_level].fs |
	       _LANDLOCK_ACCESS_FS_INITIALLY_DENIED;
}

static inline access_mask_t
landlock_get_net_access_mask(const struct landlock_domain *const domain,
			     const u16 layer_level)
{
	return domain->handled_masks[layer_level].net;
}

static inline access_mask_t
landlock_get_scope_mask(const struct landlock_domain *const domain,
			const u16 layer_level)
{
	return domain->handled_masks[layer_level].scope;
}

/**
 * landlock_union_access_masks - Return all access rights handled in the
 *				 domain
 *
 * @domain: Landlock domain
 *
 * Return: An access_masks result of the OR of all the domain's access masks.
 */
static inline struct access_masks
landlock_union_access_masks(const struct landlock_domain *const domain)
{
	union access_masks_all matches = {};
	size_t layer_level;

	for (layer_level = 0; layer_level < domain->num_layers; layer_level++) {
		union access_masks_all layer = {
			.masks = domain->handled_masks[layer_level],
		};

		matches.all |= layer.all;
	}

	return matches.masks;
}

void landlock_put_domain(struct landlock_domain *const domain);
void landlock_put_domain_deferred(struct landlock_domain *const domain);

DEFINE_FREE(landlock_put_domain, struct landlock_domain *,
	    if (!IS_ERR_OR_NULL(_T)) landlock_put_domain(_T))

struct landlock_domain *
landlock_merge_ruleset(struct landlock_domain *const parent,
		       struct landlock_ruleset *const ruleset);

bool landlock_unmask_layers(const struct landlock_domain *const domain,
			    const struct landlock_id id,
			    struct layer_masks *masks,
			    const struct landlock_rule **matched_rule);

access_mask_t
landlock_init_layer_masks(const struct landlock_domain *const domain,
			  const access_mask_t access_request,
			  struct layer_masks *masks,
			  const enum landlock_key_type key_type);

static inline void landlock_get_domain(struct landlock_domain *const domain)
{
	if (domain)
		refcount_inc(&domain->usage);
}

#endif /* _SECURITY_LANDLOCK_DOMAIN_H */
