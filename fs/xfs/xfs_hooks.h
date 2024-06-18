// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2022-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef XFS_HOOKS_H_
#define XFS_HOOKS_H_

#ifdef CONFIG_XFS_LIVE_HOOKS
struct xfs_hooks {
	struct blocking_notifier_head	head;
};

/*
 * If jump labels are enabled in Kconfig, the static key uses nop sleds and
 * code patching to eliminate the overhead of taking the rwsem in
 * blocking_notifier_call_chain when there are no hooks configured.  If not,
 * the static key per-call overhead is an atomic read.  Most arches that can
 * handle XFS also support jump labels.
 *
 * Note: Patching the kernel code requires taking the cpu hotplug lock.  Other
 * parts of the kernel allocate memory with that lock held, which means that
 * XFS callers cannot hold any locks that might be used by memory reclaim or
 * writeback when calling the static_branch_{inc,dec} functions.
 */
# define DEFINE_STATIC_XFS_HOOK_SWITCH(name) \
	static DEFINE_STATIC_KEY_FALSE(name)
# define xfs_hooks_switch_on(name)	static_branch_inc(name)
# define xfs_hooks_switch_off(name)	static_branch_dec(name)
# define xfs_hooks_switched_on(name)	static_branch_unlikely(name)

struct xfs_hook {
	/* This must come at the start of the structure. */
	struct notifier_block		nb;
};

typedef	int (*xfs_hook_fn_t)(struct xfs_hook *hook, unsigned long action,
		void *data);

void xfs_hooks_init(struct xfs_hooks *chain);
int xfs_hooks_add(struct xfs_hooks *chain, struct xfs_hook *hook);
void xfs_hooks_del(struct xfs_hooks *chain, struct xfs_hook *hook);
int xfs_hooks_call(struct xfs_hooks *chain, unsigned long action,
		void *priv);

static inline void xfs_hook_setup(struct xfs_hook *hook, notifier_fn_t fn)
{
	hook->nb.notifier_call = fn;
	hook->nb.priority = 0;
}

#else

struct xfs_hooks { /* empty */ };

# define DEFINE_STATIC_XFS_HOOK_SWITCH(name)
# define xfs_hooks_switch_on(name)		((void)0)
# define xfs_hooks_switch_off(name)		((void)0)
# define xfs_hooks_switched_on(name)		(false)

# define xfs_hooks_init(chain)			((void)0)
# define xfs_hooks_call(chain, val, priv)	(NOTIFY_DONE)
#endif

#endif /* XFS_HOOKS_H_ */
