// SPDX-License-Identifier: GPL-2.0-only
/*
 * Landlock - Domain management
 *
 * Copyright © 2016-2020 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2018-2020 ANSSI
 * Copyright © 2024-2025 Microsoft Corporation
 * Copyright © 2026 Cloudflare, Inc.
 */

#include <kunit/test.h>
#include <linux/bitops.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/overflow.h>
#include <linux/path.h>
#include <linux/pid.h>
#include <linux/rbtree.h>
#include <linux/refcount.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/uidgid.h>
#include <linux/workqueue.h>

#include "access.h"
#include "common.h"
#include "domain.h"
#include "id.h"
#include "limits.h"
#include "ruleset.h"

static void free_domain(struct landlock_domain *const domain)
{
	might_sleep();
	landlock_free_rules(&domain->rules);
	landlock_put_hierarchy(domain->hierarchy);
	kfree(domain);
}

void landlock_put_domain(struct landlock_domain *const domain)
{
	might_sleep();
	if (domain && refcount_dec_and_test(&domain->usage))
		free_domain(domain);
}

static void free_domain_work(struct work_struct *const work)
{
	struct landlock_domain *domain;

	domain = container_of(work, struct landlock_domain, work_free);
	free_domain(domain);
}

void landlock_put_domain_deferred(struct landlock_domain *const domain)
{
	if (domain && refcount_dec_and_test(&domain->usage)) {
		INIT_WORK(&domain->work_free, free_domain_work);
		schedule_work(&domain->work_free);
	}
}

/* The returned access has the same lifetime as the domain. */
const struct landlock_rule *
landlock_find_rule(const struct landlock_ruleset *const ruleset,
		   const struct landlock_id id)
{
	const struct rb_root *root;
	const struct rb_node *node;

	root = landlock_get_rule_root((struct landlock_rules *)&ruleset->rules,
				      id.type);
	if (IS_ERR(root))
		return NULL;
	node = root->rb_node;

	while (node) {
		struct landlock_rule *this =
			rb_entry(node, struct landlock_rule, node);

		if (this->key.data == id.key.data)
			return this;
		if (this->key.data < id.key.data)
			node = node->rb_right;
		else
			node = node->rb_left;
	}
	return NULL;
}

/**
 * landlock_unmask_layers - Remove the access rights in @masks which are
 *                          granted in @rule
 *
 * Updates the set of (per-layer) unfulfilled access rights @masks so that all
 * the access rights granted in @rule are removed from it (because they are now
 * fulfilled).
 *
 * @rule: A rule that grants a set of access rights for each layer.
 * @masks: A matrix of unfulfilled access rights for each layer.
 *
 * Return: True if the request is allowed (i.e. the access rights granted all
 * remaining unfulfilled access rights and masks has no leftover set bits).
 */
bool landlock_unmask_layers(const struct landlock_rule *const rule,
			    struct layer_masks *masks)
{
	if (!masks)
		return true;
	if (!rule)
		return false;

	/*
	 * An access is granted if, for each policy layer, at least one rule
	 * encountered on the pathwalk grants the requested access, regardless
	 * of its position in the layer stack.  We must then check the remaining
	 * layers for each inode, from the first added layer to the last one.
	 * When there are multiple requested accesses, for each policy layer,
	 * the full set of requested accesses may not be granted by only one
	 * rule, but by the union (binary OR) of multiple rules.  For example,
	 * /a/b <execute> + /a <read> grants /a/b <execute + read>.
	 *
	 * This function is called once per matching rule during the pathwalk,
	 * progressively clearing bits in @masks.  The overall access decision
	 * is per-layer: access is granted iff masks->layers[l].access == 0 for
	 * all layers l.  When two independent mechanisms can each grant access
	 * within a layer (e.g. a path rule OR a scope exception), the
	 * composition must evaluate per-layer: FOR-ALL l (A(l) OR B(l)), not
	 * (FOR-ALL l A(l)) OR (FOR-ALL l B(l)), to prevent bypass when
	 * different layers grant via different mechanisms.
	 */
	for (size_t i = 0; i < rule->num_layers; i++) {
		const struct landlock_layer *const layer = &rule->layers[i];

		/* Clear the bits where the layer in the rule grants access. */
		masks->layers[layer->level - 1].access &= ~layer->access;

#ifdef CONFIG_AUDIT
		/* Collect rule flags for each layer. */
		if (layer->flags.quiet)
			masks->layers[layer->level - 1].quiet = true;
#endif /* CONFIG_AUDIT */
	}

	for (size_t i = 0; i < ARRAY_SIZE(masks->layers); i++) {
		if (masks->layers[i].access)
			return false;
	}
	return true;
}

typedef access_mask_t
get_access_mask_t(const struct landlock_ruleset *const ruleset,
		  const u16 layer_level);

/**
 * landlock_init_layer_masks - Initialize layer masks from an access request
 *
 * Populates @masks such that for each access right in @access_request, the bits
 * for all the layers are set where this access right is handled.  Rule flags
 * are also zeroed.
 *
 * @domain: The domain that defines the current restrictions.
 * @access_request: The requested access rights to check.
 * @masks: Layer access masks to populate.
 * @key_type: The key type to switch between access masks of different types.
 *
 * Return: An access mask where each access right bit is set which is handled in
 * any of the active layers in @domain.
 */
access_mask_t
landlock_init_layer_masks(const struct landlock_ruleset *const domain,
			  const access_mask_t access_request,
			  struct layer_masks *const masks,
			  const enum landlock_key_type key_type)
{
	access_mask_t handled_accesses = 0;
	get_access_mask_t *get_access_mask;

	switch (key_type) {
	case LANDLOCK_KEY_INODE:
		get_access_mask = landlock_get_fs_access_mask;
		break;

#if IS_ENABLED(CONFIG_INET)
	case LANDLOCK_KEY_NET_PORT:
		get_access_mask = landlock_get_net_access_mask;
		break;
#endif /* IS_ENABLED(CONFIG_INET) */

	default:
		WARN_ON_ONCE(1);
		return 0;
	}

	/* An empty access request can happen because of O_WRONLY | O_RDWR. */
	if (!access_request)
		return 0;

	for (size_t i = 0; i < domain->num_layers; i++) {
		const access_mask_t handled = get_access_mask(domain, i);

		masks->layers[i].access = access_request & handled;
		handled_accesses |= masks->layers[i].access;
#ifdef CONFIG_AUDIT
		masks->layers[i].quiet = false;
#endif /* CONFIG_AUDIT */
	}
	for (size_t i = domain->num_layers; i < ARRAY_SIZE(masks->layers);
	     i++) {
		masks->layers[i].access = 0;
#ifdef CONFIG_AUDIT
		masks->layers[i].quiet = false;
#endif /* CONFIG_AUDIT */
	}

	return handled_accesses;
}

#ifdef CONFIG_AUDIT

/**
 * get_current_exe - Get the current's executable path, if any
 *
 * @exe_str: Returned pointer to a path string with a lifetime tied to the
 *           returned buffer, if any.
 * @exe_size: Returned size of @exe_str (including the trailing null
 *            character), if any.
 *
 * Return: A pointer to an allocated buffer where @exe_str point to, %NULL if
 * there is no executable path, or an error otherwise.
 */
static const void *get_current_exe(const char **const exe_str,
				   size_t *const exe_size)
{
	const size_t buffer_size = LANDLOCK_PATH_MAX_SIZE;
	struct mm_struct *mm = current->mm;
	struct file *file __free(fput) = NULL;
	char *buffer __free(kfree) = NULL;
	const char *exe;
	ssize_t size;

	if (!mm)
		return NULL;

	file = get_mm_exe_file(mm);
	if (!file)
		return NULL;

	buffer = kmalloc(buffer_size, GFP_KERNEL);
	if (!buffer)
		return ERR_PTR(-ENOMEM);

	exe = d_path(&file->f_path, buffer, buffer_size);
	if (WARN_ON_ONCE(IS_ERR(exe)))
		/* Should never happen according to LANDLOCK_PATH_MAX_SIZE. */
		return ERR_CAST(exe);

	size = buffer + buffer_size - exe;
	if (WARN_ON_ONCE(size <= 0))
		return ERR_PTR(-ENAMETOOLONG);

	*exe_size = size;
	*exe_str = exe;
	return no_free_ptr(buffer);
}

/*
 * Return: A newly allocated object describing a domain, or an error
 * otherwise.
 */
static struct landlock_details *get_current_details(void)
{
	/* Cf. audit_log_d_path_exe() */
	static const char null_path[] = "(null)";
	const char *path_str = null_path;
	size_t path_size = sizeof(null_path);
	const void *buffer __free(kfree) = NULL;
	struct landlock_details *details;

	buffer = get_current_exe(&path_str, &path_size);
	if (IS_ERR(buffer))
		return ERR_CAST(buffer);

	/*
	 * Create the new details according to the path's length.  Account to
	 * the calling task's memcg, like the other Landlock per-domain
	 * allocations, even if it may not control the related size.
	 */
	details =
		kzalloc_flex(*details, exe_path, path_size, GFP_KERNEL_ACCOUNT);
	if (!details)
		return ERR_PTR(-ENOMEM);

	memcpy(details->exe_path, path_str, path_size);
	details->pid = get_pid(task_tgid(current));
	details->uid = from_kuid(&init_user_ns, current_uid());
	get_task_comm(details->comm, current);
	return details;
}

/**
 * landlock_init_hierarchy_log - Partially initialize landlock_hierarchy
 *
 * @hierarchy: The hierarchy to initialize.
 *
 * The current task is referenced as the domain that is enforcing the
 * restriction.  The subjective credentials must not be in an overridden state.
 *
 * @hierarchy->parent and @hierarchy->usage should already be set.
 *
 * Return: 0 on success, -errno on failure.
 */
int landlock_init_hierarchy_log(struct landlock_hierarchy *const hierarchy)
{
	struct landlock_details *details;

	details = get_current_details();
	if (IS_ERR(details))
		return PTR_ERR(details);

	hierarchy->details = details;
	hierarchy->id = landlock_get_id_range(1);
	hierarchy->log_status = LANDLOCK_LOG_PENDING;
	hierarchy->log_same_exec = true;
	hierarchy->log_new_exec = false;
	atomic64_set(&hierarchy->num_denials, 0);
	return 0;
}

static deny_masks_t
get_layer_deny_mask(const access_mask_t all_existing_optional_access,
		    const unsigned long access_bit, const size_t layer)
{
	unsigned long access_weight;

	/* This may require change with new object types. */
	WARN_ON_ONCE(all_existing_optional_access !=
		     _LANDLOCK_ACCESS_FS_OPTIONAL);

	if (WARN_ON_ONCE(layer >= LANDLOCK_MAX_NUM_LAYERS))
		return 0;

	access_weight = hweight_long(all_existing_optional_access &
				     GENMASK(access_bit, 0));
	if (WARN_ON_ONCE(access_weight < 1))
		return 0;

	return layer
	       << ((access_weight - 1) * HWEIGHT(LANDLOCK_MAX_NUM_LAYERS - 1));
}

/**
 * landlock_get_quiet_optional_accesses - Get optional accesses which are
 *                                        covered by quiet rule flags.
 *
 * @all_existing_optional_access: Bitmask of valid optional accesses.
 * @deny_masks: Domain layer levels that denied each optional access (the
 *              deny_masks field on struct landlock_file_security).
 * @masks: The struct layer_masks collected during the path walk.
 *
 * Return: a bitmask of which optional accesses are denied by layers for which
 * the quiet flag was collected during the path walk.
 */
optional_access_t landlock_get_quiet_optional_accesses(
	const access_mask_t all_existing_optional_access,
	const deny_masks_t deny_masks, const struct layer_masks *const masks)
{
	const unsigned long access_opt = all_existing_optional_access;
	size_t access_index = 0;
	unsigned long access_bit;
	optional_access_t quiet_optional_accesses = 0;

	/* This will require change with new object types. */
	WARN_ON_ONCE(access_opt != _LANDLOCK_ACCESS_FS_OPTIONAL);

	for_each_set_bit(access_bit, &access_opt,
			 BITS_PER_TYPE(access_mask_t)) {
		const u8 layer =
			(deny_masks >> (access_index *
					HWEIGHT(LANDLOCK_MAX_NUM_LAYERS - 1))) &
			(LANDLOCK_MAX_NUM_LAYERS - 1);

		if (masks->layers[layer].quiet)
			quiet_optional_accesses |= BIT(access_index);
		access_index++;
	}
	return quiet_optional_accesses;
}

#ifdef CONFIG_SECURITY_LANDLOCK_KUNIT_TEST

static void test_get_layer_deny_mask(struct kunit *const test)
{
	const unsigned long truncate = BIT_INDEX(LANDLOCK_ACCESS_FS_TRUNCATE);
	const unsigned long ioctl_dev = BIT_INDEX(LANDLOCK_ACCESS_FS_IOCTL_DEV);

	KUNIT_EXPECT_EQ(test, 0,
			get_layer_deny_mask(_LANDLOCK_ACCESS_FS_OPTIONAL,
					    truncate, 0));
	KUNIT_EXPECT_EQ(test, 0x3,
			get_layer_deny_mask(_LANDLOCK_ACCESS_FS_OPTIONAL,
					    truncate, 3));

	KUNIT_EXPECT_EQ(test, 0,
			get_layer_deny_mask(_LANDLOCK_ACCESS_FS_OPTIONAL,
					    ioctl_dev, 0));
	KUNIT_EXPECT_EQ(test, 0xf0,
			get_layer_deny_mask(_LANDLOCK_ACCESS_FS_OPTIONAL,
					    ioctl_dev, 15));
}

#endif /* CONFIG_SECURITY_LANDLOCK_KUNIT_TEST */

deny_masks_t
landlock_get_deny_masks(const access_mask_t all_existing_optional_access,
			const access_mask_t optional_access,
			const struct layer_masks *const masks)
{
	const unsigned long access_opt = optional_access;
	unsigned long access_bit;
	deny_masks_t deny_masks = 0;
	access_mask_t all_denied = 0;

	/* This may require change with new object types. */
	WARN_ON_ONCE(!access_mask_subset(optional_access,
					 all_existing_optional_access));

	if (WARN_ON_ONCE(!masks))
		return 0;

	if (WARN_ON_ONCE(!access_opt))
		return 0;

	for (ssize_t i = ARRAY_SIZE(masks->layers) - 1; i >= 0; i--) {
		const access_mask_t denied = masks->layers[i].access &
					     optional_access;
		const unsigned long newly_denied = denied & ~all_denied;

		if (!newly_denied)
			continue;

		for_each_set_bit(access_bit, &newly_denied,
				 8 * sizeof(access_mask_t)) {
			deny_masks |= get_layer_deny_mask(
				all_existing_optional_access, access_bit, i);
		}
		all_denied |= denied;
	}
	return deny_masks;
}

#ifdef CONFIG_SECURITY_LANDLOCK_KUNIT_TEST

static void test_landlock_get_deny_masks(struct kunit *const test)
{
	const struct layer_masks layers1 = {
		.layers[0].access = LANDLOCK_ACCESS_FS_EXECUTE |
				    LANDLOCK_ACCESS_FS_IOCTL_DEV,
		.layers[1].access = LANDLOCK_ACCESS_FS_TRUNCATE,
		.layers[2].access = LANDLOCK_ACCESS_FS_IOCTL_DEV,
		.layers[9].access = LANDLOCK_ACCESS_FS_EXECUTE,
	};

	KUNIT_EXPECT_EQ(test, 0x1,
			landlock_get_deny_masks(_LANDLOCK_ACCESS_FS_OPTIONAL,
						LANDLOCK_ACCESS_FS_TRUNCATE,
						&layers1));
	KUNIT_EXPECT_EQ(test, 0x20,
			landlock_get_deny_masks(_LANDLOCK_ACCESS_FS_OPTIONAL,
						LANDLOCK_ACCESS_FS_IOCTL_DEV,
						&layers1));
	KUNIT_EXPECT_EQ(
		test, 0x21,
		landlock_get_deny_masks(_LANDLOCK_ACCESS_FS_OPTIONAL,
					LANDLOCK_ACCESS_FS_TRUNCATE |
						LANDLOCK_ACCESS_FS_IOCTL_DEV,
					&layers1));
}

#endif /* CONFIG_SECURITY_LANDLOCK_KUNIT_TEST */

#ifdef CONFIG_SECURITY_LANDLOCK_KUNIT_TEST

static struct kunit_case test_cases[] = {
	/* clang-format off */
	KUNIT_CASE(test_get_layer_deny_mask),
	KUNIT_CASE(test_landlock_get_deny_masks),
	{}
	/* clang-format on */
};

static struct kunit_suite test_suite = {
	.name = "landlock_domain",
	.test_cases = test_cases,
};

kunit_test_suite(test_suite);

#endif /* CONFIG_SECURITY_LANDLOCK_KUNIT_TEST */

#endif /* CONFIG_AUDIT */
