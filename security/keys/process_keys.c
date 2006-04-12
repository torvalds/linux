/* process_keys.c: management of a process's keyrings
 *
 * Copyright (C) 2004-5 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/keyctl.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <asm/uaccess.h>
#include "internal.h"

/* session keyring create vs join semaphore */
static DEFINE_MUTEX(key_session_mutex);

/* the root user's tracking struct */
struct key_user root_key_user = {
	.usage		= ATOMIC_INIT(3),
	.consq		= LIST_HEAD_INIT(root_key_user.consq),
	.lock		= SPIN_LOCK_UNLOCKED,
	.nkeys		= ATOMIC_INIT(2),
	.nikeys		= ATOMIC_INIT(2),
	.uid		= 0,
};

/* the root user's UID keyring */
struct key root_user_keyring = {
	.usage		= ATOMIC_INIT(1),
	.serial		= 2,
	.type		= &key_type_keyring,
	.user		= &root_key_user,
	.sem		= __RWSEM_INITIALIZER(root_user_keyring.sem),
	.perm		= (KEY_POS_ALL & ~KEY_POS_SETATTR) | KEY_USR_ALL,
	.flags		= 1 << KEY_FLAG_INSTANTIATED,
	.description	= "_uid.0",
#ifdef KEY_DEBUGGING
	.magic		= KEY_DEBUG_MAGIC,
#endif
};

/* the root user's default session keyring */
struct key root_session_keyring = {
	.usage		= ATOMIC_INIT(1),
	.serial		= 1,
	.type		= &key_type_keyring,
	.user		= &root_key_user,
	.sem		= __RWSEM_INITIALIZER(root_session_keyring.sem),
	.perm		= (KEY_POS_ALL & ~KEY_POS_SETATTR) | KEY_USR_ALL,
	.flags		= 1 << KEY_FLAG_INSTANTIATED,
	.description	= "_uid_ses.0",
#ifdef KEY_DEBUGGING
	.magic		= KEY_DEBUG_MAGIC,
#endif
};

/*****************************************************************************/
/*
 * allocate the keyrings to be associated with a UID
 */
int alloc_uid_keyring(struct user_struct *user)
{
	struct key *uid_keyring, *session_keyring;
	char buf[20];
	int ret;

	/* concoct a default session keyring */
	sprintf(buf, "_uid_ses.%u", user->uid);

	session_keyring = keyring_alloc(buf, user->uid, (gid_t) -1, 0, NULL);
	if (IS_ERR(session_keyring)) {
		ret = PTR_ERR(session_keyring);
		goto error;
	}

	/* and a UID specific keyring, pointed to by the default session
	 * keyring */
	sprintf(buf, "_uid.%u", user->uid);

	uid_keyring = keyring_alloc(buf, user->uid, (gid_t) -1, 0,
				    session_keyring);
	if (IS_ERR(uid_keyring)) {
		key_put(session_keyring);
		ret = PTR_ERR(uid_keyring);
		goto error;
	}

	/* install the keyrings */
	user->uid_keyring = uid_keyring;
	user->session_keyring = session_keyring;
	ret = 0;

error:
	return ret;

} /* end alloc_uid_keyring() */

/*****************************************************************************/
/*
 * deal with the UID changing
 */
void switch_uid_keyring(struct user_struct *new_user)
{
#if 0 /* do nothing for now */
	struct key *old;

	/* switch to the new user's session keyring if we were running under
	 * root's default session keyring */
	if (new_user->uid != 0 &&
	    current->session_keyring == &root_session_keyring
	    ) {
		atomic_inc(&new_user->session_keyring->usage);

		task_lock(current);
		old = current->session_keyring;
		current->session_keyring = new_user->session_keyring;
		task_unlock(current);

		key_put(old);
	}
#endif

} /* end switch_uid_keyring() */

/*****************************************************************************/
/*
 * install a fresh thread keyring, discarding the old one
 */
int install_thread_keyring(struct task_struct *tsk)
{
	struct key *keyring, *old;
	char buf[20];
	int ret;

	sprintf(buf, "_tid.%u", tsk->pid);

	keyring = keyring_alloc(buf, tsk->uid, tsk->gid, 1, NULL);
	if (IS_ERR(keyring)) {
		ret = PTR_ERR(keyring);
		goto error;
	}

	task_lock(tsk);
	old = tsk->thread_keyring;
	tsk->thread_keyring = keyring;
	task_unlock(tsk);

	ret = 0;

	key_put(old);
error:
	return ret;

} /* end install_thread_keyring() */

/*****************************************************************************/
/*
 * make sure a process keyring is installed
 */
int install_process_keyring(struct task_struct *tsk)
{
	struct key *keyring;
	char buf[20];
	int ret;

	might_sleep();

	if (!tsk->signal->process_keyring) {
		sprintf(buf, "_pid.%u", tsk->tgid);

		keyring = keyring_alloc(buf, tsk->uid, tsk->gid, 1, NULL);
		if (IS_ERR(keyring)) {
			ret = PTR_ERR(keyring);
			goto error;
		}

		/* attach keyring */
		spin_lock_irq(&tsk->sighand->siglock);
		if (!tsk->signal->process_keyring) {
			tsk->signal->process_keyring = keyring;
			keyring = NULL;
		}
		spin_unlock_irq(&tsk->sighand->siglock);

		key_put(keyring);
	}

	ret = 0;
error:
	return ret;

} /* end install_process_keyring() */

/*****************************************************************************/
/*
 * install a session keyring, discarding the old one
 * - if a keyring is not supplied, an empty one is invented
 */
static int install_session_keyring(struct task_struct *tsk,
				   struct key *keyring)
{
	struct key *old;
	char buf[20];

	might_sleep();

	/* create an empty session keyring */
	if (!keyring) {
		sprintf(buf, "_ses.%u", tsk->tgid);

		keyring = keyring_alloc(buf, tsk->uid, tsk->gid, 1, NULL);
		if (IS_ERR(keyring))
			return PTR_ERR(keyring);
	}
	else {
		atomic_inc(&keyring->usage);
	}

	/* install the keyring */
	spin_lock_irq(&tsk->sighand->siglock);
	old = tsk->signal->session_keyring;
	rcu_assign_pointer(tsk->signal->session_keyring, keyring);
	spin_unlock_irq(&tsk->sighand->siglock);

	/* we're using RCU on the pointer, but there's no point synchronising
	 * on it if it didn't previously point to anything */
	if (old) {
		synchronize_rcu();
		key_put(old);
	}

	return 0;

} /* end install_session_keyring() */

/*****************************************************************************/
/*
 * copy the keys in a thread group for fork without CLONE_THREAD
 */
int copy_thread_group_keys(struct task_struct *tsk)
{
	key_check(current->thread_group->session_keyring);
	key_check(current->thread_group->process_keyring);

	/* no process keyring yet */
	tsk->signal->process_keyring = NULL;

	/* same session keyring */
	rcu_read_lock();
	tsk->signal->session_keyring =
		key_get(rcu_dereference(current->signal->session_keyring));
	rcu_read_unlock();

	return 0;

} /* end copy_thread_group_keys() */

/*****************************************************************************/
/*
 * copy the keys for fork
 */
int copy_keys(unsigned long clone_flags, struct task_struct *tsk)
{
	key_check(tsk->thread_keyring);
	key_check(tsk->request_key_auth);

	/* no thread keyring yet */
	tsk->thread_keyring = NULL;

	/* copy the request_key() authorisation for this thread */
	key_get(tsk->request_key_auth);

	return 0;

} /* end copy_keys() */

/*****************************************************************************/
/*
 * dispose of thread group keys upon thread group destruction
 */
void exit_thread_group_keys(struct signal_struct *tg)
{
	key_put(tg->session_keyring);
	key_put(tg->process_keyring);

} /* end exit_thread_group_keys() */

/*****************************************************************************/
/*
 * dispose of per-thread keys upon thread exit
 */
void exit_keys(struct task_struct *tsk)
{
	key_put(tsk->thread_keyring);
	key_put(tsk->request_key_auth);

} /* end exit_keys() */

/*****************************************************************************/
/*
 * deal with execve()
 */
int exec_keys(struct task_struct *tsk)
{
	struct key *old;

	/* newly exec'd tasks don't get a thread keyring */
	task_lock(tsk);
	old = tsk->thread_keyring;
	tsk->thread_keyring = NULL;
	task_unlock(tsk);

	key_put(old);

	/* discard the process keyring from a newly exec'd task */
	spin_lock_irq(&tsk->sighand->siglock);
	old = tsk->signal->process_keyring;
	tsk->signal->process_keyring = NULL;
	spin_unlock_irq(&tsk->sighand->siglock);

	key_put(old);

	return 0;

} /* end exec_keys() */

/*****************************************************************************/
/*
 * deal with SUID programs
 * - we might want to make this invent a new session keyring
 */
int suid_keys(struct task_struct *tsk)
{
	return 0;

} /* end suid_keys() */

/*****************************************************************************/
/*
 * the filesystem user ID changed
 */
void key_fsuid_changed(struct task_struct *tsk)
{
	/* update the ownership of the thread keyring */
	if (tsk->thread_keyring) {
		down_write(&tsk->thread_keyring->sem);
		tsk->thread_keyring->uid = tsk->fsuid;
		up_write(&tsk->thread_keyring->sem);
	}

} /* end key_fsuid_changed() */

/*****************************************************************************/
/*
 * the filesystem group ID changed
 */
void key_fsgid_changed(struct task_struct *tsk)
{
	/* update the ownership of the thread keyring */
	if (tsk->thread_keyring) {
		down_write(&tsk->thread_keyring->sem);
		tsk->thread_keyring->gid = tsk->fsgid;
		up_write(&tsk->thread_keyring->sem);
	}

} /* end key_fsgid_changed() */

/*****************************************************************************/
/*
 * search the process keyrings for the first matching key
 * - we use the supplied match function to see if the description (or other
 *   feature of interest) matches
 * - we return -EAGAIN if we didn't find any matching key
 * - we return -ENOKEY if we found only negative matching keys
 */
key_ref_t search_process_keyrings(struct key_type *type,
				  const void *description,
				  key_match_func_t match,
				  struct task_struct *context)
{
	struct request_key_auth *rka;
	key_ref_t key_ref, ret, err;

	/* we want to return -EAGAIN or -ENOKEY if any of the keyrings were
	 * searchable, but we failed to find a key or we found a negative key;
	 * otherwise we want to return a sample error (probably -EACCES) if
	 * none of the keyrings were searchable
	 *
	 * in terms of priority: success > -ENOKEY > -EAGAIN > other error
	 */
	key_ref = NULL;
	ret = NULL;
	err = ERR_PTR(-EAGAIN);

	/* search the thread keyring first */
	if (context->thread_keyring) {
		key_ref = keyring_search_aux(
			make_key_ref(context->thread_keyring, 1),
			context, type, description, match);
		if (!IS_ERR(key_ref))
			goto found;

		switch (PTR_ERR(key_ref)) {
		case -EAGAIN: /* no key */
			if (ret)
				break;
		case -ENOKEY: /* negative key */
			ret = key_ref;
			break;
		default:
			err = key_ref;
			break;
		}
	}

	/* search the process keyring second */
	if (context->signal->process_keyring) {
		key_ref = keyring_search_aux(
			make_key_ref(context->signal->process_keyring, 1),
			context, type, description, match);
		if (!IS_ERR(key_ref))
			goto found;

		switch (PTR_ERR(key_ref)) {
		case -EAGAIN: /* no key */
			if (ret)
				break;
		case -ENOKEY: /* negative key */
			ret = key_ref;
			break;
		default:
			err = key_ref;
			break;
		}
	}

	/* search the session keyring */
	if (context->signal->session_keyring) {
		rcu_read_lock();
		key_ref = keyring_search_aux(
			make_key_ref(rcu_dereference(
					     context->signal->session_keyring),
				     1),
			context, type, description, match);
		rcu_read_unlock();

		if (!IS_ERR(key_ref))
			goto found;

		switch (PTR_ERR(key_ref)) {
		case -EAGAIN: /* no key */
			if (ret)
				break;
		case -ENOKEY: /* negative key */
			ret = key_ref;
			break;
		default:
			err = key_ref;
			break;
		}
	}
	/* or search the user-session keyring */
	else {
		key_ref = keyring_search_aux(
			make_key_ref(context->user->session_keyring, 1),
			context, type, description, match);
		if (!IS_ERR(key_ref))
			goto found;

		switch (PTR_ERR(key_ref)) {
		case -EAGAIN: /* no key */
			if (ret)
				break;
		case -ENOKEY: /* negative key */
			ret = key_ref;
			break;
		default:
			err = key_ref;
			break;
		}
	}

	/* if this process has an instantiation authorisation key, then we also
	 * search the keyrings of the process mentioned there
	 * - we don't permit access to request_key auth keys via this method
	 */
	if (context->request_key_auth &&
	    context == current &&
	    type != &key_type_request_key_auth &&
	    key_validate(context->request_key_auth) == 0
	    ) {
		rka = context->request_key_auth->payload.data;

		key_ref = search_process_keyrings(type, description, match,
						  rka->context);

		if (!IS_ERR(key_ref))
			goto found;

		switch (PTR_ERR(key_ref)) {
		case -EAGAIN: /* no key */
			if (ret)
				break;
		case -ENOKEY: /* negative key */
			ret = key_ref;
			break;
		default:
			err = key_ref;
			break;
		}
	}

	/* no key - decide on the error we're going to go for */
	key_ref = ret ? ret : err;

found:
	return key_ref;

} /* end search_process_keyrings() */

/*****************************************************************************/
/*
 * see if the key we're looking at is the target key
 */
static int lookup_user_key_possessed(const struct key *key, const void *target)
{
	return key == target;

} /* end lookup_user_key_possessed() */

/*****************************************************************************/
/*
 * lookup a key given a key ID from userspace with a given permissions mask
 * - don't create special keyrings unless so requested
 * - partially constructed keys aren't found unless requested
 */
key_ref_t lookup_user_key(struct task_struct *context, key_serial_t id,
			  int create, int partial, key_perm_t perm)
{
	key_ref_t key_ref, skey_ref;
	struct key *key;
	int ret;

	if (!context)
		context = current;

	key_ref = ERR_PTR(-ENOKEY);

	switch (id) {
	case KEY_SPEC_THREAD_KEYRING:
		if (!context->thread_keyring) {
			if (!create)
				goto error;

			ret = install_thread_keyring(context);
			if (ret < 0) {
				key = ERR_PTR(ret);
				goto error;
			}
		}

		key = context->thread_keyring;
		atomic_inc(&key->usage);
		key_ref = make_key_ref(key, 1);
		break;

	case KEY_SPEC_PROCESS_KEYRING:
		if (!context->signal->process_keyring) {
			if (!create)
				goto error;

			ret = install_process_keyring(context);
			if (ret < 0) {
				key = ERR_PTR(ret);
				goto error;
			}
		}

		key = context->signal->process_keyring;
		atomic_inc(&key->usage);
		key_ref = make_key_ref(key, 1);
		break;

	case KEY_SPEC_SESSION_KEYRING:
		if (!context->signal->session_keyring) {
			/* always install a session keyring upon access if one
			 * doesn't exist yet */
			ret = install_session_keyring(
				context, context->user->session_keyring);
			if (ret < 0)
				goto error;
		}

		rcu_read_lock();
		key = rcu_dereference(context->signal->session_keyring);
		atomic_inc(&key->usage);
		rcu_read_unlock();
		key_ref = make_key_ref(key, 1);
		break;

	case KEY_SPEC_USER_KEYRING:
		key = context->user->uid_keyring;
		atomic_inc(&key->usage);
		key_ref = make_key_ref(key, 1);
		break;

	case KEY_SPEC_USER_SESSION_KEYRING:
		key = context->user->session_keyring;
		atomic_inc(&key->usage);
		key_ref = make_key_ref(key, 1);
		break;

	case KEY_SPEC_GROUP_KEYRING:
		/* group keyrings are not yet supported */
		key = ERR_PTR(-EINVAL);
		goto error;

	case KEY_SPEC_REQKEY_AUTH_KEY:
		key = context->request_key_auth;
		if (!key)
			goto error;

		atomic_inc(&key->usage);
		key_ref = make_key_ref(key, 1);
		break;

	default:
		key_ref = ERR_PTR(-EINVAL);
		if (id < 1)
			goto error;

		key = key_lookup(id);
		if (IS_ERR(key)) {
			key_ref = ERR_PTR(PTR_ERR(key));
			goto error;
		}

		key_ref = make_key_ref(key, 0);

		/* check to see if we possess the key */
		skey_ref = search_process_keyrings(key->type, key,
						   lookup_user_key_possessed,
						   current);

		if (!IS_ERR(skey_ref)) {
			key_put(key);
			key_ref = skey_ref;
		}

		break;
	}

	/* check the status */
	if (perm) {
		ret = key_validate(key);
		if (ret < 0)
			goto invalid_key;
	}

	ret = -EIO;
	if (!partial && !test_bit(KEY_FLAG_INSTANTIATED, &key->flags))
		goto invalid_key;

	/* check the permissions */
	ret = key_task_permission(key_ref, context, perm);
	if (ret < 0)
		goto invalid_key;

error:
	return key_ref;

invalid_key:
	key_ref_put(key_ref);
	key_ref = ERR_PTR(ret);
	goto error;

} /* end lookup_user_key() */

/*****************************************************************************/
/*
 * join the named keyring as the session keyring if possible, or attempt to
 * create a new one of that name if not
 * - if the name is NULL, an empty anonymous keyring is installed instead
 * - named session keyring joining is done with a semaphore held
 */
long join_session_keyring(const char *name)
{
	struct task_struct *tsk = current;
	struct key *keyring;
	long ret;

	/* if no name is provided, install an anonymous keyring */
	if (!name) {
		ret = install_session_keyring(tsk, NULL);
		if (ret < 0)
			goto error;

		rcu_read_lock();
		ret = rcu_dereference(tsk->signal->session_keyring)->serial;
		rcu_read_unlock();
		goto error;
	}

	/* allow the user to join or create a named keyring */
	mutex_lock(&key_session_mutex);

	/* look for an existing keyring of this name */
	keyring = find_keyring_by_name(name, 0);
	if (PTR_ERR(keyring) == -ENOKEY) {
		/* not found - try and create a new one */
		keyring = keyring_alloc(name, tsk->uid, tsk->gid, 0, NULL);
		if (IS_ERR(keyring)) {
			ret = PTR_ERR(keyring);
			goto error2;
		}
	}
	else if (IS_ERR(keyring)) {
		ret = PTR_ERR(keyring);
		goto error2;
	}

	/* we've got a keyring - now to install it */
	ret = install_session_keyring(tsk, keyring);
	if (ret < 0)
		goto error2;

	ret = keyring->serial;
	key_put(keyring);

error2:
	mutex_unlock(&key_session_mutex);
error:
	return ret;

} /* end join_session_keyring() */
