/* request_key.c: request a key from userspace
 *
 * Copyright (C) 2004-6 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * See Documentation/keys-request-key.txt
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kmod.h>
#include <linux/err.h>
#include <linux/keyctl.h>
#include "internal.h"

struct key_construction {
	struct list_head	link;	/* link in construction queue */
	struct key		*key;	/* key being constructed */
};

/* when waiting for someone else's keys, you get added to this */
DECLARE_WAIT_QUEUE_HEAD(request_key_conswq);

/*****************************************************************************/
/*
 * request userspace finish the construction of a key
 * - execute "/sbin/request-key <op> <key> <uid> <gid> <keyring> <keyring> <keyring>"
 */
static int call_sbin_request_key(struct key *key,
				 struct key *authkey,
				 const char *op,
				 void *aux)
{
	struct task_struct *tsk = current;
	key_serial_t prkey, sskey;
	struct key *keyring;
	char *argv[9], *envp[3], uid_str[12], gid_str[12];
	char key_str[12], keyring_str[3][12];
	char desc[20];
	int ret, i;

	kenter("{%d},{%d},%s", key->serial, authkey->serial, op);

	/* allocate a new session keyring */
	sprintf(desc, "_req.%u", key->serial);

	keyring = keyring_alloc(desc, current->fsuid, current->fsgid, current,
				KEY_ALLOC_QUOTA_OVERRUN, NULL);
	if (IS_ERR(keyring)) {
		ret = PTR_ERR(keyring);
		goto error_alloc;
	}

	/* attach the auth key to the session keyring */
	ret = __key_link(keyring, authkey);
	if (ret < 0)
		goto error_link;

	/* record the UID and GID */
	sprintf(uid_str, "%d", current->fsuid);
	sprintf(gid_str, "%d", current->fsgid);

	/* we say which key is under construction */
	sprintf(key_str, "%d", key->serial);

	/* we specify the process's default keyrings */
	sprintf(keyring_str[0], "%d",
		tsk->thread_keyring ? tsk->thread_keyring->serial : 0);

	prkey = 0;
	if (tsk->signal->process_keyring)
		prkey = tsk->signal->process_keyring->serial;

	sprintf(keyring_str[1], "%d", prkey);

	if (tsk->signal->session_keyring) {
		rcu_read_lock();
		sskey = rcu_dereference(tsk->signal->session_keyring)->serial;
		rcu_read_unlock();
	}
	else {
		sskey = tsk->user->session_keyring->serial;
	}

	sprintf(keyring_str[2], "%d", sskey);

	/* set up a minimal environment */
	i = 0;
	envp[i++] = "HOME=/";
	envp[i++] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";
	envp[i] = NULL;

	/* set up the argument list */
	i = 0;
	argv[i++] = "/sbin/request-key";
	argv[i++] = (char *) op;
	argv[i++] = key_str;
	argv[i++] = uid_str;
	argv[i++] = gid_str;
	argv[i++] = keyring_str[0];
	argv[i++] = keyring_str[1];
	argv[i++] = keyring_str[2];
	argv[i] = NULL;

	/* do it */
	ret = call_usermodehelper_keys(argv[0], argv, envp, keyring, 1);

error_link:
	key_put(keyring);

error_alloc:
	kleave(" = %d", ret);
	return ret;

} /* end call_sbin_request_key() */

/*****************************************************************************/
/*
 * call out to userspace for the key
 * - called with the construction sem held, but the sem is dropped here
 * - we ignore program failure and go on key status instead
 */
static struct key *__request_key_construction(struct key_type *type,
					      const char *description,
					      const char *callout_info,
					      void *aux,
					      unsigned long flags)
{
	request_key_actor_t actor;
	struct key_construction cons;
	struct timespec now;
	struct key *key, *authkey;
	int ret, negated;

	kenter("%s,%s,%s,%lx", type->name, description, callout_info, flags);

	/* create a key and add it to the queue */
	key = key_alloc(type, description,
			current->fsuid, current->fsgid, current, KEY_POS_ALL,
			flags);
	if (IS_ERR(key))
		goto alloc_failed;

	set_bit(KEY_FLAG_USER_CONSTRUCT, &key->flags);

	cons.key = key;
	list_add_tail(&cons.link, &key->user->consq);

	/* we drop the construction sem here on behalf of the caller */
	up_write(&key_construction_sem);

	/* allocate an authorisation key */
	authkey = request_key_auth_new(key, callout_info);
	if (IS_ERR(authkey)) {
		ret = PTR_ERR(authkey);
		authkey = NULL;
		goto alloc_authkey_failed;
	}

	/* make the call */
	actor = call_sbin_request_key;
	if (type->request_key)
		actor = type->request_key;
	ret = actor(key, authkey, "create", aux);
	if (ret < 0)
		goto request_failed;

	/* if the key wasn't instantiated, then we want to give an error */
	ret = -ENOKEY;
	if (!test_bit(KEY_FLAG_INSTANTIATED, &key->flags))
		goto request_failed;

	key_revoke(authkey);
	key_put(authkey);

	down_write(&key_construction_sem);
	list_del(&cons.link);
	up_write(&key_construction_sem);

	/* also give an error if the key was negatively instantiated */
check_not_negative:
	if (test_bit(KEY_FLAG_NEGATIVE, &key->flags)) {
		key_put(key);
		key = ERR_PTR(-ENOKEY);
	}

out:
	kleave(" = %p", key);
	return key;

request_failed:
	key_revoke(authkey);
	key_put(authkey);

alloc_authkey_failed:
	/* it wasn't instantiated
	 * - remove from construction queue
	 * - mark the key as dead
	 */
	negated = 0;
	down_write(&key_construction_sem);

	list_del(&cons.link);

	/* check it didn't get instantiated between the check and the down */
	if (!test_bit(KEY_FLAG_INSTANTIATED, &key->flags)) {
		set_bit(KEY_FLAG_NEGATIVE, &key->flags);
		set_bit(KEY_FLAG_INSTANTIATED, &key->flags);
		negated = 1;
	}

	clear_bit(KEY_FLAG_USER_CONSTRUCT, &key->flags);

	up_write(&key_construction_sem);

	if (!negated)
		goto check_not_negative; /* surprisingly, the key got
					  * instantiated */

	/* set the timeout and store in the session keyring if we can */
	now = current_kernel_time();
	key->expiry = now.tv_sec + key_negative_timeout;

	if (current->signal->session_keyring) {
		struct key *keyring;

		rcu_read_lock();
		keyring = rcu_dereference(current->signal->session_keyring);
		atomic_inc(&keyring->usage);
		rcu_read_unlock();

		key_link(keyring, key);
		key_put(keyring);
	}

	key_put(key);

	/* notify anyone who was waiting */
	wake_up_all(&request_key_conswq);

	key = ERR_PTR(ret);
	goto out;

alloc_failed:
	up_write(&key_construction_sem);
	goto out;

} /* end __request_key_construction() */

/*****************************************************************************/
/*
 * call out to userspace to request the key
 * - we check the construction queue first to see if an appropriate key is
 *   already being constructed by userspace
 */
static struct key *request_key_construction(struct key_type *type,
					    const char *description,
					    const char *callout_info,
					    void *aux,
					    struct key_user *user,
					    unsigned long flags)
{
	struct key_construction *pcons;
	struct key *key, *ckey;

	DECLARE_WAITQUEUE(myself, current);

	kenter("%s,%s,{%d},%s,%lx",
	       type->name, description, user->uid, callout_info, flags);

	/* see if there's such a key under construction already */
	down_write(&key_construction_sem);

	list_for_each_entry(pcons, &user->consq, link) {
		ckey = pcons->key;

		if (ckey->type != type)
			continue;

		if (type->match(ckey, description))
			goto found_key_under_construction;
	}

	/* see about getting userspace to construct the key */
	key = __request_key_construction(type, description, callout_info, aux,
					 flags);
 error:
	kleave(" = %p", key);
	return key;

	/* someone else has the same key under construction
	 * - we want to keep an eye on their key
	 */
 found_key_under_construction:
	atomic_inc(&ckey->usage);
	up_write(&key_construction_sem);

	/* wait for the key to be completed one way or another */
	add_wait_queue(&request_key_conswq, &myself);

	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (!test_bit(KEY_FLAG_USER_CONSTRUCT, &ckey->flags))
			break;
		if (signal_pending(current))
			break;
		schedule();
	}

	set_current_state(TASK_RUNNING);
	remove_wait_queue(&request_key_conswq, &myself);

	/* we'll need to search this process's keyrings to see if the key is
	 * now there since we can't automatically assume it's also available
	 * there */
	key_put(ckey);
	ckey = NULL;

	key = NULL; /* request a retry */
	goto error;

} /* end request_key_construction() */

/*****************************************************************************/
/*
 * link a freshly minted key to an appropriate destination keyring
 */
static void request_key_link(struct key *key, struct key *dest_keyring)
{
	struct task_struct *tsk = current;
	struct key *drop = NULL;

	kenter("{%d},%p", key->serial, dest_keyring);

	/* find the appropriate keyring */
	if (!dest_keyring) {
		switch (tsk->jit_keyring) {
		case KEY_REQKEY_DEFL_DEFAULT:
		case KEY_REQKEY_DEFL_THREAD_KEYRING:
			dest_keyring = tsk->thread_keyring;
			if (dest_keyring)
				break;

		case KEY_REQKEY_DEFL_PROCESS_KEYRING:
			dest_keyring = tsk->signal->process_keyring;
			if (dest_keyring)
				break;

		case KEY_REQKEY_DEFL_SESSION_KEYRING:
			rcu_read_lock();
			dest_keyring = key_get(
				rcu_dereference(tsk->signal->session_keyring));
			rcu_read_unlock();
			drop = dest_keyring;

			if (dest_keyring)
				break;

		case KEY_REQKEY_DEFL_USER_SESSION_KEYRING:
			dest_keyring = current->user->session_keyring;
			break;

		case KEY_REQKEY_DEFL_USER_KEYRING:
			dest_keyring = current->user->uid_keyring;
			break;

		case KEY_REQKEY_DEFL_GROUP_KEYRING:
		default:
			BUG();
		}
	}

	/* and attach the key to it */
	key_link(dest_keyring, key);

	key_put(drop);

	kleave("");

} /* end request_key_link() */

/*****************************************************************************/
/*
 * request a key
 * - search the process's keyrings
 * - check the list of keys being created or updated
 * - call out to userspace for a key if supplementary info was provided
 * - cache the key in an appropriate keyring
 */
struct key *request_key_and_link(struct key_type *type,
				 const char *description,
				 const char *callout_info,
				 void *aux,
				 struct key *dest_keyring,
				 unsigned long flags)
{
	struct key_user *user;
	struct key *key;
	key_ref_t key_ref;

	kenter("%s,%s,%s,%p,%p,%lx",
	       type->name, description, callout_info, aux,
	       dest_keyring, flags);

	/* search all the process keyrings for a key */
	key_ref = search_process_keyrings(type, description, type->match,
					  current);

	kdebug("search 1: %p", key_ref);

	if (!IS_ERR(key_ref)) {
		key = key_ref_to_ptr(key_ref);
	}
	else if (PTR_ERR(key_ref) != -EAGAIN) {
		key = ERR_PTR(PTR_ERR(key_ref));
	}
	else  {
		/* the search failed, but the keyrings were searchable, so we
		 * should consult userspace if we can */
		key = ERR_PTR(-ENOKEY);
		if (!callout_info)
			goto error;

		/* - get hold of the user's construction queue */
		user = key_user_lookup(current->fsuid);
		if (!user)
			goto nomem;

		for (;;) {
			if (signal_pending(current))
				goto interrupted;

			/* ask userspace (returns NULL if it waited on a key
			 * being constructed) */
			key = request_key_construction(type, description,
						       callout_info, aux,
						       user, flags);
			if (key)
				break;

			/* someone else made the key we want, so we need to
			 * search again as it might now be available to us */
			key_ref = search_process_keyrings(type, description,
							  type->match,
							  current);

			kdebug("search 2: %p", key_ref);

			if (!IS_ERR(key_ref)) {
				key = key_ref_to_ptr(key_ref);
				break;
			}

			if (PTR_ERR(key_ref) != -EAGAIN) {
				key = ERR_PTR(PTR_ERR(key_ref));
				break;
			}
		}

		key_user_put(user);

		/* link the new key into the appropriate keyring */
		if (!IS_ERR(key))
			request_key_link(key, dest_keyring);
	}

error:
	kleave(" = %p", key);
	return key;

nomem:
	key = ERR_PTR(-ENOMEM);
	goto error;

interrupted:
	key_user_put(user);
	key = ERR_PTR(-EINTR);
	goto error;

} /* end request_key_and_link() */

/*****************************************************************************/
/*
 * request a key
 * - search the process's keyrings
 * - check the list of keys being created or updated
 * - call out to userspace for a key if supplementary info was provided
 */
struct key *request_key(struct key_type *type,
			const char *description,
			const char *callout_info)
{
	return request_key_and_link(type, description, callout_info, NULL,
				    NULL, KEY_ALLOC_IN_QUOTA);

} /* end request_key() */

EXPORT_SYMBOL(request_key);

/*****************************************************************************/
/*
 * request a key with auxiliary data for the upcaller
 * - search the process's keyrings
 * - check the list of keys being created or updated
 * - call out to userspace for a key if supplementary info was provided
 */
struct key *request_key_with_auxdata(struct key_type *type,
				     const char *description,
				     const char *callout_info,
				     void *aux)
{
	return request_key_and_link(type, description, callout_info, aux,
				    NULL, KEY_ALLOC_IN_QUOTA);

} /* end request_key_with_auxdata() */

EXPORT_SYMBOL(request_key_with_auxdata);
