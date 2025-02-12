// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * LSM initialization functions
 */

#define pr_fmt(fmt) "LSM: " fmt

#include <linux/init.h>
#include <linux/lsm_hooks.h>

#include "lsm.h"

char *lsm_names;

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

/* Mark an LSM's enabled flag. */
static int lsm_enabled_true __initdata = 1;
static int lsm_enabled_false __initdata = 0;
static void __init set_enabled(struct lsm_info *lsm, bool enabled)
{
	/*
	 * When an LSM hasn't configured an enable variable, we can use
	 * a hard-coded location for storing the default enabled state.
	 */
	if (!lsm->enabled) {
		if (enabled)
			lsm->enabled = &lsm_enabled_true;
		else
			lsm->enabled = &lsm_enabled_false;
	} else if (lsm->enabled == &lsm_enabled_true) {
		if (!enabled)
			lsm->enabled = &lsm_enabled_false;
	} else if (lsm->enabled == &lsm_enabled_false) {
		if (enabled)
			lsm->enabled = &lsm_enabled_true;
	} else {
		*lsm->enabled = enabled;
	}
}

static inline bool is_enabled(struct lsm_info *lsm)
{
	if (!lsm->enabled)
		return false;

	return *lsm->enabled;
}

/* Is an LSM already listed in the ordered LSMs list? */
static bool __init exists_ordered_lsm(struct lsm_info *lsm)
{
	struct lsm_info **check;

	lsm_order_for_each(check) {
		if (*check == lsm)
			return true;
	}

	return false;
}

/* Append an LSM to the list of ordered LSMs to initialize. */
static int last_lsm __initdata;
static void __init append_ordered_lsm(struct lsm_info *lsm, const char *from)
{
	/* Ignore duplicate selections. */
	if (exists_ordered_lsm(lsm))
		return;

	if (WARN(last_lsm == MAX_LSM_COUNT, "%s: out of LSM static calls!?\n", from))
		return;

	/* Enable this LSM, if it is not already set. */
	if (!lsm->enabled)
		lsm->enabled = &lsm_enabled_true;
	lsm_order[last_lsm] = lsm;
	lsm_idlist[last_lsm++] = lsm->id;

	init_debug("%s ordered: %s (%s)\n", from, lsm->id->name,
		   is_enabled(lsm) ? "enabled" : "disabled");
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
	struct lsm_blob_sizes *blobs;

	if (!is_enabled(lsm)) {
		set_enabled(lsm, false);
		return;
	} else if ((lsm->flags & LSM_FLAG_EXCLUSIVE) && lsm_exclusive) {
		init_debug("exclusive disabled: %s\n", lsm->id->name);
		set_enabled(lsm, false);
		return;
	}

	/* Mark the LSM as enabled. */
	set_enabled(lsm, true);
	if ((lsm->flags & LSM_FLAG_EXCLUSIVE) && !lsm_exclusive) {
		init_debug("exclusive chosen:   %s\n", lsm->id->name);
		lsm_exclusive = lsm;
	}

	/* Register the LSM blob sizes. */
	blobs = lsm->blobs;
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
	if (is_enabled(lsm)) {
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
			append_ordered_lsm(lsm, "  first");
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
				set_enabled(major, false);
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
					append_ordered_lsm(lsm, origin);
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
			if (exists_ordered_lsm(lsm))
				continue;
			if (strcmp(lsm->id->name, lsm_order_legacy) == 0)
				append_ordered_lsm(lsm, "security=");
		}
	}

	/* LSM_ORDER_LAST is always last. */
	lsm_for_each_raw(lsm) {
		if (lsm->order == LSM_ORDER_LAST)
			append_ordered_lsm(lsm, "   last");
	}

	/* Disable all LSMs not in the ordered list. */
	lsm_for_each_raw(lsm) {
		if (exists_ordered_lsm(lsm))
			continue;
		set_enabled(lsm, false);
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
		if (is_enabled(early))
			pr_cont("%s%s",
				first++ == 0 ? "" : ",", early->id->name);
	}
	lsm_order_for_each(lsm) {
		if (is_enabled(*lsm))
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

static bool match_last_lsm(const char *list, const char *lsm)
{
	const char *last;

	if (WARN_ON(!list || !lsm))
		return false;
	last = strrchr(list, ',');
	if (last)
		/* Pass the comma, strcmp() will check for '\0' */
		last++;
	else
		last = list;
	return !strcmp(last, lsm);
}

static int lsm_append(const char *new, char **result)
{
	char *cp;

	if (*result == NULL) {
		*result = kstrdup(new, GFP_KERNEL);
		if (*result == NULL)
			return -ENOMEM;
	} else {
		/* Check if it is the last registered name */
		if (match_last_lsm(*result, new))
			return 0;
		cp = kasprintf(GFP_KERNEL, "%s,%s", *result, new);
		if (cp == NULL)
			return -ENOMEM;
		kfree(*result);
		*result = cp;
	}
	return 0;
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

	/*
	 * Don't try to append during early_security_init(), we'll come back
	 * and fix this up afterwards.
	 */
	if (slab_is_available()) {
		if (lsm_append(lsmid->name, &lsm_names) < 0)
			panic("%s - Cannot get early memory.\n", __func__);
	}
}

int __init early_security_init(void)
{
	struct lsm_info *lsm;

	lsm_early_for_each_raw(lsm) {
		if (!lsm->enabled)
			lsm->enabled = &lsm_enabled_true;
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
			   is_enabled(lsm) ? "enabled" : "disabled");
		if (lsm->enabled)
			lsm_append(lsm->id->name, &lsm_names);
	}

	/* Load LSMs in specified order. */
	lsm_init_ordered();

	return 0;
}
