// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Security plug functions
 *
 * Copyright (C) 2001 WireX Communications, Inc <chris@wirex.com>
 * Copyright (C) 2001-2002 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (C) 2001 Networks Associates Technology, Inc <ssmalley@nai.com>
 * Copyright (C) 2016 Mellanox Technologies
 * Copyright (C) 2023 Microsoft Corporation <paul@paul-moore.com>
 */

#define pr_fmt(fmt) "LSM: " fmt

#include <linux/bpf.h>
#include <linux/capability.h>
#include <linux/dcache.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kernel_read_file.h>
#include <linux/lsm_hooks.h>
#include <linux/integrity.h>
#include <linux/ima.h>
#include <linux/evm.h>
#include <linux/fsnotify.h>
#include <linux/mman.h>
#include <linux/mount.h>
#include <linux/personality.h>
#include <linux/backing-dev.h>
#include <linux/string.h>
#include <linux/msg.h>
#include <net/flow.h>

/* How many LSMs were built into the kernel? */
#define LSM_COUNT (__end_lsm_info - __start_lsm_info)

/*
 * These are descriptions of the reasons that can be passed to the
 * security_locked_down() LSM hook. Placing this array here allows
 * all security modules to use the same descriptions for auditing
 * purposes.
 */
const char *const lockdown_reasons[LOCKDOWN_CONFIDENTIALITY_MAX + 1] = {
	[LOCKDOWN_NONE] = "none",
	[LOCKDOWN_MODULE_SIGNATURE] = "unsigned module loading",
	[LOCKDOWN_DEV_MEM] = "/dev/mem,kmem,port",
	[LOCKDOWN_EFI_TEST] = "/dev/efi_test access",
	[LOCKDOWN_KEXEC] = "kexec of unsigned images",
	[LOCKDOWN_HIBERNATION] = "hibernation",
	[LOCKDOWN_PCI_ACCESS] = "direct PCI access",
	[LOCKDOWN_IOPORT] = "raw io port access",
	[LOCKDOWN_MSR] = "raw MSR access",
	[LOCKDOWN_ACPI_TABLES] = "modifying ACPI tables",
	[LOCKDOWN_DEVICE_TREE] = "modifying device tree contents",
	[LOCKDOWN_PCMCIA_CIS] = "direct PCMCIA CIS storage",
	[LOCKDOWN_TIOCSSERIAL] = "reconfiguration of serial port IO",
	[LOCKDOWN_MODULE_PARAMETERS] = "unsafe module parameters",
	[LOCKDOWN_MMIOTRACE] = "unsafe mmio",
	[LOCKDOWN_DEBUGFS] = "debugfs access",
	[LOCKDOWN_XMON_WR] = "xmon write access",
	[LOCKDOWN_BPF_WRITE_USER] = "use of bpf to write user RAM",
	[LOCKDOWN_DBG_WRITE_KERNEL] = "use of kgdb/kdb to write kernel RAM",
	[LOCKDOWN_RTAS_ERROR_INJECTION] = "RTAS error injection",
	[LOCKDOWN_INTEGRITY_MAX] = "integrity",
	[LOCKDOWN_KCORE] = "/proc/kcore access",
	[LOCKDOWN_KPROBES] = "use of kprobes",
	[LOCKDOWN_BPF_READ_KERNEL] = "use of bpf to read kernel RAM",
	[LOCKDOWN_DBG_READ_KERNEL] = "use of kgdb/kdb to read kernel RAM",
	[LOCKDOWN_PERF] = "unsafe use of perf",
	[LOCKDOWN_TRACEFS] = "use of tracefs",
	[LOCKDOWN_XMON_RW] = "xmon read and write access",
	[LOCKDOWN_XFRM_SECRET] = "xfrm SA secret",
	[LOCKDOWN_CONFIDENTIALITY_MAX] = "confidentiality",
};

struct security_hook_heads security_hook_heads __ro_after_init;
static BLOCKING_NOTIFIER_HEAD(blocking_lsm_notifier_chain);

static struct kmem_cache *lsm_file_cache;
static struct kmem_cache *lsm_inode_cache;

char *lsm_names;
static struct lsm_blob_sizes blob_sizes __ro_after_init;

/* Boot-time LSM user choice */
static __initdata const char *chosen_lsm_order;
static __initdata const char *chosen_major_lsm;

static __initconst const char *const builtin_lsm_order = CONFIG_LSM;

/* Ordered list of LSMs to initialize. */
static __initdata struct lsm_info **ordered_lsms;
static __initdata struct lsm_info *exclusive;

static __initdata bool debug;
#define init_debug(...)						\
	do {							\
		if (debug)					\
			pr_info(__VA_ARGS__);			\
	} while (0)

static bool __init is_enabled(struct lsm_info *lsm)
{
	if (!lsm->enabled)
		return false;

	return *lsm->enabled;
}

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

/* Is an LSM already listed in the ordered LSMs list? */
static bool __init exists_ordered_lsm(struct lsm_info *lsm)
{
	struct lsm_info **check;

	for (check = ordered_lsms; *check; check++)
		if (*check == lsm)
			return true;

	return false;
}

/* Append an LSM to the list of ordered LSMs to initialize. */
static int last_lsm __initdata;
static void __init append_ordered_lsm(struct lsm_info *lsm, const char *from)
{
	/* Ignore duplicate selections. */
	if (exists_ordered_lsm(lsm))
		return;

	if (WARN(last_lsm == LSM_COUNT, "%s: out of LSM slots!?\n", from))
		return;

	/* Enable this LSM, if it is not already set. */
	if (!lsm->enabled)
		lsm->enabled = &lsm_enabled_true;
	ordered_lsms[last_lsm++] = lsm;

	init_debug("%s ordered: %s (%s)\n", from, lsm->name,
		   is_enabled(lsm) ? "enabled" : "disabled");
}

/* Is an LSM allowed to be initialized? */
static bool __init lsm_allowed(struct lsm_info *lsm)
{
	/* Skip if the LSM is disabled. */
	if (!is_enabled(lsm))
		return false;

	/* Not allowed if another exclusive LSM already initialized. */
	if ((lsm->flags & LSM_FLAG_EXCLUSIVE) && exclusive) {
		init_debug("exclusive disabled: %s\n", lsm->name);
		return false;
	}

	return true;
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

static void __init lsm_set_blob_sizes(struct lsm_blob_sizes *needed)
{
	if (!needed)
		return;

	lsm_set_blob_size(&needed->lbs_cred, &blob_sizes.lbs_cred);
	lsm_set_blob_size(&needed->lbs_file, &blob_sizes.lbs_file);
	/*
	 * The inode blob gets an rcu_head in addition to
	 * what the modules might need.
	 */
	if (needed->lbs_inode && blob_sizes.lbs_inode == 0)
		blob_sizes.lbs_inode = sizeof(struct rcu_head);
	lsm_set_blob_size(&needed->lbs_inode, &blob_sizes.lbs_inode);
	lsm_set_blob_size(&needed->lbs_ipc, &blob_sizes.lbs_ipc);
	lsm_set_blob_size(&needed->lbs_msg_msg, &blob_sizes.lbs_msg_msg);
	lsm_set_blob_size(&needed->lbs_superblock, &blob_sizes.lbs_superblock);
	lsm_set_blob_size(&needed->lbs_task, &blob_sizes.lbs_task);
	lsm_set_blob_size(&needed->lbs_xattr_count,
			  &blob_sizes.lbs_xattr_count);
}

/* Prepare LSM for initialization. */
static void __init prepare_lsm(struct lsm_info *lsm)
{
	int enabled = lsm_allowed(lsm);

	/* Record enablement (to handle any following exclusive LSMs). */
	set_enabled(lsm, enabled);

	/* If enabled, do pre-initialization work. */
	if (enabled) {
		if ((lsm->flags & LSM_FLAG_EXCLUSIVE) && !exclusive) {
			exclusive = lsm;
			init_debug("exclusive chosen:   %s\n", lsm->name);
		}

		lsm_set_blob_sizes(lsm->blobs);
	}
}

/* Initialize a given LSM, if it is enabled. */
static void __init initialize_lsm(struct lsm_info *lsm)
{
	if (is_enabled(lsm)) {
		int ret;

		init_debug("initializing %s\n", lsm->name);
		ret = lsm->init();
		WARN(ret, "%s failed to initialize: %d\n", lsm->name, ret);
	}
}

/* Populate ordered LSMs list from comma-separated LSM name list. */
static void __init ordered_lsm_parse(const char *order, const char *origin)
{
	struct lsm_info *lsm;
	char *sep, *name, *next;

	/* LSM_ORDER_FIRST is always first. */
	for (lsm = __start_lsm_info; lsm < __end_lsm_info; lsm++) {
		if (lsm->order == LSM_ORDER_FIRST)
			append_ordered_lsm(lsm, "  first");
	}

	/* Process "security=", if given. */
	if (chosen_major_lsm) {
		struct lsm_info *major;

		/*
		 * To match the original "security=" behavior, this
		 * explicitly does NOT fallback to another Legacy Major
		 * if the selected one was separately disabled: disable
		 * all non-matching Legacy Major LSMs.
		 */
		for (major = __start_lsm_info; major < __end_lsm_info;
		     major++) {
			if ((major->flags & LSM_FLAG_LEGACY_MAJOR) &&
			    strcmp(major->name, chosen_major_lsm) != 0) {
				set_enabled(major, false);
				init_debug("security=%s disabled: %s (only one legacy major LSM)\n",
					   chosen_major_lsm, major->name);
			}
		}
	}

	sep = kstrdup(order, GFP_KERNEL);
	next = sep;
	/* Walk the list, looking for matching LSMs. */
	while ((name = strsep(&next, ",")) != NULL) {
		bool found = false;

		for (lsm = __start_lsm_info; lsm < __end_lsm_info; lsm++) {
			if (strcmp(lsm->name, name) == 0) {
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
	if (chosen_major_lsm) {
		for (lsm = __start_lsm_info; lsm < __end_lsm_info; lsm++) {
			if (exists_ordered_lsm(lsm))
				continue;
			if (strcmp(lsm->name, chosen_major_lsm) == 0)
				append_ordered_lsm(lsm, "security=");
		}
	}

	/* LSM_ORDER_LAST is always last. */
	for (lsm = __start_lsm_info; lsm < __end_lsm_info; lsm++) {
		if (lsm->order == LSM_ORDER_LAST)
			append_ordered_lsm(lsm, "   last");
	}

	/* Disable all LSMs not in the ordered list. */
	for (lsm = __start_lsm_info; lsm < __end_lsm_info; lsm++) {
		if (exists_ordered_lsm(lsm))
			continue;
		set_enabled(lsm, false);
		init_debug("%s skipped: %s (not in requested order)\n",
			   origin, lsm->name);
	}

	kfree(sep);
}

static void __init lsm_early_cred(struct cred *cred);
static void __init lsm_early_task(struct task_struct *task);

static int lsm_append(const char *new, char **result);

static void __init report_lsm_order(void)
{
	struct lsm_info **lsm, *early;
	int first = 0;

	pr_info("initializing lsm=");

	/* Report each enabled LSM name, comma separated. */
	for (early = __start_early_lsm_info;
	     early < __end_early_lsm_info; early++)
		if (is_enabled(early))
			pr_cont("%s%s", first++ == 0 ? "" : ",", early->name);
	for (lsm = ordered_lsms; *lsm; lsm++)
		if (is_enabled(*lsm))
			pr_cont("%s%s", first++ == 0 ? "" : ",", (*lsm)->name);

	pr_cont("\n");
}

static void __init ordered_lsm_init(void)
{
	struct lsm_info **lsm;

	ordered_lsms = kcalloc(LSM_COUNT + 1, sizeof(*ordered_lsms),
			       GFP_KERNEL);

	if (chosen_lsm_order) {
		if (chosen_major_lsm) {
			pr_warn("security=%s is ignored because it is superseded by lsm=%s\n",
				chosen_major_lsm, chosen_lsm_order);
			chosen_major_lsm = NULL;
		}
		ordered_lsm_parse(chosen_lsm_order, "cmdline");
	} else
		ordered_lsm_parse(builtin_lsm_order, "builtin");

	for (lsm = ordered_lsms; *lsm; lsm++)
		prepare_lsm(*lsm);

	report_lsm_order();

	init_debug("cred blob size       = %d\n", blob_sizes.lbs_cred);
	init_debug("file blob size       = %d\n", blob_sizes.lbs_file);
	init_debug("inode blob size      = %d\n", blob_sizes.lbs_inode);
	init_debug("ipc blob size        = %d\n", blob_sizes.lbs_ipc);
	init_debug("msg_msg blob size    = %d\n", blob_sizes.lbs_msg_msg);
	init_debug("superblock blob size = %d\n", blob_sizes.lbs_superblock);
	init_debug("task blob size       = %d\n", blob_sizes.lbs_task);
	init_debug("xattr slots          = %d\n", blob_sizes.lbs_xattr_count);

	/*
	 * Create any kmem_caches needed for blobs
	 */
	if (blob_sizes.lbs_file)
		lsm_file_cache = kmem_cache_create("lsm_file_cache",
						   blob_sizes.lbs_file, 0,
						   SLAB_PANIC, NULL);
	if (blob_sizes.lbs_inode)
		lsm_inode_cache = kmem_cache_create("lsm_inode_cache",
						    blob_sizes.lbs_inode, 0,
						    SLAB_PANIC, NULL);

	lsm_early_cred((struct cred *) current->cred);
	lsm_early_task(current);
	for (lsm = ordered_lsms; *lsm; lsm++)
		initialize_lsm(*lsm);

	kfree(ordered_lsms);
}

int __init early_security_init(void)
{
	struct lsm_info *lsm;

#define LSM_HOOK(RET, DEFAULT, NAME, ...) \
	INIT_HLIST_HEAD(&security_hook_heads.NAME);
#include "linux/lsm_hook_defs.h"
#undef LSM_HOOK

	for (lsm = __start_early_lsm_info; lsm < __end_early_lsm_info; lsm++) {
		if (!lsm->enabled)
			lsm->enabled = &lsm_enabled_true;
		prepare_lsm(lsm);
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

	init_debug("legacy security=%s\n", chosen_major_lsm ? : " *unspecified*");
	init_debug("  CONFIG_LSM=%s\n", builtin_lsm_order);
	init_debug("boot arg lsm=%s\n", chosen_lsm_order ? : " *unspecified*");

	/*
	 * Append the names of the early LSM modules now that kmalloc() is
	 * available
	 */
	for (lsm = __start_early_lsm_info; lsm < __end_early_lsm_info; lsm++) {
		init_debug("  early started: %s (%s)\n", lsm->name,
			   is_enabled(lsm) ? "enabled" : "disabled");
		if (lsm->enabled)
			lsm_append(lsm->name, &lsm_names);
	}

	/* Load LSMs in specified order. */
	ordered_lsm_init();

	return 0;
}

/* Save user chosen LSM */
static int __init choose_major_lsm(char *str)
{
	chosen_major_lsm = str;
	return 1;
}
__setup("security=", choose_major_lsm);

/* Explicitly choose LSM initialization order. */
static int __init choose_lsm_order(char *str)
{
	chosen_lsm_order = str;
	return 1;
}
__setup("lsm=", choose_lsm_order);

/* Enable LSM order debugging. */
static int __init enable_debug(char *str)
{
	debug = true;
	return 1;
}
__setup("lsm.debug", enable_debug);

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

/**
 * security_add_hooks - Add a modules hooks to the hook lists.
 * @hooks: the hooks to add
 * @count: the number of hooks to add
 * @lsm: the name of the security module
 *
 * Each LSM has to register its hooks with the infrastructure.
 */
void __init security_add_hooks(struct security_hook_list *hooks, int count,
			       const char *lsm)
{
	int i;

	for (i = 0; i < count; i++) {
		hooks[i].lsm = lsm;
		hlist_add_tail_rcu(&hooks[i].list, hooks[i].head);
	}

	/*
	 * Don't try to append during early_security_init(), we'll come back
	 * and fix this up afterwards.
	 */
	if (slab_is_available()) {
		if (lsm_append(lsm, &lsm_names) < 0)
			panic("%s - Cannot get early memory.\n", __func__);
	}
}

int call_blocking_lsm_notifier(enum lsm_event event, void *data)
{
	return blocking_notifier_call_chain(&blocking_lsm_notifier_chain,
					    event, data);
}
EXPORT_SYMBOL(call_blocking_lsm_notifier);

int register_blocking_lsm_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&blocking_lsm_notifier_chain,
						nb);
}
EXPORT_SYMBOL(register_blocking_lsm_notifier);

int unregister_blocking_lsm_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&blocking_lsm_notifier_chain,
						  nb);
}
EXPORT_SYMBOL(unregister_blocking_lsm_notifier);

/**
 * lsm_cred_alloc - allocate a composite cred blob
 * @cred: the cred that needs a blob
 * @gfp: allocation type
 *
 * Allocate the cred blob for all the modules
 *
 * Returns 0, or -ENOMEM if memory can't be allocated.
 */
static int lsm_cred_alloc(struct cred *cred, gfp_t gfp)
{
	if (blob_sizes.lbs_cred == 0) {
		cred->security = NULL;
		return 0;
	}

	cred->security = kzalloc(blob_sizes.lbs_cred, gfp);
	if (cred->security == NULL)
		return -ENOMEM;
	return 0;
}

/**
 * lsm_early_cred - during initialization allocate a composite cred blob
 * @cred: the cred that needs a blob
 *
 * Allocate the cred blob for all the modules
 */
static void __init lsm_early_cred(struct cred *cred)
{
	int rc = lsm_cred_alloc(cred, GFP_KERNEL);

	if (rc)
		panic("%s: Early cred alloc failed.\n", __func__);
}

/**
 * lsm_file_alloc - allocate a composite file blob
 * @file: the file that needs a blob
 *
 * Allocate the file blob for all the modules
 *
 * Returns 0, or -ENOMEM if memory can't be allocated.
 */
static int lsm_file_alloc(struct file *file)
{
	if (!lsm_file_cache) {
		file->f_security = NULL;
		return 0;
	}

	file->f_security = kmem_cache_zalloc(lsm_file_cache, GFP_KERNEL);
	if (file->f_security == NULL)
		return -ENOMEM;
	return 0;
}

/**
 * lsm_inode_alloc - allocate a composite inode blob
 * @inode: the inode that needs a blob
 *
 * Allocate the inode blob for all the modules
 *
 * Returns 0, or -ENOMEM if memory can't be allocated.
 */
int lsm_inode_alloc(struct inode *inode)
{
	if (!lsm_inode_cache) {
		inode->i_security = NULL;
		return 0;
	}

	inode->i_security = kmem_cache_zalloc(lsm_inode_cache, GFP_NOFS);
	if (inode->i_security == NULL)
		return -ENOMEM;
	return 0;
}

/**
 * lsm_task_alloc - allocate a composite task blob
 * @task: the task that needs a blob
 *
 * Allocate the task blob for all the modules
 *
 * Returns 0, or -ENOMEM if memory can't be allocated.
 */
static int lsm_task_alloc(struct task_struct *task)
{
	if (blob_sizes.lbs_task == 0) {
		task->security = NULL;
		return 0;
	}

	task->security = kzalloc(blob_sizes.lbs_task, GFP_KERNEL);
	if (task->security == NULL)
		return -ENOMEM;
	return 0;
}

/**
 * lsm_ipc_alloc - allocate a composite ipc blob
 * @kip: the ipc that needs a blob
 *
 * Allocate the ipc blob for all the modules
 *
 * Returns 0, or -ENOMEM if memory can't be allocated.
 */
static int lsm_ipc_alloc(struct kern_ipc_perm *kip)
{
	if (blob_sizes.lbs_ipc == 0) {
		kip->security = NULL;
		return 0;
	}

	kip->security = kzalloc(blob_sizes.lbs_ipc, GFP_KERNEL);
	if (kip->security == NULL)
		return -ENOMEM;
	return 0;
}

/**
 * lsm_msg_msg_alloc - allocate a composite msg_msg blob
 * @mp: the msg_msg that needs a blob
 *
 * Allocate the ipc blob for all the modules
 *
 * Returns 0, or -ENOMEM if memory can't be allocated.
 */
static int lsm_msg_msg_alloc(struct msg_msg *mp)
{
	if (blob_sizes.lbs_msg_msg == 0) {
		mp->security = NULL;
		return 0;
	}

	mp->security = kzalloc(blob_sizes.lbs_msg_msg, GFP_KERNEL);
	if (mp->security == NULL)
		return -ENOMEM;
	return 0;
}

/**
 * lsm_early_task - during initialization allocate a composite task blob
 * @task: the task that needs a blob
 *
 * Allocate the task blob for all the modules
 */
static void __init lsm_early_task(struct task_struct *task)
{
	int rc = lsm_task_alloc(task);

	if (rc)
		panic("%s: Early task alloc failed.\n", __func__);
}

/**
 * lsm_superblock_alloc - allocate a composite superblock blob
 * @sb: the superblock that needs a blob
 *
 * Allocate the superblock blob for all the modules
 *
 * Returns 0, or -ENOMEM if memory can't be allocated.
 */
static int lsm_superblock_alloc(struct super_block *sb)
{
	if (blob_sizes.lbs_superblock == 0) {
		sb->s_security = NULL;
		return 0;
	}

	sb->s_security = kzalloc(blob_sizes.lbs_superblock, GFP_KERNEL);
	if (sb->s_security == NULL)
		return -ENOMEM;
	return 0;
}

/*
 * The default value of the LSM hook is defined in linux/lsm_hook_defs.h and
 * can be accessed with:
 *
 *	LSM_RET_DEFAULT(<hook_name>)
 *
 * The macros below define static constants for the default value of each
 * LSM hook.
 */
#define LSM_RET_DEFAULT(NAME) (NAME##_default)
#define DECLARE_LSM_RET_DEFAULT_void(DEFAULT, NAME)
#define DECLARE_LSM_RET_DEFAULT_int(DEFAULT, NAME) \
	static const int __maybe_unused LSM_RET_DEFAULT(NAME) = (DEFAULT);
#define LSM_HOOK(RET, DEFAULT, NAME, ...) \
	DECLARE_LSM_RET_DEFAULT_##RET(DEFAULT, NAME)

#include <linux/lsm_hook_defs.h>
#undef LSM_HOOK

/*
 * Hook list operation macros.
 *
 * call_void_hook:
 *	This is a hook that does not return a value.
 *
 * call_int_hook:
 *	This is a hook that returns a value.
 */

#define call_void_hook(FUNC, ...)				\
	do {							\
		struct security_hook_list *P;			\
								\
		hlist_for_each_entry(P, &security_hook_heads.FUNC, list) \
			P->hook.FUNC(__VA_ARGS__);		\
	} while (0)

#define call_int_hook(FUNC, IRC, ...) ({			\
	int RC = IRC;						\
	do {							\
		struct security_hook_list *P;			\
								\
		hlist_for_each_entry(P, &security_hook_heads.FUNC, list) { \
			RC = P->hook.FUNC(__VA_ARGS__);		\
			if (RC != 0)				\
				break;				\
		}						\
	} while (0);						\
	RC;							\
})

/* Security operations */

/**
 * security_binder_set_context_mgr() - Check if becoming binder ctx mgr is ok
 * @mgr: task credentials of current binder process
 *
 * Check whether @mgr is allowed to be the binder context manager.
 *
 * Return: Return 0 if permission is granted.
 */
int security_binder_set_context_mgr(const struct cred *mgr)
{
	return call_int_hook(binder_set_context_mgr, 0, mgr);
}

/**
 * security_binder_transaction() - Check if a binder transaction is allowed
 * @from: sending process
 * @to: receiving process
 *
 * Check whether @from is allowed to invoke a binder transaction call to @to.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_binder_transaction(const struct cred *from,
				const struct cred *to)
{
	return call_int_hook(binder_transaction, 0, from, to);
}

/**
 * security_binder_transfer_binder() - Check if a binder transfer is allowed
 * @from: sending process
 * @to: receiving process
 *
 * Check whether @from is allowed to transfer a binder reference to @to.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_binder_transfer_binder(const struct cred *from,
				    const struct cred *to)
{
	return call_int_hook(binder_transfer_binder, 0, from, to);
}

/**
 * security_binder_transfer_file() - Check if a binder file xfer is allowed
 * @from: sending process
 * @to: receiving process
 * @file: file being transferred
 *
 * Check whether @from is allowed to transfer @file to @to.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_binder_transfer_file(const struct cred *from,
				  const struct cred *to, const struct file *file)
{
	return call_int_hook(binder_transfer_file, 0, from, to, file);
}

/**
 * security_ptrace_access_check() - Check if tracing is allowed
 * @child: target process
 * @mode: PTRACE_MODE flags
 *
 * Check permission before allowing the current process to trace the @child
 * process.  Security modules may also want to perform a process tracing check
 * during an execve in the set_security or apply_creds hooks of tracing check
 * during an execve in the bprm_set_creds hook of binprm_security_ops if the
 * process is being traced and its security attributes would be changed by the
 * execve.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_ptrace_access_check(struct task_struct *child, unsigned int mode)
{
	return call_int_hook(ptrace_access_check, 0, child, mode);
}

/**
 * security_ptrace_traceme() - Check if tracing is allowed
 * @parent: tracing process
 *
 * Check that the @parent process has sufficient permission to trace the
 * current process before allowing the current process to present itself to the
 * @parent process for tracing.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_ptrace_traceme(struct task_struct *parent)
{
	return call_int_hook(ptrace_traceme, 0, parent);
}

/**
 * security_capget() - Get the capability sets for a process
 * @target: target process
 * @effective: effective capability set
 * @inheritable: inheritable capability set
 * @permitted: permitted capability set
 *
 * Get the @effective, @inheritable, and @permitted capability sets for the
 * @target process.  The hook may also perform permission checking to determine
 * if the current process is allowed to see the capability sets of the @target
 * process.
 *
 * Return: Returns 0 if the capability sets were successfully obtained.
 */
int security_capget(const struct task_struct *target,
		    kernel_cap_t *effective,
		    kernel_cap_t *inheritable,
		    kernel_cap_t *permitted)
{
	return call_int_hook(capget, 0, target,
			     effective, inheritable, permitted);
}

/**
 * security_capset() - Set the capability sets for a process
 * @new: new credentials for the target process
 * @old: current credentials of the target process
 * @effective: effective capability set
 * @inheritable: inheritable capability set
 * @permitted: permitted capability set
 *
 * Set the @effective, @inheritable, and @permitted capability sets for the
 * current process.
 *
 * Return: Returns 0 and update @new if permission is granted.
 */
int security_capset(struct cred *new, const struct cred *old,
		    const kernel_cap_t *effective,
		    const kernel_cap_t *inheritable,
		    const kernel_cap_t *permitted)
{
	return call_int_hook(capset, 0, new, old,
			     effective, inheritable, permitted);
}

/**
 * security_capable() - Check if a process has the necessary capability
 * @cred: credentials to examine
 * @ns: user namespace
 * @cap: capability requested
 * @opts: capability check options
 *
 * Check whether the @tsk process has the @cap capability in the indicated
 * credentials.  @cap contains the capability <include/linux/capability.h>.
 * @opts contains options for the capable check <include/linux/security.h>.
 *
 * Return: Returns 0 if the capability is granted.
 */
int security_capable(const struct cred *cred,
		     struct user_namespace *ns,
		     int cap,
		     unsigned int opts)
{
	return call_int_hook(capable, 0, cred, ns, cap, opts);
}

/**
 * security_quotactl() - Check if a quotactl() syscall is allowed for this fs
 * @cmds: commands
 * @type: type
 * @id: id
 * @sb: filesystem
 *
 * Check whether the quotactl syscall is allowed for this @sb.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_quotactl(int cmds, int type, int id, struct super_block *sb)
{
	return call_int_hook(quotactl, 0, cmds, type, id, sb);
}

/**
 * security_quota_on() - Check if QUOTAON is allowed for a dentry
 * @dentry: dentry
 *
 * Check whether QUOTAON is allowed for @dentry.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_quota_on(struct dentry *dentry)
{
	return call_int_hook(quota_on, 0, dentry);
}

/**
 * security_syslog() - Check if accessing the kernel message ring is allowed
 * @type: SYSLOG_ACTION_* type
 *
 * Check permission before accessing the kernel message ring or changing
 * logging to the console.  See the syslog(2) manual page for an explanation of
 * the @type values.
 *
 * Return: Return 0 if permission is granted.
 */
int security_syslog(int type)
{
	return call_int_hook(syslog, 0, type);
}

/**
 * security_settime64() - Check if changing the system time is allowed
 * @ts: new time
 * @tz: timezone
 *
 * Check permission to change the system time, struct timespec64 is defined in
 * <include/linux/time64.h> and timezone is defined in <include/linux/time.h>.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_settime64(const struct timespec64 *ts, const struct timezone *tz)
{
	return call_int_hook(settime, 0, ts, tz);
}

/**
 * security_vm_enough_memory_mm() - Check if allocating a new mem map is allowed
 * @mm: mm struct
 * @pages: number of pages
 *
 * Check permissions for allocating a new virtual mapping.  If all LSMs return
 * a positive value, __vm_enough_memory() will be called with cap_sys_admin
 * set. If at least one LSM returns 0 or negative, __vm_enough_memory() will be
 * called with cap_sys_admin cleared.
 *
 * Return: Returns 0 if permission is granted by the LSM infrastructure to the
 *         caller.
 */
int security_vm_enough_memory_mm(struct mm_struct *mm, long pages)
{
	struct security_hook_list *hp;
	int cap_sys_admin = 1;
	int rc;

	/*
	 * The module will respond with a positive value if
	 * it thinks the __vm_enough_memory() call should be
	 * made with the cap_sys_admin set. If all of the modules
	 * agree that it should be set it will. If any module
	 * thinks it should not be set it won't.
	 */
	hlist_for_each_entry(hp, &security_hook_heads.vm_enough_memory, list) {
		rc = hp->hook.vm_enough_memory(mm, pages);
		if (rc <= 0) {
			cap_sys_admin = 0;
			break;
		}
	}
	return __vm_enough_memory(mm, pages, cap_sys_admin);
}

/**
 * security_bprm_creds_for_exec() - Prepare the credentials for exec()
 * @bprm: binary program information
 *
 * If the setup in prepare_exec_creds did not setup @bprm->cred->security
 * properly for executing @bprm->file, update the LSM's portion of
 * @bprm->cred->security to be what commit_creds needs to install for the new
 * program.  This hook may also optionally check permissions (e.g. for
 * transitions between security domains).  The hook must set @bprm->secureexec
 * to 1 if AT_SECURE should be set to request libc enable secure mode.  @bprm
 * contains the linux_binprm structure.
 *
 * Return: Returns 0 if the hook is successful and permission is granted.
 */
int security_bprm_creds_for_exec(struct linux_binprm *bprm)
{
	return call_int_hook(bprm_creds_for_exec, 0, bprm);
}

/**
 * security_bprm_creds_from_file() - Update linux_binprm creds based on file
 * @bprm: binary program information
 * @file: associated file
 *
 * If @file is setpcap, suid, sgid or otherwise marked to change privilege upon
 * exec, update @bprm->cred to reflect that change. This is called after
 * finding the binary that will be executed without an interpreter.  This
 * ensures that the credentials will not be derived from a script that the
 * binary will need to reopen, which when reopend may end up being a completely
 * different file.  This hook may also optionally check permissions (e.g. for
 * transitions between security domains).  The hook must set @bprm->secureexec
 * to 1 if AT_SECURE should be set to request libc enable secure mode.  The
 * hook must add to @bprm->per_clear any personality flags that should be
 * cleared from current->personality.  @bprm contains the linux_binprm
 * structure.
 *
 * Return: Returns 0 if the hook is successful and permission is granted.
 */
int security_bprm_creds_from_file(struct linux_binprm *bprm, struct file *file)
{
	return call_int_hook(bprm_creds_from_file, 0, bprm, file);
}

/**
 * security_bprm_check() - Mediate binary handler search
 * @bprm: binary program information
 *
 * This hook mediates the point when a search for a binary handler will begin.
 * It allows a check against the @bprm->cred->security value which was set in
 * the preceding creds_for_exec call.  The argv list and envp list are reliably
 * available in @bprm.  This hook may be called multiple times during a single
 * execve.  @bprm contains the linux_binprm structure.
 *
 * Return: Returns 0 if the hook is successful and permission is granted.
 */
int security_bprm_check(struct linux_binprm *bprm)
{
	int ret;

	ret = call_int_hook(bprm_check_security, 0, bprm);
	if (ret)
		return ret;
	return ima_bprm_check(bprm);
}

/**
 * security_bprm_committing_creds() - Install creds for a process during exec()
 * @bprm: binary program information
 *
 * Prepare to install the new security attributes of a process being
 * transformed by an execve operation, based on the old credentials pointed to
 * by @current->cred and the information set in @bprm->cred by the
 * bprm_creds_for_exec hook.  @bprm points to the linux_binprm structure.  This
 * hook is a good place to perform state changes on the process such as closing
 * open file descriptors to which access will no longer be granted when the
 * attributes are changed.  This is called immediately before commit_creds().
 */
void security_bprm_committing_creds(struct linux_binprm *bprm)
{
	call_void_hook(bprm_committing_creds, bprm);
}

/**
 * security_bprm_committed_creds() - Tidy up after cred install during exec()
 * @bprm: binary program information
 *
 * Tidy up after the installation of the new security attributes of a process
 * being transformed by an execve operation.  The new credentials have, by this
 * point, been set to @current->cred.  @bprm points to the linux_binprm
 * structure.  This hook is a good place to perform state changes on the
 * process such as clearing out non-inheritable signal state.  This is called
 * immediately after commit_creds().
 */
void security_bprm_committed_creds(struct linux_binprm *bprm)
{
	call_void_hook(bprm_committed_creds, bprm);
}

/**
 * security_fs_context_submount() - Initialise fc->security
 * @fc: new filesystem context
 * @reference: dentry reference for submount/remount
 *
 * Fill out the ->security field for a new fs_context.
 *
 * Return: Returns 0 on success or negative error code on failure.
 */
int security_fs_context_submount(struct fs_context *fc, struct super_block *reference)
{
	return call_int_hook(fs_context_submount, 0, fc, reference);
}

/**
 * security_fs_context_dup() - Duplicate a fs_context LSM blob
 * @fc: destination filesystem context
 * @src_fc: source filesystem context
 *
 * Allocate and attach a security structure to sc->security.  This pointer is
 * initialised to NULL by the caller.  @fc indicates the new filesystem context.
 * @src_fc indicates the original filesystem context.
 *
 * Return: Returns 0 on success or a negative error code on failure.
 */
int security_fs_context_dup(struct fs_context *fc, struct fs_context *src_fc)
{
	return call_int_hook(fs_context_dup, 0, fc, src_fc);
}

/**
 * security_fs_context_parse_param() - Configure a filesystem context
 * @fc: filesystem context
 * @param: filesystem parameter
 *
 * Userspace provided a parameter to configure a superblock.  The LSM can
 * consume the parameter or return it to the caller for use elsewhere.
 *
 * Return: If the parameter is used by the LSM it should return 0, if it is
 *         returned to the caller -ENOPARAM is returned, otherwise a negative
 *         error code is returned.
 */
int security_fs_context_parse_param(struct fs_context *fc,
				    struct fs_parameter *param)
{
	struct security_hook_list *hp;
	int trc;
	int rc = -ENOPARAM;

	hlist_for_each_entry(hp, &security_hook_heads.fs_context_parse_param,
			     list) {
		trc = hp->hook.fs_context_parse_param(fc, param);
		if (trc == 0)
			rc = 0;
		else if (trc != -ENOPARAM)
			return trc;
	}
	return rc;
}

/**
 * security_sb_alloc() - Allocate a super_block LSM blob
 * @sb: filesystem superblock
 *
 * Allocate and attach a security structure to the sb->s_security field.  The
 * s_security field is initialized to NULL when the structure is allocated.
 * @sb contains the super_block structure to be modified.
 *
 * Return: Returns 0 if operation was successful.
 */
int security_sb_alloc(struct super_block *sb)
{
	int rc = lsm_superblock_alloc(sb);

	if (unlikely(rc))
		return rc;
	rc = call_int_hook(sb_alloc_security, 0, sb);
	if (unlikely(rc))
		security_sb_free(sb);
	return rc;
}

/**
 * security_sb_delete() - Release super_block LSM associated objects
 * @sb: filesystem superblock
 *
 * Release objects tied to a superblock (e.g. inodes).  @sb contains the
 * super_block structure being released.
 */
void security_sb_delete(struct super_block *sb)
{
	call_void_hook(sb_delete, sb);
}

/**
 * security_sb_free() - Free a super_block LSM blob
 * @sb: filesystem superblock
 *
 * Deallocate and clear the sb->s_security field.  @sb contains the super_block
 * structure to be modified.
 */
void security_sb_free(struct super_block *sb)
{
	call_void_hook(sb_free_security, sb);
	kfree(sb->s_security);
	sb->s_security = NULL;
}

/**
 * security_free_mnt_opts() - Free memory associated with mount options
 * @mnt_opts: LSM processed mount options
 *
 * Free memory associated with @mnt_ops.
 */
void security_free_mnt_opts(void **mnt_opts)
{
	if (!*mnt_opts)
		return;
	call_void_hook(sb_free_mnt_opts, *mnt_opts);
	*mnt_opts = NULL;
}
EXPORT_SYMBOL(security_free_mnt_opts);

/**
 * security_sb_eat_lsm_opts() - Consume LSM mount options
 * @options: mount options
 * @mnt_opts: LSM processed mount options
 *
 * Eat (scan @options) and save them in @mnt_opts.
 *
 * Return: Returns 0 on success, negative values on failure.
 */
int security_sb_eat_lsm_opts(char *options, void **mnt_opts)
{
	return call_int_hook(sb_eat_lsm_opts, 0, options, mnt_opts);
}
EXPORT_SYMBOL(security_sb_eat_lsm_opts);

/**
 * security_sb_mnt_opts_compat() - Check if new mount options are allowed
 * @sb: filesystem superblock
 * @mnt_opts: new mount options
 *
 * Determine if the new mount options in @mnt_opts are allowed given the
 * existing mounted filesystem at @sb.  @sb superblock being compared.
 *
 * Return: Returns 0 if options are compatible.
 */
int security_sb_mnt_opts_compat(struct super_block *sb,
				void *mnt_opts)
{
	return call_int_hook(sb_mnt_opts_compat, 0, sb, mnt_opts);
}
EXPORT_SYMBOL(security_sb_mnt_opts_compat);

/**
 * security_sb_remount() - Verify no incompatible mount changes during remount
 * @sb: filesystem superblock
 * @mnt_opts: (re)mount options
 *
 * Extracts security system specific mount options and verifies no changes are
 * being made to those options.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_sb_remount(struct super_block *sb,
			void *mnt_opts)
{
	return call_int_hook(sb_remount, 0, sb, mnt_opts);
}
EXPORT_SYMBOL(security_sb_remount);

/**
 * security_sb_kern_mount() - Check if a kernel mount is allowed
 * @sb: filesystem superblock
 *
 * Mount this @sb if allowed by permissions.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_sb_kern_mount(struct super_block *sb)
{
	return call_int_hook(sb_kern_mount, 0, sb);
}

/**
 * security_sb_show_options() - Output the mount options for a superblock
 * @m: output file
 * @sb: filesystem superblock
 *
 * Show (print on @m) mount options for this @sb.
 *
 * Return: Returns 0 on success, negative values on failure.
 */
int security_sb_show_options(struct seq_file *m, struct super_block *sb)
{
	return call_int_hook(sb_show_options, 0, m, sb);
}

/**
 * security_sb_statfs() - Check if accessing fs stats is allowed
 * @dentry: superblock handle
 *
 * Check permission before obtaining filesystem statistics for the @mnt
 * mountpoint.  @dentry is a handle on the superblock for the filesystem.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_sb_statfs(struct dentry *dentry)
{
	return call_int_hook(sb_statfs, 0, dentry);
}

/**
 * security_sb_mount() - Check permission for mounting a filesystem
 * @dev_name: filesystem backing device
 * @path: mount point
 * @type: filesystem type
 * @flags: mount flags
 * @data: filesystem specific data
 *
 * Check permission before an object specified by @dev_name is mounted on the
 * mount point named by @nd.  For an ordinary mount, @dev_name identifies a
 * device if the file system type requires a device.  For a remount
 * (@flags & MS_REMOUNT), @dev_name is irrelevant.  For a loopback/bind mount
 * (@flags & MS_BIND), @dev_name identifies the	pathname of the object being
 * mounted.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_sb_mount(const char *dev_name, const struct path *path,
		      const char *type, unsigned long flags, void *data)
{
	return call_int_hook(sb_mount, 0, dev_name, path, type, flags, data);
}

/**
 * security_sb_umount() - Check permission for unmounting a filesystem
 * @mnt: mounted filesystem
 * @flags: unmount flags
 *
 * Check permission before the @mnt file system is unmounted.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_sb_umount(struct vfsmount *mnt, int flags)
{
	return call_int_hook(sb_umount, 0, mnt, flags);
}

/**
 * security_sb_pivotroot() - Check permissions for pivoting the rootfs
 * @old_path: new location for current rootfs
 * @new_path: location of the new rootfs
 *
 * Check permission before pivoting the root filesystem.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_sb_pivotroot(const struct path *old_path,
			  const struct path *new_path)
{
	return call_int_hook(sb_pivotroot, 0, old_path, new_path);
}

/**
 * security_sb_set_mnt_opts() - Set the mount options for a filesystem
 * @sb: filesystem superblock
 * @mnt_opts: binary mount options
 * @kern_flags: kernel flags (in)
 * @set_kern_flags: kernel flags (out)
 *
 * Set the security relevant mount options used for a superblock.
 *
 * Return: Returns 0 on success, error on failure.
 */
int security_sb_set_mnt_opts(struct super_block *sb,
			     void *mnt_opts,
			     unsigned long kern_flags,
			     unsigned long *set_kern_flags)
{
	return call_int_hook(sb_set_mnt_opts,
			     mnt_opts ? -EOPNOTSUPP : 0, sb,
			     mnt_opts, kern_flags, set_kern_flags);
}
EXPORT_SYMBOL(security_sb_set_mnt_opts);

/**
 * security_sb_clone_mnt_opts() - Duplicate superblock mount options
 * @oldsb: source superblock
 * @newsb: destination superblock
 * @kern_flags: kernel flags (in)
 * @set_kern_flags: kernel flags (out)
 *
 * Copy all security options from a given superblock to another.
 *
 * Return: Returns 0 on success, error on failure.
 */
int security_sb_clone_mnt_opts(const struct super_block *oldsb,
			       struct super_block *newsb,
			       unsigned long kern_flags,
			       unsigned long *set_kern_flags)
{
	return call_int_hook(sb_clone_mnt_opts, 0, oldsb, newsb,
			     kern_flags, set_kern_flags);
}
EXPORT_SYMBOL(security_sb_clone_mnt_opts);

/**
 * security_move_mount() - Check permissions for moving a mount
 * @from_path: source mount point
 * @to_path: destination mount point
 *
 * Check permission before a mount is moved.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_move_mount(const struct path *from_path,
			const struct path *to_path)
{
	return call_int_hook(move_mount, 0, from_path, to_path);
}

/**
 * security_path_notify() - Check if setting a watch is allowed
 * @path: file path
 * @mask: event mask
 * @obj_type: file path type
 *
 * Check permissions before setting a watch on events as defined by @mask, on
 * an object at @path, whose type is defined by @obj_type.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_path_notify(const struct path *path, u64 mask,
			 unsigned int obj_type)
{
	return call_int_hook(path_notify, 0, path, mask, obj_type);
}

/**
 * security_inode_alloc() - Allocate an inode LSM blob
 * @inode: the inode
 *
 * Allocate and attach a security structure to @inode->i_security.  The
 * i_security field is initialized to NULL when the inode structure is
 * allocated.
 *
 * Return: Return 0 if operation was successful.
 */
int security_inode_alloc(struct inode *inode)
{
	int rc = lsm_inode_alloc(inode);

	if (unlikely(rc))
		return rc;
	rc = call_int_hook(inode_alloc_security, 0, inode);
	if (unlikely(rc))
		security_inode_free(inode);
	return rc;
}

static void inode_free_by_rcu(struct rcu_head *head)
{
	/*
	 * The rcu head is at the start of the inode blob
	 */
	kmem_cache_free(lsm_inode_cache, head);
}

/**
 * security_inode_free() - Free an inode's LSM blob
 * @inode: the inode
 *
 * Deallocate the inode security structure and set @inode->i_security to NULL.
 */
void security_inode_free(struct inode *inode)
{
	integrity_inode_free(inode);
	call_void_hook(inode_free_security, inode);
	/*
	 * The inode may still be referenced in a path walk and
	 * a call to security_inode_permission() can be made
	 * after inode_free_security() is called. Ideally, the VFS
	 * wouldn't do this, but fixing that is a much harder
	 * job. For now, simply free the i_security via RCU, and
	 * leave the current inode->i_security pointer intact.
	 * The inode will be freed after the RCU grace period too.
	 */
	if (inode->i_security)
		call_rcu((struct rcu_head *)inode->i_security,
			 inode_free_by_rcu);
}

/**
 * security_dentry_init_security() - Perform dentry initialization
 * @dentry: the dentry to initialize
 * @mode: mode used to determine resource type
 * @name: name of the last path component
 * @xattr_name: name of the security/LSM xattr
 * @ctx: pointer to the resulting LSM context
 * @ctxlen: length of @ctx
 *
 * Compute a context for a dentry as the inode is not yet available since NFSv4
 * has no label backed by an EA anyway.  It is important to note that
 * @xattr_name does not need to be free'd by the caller, it is a static string.
 *
 * Return: Returns 0 on success, negative values on failure.
 */
int security_dentry_init_security(struct dentry *dentry, int mode,
				  const struct qstr *name,
				  const char **xattr_name, void **ctx,
				  u32 *ctxlen)
{
	struct security_hook_list *hp;
	int rc;

	/*
	 * Only one module will provide a security context.
	 */
	hlist_for_each_entry(hp, &security_hook_heads.dentry_init_security,
			     list) {
		rc = hp->hook.dentry_init_security(dentry, mode, name,
						   xattr_name, ctx, ctxlen);
		if (rc != LSM_RET_DEFAULT(dentry_init_security))
			return rc;
	}
	return LSM_RET_DEFAULT(dentry_init_security);
}
EXPORT_SYMBOL(security_dentry_init_security);

/**
 * security_dentry_create_files_as() - Perform dentry initialization
 * @dentry: the dentry to initialize
 * @mode: mode used to determine resource type
 * @name: name of the last path component
 * @old: creds to use for LSM context calculations
 * @new: creds to modify
 *
 * Compute a context for a dentry as the inode is not yet available and set
 * that context in passed in creds so that new files are created using that
 * context. Context is calculated using the passed in creds and not the creds
 * of the caller.
 *
 * Return: Returns 0 on success, error on failure.
 */
int security_dentry_create_files_as(struct dentry *dentry, int mode,
				    struct qstr *name,
				    const struct cred *old, struct cred *new)
{
	return call_int_hook(dentry_create_files_as, 0, dentry, mode,
			     name, old, new);
}
EXPORT_SYMBOL(security_dentry_create_files_as);

/**
 * security_inode_init_security() - Initialize an inode's LSM context
 * @inode: the inode
 * @dir: parent directory
 * @qstr: last component of the pathname
 * @initxattrs: callback function to write xattrs
 * @fs_data: filesystem specific data
 *
 * Obtain the security attribute name suffix and value to set on a newly
 * created inode and set up the incore security field for the new inode.  This
 * hook is called by the fs code as part of the inode creation transaction and
 * provides for atomic labeling of the inode, unlike the post_create/mkdir/...
 * hooks called by the VFS.
 *
 * The hook function is expected to populate the xattrs array, by calling
 * lsm_get_xattr_slot() to retrieve the slots reserved by the security module
 * with the lbs_xattr_count field of the lsm_blob_sizes structure.  For each
 * slot, the hook function should set ->name to the attribute name suffix
 * (e.g. selinux), to allocate ->value (will be freed by the caller) and set it
 * to the attribute value, to set ->value_len to the length of the value.  If
 * the security module does not use security attributes or does not wish to put
 * a security attribute on this particular inode, then it should return
 * -EOPNOTSUPP to skip this processing.
 *
 * Return: Returns 0 if the LSM successfully initialized all of the inode
 *         security attributes that are required, negative values otherwise.
 */
int security_inode_init_security(struct inode *inode, struct inode *dir,
				 const struct qstr *qstr,
				 const initxattrs initxattrs, void *fs_data)
{
	struct security_hook_list *hp;
	struct xattr *new_xattrs = NULL;
	int ret = -EOPNOTSUPP, xattr_count = 0;

	if (unlikely(IS_PRIVATE(inode)))
		return 0;

	if (!blob_sizes.lbs_xattr_count)
		return 0;

	if (initxattrs) {
		/* Allocate +1 for EVM and +1 as terminator. */
		new_xattrs = kcalloc(blob_sizes.lbs_xattr_count + 2,
				     sizeof(*new_xattrs), GFP_NOFS);
		if (!new_xattrs)
			return -ENOMEM;
	}

	hlist_for_each_entry(hp, &security_hook_heads.inode_init_security,
			     list) {
		ret = hp->hook.inode_init_security(inode, dir, qstr, new_xattrs,
						  &xattr_count);
		if (ret && ret != -EOPNOTSUPP)
			goto out;
		/*
		 * As documented in lsm_hooks.h, -EOPNOTSUPP in this context
		 * means that the LSM is not willing to provide an xattr, not
		 * that it wants to signal an error. Thus, continue to invoke
		 * the remaining LSMs.
		 */
	}

	/* If initxattrs() is NULL, xattr_count is zero, skip the call. */
	if (!xattr_count)
		goto out;

	ret = evm_inode_init_security(inode, dir, qstr, new_xattrs,
				      &xattr_count);
	if (ret)
		goto out;
	ret = initxattrs(inode, new_xattrs, fs_data);
out:
	for (; xattr_count > 0; xattr_count--)
		kfree(new_xattrs[xattr_count - 1].value);
	kfree(new_xattrs);
	return (ret == -EOPNOTSUPP) ? 0 : ret;
}
EXPORT_SYMBOL(security_inode_init_security);

/**
 * security_inode_init_security_anon() - Initialize an anonymous inode
 * @inode: the inode
 * @name: the anonymous inode class
 * @context_inode: an optional related inode
 *
 * Set up the incore security field for the new anonymous inode and return
 * whether the inode creation is permitted by the security module or not.
 *
 * Return: Returns 0 on success, -EACCES if the security module denies the
 * creation of this inode, or another -errno upon other errors.
 */
int security_inode_init_security_anon(struct inode *inode,
				      const struct qstr *name,
				      const struct inode *context_inode)
{
	return call_int_hook(inode_init_security_anon, 0, inode, name,
			     context_inode);
}

#ifdef CONFIG_SECURITY_PATH
/**
 * security_path_mknod() - Check if creating a special file is allowed
 * @dir: parent directory
 * @dentry: new file
 * @mode: new file mode
 * @dev: device number
 *
 * Check permissions when creating a file. Note that this hook is called even
 * if mknod operation is being done for a regular file.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_path_mknod(const struct path *dir, struct dentry *dentry,
			umode_t mode, unsigned int dev)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(dir->dentry))))
		return 0;
	return call_int_hook(path_mknod, 0, dir, dentry, mode, dev);
}
EXPORT_SYMBOL(security_path_mknod);

/**
 * security_path_mkdir() - Check if creating a new directory is allowed
 * @dir: parent directory
 * @dentry: new directory
 * @mode: new directory mode
 *
 * Check permissions to create a new directory in the existing directory.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_path_mkdir(const struct path *dir, struct dentry *dentry,
			umode_t mode)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(dir->dentry))))
		return 0;
	return call_int_hook(path_mkdir, 0, dir, dentry, mode);
}
EXPORT_SYMBOL(security_path_mkdir);

/**
 * security_path_rmdir() - Check if removing a directory is allowed
 * @dir: parent directory
 * @dentry: directory to remove
 *
 * Check the permission to remove a directory.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_path_rmdir(const struct path *dir, struct dentry *dentry)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(dir->dentry))))
		return 0;
	return call_int_hook(path_rmdir, 0, dir, dentry);
}

/**
 * security_path_unlink() - Check if removing a hard link is allowed
 * @dir: parent directory
 * @dentry: file
 *
 * Check the permission to remove a hard link to a file.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_path_unlink(const struct path *dir, struct dentry *dentry)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(dir->dentry))))
		return 0;
	return call_int_hook(path_unlink, 0, dir, dentry);
}
EXPORT_SYMBOL(security_path_unlink);

/**
 * security_path_symlink() - Check if creating a symbolic link is allowed
 * @dir: parent directory
 * @dentry: symbolic link
 * @old_name: file pathname
 *
 * Check the permission to create a symbolic link to a file.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_path_symlink(const struct path *dir, struct dentry *dentry,
			  const char *old_name)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(dir->dentry))))
		return 0;
	return call_int_hook(path_symlink, 0, dir, dentry, old_name);
}

/**
 * security_path_link - Check if creating a hard link is allowed
 * @old_dentry: existing file
 * @new_dir: new parent directory
 * @new_dentry: new link
 *
 * Check permission before creating a new hard link to a file.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_path_link(struct dentry *old_dentry, const struct path *new_dir,
		       struct dentry *new_dentry)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(old_dentry))))
		return 0;
	return call_int_hook(path_link, 0, old_dentry, new_dir, new_dentry);
}

/**
 * security_path_rename() - Check if renaming a file is allowed
 * @old_dir: parent directory of the old file
 * @old_dentry: the old file
 * @new_dir: parent directory of the new file
 * @new_dentry: the new file
 * @flags: flags
 *
 * Check for permission to rename a file or directory.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_path_rename(const struct path *old_dir, struct dentry *old_dentry,
			 const struct path *new_dir, struct dentry *new_dentry,
			 unsigned int flags)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(old_dentry)) ||
		     (d_is_positive(new_dentry) &&
		      IS_PRIVATE(d_backing_inode(new_dentry)))))
		return 0;

	return call_int_hook(path_rename, 0, old_dir, old_dentry, new_dir,
			     new_dentry, flags);
}
EXPORT_SYMBOL(security_path_rename);

/**
 * security_path_truncate() - Check if truncating a file is allowed
 * @path: file
 *
 * Check permission before truncating the file indicated by path.  Note that
 * truncation permissions may also be checked based on already opened files,
 * using the security_file_truncate() hook.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_path_truncate(const struct path *path)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(path->dentry))))
		return 0;
	return call_int_hook(path_truncate, 0, path);
}

/**
 * security_path_chmod() - Check if changing the file's mode is allowed
 * @path: file
 * @mode: new mode
 *
 * Check for permission to change a mode of the file @path. The new mode is
 * specified in @mode which is a bitmask of constants from
 * <include/uapi/linux/stat.h>.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_path_chmod(const struct path *path, umode_t mode)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(path->dentry))))
		return 0;
	return call_int_hook(path_chmod, 0, path, mode);
}

/**
 * security_path_chown() - Check if changing the file's owner/group is allowed
 * @path: file
 * @uid: file owner
 * @gid: file group
 *
 * Check for permission to change owner/group of a file or directory.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_path_chown(const struct path *path, kuid_t uid, kgid_t gid)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(path->dentry))))
		return 0;
	return call_int_hook(path_chown, 0, path, uid, gid);
}

/**
 * security_path_chroot() - Check if changing the root directory is allowed
 * @path: directory
 *
 * Check for permission to change root directory.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_path_chroot(const struct path *path)
{
	return call_int_hook(path_chroot, 0, path);
}
#endif /* CONFIG_SECURITY_PATH */

/**
 * security_inode_create() - Check if creating a file is allowed
 * @dir: the parent directory
 * @dentry: the file being created
 * @mode: requested file mode
 *
 * Check permission to create a regular file.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_inode_create(struct inode *dir, struct dentry *dentry,
			  umode_t mode)
{
	if (unlikely(IS_PRIVATE(dir)))
		return 0;
	return call_int_hook(inode_create, 0, dir, dentry, mode);
}
EXPORT_SYMBOL_GPL(security_inode_create);

/**
 * security_inode_link() - Check if creating a hard link is allowed
 * @old_dentry: existing file
 * @dir: new parent directory
 * @new_dentry: new link
 *
 * Check permission before creating a new hard link to a file.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_inode_link(struct dentry *old_dentry, struct inode *dir,
			struct dentry *new_dentry)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(old_dentry))))
		return 0;
	return call_int_hook(inode_link, 0, old_dentry, dir, new_dentry);
}

/**
 * security_inode_unlink() - Check if removing a hard link is allowed
 * @dir: parent directory
 * @dentry: file
 *
 * Check the permission to remove a hard link to a file.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_inode_unlink(struct inode *dir, struct dentry *dentry)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(dentry))))
		return 0;
	return call_int_hook(inode_unlink, 0, dir, dentry);
}

/**
 * security_inode_symlink() - Check if creating a symbolic link is allowed
 * @dir: parent directory
 * @dentry: symbolic link
 * @old_name: existing filename
 *
 * Check the permission to create a symbolic link to a file.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_inode_symlink(struct inode *dir, struct dentry *dentry,
			   const char *old_name)
{
	if (unlikely(IS_PRIVATE(dir)))
		return 0;
	return call_int_hook(inode_symlink, 0, dir, dentry, old_name);
}

/**
 * security_inode_mkdir() - Check if creation a new director is allowed
 * @dir: parent directory
 * @dentry: new directory
 * @mode: new directory mode
 *
 * Check permissions to create a new directory in the existing directory
 * associated with inode structure @dir.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_inode_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	if (unlikely(IS_PRIVATE(dir)))
		return 0;
	return call_int_hook(inode_mkdir, 0, dir, dentry, mode);
}
EXPORT_SYMBOL_GPL(security_inode_mkdir);

/**
 * security_inode_rmdir() - Check if removing a directory is allowed
 * @dir: parent directory
 * @dentry: directory to be removed
 *
 * Check the permission to remove a directory.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_inode_rmdir(struct inode *dir, struct dentry *dentry)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(dentry))))
		return 0;
	return call_int_hook(inode_rmdir, 0, dir, dentry);
}

/**
 * security_inode_mknod() - Check if creating a special file is allowed
 * @dir: parent directory
 * @dentry: new file
 * @mode: new file mode
 * @dev: device number
 *
 * Check permissions when creating a special file (or a socket or a fifo file
 * created via the mknod system call).  Note that if mknod operation is being
 * done for a regular file, then the create hook will be called and not this
 * hook.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_inode_mknod(struct inode *dir, struct dentry *dentry,
			 umode_t mode, dev_t dev)
{
	if (unlikely(IS_PRIVATE(dir)))
		return 0;
	return call_int_hook(inode_mknod, 0, dir, dentry, mode, dev);
}

/**
 * security_inode_rename() - Check if renaming a file is allowed
 * @old_dir: parent directory of the old file
 * @old_dentry: the old file
 * @new_dir: parent directory of the new file
 * @new_dentry: the new file
 * @flags: flags
 *
 * Check for permission to rename a file or directory.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_inode_rename(struct inode *old_dir, struct dentry *old_dentry,
			  struct inode *new_dir, struct dentry *new_dentry,
			  unsigned int flags)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(old_dentry)) ||
		     (d_is_positive(new_dentry) &&
		      IS_PRIVATE(d_backing_inode(new_dentry)))))
		return 0;

	if (flags & RENAME_EXCHANGE) {
		int err = call_int_hook(inode_rename, 0, new_dir, new_dentry,
					old_dir, old_dentry);
		if (err)
			return err;
	}

	return call_int_hook(inode_rename, 0, old_dir, old_dentry,
			     new_dir, new_dentry);
}

/**
 * security_inode_readlink() - Check if reading a symbolic link is allowed
 * @dentry: link
 *
 * Check the permission to read the symbolic link.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_inode_readlink(struct dentry *dentry)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(dentry))))
		return 0;
	return call_int_hook(inode_readlink, 0, dentry);
}

/**
 * security_inode_follow_link() - Check if following a symbolic link is allowed
 * @dentry: link dentry
 * @inode: link inode
 * @rcu: true if in RCU-walk mode
 *
 * Check permission to follow a symbolic link when looking up a pathname.  If
 * @rcu is true, @inode is not stable.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_inode_follow_link(struct dentry *dentry, struct inode *inode,
			       bool rcu)
{
	if (unlikely(IS_PRIVATE(inode)))
		return 0;
	return call_int_hook(inode_follow_link, 0, dentry, inode, rcu);
}

/**
 * security_inode_permission() - Check if accessing an inode is allowed
 * @inode: inode
 * @mask: access mask
 *
 * Check permission before accessing an inode.  This hook is called by the
 * existing Linux permission function, so a security module can use it to
 * provide additional checking for existing Linux permission checks.  Notice
 * that this hook is called when a file is opened (as well as many other
 * operations), whereas the file_security_ops permission hook is called when
 * the actual read/write operations are performed.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_inode_permission(struct inode *inode, int mask)
{
	if (unlikely(IS_PRIVATE(inode)))
		return 0;
	return call_int_hook(inode_permission, 0, inode, mask);
}

/**
 * security_inode_setattr() - Check if setting file attributes is allowed
 * @idmap: idmap of the mount
 * @dentry: file
 * @attr: new attributes
 *
 * Check permission before setting file attributes.  Note that the kernel call
 * to notify_change is performed from several locations, whenever file
 * attributes change (such as when a file is truncated, chown/chmod operations,
 * transferring disk quotas, etc).
 *
 * Return: Returns 0 if permission is granted.
 */
int security_inode_setattr(struct mnt_idmap *idmap,
			   struct dentry *dentry, struct iattr *attr)
{
	int ret;

	if (unlikely(IS_PRIVATE(d_backing_inode(dentry))))
		return 0;
	ret = call_int_hook(inode_setattr, 0, dentry, attr);
	if (ret)
		return ret;
	return evm_inode_setattr(idmap, dentry, attr);
}
EXPORT_SYMBOL_GPL(security_inode_setattr);

/**
 * security_inode_getattr() - Check if getting file attributes is allowed
 * @path: file
 *
 * Check permission before obtaining file attributes.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_inode_getattr(const struct path *path)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(path->dentry))))
		return 0;
	return call_int_hook(inode_getattr, 0, path);
}

/**
 * security_inode_setxattr() - Check if setting file xattrs is allowed
 * @idmap: idmap of the mount
 * @dentry: file
 * @name: xattr name
 * @value: xattr value
 * @size: size of xattr value
 * @flags: flags
 *
 * Check permission before setting the extended attributes.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_inode_setxattr(struct mnt_idmap *idmap,
			    struct dentry *dentry, const char *name,
			    const void *value, size_t size, int flags)
{
	int ret;

	if (unlikely(IS_PRIVATE(d_backing_inode(dentry))))
		return 0;
	/*
	 * SELinux and Smack integrate the cap call,
	 * so assume that all LSMs supplying this call do so.
	 */
	ret = call_int_hook(inode_setxattr, 1, idmap, dentry, name, value,
			    size, flags);

	if (ret == 1)
		ret = cap_inode_setxattr(dentry, name, value, size, flags);
	if (ret)
		return ret;
	ret = ima_inode_setxattr(dentry, name, value, size);
	if (ret)
		return ret;
	return evm_inode_setxattr(idmap, dentry, name, value, size);
}

/**
 * security_inode_set_acl() - Check if setting posix acls is allowed
 * @idmap: idmap of the mount
 * @dentry: file
 * @acl_name: acl name
 * @kacl: acl struct
 *
 * Check permission before setting posix acls, the posix acls in @kacl are
 * identified by @acl_name.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_inode_set_acl(struct mnt_idmap *idmap,
			   struct dentry *dentry, const char *acl_name,
			   struct posix_acl *kacl)
{
	int ret;

	if (unlikely(IS_PRIVATE(d_backing_inode(dentry))))
		return 0;
	ret = call_int_hook(inode_set_acl, 0, idmap, dentry, acl_name,
			    kacl);
	if (ret)
		return ret;
	ret = ima_inode_set_acl(idmap, dentry, acl_name, kacl);
	if (ret)
		return ret;
	return evm_inode_set_acl(idmap, dentry, acl_name, kacl);
}

/**
 * security_inode_get_acl() - Check if reading posix acls is allowed
 * @idmap: idmap of the mount
 * @dentry: file
 * @acl_name: acl name
 *
 * Check permission before getting osix acls, the posix acls are identified by
 * @acl_name.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_inode_get_acl(struct mnt_idmap *idmap,
			   struct dentry *dentry, const char *acl_name)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(dentry))))
		return 0;
	return call_int_hook(inode_get_acl, 0, idmap, dentry, acl_name);
}

/**
 * security_inode_remove_acl() - Check if removing a posix acl is allowed
 * @idmap: idmap of the mount
 * @dentry: file
 * @acl_name: acl name
 *
 * Check permission before removing posix acls, the posix acls are identified
 * by @acl_name.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_inode_remove_acl(struct mnt_idmap *idmap,
			      struct dentry *dentry, const char *acl_name)
{
	int ret;

	if (unlikely(IS_PRIVATE(d_backing_inode(dentry))))
		return 0;
	ret = call_int_hook(inode_remove_acl, 0, idmap, dentry, acl_name);
	if (ret)
		return ret;
	ret = ima_inode_remove_acl(idmap, dentry, acl_name);
	if (ret)
		return ret;
	return evm_inode_remove_acl(idmap, dentry, acl_name);
}

/**
 * security_inode_post_setxattr() - Update the inode after a setxattr operation
 * @dentry: file
 * @name: xattr name
 * @value: xattr value
 * @size: xattr value size
 * @flags: flags
 *
 * Update inode security field after successful setxattr operation.
 */
void security_inode_post_setxattr(struct dentry *dentry, const char *name,
				  const void *value, size_t size, int flags)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(dentry))))
		return;
	call_void_hook(inode_post_setxattr, dentry, name, value, size, flags);
	evm_inode_post_setxattr(dentry, name, value, size);
}

/**
 * security_inode_getxattr() - Check if xattr access is allowed
 * @dentry: file
 * @name: xattr name
 *
 * Check permission before obtaining the extended attributes identified by
 * @name for @dentry.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_inode_getxattr(struct dentry *dentry, const char *name)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(dentry))))
		return 0;
	return call_int_hook(inode_getxattr, 0, dentry, name);
}

/**
 * security_inode_listxattr() - Check if listing xattrs is allowed
 * @dentry: file
 *
 * Check permission before obtaining the list of extended attribute names for
 * @dentry.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_inode_listxattr(struct dentry *dentry)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(dentry))))
		return 0;
	return call_int_hook(inode_listxattr, 0, dentry);
}

/**
 * security_inode_removexattr() - Check if removing an xattr is allowed
 * @idmap: idmap of the mount
 * @dentry: file
 * @name: xattr name
 *
 * Check permission before removing the extended attribute identified by @name
 * for @dentry.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_inode_removexattr(struct mnt_idmap *idmap,
			       struct dentry *dentry, const char *name)
{
	int ret;

	if (unlikely(IS_PRIVATE(d_backing_inode(dentry))))
		return 0;
	/*
	 * SELinux and Smack integrate the cap call,
	 * so assume that all LSMs supplying this call do so.
	 */
	ret = call_int_hook(inode_removexattr, 1, idmap, dentry, name);
	if (ret == 1)
		ret = cap_inode_removexattr(idmap, dentry, name);
	if (ret)
		return ret;
	ret = ima_inode_removexattr(dentry, name);
	if (ret)
		return ret;
	return evm_inode_removexattr(idmap, dentry, name);
}

/**
 * security_inode_need_killpriv() - Check if security_inode_killpriv() required
 * @dentry: associated dentry
 *
 * Called when an inode has been changed to determine if
 * security_inode_killpriv() should be called.
 *
 * Return: Return <0 on error to abort the inode change operation, return 0 if
 *         security_inode_killpriv() does not need to be called, return >0 if
 *         security_inode_killpriv() does need to be called.
 */
int security_inode_need_killpriv(struct dentry *dentry)
{
	return call_int_hook(inode_need_killpriv, 0, dentry);
}

/**
 * security_inode_killpriv() - The setuid bit is removed, update LSM state
 * @idmap: idmap of the mount
 * @dentry: associated dentry
 *
 * The @dentry's setuid bit is being removed.  Remove similar security labels.
 * Called with the dentry->d_inode->i_mutex held.
 *
 * Return: Return 0 on success.  If error is returned, then the operation
 *         causing setuid bit removal is failed.
 */
int security_inode_killpriv(struct mnt_idmap *idmap,
			    struct dentry *dentry)
{
	return call_int_hook(inode_killpriv, 0, idmap, dentry);
}

/**
 * security_inode_getsecurity() - Get the xattr security label of an inode
 * @idmap: idmap of the mount
 * @inode: inode
 * @name: xattr name
 * @buffer: security label buffer
 * @alloc: allocation flag
 *
 * Retrieve a copy of the extended attribute representation of the security
 * label associated with @name for @inode via @buffer.  Note that @name is the
 * remainder of the attribute name after the security prefix has been removed.
 * @alloc is used to specify if the call should return a value via the buffer
 * or just the value length.
 *
 * Return: Returns size of buffer on success.
 */
int security_inode_getsecurity(struct mnt_idmap *idmap,
			       struct inode *inode, const char *name,
			       void **buffer, bool alloc)
{
	struct security_hook_list *hp;
	int rc;

	if (unlikely(IS_PRIVATE(inode)))
		return LSM_RET_DEFAULT(inode_getsecurity);
	/*
	 * Only one module will provide an attribute with a given name.
	 */
	hlist_for_each_entry(hp, &security_hook_heads.inode_getsecurity, list) {
		rc = hp->hook.inode_getsecurity(idmap, inode, name, buffer,
						alloc);
		if (rc != LSM_RET_DEFAULT(inode_getsecurity))
			return rc;
	}
	return LSM_RET_DEFAULT(inode_getsecurity);
}

/**
 * security_inode_setsecurity() - Set the xattr security label of an inode
 * @inode: inode
 * @name: xattr name
 * @value: security label
 * @size: length of security label
 * @flags: flags
 *
 * Set the security label associated with @name for @inode from the extended
 * attribute value @value.  @size indicates the size of the @value in bytes.
 * @flags may be XATTR_CREATE, XATTR_REPLACE, or 0. Note that @name is the
 * remainder of the attribute name after the security. prefix has been removed.
 *
 * Return: Returns 0 on success.
 */
int security_inode_setsecurity(struct inode *inode, const char *name,
			       const void *value, size_t size, int flags)
{
	struct security_hook_list *hp;
	int rc;

	if (unlikely(IS_PRIVATE(inode)))
		return LSM_RET_DEFAULT(inode_setsecurity);
	/*
	 * Only one module will provide an attribute with a given name.
	 */
	hlist_for_each_entry(hp, &security_hook_heads.inode_setsecurity, list) {
		rc = hp->hook.inode_setsecurity(inode, name, value, size,
						flags);
		if (rc != LSM_RET_DEFAULT(inode_setsecurity))
			return rc;
	}
	return LSM_RET_DEFAULT(inode_setsecurity);
}

/**
 * security_inode_listsecurity() - List the xattr security label names
 * @inode: inode
 * @buffer: buffer
 * @buffer_size: size of buffer
 *
 * Copy the extended attribute names for the security labels associated with
 * @inode into @buffer.  The maximum size of @buffer is specified by
 * @buffer_size.  @buffer may be NULL to request the size of the buffer
 * required.
 *
 * Return: Returns number of bytes used/required on success.
 */
int security_inode_listsecurity(struct inode *inode,
				char *buffer, size_t buffer_size)
{
	if (unlikely(IS_PRIVATE(inode)))
		return 0;
	return call_int_hook(inode_listsecurity, 0, inode, buffer, buffer_size);
}
EXPORT_SYMBOL(security_inode_listsecurity);

/**
 * security_inode_getsecid() - Get an inode's secid
 * @inode: inode
 * @secid: secid to return
 *
 * Get the secid associated with the node.  In case of failure, @secid will be
 * set to zero.
 */
void security_inode_getsecid(struct inode *inode, u32 *secid)
{
	call_void_hook(inode_getsecid, inode, secid);
}

/**
 * security_inode_copy_up() - Create new creds for an overlayfs copy-up op
 * @src: union dentry of copy-up file
 * @new: newly created creds
 *
 * A file is about to be copied up from lower layer to upper layer of overlay
 * filesystem. Security module can prepare a set of new creds and modify as
 * need be and return new creds. Caller will switch to new creds temporarily to
 * create new file and release newly allocated creds.
 *
 * Return: Returns 0 on success or a negative error code on error.
 */
int security_inode_copy_up(struct dentry *src, struct cred **new)
{
	return call_int_hook(inode_copy_up, 0, src, new);
}
EXPORT_SYMBOL(security_inode_copy_up);

/**
 * security_inode_copy_up_xattr() - Filter xattrs in an overlayfs copy-up op
 * @name: xattr name
 *
 * Filter the xattrs being copied up when a unioned file is copied up from a
 * lower layer to the union/overlay layer.   The caller is responsible for
 * reading and writing the xattrs, this hook is merely a filter.
 *
 * Return: Returns 0 to accept the xattr, 1 to discard the xattr, -EOPNOTSUPP
 *         if the security module does not know about attribute, or a negative
 *         error code to abort the copy up.
 */
int security_inode_copy_up_xattr(const char *name)
{
	struct security_hook_list *hp;
	int rc;

	/*
	 * The implementation can return 0 (accept the xattr), 1 (discard the
	 * xattr), -EOPNOTSUPP if it does not know anything about the xattr or
	 * any other error code in case of an error.
	 */
	hlist_for_each_entry(hp,
			     &security_hook_heads.inode_copy_up_xattr, list) {
		rc = hp->hook.inode_copy_up_xattr(name);
		if (rc != LSM_RET_DEFAULT(inode_copy_up_xattr))
			return rc;
	}

	return LSM_RET_DEFAULT(inode_copy_up_xattr);
}
EXPORT_SYMBOL(security_inode_copy_up_xattr);

/**
 * security_kernfs_init_security() - Init LSM context for a kernfs node
 * @kn_dir: parent kernfs node
 * @kn: the kernfs node to initialize
 *
 * Initialize the security context of a newly created kernfs node based on its
 * own and its parent's attributes.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_kernfs_init_security(struct kernfs_node *kn_dir,
				  struct kernfs_node *kn)
{
	return call_int_hook(kernfs_init_security, 0, kn_dir, kn);
}

/**
 * security_file_permission() - Check file permissions
 * @file: file
 * @mask: requested permissions
 *
 * Check file permissions before accessing an open file.  This hook is called
 * by various operations that read or write files.  A security module can use
 * this hook to perform additional checking on these operations, e.g. to
 * revalidate permissions on use to support privilege bracketing or policy
 * changes.  Notice that this hook is used when the actual read/write
 * operations are performed, whereas the inode_security_ops hook is called when
 * a file is opened (as well as many other operations).  Although this hook can
 * be used to revalidate permissions for various system call operations that
 * read or write files, it does not address the revalidation of permissions for
 * memory-mapped files.  Security modules must handle this separately if they
 * need such revalidation.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_file_permission(struct file *file, int mask)
{
	int ret;

	ret = call_int_hook(file_permission, 0, file, mask);
	if (ret)
		return ret;

	return fsnotify_perm(file, mask);
}

/**
 * security_file_alloc() - Allocate and init a file's LSM blob
 * @file: the file
 *
 * Allocate and attach a security structure to the file->f_security field.  The
 * security field is initialized to NULL when the structure is first created.
 *
 * Return: Return 0 if the hook is successful and permission is granted.
 */
int security_file_alloc(struct file *file)
{
	int rc = lsm_file_alloc(file);

	if (rc)
		return rc;
	rc = call_int_hook(file_alloc_security, 0, file);
	if (unlikely(rc))
		security_file_free(file);
	return rc;
}

/**
 * security_file_free() - Free a file's LSM blob
 * @file: the file
 *
 * Deallocate and free any security structures stored in file->f_security.
 */
void security_file_free(struct file *file)
{
	void *blob;

	call_void_hook(file_free_security, file);

	blob = file->f_security;
	if (blob) {
		file->f_security = NULL;
		kmem_cache_free(lsm_file_cache, blob);
	}
}

/**
 * security_file_ioctl() - Check if an ioctl is allowed
 * @file: associated file
 * @cmd: ioctl cmd
 * @arg: ioctl arguments
 *
 * Check permission for an ioctl operation on @file.  Note that @arg sometimes
 * represents a user space pointer; in other cases, it may be a simple integer
 * value.  When @arg represents a user space pointer, it should never be used
 * by the security module.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_file_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return call_int_hook(file_ioctl, 0, file, cmd, arg);
}
EXPORT_SYMBOL_GPL(security_file_ioctl);

/**
 * security_file_ioctl_compat() - Check if an ioctl is allowed in compat mode
 * @file: associated file
 * @cmd: ioctl cmd
 * @arg: ioctl arguments
 *
 * Compat version of security_file_ioctl() that correctly handles 32-bit
 * processes running on 64-bit kernels.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_file_ioctl_compat(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	return call_int_hook(file_ioctl_compat, 0, file, cmd, arg);
}
EXPORT_SYMBOL_GPL(security_file_ioctl_compat);

static inline unsigned long mmap_prot(struct file *file, unsigned long prot)
{
	/*
	 * Does we have PROT_READ and does the application expect
	 * it to imply PROT_EXEC?  If not, nothing to talk about...
	 */
	if ((prot & (PROT_READ | PROT_EXEC)) != PROT_READ)
		return prot;
	if (!(current->personality & READ_IMPLIES_EXEC))
		return prot;
	/*
	 * if that's an anonymous mapping, let it.
	 */
	if (!file)
		return prot | PROT_EXEC;
	/*
	 * ditto if it's not on noexec mount, except that on !MMU we need
	 * NOMMU_MAP_EXEC (== VM_MAYEXEC) in this case
	 */
	if (!path_noexec(&file->f_path)) {
#ifndef CONFIG_MMU
		if (file->f_op->mmap_capabilities) {
			unsigned caps = file->f_op->mmap_capabilities(file);
			if (!(caps & NOMMU_MAP_EXEC))
				return prot;
		}
#endif
		return prot | PROT_EXEC;
	}
	/* anything on noexec mount won't get PROT_EXEC */
	return prot;
}

/**
 * security_mmap_file() - Check if mmap'ing a file is allowed
 * @file: file
 * @prot: protection applied by the kernel
 * @flags: flags
 *
 * Check permissions for a mmap operation.  The @file may be NULL, e.g. if
 * mapping anonymous memory.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_mmap_file(struct file *file, unsigned long prot,
		       unsigned long flags)
{
	unsigned long prot_adj = mmap_prot(file, prot);
	int ret;

	ret = call_int_hook(mmap_file, 0, file, prot, prot_adj, flags);
	if (ret)
		return ret;
	return ima_file_mmap(file, prot, prot_adj, flags);
}

/**
 * security_mmap_addr() - Check if mmap'ing an address is allowed
 * @addr: address
 *
 * Check permissions for a mmap operation at @addr.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_mmap_addr(unsigned long addr)
{
	return call_int_hook(mmap_addr, 0, addr);
}

/**
 * security_file_mprotect() - Check if changing memory protections is allowed
 * @vma: memory region
 * @reqprot: application requested protection
 * @prot: protection applied by the kernel
 *
 * Check permissions before changing memory access permissions.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_file_mprotect(struct vm_area_struct *vma, unsigned long reqprot,
			   unsigned long prot)
{
	int ret;

	ret = call_int_hook(file_mprotect, 0, vma, reqprot, prot);
	if (ret)
		return ret;
	return ima_file_mprotect(vma, prot);
}

/**
 * security_file_lock() - Check if a file lock is allowed
 * @file: file
 * @cmd: lock operation (e.g. F_RDLCK, F_WRLCK)
 *
 * Check permission before performing file locking operations.  Note the hook
 * mediates both flock and fcntl style locks.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_file_lock(struct file *file, unsigned int cmd)
{
	return call_int_hook(file_lock, 0, file, cmd);
}

/**
 * security_file_fcntl() - Check if fcntl() op is allowed
 * @file: file
 * @cmd: fcntl command
 * @arg: command argument
 *
 * Check permission before allowing the file operation specified by @cmd from
 * being performed on the file @file.  Note that @arg sometimes represents a
 * user space pointer; in other cases, it may be a simple integer value.  When
 * @arg represents a user space pointer, it should never be used by the
 * security module.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_file_fcntl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return call_int_hook(file_fcntl, 0, file, cmd, arg);
}

/**
 * security_file_set_fowner() - Set the file owner info in the LSM blob
 * @file: the file
 *
 * Save owner security information (typically from current->security) in
 * file->f_security for later use by the send_sigiotask hook.
 *
 * Return: Returns 0 on success.
 */
void security_file_set_fowner(struct file *file)
{
	call_void_hook(file_set_fowner, file);
}

/**
 * security_file_send_sigiotask() - Check if sending SIGIO/SIGURG is allowed
 * @tsk: target task
 * @fown: signal sender
 * @sig: signal to be sent, SIGIO is sent if 0
 *
 * Check permission for the file owner @fown to send SIGIO or SIGURG to the
 * process @tsk.  Note that this hook is sometimes called from interrupt.  Note
 * that the fown_struct, @fown, is never outside the context of a struct file,
 * so the file structure (and associated security information) can always be
 * obtained: container_of(fown, struct file, f_owner).
 *
 * Return: Returns 0 if permission is granted.
 */
int security_file_send_sigiotask(struct task_struct *tsk,
				 struct fown_struct *fown, int sig)
{
	return call_int_hook(file_send_sigiotask, 0, tsk, fown, sig);
}

/**
 * security_file_receive() - Check is receiving a file via IPC is allowed
 * @file: file being received
 *
 * This hook allows security modules to control the ability of a process to
 * receive an open file descriptor via socket IPC.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_file_receive(struct file *file)
{
	return call_int_hook(file_receive, 0, file);
}

/**
 * security_file_open() - Save open() time state for late use by the LSM
 * @file:
 *
 * Save open-time permission checking state for later use upon file_permission,
 * and recheck access if anything has changed since inode_permission.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_file_open(struct file *file)
{
	int ret;

	ret = call_int_hook(file_open, 0, file);
	if (ret)
		return ret;

	return fsnotify_perm(file, MAY_OPEN);
}

/**
 * security_file_truncate() - Check if truncating a file is allowed
 * @file: file
 *
 * Check permission before truncating a file, i.e. using ftruncate.  Note that
 * truncation permission may also be checked based on the path, using the
 * @path_truncate hook.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_file_truncate(struct file *file)
{
	return call_int_hook(file_truncate, 0, file);
}

/**
 * security_task_alloc() - Allocate a task's LSM blob
 * @task: the task
 * @clone_flags: flags indicating what is being shared
 *
 * Handle allocation of task-related resources.
 *
 * Return: Returns a zero on success, negative values on failure.
 */
int security_task_alloc(struct task_struct *task, unsigned long clone_flags)
{
	int rc = lsm_task_alloc(task);

	if (rc)
		return rc;
	rc = call_int_hook(task_alloc, 0, task, clone_flags);
	if (unlikely(rc))
		security_task_free(task);
	return rc;
}

/**
 * security_task_free() - Free a task's LSM blob and related resources
 * @task: task
 *
 * Handle release of task-related resources.  Note that this can be called from
 * interrupt context.
 */
void security_task_free(struct task_struct *task)
{
	call_void_hook(task_free, task);

	kfree(task->security);
	task->security = NULL;
}

/**
 * security_cred_alloc_blank() - Allocate the min memory to allow cred_transfer
 * @cred: credentials
 * @gfp: gfp flags
 *
 * Only allocate sufficient memory and attach to @cred such that
 * cred_transfer() will not get ENOMEM.
 *
 * Return: Returns 0 on success, negative values on failure.
 */
int security_cred_alloc_blank(struct cred *cred, gfp_t gfp)
{
	int rc = lsm_cred_alloc(cred, gfp);

	if (rc)
		return rc;

	rc = call_int_hook(cred_alloc_blank, 0, cred, gfp);
	if (unlikely(rc))
		security_cred_free(cred);
	return rc;
}

/**
 * security_cred_free() - Free the cred's LSM blob and associated resources
 * @cred: credentials
 *
 * Deallocate and clear the cred->security field in a set of credentials.
 */
void security_cred_free(struct cred *cred)
{
	/*
	 * There is a failure case in prepare_creds() that
	 * may result in a call here with ->security being NULL.
	 */
	if (unlikely(cred->security == NULL))
		return;

	call_void_hook(cred_free, cred);

	kfree(cred->security);
	cred->security = NULL;
}

/**
 * security_prepare_creds() - Prepare a new set of credentials
 * @new: new credentials
 * @old: original credentials
 * @gfp: gfp flags
 *
 * Prepare a new set of credentials by copying the data from the old set.
 *
 * Return: Returns 0 on success, negative values on failure.
 */
int security_prepare_creds(struct cred *new, const struct cred *old, gfp_t gfp)
{
	int rc = lsm_cred_alloc(new, gfp);

	if (rc)
		return rc;

	rc = call_int_hook(cred_prepare, 0, new, old, gfp);
	if (unlikely(rc))
		security_cred_free(new);
	return rc;
}

/**
 * security_transfer_creds() - Transfer creds
 * @new: target credentials
 * @old: original credentials
 *
 * Transfer data from original creds to new creds.
 */
void security_transfer_creds(struct cred *new, const struct cred *old)
{
	call_void_hook(cred_transfer, new, old);
}

/**
 * security_cred_getsecid() - Get the secid from a set of credentials
 * @c: credentials
 * @secid: secid value
 *
 * Retrieve the security identifier of the cred structure @c.  In case of
 * failure, @secid will be set to zero.
 */
void security_cred_getsecid(const struct cred *c, u32 *secid)
{
	*secid = 0;
	call_void_hook(cred_getsecid, c, secid);
}
EXPORT_SYMBOL(security_cred_getsecid);

/**
 * security_kernel_act_as() - Set the kernel credentials to act as secid
 * @new: credentials
 * @secid: secid
 *
 * Set the credentials for a kernel service to act as (subjective context).
 * The current task must be the one that nominated @secid.
 *
 * Return: Returns 0 if successful.
 */
int security_kernel_act_as(struct cred *new, u32 secid)
{
	return call_int_hook(kernel_act_as, 0, new, secid);
}

/**
 * security_kernel_create_files_as() - Set file creation context using an inode
 * @new: target credentials
 * @inode: reference inode
 *
 * Set the file creation context in a set of credentials to be the same as the
 * objective context of the specified inode.  The current task must be the one
 * that nominated @inode.
 *
 * Return: Returns 0 if successful.
 */
int security_kernel_create_files_as(struct cred *new, struct inode *inode)
{
	return call_int_hook(kernel_create_files_as, 0, new, inode);
}

/**
 * security_kernel_module_request() - Check is loading a module is allowed
 * @kmod_name: module name
 *
 * Ability to trigger the kernel to automatically upcall to userspace for
 * userspace to load a kernel module with the given name.
 *
 * Return: Returns 0 if successful.
 */
int security_kernel_module_request(char *kmod_name)
{
	int ret;

	ret = call_int_hook(kernel_module_request, 0, kmod_name);
	if (ret)
		return ret;
	return integrity_kernel_module_request(kmod_name);
}

/**
 * security_kernel_read_file() - Read a file specified by userspace
 * @file: file
 * @id: file identifier
 * @contents: trust if security_kernel_post_read_file() will be called
 *
 * Read a file specified by userspace.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_kernel_read_file(struct file *file, enum kernel_read_file_id id,
			      bool contents)
{
	int ret;

	ret = call_int_hook(kernel_read_file, 0, file, id, contents);
	if (ret)
		return ret;
	return ima_read_file(file, id, contents);
}
EXPORT_SYMBOL_GPL(security_kernel_read_file);

/**
 * security_kernel_post_read_file() - Read a file specified by userspace
 * @file: file
 * @buf: file contents
 * @size: size of file contents
 * @id: file identifier
 *
 * Read a file specified by userspace.  This must be paired with a prior call
 * to security_kernel_read_file() call that indicated this hook would also be
 * called, see security_kernel_read_file() for more information.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_kernel_post_read_file(struct file *file, char *buf, loff_t size,
				   enum kernel_read_file_id id)
{
	int ret;

	ret = call_int_hook(kernel_post_read_file, 0, file, buf, size, id);
	if (ret)
		return ret;
	return ima_post_read_file(file, buf, size, id);
}
EXPORT_SYMBOL_GPL(security_kernel_post_read_file);

/**
 * security_kernel_load_data() - Load data provided by userspace
 * @id: data identifier
 * @contents: true if security_kernel_post_load_data() will be called
 *
 * Load data provided by userspace.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_kernel_load_data(enum kernel_load_data_id id, bool contents)
{
	int ret;

	ret = call_int_hook(kernel_load_data, 0, id, contents);
	if (ret)
		return ret;
	return ima_load_data(id, contents);
}
EXPORT_SYMBOL_GPL(security_kernel_load_data);

/**
 * security_kernel_post_load_data() - Load userspace data from a non-file source
 * @buf: data
 * @size: size of data
 * @id: data identifier
 * @description: text description of data, specific to the id value
 *
 * Load data provided by a non-file source (usually userspace buffer).  This
 * must be paired with a prior security_kernel_load_data() call that indicated
 * this hook would also be called, see security_kernel_load_data() for more
 * information.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_kernel_post_load_data(char *buf, loff_t size,
				   enum kernel_load_data_id id,
				   char *description)
{
	int ret;

	ret = call_int_hook(kernel_post_load_data, 0, buf, size, id,
			    description);
	if (ret)
		return ret;
	return ima_post_load_data(buf, size, id, description);
}
EXPORT_SYMBOL_GPL(security_kernel_post_load_data);

/**
 * security_task_fix_setuid() - Update LSM with new user id attributes
 * @new: updated credentials
 * @old: credentials being replaced
 * @flags: LSM_SETID_* flag values
 *
 * Update the module's state after setting one or more of the user identity
 * attributes of the current process.  The @flags parameter indicates which of
 * the set*uid system calls invoked this hook.  If @new is the set of
 * credentials that will be installed.  Modifications should be made to this
 * rather than to @current->cred.
 *
 * Return: Returns 0 on success.
 */
int security_task_fix_setuid(struct cred *new, const struct cred *old,
			     int flags)
{
	return call_int_hook(task_fix_setuid, 0, new, old, flags);
}

/**
 * security_task_fix_setgid() - Update LSM with new group id attributes
 * @new: updated credentials
 * @old: credentials being replaced
 * @flags: LSM_SETID_* flag value
 *
 * Update the module's state after setting one or more of the group identity
 * attributes of the current process.  The @flags parameter indicates which of
 * the set*gid system calls invoked this hook.  @new is the set of credentials
 * that will be installed.  Modifications should be made to this rather than to
 * @current->cred.
 *
 * Return: Returns 0 on success.
 */
int security_task_fix_setgid(struct cred *new, const struct cred *old,
			     int flags)
{
	return call_int_hook(task_fix_setgid, 0, new, old, flags);
}

/**
 * security_task_fix_setgroups() - Update LSM with new supplementary groups
 * @new: updated credentials
 * @old: credentials being replaced
 *
 * Update the module's state after setting the supplementary group identity
 * attributes of the current process.  @new is the set of credentials that will
 * be installed.  Modifications should be made to this rather than to
 * @current->cred.
 *
 * Return: Returns 0 on success.
 */
int security_task_fix_setgroups(struct cred *new, const struct cred *old)
{
	return call_int_hook(task_fix_setgroups, 0, new, old);
}

/**
 * security_task_setpgid() - Check if setting the pgid is allowed
 * @p: task being modified
 * @pgid: new pgid
 *
 * Check permission before setting the process group identifier of the process
 * @p to @pgid.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_task_setpgid(struct task_struct *p, pid_t pgid)
{
	return call_int_hook(task_setpgid, 0, p, pgid);
}

/**
 * security_task_getpgid() - Check if getting the pgid is allowed
 * @p: task
 *
 * Check permission before getting the process group identifier of the process
 * @p.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_task_getpgid(struct task_struct *p)
{
	return call_int_hook(task_getpgid, 0, p);
}

/**
 * security_task_getsid() - Check if getting the session id is allowed
 * @p: task
 *
 * Check permission before getting the session identifier of the process @p.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_task_getsid(struct task_struct *p)
{
	return call_int_hook(task_getsid, 0, p);
}

/**
 * security_current_getsecid_subj() - Get the current task's subjective secid
 * @secid: secid value
 *
 * Retrieve the subjective security identifier of the current task and return
 * it in @secid.  In case of failure, @secid will be set to zero.
 */
void security_current_getsecid_subj(u32 *secid)
{
	*secid = 0;
	call_void_hook(current_getsecid_subj, secid);
}
EXPORT_SYMBOL(security_current_getsecid_subj);

/**
 * security_task_getsecid_obj() - Get a task's objective secid
 * @p: target task
 * @secid: secid value
 *
 * Retrieve the objective security identifier of the task_struct in @p and
 * return it in @secid. In case of failure, @secid will be set to zero.
 */
void security_task_getsecid_obj(struct task_struct *p, u32 *secid)
{
	*secid = 0;
	call_void_hook(task_getsecid_obj, p, secid);
}
EXPORT_SYMBOL(security_task_getsecid_obj);

/**
 * security_task_setnice() - Check if setting a task's nice value is allowed
 * @p: target task
 * @nice: nice value
 *
 * Check permission before setting the nice value of @p to @nice.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_task_setnice(struct task_struct *p, int nice)
{
	return call_int_hook(task_setnice, 0, p, nice);
}

/**
 * security_task_setioprio() - Check if setting a task's ioprio is allowed
 * @p: target task
 * @ioprio: ioprio value
 *
 * Check permission before setting the ioprio value of @p to @ioprio.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_task_setioprio(struct task_struct *p, int ioprio)
{
	return call_int_hook(task_setioprio, 0, p, ioprio);
}

/**
 * security_task_getioprio() - Check if getting a task's ioprio is allowed
 * @p: task
 *
 * Check permission before getting the ioprio value of @p.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_task_getioprio(struct task_struct *p)
{
	return call_int_hook(task_getioprio, 0, p);
}

/**
 * security_task_prlimit() - Check if get/setting resources limits is allowed
 * @cred: current task credentials
 * @tcred: target task credentials
 * @flags: LSM_PRLIMIT_* flag bits indicating a get/set/both
 *
 * Check permission before getting and/or setting the resource limits of
 * another task.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_task_prlimit(const struct cred *cred, const struct cred *tcred,
			  unsigned int flags)
{
	return call_int_hook(task_prlimit, 0, cred, tcred, flags);
}

/**
 * security_task_setrlimit() - Check if setting a new rlimit value is allowed
 * @p: target task's group leader
 * @resource: resource whose limit is being set
 * @new_rlim: new resource limit
 *
 * Check permission before setting the resource limits of process @p for
 * @resource to @new_rlim.  The old resource limit values can be examined by
 * dereferencing (p->signal->rlim + resource).
 *
 * Return: Returns 0 if permission is granted.
 */
int security_task_setrlimit(struct task_struct *p, unsigned int resource,
			    struct rlimit *new_rlim)
{
	return call_int_hook(task_setrlimit, 0, p, resource, new_rlim);
}

/**
 * security_task_setscheduler() - Check if setting sched policy/param is allowed
 * @p: target task
 *
 * Check permission before setting scheduling policy and/or parameters of
 * process @p.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_task_setscheduler(struct task_struct *p)
{
	return call_int_hook(task_setscheduler, 0, p);
}

/**
 * security_task_getscheduler() - Check if getting scheduling info is allowed
 * @p: target task
 *
 * Check permission before obtaining scheduling information for process @p.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_task_getscheduler(struct task_struct *p)
{
	return call_int_hook(task_getscheduler, 0, p);
}

/**
 * security_task_movememory() - Check if moving memory is allowed
 * @p: task
 *
 * Check permission before moving memory owned by process @p.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_task_movememory(struct task_struct *p)
{
	return call_int_hook(task_movememory, 0, p);
}

/**
 * security_task_kill() - Check if sending a signal is allowed
 * @p: target process
 * @info: signal information
 * @sig: signal value
 * @cred: credentials of the signal sender, NULL if @current
 *
 * Check permission before sending signal @sig to @p.  @info can be NULL, the
 * constant 1, or a pointer to a kernel_siginfo structure.  If @info is 1 or
 * SI_FROMKERNEL(info) is true, then the signal should be viewed as coming from
 * the kernel and should typically be permitted.  SIGIO signals are handled
 * separately by the send_sigiotask hook in file_security_ops.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_task_kill(struct task_struct *p, struct kernel_siginfo *info,
		       int sig, const struct cred *cred)
{
	return call_int_hook(task_kill, 0, p, info, sig, cred);
}

/**
 * security_task_prctl() - Check if a prctl op is allowed
 * @option: operation
 * @arg2: argument
 * @arg3: argument
 * @arg4: argument
 * @arg5: argument
 *
 * Check permission before performing a process control operation on the
 * current process.
 *
 * Return: Return -ENOSYS if no-one wanted to handle this op, any other value
 *         to cause prctl() to return immediately with that value.
 */
int security_task_prctl(int option, unsigned long arg2, unsigned long arg3,
			unsigned long arg4, unsigned long arg5)
{
	int thisrc;
	int rc = LSM_RET_DEFAULT(task_prctl);
	struct security_hook_list *hp;

	hlist_for_each_entry(hp, &security_hook_heads.task_prctl, list) {
		thisrc = hp->hook.task_prctl(option, arg2, arg3, arg4, arg5);
		if (thisrc != LSM_RET_DEFAULT(task_prctl)) {
			rc = thisrc;
			if (thisrc != 0)
				break;
		}
	}
	return rc;
}

/**
 * security_task_to_inode() - Set the security attributes of a task's inode
 * @p: task
 * @inode: inode
 *
 * Set the security attributes for an inode based on an associated task's
 * security attributes, e.g. for /proc/pid inodes.
 */
void security_task_to_inode(struct task_struct *p, struct inode *inode)
{
	call_void_hook(task_to_inode, p, inode);
}

/**
 * security_create_user_ns() - Check if creating a new userns is allowed
 * @cred: prepared creds
 *
 * Check permission prior to creating a new user namespace.
 *
 * Return: Returns 0 if successful, otherwise < 0 error code.
 */
int security_create_user_ns(const struct cred *cred)
{
	return call_int_hook(userns_create, 0, cred);
}

/**
 * security_ipc_permission() - Check if sysv ipc access is allowed
 * @ipcp: ipc permission structure
 * @flag: requested permissions
 *
 * Check permissions for access to IPC.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_ipc_permission(struct kern_ipc_perm *ipcp, short flag)
{
	return call_int_hook(ipc_permission, 0, ipcp, flag);
}

/**
 * security_ipc_getsecid() - Get the sysv ipc object's secid
 * @ipcp: ipc permission structure
 * @secid: secid pointer
 *
 * Get the secid associated with the ipc object.  In case of failure, @secid
 * will be set to zero.
 */
void security_ipc_getsecid(struct kern_ipc_perm *ipcp, u32 *secid)
{
	*secid = 0;
	call_void_hook(ipc_getsecid, ipcp, secid);
}

/**
 * security_msg_msg_alloc() - Allocate a sysv ipc message LSM blob
 * @msg: message structure
 *
 * Allocate and attach a security structure to the msg->security field.  The
 * security field is initialized to NULL when the structure is first created.
 *
 * Return: Return 0 if operation was successful and permission is granted.
 */
int security_msg_msg_alloc(struct msg_msg *msg)
{
	int rc = lsm_msg_msg_alloc(msg);

	if (unlikely(rc))
		return rc;
	rc = call_int_hook(msg_msg_alloc_security, 0, msg);
	if (unlikely(rc))
		security_msg_msg_free(msg);
	return rc;
}

/**
 * security_msg_msg_free() - Free a sysv ipc message LSM blob
 * @msg: message structure
 *
 * Deallocate the security structure for this message.
 */
void security_msg_msg_free(struct msg_msg *msg)
{
	call_void_hook(msg_msg_free_security, msg);
	kfree(msg->security);
	msg->security = NULL;
}

/**
 * security_msg_queue_alloc() - Allocate a sysv ipc msg queue LSM blob
 * @msq: sysv ipc permission structure
 *
 * Allocate and attach a security structure to @msg. The security field is
 * initialized to NULL when the structure is first created.
 *
 * Return: Returns 0 if operation was successful and permission is granted.
 */
int security_msg_queue_alloc(struct kern_ipc_perm *msq)
{
	int rc = lsm_ipc_alloc(msq);

	if (unlikely(rc))
		return rc;
	rc = call_int_hook(msg_queue_alloc_security, 0, msq);
	if (unlikely(rc))
		security_msg_queue_free(msq);
	return rc;
}

/**
 * security_msg_queue_free() - Free a sysv ipc msg queue LSM blob
 * @msq: sysv ipc permission structure
 *
 * Deallocate security field @perm->security for the message queue.
 */
void security_msg_queue_free(struct kern_ipc_perm *msq)
{
	call_void_hook(msg_queue_free_security, msq);
	kfree(msq->security);
	msq->security = NULL;
}

/**
 * security_msg_queue_associate() - Check if a msg queue operation is allowed
 * @msq: sysv ipc permission structure
 * @msqflg: operation flags
 *
 * Check permission when a message queue is requested through the msgget system
 * call. This hook is only called when returning the message queue identifier
 * for an existing message queue, not when a new message queue is created.
 *
 * Return: Return 0 if permission is granted.
 */
int security_msg_queue_associate(struct kern_ipc_perm *msq, int msqflg)
{
	return call_int_hook(msg_queue_associate, 0, msq, msqflg);
}

/**
 * security_msg_queue_msgctl() - Check if a msg queue operation is allowed
 * @msq: sysv ipc permission structure
 * @cmd: operation
 *
 * Check permission when a message control operation specified by @cmd is to be
 * performed on the message queue with permissions.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_msg_queue_msgctl(struct kern_ipc_perm *msq, int cmd)
{
	return call_int_hook(msg_queue_msgctl, 0, msq, cmd);
}

/**
 * security_msg_queue_msgsnd() - Check if sending a sysv ipc message is allowed
 * @msq: sysv ipc permission structure
 * @msg: message
 * @msqflg: operation flags
 *
 * Check permission before a message, @msg, is enqueued on the message queue
 * with permissions specified in @msq.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_msg_queue_msgsnd(struct kern_ipc_perm *msq,
			      struct msg_msg *msg, int msqflg)
{
	return call_int_hook(msg_queue_msgsnd, 0, msq, msg, msqflg);
}

/**
 * security_msg_queue_msgrcv() - Check if receiving a sysv ipc msg is allowed
 * @msq: sysv ipc permission structure
 * @msg: message
 * @target: target task
 * @type: type of message requested
 * @mode: operation flags
 *
 * Check permission before a message, @msg, is removed from the message	queue.
 * The @target task structure contains a pointer to the process that will be
 * receiving the message (not equal to the current process when inline receives
 * are being performed).
 *
 * Return: Returns 0 if permission is granted.
 */
int security_msg_queue_msgrcv(struct kern_ipc_perm *msq, struct msg_msg *msg,
			      struct task_struct *target, long type, int mode)
{
	return call_int_hook(msg_queue_msgrcv, 0, msq, msg, target, type, mode);
}

/**
 * security_shm_alloc() - Allocate a sysv shm LSM blob
 * @shp: sysv ipc permission structure
 *
 * Allocate and attach a security structure to the @shp security field.  The
 * security field is initialized to NULL when the structure is first created.
 *
 * Return: Returns 0 if operation was successful and permission is granted.
 */
int security_shm_alloc(struct kern_ipc_perm *shp)
{
	int rc = lsm_ipc_alloc(shp);

	if (unlikely(rc))
		return rc;
	rc = call_int_hook(shm_alloc_security, 0, shp);
	if (unlikely(rc))
		security_shm_free(shp);
	return rc;
}

/**
 * security_shm_free() - Free a sysv shm LSM blob
 * @shp: sysv ipc permission structure
 *
 * Deallocate the security structure @perm->security for the memory segment.
 */
void security_shm_free(struct kern_ipc_perm *shp)
{
	call_void_hook(shm_free_security, shp);
	kfree(shp->security);
	shp->security = NULL;
}

/**
 * security_shm_associate() - Check if a sysv shm operation is allowed
 * @shp: sysv ipc permission structure
 * @shmflg: operation flags
 *
 * Check permission when a shared memory region is requested through the shmget
 * system call. This hook is only called when returning the shared memory
 * region identifier for an existing region, not when a new shared memory
 * region is created.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_shm_associate(struct kern_ipc_perm *shp, int shmflg)
{
	return call_int_hook(shm_associate, 0, shp, shmflg);
}

/**
 * security_shm_shmctl() - Check if a sysv shm operation is allowed
 * @shp: sysv ipc permission structure
 * @cmd: operation
 *
 * Check permission when a shared memory control operation specified by @cmd is
 * to be performed on the shared memory region with permissions in @shp.
 *
 * Return: Return 0 if permission is granted.
 */
int security_shm_shmctl(struct kern_ipc_perm *shp, int cmd)
{
	return call_int_hook(shm_shmctl, 0, shp, cmd);
}

/**
 * security_shm_shmat() - Check if a sysv shm attach operation is allowed
 * @shp: sysv ipc permission structure
 * @shmaddr: address of memory region to attach
 * @shmflg: operation flags
 *
 * Check permissions prior to allowing the shmat system call to attach the
 * shared memory segment with permissions @shp to the data segment of the
 * calling process. The attaching address is specified by @shmaddr.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_shm_shmat(struct kern_ipc_perm *shp,
		       char __user *shmaddr, int shmflg)
{
	return call_int_hook(shm_shmat, 0, shp, shmaddr, shmflg);
}

/**
 * security_sem_alloc() - Allocate a sysv semaphore LSM blob
 * @sma: sysv ipc permission structure
 *
 * Allocate and attach a security structure to the @sma security field. The
 * security field is initialized to NULL when the structure is first created.
 *
 * Return: Returns 0 if operation was successful and permission is granted.
 */
int security_sem_alloc(struct kern_ipc_perm *sma)
{
	int rc = lsm_ipc_alloc(sma);

	if (unlikely(rc))
		return rc;
	rc = call_int_hook(sem_alloc_security, 0, sma);
	if (unlikely(rc))
		security_sem_free(sma);
	return rc;
}

/**
 * security_sem_free() - Free a sysv semaphore LSM blob
 * @sma: sysv ipc permission structure
 *
 * Deallocate security structure @sma->security for the semaphore.
 */
void security_sem_free(struct kern_ipc_perm *sma)
{
	call_void_hook(sem_free_security, sma);
	kfree(sma->security);
	sma->security = NULL;
}

/**
 * security_sem_associate() - Check if a sysv semaphore operation is allowed
 * @sma: sysv ipc permission structure
 * @semflg: operation flags
 *
 * Check permission when a semaphore is requested through the semget system
 * call. This hook is only called when returning the semaphore identifier for
 * an existing semaphore, not when a new one must be created.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_sem_associate(struct kern_ipc_perm *sma, int semflg)
{
	return call_int_hook(sem_associate, 0, sma, semflg);
}

/**
 * security_sem_semctl() - Check if a sysv semaphore operation is allowed
 * @sma: sysv ipc permission structure
 * @cmd: operation
 *
 * Check permission when a semaphore operation specified by @cmd is to be
 * performed on the semaphore.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_sem_semctl(struct kern_ipc_perm *sma, int cmd)
{
	return call_int_hook(sem_semctl, 0, sma, cmd);
}

/**
 * security_sem_semop() - Check if a sysv semaphore operation is allowed
 * @sma: sysv ipc permission structure
 * @sops: operations to perform
 * @nsops: number of operations
 * @alter: flag indicating changes will be made
 *
 * Check permissions before performing operations on members of the semaphore
 * set. If the @alter flag is nonzero, the semaphore set may be modified.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_sem_semop(struct kern_ipc_perm *sma, struct sembuf *sops,
		       unsigned nsops, int alter)
{
	return call_int_hook(sem_semop, 0, sma, sops, nsops, alter);
}

/**
 * security_d_instantiate() - Populate an inode's LSM state based on a dentry
 * @dentry: dentry
 * @inode: inode
 *
 * Fill in @inode security information for a @dentry if allowed.
 */
void security_d_instantiate(struct dentry *dentry, struct inode *inode)
{
	if (unlikely(inode && IS_PRIVATE(inode)))
		return;
	call_void_hook(d_instantiate, dentry, inode);
}
EXPORT_SYMBOL(security_d_instantiate);

/**
 * security_getprocattr() - Read an attribute for a task
 * @p: the task
 * @lsm: LSM name
 * @name: attribute name
 * @value: attribute value
 *
 * Read attribute @name for task @p and store it into @value if allowed.
 *
 * Return: Returns the length of @value on success, a negative value otherwise.
 */
int security_getprocattr(struct task_struct *p, const char *lsm,
			 const char *name, char **value)
{
	struct security_hook_list *hp;

	hlist_for_each_entry(hp, &security_hook_heads.getprocattr, list) {
		if (lsm != NULL && strcmp(lsm, hp->lsm))
			continue;
		return hp->hook.getprocattr(p, name, value);
	}
	return LSM_RET_DEFAULT(getprocattr);
}

/**
 * security_setprocattr() - Set an attribute for a task
 * @lsm: LSM name
 * @name: attribute name
 * @value: attribute value
 * @size: attribute value size
 *
 * Write (set) the current task's attribute @name to @value, size @size if
 * allowed.
 *
 * Return: Returns bytes written on success, a negative value otherwise.
 */
int security_setprocattr(const char *lsm, const char *name, void *value,
			 size_t size)
{
	struct security_hook_list *hp;

	hlist_for_each_entry(hp, &security_hook_heads.setprocattr, list) {
		if (lsm != NULL && strcmp(lsm, hp->lsm))
			continue;
		return hp->hook.setprocattr(name, value, size);
	}
	return LSM_RET_DEFAULT(setprocattr);
}

/**
 * security_netlink_send() - Save info and check if netlink sending is allowed
 * @sk: sending socket
 * @skb: netlink message
 *
 * Save security information for a netlink message so that permission checking
 * can be performed when the message is processed.  The security information
 * can be saved using the eff_cap field of the netlink_skb_parms structure.
 * Also may be used to provide fine grained control over message transmission.
 *
 * Return: Returns 0 if the information was successfully saved and message is
 *         allowed to be transmitted.
 */
int security_netlink_send(struct sock *sk, struct sk_buff *skb)
{
	return call_int_hook(netlink_send, 0, sk, skb);
}

/**
 * security_ismaclabel() - Check is the named attribute is a MAC label
 * @name: full extended attribute name
 *
 * Check if the extended attribute specified by @name represents a MAC label.
 *
 * Return: Returns 1 if name is a MAC attribute otherwise returns 0.
 */
int security_ismaclabel(const char *name)
{
	return call_int_hook(ismaclabel, 0, name);
}
EXPORT_SYMBOL(security_ismaclabel);

/**
 * security_secid_to_secctx() - Convert a secid to a secctx
 * @secid: secid
 * @secdata: secctx
 * @seclen: secctx length
 *
 * Convert secid to security context.  If @secdata is NULL the length of the
 * result will be returned in @seclen, but no @secdata will be returned.  This
 * does mean that the length could change between calls to check the length and
 * the next call which actually allocates and returns the @secdata.
 *
 * Return: Return 0 on success, error on failure.
 */
int security_secid_to_secctx(u32 secid, char **secdata, u32 *seclen)
{
	struct security_hook_list *hp;
	int rc;

	/*
	 * Currently, only one LSM can implement secid_to_secctx (i.e this
	 * LSM hook is not "stackable").
	 */
	hlist_for_each_entry(hp, &security_hook_heads.secid_to_secctx, list) {
		rc = hp->hook.secid_to_secctx(secid, secdata, seclen);
		if (rc != LSM_RET_DEFAULT(secid_to_secctx))
			return rc;
	}

	return LSM_RET_DEFAULT(secid_to_secctx);
}
EXPORT_SYMBOL(security_secid_to_secctx);

/**
 * security_secctx_to_secid() - Convert a secctx to a secid
 * @secdata: secctx
 * @seclen: length of secctx
 * @secid: secid
 *
 * Convert security context to secid.
 *
 * Return: Returns 0 on success, error on failure.
 */
int security_secctx_to_secid(const char *secdata, u32 seclen, u32 *secid)
{
	*secid = 0;
	return call_int_hook(secctx_to_secid, 0, secdata, seclen, secid);
}
EXPORT_SYMBOL(security_secctx_to_secid);

/**
 * security_release_secctx() - Free a secctx buffer
 * @secdata: secctx
 * @seclen: length of secctx
 *
 * Release the security context.
 */
void security_release_secctx(char *secdata, u32 seclen)
{
	call_void_hook(release_secctx, secdata, seclen);
}
EXPORT_SYMBOL(security_release_secctx);

/**
 * security_inode_invalidate_secctx() - Invalidate an inode's security label
 * @inode: inode
 *
 * Notify the security module that it must revalidate the security context of
 * an inode.
 */
void security_inode_invalidate_secctx(struct inode *inode)
{
	call_void_hook(inode_invalidate_secctx, inode);
}
EXPORT_SYMBOL(security_inode_invalidate_secctx);

/**
 * security_inode_notifysecctx() - Nofify the LSM of an inode's security label
 * @inode: inode
 * @ctx: secctx
 * @ctxlen: length of secctx
 *
 * Notify the security module of what the security context of an inode should
 * be.  Initializes the incore security context managed by the security module
 * for this inode.  Example usage: NFS client invokes this hook to initialize
 * the security context in its incore inode to the value provided by the server
 * for the file when the server returned the file's attributes to the client.
 * Must be called with inode->i_mutex locked.
 *
 * Return: Returns 0 on success, error on failure.
 */
int security_inode_notifysecctx(struct inode *inode, void *ctx, u32 ctxlen)
{
	return call_int_hook(inode_notifysecctx, 0, inode, ctx, ctxlen);
}
EXPORT_SYMBOL(security_inode_notifysecctx);

/**
 * security_inode_setsecctx() - Change the security label of an inode
 * @dentry: inode
 * @ctx: secctx
 * @ctxlen: length of secctx
 *
 * Change the security context of an inode.  Updates the incore security
 * context managed by the security module and invokes the fs code as needed
 * (via __vfs_setxattr_noperm) to update any backing xattrs that represent the
 * context.  Example usage: NFS server invokes this hook to change the security
 * context in its incore inode and on the backing filesystem to a value
 * provided by the client on a SETATTR operation.  Must be called with
 * inode->i_mutex locked.
 *
 * Return: Returns 0 on success, error on failure.
 */
int security_inode_setsecctx(struct dentry *dentry, void *ctx, u32 ctxlen)
{
	return call_int_hook(inode_setsecctx, 0, dentry, ctx, ctxlen);
}
EXPORT_SYMBOL(security_inode_setsecctx);

/**
 * security_inode_getsecctx() - Get the security label of an inode
 * @inode: inode
 * @ctx: secctx
 * @ctxlen: length of secctx
 *
 * On success, returns 0 and fills out @ctx and @ctxlen with the security
 * context for the given @inode.
 *
 * Return: Returns 0 on success, error on failure.
 */
int security_inode_getsecctx(struct inode *inode, void **ctx, u32 *ctxlen)
{
	struct security_hook_list *hp;
	int rc;

	/*
	 * Only one module will provide a security context.
	 */
	hlist_for_each_entry(hp, &security_hook_heads.inode_getsecctx, list) {
		rc = hp->hook.inode_getsecctx(inode, ctx, ctxlen);
		if (rc != LSM_RET_DEFAULT(inode_getsecctx))
			return rc;
	}

	return LSM_RET_DEFAULT(inode_getsecctx);
}
EXPORT_SYMBOL(security_inode_getsecctx);

#ifdef CONFIG_WATCH_QUEUE
/**
 * security_post_notification() - Check if a watch notification can be posted
 * @w_cred: credentials of the task that set the watch
 * @cred: credentials of the task which triggered the watch
 * @n: the notification
 *
 * Check to see if a watch notification can be posted to a particular queue.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_post_notification(const struct cred *w_cred,
			       const struct cred *cred,
			       struct watch_notification *n)
{
	return call_int_hook(post_notification, 0, w_cred, cred, n);
}
#endif /* CONFIG_WATCH_QUEUE */

#ifdef CONFIG_KEY_NOTIFICATIONS
/**
 * security_watch_key() - Check if a task is allowed to watch for key events
 * @key: the key to watch
 *
 * Check to see if a process is allowed to watch for event notifications from
 * a key or keyring.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_watch_key(struct key *key)
{
	return call_int_hook(watch_key, 0, key);
}
#endif /* CONFIG_KEY_NOTIFICATIONS */

#ifdef CONFIG_SECURITY_NETWORK
/**
 * security_unix_stream_connect() - Check if a AF_UNIX stream is allowed
 * @sock: originating sock
 * @other: peer sock
 * @newsk: new sock
 *
 * Check permissions before establishing a Unix domain stream connection
 * between @sock and @other.
 *
 * The @unix_stream_connect and @unix_may_send hooks were necessary because
 * Linux provides an alternative to the conventional file name space for Unix
 * domain sockets.  Whereas binding and connecting to sockets in the file name
 * space is mediated by the typical file permissions (and caught by the mknod
 * and permission hooks in inode_security_ops), binding and connecting to
 * sockets in the abstract name space is completely unmediated.  Sufficient
 * control of Unix domain sockets in the abstract name space isn't possible
 * using only the socket layer hooks, since we need to know the actual target
 * socket, which is not looked up until we are inside the af_unix code.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_unix_stream_connect(struct sock *sock, struct sock *other,
				 struct sock *newsk)
{
	return call_int_hook(unix_stream_connect, 0, sock, other, newsk);
}
EXPORT_SYMBOL(security_unix_stream_connect);

/**
 * security_unix_may_send() - Check if AF_UNIX socket can send datagrams
 * @sock: originating sock
 * @other: peer sock
 *
 * Check permissions before connecting or sending datagrams from @sock to
 * @other.
 *
 * The @unix_stream_connect and @unix_may_send hooks were necessary because
 * Linux provides an alternative to the conventional file name space for Unix
 * domain sockets.  Whereas binding and connecting to sockets in the file name
 * space is mediated by the typical file permissions (and caught by the mknod
 * and permission hooks in inode_security_ops), binding and connecting to
 * sockets in the abstract name space is completely unmediated.  Sufficient
 * control of Unix domain sockets in the abstract name space isn't possible
 * using only the socket layer hooks, since we need to know the actual target
 * socket, which is not looked up until we are inside the af_unix code.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_unix_may_send(struct socket *sock,  struct socket *other)
{
	return call_int_hook(unix_may_send, 0, sock, other);
}
EXPORT_SYMBOL(security_unix_may_send);

/**
 * security_socket_create() - Check if creating a new socket is allowed
 * @family: protocol family
 * @type: communications type
 * @protocol: requested protocol
 * @kern: set to 1 if a kernel socket is requested
 *
 * Check permissions prior to creating a new socket.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_socket_create(int family, int type, int protocol, int kern)
{
	return call_int_hook(socket_create, 0, family, type, protocol, kern);
}

/**
 * security_socket_post_create() - Initialize a newly created socket
 * @sock: socket
 * @family: protocol family
 * @type: communications type
 * @protocol: requested protocol
 * @kern: set to 1 if a kernel socket is requested
 *
 * This hook allows a module to update or allocate a per-socket security
 * structure. Note that the security field was not added directly to the socket
 * structure, but rather, the socket security information is stored in the
 * associated inode.  Typically, the inode alloc_security hook will allocate
 * and attach security information to SOCK_INODE(sock)->i_security.  This hook
 * may be used to update the SOCK_INODE(sock)->i_security field with additional
 * information that wasn't available when the inode was allocated.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_socket_post_create(struct socket *sock, int family,
				int type, int protocol, int kern)
{
	return call_int_hook(socket_post_create, 0, sock, family, type,
			     protocol, kern);
}

/**
 * security_socket_socketpair() - Check if creating a socketpair is allowed
 * @socka: first socket
 * @sockb: second socket
 *
 * Check permissions before creating a fresh pair of sockets.
 *
 * Return: Returns 0 if permission is granted and the connection was
 *         established.
 */
int security_socket_socketpair(struct socket *socka, struct socket *sockb)
{
	return call_int_hook(socket_socketpair, 0, socka, sockb);
}
EXPORT_SYMBOL(security_socket_socketpair);

/**
 * security_socket_bind() - Check if a socket bind operation is allowed
 * @sock: socket
 * @address: requested bind address
 * @addrlen: length of address
 *
 * Check permission before socket protocol layer bind operation is performed
 * and the socket @sock is bound to the address specified in the @address
 * parameter.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_socket_bind(struct socket *sock,
			 struct sockaddr *address, int addrlen)
{
	return call_int_hook(socket_bind, 0, sock, address, addrlen);
}

/**
 * security_socket_connect() - Check if a socket connect operation is allowed
 * @sock: socket
 * @address: address of remote connection point
 * @addrlen: length of address
 *
 * Check permission before socket protocol layer connect operation attempts to
 * connect socket @sock to a remote address, @address.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_socket_connect(struct socket *sock,
			    struct sockaddr *address, int addrlen)
{
	return call_int_hook(socket_connect, 0, sock, address, addrlen);
}

/**
 * security_socket_listen() - Check if a socket is allowed to listen
 * @sock: socket
 * @backlog: connection queue size
 *
 * Check permission before socket protocol layer listen operation.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_socket_listen(struct socket *sock, int backlog)
{
	return call_int_hook(socket_listen, 0, sock, backlog);
}

/**
 * security_socket_accept() - Check if a socket is allowed to accept connections
 * @sock: listening socket
 * @newsock: newly creation connection socket
 *
 * Check permission before accepting a new connection.  Note that the new
 * socket, @newsock, has been created and some information copied to it, but
 * the accept operation has not actually been performed.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_socket_accept(struct socket *sock, struct socket *newsock)
{
	return call_int_hook(socket_accept, 0, sock, newsock);
}

/**
 * security_socket_sendmsg() - Check is sending a message is allowed
 * @sock: sending socket
 * @msg: message to send
 * @size: size of message
 *
 * Check permission before transmitting a message to another socket.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_socket_sendmsg(struct socket *sock, struct msghdr *msg, int size)
{
	return call_int_hook(socket_sendmsg, 0, sock, msg, size);
}

/**
 * security_socket_recvmsg() - Check if receiving a message is allowed
 * @sock: receiving socket
 * @msg: message to receive
 * @size: size of message
 * @flags: operational flags
 *
 * Check permission before receiving a message from a socket.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_socket_recvmsg(struct socket *sock, struct msghdr *msg,
			    int size, int flags)
{
	return call_int_hook(socket_recvmsg, 0, sock, msg, size, flags);
}

/**
 * security_socket_getsockname() - Check if reading the socket addr is allowed
 * @sock: socket
 *
 * Check permission before reading the local address (name) of the socket
 * object.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_socket_getsockname(struct socket *sock)
{
	return call_int_hook(socket_getsockname, 0, sock);
}

/**
 * security_socket_getpeername() - Check if reading the peer's addr is allowed
 * @sock: socket
 *
 * Check permission before the remote address (name) of a socket object.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_socket_getpeername(struct socket *sock)
{
	return call_int_hook(socket_getpeername, 0, sock);
}

/**
 * security_socket_getsockopt() - Check if reading a socket option is allowed
 * @sock: socket
 * @level: option's protocol level
 * @optname: option name
 *
 * Check permissions before retrieving the options associated with socket
 * @sock.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_socket_getsockopt(struct socket *sock, int level, int optname)
{
	return call_int_hook(socket_getsockopt, 0, sock, level, optname);
}

/**
 * security_socket_setsockopt() - Check if setting a socket option is allowed
 * @sock: socket
 * @level: option's protocol level
 * @optname: option name
 *
 * Check permissions before setting the options associated with socket @sock.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_socket_setsockopt(struct socket *sock, int level, int optname)
{
	return call_int_hook(socket_setsockopt, 0, sock, level, optname);
}

/**
 * security_socket_shutdown() - Checks if shutting down the socket is allowed
 * @sock: socket
 * @how: flag indicating how sends and receives are handled
 *
 * Checks permission before all or part of a connection on the socket @sock is
 * shut down.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_socket_shutdown(struct socket *sock, int how)
{
	return call_int_hook(socket_shutdown, 0, sock, how);
}

/**
 * security_sock_rcv_skb() - Check if an incoming network packet is allowed
 * @sk: destination sock
 * @skb: incoming packet
 *
 * Check permissions on incoming network packets.  This hook is distinct from
 * Netfilter's IP input hooks since it is the first time that the incoming
 * sk_buff @skb has been associated with a particular socket, @sk.  Must not
 * sleep inside this hook because some callers hold spinlocks.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_sock_rcv_skb(struct sock *sk, struct sk_buff *skb)
{
	return call_int_hook(socket_sock_rcv_skb, 0, sk, skb);
}
EXPORT_SYMBOL(security_sock_rcv_skb);

/**
 * security_socket_getpeersec_stream() - Get the remote peer label
 * @sock: socket
 * @optval: destination buffer
 * @optlen: size of peer label copied into the buffer
 * @len: maximum size of the destination buffer
 *
 * This hook allows the security module to provide peer socket security state
 * for unix or connected tcp sockets to userspace via getsockopt SO_GETPEERSEC.
 * For tcp sockets this can be meaningful if the socket is associated with an
 * ipsec SA.
 *
 * Return: Returns 0 if all is well, otherwise, typical getsockopt return
 *         values.
 */
int security_socket_getpeersec_stream(struct socket *sock, sockptr_t optval,
				      sockptr_t optlen, unsigned int len)
{
	struct security_hook_list *hp;
	int rc;

	/*
	 * Only one module will provide a security context.
	 */
	hlist_for_each_entry(hp, &security_hook_heads.socket_getpeersec_stream,
			     list) {
		rc = hp->hook.socket_getpeersec_stream(sock, optval, optlen,
						       len);
		if (rc != LSM_RET_DEFAULT(socket_getpeersec_stream))
			return rc;
	}
	return LSM_RET_DEFAULT(socket_getpeersec_stream);
}

/**
 * security_socket_getpeersec_dgram() - Get the remote peer label
 * @sock: socket
 * @skb: datagram packet
 * @secid: remote peer label secid
 *
 * This hook allows the security module to provide peer socket security state
 * for udp sockets on a per-packet basis to userspace via getsockopt
 * SO_GETPEERSEC. The application must first have indicated the IP_PASSSEC
 * option via getsockopt. It can then retrieve the security state returned by
 * this hook for a packet via the SCM_SECURITY ancillary message type.
 *
 * Return: Returns 0 on success, error on failure.
 */
int security_socket_getpeersec_dgram(struct socket *sock,
				     struct sk_buff *skb, u32 *secid)
{
	struct security_hook_list *hp;
	int rc;

	/*
	 * Only one module will provide a security context.
	 */
	hlist_for_each_entry(hp, &security_hook_heads.socket_getpeersec_dgram,
			     list) {
		rc = hp->hook.socket_getpeersec_dgram(sock, skb, secid);
		if (rc != LSM_RET_DEFAULT(socket_getpeersec_dgram))
			return rc;
	}
	return LSM_RET_DEFAULT(socket_getpeersec_dgram);
}
EXPORT_SYMBOL(security_socket_getpeersec_dgram);

/**
 * security_sk_alloc() - Allocate and initialize a sock's LSM blob
 * @sk: sock
 * @family: protocol family
 * @priority: gfp flags
 *
 * Allocate and attach a security structure to the sk->sk_security field, which
 * is used to copy security attributes between local stream sockets.
 *
 * Return: Returns 0 on success, error on failure.
 */
int security_sk_alloc(struct sock *sk, int family, gfp_t priority)
{
	return call_int_hook(sk_alloc_security, 0, sk, family, priority);
}

/**
 * security_sk_free() - Free the sock's LSM blob
 * @sk: sock
 *
 * Deallocate security structure.
 */
void security_sk_free(struct sock *sk)
{
	call_void_hook(sk_free_security, sk);
}

/**
 * security_sk_clone() - Clone a sock's LSM state
 * @sk: original sock
 * @newsk: target sock
 *
 * Clone/copy security structure.
 */
void security_sk_clone(const struct sock *sk, struct sock *newsk)
{
	call_void_hook(sk_clone_security, sk, newsk);
}
EXPORT_SYMBOL(security_sk_clone);

/**
 * security_sk_classify_flow() - Set a flow's secid based on socket
 * @sk: original socket
 * @flic: target flow
 *
 * Set the target flow's secid to socket's secid.
 */
void security_sk_classify_flow(const struct sock *sk, struct flowi_common *flic)
{
	call_void_hook(sk_getsecid, sk, &flic->flowic_secid);
}
EXPORT_SYMBOL(security_sk_classify_flow);

/**
 * security_req_classify_flow() - Set a flow's secid based on request_sock
 * @req: request_sock
 * @flic: target flow
 *
 * Sets @flic's secid to @req's secid.
 */
void security_req_classify_flow(const struct request_sock *req,
				struct flowi_common *flic)
{
	call_void_hook(req_classify_flow, req, flic);
}
EXPORT_SYMBOL(security_req_classify_flow);

/**
 * security_sock_graft() - Reconcile LSM state when grafting a sock on a socket
 * @sk: sock being grafted
 * @parent: target parent socket
 *
 * Sets @parent's inode secid to @sk's secid and update @sk with any necessary
 * LSM state from @parent.
 */
void security_sock_graft(struct sock *sk, struct socket *parent)
{
	call_void_hook(sock_graft, sk, parent);
}
EXPORT_SYMBOL(security_sock_graft);

/**
 * security_inet_conn_request() - Set request_sock state using incoming connect
 * @sk: parent listening sock
 * @skb: incoming connection
 * @req: new request_sock
 *
 * Initialize the @req LSM state based on @sk and the incoming connect in @skb.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_inet_conn_request(const struct sock *sk,
			       struct sk_buff *skb, struct request_sock *req)
{
	return call_int_hook(inet_conn_request, 0, sk, skb, req);
}
EXPORT_SYMBOL(security_inet_conn_request);

/**
 * security_inet_csk_clone() - Set new sock LSM state based on request_sock
 * @newsk: new sock
 * @req: connection request_sock
 *
 * Set that LSM state of @sock using the LSM state from @req.
 */
void security_inet_csk_clone(struct sock *newsk,
			     const struct request_sock *req)
{
	call_void_hook(inet_csk_clone, newsk, req);
}

/**
 * security_inet_conn_established() - Update sock's LSM state with connection
 * @sk: sock
 * @skb: connection packet
 *
 * Update @sock's LSM state to represent a new connection from @skb.
 */
void security_inet_conn_established(struct sock *sk,
				    struct sk_buff *skb)
{
	call_void_hook(inet_conn_established, sk, skb);
}
EXPORT_SYMBOL(security_inet_conn_established);

/**
 * security_secmark_relabel_packet() - Check if setting a secmark is allowed
 * @secid: new secmark value
 *
 * Check if the process should be allowed to relabel packets to @secid.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_secmark_relabel_packet(u32 secid)
{
	return call_int_hook(secmark_relabel_packet, 0, secid);
}
EXPORT_SYMBOL(security_secmark_relabel_packet);

/**
 * security_secmark_refcount_inc() - Increment the secmark labeling rule count
 *
 * Tells the LSM to increment the number of secmark labeling rules loaded.
 */
void security_secmark_refcount_inc(void)
{
	call_void_hook(secmark_refcount_inc);
}
EXPORT_SYMBOL(security_secmark_refcount_inc);

/**
 * security_secmark_refcount_dec() - Decrement the secmark labeling rule count
 *
 * Tells the LSM to decrement the number of secmark labeling rules loaded.
 */
void security_secmark_refcount_dec(void)
{
	call_void_hook(secmark_refcount_dec);
}
EXPORT_SYMBOL(security_secmark_refcount_dec);

/**
 * security_tun_dev_alloc_security() - Allocate a LSM blob for a TUN device
 * @security: pointer to the LSM blob
 *
 * This hook allows a module to allocate a security structure for a TUN	device,
 * returning the pointer in @security.
 *
 * Return: Returns a zero on success, negative values on failure.
 */
int security_tun_dev_alloc_security(void **security)
{
	return call_int_hook(tun_dev_alloc_security, 0, security);
}
EXPORT_SYMBOL(security_tun_dev_alloc_security);

/**
 * security_tun_dev_free_security() - Free a TUN device LSM blob
 * @security: LSM blob
 *
 * This hook allows a module to free the security structure for a TUN device.
 */
void security_tun_dev_free_security(void *security)
{
	call_void_hook(tun_dev_free_security, security);
}
EXPORT_SYMBOL(security_tun_dev_free_security);

/**
 * security_tun_dev_create() - Check if creating a TUN device is allowed
 *
 * Check permissions prior to creating a new TUN device.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_tun_dev_create(void)
{
	return call_int_hook(tun_dev_create, 0);
}
EXPORT_SYMBOL(security_tun_dev_create);

/**
 * security_tun_dev_attach_queue() - Check if attaching a TUN queue is allowed
 * @security: TUN device LSM blob
 *
 * Check permissions prior to attaching to a TUN device queue.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_tun_dev_attach_queue(void *security)
{
	return call_int_hook(tun_dev_attach_queue, 0, security);
}
EXPORT_SYMBOL(security_tun_dev_attach_queue);

/**
 * security_tun_dev_attach() - Update TUN device LSM state on attach
 * @sk: associated sock
 * @security: TUN device LSM blob
 *
 * This hook can be used by the module to update any security state associated
 * with the TUN device's sock structure.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_tun_dev_attach(struct sock *sk, void *security)
{
	return call_int_hook(tun_dev_attach, 0, sk, security);
}
EXPORT_SYMBOL(security_tun_dev_attach);

/**
 * security_tun_dev_open() - Update TUN device LSM state on open
 * @security: TUN device LSM blob
 *
 * This hook can be used by the module to update any security state associated
 * with the TUN device's security structure.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_tun_dev_open(void *security)
{
	return call_int_hook(tun_dev_open, 0, security);
}
EXPORT_SYMBOL(security_tun_dev_open);

/**
 * security_sctp_assoc_request() - Update the LSM on a SCTP association req
 * @asoc: SCTP association
 * @skb: packet requesting the association
 *
 * Passes the @asoc and @chunk->skb of the association INIT packet to the LSM.
 *
 * Return: Returns 0 on success, error on failure.
 */
int security_sctp_assoc_request(struct sctp_association *asoc,
				struct sk_buff *skb)
{
	return call_int_hook(sctp_assoc_request, 0, asoc, skb);
}
EXPORT_SYMBOL(security_sctp_assoc_request);

/**
 * security_sctp_bind_connect() - Validate a list of addrs for a SCTP option
 * @sk: socket
 * @optname: SCTP option to validate
 * @address: list of IP addresses to validate
 * @addrlen: length of the address list
 *
 * Validiate permissions required for each address associated with sock	@sk.
 * Depending on @optname, the addresses will be treated as either a connect or
 * bind service. The @addrlen is calculated on each IPv4 and IPv6 address using
 * sizeof(struct sockaddr_in) or sizeof(struct sockaddr_in6).
 *
 * Return: Returns 0 on success, error on failure.
 */
int security_sctp_bind_connect(struct sock *sk, int optname,
			       struct sockaddr *address, int addrlen)
{
	return call_int_hook(sctp_bind_connect, 0, sk, optname,
			     address, addrlen);
}
EXPORT_SYMBOL(security_sctp_bind_connect);

/**
 * security_sctp_sk_clone() - Clone a SCTP sock's LSM state
 * @asoc: SCTP association
 * @sk: original sock
 * @newsk: target sock
 *
 * Called whenever a new socket is created by accept(2) (i.e. a TCP style
 * socket) or when a socket is 'peeled off' e.g userspace calls
 * sctp_peeloff(3).
 */
void security_sctp_sk_clone(struct sctp_association *asoc, struct sock *sk,
			    struct sock *newsk)
{
	call_void_hook(sctp_sk_clone, asoc, sk, newsk);
}
EXPORT_SYMBOL(security_sctp_sk_clone);

/**
 * security_sctp_assoc_established() - Update LSM state when assoc established
 * @asoc: SCTP association
 * @skb: packet establishing the association
 *
 * Passes the @asoc and @chunk->skb of the association COOKIE_ACK packet to the
 * security module.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_sctp_assoc_established(struct sctp_association *asoc,
				    struct sk_buff *skb)
{
	return call_int_hook(sctp_assoc_established, 0, asoc, skb);
}
EXPORT_SYMBOL(security_sctp_assoc_established);

/**
 * security_mptcp_add_subflow() - Inherit the LSM label from the MPTCP socket
 * @sk: the owning MPTCP socket
 * @ssk: the new subflow
 *
 * Update the labeling for the given MPTCP subflow, to match the one of the
 * owning MPTCP socket. This hook has to be called after the socket creation and
 * initialization via the security_socket_create() and
 * security_socket_post_create() LSM hooks.
 *
 * Return: Returns 0 on success or a negative error code on failure.
 */
int security_mptcp_add_subflow(struct sock *sk, struct sock *ssk)
{
	return call_int_hook(mptcp_add_subflow, 0, sk, ssk);
}

#endif	/* CONFIG_SECURITY_NETWORK */

#ifdef CONFIG_SECURITY_INFINIBAND
/**
 * security_ib_pkey_access() - Check if access to an IB pkey is allowed
 * @sec: LSM blob
 * @subnet_prefix: subnet prefix of the port
 * @pkey: IB pkey
 *
 * Check permission to access a pkey when modifying a QP.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_ib_pkey_access(void *sec, u64 subnet_prefix, u16 pkey)
{
	return call_int_hook(ib_pkey_access, 0, sec, subnet_prefix, pkey);
}
EXPORT_SYMBOL(security_ib_pkey_access);

/**
 * security_ib_endport_manage_subnet() - Check if SMPs traffic is allowed
 * @sec: LSM blob
 * @dev_name: IB device name
 * @port_num: port number
 *
 * Check permissions to send and receive SMPs on a end port.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_ib_endport_manage_subnet(void *sec,
				      const char *dev_name, u8 port_num)
{
	return call_int_hook(ib_endport_manage_subnet, 0, sec,
			     dev_name, port_num);
}
EXPORT_SYMBOL(security_ib_endport_manage_subnet);

/**
 * security_ib_alloc_security() - Allocate an Infiniband LSM blob
 * @sec: LSM blob
 *
 * Allocate a security structure for Infiniband objects.
 *
 * Return: Returns 0 on success, non-zero on failure.
 */
int security_ib_alloc_security(void **sec)
{
	return call_int_hook(ib_alloc_security, 0, sec);
}
EXPORT_SYMBOL(security_ib_alloc_security);

/**
 * security_ib_free_security() - Free an Infiniband LSM blob
 * @sec: LSM blob
 *
 * Deallocate an Infiniband security structure.
 */
void security_ib_free_security(void *sec)
{
	call_void_hook(ib_free_security, sec);
}
EXPORT_SYMBOL(security_ib_free_security);
#endif	/* CONFIG_SECURITY_INFINIBAND */

#ifdef CONFIG_SECURITY_NETWORK_XFRM
/**
 * security_xfrm_policy_alloc() - Allocate a xfrm policy LSM blob
 * @ctxp: xfrm security context being added to the SPD
 * @sec_ctx: security label provided by userspace
 * @gfp: gfp flags
 *
 * Allocate a security structure to the xp->security field; the security field
 * is initialized to NULL when the xfrm_policy is allocated.
 *
 * Return:  Return 0 if operation was successful.
 */
int security_xfrm_policy_alloc(struct xfrm_sec_ctx **ctxp,
			       struct xfrm_user_sec_ctx *sec_ctx,
			       gfp_t gfp)
{
	return call_int_hook(xfrm_policy_alloc_security, 0, ctxp, sec_ctx, gfp);
}
EXPORT_SYMBOL(security_xfrm_policy_alloc);

/**
 * security_xfrm_policy_clone() - Clone xfrm policy LSM state
 * @old_ctx: xfrm security context
 * @new_ctxp: target xfrm security context
 *
 * Allocate a security structure in new_ctxp that contains the information from
 * the old_ctx structure.
 *
 * Return: Return 0 if operation was successful.
 */
int security_xfrm_policy_clone(struct xfrm_sec_ctx *old_ctx,
			       struct xfrm_sec_ctx **new_ctxp)
{
	return call_int_hook(xfrm_policy_clone_security, 0, old_ctx, new_ctxp);
}

/**
 * security_xfrm_policy_free() - Free a xfrm security context
 * @ctx: xfrm security context
 *
 * Free LSM resources associated with @ctx.
 */
void security_xfrm_policy_free(struct xfrm_sec_ctx *ctx)
{
	call_void_hook(xfrm_policy_free_security, ctx);
}
EXPORT_SYMBOL(security_xfrm_policy_free);

/**
 * security_xfrm_policy_delete() - Check if deleting a xfrm policy is allowed
 * @ctx: xfrm security context
 *
 * Authorize deletion of a SPD entry.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_xfrm_policy_delete(struct xfrm_sec_ctx *ctx)
{
	return call_int_hook(xfrm_policy_delete_security, 0, ctx);
}

/**
 * security_xfrm_state_alloc() - Allocate a xfrm state LSM blob
 * @x: xfrm state being added to the SAD
 * @sec_ctx: security label provided by userspace
 *
 * Allocate a security structure to the @x->security field; the security field
 * is initialized to NULL when the xfrm_state is allocated. Set the context to
 * correspond to @sec_ctx.
 *
 * Return: Return 0 if operation was successful.
 */
int security_xfrm_state_alloc(struct xfrm_state *x,
			      struct xfrm_user_sec_ctx *sec_ctx)
{
	return call_int_hook(xfrm_state_alloc, 0, x, sec_ctx);
}
EXPORT_SYMBOL(security_xfrm_state_alloc);

/**
 * security_xfrm_state_alloc_acquire() - Allocate a xfrm state LSM blob
 * @x: xfrm state being added to the SAD
 * @polsec: associated policy's security context
 * @secid: secid from the flow
 *
 * Allocate a security structure to the x->security field; the security field
 * is initialized to NULL when the xfrm_state is allocated.  Set the context to
 * correspond to secid.
 *
 * Return: Returns 0 if operation was successful.
 */
int security_xfrm_state_alloc_acquire(struct xfrm_state *x,
				      struct xfrm_sec_ctx *polsec, u32 secid)
{
	return call_int_hook(xfrm_state_alloc_acquire, 0, x, polsec, secid);
}

/**
 * security_xfrm_state_delete() - Check if deleting a xfrm state is allowed
 * @x: xfrm state
 *
 * Authorize deletion of x->security.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_xfrm_state_delete(struct xfrm_state *x)
{
	return call_int_hook(xfrm_state_delete_security, 0, x);
}
EXPORT_SYMBOL(security_xfrm_state_delete);

/**
 * security_xfrm_state_free() - Free a xfrm state
 * @x: xfrm state
 *
 * Deallocate x->security.
 */
void security_xfrm_state_free(struct xfrm_state *x)
{
	call_void_hook(xfrm_state_free_security, x);
}

/**
 * security_xfrm_policy_lookup() - Check if using a xfrm policy is allowed
 * @ctx: target xfrm security context
 * @fl_secid: flow secid used to authorize access
 *
 * Check permission when a flow selects a xfrm_policy for processing XFRMs on a
 * packet.  The hook is called when selecting either a per-socket policy or a
 * generic xfrm policy.
 *
 * Return: Return 0 if permission is granted, -ESRCH otherwise, or -errno on
 *         other errors.
 */
int security_xfrm_policy_lookup(struct xfrm_sec_ctx *ctx, u32 fl_secid)
{
	return call_int_hook(xfrm_policy_lookup, 0, ctx, fl_secid);
}

/**
 * security_xfrm_state_pol_flow_match() - Check for a xfrm match
 * @x: xfrm state to match
 * @xp: xfrm policy to check for a match
 * @flic: flow to check for a match.
 *
 * Check @xp and @flic for a match with @x.
 *
 * Return: Returns 1 if there is a match.
 */
int security_xfrm_state_pol_flow_match(struct xfrm_state *x,
				       struct xfrm_policy *xp,
				       const struct flowi_common *flic)
{
	struct security_hook_list *hp;
	int rc = LSM_RET_DEFAULT(xfrm_state_pol_flow_match);

	/*
	 * Since this function is expected to return 0 or 1, the judgment
	 * becomes difficult if multiple LSMs supply this call. Fortunately,
	 * we can use the first LSM's judgment because currently only SELinux
	 * supplies this call.
	 *
	 * For speed optimization, we explicitly break the loop rather than
	 * using the macro
	 */
	hlist_for_each_entry(hp, &security_hook_heads.xfrm_state_pol_flow_match,
			     list) {
		rc = hp->hook.xfrm_state_pol_flow_match(x, xp, flic);
		break;
	}
	return rc;
}

/**
 * security_xfrm_decode_session() - Determine the xfrm secid for a packet
 * @skb: xfrm packet
 * @secid: secid
 *
 * Decode the packet in @skb and return the security label in @secid.
 *
 * Return: Return 0 if all xfrms used have the same secid.
 */
int security_xfrm_decode_session(struct sk_buff *skb, u32 *secid)
{
	return call_int_hook(xfrm_decode_session, 0, skb, secid, 1);
}

void security_skb_classify_flow(struct sk_buff *skb, struct flowi_common *flic)
{
	int rc = call_int_hook(xfrm_decode_session, 0, skb, &flic->flowic_secid,
			       0);

	BUG_ON(rc);
}
EXPORT_SYMBOL(security_skb_classify_flow);
#endif	/* CONFIG_SECURITY_NETWORK_XFRM */

#ifdef CONFIG_KEYS
/**
 * security_key_alloc() - Allocate and initialize a kernel key LSM blob
 * @key: key
 * @cred: credentials
 * @flags: allocation flags
 *
 * Permit allocation of a key and assign security data. Note that key does not
 * have a serial number assigned at this point.
 *
 * Return: Return 0 if permission is granted, -ve error otherwise.
 */
int security_key_alloc(struct key *key, const struct cred *cred,
		       unsigned long flags)
{
	return call_int_hook(key_alloc, 0, key, cred, flags);
}

/**
 * security_key_free() - Free a kernel key LSM blob
 * @key: key
 *
 * Notification of destruction; free security data.
 */
void security_key_free(struct key *key)
{
	call_void_hook(key_free, key);
}

/**
 * security_key_permission() - Check if a kernel key operation is allowed
 * @key_ref: key reference
 * @cred: credentials of actor requesting access
 * @need_perm: requested permissions
 *
 * See whether a specific operational right is granted to a process on a key.
 *
 * Return: Return 0 if permission is granted, -ve error otherwise.
 */
int security_key_permission(key_ref_t key_ref, const struct cred *cred,
			    enum key_need_perm need_perm)
{
	return call_int_hook(key_permission, 0, key_ref, cred, need_perm);
}

/**
 * security_key_getsecurity() - Get the key's security label
 * @key: key
 * @buffer: security label buffer
 *
 * Get a textual representation of the security context attached to a key for
 * the purposes of honouring KEYCTL_GETSECURITY.  This function allocates the
 * storage for the NUL-terminated string and the caller should free it.
 *
 * Return: Returns the length of @buffer (including terminating NUL) or -ve if
 *         an error occurs.  May also return 0 (and a NULL buffer pointer) if
 *         there is no security label assigned to the key.
 */
int security_key_getsecurity(struct key *key, char **buffer)
{
	*buffer = NULL;
	return call_int_hook(key_getsecurity, 0, key, buffer);
}
#endif	/* CONFIG_KEYS */

#ifdef CONFIG_AUDIT
/**
 * security_audit_rule_init() - Allocate and init an LSM audit rule struct
 * @field: audit action
 * @op: rule operator
 * @rulestr: rule context
 * @lsmrule: receive buffer for audit rule struct
 *
 * Allocate and initialize an LSM audit rule structure.
 *
 * Return: Return 0 if @lsmrule has been successfully set, -EINVAL in case of
 *         an invalid rule.
 */
int security_audit_rule_init(u32 field, u32 op, char *rulestr, void **lsmrule)
{
	return call_int_hook(audit_rule_init, 0, field, op, rulestr, lsmrule);
}

/**
 * security_audit_rule_known() - Check if an audit rule contains LSM fields
 * @krule: audit rule
 *
 * Specifies whether given @krule contains any fields related to the current
 * LSM.
 *
 * Return: Returns 1 in case of relation found, 0 otherwise.
 */
int security_audit_rule_known(struct audit_krule *krule)
{
	return call_int_hook(audit_rule_known, 0, krule);
}

/**
 * security_audit_rule_free() - Free an LSM audit rule struct
 * @lsmrule: audit rule struct
 *
 * Deallocate the LSM audit rule structure previously allocated by
 * audit_rule_init().
 */
void security_audit_rule_free(void *lsmrule)
{
	call_void_hook(audit_rule_free, lsmrule);
}

/**
 * security_audit_rule_match() - Check if a label matches an audit rule
 * @secid: security label
 * @field: LSM audit field
 * @op: matching operator
 * @lsmrule: audit rule
 *
 * Determine if given @secid matches a rule previously approved by
 * security_audit_rule_known().
 *
 * Return: Returns 1 if secid matches the rule, 0 if it does not, -ERRNO on
 *         failure.
 */
int security_audit_rule_match(u32 secid, u32 field, u32 op, void *lsmrule)
{
	return call_int_hook(audit_rule_match, 0, secid, field, op, lsmrule);
}
#endif /* CONFIG_AUDIT */

#ifdef CONFIG_BPF_SYSCALL
/**
 * security_bpf() - Check if the bpf syscall operation is allowed
 * @cmd: command
 * @attr: bpf attribute
 * @size: size
 *
 * Do a initial check for all bpf syscalls after the attribute is copied into
 * the kernel. The actual security module can implement their own rules to
 * check the specific cmd they need.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_bpf(int cmd, union bpf_attr *attr, unsigned int size)
{
	return call_int_hook(bpf, 0, cmd, attr, size);
}

/**
 * security_bpf_map() - Check if access to a bpf map is allowed
 * @map: bpf map
 * @fmode: mode
 *
 * Do a check when the kernel generates and returns a file descriptor for eBPF
 * maps.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_bpf_map(struct bpf_map *map, fmode_t fmode)
{
	return call_int_hook(bpf_map, 0, map, fmode);
}

/**
 * security_bpf_prog() - Check if access to a bpf program is allowed
 * @prog: bpf program
 *
 * Do a check when the kernel generates and returns a file descriptor for eBPF
 * programs.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_bpf_prog(struct bpf_prog *prog)
{
	return call_int_hook(bpf_prog, 0, prog);
}

/**
 * security_bpf_map_alloc() - Allocate a bpf map LSM blob
 * @map: bpf map
 *
 * Initialize the security field inside bpf map.
 *
 * Return: Returns 0 on success, error on failure.
 */
int security_bpf_map_alloc(struct bpf_map *map)
{
	return call_int_hook(bpf_map_alloc_security, 0, map);
}

/**
 * security_bpf_prog_alloc() - Allocate a bpf program LSM blob
 * @aux: bpf program aux info struct
 *
 * Initialize the security field inside bpf program.
 *
 * Return: Returns 0 on success, error on failure.
 */
int security_bpf_prog_alloc(struct bpf_prog_aux *aux)
{
	return call_int_hook(bpf_prog_alloc_security, 0, aux);
}

/**
 * security_bpf_map_free() - Free a bpf map's LSM blob
 * @map: bpf map
 *
 * Clean up the security information stored inside bpf map.
 */
void security_bpf_map_free(struct bpf_map *map)
{
	call_void_hook(bpf_map_free_security, map);
}

/**
 * security_bpf_prog_free() - Free a bpf program's LSM blob
 * @aux: bpf program aux info struct
 *
 * Clean up the security information stored inside bpf prog.
 */
void security_bpf_prog_free(struct bpf_prog_aux *aux)
{
	call_void_hook(bpf_prog_free_security, aux);
}
#endif /* CONFIG_BPF_SYSCALL */

/**
 * security_locked_down() - Check if a kernel feature is allowed
 * @what: requested kernel feature
 *
 * Determine whether a kernel feature that potentially enables arbitrary code
 * execution in kernel space should be permitted.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_locked_down(enum lockdown_reason what)
{
	return call_int_hook(locked_down, 0, what);
}
EXPORT_SYMBOL(security_locked_down);

#ifdef CONFIG_PERF_EVENTS
/**
 * security_perf_event_open() - Check if a perf event open is allowed
 * @attr: perf event attribute
 * @type: type of event
 *
 * Check whether the @type of perf_event_open syscall is allowed.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_perf_event_open(struct perf_event_attr *attr, int type)
{
	return call_int_hook(perf_event_open, 0, attr, type);
}

/**
 * security_perf_event_alloc() - Allocate a perf event LSM blob
 * @event: perf event
 *
 * Allocate and save perf_event security info.
 *
 * Return: Returns 0 on success, error on failure.
 */
int security_perf_event_alloc(struct perf_event *event)
{
	return call_int_hook(perf_event_alloc, 0, event);
}

/**
 * security_perf_event_free() - Free a perf event LSM blob
 * @event: perf event
 *
 * Release (free) perf_event security info.
 */
void security_perf_event_free(struct perf_event *event)
{
	call_void_hook(perf_event_free, event);
}

/**
 * security_perf_event_read() - Check if reading a perf event label is allowed
 * @event: perf event
 *
 * Read perf_event security info if allowed.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_perf_event_read(struct perf_event *event)
{
	return call_int_hook(perf_event_read, 0, event);
}

/**
 * security_perf_event_write() - Check if writing a perf event label is allowed
 * @event: perf event
 *
 * Write perf_event security info if allowed.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_perf_event_write(struct perf_event *event)
{
	return call_int_hook(perf_event_write, 0, event);
}
#endif /* CONFIG_PERF_EVENTS */

#ifdef CONFIG_IO_URING
/**
 * security_uring_override_creds() - Check if overriding creds is allowed
 * @new: new credentials
 *
 * Check if the current task, executing an io_uring operation, is allowed to
 * override it's credentials with @new.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_uring_override_creds(const struct cred *new)
{
	return call_int_hook(uring_override_creds, 0, new);
}

/**
 * security_uring_sqpoll() - Check if IORING_SETUP_SQPOLL is allowed
 *
 * Check whether the current task is allowed to spawn a io_uring polling thread
 * (IORING_SETUP_SQPOLL).
 *
 * Return: Returns 0 if permission is granted.
 */
int security_uring_sqpoll(void)
{
	return call_int_hook(uring_sqpoll, 0);
}

/**
 * security_uring_cmd() - Check if a io_uring passthrough command is allowed
 * @ioucmd: command
 *
 * Check whether the file_operations uring_cmd is allowed to run.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_uring_cmd(struct io_uring_cmd *ioucmd)
{
	return call_int_hook(uring_cmd, 0, ioucmd);
}
#endif /* CONFIG_IO_URING */
