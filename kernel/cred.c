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
	.usage			= ATOMIC_INIT(3),
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
 * Returns a pointer to the new creds-to-be if successful, NULL otherwise.
 *
 * Call commit_creds() or abort_creds() to clean up.
 */
struct cred *prepare_creds(void)
{
	struct task_struct *task = current;
	const struct cred *old;
	struct cred *new;

	BUG_ON(atomic_read(&task->cred->usage) < 1);

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
 * - The caller must hold current->cred_exec_mutex
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
 */
int copy_creds(struct task_struct *p, unsigned long clone_flags)
{
#ifdef CONFIG_KEYS
	struct thread_group_cred *tgcred;
#endif
	struct cred *new;

	mutex_init(&p->cred_exec_mutex);

	if (
#ifdef CONFIG_KEYS
		!p->cred->thread_keyring &&
#endif
		clone_flags & CLONE_THREAD
	    ) {
		get_cred(p->cred);
		atomic_inc(&p->cred->user->processes);
		return 0;
	}

	new = prepare_creds();
	if (!new)
		return -ENOMEM;

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
			put_cred(new);
			return -ENOMEM;
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
	p->cred = new;
	return 0;
}

/**
 * commit_creds - Install new credentials upon the current task
 * @new: The credentials to be assigned
 *
 * Install a new set of credentials to the current task, using RCU to replace
 * the old set.
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

	BUG_ON(atomic_read(&new->usage) < 1);
	BUG_ON(atomic_read(&task->cred->usage) < 1);

	old = task->cred;
	security_commit_creds(new, old);

	/* dumpability changes */
	if (old->euid != new->euid ||
	    old->egid != new->egid ||
	    old->fsuid != new->fsuid ||
	    old->fsgid != new->fsgid ||
	    !cap_issubset(new->cap_permitted, old->cap_permitted)) {
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
 * override_creds - Temporarily override the current process's credentials
 * @new: The credentials to be assigned
 *
 * Install a set of temporary override credentials on the current process,
 * returning the old set for later reversion.
 */
const struct cred *override_creds(const struct cred *new)
{
	const struct cred *old = current->cred;

	rcu_assign_pointer(current->cred, get_cred(new));
	return old;
}
EXPORT_SYMBOL(override_creds);

/**
 * revert_creds - Revert a temporary credentials override
 * @old: The credentials to be restored
 *
 * Revert a temporary set of override credentials to an old set, discarding the
 * override set.
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
