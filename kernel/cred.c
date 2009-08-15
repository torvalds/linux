/* Task credentials management - see Documentation/credentials.txt
 *
 * Copyright (C) 2008 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/module.h>
#include <linux/cred.h>
#include <linux/sched.h>
#include <linux/key.h>
#include <linux/keyctl.h>
#include <linux/init_task.h>
#include <linux/security.h>
#include <linux/cn_proc.h>
#include "cred-internals.h"

static struct kmem_cache *cred_jar;

/*
 * The common credentials for the initial task's thread group
 */
#ifdef CONFIG_KEYS
static struct thread_group_cred init_tgcred = {
	.usage	= ATOMIC_INIT(2),
	.tgid	= 0,
	.lock	= SPIN_LOCK_UNLOCKED,
};
#endif

/*
 * The initial credentials for the initial task
 */
struct cred init_cred = {
	.usage			= ATOMIC_INIT(4),
	.securebits		= SECUREBITS_DEFAULT,
	.cap_inheritable	= CAP_INIT_INH_SET,
	.cap_permitted		= CAP_FULL_SET,
	.cap_effective		= CAP_INIT_EFF_SET,
	.cap_bset		= CAP_INIT_BSET,
	.user			= INIT_USER,
	.group_info		= &init_groups,
#ifdef CONFIG_KEYS
	.tgcred			= &init_tgcred,
#endif
};

/*
 * Dispose of the shared task group credentials
 */
#ifdef CONFIG_KEYS
static void release_tgcred_rcu(struct rcu_head *rcu)
{
	struct thread_group_cred *tgcred =
		container_of(rcu, struct thread_group_cred, rcu);

	BUG_ON(atomic_read(&tgcred->usage) != 0);

	key_put(tgcred->session_keyring);
	key_put(tgcred->process_keyring);
	kfree(tgcred);
}
#endif

/*
 * Release a set of thread group credentials.
 */
static void release_tgcred(struct cred *cred)
{
#ifdef CONFIG_KEYS
	struct thread_group_cred *tgcred = cred->tgcred;

	if (atomic_dec_and_test(&tgcred->usage))
		call_rcu(&tgcred->rcu, release_tgcred_rcu);
#endif
}

/*
 * The RCU callback to actually dispose of a set of credentials
 */
static void put_cred_rcu(struct rcu_head *rcu)
{
	struct cred *cred = container_of(rcu, struct cred, rcu);

	if (atomic_read(&cred->usage) != 0)
		panic("CRED: put_cred_rcu() sees %p with usage %d\n",
		      cred, atomic_read(&cred->usage));

	security_cred_free(cred);
	key_put(cred->thread_keyring);
	key_put(cred->request_key_auth);
	release_tgcred(cred);
	put_group_info(cred->group_info);
	free_uid(cred->user);
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
	BUG_ON(atomic_read(&cred->usage) != 0);

	call_rcu(&cred->rcu, put_cred_rcu);
}
EXPORT_SYMBOL(__put_cred);

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

	BUG_ON(atomic_read(&task->real_cred->usage) < 1);

	new = kmem_cache_alloc(cred_jar, GFP_KERNEL);
	if (!new)
		return NULL;

	old = task->cred;
	memcpy(new, old, sizeof(struct cred));

	atomic_set(&new->usage, 1);
	get_group_info(new->group_info);
	get_uid(new->user);

#ifdef CONFIG_KEYS
	key_get(new->thread_keyring);
	key_get(new->request_key_auth);
	atomic_inc(&new->tgcred->usage);
#endif

#ifdef CONFIG_SECURITY
	new->security = NULL;
#endif

	if (security_prepare_creds(new, old, GFP_KERNEL) < 0)
		goto error;
	return new;

error:
	abort_creds(new);
	return NULL;
}
EXPORT_SYMBOL(prepare_creds);

/*
 * Prepare credentials for current to perform an execve()
 * - The caller must hold current->cred_guard_mutex
 */
struct cred *prepare_exec_creds(void)
{
	struct thread_group_cred *tgcred = NULL;
	struct cred *new;

#ifdef CONFIG_KEYS
	tgcred = kmalloc(sizeof(*tgcred), GFP_KERNEL);
	if (!tgcred)
		return NULL;
#endif

	new = prepare_creds();
	if (!new) {
		kfree(tgcred);
		return new;
	}

#ifdef CONFIG_KEYS
	/* newly exec'd tasks don't get a thread keyring */
	key_put(new->thread_keyring);
	new->thread_keyring = NULL;

	/* create a new per-thread-group creds for all this set of threads to
	 * share */
	memcpy(tgcred, new->tgcred, sizeof(struct thread_group_cred));

	atomic_set(&tgcred->usage, 1);
	spin_lock_init(&tgcred->lock);

	/* inherit the session keyring; new process keyring */
	key_get(tgcred->session_keyring);
	tgcred->process_keyring = NULL;

	release_tgcred(new);
	new->tgcred = tgcred;
#endif

	return new;
}

/*
 * prepare new credentials for the usermode helper dispatcher
 */
struct cred *prepare_usermodehelper_creds(void)
{
#ifdef CONFIG_KEYS
	struct thread_group_cred *tgcred = NULL;
#endif
	struct cred *new;

#ifdef CONFIG_KEYS
	tgcred = kzalloc(sizeof(*new->tgcred), GFP_ATOMIC);
	if (!tgcred)
		return NULL;
#endif

	new = kmem_cache_alloc(cred_jar, GFP_ATOMIC);
	if (!new)
		return NULL;

	memcpy(new, &init_cred, sizeof(struct cred));

	atomic_set(&new->usage, 1);
	get_group_info(new->group_info);
	get_uid(new->user);

#ifdef CONFIG_KEYS
	new->thread_keyring = NULL;
	new->request_key_auth = NULL;
	new->jit_keyring = KEY_REQKEY_DEFL_DEFAULT;

	atomic_set(&tgcred->usage, 1);
	spin_lock_init(&tgcred->lock);
	new->tgcred = tgcred;
#endif

#ifdef CONFIG_SECURITY
	new->security = NULL;
#endif
	if (security_prepare_creds(new, &init_cred, GFP_ATOMIC) < 0)
		goto error;

	BUG_ON(atomic_read(&new->usage) != 1);
	return new;

error:
	put_cred(new);
	return NULL;
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
#ifdef CONFIG_KEYS
	struct thread_group_cred *tgcred;
#endif
	struct cred *new;
	int ret;

	mutex_init(&p->cred_guard_mutex);

	if (
#ifdef CONFIG_KEYS
		!p->cred->thread_keyring &&
#endif
		clone_flags & CLONE_THREAD
	    ) {
		p->real_cred = get_cred(p->cred);
		get_cred(p->cred);
		atomic_inc(&p->cred->user->processes);
		return 0;
	}

	new = prepare_creds();
	if (!new)
		return -ENOMEM;

	if (clone_flags & CLONE_NEWUSER) {
		ret = create_user_ns(new);
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

	/* we share the process and session keyrings between all the threads in
	 * a process - this is slightly icky as we violate COW credentials a
	 * bit */
	if (!(clone_flags & CLONE_THREAD)) {
		tgcred = kmalloc(sizeof(*tgcred), GFP_KERNEL);
		if (!tgcred) {
			ret = -ENOMEM;
			goto error_put;
		}
		atomic_set(&tgcred->usage, 1);
		spin_lock_init(&tgcred->lock);
		tgcred->process_keyring = NULL;
		tgcred->session_keyring = key_get(new->tgcred->session_keyring);

		release_tgcred(new);
		new->tgcred = tgcred;
	}
#endif

	atomic_inc(&new->user->processes);
	p->cred = p->real_cred = get_cred(new);
	return 0;

error_put:
	put_cred(new);
	return ret;
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
	const struct cred *old;

	BUG_ON(task->cred != task->real_cred);
	BUG_ON(atomic_read(&task->real_cred->usage) < 2);
	BUG_ON(atomic_read(&new->usage) < 1);

	old = task->real_cred;
	security_commit_creds(new, old);

	get_cred(new); /* we will require a ref for the subj creds too */

	/* dumpability changes */
	if (old->euid != new->euid ||
	    old->egid != new->egid ||
	    old->fsuid != new->fsuid ||
	    old->fsgid != new->fsgid ||
	    !cap_issubset(new->cap_permitted, old->cap_permitted)) {
		if (task->mm)
			set_dumpable(task->mm, suid_dumpable);
		task->pdeath_signal = 0;
		smp_wmb();
	}

	/* alter the thread keyring */
	if (new->fsuid != old->fsuid)
		key_fsuid_changed(task);
	if (new->fsgid != old->fsgid)
		key_fsgid_changed(task);

	/* do it
	 * - What if a process setreuid()'s and this brings the
	 *   new uid over his NPROC rlimit?  We can check this now
	 *   cheaply with the new uid cache, so if it matters
	 *   we should be checking for it.  -DaveM
	 */
	if (new->user != old->user)
		atomic_inc(&new->user->processes);
	rcu_assign_pointer(task->real_cred, new);
	rcu_assign_pointer(task->cred, new);
	if (new->user != old->user)
		atomic_dec(&old->user->processes);

	sched_switch_user(task);

	/* send notifications */
	if (new->uid   != old->uid  ||
	    new->euid  != old->euid ||
	    new->suid  != old->suid ||
	    new->fsuid != old->fsuid)
		proc_id_connector(task, PROC_EVENT_UID);

	if (new->gid   != old->gid  ||
	    new->egid  != old->egid ||
	    new->sgid  != old->sgid ||
	    new->fsgid != old->fsgid)
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

	rcu_assign_pointer(current->cred, get_cred(new));
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

	rcu_assign_pointer(current->cred, old);
	put_cred(override);
}
EXPORT_SYMBOL(revert_creds);

/*
 * initialise the credentials stuff
 */
void __init cred_init(void)
{
	/* allocate a slab in which we can store credentials */
	cred_jar = kmem_cache_create("cred_jar", sizeof(struct cred),
				     0, SLAB_HWCACHE_ALIGN|SLAB_PANIC, NULL);
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
 *
 * Does not take, and does not return holding current->cred_replace_mutex.
 */
struct cred *prepare_kernel_cred(struct task_struct *daemon)
{
	const struct cred *old;
	struct cred *new;

	new = kmem_cache_alloc(cred_jar, GFP_KERNEL);
	if (!new)
		return NULL;

	if (daemon)
		old = get_task_cred(daemon);
	else
		old = get_cred(&init_cred);

	*new = *old;
	get_uid(new->user);
	get_group_info(new->group_info);

#ifdef CONFIG_KEYS
	atomic_inc(&init_tgcred.usage);
	new->tgcred = &init_tgcred;
	new->request_key_auth = NULL;
	new->thread_keyring = NULL;
	new->jit_keyring = KEY_REQKEY_DEFL_THREAD_KEYRING;
#endif

#ifdef CONFIG_SECURITY
	new->security = NULL;
#endif
	if (security_prepare_creds(new, old, GFP_KERNEL) < 0)
		goto error;

	atomic_set(&new->usage, 1);
	put_cred(old);
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
	new->fsuid = inode->i_uid;
	new->fsgid = inode->i_gid;
	return security_kernel_create_files_as(new, inode);
}
EXPORT_SYMBOL(set_create_files_as);
