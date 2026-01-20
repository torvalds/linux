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

/* Number of "early" LSMs */
static __initdata unsigned int lsm_count_early;

/* Build and boot-time LSM ordering. */
static __initconst const char *const lsm_order_builtin = CONFIG_LSM;
static __initdata const char *lsm_order_cmdline;
static __initdata const char *lsm_order_legacy;

/* Ordered list of LSMs to initialize. */
static __initdata struct lsm_info *lsm_order[MAX_LSM_COUNT + 1];
static __initdata struct lsm_info *lsm_exclusive;

#define lsm_order_for_each(iter)					\
	for ((iter) = lsm_order; *(iter); (iter)++)
#define lsm_for_each_raw(iter)						\
	for ((iter) = __start_lsm_info;					\
	     (iter) < __end_lsm_info; (iter)++)
#define lsm_early_for_each_raw(iter)					\
	for ((iter) = __start_early_lsm_info;				\
	     (iter) < __end_early_lsm_info; (iter)++)

#define lsm_initcall(level)						\
	({								\
		int _r, _rc = 0;					\
		struct lsm_info **_lp, *_l;				\
		lsm_order_for_each(_lp) {				\
			_l = *_lp;					\
			if (!_l->initcall_##level)			\
				continue;				\
			lsm_pr_dbg("running %s %s initcall",		\
				   _l->id->name, #level);		\
			_r = _l->initcall_##level();			\
			if (_r) {					\
				pr_warn("failed LSM %s %s initcall with errno %d\n", \
					_l->id->name, #level, _r);	\
				if (!_rc)				\
					_rc = _r;			\
			}						\
		}							\
		_rc;							\
	})

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
	lsm_debug = true;
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
	if (lsm->enabled && !lsm_is_enabled(lsm)) {
		lsm_pr_dbg("skip previously disabled LSM %s:%s\n",
			   src, lsm->id->name);
		return;
	}

	if (lsm_active_cnt == MAX_LSM_COUNT) {
		pr_warn("exceeded maximum LSM count on %s:%s\n",
			src, lsm->id->name);
		lsm_enabled_set(lsm, false);
		return;
	}

	if (lsm->flags & LSM_FLAG_EXCLUSIVE) {
		if (lsm_exclusive) {
			lsm_pr_dbg("skip exclusive LSM conflict %s:%s\n",
				   src, lsm->id->name);
			lsm_enabled_set(lsm, false);
			return;
		} else {
			lsm_pr_dbg("select exclusive LSM %s:%s\n",
				   src, lsm->id->name);
			lsm_exclusive = lsm;
		}
	}

	lsm_enabled_set(lsm, true);
	lsm_order[lsm_active_cnt] = lsm;
	lsm_idlist[lsm_active_cnt++] = lsm->id;

	lsm_pr_dbg("enabling LSM %s:%s\n", src, lsm->id->name);
}

/**
 * lsm_order_parse - Parse the comma delimited LSM list
 * @list: LSM list
 * @src: source of the list
 */
static void __init lsm_order_parse(const char *list, const char *src)
{
	struct lsm_info *lsm;
	char *sep, *name, *next;

	/* Handle any Legacy LSM exclusions if one was specified. */
	if (lsm_order_legacy) {
		/*
		 * To match the original "security=" behavior, this explicitly
		 * does NOT fallback to another Legacy Major if the selected
		 * one was separately disabled: disable all non-matching
		 * Legacy Major LSMs.
		 */
		lsm_for_each_raw(lsm) {
			if ((lsm->flags & LSM_FLAG_LEGACY_MAJOR) &&
			     strcmp(lsm->id->name, lsm_order_legacy)) {
				lsm_enabled_set(lsm, false);
				lsm_pr_dbg("skip legacy LSM conflict %s:%s\n",
					   src, lsm->id->name);
			}
		}
	}

	/* LSM_ORDER_FIRST */
	lsm_for_each_raw(lsm) {
		if (lsm->order == LSM_ORDER_FIRST)
			lsm_order_append(lsm, "first");
	}

	/* Normal or "mutable" LSMs */
	sep = kstrdup(list, GFP_KERNEL);
	next = sep;
	/* Walk the list, looking for matching LSMs. */
	while ((name = strsep(&next, ",")) != NULL) {
		lsm_for_each_raw(lsm) {
			if (!strcmp(lsm->id->name, name) &&
			    lsm->order == LSM_ORDER_MUTABLE)
				lsm_order_append(lsm, src);
		}
	}
	kfree(sep);

	/* Legacy LSM if specified. */
	if (lsm_order_legacy) {
		lsm_for_each_raw(lsm) {
			if (!strcmp(lsm->id->name, lsm_order_legacy))
				lsm_order_append(lsm, src);
		}
	}

	/* LSM_ORDER_LAST */
	lsm_for_each_raw(lsm) {
		if (lsm->order == LSM_ORDER_LAST)
			lsm_order_append(lsm, "last");
	}

	/* Disable all LSMs not previously enabled. */
	lsm_for_each_raw(lsm) {
		if (lsm_order_exists(lsm))
			continue;
		lsm_enabled_set(lsm, false);
		lsm_pr_dbg("skip disabled LSM %s:%s\n", src, lsm->id->name);
	}
}

/**
 * lsm_blob_size_update - Update the LSM blob size and offset information
 * @sz_req: the requested additional blob size
 * @sz_cur: the existing blob size
 */
static void __init lsm_blob_size_update(unsigned int *sz_req,
					unsigned int *sz_cur)
{
	unsigned int offset;

	if (*sz_req == 0)
		return;

	offset = ALIGN(*sz_cur, sizeof(void *));
	*sz_cur = offset + *sz_req;
	*sz_req = offset;
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
	blobs = lsm->blobs;
	lsm_blob_size_update(&blobs->lbs_cred, &blob_sizes.lbs_cred);
	lsm_blob_size_update(&blobs->lbs_file, &blob_sizes.lbs_file);
	lsm_blob_size_update(&blobs->lbs_ib, &blob_sizes.lbs_ib);
	/* inode blob gets an rcu_head in addition to LSM blobs. */
	if (blobs->lbs_inode && blob_sizes.lbs_inode == 0)
		blob_sizes.lbs_inode = sizeof(struct rcu_head);
	lsm_blob_size_update(&blobs->lbs_inode, &blob_sizes.lbs_inode);
	lsm_blob_size_update(&blobs->lbs_ipc, &blob_sizes.lbs_ipc);
	lsm_blob_size_update(&blobs->lbs_key, &blob_sizes.lbs_key);
	lsm_blob_size_update(&blobs->lbs_msg_msg, &blob_sizes.lbs_msg_msg);
	lsm_blob_size_update(&blobs->lbs_perf_event,
			     &blob_sizes.lbs_perf_event);
	lsm_blob_size_update(&blobs->lbs_sock, &blob_sizes.lbs_sock);
	lsm_blob_size_update(&blobs->lbs_superblock,
			     &blob_sizes.lbs_superblock);
	lsm_blob_size_update(&blobs->lbs_task, &blob_sizes.lbs_task);
	lsm_blob_size_update(&blobs->lbs_tun_dev, &blob_sizes.lbs_tun_dev);
	lsm_blob_size_update(&blobs->lbs_xattr_count,
			     &blob_sizes.lbs_xattr_count);
	lsm_blob_size_update(&blobs->lbs_bdev, &blob_sizes.lbs_bdev);
	lsm_blob_size_update(&blobs->lbs_bpf_map, &blob_sizes.lbs_bpf_map);
	lsm_blob_size_update(&blobs->lbs_bpf_prog, &blob_sizes.lbs_bpf_prog);
	lsm_blob_size_update(&blobs->lbs_bpf_token, &blob_sizes.lbs_bpf_token);
}

/**
 * lsm_init_single - Initialize a given LSM
 * @lsm: LSM definition
 */
static void __init lsm_init_single(struct lsm_info *lsm)
{
	int ret;

	if (!lsm_is_enabled(lsm))
		return;

	lsm_pr_dbg("initializing %s\n", lsm->id->name);
	ret = lsm->init();
	WARN(ret, "%s failed to initialize: %d\n", lsm->id->name, ret);
}

/**
 * lsm_static_call_init - Initialize a LSM's static calls
 * @hl: LSM hook list
 */
static int __init lsm_static_call_init(struct security_hook_list *hl)
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
			return 0;
		}
		scall++;
	}

	return -ENOSPC;
}

/**
 * security_add_hooks - Add a LSM's hooks to the LSM framework's hook lists
 * @hooks: LSM hooks to add
 * @count: number of hooks to add
 * @lsmid: identification information for the LSM
 *
 * Each LSM has to register its hooks with the LSM framework.
 */
void __init security_add_hooks(struct security_hook_list *hooks, int count,
			       const struct lsm_id *lsmid)
{
	int i;

	for (i = 0; i < count; i++) {
		hooks[i].lsmid = lsmid;
		if (lsm_static_call_init(&hooks[i]))
			panic("exhausted LSM callback slots with LSM %s\n",
			      lsmid->name);
	}
}

/**
 * early_security_init - Initialize the early LSMs
 */
int __init early_security_init(void)
{
	struct lsm_info *lsm;

	/* NOTE: lsm_pr_dbg() doesn't work here as lsm_debug is not yet set */

	lsm_early_for_each_raw(lsm) {
		lsm_enabled_set(lsm, true);
		lsm_order_append(lsm, "early");
		lsm_prepare(lsm);
		lsm_init_single(lsm);
		lsm_count_early++;
	}

	return 0;
}

/**
 * security_init - Initializes the LSM framework
 *
 * This should be called early in the kernel initialization sequence.
 */
int __init security_init(void)
{
	unsigned int cnt;
	struct lsm_info **lsm;

	if (lsm_debug) {
		struct lsm_info *i;

		cnt = 0;
		lsm_pr("available LSMs: ");
		lsm_early_for_each_raw(i)
			lsm_pr_cont("%s%s(E)", (cnt++ ? "," : ""), i->id->name);
		lsm_for_each_raw(i)
			lsm_pr_cont("%s%s", (cnt++ ? "," : ""), i->id->name);
		lsm_pr_cont("\n");

		lsm_pr("built-in LSM config: %s\n", lsm_order_builtin);

		lsm_pr("legacy LSM parameter: %s\n", lsm_order_legacy);
		lsm_pr("boot LSM parameter: %s\n", lsm_order_cmdline);

		/* see the note about lsm_pr_dbg() in early_security_init() */
		lsm_early_for_each_raw(i)
			lsm_pr("enabled LSM early:%s\n", i->id->name);
	}

	if (lsm_order_cmdline) {
		if (lsm_order_legacy)
			lsm_order_legacy = NULL;
		lsm_order_parse(lsm_order_cmdline, "cmdline");
	} else
		lsm_order_parse(lsm_order_builtin, "builtin");

	lsm_order_for_each(lsm)
		lsm_prepare(*lsm);

	if (lsm_debug) {
		lsm_pr("blob(cred) size %d\n", blob_sizes.lbs_cred);
		lsm_pr("blob(file) size %d\n", blob_sizes.lbs_file);
		lsm_pr("blob(ib) size %d\n", blob_sizes.lbs_ib);
		lsm_pr("blob(inode) size %d\n", blob_sizes.lbs_inode);
		lsm_pr("blob(ipc) size %d\n", blob_sizes.lbs_ipc);
		lsm_pr("blob(key) size %d\n", blob_sizes.lbs_key);
		lsm_pr("blob(msg_msg)_size %d\n", blob_sizes.lbs_msg_msg);
		lsm_pr("blob(sock) size %d\n", blob_sizes.lbs_sock);
		lsm_pr("blob(superblock) size %d\n", blob_sizes.lbs_superblock);
		lsm_pr("blob(perf_event) size %d\n", blob_sizes.lbs_perf_event);
		lsm_pr("blob(task) size %d\n", blob_sizes.lbs_task);
		lsm_pr("blob(tun_dev) size %d\n", blob_sizes.lbs_tun_dev);
		lsm_pr("blob(xattr) count %d\n", blob_sizes.lbs_xattr_count);
		lsm_pr("blob(bdev) size %d\n", blob_sizes.lbs_bdev);
		lsm_pr("blob(bpf_map) size %d\n", blob_sizes.lbs_bpf_map);
		lsm_pr("blob(bpf_prog) size %d\n", blob_sizes.lbs_bpf_prog);
		lsm_pr("blob(bpf_token) size %d\n", blob_sizes.lbs_bpf_token);
	}

	if (blob_sizes.lbs_file)
		lsm_file_cache = kmem_cache_create("lsm_file_cache",
						   blob_sizes.lbs_file, 0,
						   SLAB_PANIC, NULL);
	if (blob_sizes.lbs_inode)
		lsm_inode_cache = kmem_cache_create("lsm_inode_cache",
						    blob_sizes.lbs_inode, 0,
						    SLAB_PANIC, NULL);

	if (lsm_cred_alloc((struct cred *)unrcu_pointer(current->cred),
			   GFP_KERNEL))
		panic("early LSM cred alloc failed\n");
	if (lsm_task_alloc(current))
		panic("early LSM task alloc failed\n");

	cnt = 0;
	lsm_order_for_each(lsm) {
		/* skip the "early" LSMs as they have already been setup */
		if (cnt++ < lsm_count_early)
			continue;
		lsm_init_single(*lsm);
	}

	return 0;
}

/**
 * security_initcall_pure - Run the LSM pure initcalls
 */
static int __init security_initcall_pure(void)
{
	int rc_adr, rc_lsm;

	rc_adr = min_addr_init();
	rc_lsm = lsm_initcall(pure);

	return (rc_adr ? rc_adr : rc_lsm);
}
pure_initcall(security_initcall_pure);

/**
 * security_initcall_early - Run the LSM early initcalls
 */
static int __init security_initcall_early(void)
{
	return lsm_initcall(early);
}
early_initcall(security_initcall_early);

/**
 * security_initcall_core - Run the LSM core initcalls
 */
static int __init security_initcall_core(void)
{
	int rc_sfs, rc_lsm;

	rc_sfs = securityfs_init();
	rc_lsm = lsm_initcall(core);

	return (rc_sfs ? rc_sfs : rc_lsm);
}
core_initcall(security_initcall_core);

/**
 * security_initcall_subsys - Run the LSM subsys initcalls
 */
static int __init security_initcall_subsys(void)
{
	return lsm_initcall(subsys);
}
subsys_initcall(security_initcall_subsys);

/**
 * security_initcall_fs - Run the LSM fs initcalls
 */
static int __init security_initcall_fs(void)
{
	return lsm_initcall(fs);
}
fs_initcall(security_initcall_fs);

/**
 * security_initcall_device - Run the LSM device initcalls
 */
static int __init security_initcall_device(void)
{
	return lsm_initcall(device);
}
device_initcall(security_initcall_device);

/**
 * security_initcall_late - Run the LSM late initcalls
 */
static int __init security_initcall_late(void)
{
	int rc;

	rc = lsm_initcall(late);
	lsm_pr_dbg("all enabled LSMs fully activated\n");
	call_blocking_lsm_notifier(LSM_STARTED_ALL, NULL);

	return rc;
}
late_initcall(security_initcall_late);
