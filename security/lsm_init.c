// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * LSM initialization functions
 */

#define pr_fmt(fmt) "LSM: " fmt

#include <linux/init.h>
#include <linux/lsm_hooks.h>

#include "lsm.h"

/* LSM enabled constants. */
static __initdata int lsm_enabled_true = 1;
static __initdata int lsm_enabled_false = 0;

/* Pointers to LSM sections defined in include/asm-generic/vmlinux.lds.h */
extern struct lsm_info __start_lsm_info[], __end_lsm_info[];
extern struct lsm_info __start_early_lsm_info[], __end_early_lsm_info[];

/* Build and boot-time LSM ordering. */
static __initconst const char *const lsm_order_builtin = CONFIG_LSM;
static __initdata const char *lsm_order_cmdline;
static __initdata const char *lsm_order_legacy;

/* Ordered list of LSMs to initialize. */
static __initdata struct lsm_info *lsm_order[MAX_LSM_COUNT + 1];
static __initdata struct lsm_info *lsm_exclusive;

static __initdata bool debug;
#define init_debug(...)							\
	do {								\
		if (debug)						\
			pr_info(__VA_ARGS__);				\
	} while (0)

#define lsm_order_for_each(iter)					\
	for ((iter) = lsm_order; *(iter); (iter)++)
#define lsm_for_each_raw(iter)						\
	for ((iter) = __start_lsm_info;					\
	     (iter) < __end_lsm_info; (iter)++)
#define lsm_early_for_each_raw(iter)					\
	for ((iter) = __start_early_lsm_info;				\
	     (iter) < __end_early_lsm_info; (iter)++)

/**
 * lsm_choose_security - Legacy "major" LSM selection
 * @str: kernel command line parameter
 */
static int __init lsm_choose_security(char *str)
{
	lsm_order_legacy = str;
	return 1;
}
__setup("security=", lsm_choose_security);

/**
 * lsm_choose_lsm - Modern LSM selection
 * @str: kernel command line parameter
 */
static int __init lsm_choose_lsm(char *str)
{
	lsm_order_cmdline = str;
	return 1;
}
__setup("lsm=", lsm_choose_lsm);

/**
 * lsm_debug_enable - Enable LSM framework debugging
 * @str: kernel command line parameter
 *
 * Currently we only provide debug info during LSM initialization, but we may
 * want to expand this in the future.
 */
static int __init lsm_debug_enable(char *str)
{
	debug = true;
	return 1;
}
__setup("lsm.debug", lsm_debug_enable);

/**
 * lsm_enabled_set - Mark a LSM as enabled
 * @lsm: LSM definition
 * @enabled: enabled flag
 */
static void __init lsm_enabled_set(struct lsm_info *lsm, bool enabled)
{
	/*
	 * When an LSM hasn't configured an enable variable, we can use
	 * a hard-coded location for storing the default enabled state.
	 */
	if (!lsm->enabled ||
	    lsm->enabled == &lsm_enabled_true ||
	    lsm->enabled == &lsm_enabled_false) {
		lsm->enabled = enabled ? &lsm_enabled_true : &lsm_enabled_false;
	} else {
		*lsm->enabled = enabled;
	}
}

/**
 * lsm_is_enabled - Determine if a LSM is enabled
 * @lsm: LSM definition
 */
static inline bool lsm_is_enabled(struct lsm_info *lsm)
{
	return (lsm->enabled ? *lsm->enabled : false);
}

/**
 * lsm_order_exists - Determine if a LSM exists in the ordered list
 * @lsm: LSM definition
 */
static bool __init lsm_order_exists(struct lsm_info *lsm)
{
	struct lsm_info **check;

	lsm_order_for_each(check) {
		if (*check == lsm)
			return true;
	}

	return false;
}

/**
 * lsm_order_append - Append a LSM to the ordered list
 * @lsm: LSM definition
 * @src: source of the addition
 *
 * Append @lsm to the enabled LSM array after ensuring that it hasn't been
 * explicitly disabled, is a duplicate entry, or would run afoul of the
 * LSM_FLAG_EXCLUSIVE logic.
 */
static void __init lsm_order_append(struct lsm_info *lsm, const char *src)
{
	/* Ignore duplicate selections. */
	if (lsm_order_exists(lsm))
		return;

	/* Skip explicitly disabled LSMs. */
	if (lsm->enabled && !lsm_is_enabled(lsm))
		goto out;

	if (WARN(lsm_active_cnt == MAX_LSM_COUNT,
		 "%s: out of LSM static calls!?\n", src)) {
		lsm_enabled_set(lsm, false);
		goto out;
	}

	if (lsm->flags & LSM_FLAG_EXCLUSIVE) {
		if (lsm_exclusive) {
			init_debug("exclusive disabled: %s\n", lsm->id->name);
			lsm_enabled_set(lsm, false);
			goto out;
		} else {
			init_debug("exclusive chosen:   %s\n", lsm->id->name);
			lsm_exclusive = lsm;
		}
	}

	lsm_enabled_set(lsm, true);
	lsm_order[lsm_active_cnt] = lsm;
	lsm_idlist[lsm_active_cnt++] = lsm->id;

out:
	init_debug("%s ordered: %s (%s)\n", src, lsm->id->name,
		   lsm_is_enabled(lsm) ? "enabled" : "disabled");
}

static void __init lsm_set_blob_size(int *need, int *lbs)
{
	int offset;

	if (*need <= 0)
		return;

	offset = ALIGN(*lbs, sizeof(void *));
	*lbs = offset + *need;
	*need = offset;
}

/**
 * lsm_prepare - Prepare the LSM framework for a new LSM
 * @lsm: LSM definition
 */
static void __init lsm_prepare(struct lsm_info *lsm)
{
	struct lsm_blob_sizes *blobs = lsm->blobs;

	if (!blobs)
		return;

	/* Register the LSM blob sizes. */
	lsm_set_blob_size(&blobs->lbs_cred, &blob_sizes.lbs_cred);
	lsm_set_blob_size(&blobs->lbs_file, &blob_sizes.lbs_file);
	lsm_set_blob_size(&blobs->lbs_ib, &blob_sizes.lbs_ib);
	/* inode blob gets an rcu_head in addition to LSM blobs. */
	if (blobs->lbs_inode && blob_sizes.lbs_inode == 0)
		blob_sizes.lbs_inode = sizeof(struct rcu_head);
	lsm_set_blob_size(&blobs->lbs_inode, &blob_sizes.lbs_inode);
	lsm_set_blob_size(&blobs->lbs_ipc, &blob_sizes.lbs_ipc);
	lsm_set_blob_size(&blobs->lbs_key, &blob_sizes.lbs_key);
	lsm_set_blob_size(&blobs->lbs_msg_msg, &blob_sizes.lbs_msg_msg);
	lsm_set_blob_size(&blobs->lbs_perf_event, &blob_sizes.lbs_perf_event);
	lsm_set_blob_size(&blobs->lbs_sock, &blob_sizes.lbs_sock);
	lsm_set_blob_size(&blobs->lbs_superblock, &blob_sizes.lbs_superblock);
	lsm_set_blob_size(&blobs->lbs_task, &blob_sizes.lbs_task);
	lsm_set_blob_size(&blobs->lbs_tun_dev, &blob_sizes.lbs_tun_dev);
	lsm_set_blob_size(&blobs->lbs_xattr_count,
			  &blob_sizes.lbs_xattr_count);
	lsm_set_blob_size(&blobs->lbs_bdev, &blob_sizes.lbs_bdev);
	lsm_set_blob_size(&blobs->lbs_bpf_map, &blob_sizes.lbs_bpf_map);
	lsm_set_blob_size(&blobs->lbs_bpf_prog, &blob_sizes.lbs_bpf_prog);
	lsm_set_blob_size(&blobs->lbs_bpf_token, &blob_sizes.lbs_bpf_token);
}

/* Initialize a given LSM, if it is enabled. */
static void __init initialize_lsm(struct lsm_info *lsm)
{
	if (lsm_is_enabled(lsm)) {
		int ret;

		init_debug("initializing %s\n", lsm->id->name);
		ret = lsm->init();
		WARN(ret, "%s failed to initialize: %d\n", lsm->id->name, ret);
	}
}

/* Populate ordered LSMs list from comma-separated LSM name list. */
static void __init ordered_lsm_parse(const char *order, const char *origin)
{
	struct lsm_info *lsm;
	char *sep, *name, *next;

	/* LSM_ORDER_FIRST is always first. */
	lsm_for_each_raw(lsm) {
		if (lsm->order == LSM_ORDER_FIRST)
			lsm_order_append(lsm, "  first");
	}

	/* Process "security=", if given. */
	if (lsm_order_legacy) {
		struct lsm_info *major;

		/*
		 * To match the original "security=" behavior, this
		 * explicitly does NOT fallback to another Legacy Major
		 * if the selected one was separately disabled: disable
		 * all non-matching Legacy Major LSMs.
		 */
		lsm_for_each_raw(major) {
			if ((major->flags & LSM_FLAG_LEGACY_MAJOR) &&
			    strcmp(major->id->name, lsm_order_legacy) != 0) {
				lsm_enabled_set(major, false);
				init_debug("security=%s disabled: %s (only one legacy major LSM)\n",
					   lsm_order_legacy, major->id->name);
			}
		}
	}

	sep = kstrdup(order, GFP_KERNEL);
	next = sep;
	/* Walk the list, looking for matching LSMs. */
	while ((name = strsep(&next, ",")) != NULL) {
		bool found = false;

		lsm_for_each_raw(lsm) {
			if (strcmp(lsm->id->name, name) == 0) {
				if (lsm->order == LSM_ORDER_MUTABLE)
					lsm_order_append(lsm, origin);
				found = true;
			}
		}

		if (!found)
			init_debug("%s ignored: %s (not built into kernel)\n",
				   origin, name);
	}

	/* Process "security=", if given. */
	if (lsm_order_legacy) {
		lsm_for_each_raw(lsm) {
			if (lsm_order_exists(lsm))
				continue;
			if (strcmp(lsm->id->name, lsm_order_legacy) == 0)
				lsm_order_append(lsm, "security=");
		}
	}

	/* LSM_ORDER_LAST is always last. */
	lsm_for_each_raw(lsm) {
		if (lsm->order == LSM_ORDER_LAST)
			lsm_order_append(lsm, "   last");
	}

	/* Disable all LSMs not in the ordered list. */
	lsm_for_each_raw(lsm) {
		if (lsm_order_exists(lsm))
			continue;
		lsm_enabled_set(lsm, false);
		init_debug("%s skipped: %s (not in requested order)\n",
			   origin, lsm->id->name);
	}

	kfree(sep);
}

/**
 * lsm_init_ordered - Initialize the ordered LSMs
 */
static void __init lsm_init_ordered(void)
{
	unsigned int first = 0;
	struct lsm_info **lsm;
	struct lsm_info *early;

	if (lsm_order_cmdline) {
		if (lsm_order_legacy) {
			pr_warn("security=%s is ignored because it is superseded by lsm=%s\n",
				lsm_order_legacy, lsm_order_cmdline);
			lsm_order_legacy = NULL;
		}
		ordered_lsm_parse(lsm_order_cmdline, "cmdline");
	} else
		ordered_lsm_parse(lsm_order_builtin, "builtin");

	lsm_order_for_each(lsm) {
		lsm_prepare(*lsm);
	}

	pr_info("initializing lsm=");
	lsm_early_for_each_raw(early) {
		if (lsm_is_enabled(early))
			pr_cont("%s%s",
				first++ == 0 ? "" : ",", early->id->name);
	}
	lsm_order_for_each(lsm) {
		if (lsm_is_enabled(*lsm))
			pr_cont("%s%s",
				first++ == 0 ? "" : ",", (*lsm)->id->name);
	}
	pr_cont("\n");

	init_debug("cred blob size       = %d\n", blob_sizes.lbs_cred);
	init_debug("file blob size       = %d\n", blob_sizes.lbs_file);
	init_debug("ib blob size         = %d\n", blob_sizes.lbs_ib);
	init_debug("inode blob size      = %d\n", blob_sizes.lbs_inode);
	init_debug("ipc blob size        = %d\n", blob_sizes.lbs_ipc);
#ifdef CONFIG_KEYS
	init_debug("key blob size        = %d\n", blob_sizes.lbs_key);
#endif /* CONFIG_KEYS */
	init_debug("msg_msg blob size    = %d\n", blob_sizes.lbs_msg_msg);
	init_debug("sock blob size       = %d\n", blob_sizes.lbs_sock);
	init_debug("superblock blob size = %d\n", blob_sizes.lbs_superblock);
	init_debug("perf event blob size = %d\n", blob_sizes.lbs_perf_event);
	init_debug("task blob size       = %d\n", blob_sizes.lbs_task);
	init_debug("tun device blob size = %d\n", blob_sizes.lbs_tun_dev);
	init_debug("xattr slots          = %d\n", blob_sizes.lbs_xattr_count);
	init_debug("bdev blob size       = %d\n", blob_sizes.lbs_bdev);
	init_debug("bpf map blob size    = %d\n", blob_sizes.lbs_bpf_map);
	init_debug("bpf prog blob size   = %d\n", blob_sizes.lbs_bpf_prog);
	init_debug("bpf token blob size  = %d\n", blob_sizes.lbs_bpf_token);

	if (blob_sizes.lbs_file)
		lsm_file_cache = kmem_cache_create("lsm_file_cache",
						   blob_sizes.lbs_file, 0,
						   SLAB_PANIC, NULL);
	if (blob_sizes.lbs_inode)
		lsm_inode_cache = kmem_cache_create("lsm_inode_cache",
						    blob_sizes.lbs_inode, 0,
						    SLAB_PANIC, NULL);

	if (lsm_cred_alloc((struct cred __rcu *)current->cred, GFP_KERNEL))
		panic("%s: early cred alloc failed.\n", __func__);
	if (lsm_task_alloc(current))
		panic("%s: early task alloc failed.\n", __func__);

	lsm_order_for_each(lsm) {
		initialize_lsm(*lsm);
	}
}

static void __init lsm_static_call_init(struct security_hook_list *hl)
{
	struct lsm_static_call *scall = hl->scalls;
	int i;

	for (i = 0; i < MAX_LSM_COUNT; i++) {
		/* Update the first static call that is not used yet */
		if (!scall->hl) {
			__static_call_update(scall->key, scall->trampoline,
					     hl->hook.lsm_func_addr);
			scall->hl = hl;
			static_branch_enable(scall->active);
			return;
		}
		scall++;
	}
	panic("%s - Ran out of static slots.\n", __func__);
}

/**
 * security_add_hooks - Add a modules hooks to the hook lists.
 * @hooks: the hooks to add
 * @count: the number of hooks to add
 * @lsmid: the identification information for the security module
 *
 * Each LSM has to register its hooks with the infrastructure.
 */
void __init security_add_hooks(struct security_hook_list *hooks, int count,
			       const struct lsm_id *lsmid)
{
	int i;

	for (i = 0; i < count; i++) {
		hooks[i].lsmid = lsmid;
		lsm_static_call_init(&hooks[i]);
	}
}

int __init early_security_init(void)
{
	struct lsm_info *lsm;

	lsm_early_for_each_raw(lsm) {
		lsm_enabled_set(lsm, true);
		lsm_order_append(lsm, "early");
		lsm_prepare(lsm);
		initialize_lsm(lsm);
	}

	return 0;
}

/**
 * security_init - initializes the security framework
 *
 * This should be called early in the kernel initialization sequence.
 */
int __init security_init(void)
{
	struct lsm_info *lsm;

	init_debug("legacy security=%s\n", lsm_order_legacy ? : " *unspecified*");
	init_debug("  CONFIG_LSM=%s\n", lsm_order_builtin);
	init_debug("boot arg lsm=%s\n", lsm_order_cmdline ? : " *unspecified*");

	/*
	 * Append the names of the early LSM modules now that kmalloc() is
	 * available
	 */
	lsm_early_for_each_raw(lsm) {
		init_debug("  early started: %s (%s)\n", lsm->id->name,
			   lsm_is_enabled(lsm) ? "enabled" : "disabled");
	}

	/* Load LSMs in specified order. */
	lsm_init_ordered();

	return 0;
}
