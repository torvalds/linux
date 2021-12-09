// SPDX-License-Identifier: GPL-2.0-or-later
/* Task credentials management - see Documentation/security/credentials.rst
 *
 * Copyright (C) 2008 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */
#include <linux/export.h>
#include <linux/cred.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/sched/coredump.h>
#include <linux/key.h>
#include <linux/keyctl.h>
#include <linux/init_task.h>
#include <linux/security.h>
#include <linux/binfmts.h>
#include <linux/cn_proc.h>
#include <linux/uidgid.h>

#if 0
#define kdebug(FMT, ...)						\
	printk("[%-5.5s%5u] " FMT "\n",					\
	       current->comm, current->pid, ##__VA_ARGS__)
#else
#define kdebug(FMT, ...)						\
do {									\
	if (0)								\
		no_printk("[%-5.5s%5u] " FMT "\n",			\
			  current->comm, current->pid, ##__VA_ARGS__);	\
} while (0)
#endif

static struct kmem_cache *cred_jar;

/* init to 2 - one for init_task, one to ensure it is never freed */
static struct group_info init_groups = { .usage = ATOMIC_INIT(2) };

/*
 * The initial credentials for the initial task
 */
struct cred init_cred = {
	.usage			= ATOMIC_INIT(4),
#ifdef CONFIG_DEBUG_CREDENTIALS
	.subscribers		= ATOMIC_INIT(2),
	.magic			= CRED_MAGIC,
#endif
	.uid			= GLOBAL_ROOT_UID,
	.gid			= GLOBAL_ROOT_GID,
	.suid			= GLOBAL_ROOT_UID,
	.sgid			= GLOBAL_ROOT_GID,
	.euid			= GLOBAL_ROOT_UID,
	.egid			= GLOBAL_ROOT_GID,
	.fsuid			= GLOBAL_ROOT_UID,
	.fsgid			= GLOBAL_ROOT_GID,
	.securebits		= SECUREBITS_DEFAULT,
	.cap_inheritable	= CAP_EMPTY_SET,
	.cap_permitted		= CAP_FULL_SET,
	.cap_effective		= CAP_FULL_SET,
	.cap_bset		= CAP_FULL_SET,
	.user			= INIT_USER,
	.user_ns		= &init_user_ns,
	.group_info		= &init_groups,
	.ucounts		= &init_ucounts,
};

static inline void set_cred_subscribers(struct cred *cred, int n)
{
#ifdef CONFIG_DEBUG_CREDENTIALS
	atomic_set(&cred->subscribers, n);
#endif
}

static inline int read_cred_subscribers(const struct cred *cred)
{
#ifdef CONFIG_DEBUG_CREDENTIALS
	return atomic_read(&cred->subscribers);
#else
	return 0;
#endif
}

static inline void alter_cred_subscribers(const struct cred *_cred, int n)
{
#ifdef CONFIG_DEBUG_CREDENTIALS
	struct cred *cred = (struct cred *) _cred;

	atomic_add(n, &cred->subscribers);
#endif
}

/*
 * The RCU callback to actually dispose of a set of credentials
 */
static void put_cred_rcu(struct rcu_head *rcu)
{
	struct cred *cred = container_of(rcu, struct cred, rcu);

	kdebug("put_cred_rcu(%p)", cred);

#ifdef CONFIG_DEBUG_CREDENTIALS
	if (cred->magic != CRED_MAGIC_DEAD ||
	    atomic_read(&cred->usage) != 0 ||
	    read_cred_subscribers(cred) != 0)
		panic("CRED: put_cred_rcu() sees %p with"
		      " mag %x, put %p, usage %d, subscr %d\n",
		      cred, cred->magic, cred->put_addr,
		      atomic_read(&cred->usage),
		      read_cred_subscribers(cred));
#else
	if (atomic_read(&cred->usage) != 0)
		panic("CRED: put_cred_rcu() sees %p with usage %d\n",
		      cred, atomic_read(&cred->usage));
#endif

	security_cred_free(cred);
	key_put(cred->session_keyring);
	key_put(cred->process_keyring);
	key_put(cred->thread_keyring);
	key_put(cred->request_key_auth);
	if (cred->group_info)
		put_group_info(cred->group_info);
	free_uid(cred->user);
	if (cred->ucounts)
		put_ucounts(cred->ucounts);
	put_user_ns(cred->user_ns);
	kmem_cache_free(cred_jar, cred);
}

/**
 * __put_cred - Destroy a set of credentials
 * @cred: The record to release
 *
 * Destroy a set of credentials on which no references remain.
 */
void __put_cred(struct cred *cred)
{
	kdebug("__put_cred(%p{%d,%d})", cred,
	       atomic_read(&cred->usage),
	       read_cred_subscribers(cred));

	BUG_ON(atomic_read(&cred->usage) != 0);
#ifdef CONFIG_DEBUG_CREDENTIALS
	BUG_ON(read_cred_subscribers(cred) != 0);
	cred->magic = CRED_MAGIC_DEAD;
	cred->put_addr = __builtin_return_address(0);
#endif
	BUG_ON(cred == current->cred);
	BUG_ON(cred == current->real_cred);

	if (cred->non_rcu)
		put_cred_rcu(&cred->rcu);
	else
		call_rcu(&cred->rcu, put_cred_rcu);
}
EXPORT_SYMBOL(__put_cred);

/*
 * Clean up a task's credentials when it exits
 */
void exit_creds(struct task_struct *tsk)
{
	struct cred *cred;

	kdebug("exit_creds(%u,%p,%p,{%d,%d})", tsk->pid, tsk->real_cred, tsk->cred,
	       atomic_read(&tsk->cred->usage),
	       read_cred_subscribers(tsk->cred));

	cred = (struct cred *) tsk->real_cred;
	tsk->real_cred = NULL;
	validate_creds(cred);
	alter_cred_subscribers(cred, -1);
	put_cred(cred);

	cred = (struct cred *) tsk->cred;
	tsk->cred = NULL;
	validate_creds(cred);
	alter_cred_subscribers(cred, -1);
	put_cred(cred);

#ifdef CONFIG_KEYS_REQUEST_CACHE
	key_put(tsk->cached_requested_key);
	tsk->cached_requested_key = NULL;
#endif
}

/**
 * get_task_cred - Get another task's objective credentials
 * @task: The task to query
 *
 * Get the objective credentials of a task, pinning them so that they can't go
 * away.  Accessing a task's credentials directly is not permitted.
 *
 * The caller must also make sure task doesn't get deleted, either by holding a
 * ref on task or by holding tasklist_lock to prevent it from being unlinked.
 */
const struct cred *get_task_cred(struct task_struct *task)
{
	const struct cred *cred;

	rcu_read_lock();

	do {
		cred = __task_cred((task));
		BUG_ON(!cred);
	} while (!get_cred_rcu(cred));

	rcu_read_unlock();
	return cred;
}
EXPORT_SYMBOL(get_task_cred);

/*
 * Allocate blank credentials, such that the credentials can be filled in at a
 * later date without risk of ENOMEM.
 */
struct cred *cred_alloc_blank(void)
{
	struct cred *new;

	new = kmem_cache_zalloc(cred_jar, GFP_KERNEL);
	if (!new)
		return NULL;

	atomic_set(&new->usage, 1);
#ifdef CONFIG_DEBUG_CREDENTIALS
	new->magic = CRED_MAGIC;
#endif
	if (security_cred_alloc_blank(new, GFP_KERNEL_ACCOUNT) < 0)
		goto error;

	return new;

error:
	abort_creds(new);
	return NULL;
}

/**
 * prepare_creds - Prepare a new set of credentials for modification
 *
 * Prepare a new set of task credentials for modification.  A task's creds
 * shouldn't generally be modified directly, therefore this function is used to
 * prepare a new copy, which the caller then modifies and then commits by
 * calling commit_creds().
 *
 * Preparation involves making a copy of the objective creds for modification.
 *
 * Returns a pointer to the new creds-to-be if successful, NULL otherwise.
 *
 * Call commit_creds() or abort_creds() to clean up.
 */
struct cred *prepare_creds(void)
{
	struct task_struct *task = current;
	const struct cred *old;
	struct cred *new;

	validate_process_creds();

	new = kmem_cache_alloc(cred_jar, GFP_KERNEL);
	if (!new)
		return NULL;

	kdebug("prepare_creds() alloc %p", new);

	old = task->cred;
	memcpy(new, old, sizeof(struct cred));

	new->non_rcu = 0;
	atomic_set(&new->usage, 1);
	set_cred_subscribers(new, 0);
	get_group_info(new->group_info);
	get_uid(new->user);
	get_user_ns(new->user_ns);

#ifdef CONFIG_KEYS
	key_get(new->session_keyring);
	key_get(new->process_keyring);
	key_get(new->thread_keyring);
	key_get(new->request_key_auth);
#endif

#ifdef CONFIG_SECURITY
	new->security = NULL;
#endif

	new->ucounts = get_ucounts(new->ucounts);
	if (!new->ucounts)
		goto error;

	if (security_prepare_creds(new, old, GFP_KERNEL_ACCOUNT) < 0)
		goto error;

	validate_creds(new);
	return new;

error:
	abort_creds(new);
	return NULL;
}
EXPORT_SYMBOL(prepare_creds);

/*
 * Prepare credentials for current to perform an execve()
 * - The caller must hold ->cred_guard_mutex
 */
struct cred *prepare_exec_creds(void)
{
	struct cred *new;

	new = prepare_creds();
	if (!new)
		return new;

#ifdef CONFIG_KEYS
	/* newly exec'd tasks don't get a thread keyring */
	key_put(new->thread_keyring);
	new->thread_keyring = NULL;

	/* inherit the session keyring; new process keyring */
	key_put(new->process_keyring);
	new->process_keyring = NULL;
#endif

	new->suid = new->fsuid = new->euid;
	new->sgid = new->fsgid = new->egid;

	return new;
}

/*
 * Copy credentials for the new process created by fork()
 *
 * We share if we can, but under some circumstances we have to generate a new
 * set.
 *
 * The new process gets the current process's subjective credentials as its
 * objective and subjective credentials
 */
int copy_creds(struct task_struct *p, unsigned long clone_flags)
{
	struct cred *new;
	int ret;

#ifdef CONFIG_KEYS_REQUEST_CACHE
	p->cached_requested_key = NULL;
#endif

	if (
#ifdef CONFIG_KEYS
		!p->cred->thread_keyring &&
#endif
		clone_flags & CLONE_THREAD
	    ) {
		p->real_cred = get_cred(p->cred);
		get_cred(p->cred);
		alter_cred_subscribers(p->cred, 2);
		kdebug("share_creds(%p{%d,%d})",
		       p->cred, atomic_read(&p->cred->usage),
		       read_cred_subscribers(p->cred));
		inc_rlimit_ucounts(task_ucounts(p), UCOUNT_RLIMIT_NPROC, 1);
		return 0;
	}

	new = prepare_creds();
	if (!new)
		return -ENOMEM;

	if (clone_flags & CLONE_NEWUSER) {
		ret = create_user_ns(new);
		if (ret < 0)
			goto error_put;
		ret = set_cred_ucounts(new);
		if (ret < 0)
			goto error_put;
	}

#ifdef CONFIG_KEYS
	/* new threads get their own thread keyrings if their parent already
	 * had one */
	if (new->thread_keyring) {
		key_put(new->thread_keyring);
		new->thread_keyring = NULL;
		if (clone_flags & CLONE_THREAD)
			install_thread_keyring_to_cred(new);
	}

	/* The process keyring is only shared between the threads in a process;
	 * anything outside of those threads doesn't inherit.
	 */
	if (!(clone_flags & CLONE_THREAD)) {
		key_put(new->process_keyring);
		new->process_keyring = NULL;
	}
#endif

	p->cred = p->real_cred = get_cred(new);
	inc_rlimit_ucounts(task_ucounts(p), UCOUNT_RLIMIT_NPROC, 1);
	alter_cred_subscribers(new, 2);
	validate_creds(new);
	return 0;

error_put:
	put_cred(new);
	return ret;
}

static bool cred_cap_issubset(const struct cred *set, const struct cred *subset)
{
	const struct user_namespace *set_ns = set->user_ns;
	const struct user_namespace *subset_ns = subset->user_ns;

	/* If the two credentials are in the same user namespace see if
	 * the capabilities of subset are a subset of set.
	 */
	if (set_ns == subset_ns)
		return cap_issubset(subset->cap_permitted, set->cap_permitted);

	/* The credentials are in a different user namespaces
	 * therefore one is a subset of the other only if a set is an
	 * ancestor of subset and set->euid is owner of subset or one
	 * of subsets ancestors.
	 */
	for (;subset_ns != &init_user_ns; subset_ns = subset_ns->parent) {
		if ((set_ns == subset_ns->parent)  &&
		    uid_eq(subset_ns->owner, set->euid))
			return true;
	}

	return false;
}

/**
 * commit_creds - Install new credentials upon the current task
 * @new: The credentials to be assigned
 *
 * Install a new set of credentials to the current task, using RCU to replace
 * the old set.  Both the objective and the subjective credentials pointers are
 * updated.  This function may not be called if the subjective credentials are
 * in an overridden state.
 *
 * This function eats the caller's reference to the new credentials.
 *
 * Always returns 0 thus allowing this function to be tail-called at the end
 * of, say, sys_setgid().
 */
int commit_creds(struct cred *new)
{
	struct task_struct *task = current;
	const struct cred *old = task->real_cred;

	kdebug("commit_creds(%p{%d,%d})", new,
	       atomic_read(&new->usage),
	       read_cred_subscribers(new));

	BUG_ON(task->cred != old);
#ifdef CONFIG_DEBUG_CREDENTIALS
	BUG_ON(read_cred_subscribers(old) < 2);
	validate_creds(old);
	validate_creds(new);
#endif
	BUG_ON(atomic_read(&new->usage) < 1);

	get_cred(new); /* we will require a ref for the subj creds too */

	/* dumpability changes */
	if (!uid_eq(old->euid, new->euid) ||
	    !gid_eq(old->egid, new->egid) ||
	    !uid_eq(old->fsuid, new->fsuid) ||
	    !gid_eq(old->fsgid, new->fsgid) ||
	    !cred_cap_issubset(old, new)) {
		if (task->mm)
			set_dumpable(task->mm, suid_dumpable);
		task->pdeath_signal = 0;
		/*
		 * If a task drops privileges and becomes nondumpable,
		 * the dumpability change must become visible before
		 * the credential change; otherwise, a __ptrace_may_access()
		 * racing with this change may be able to attach to a task it
		 * shouldn't be able to attach to (as if the task had dropped
		 * privileges without becoming nondumpable).
		 * Pairs with a read barrier in __ptrace_may_access().
		 */
		smp_wmb();
	}

	/* alter the thread keyring */
	if (!uid_eq(new->fsuid, old->fsuid))
		key_fsuid_changed(new);
	if (!gid_eq(new->fsgid, old->fsgid))
		key_fsgid_changed(new);

	/* do it
	 * RLIMIT_NPROC limits on user->processes have already been checked
	 * in set_user().
	 */
	alter_cred_subscribers(new, 2);
	if (new->user != old->user || new->user_ns != old->user_ns)
		inc_rlimit_ucounts(new->ucounts, UCOUNT_RLIMIT_NPROC, 1);
	rcu_assign_pointer(task->real_cred, new);
	rcu_assign_pointer(task->cred, new);
	if (new->user != old->user || new->user_ns != old->user_ns)
		dec_rlimit_ucounts(old->ucounts, UCOUNT_RLIMIT_NPROC, 1);
	alter_cred_subscribers(old, -2);

	/* send notifications */
	if (!uid_eq(new->uid,   old->uid)  ||
	    !uid_eq(new->euid,  old->euid) ||
	    !uid_eq(new->suid,  old->suid) ||
	    !uid_eq(new->fsuid, old->fsuid))
		proc_id_connector(task, PROC_EVENT_UID);

	if (!gid_eq(new->gid,   old->gid)  ||
	    !gid_eq(new->egid,  old->egid) ||
	    !gid_eq(new->sgid,  old->sgid) ||
	    !gid_eq(new->fsgid, old->fsgid))
		proc_id_connector(task, PROC_EVENT_GID);

	/* release the old obj and subj refs both */
	put_cred(old);
	put_cred(old);
	return 0;
}
EXPORT_SYMBOL(commit_creds);

/**
 * abort_creds - Discard a set of credentials and unlock the current task
 * @new: The credentials that were going to be applied
 *
 * Discard a set of credentials that were under construction and unlock the
 * current task.
 */
void abort_creds(struct cred *new)
{
	kdebug("abort_creds(%p{%d,%d})", new,
	       atomic_read(&new->usage),
	       read_cred_subscribers(new));

#ifdef CONFIG_DEBUG_CREDENTIALS
	BUG_ON(read_cred_subscribers(new) != 0);
#endif
	BUG_ON(atomic_read(&new->usage) < 1);
	put_cred(new);
}
EXPORT_SYMBOL(abort_creds);

/**
 * override_creds - Override the current process's subjective credentials
 * @new: The credentials to be assigned
 *
 * Install a set of temporary override subjective credentials on the current
 * process, returning the old set for later reversion.
 */
const struct cred *override_creds(const struct cred *new)
{
	const struct cred *old = current->cred;

	kdebug("override_creds(%p{%d,%d})", new,
	       atomic_read(&new->usage),
	       read_cred_subscribers(new));

	validate_creds(old);
	validate_creds(new);

	/*
	 * NOTE! This uses 'get_new_cred()' rather than 'get_cred()'.
	 *
	 * That means that we do not clear the 'non_rcu' flag, since
	 * we are only installing the cred into the thread-synchronous
	 * '->cred' pointer, not the '->real_cred' pointer that is
	 * visible to other threads under RCU.
	 *
	 * Also note that we did validate_creds() manually, not depending
	 * on the validation in 'get_cred()'.
	 */
	get_new_cred((struct cred *)new);
	alter_cred_subscribers(new, 1);
	rcu_assign_pointer(current->cred, new);
	alter_cred_subscribers(old, -1);

	kdebug("override_creds() = %p{%d,%d}", old,
	       atomic_read(&old->usage),
	       read_cred_subscribers(old));
	return old;
}
EXPORT_SYMBOL(override_creds);

/**
 * revert_creds - Revert a temporary subjective credentials override
 * @old: The credentials to be restored
 *
 * Revert a temporary set of override subjective credentials to an old set,
 * discarding the override set.
 */
void revert_creds(const struct cred *old)
{
	const struct cred *override = current->cred;

	kdebug("revert_creds(%p{%d,%d})", old,
	       atomic_read(&old->usage),
	       read_cred_subscribers(old));

	validate_creds(old);
	validate_creds(override);
	alter_cred_subscribers(old, 1);
	rcu_assign_pointer(current->cred, old);
	alter_cred_subscribers(override, -1);
	put_cred(override);
}
EXPORT_SYMBOL(revert_creds);

/**
 * cred_fscmp - Compare two credentials with respect to filesystem access.
 * @a: The first credential
 * @b: The second credential
 *
 * cred_cmp() will return zero if both credentials have the same
 * fsuid, fsgid, and supplementary groups.  That is, if they will both
 * provide the same access to files based on mode/uid/gid.
 * If the credentials are different, then either -1 or 1 will
 * be returned depending on whether @a comes before or after @b
 * respectively in an arbitrary, but stable, ordering of credentials.
 *
 * Return: -1, 0, or 1 depending on comparison
 */
int cred_fscmp(const struct cred *a, const struct cred *b)
{
	struct group_info *ga, *gb;
	int g;

	if (a == b)
		return 0;
	if (uid_lt(a->fsuid, b->fsuid))
		return -1;
	if (uid_gt(a->fsuid, b->fsuid))
		return 1;

	if (gid_lt(a->fsgid, b->fsgid))
		return -1;
	if (gid_gt(a->fsgid, b->fsgid))
		return 1;

	ga = a->group_info;
	gb = b->group_info;
	if (ga == gb)
		return 0;
	if (ga == NULL)
		return -1;
	if (gb == NULL)
		return 1;
	if (ga->ngroups < gb->ngroups)
		return -1;
	if (ga->ngroups > gb->ngroups)
		return 1;

	for (g = 0; g < ga->ngroups; g++) {
		if (gid_lt(ga->gid[g], gb->gid[g]))
			return -1;
		if (gid_gt(ga->gid[g], gb->gid[g]))
			return 1;
	}
	return 0;
}
EXPORT_SYMBOL(cred_fscmp);

int set_cred_ucounts(struct cred *new)
{
	struct task_struct *task = current;
	const struct cred *old = task->real_cred;
	struct ucounts *new_ucounts, *old_ucounts = new->ucounts;

	if (new->user == old->user && new->user_ns == old->user_ns)
		return 0;

	/*
	 * This optimization is needed because alloc_ucounts() uses locks
	 * for table lookups.
	 */
	if (old_ucounts && old_ucounts->ns == new->user_ns && uid_eq(old_ucounts->uid, new->euid))
		return 0;

	if (!(new_ucounts = alloc_ucounts(new->user_ns, new->euid)))
		return -EAGAIN;

	new->ucounts = new_ucounts;
	if (old_ucounts)
		put_ucounts(old_ucounts);

	return 0;
}

/*
 * initialise the credentials stuff
 */
void __init cred_init(void)
{
	/* allocate a slab in which we can store credentials */
	cred_jar = kmem_cache_create("cred_jar", sizeof(struct cred), 0,
			SLAB_HWCACHE_ALIGN|SLAB_PANIC|SLAB_ACCOUNT, NULL);
}

/**
 * prepare_kernel_cred - Prepare a set of credentials for a kernel service
 * @daemon: A userspace daemon to be used as a reference
 *
 * Prepare a set of credentials for a kernel service.  This can then be used to
 * override a task's own credentials so that work can be done on behalf of that
 * task that requires a different subjective context.
 *
 * @daemon is used to provide a base for the security record, but can be NULL.
 * If @daemon is supplied, then the security data will be derived from that;
 * otherwise they'll be set to 0 and no groups, full capabilities and no keys.
 *
 * The caller may change these controls afterwards if desired.
 *
 * Returns the new credentials or NULL if out of memory.
 */
struct cred *prepare_kernel_cred(struct task_struct *daemon)
{
	const struct cred *old;
	struct cred *new;

	new = kmem_cache_alloc(cred_jar, GFP_KERNEL);
	if (!new)
		return NULL;

	kdebug("prepare_kernel_cred() alloc %p", new);

	if (daemon)
		old = get_task_cred(daemon);
	else
		old = get_cred(&init_cred);

	validate_creds(old);

	*new = *old;
	new->non_rcu = 0;
	atomic_set(&new->usage, 1);
	set_cred_subscribers(new, 0);
	get_uid(new->user);
	get_user_ns(new->user_ns);
	get_group_info(new->group_info);

#ifdef CONFIG_KEYS
	new->session_keyring = NULL;
	new->process_keyring = NULL;
	new->thread_keyring = NULL;
	new->request_key_auth = NULL;
	new->jit_keyring = KEY_REQKEY_DEFL_THREAD_KEYRING;
#endif

#ifdef CONFIG_SECURITY
	new->security = NULL;
#endif
	new->ucounts = get_ucounts(new->ucounts);
	if (!new->ucounts)
		goto error;

	if (security_prepare_creds(new, old, GFP_KERNEL_ACCOUNT) < 0)
		goto error;

	put_cred(old);
	validate_creds(new);
	return new;

error:
	put_cred(new);
	put_cred(old);
	return NULL;
}
EXPORT_SYMBOL(prepare_kernel_cred);

/**
 * set_security_override - Set the security ID in a set of credentials
 * @new: The credentials to alter
 * @secid: The LSM security ID to set
 *
 * Set the LSM security ID in a set of credentials so that the subjective
 * security is overridden when an alternative set of credentials is used.
 */
int set_security_override(struct cred *new, u32 secid)
{
	return security_kernel_act_as(new, secid);
}
EXPORT_SYMBOL(set_security_override);

/**
 * set_security_override_from_ctx - Set the security ID in a set of credentials
 * @new: The credentials to alter
 * @secctx: The LSM security context to generate the security ID from.
 *
 * Set the LSM security ID in a set of credentials so that the subjective
 * security is overridden when an alternative set of credentials is used.  The
 * security ID is specified in string form as a security context to be
 * interpreted by the LSM.
 */
int set_security_override_from_ctx(struct cred *new, const char *secctx)
{
	u32 secid;
	int ret;

	ret = security_secctx_to_secid(secctx, strlen(secctx), &secid);
	if (ret < 0)
		return ret;

	return set_security_override(new, secid);
}
EXPORT_SYMBOL(set_security_override_from_ctx);

/**
 * set_create_files_as - Set the LSM file create context in a set of credentials
 * @new: The credentials to alter
 * @inode: The inode to take the context from
 *
 * Change the LSM file creation context in a set of credentials to be the same
 * as the object context of the specified inode, so that the new inodes have
 * the same MAC context as that inode.
 */
int set_create_files_as(struct cred *new, struct inode *inode)
{
	if (!uid_valid(inode->i_uid) || !gid_valid(inode->i_gid))
		return -EINVAL;
	new->fsuid = inode->i_uid;
	new->fsgid = inode->i_gid;
	return security_kernel_create_files_as(new, inode);
}
EXPORT_SYMBOL(set_create_files_as);

#ifdef CONFIG_DEBUG_CREDENTIALS

bool creds_are_invalid(const struct cred *cred)
{
	if (cred->magic != CRED_MAGIC)
		return true;
	return false;
}
EXPORT_SYMBOL(creds_are_invalid);

/*
 * dump invalid credentials
 */
static void dump_invalid_creds(const struct cred *cred, const char *label,
			       const struct task_struct *tsk)
{
	printk(KERN_ERR "CRED: %s credentials: %p %s%s%s\n",
	       label, cred,
	       cred == &init_cred ? "[init]" : "",
	       cred == tsk->real_cred ? "[real]" : "",
	       cred == tsk->cred ? "[eff]" : "");
	printk(KERN_ERR "CRED: ->magic=%x, put_addr=%p\n",
	       cred->magic, cred->put_addr);
	printk(KERN_ERR "CRED: ->usage=%d, subscr=%d\n",
	       atomic_read(&cred->usage),
	       read_cred_subscribers(cred));
	printk(KERN_ERR "CRED: ->*uid = { %d,%d,%d,%d }\n",
		from_kuid_munged(&init_user_ns, cred->uid),
		from_kuid_munged(&init_user_ns, cred->euid),
		from_kuid_munged(&init_user_ns, cred->suid),
		from_kuid_munged(&init_user_ns, cred->fsuid));
	printk(KERN_ERR "CRED: ->*gid = { %d,%d,%d,%d }\n",
		from_kgid_munged(&init_user_ns, cred->gid),
		from_kgid_munged(&init_user_ns, cred->egid),
		from_kgid_munged(&init_user_ns, cred->sgid),
		from_kgid_munged(&init_user_ns, cred->fsgid));
#ifdef CONFIG_SECURITY
	printk(KERN_ERR "CRED: ->security is %p\n", cred->security);
	if ((unsigned long) cred->security >= PAGE_SIZE &&
	    (((unsigned long) cred->security & 0xffffff00) !=
	     (POISON_FREE << 24 | POISON_FREE << 16 | POISON_FREE << 8)))
		printk(KERN_ERR "CRED: ->security {%x, %x}\n",
		       ((u32*)cred->security)[0],
		       ((u32*)cred->security)[1]);
#endif
}

/*
 * report use of invalid credentials
 */
void __invalid_creds(const struct cred *cred, const char *file, unsigned line)
{
	printk(KERN_ERR "CRED: Invalid credentials\n");
	printk(KERN_ERR "CRED: At %s:%u\n", file, line);
	dump_invalid_creds(cred, "Specified", current);
	BUG();
}
EXPORT_SYMBOL(__invalid_creds);

/*
 * check the credentials on a process
 */
void __validate_process_creds(struct task_struct *tsk,
			      const char *file, unsigned line)
{
	if (tsk->cred == tsk->real_cred) {
		if (unlikely(read_cred_subscribers(tsk->cred) < 2 ||
			     creds_are_invalid(tsk->cred)))
			goto invalid_creds;
	} else {
		if (unlikely(read_cred_subscribers(tsk->real_cred) < 1 ||
			     read_cred_subscribers(tsk->cred) < 1 ||
			     creds_are_invalid(tsk->real_cred) ||
			     creds_are_invalid(tsk->cred)))
			goto invalid_creds;
	}
	return;

invalid_creds:
	printk(KERN_ERR "CRED: Invalid process credentials\n");
	printk(KERN_ERR "CRED: At %s:%u\n", file, line);

	dump_invalid_creds(tsk->real_cred, "Real", tsk);
	if (tsk->cred != tsk->real_cred)
		dump_invalid_creds(tsk->cred, "Effective", tsk);
	else
		printk(KERN_ERR "CRED: Effective creds == Real creds\n");
	BUG();
}
EXPORT_SYMBOL(__validate_process_creds);

/*
 * check creds for do_exit()
 */
void validate_creds_for_do_exit(struct task_struct *tsk)
{
	kdebug("validate_creds_for_do_exit(%p,%p{%d,%d})",
	       tsk->real_cred, tsk->cred,
	       atomic_read(&tsk->cred->usage),
	       read_cred_subscribers(tsk->cred));

	__validate_process_creds(tsk, __FILE__, __LINE__);
}

#endif /* CONFIG_DEBUG_CREDENTIALS */
