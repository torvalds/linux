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
#include <linux/fsnotify.h>
#include <linux/mman.h>
#include <linux/mount.h>
#include <linux/personality.h>
#include <linux/backing-dev.h>
#include <linux/string.h>
#include <linux/xattr.h>
#include <linux/msg.h>
#include <linux/overflow.h>
#include <linux/perf_event.h>
#include <linux/fs.h>
#include <net/flow.h>
#include <net/sock.h>

#define SECURITY_HOOK_ACTIVE_KEY(HOOK, IDX) security_hook_active_##HOOK##_##IDX

/*
 * Identifier for the LSM static calls.
 * HOOK is an LSM hook as defined in linux/lsm_hookdefs.h
 * IDX is the index of the static call. 0 <= NUM < MAX_LSM_COUNT
 */
#define LSM_STATIC_CALL(HOOK, IDX) lsm_static_call_##HOOK##_##IDX

/*
 * Call the macro M for each LSM hook MAX_LSM_COUNT times.
 */
#define LSM_LOOP_UNROLL(M, ...) 		\
do {						\
	UNROLL(MAX_LSM_COUNT, M, __VA_ARGS__)	\
} while (0)

#define LSM_DEFINE_UNROLL(M, ...) UNROLL(MAX_LSM_COUNT, M, __VA_ARGS__)

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
static __initdata struct lsm_info *ordered_lsms[MAX_LSM_COUNT + 1];
static __initdata struct lsm_info *exclusive;

#ifdef CONFIG_HAVE_STATIC_CALL
#define LSM_HOOK_TRAMP(NAME, NUM) \
	&STATIC_CALL_TRAMP(LSM_STATIC_CALL(NAME, NUM))
#else
#define LSM_HOOK_TRAMP(NAME, NUM) NULL
#endif

/*
 * Define static calls and static keys for each LSM hook.
 */
#define DEFINE_LSM_STATIC_CALL(NUM, NAME, RET, ...)			\
	DEFINE_STATIC_CALL_NULL(LSM_STATIC_CALL(NAME, NUM),		\
				*((RET(*)(__VA_ARGS__))NULL));		\
	DEFINE_STATIC_KEY_FALSE(SECURITY_HOOK_ACTIVE_KEY(NAME, NUM));

#define LSM_HOOK(RET, DEFAULT, NAME, ...)				\
	LSM_DEFINE_UNROLL(DEFINE_LSM_STATIC_CALL, NAME, RET, __VA_ARGS__)
#include <linux/lsm_hook_defs.h>
#undef LSM_HOOK
#undef DEFINE_LSM_STATIC_CALL

/*
 * Initialise a table of static calls for each LSM hook.
 * DEFINE_STATIC_CALL_NULL invocation above generates a key (STATIC_CALL_KEY)
 * and a trampoline (STATIC_CALL_TRAMP) which are used to call
 * __static_call_update when updating the static call.
 *
 * The static calls table is used by early LSMs, some architectures can fault on
 * unaligned accesses and the fault handling code may not be ready by then.
 * Thus, the static calls table should be aligned to avoid any unhandled faults
 * in early init.
 */
struct lsm_static_calls_table
	static_calls_table __ro_after_init __aligned(sizeof(u64)) = {
#define INIT_LSM_STATIC_CALL(NUM, NAME)					\
	(struct lsm_static_call) {					\
		.key = &STATIC_CALL_KEY(LSM_STATIC_CALL(NAME, NUM)),	\
		.trampoline = LSM_HOOK_TRAMP(NAME, NUM),		\
		.active = &SECURITY_HOOK_ACTIVE_KEY(NAME, NUM),		\
	},
#define LSM_HOOK(RET, DEFAULT, NAME, ...)				\
	.NAME = {							\
		LSM_DEFINE_UNROLL(INIT_LSM_STATIC_CALL, NAME)		\
	},
#include <linux/lsm_hook_defs.h>
#undef LSM_HOOK
#undef INIT_LSM_STATIC_CALL
	};

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

	if (WARN(last_lsm == MAX_LSM_COUNT, "%s: out of LSM static calls!?\n", from))
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
	lsm_set_blob_size(&needed->lbs_ib, &blob_sizes.lbs_ib);
	/*
	 * The inode blob gets an rcu_head in addition to
	 * what the modules might need.
	 */
	if (needed->lbs_inode && blob_sizes.lbs_inode == 0)
		blob_sizes.lbs_inode = sizeof(struct rcu_head);
	lsm_set_blob_size(&needed->lbs_inode, &blob_sizes.lbs_inode);
	lsm_set_blob_size(&needed->lbs_ipc, &blob_sizes.lbs_ipc);
	lsm_set_blob_size(&needed->lbs_key, &blob_sizes.lbs_key);
	lsm_set_blob_size(&needed->lbs_msg_msg, &blob_sizes.lbs_msg_msg);
	lsm_set_blob_size(&needed->lbs_perf_event, &blob_sizes.lbs_perf_event);
	lsm_set_blob_size(&needed->lbs_sock, &blob_sizes.lbs_sock);
	lsm_set_blob_size(&needed->lbs_superblock, &blob_sizes.lbs_superblock);
	lsm_set_blob_size(&needed->lbs_task, &blob_sizes.lbs_task);
	lsm_set_blob_size(&needed->lbs_tun_dev, &blob_sizes.lbs_tun_dev);
	lsm_set_blob_size(&needed->lbs_xattr_count,
			  &blob_sizes.lbs_xattr_count);
	lsm_set_blob_size(&needed->lbs_bdev, &blob_sizes.lbs_bdev);
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

/*
 * Current index to use while initializing the lsm id list.
 */
u32 lsm_active_cnt __ro_after_init;
const struct lsm_id *lsm_idlist[MAX_LSM_COUNT];

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
}

int __init early_security_init(void)
{
	struct lsm_info *lsm;

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
 * @lsmid: the identification information for the security module
 *
 * Each LSM has to register its hooks with the infrastructure.
 */
void __init security_add_hooks(struct security_hook_list *hooks, int count,
			       const struct lsm_id *lsmid)
{
	int i;

	/*
	 * A security module may call security_add_hooks() more
	 * than once during initialization, and LSM initialization
	 * is serialized. Landlock is one such case.
	 * Look at the previous entry, if there is one, for duplication.
	 */
	if (lsm_active_cnt == 0 || lsm_idlist[lsm_active_cnt - 1] != lsmid) {
		if (lsm_active_cnt >= MAX_LSM_COUNT)
			panic("%s Too many LSMs registered.\n", __func__);
		lsm_idlist[lsm_active_cnt++] = lsmid;
	}

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
 * lsm_blob_alloc - allocate a composite blob
 * @dest: the destination for the blob
 * @size: the size of the blob
 * @gfp: allocation type
 *
 * Allocate a blob for all the modules
 *
 * Returns 0, or -ENOMEM if memory can't be allocated.
 */
static int lsm_blob_alloc(void **dest, size_t size, gfp_t gfp)
{
	if (size == 0) {
		*dest = NULL;
		return 0;
	}

	*dest = kzalloc(size, gfp);
	if (*dest == NULL)
		return -ENOMEM;
	return 0;
}

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
	return lsm_blob_alloc(&cred->security, blob_sizes.lbs_cred, gfp);
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
 * @gfp: allocation flags
 *
 * Allocate the inode blob for all the modules
 *
 * Returns 0, or -ENOMEM if memory can't be allocated.
 */
static int lsm_inode_alloc(struct inode *inode, gfp_t gfp)
{
	if (!lsm_inode_cache) {
		inode->i_security = NULL;
		return 0;
	}

	inode->i_security = kmem_cache_zalloc(lsm_inode_cache, gfp);
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
	return lsm_blob_alloc(&task->security, blob_sizes.lbs_task, GFP_KERNEL);
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
	return lsm_blob_alloc(&kip->security, blob_sizes.lbs_ipc, GFP_KERNEL);
}

#ifdef CONFIG_KEYS
/**
 * lsm_key_alloc - allocate a composite key blob
 * @key: the key that needs a blob
 *
 * Allocate the key blob for all the modules
 *
 * Returns 0, or -ENOMEM if memory can't be allocated.
 */
static int lsm_key_alloc(struct key *key)
{
	return lsm_blob_alloc(&key->security, blob_sizes.lbs_key, GFP_KERNEL);
}
#endif /* CONFIG_KEYS */

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
	return lsm_blob_alloc(&mp->security, blob_sizes.lbs_msg_msg,
			      GFP_KERNEL);
}

/**
 * lsm_bdev_alloc - allocate a composite block_device blob
 * @bdev: the block_device that needs a blob
 *
 * Allocate the block_device blob for all the modules
 *
 * Returns 0, or -ENOMEM if memory can't be allocated.
 */
static int lsm_bdev_alloc(struct block_device *bdev)
{
	if (blob_sizes.lbs_bdev == 0) {
		bdev->bd_security = NULL;
		return 0;
	}

	bdev->bd_security = kzalloc(blob_sizes.lbs_bdev, GFP_KERNEL);
	if (!bdev->bd_security)
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
	return lsm_blob_alloc(&sb->s_security, blob_sizes.lbs_superblock,
			      GFP_KERNEL);
}

/**
 * lsm_fill_user_ctx - Fill a user space lsm_ctx structure
 * @uctx: a userspace LSM context to be filled
 * @uctx_len: available uctx size (input), used uctx size (output)
 * @val: the new LSM context value
 * @val_len: the size of the new LSM context value
 * @id: LSM id
 * @flags: LSM defined flags
 *
 * Fill all of the fields in a userspace lsm_ctx structure.  If @uctx is NULL
 * simply calculate the required size to output via @utc_len and return
 * success.
 *
 * Returns 0 on success, -E2BIG if userspace buffer is not large enough,
 * -EFAULT on a copyout error, -ENOMEM if memory can't be allocated.
 */
int lsm_fill_user_ctx(struct lsm_ctx __user *uctx, u32 *uctx_len,
		      void *val, size_t val_len,
		      u64 id, u64 flags)
{
	struct lsm_ctx *nctx = NULL;
	size_t nctx_len;
	int rc = 0;

	nctx_len = ALIGN(struct_size(nctx, ctx, val_len), sizeof(void *));
	if (nctx_len > *uctx_len) {
		rc = -E2BIG;
		goto out;
	}

	/* no buffer - return success/0 and set @uctx_len to the req size */
	if (!uctx)
		goto out;

	nctx = kzalloc(nctx_len, GFP_KERNEL);
	if (nctx == NULL) {
		rc = -ENOMEM;
		goto out;
	}
	nctx->id = id;
	nctx->flags = flags;
	nctx->len = nctx_len;
	nctx->ctx_len = val_len;
	memcpy(nctx->ctx, val, val_len);

	if (copy_to_user(uctx, nctx, nctx_len))
		rc = -EFAULT;

out:
	kfree(nctx);
	*uctx_len = nctx_len;
	return rc;
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
#define __CALL_STATIC_VOID(NUM, HOOK, ...)				     \
do {									     \
	if (static_branch_unlikely(&SECURITY_HOOK_ACTIVE_KEY(HOOK, NUM))) {    \
		static_call(LSM_STATIC_CALL(HOOK, NUM))(__VA_ARGS__);	     \
	}								     \
} while (0);

#define call_void_hook(HOOK, ...)                                 \
	do {                                                      \
		LSM_LOOP_UNROLL(__CALL_STATIC_VOID, HOOK, __VA_ARGS__); \
	} while (0)


#define __CALL_STATIC_INT(NUM, R, HOOK, LABEL, ...)			     \
do {									     \
	if (static_branch_unlikely(&SECURITY_HOOK_ACTIVE_KEY(HOOK, NUM))) {  \
		R = static_call(LSM_STATIC_CALL(HOOK, NUM))(__VA_ARGS__);    \
		if (R != LSM_RET_DEFAULT(HOOK))				     \
			goto LABEL;					     \
	}								     \
} while (0);

#define call_int_hook(HOOK, ...)					\
({									\
	__label__ OUT;							\
	int RC = LSM_RET_DEFAULT(HOOK);					\
									\
	LSM_LOOP_UNROLL(__CALL_STATIC_INT, RC, HOOK, OUT, __VA_ARGS__);	\
OUT:									\
	RC;								\
})

#define lsm_for_each_hook(scall, NAME)					\
	for (scall = static_calls_table.NAME;				\
	     scall - static_calls_table.NAME < MAX_LSM_COUNT; scall++)  \
		if (static_key_enabled(&scall->active->key))

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
	return call_int_hook(binder_set_context_mgr, mgr);
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
	return call_int_hook(binder_transaction, from, to);
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
	return call_int_hook(binder_transfer_binder, from, to);
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
	return call_int_hook(binder_transfer_file, from, to, file);
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
	return call_int_hook(ptrace_access_check, child, mode);
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
	return call_int_hook(ptrace_traceme, parent);
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
	return call_int_hook(capget, target, effective, inheritable, permitted);
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
	return call_int_hook(capset, new, old, effective, inheritable,
			     permitted);
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
	return call_int_hook(capable, cred, ns, cap, opts);
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
int security_quotactl(int cmds, int type, int id, const struct super_block *sb)
{
	return call_int_hook(quotactl, cmds, type, id, sb);
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
	return call_int_hook(quota_on, dentry);
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
	return call_int_hook(syslog, type);
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
	return call_int_hook(settime, ts, tz);
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
	struct lsm_static_call *scall;
	int cap_sys_admin = 1;
	int rc;

	/*
	 * The module will respond with 0 if it thinks the __vm_enough_memory()
	 * call should be made with the cap_sys_admin set. If all of the modules
	 * agree that it should be set it will. If any module thinks it should
	 * not be set it won't.
	 */
	lsm_for_each_hook(scall, vm_enough_memory) {
		rc = scall->hl->hook.vm_enough_memory(mm, pages);
		if (rc < 0) {
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
	return call_int_hook(bprm_creds_for_exec, bprm);
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
int security_bprm_creds_from_file(struct linux_binprm *bprm, const struct file *file)
{
	return call_int_hook(bprm_creds_from_file, bprm, file);
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
	return call_int_hook(bprm_check_security, bprm);
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
void security_bprm_committing_creds(const struct linux_binprm *bprm)
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
void security_bprm_committed_creds(const struct linux_binprm *bprm)
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
	return call_int_hook(fs_context_submount, fc, reference);
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
	return call_int_hook(fs_context_dup, fc, src_fc);
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
	struct lsm_static_call *scall;
	int trc;
	int rc = -ENOPARAM;

	lsm_for_each_hook(scall, fs_context_parse_param) {
		trc = scall->hl->hook.fs_context_parse_param(fc, param);
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
	rc = call_int_hook(sb_alloc_security, sb);
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
	return call_int_hook(sb_eat_lsm_opts, options, mnt_opts);
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
	return call_int_hook(sb_mnt_opts_compat, sb, mnt_opts);
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
	return call_int_hook(sb_remount, sb, mnt_opts);
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
int security_sb_kern_mount(const struct super_block *sb)
{
	return call_int_hook(sb_kern_mount, sb);
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
	return call_int_hook(sb_show_options, m, sb);
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
	return call_int_hook(sb_statfs, dentry);
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
	return call_int_hook(sb_mount, dev_name, path, type, flags, data);
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
	return call_int_hook(sb_umount, mnt, flags);
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
	return call_int_hook(sb_pivotroot, old_path, new_path);
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
	struct lsm_static_call *scall;
	int rc = mnt_opts ? -EOPNOTSUPP : LSM_RET_DEFAULT(sb_set_mnt_opts);

	lsm_for_each_hook(scall, sb_set_mnt_opts) {
		rc = scall->hl->hook.sb_set_mnt_opts(sb, mnt_opts, kern_flags,
					      set_kern_flags);
		if (rc != LSM_RET_DEFAULT(sb_set_mnt_opts))
			break;
	}
	return rc;
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
	return call_int_hook(sb_clone_mnt_opts, oldsb, newsb,
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
	return call_int_hook(move_mount, from_path, to_path);
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
	return call_int_hook(path_notify, path, mask, obj_type);
}

/**
 * security_inode_alloc() - Allocate an inode LSM blob
 * @inode: the inode
 * @gfp: allocation flags
 *
 * Allocate and attach a security structure to @inode->i_security.  The
 * i_security field is initialized to NULL when the inode structure is
 * allocated.
 *
 * Return: Return 0 if operation was successful.
 */
int security_inode_alloc(struct inode *inode, gfp_t gfp)
{
	int rc = lsm_inode_alloc(inode, gfp);

	if (unlikely(rc))
		return rc;
	rc = call_int_hook(inode_alloc_security, inode);
	if (unlikely(rc))
		security_inode_free(inode);
	return rc;
}

static void inode_free_by_rcu(struct rcu_head *head)
{
	/* The rcu head is at the start of the inode blob */
	call_void_hook(inode_free_security_rcu, head);
	kmem_cache_free(lsm_inode_cache, head);
}

/**
 * security_inode_free() - Free an inode's LSM blob
 * @inode: the inode
 *
 * Release any LSM resources associated with @inode, although due to the
 * inode's RCU protections it is possible that the resources will not be
 * fully released until after the current RCU grace period has elapsed.
 *
 * It is important for LSMs to note that despite being present in a call to
 * security_inode_free(), @inode may still be referenced in a VFS path walk
 * and calls to security_inode_permission() may be made during, or after,
 * a call to security_inode_free().  For this reason the inode->i_security
 * field is released via a call_rcu() callback and any LSMs which need to
 * retain inode state for use in security_inode_permission() should only
 * release that state in the inode_free_security_rcu() LSM hook callback.
 */
void security_inode_free(struct inode *inode)
{
	call_void_hook(inode_free_security, inode);
	if (!inode->i_security)
		return;
	call_rcu((struct rcu_head *)inode->i_security, inode_free_by_rcu);
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
	return call_int_hook(dentry_init_security, dentry, mode, name,
			     xattr_name, ctx, ctxlen);
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
	return call_int_hook(dentry_create_files_as, dentry, mode,
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
	struct lsm_static_call *scall;
	struct xattr *new_xattrs = NULL;
	int ret = -EOPNOTSUPP, xattr_count = 0;

	if (unlikely(IS_PRIVATE(inode)))
		return 0;

	if (!blob_sizes.lbs_xattr_count)
		return 0;

	if (initxattrs) {
		/* Allocate +1 as terminator. */
		new_xattrs = kcalloc(blob_sizes.lbs_xattr_count + 1,
				     sizeof(*new_xattrs), GFP_NOFS);
		if (!new_xattrs)
			return -ENOMEM;
	}

	lsm_for_each_hook(scall, inode_init_security) {
		ret = scall->hl->hook.inode_init_security(inode, dir, qstr, new_xattrs,
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
	return call_int_hook(inode_init_security_anon, inode, name,
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
	return call_int_hook(path_mknod, dir, dentry, mode, dev);
}
EXPORT_SYMBOL(security_path_mknod);

/**
 * security_path_post_mknod() - Update inode security after reg file creation
 * @idmap: idmap of the mount
 * @dentry: new file
 *
 * Update inode security field after a regular file has been created.
 */
void security_path_post_mknod(struct mnt_idmap *idmap, struct dentry *dentry)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(dentry))))
		return;
	call_void_hook(path_post_mknod, idmap, dentry);
}

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
	return call_int_hook(path_mkdir, dir, dentry, mode);
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
	return call_int_hook(path_rmdir, dir, dentry);
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
	return call_int_hook(path_unlink, dir, dentry);
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
	return call_int_hook(path_symlink, dir, dentry, old_name);
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
	return call_int_hook(path_link, old_dentry, new_dir, new_dentry);
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

	return call_int_hook(path_rename, old_dir, old_dentry, new_dir,
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
	return call_int_hook(path_truncate, path);
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
	return call_int_hook(path_chmod, path, mode);
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
	return call_int_hook(path_chown, path, uid, gid);
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
	return call_int_hook(path_chroot, path);
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
	return call_int_hook(inode_create, dir, dentry, mode);
}
EXPORT_SYMBOL_GPL(security_inode_create);

/**
 * security_inode_post_create_tmpfile() - Update inode security of new tmpfile
 * @idmap: idmap of the mount
 * @inode: inode of the new tmpfile
 *
 * Update inode security data after a tmpfile has been created.
 */
void security_inode_post_create_tmpfile(struct mnt_idmap *idmap,
					struct inode *inode)
{
	if (unlikely(IS_PRIVATE(inode)))
		return;
	call_void_hook(inode_post_create_tmpfile, idmap, inode);
}

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
	return call_int_hook(inode_link, old_dentry, dir, new_dentry);
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
	return call_int_hook(inode_unlink, dir, dentry);
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
	return call_int_hook(inode_symlink, dir, dentry, old_name);
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
	return call_int_hook(inode_mkdir, dir, dentry, mode);
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
	return call_int_hook(inode_rmdir, dir, dentry);
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
	return call_int_hook(inode_mknod, dir, dentry, mode, dev);
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
		int err = call_int_hook(inode_rename, new_dir, new_dentry,
					old_dir, old_dentry);
		if (err)
			return err;
	}

	return call_int_hook(inode_rename, old_dir, old_dentry,
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
	return call_int_hook(inode_readlink, dentry);
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
	return call_int_hook(inode_follow_link, dentry, inode, rcu);
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
	return call_int_hook(inode_permission, inode, mask);
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
	if (unlikely(IS_PRIVATE(d_backing_inode(dentry))))
		return 0;
	return call_int_hook(inode_setattr, idmap, dentry, attr);
}
EXPORT_SYMBOL_GPL(security_inode_setattr);

/**
 * security_inode_post_setattr() - Update the inode after a setattr operation
 * @idmap: idmap of the mount
 * @dentry: file
 * @ia_valid: file attributes set
 *
 * Update inode security field after successful setting file attributes.
 */
void security_inode_post_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
				 int ia_valid)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(dentry))))
		return;
	call_void_hook(inode_post_setattr, idmap, dentry, ia_valid);
}

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
	return call_int_hook(inode_getattr, path);
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
 * This hook performs the desired permission checks before setting the extended
 * attributes (xattrs) on @dentry.  It is important to note that we have some
 * additional logic before the main LSM implementation calls to detect if we
 * need to perform an additional capability check at the LSM layer.
 *
 * Normally we enforce a capability check prior to executing the various LSM
 * hook implementations, but if a LSM wants to avoid this capability check,
 * it can register a 'inode_xattr_skipcap' hook and return a value of 1 for
 * xattrs that it wants to avoid the capability check, leaving the LSM fully
 * responsible for enforcing the access control for the specific xattr.  If all
 * of the enabled LSMs refrain from registering a 'inode_xattr_skipcap' hook,
 * or return a 0 (the default return value), the capability check is still
 * performed.  If no 'inode_xattr_skipcap' hooks are registered the capability
 * check is performed.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_inode_setxattr(struct mnt_idmap *idmap,
			    struct dentry *dentry, const char *name,
			    const void *value, size_t size, int flags)
{
	int rc;

	if (unlikely(IS_PRIVATE(d_backing_inode(dentry))))
		return 0;

	/* enforce the capability checks at the lsm layer, if needed */
	if (!call_int_hook(inode_xattr_skipcap, name)) {
		rc = cap_inode_setxattr(dentry, name, value, size, flags);
		if (rc)
			return rc;
	}

	return call_int_hook(inode_setxattr, idmap, dentry, name, value, size,
			     flags);
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
	if (unlikely(IS_PRIVATE(d_backing_inode(dentry))))
		return 0;
	return call_int_hook(inode_set_acl, idmap, dentry, acl_name, kacl);
}

/**
 * security_inode_post_set_acl() - Update inode security from posix acls set
 * @dentry: file
 * @acl_name: acl name
 * @kacl: acl struct
 *
 * Update inode security data after successfully setting posix acls on @dentry.
 * The posix acls in @kacl are identified by @acl_name.
 */
void security_inode_post_set_acl(struct dentry *dentry, const char *acl_name,
				 struct posix_acl *kacl)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(dentry))))
		return;
	call_void_hook(inode_post_set_acl, dentry, acl_name, kacl);
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
	return call_int_hook(inode_get_acl, idmap, dentry, acl_name);
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
	if (unlikely(IS_PRIVATE(d_backing_inode(dentry))))
		return 0;
	return call_int_hook(inode_remove_acl, idmap, dentry, acl_name);
}

/**
 * security_inode_post_remove_acl() - Update inode security after rm posix acls
 * @idmap: idmap of the mount
 * @dentry: file
 * @acl_name: acl name
 *
 * Update inode security data after successfully removing posix acls on
 * @dentry in @idmap. The posix acls are identified by @acl_name.
 */
void security_inode_post_remove_acl(struct mnt_idmap *idmap,
				    struct dentry *dentry, const char *acl_name)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(dentry))))
		return;
	call_void_hook(inode_post_remove_acl, idmap, dentry, acl_name);
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
	return call_int_hook(inode_getxattr, dentry, name);
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
	return call_int_hook(inode_listxattr, dentry);
}

/**
 * security_inode_removexattr() - Check if removing an xattr is allowed
 * @idmap: idmap of the mount
 * @dentry: file
 * @name: xattr name
 *
 * This hook performs the desired permission checks before setting the extended
 * attributes (xattrs) on @dentry.  It is important to note that we have some
 * additional logic before the main LSM implementation calls to detect if we
 * need to perform an additional capability check at the LSM layer.
 *
 * Normally we enforce a capability check prior to executing the various LSM
 * hook implementations, but if a LSM wants to avoid this capability check,
 * it can register a 'inode_xattr_skipcap' hook and return a value of 1 for
 * xattrs that it wants to avoid the capability check, leaving the LSM fully
 * responsible for enforcing the access control for the specific xattr.  If all
 * of the enabled LSMs refrain from registering a 'inode_xattr_skipcap' hook,
 * or return a 0 (the default return value), the capability check is still
 * performed.  If no 'inode_xattr_skipcap' hooks are registered the capability
 * check is performed.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_inode_removexattr(struct mnt_idmap *idmap,
			       struct dentry *dentry, const char *name)
{
	int rc;

	if (unlikely(IS_PRIVATE(d_backing_inode(dentry))))
		return 0;

	/* enforce the capability checks at the lsm layer, if needed */
	if (!call_int_hook(inode_xattr_skipcap, name)) {
		rc = cap_inode_removexattr(idmap, dentry, name);
		if (rc)
			return rc;
	}

	return call_int_hook(inode_removexattr, idmap, dentry, name);
}

/**
 * security_inode_post_removexattr() - Update the inode after a removexattr op
 * @dentry: file
 * @name: xattr name
 *
 * Update the inode after a successful removexattr operation.
 */
void security_inode_post_removexattr(struct dentry *dentry, const char *name)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(dentry))))
		return;
	call_void_hook(inode_post_removexattr, dentry, name);
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
	return call_int_hook(inode_need_killpriv, dentry);
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
	return call_int_hook(inode_killpriv, idmap, dentry);
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
	if (unlikely(IS_PRIVATE(inode)))
		return LSM_RET_DEFAULT(inode_getsecurity);

	return call_int_hook(inode_getsecurity, idmap, inode, name, buffer,
			     alloc);
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
	if (unlikely(IS_PRIVATE(inode)))
		return LSM_RET_DEFAULT(inode_setsecurity);

	return call_int_hook(inode_setsecurity, inode, name, value, size,
			     flags);
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
	return call_int_hook(inode_listsecurity, inode, buffer, buffer_size);
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
	return call_int_hook(inode_copy_up, src, new);
}
EXPORT_SYMBOL(security_inode_copy_up);

/**
 * security_inode_copy_up_xattr() - Filter xattrs in an overlayfs copy-up op
 * @src: union dentry of copy-up file
 * @name: xattr name
 *
 * Filter the xattrs being copied up when a unioned file is copied up from a
 * lower layer to the union/overlay layer.   The caller is responsible for
 * reading and writing the xattrs, this hook is merely a filter.
 *
 * Return: Returns 0 to accept the xattr, -ECANCELED to discard the xattr,
 *         -EOPNOTSUPP if the security module does not know about attribute,
 *         or a negative error code to abort the copy up.
 */
int security_inode_copy_up_xattr(struct dentry *src, const char *name)
{
	int rc;

	rc = call_int_hook(inode_copy_up_xattr, src, name);
	if (rc != LSM_RET_DEFAULT(inode_copy_up_xattr))
		return rc;

	return LSM_RET_DEFAULT(inode_copy_up_xattr);
}
EXPORT_SYMBOL(security_inode_copy_up_xattr);

/**
 * security_inode_setintegrity() - Set the inode's integrity data
 * @inode: inode
 * @type: type of integrity, e.g. hash digest, signature, etc
 * @value: the integrity value
 * @size: size of the integrity value
 *
 * Register a verified integrity measurement of a inode with LSMs.
 * LSMs should free the previously saved data if @value is NULL.
 *
 * Return: Returns 0 on success, negative values on failure.
 */
int security_inode_setintegrity(const struct inode *inode,
				enum lsm_integrity_type type, const void *value,
				size_t size)
{
	return call_int_hook(inode_setintegrity, inode, type, value, size);
}
EXPORT_SYMBOL(security_inode_setintegrity);

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
	return call_int_hook(kernfs_init_security, kn_dir, kn);
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
	return call_int_hook(file_permission, file, mask);
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
	rc = call_int_hook(file_alloc_security, file);
	if (unlikely(rc))
		security_file_free(file);
	return rc;
}

/**
 * security_file_release() - Perform actions before releasing the file ref
 * @file: the file
 *
 * Perform actions before releasing the last reference to a file.
 */
void security_file_release(struct file *file)
{
	call_void_hook(file_release, file);
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
	return call_int_hook(file_ioctl, file, cmd, arg);
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
	return call_int_hook(file_ioctl_compat, file, cmd, arg);
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
	return call_int_hook(mmap_file, file, prot, mmap_prot(file, prot),
			     flags);
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
	return call_int_hook(mmap_addr, addr);
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
	return call_int_hook(file_mprotect, vma, reqprot, prot);
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
	return call_int_hook(file_lock, file, cmd);
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
	return call_int_hook(file_fcntl, file, cmd, arg);
}

/**
 * security_file_set_fowner() - Set the file owner info in the LSM blob
 * @file: the file
 *
 * Save owner security information (typically from current->security) in
 * file->f_security for later use by the send_sigiotask hook.
 *
 * This hook is called with file->f_owner.lock held.
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
	return call_int_hook(file_send_sigiotask, tsk, fown, sig);
}

/**
 * security_file_receive() - Check if receiving a file via IPC is allowed
 * @file: file being received
 *
 * This hook allows security modules to control the ability of a process to
 * receive an open file descriptor via socket IPC.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_file_receive(struct file *file)
{
	return call_int_hook(file_receive, file);
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

	ret = call_int_hook(file_open, file);
	if (ret)
		return ret;

	return fsnotify_open_perm(file);
}

/**
 * security_file_post_open() - Evaluate a file after it has been opened
 * @file: the file
 * @mask: access mask
 *
 * Evaluate an opened file and the access mask requested with open(). The hook
 * is useful for LSMs that require the file content to be available in order to
 * make decisions.
 *
 * Return: Returns 0 if permission is granted.
 */
int security_file_post_open(struct file *file, int mask)
{
	return call_int_hook(file_post_open, file, mask);
}
EXPORT_SYMBOL_GPL(security_file_post_open);

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
	return call_int_hook(file_truncate, file);
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
	rc = call_int_hook(task_alloc, task, clone_flags);
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

	rc = call_int_hook(cred_alloc_blank, cred, gfp);
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

	rc = call_int_hook(cred_prepare, new, old, gfp);
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
	return call_int_hook(kernel_act_as, new, secid);
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
	return call_int_hook(kernel_create_files_as, new, inode);
}

/**
 * security_kernel_module_request() - Check if loading a module is allowed
 * @kmod_name: module name
 *
 * Ability to trigger the kernel to automatically upcall to userspace for
 * userspace to load a kernel module with the given name.
 *
 * Return: Returns 0 if successful.
 */
int security_kernel_module_request(char *kmod_name)
{
	return call_int_hook(kernel_module_request, kmod_name);
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
	return call_int_hook(kernel_read_file, file, id, contents);
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
	return call_int_hook(kernel_post_read_file, file, buf, size, id);
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
	return call_int_hook(kernel_load_data, id, contents);
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
	return call_int_hook(kernel_post_load_data, buf, size, id, description);
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
	return call_int_hook(task_fix_setuid, new, old, flags);
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
	return call_int_hook(task_fix_setgid, new, old, flags);
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
	return call_int_hook(task_fix_setgroups, new, old);
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
	return call_int_hook(task_setpgid, p, pgid);
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
	return call_int_hook(task_getpgid, p);
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
	return call_int_hook(task_getsid, p);
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
	return call_int_hook(task_setnice, p, nice);
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
	return call_int_hook(task_setioprio, p, ioprio);
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
	return call_int_hook(task_getioprio, p);
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
	return call_int_hook(task_prlimit, cred, tcred, flags);
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
	return call_int_hook(task_setrlimit, p, resource, new_rlim);
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
	return call_int_hook(task_setscheduler, p);
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
	return call_int_hook(task_getscheduler, p);
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
	return call_int_hook(task_movememory, p);
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
	return call_int_hook(task_kill, p, info, sig, cred);
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
	struct lsm_static_call *scall;

	lsm_for_each_hook(scall, task_prctl) {
		thisrc = scall->hl->hook.task_prctl(option, arg2, arg3, arg4, arg5);
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
	return call_int_hook(userns_create, cred);
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
	return call_int_hook(ipc_permission, ipcp, flag);
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
	rc = call_int_hook(msg_msg_alloc_security, msg);
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
	rc = call_int_hook(msg_queue_alloc_security, msq);
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
	return call_int_hook(msg_queue_associate, msq, msqflg);
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
	return call_int_hook(msg_queue_msgctl, msq, cmd);
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
	return call_int_hook(msg_queue_msgsnd, msq, msg, msqflg);
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
	return call_int_hook(msg_queue_msgrcv, msq, msg, target, type, mode);
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
	rc = call_int_hook(shm_alloc_security, shp);
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
	return call_int_hook(shm_associate, shp, shmflg);
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
	return call_int_hook(shm_shmctl, shp, cmd);
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
	return call_int_hook(shm_shmat, shp, shmaddr, shmflg);
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
	rc = call_int_hook(sem_alloc_security, sma);
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
	return call_int_hook(sem_associate, sma, semflg);
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
	return call_int_hook(sem_semctl, sma, cmd);
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
	return call_int_hook(sem_semop, sma, sops, nsops, alter);
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

/*
 * Please keep this in sync with it's counterpart in security/lsm_syscalls.c
 */

/**
 * security_getselfattr - Read an LSM attribute of the current process.
 * @attr: which attribute to return
 * @uctx: the user-space destination for the information, or NULL
 * @size: pointer to the size of space available to receive the data
 * @flags: special handling options. LSM_FLAG_SINGLE indicates that only
 * attributes associated with the LSM identified in the passed @ctx be
 * reported.
 *
 * A NULL value for @uctx can be used to get both the number of attributes
 * and the size of the data.
 *
 * Returns the number of attributes found on success, negative value
 * on error. @size is reset to the total size of the data.
 * If @size is insufficient to contain the data -E2BIG is returned.
 */
int security_getselfattr(unsigned int attr, struct lsm_ctx __user *uctx,
			 u32 __user *size, u32 flags)
{
	struct lsm_static_call *scall;
	struct lsm_ctx lctx = { .id = LSM_ID_UNDEF, };
	u8 __user *base = (u8 __user *)uctx;
	u32 entrysize;
	u32 total = 0;
	u32 left;
	bool toobig = false;
	bool single = false;
	int count = 0;
	int rc;

	if (attr == LSM_ATTR_UNDEF)
		return -EINVAL;
	if (size == NULL)
		return -EINVAL;
	if (get_user(left, size))
		return -EFAULT;

	if (flags) {
		/*
		 * Only flag supported is LSM_FLAG_SINGLE
		 */
		if (flags != LSM_FLAG_SINGLE || !uctx)
			return -EINVAL;
		if (copy_from_user(&lctx, uctx, sizeof(lctx)))
			return -EFAULT;
		/*
		 * If the LSM ID isn't specified it is an error.
		 */
		if (lctx.id == LSM_ID_UNDEF)
			return -EINVAL;
		single = true;
	}

	/*
	 * In the usual case gather all the data from the LSMs.
	 * In the single case only get the data from the LSM specified.
	 */
	lsm_for_each_hook(scall, getselfattr) {
		if (single && lctx.id != scall->hl->lsmid->id)
			continue;
		entrysize = left;
		if (base)
			uctx = (struct lsm_ctx __user *)(base + total);
		rc = scall->hl->hook.getselfattr(attr, uctx, &entrysize, flags);
		if (rc == -EOPNOTSUPP) {
			rc = 0;
			continue;
		}
		if (rc == -E2BIG) {
			rc = 0;
			left = 0;
			toobig = true;
		} else if (rc < 0)
			return rc;
		else
			left -= entrysize;

		total += entrysize;
		count += rc;
		if (single)
			break;
	}
	if (put_user(total, size))
		return -EFAULT;
	if (toobig)
		return -E2BIG;
	if (count == 0)
		return LSM_RET_DEFAULT(getselfattr);
	return count;
}

/*
 * Please keep this in sync with it's counterpart in security/lsm_syscalls.c
 */

/**
 * security_setselfattr - Set an LSM attribute on the current process.
 * @attr: which attribute to set
 * @uctx: the user-space source for the information
 * @size: the size of the data
 * @flags: reserved for future use, must be 0
 *
 * Set an LSM attribute for the current process. The LSM, attribute
 * and new value are included in @uctx.
 *
 * Returns 0 on success, -EINVAL if the input is inconsistent, -EFAULT
 * if the user buffer is inaccessible, E2BIG if size is too big, or an
 * LSM specific failure.
 */
int security_setselfattr(unsigned int attr, struct lsm_ctx __user *uctx,
			 u32 size, u32 flags)
{
	struct lsm_static_call *scall;
	struct lsm_ctx *lctx;
	int rc = LSM_RET_DEFAULT(setselfattr);
	u64 required_len;

	if (flags)
		return -EINVAL;
	if (size < sizeof(*lctx))
		return -EINVAL;
	if (size > PAGE_SIZE)
		return -E2BIG;

	lctx = memdup_user(uctx, size);
	if (IS_ERR(lctx))
		return PTR_ERR(lctx);

	if (size < lctx->len ||
	    check_add_overflow(sizeof(*lctx), lctx->ctx_len, &required_len) ||
	    lctx->len < required_len) {
		rc = -EINVAL;
		goto free_out;
	}

	lsm_for_each_hook(scall, setselfattr)
		if ((scall->hl->lsmid->id) == lctx->id) {
			rc = scall->hl->hook.setselfattr(attr, lctx, size, flags);
			break;
		}

free_out:
	kfree(lctx);
	return rc;
}

/**
 * security_getprocattr() - Read an attribute for a task
 * @p: the task
 * @lsmid: LSM identification
 * @name: attribute name
 * @value: attribute value
 *
 * Read attribute @name for task @p and store it into @value if allowed.
 *
 * Return: Returns the length of @value on success, a negative value otherwise.
 */
int security_getprocattr(struct task_struct *p, int lsmid, const char *name,
			 char **value)
{
	struct lsm_static_call *scall;

	lsm_for_each_hook(scall, getprocattr) {
		if (lsmid != 0 && lsmid != scall->hl->lsmid->id)
			continue;
		return scall->hl->hook.getprocattr(p, name, value);
	}
	return LSM_RET_DEFAULT(getprocattr);
}

/**
 * security_setprocattr() - Set an attribute for a task
 * @lsmid: LSM identification
 * @name: attribute name
 * @value: attribute value
 * @size: attribute value size
 *
 * Write (set) the current task's attribute @name to @value, size @size if
 * allowed.
 *
 * Return: Returns bytes written on success, a negative value otherwise.
 */
int security_setprocattr(int lsmid, const char *name, void *value, size_t size)
{
	struct lsm_static_call *scall;

	lsm_for_each_hook(scall, setprocattr) {
		if (lsmid != 0 && lsmid != scall->hl->lsmid->id)
			continue;
		return scall->hl->hook.setprocattr(name, value, size);
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
	return call_int_hook(netlink_send, sk, skb);
}

/**
 * security_ismaclabel() - Check if the named attribute is a MAC label
 * @name: full extended attribute name
 *
 * Check if the extended attribute specified by @name represents a MAC label.
 *
 * Return: Returns 1 if name is a MAC attribute otherwise returns 0.
 */
int security_ismaclabel(const char *name)
{
	return call_int_hook(ismaclabel, name);
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
	return call_int_hook(secid_to_secctx, secid, secdata, seclen);
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
	return call_int_hook(secctx_to_secid, secdata, seclen, secid);
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
 * security_inode_notifysecctx() - Notify the LSM of an inode's security label
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
	return call_int_hook(inode_notifysecctx, inode, ctx, ctxlen);
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
	return call_int_hook(inode_setsecctx, dentry, ctx, ctxlen);
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
	return call_int_hook(inode_getsecctx, inode, ctx, ctxlen);
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
	return call_int_hook(post_notification, w_cred, cred, n);
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
	return call_int_hook(watch_key, key);
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
	return call_int_hook(unix_stream_connect, sock, other, newsk);
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
	return call_int_hook(unix_may_send, sock, other);
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
	return call_int_hook(socket_create, family, type, protocol, kern);
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
	return call_int_hook(socket_post_create, sock, family, type,
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
	return call_int_hook(socket_socketpair, socka, sockb);
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
	return call_int_hook(socket_bind, sock, address, addrlen);
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
	return call_int_hook(socket_connect, sock, address, addrlen);
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
	return call_int_hook(socket_listen, sock, backlog);
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
	return call_int_hook(socket_accept, sock, newsock);
}

/**
 * security_socket_sendmsg() - Check if sending a message is allowed
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
	return call_int_hook(socket_sendmsg, sock, msg, size);
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
	return call_int_hook(socket_recvmsg, sock, msg, size, flags);
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
	return call_int_hook(socket_getsockname, sock);
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
	return call_int_hook(socket_getpeername, sock);
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
	return call_int_hook(socket_getsockopt, sock, level, optname);
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
	return call_int_hook(socket_setsockopt, sock, level, optname);
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
	return call_int_hook(socket_shutdown, sock, how);
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
	return call_int_hook(socket_sock_rcv_skb, sk, skb);
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
	return call_int_hook(socket_getpeersec_stream, sock, optval, optlen,
			     len);
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
	return call_int_hook(socket_getpeersec_dgram, sock, skb, secid);
}
EXPORT_SYMBOL(security_socket_getpeersec_dgram);

/**
 * lsm_sock_alloc - allocate a composite sock blob
 * @sock: the sock that needs a blob
 * @gfp: allocation mode
 *
 * Allocate the sock blob for all the modules
 *
 * Returns 0, or -ENOMEM if memory can't be allocated.
 */
static int lsm_sock_alloc(struct sock *sock, gfp_t gfp)
{
	return lsm_blob_alloc(&sock->sk_security, blob_sizes.lbs_sock, gfp);
}

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
	int rc = lsm_sock_alloc(sk, priority);

	if (unlikely(rc))
		return rc;
	rc = call_int_hook(sk_alloc_security, sk, family, priority);
	if (unlikely(rc))
		security_sk_free(sk);
	return rc;
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
	kfree(sk->sk_security);
	sk->sk_security = NULL;
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
	return call_int_hook(inet_conn_request, sk, skb, req);
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
	return call_int_hook(secmark_relabel_packet, secid);
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
	int rc;

	rc = lsm_blob_alloc(security, blob_sizes.lbs_tun_dev, GFP_KERNEL);
	if (rc)
		return rc;

	rc = call_int_hook(tun_dev_alloc_security, *security);
	if (rc) {
		kfree(*security);
		*security = NULL;
	}
	return rc;
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
	kfree(security);
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
	return call_int_hook(tun_dev_create);
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
	return call_int_hook(tun_dev_attach_queue, security);
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
	return call_int_hook(tun_dev_attach, sk, security);
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
	return call_int_hook(tun_dev_open, security);
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
	return call_int_hook(sctp_assoc_request, asoc, skb);
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
	return call_int_hook(sctp_bind_connect, sk, optname, address, addrlen);
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
	return call_int_hook(sctp_assoc_established, asoc, skb);
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
	return call_int_hook(mptcp_add_subflow, sk, ssk);
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
	return call_int_hook(ib_pkey_access, sec, subnet_prefix, pkey);
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
	return call_int_hook(ib_endport_manage_subnet, sec, dev_name, port_num);
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
	int rc;

	rc = lsm_blob_alloc(sec, blob_sizes.lbs_ib, GFP_KERNEL);
	if (rc)
		return rc;

	rc = call_int_hook(ib_alloc_security, *sec);
	if (rc) {
		kfree(*sec);
		*sec = NULL;
	}
	return rc;
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
	kfree(sec);
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
	return call_int_hook(xfrm_policy_alloc_security, ctxp, sec_ctx, gfp);
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
	return call_int_hook(xfrm_policy_clone_security, old_ctx, new_ctxp);
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
	return call_int_hook(xfrm_policy_delete_security, ctx);
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
	return call_int_hook(xfrm_state_alloc, x, sec_ctx);
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
	return call_int_hook(xfrm_state_alloc_acquire, x, polsec, secid);
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
	return call_int_hook(xfrm_state_delete_security, x);
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
	return call_int_hook(xfrm_policy_lookup, ctx, fl_secid);
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
	struct lsm_static_call *scall;
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
	lsm_for_each_hook(scall, xfrm_state_pol_flow_match) {
		rc = scall->hl->hook.xfrm_state_pol_flow_match(x, xp, flic);
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
	return call_int_hook(xfrm_decode_session, skb, secid, 1);
}

void security_skb_classify_flow(struct sk_buff *skb, struct flowi_common *flic)
{
	int rc = call_int_hook(xfrm_decode_session, skb, &flic->flowic_secid,
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
	int rc = lsm_key_alloc(key);

	if (unlikely(rc))
		return rc;
	rc = call_int_hook(key_alloc, key, cred, flags);
	if (unlikely(rc))
		security_key_free(key);
	return rc;
}

/**
 * security_key_free() - Free a kernel key LSM blob
 * @key: key
 *
 * Notification of destruction; free security data.
 */
void security_key_free(struct key *key)
{
	kfree(key->security);
	key->security = NULL;
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
	return call_int_hook(key_permission, key_ref, cred, need_perm);
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
	return call_int_hook(key_getsecurity, key, buffer);
}

/**
 * security_key_post_create_or_update() - Notification of key create or update
 * @keyring: keyring to which the key is linked to
 * @key: created or updated key
 * @payload: data used to instantiate or update the key
 * @payload_len: length of payload
 * @flags: key flags
 * @create: flag indicating whether the key was created or updated
 *
 * Notify the caller of a key creation or update.
 */
void security_key_post_create_or_update(struct key *keyring, struct key *key,
					const void *payload, size_t payload_len,
					unsigned long flags, bool create)
{
	call_void_hook(key_post_create_or_update, keyring, key, payload,
		       payload_len, flags, create);
}
#endif	/* CONFIG_KEYS */

#ifdef CONFIG_AUDIT
/**
 * security_audit_rule_init() - Allocate and init an LSM audit rule struct
 * @field: audit action
 * @op: rule operator
 * @rulestr: rule context
 * @lsmrule: receive buffer for audit rule struct
 * @gfp: GFP flag used for kmalloc
 *
 * Allocate and initialize an LSM audit rule structure.
 *
 * Return: Return 0 if @lsmrule has been successfully set, -EINVAL in case of
 *         an invalid rule.
 */
int security_audit_rule_init(u32 field, u32 op, char *rulestr, void **lsmrule,
			     gfp_t gfp)
{
	return call_int_hook(audit_rule_init, field, op, rulestr, lsmrule, gfp);
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
	return call_int_hook(audit_rule_known, krule);
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
	return call_int_hook(audit_rule_match, secid, field, op, lsmrule);
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
	return call_int_hook(bpf, cmd, attr, size);
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
	return call_int_hook(bpf_map, map, fmode);
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
	return call_int_hook(bpf_prog, prog);
}

/**
 * security_bpf_map_create() - Check if BPF map creation is allowed
 * @map: BPF map object
 * @attr: BPF syscall attributes used to create BPF map
 * @token: BPF token used to grant user access
 *
 * Do a check when the kernel creates a new BPF map. This is also the
 * point where LSM blob is allocated for LSMs that need them.
 *
 * Return: Returns 0 on success, error on failure.
 */
int security_bpf_map_create(struct bpf_map *map, union bpf_attr *attr,
			    struct bpf_token *token)
{
	return call_int_hook(bpf_map_create, map, attr, token);
}

/**
 * security_bpf_prog_load() - Check if loading of BPF program is allowed
 * @prog: BPF program object
 * @attr: BPF syscall attributes used to create BPF program
 * @token: BPF token used to grant user access to BPF subsystem
 *
 * Perform an access control check when the kernel loads a BPF program and
 * allocates associated BPF program object. This hook is also responsible for
 * allocating any required LSM state for the BPF program.
 *
 * Return: Returns 0 on success, error on failure.
 */
int security_bpf_prog_load(struct bpf_prog *prog, union bpf_attr *attr,
			   struct bpf_token *token)
{
	return call_int_hook(bpf_prog_load, prog, attr, token);
}

/**
 * security_bpf_token_create() - Check if creating of BPF token is allowed
 * @token: BPF token object
 * @attr: BPF syscall attributes used to create BPF token
 * @path: path pointing to BPF FS mount point from which BPF token is created
 *
 * Do a check when the kernel instantiates a new BPF token object from BPF FS
 * instance. This is also the point where LSM blob can be allocated for LSMs.
 *
 * Return: Returns 0 on success, error on failure.
 */
int security_bpf_token_create(struct bpf_token *token, union bpf_attr *attr,
			      const struct path *path)
{
	return call_int_hook(bpf_token_create, token, attr, path);
}

/**
 * security_bpf_token_cmd() - Check if BPF token is allowed to delegate
 * requested BPF syscall command
 * @token: BPF token object
 * @cmd: BPF syscall command requested to be delegated by BPF token
 *
 * Do a check when the kernel decides whether provided BPF token should allow
 * delegation of requested BPF syscall command.
 *
 * Return: Returns 0 on success, error on failure.
 */
int security_bpf_token_cmd(const struct bpf_token *token, enum bpf_cmd cmd)
{
	return call_int_hook(bpf_token_cmd, token, cmd);
}

/**
 * security_bpf_token_capable() - Check if BPF token is allowed to delegate
 * requested BPF-related capability
 * @token: BPF token object
 * @cap: capabilities requested to be delegated by BPF token
 *
 * Do a check when the kernel decides whether provided BPF token should allow
 * delegation of requested BPF-related capabilities.
 *
 * Return: Returns 0 on success, error on failure.
 */
int security_bpf_token_capable(const struct bpf_token *token, int cap)
{
	return call_int_hook(bpf_token_capable, token, cap);
}

/**
 * security_bpf_map_free() - Free a bpf map's LSM blob
 * @map: bpf map
 *
 * Clean up the security information stored inside bpf map.
 */
void security_bpf_map_free(struct bpf_map *map)
{
	call_void_hook(bpf_map_free, map);
}

/**
 * security_bpf_prog_free() - Free a BPF program's LSM blob
 * @prog: BPF program struct
 *
 * Clean up the security information stored inside BPF program.
 */
void security_bpf_prog_free(struct bpf_prog *prog)
{
	call_void_hook(bpf_prog_free, prog);
}

/**
 * security_bpf_token_free() - Free a BPF token's LSM blob
 * @token: BPF token struct
 *
 * Clean up the security information stored inside BPF token.
 */
void security_bpf_token_free(struct bpf_token *token)
{
	call_void_hook(bpf_token_free, token);
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
	return call_int_hook(locked_down, what);
}
EXPORT_SYMBOL(security_locked_down);

/**
 * security_bdev_alloc() - Allocate a block device LSM blob
 * @bdev: block device
 *
 * Allocate and attach a security structure to @bdev->bd_security.  The
 * security field is initialized to NULL when the bdev structure is
 * allocated.
 *
 * Return: Return 0 if operation was successful.
 */
int security_bdev_alloc(struct block_device *bdev)
{
	int rc = 0;

	rc = lsm_bdev_alloc(bdev);
	if (unlikely(rc))
		return rc;

	rc = call_int_hook(bdev_alloc_security, bdev);
	if (unlikely(rc))
		security_bdev_free(bdev);

	return rc;
}
EXPORT_SYMBOL(security_bdev_alloc);

/**
 * security_bdev_free() - Free a block device's LSM blob
 * @bdev: block device
 *
 * Deallocate the bdev security structure and set @bdev->bd_security to NULL.
 */
void security_bdev_free(struct block_device *bdev)
{
	if (!bdev->bd_security)
		return;

	call_void_hook(bdev_free_security, bdev);

	kfree(bdev->bd_security);
	bdev->bd_security = NULL;
}
EXPORT_SYMBOL(security_bdev_free);

/**
 * security_bdev_setintegrity() - Set the device's integrity data
 * @bdev: block device
 * @type: type of integrity, e.g. hash digest, signature, etc
 * @value: the integrity value
 * @size: size of the integrity value
 *
 * Register a verified integrity measurement of a bdev with LSMs.
 * LSMs should free the previously saved data if @value is NULL.
 * Please note that the new hook should be invoked every time the security
 * information is updated to keep these data current. For example, in dm-verity,
 * if the mapping table is reloaded and configured to use a different dm-verity
 * target with a new roothash and signing information, the previously stored
 * data in the LSM blob will become obsolete. It is crucial to re-invoke the
 * hook to refresh these data and ensure they are up to date. This necessity
 * arises from the design of device-mapper, where a device-mapper device is
 * first created, and then targets are subsequently loaded into it. These
 * targets can be modified multiple times during the device's lifetime.
 * Therefore, while the LSM blob is allocated during the creation of the block
 * device, its actual contents are not initialized at this stage and can change
 * substantially over time. This includes alterations from data that the LSMs
 * 'trusts' to those they do not, making it essential to handle these changes
 * correctly. Failure to address this dynamic aspect could potentially allow
 * for bypassing LSM checks.
 *
 * Return: Returns 0 on success, negative values on failure.
 */
int security_bdev_setintegrity(struct block_device *bdev,
			       enum lsm_integrity_type type, const void *value,
			       size_t size)
{
	return call_int_hook(bdev_setintegrity, bdev, type, value, size);
}
EXPORT_SYMBOL(security_bdev_setintegrity);

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
	return call_int_hook(perf_event_open, attr, type);
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
	int rc;

	rc = lsm_blob_alloc(&event->security, blob_sizes.lbs_perf_event,
			    GFP_KERNEL);
	if (rc)
		return rc;

	rc = call_int_hook(perf_event_alloc, event);
	if (rc) {
		kfree(event->security);
		event->security = NULL;
	}
	return rc;
}

/**
 * security_perf_event_free() - Free a perf event LSM blob
 * @event: perf event
 *
 * Release (free) perf_event security info.
 */
void security_perf_event_free(struct perf_event *event)
{
	kfree(event->security);
	event->security = NULL;
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
	return call_int_hook(perf_event_read, event);
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
	return call_int_hook(perf_event_write, event);
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
	return call_int_hook(uring_override_creds, new);
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
	return call_int_hook(uring_sqpoll);
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
	return call_int_hook(uring_cmd, ioucmd);
}
#endif /* CONFIG_IO_URING */

/**
 * security_initramfs_populated() - Notify LSMs that initramfs has been loaded
 *
 * Tells the LSMs the initramfs has been unpacked into the rootfs.
 */
void security_initramfs_populated(void)
{
	call_void_hook(initramfs_populated);
}
