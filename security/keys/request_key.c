/* request_key.c: request a key from userspace
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kmod.h>
#include <linux/err.h>
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
 * - execute "/sbin/request-key <op> <key> <uid> <gid> <keyring> <keyring> <keyring> <info>"
 * - if callout_info is an empty string, it'll be rendered as a "-" instead
 */
static int call_request_key(struct key *key,
			    const char *op,
			    const char *callout_info)
{
	struct task_struct *tsk = current;
	unsigned long flags;
	key_serial_t prkey, sskey;
	char *argv[10], *envp[3], uid_str[12], gid_str[12];
	char key_str[12], keyring_str[3][12];
	int i;

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

	sskey = 0;
	spin_lock_irqsave(&tsk->sighand->siglock, flags);
	if (tsk->signal->session_keyring)
		sskey = tsk->signal->session_keyring->serial;
	spin_unlock_irqrestore(&tsk->sighand->siglock, flags);


	if (!sskey)
		sskey = tsk->user->session_keyring->serial;

	sprintf(keyring_str[1], "%d", prkey);
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
	argv[i++] = callout_info[0] ? (char *) callout_info : "-";
	argv[i] = NULL;

	/* do it */
	return call_usermodehelper(argv[0], argv, envp, 1);

} /* end call_request_key() */

/*****************************************************************************/
/*
 * call out to userspace for the key
 * - called with the construction sem held, but the sem is dropped here
 * - we ignore program failure and go on key status instead
 */
static struct key *__request_key_construction(struct key_type *type,
					      const char *description,
					      const char *callout_info)
{
	struct key_construction cons;
	struct timespec now;
	struct key *key;
	int ret, negative;

	/* create a key and add it to the queue */
	key = key_alloc(type, description,
			current->fsuid, current->fsgid, KEY_USR_ALL, 0);
	if (IS_ERR(key))
		goto alloc_failed;

	write_lock(&key->lock);
	key->flags |= KEY_FLAG_USER_CONSTRUCT;
	write_unlock(&key->lock);

	cons.key = key;
	list_add_tail(&cons.link, &key->user->consq);

	/* we drop the construction sem here on behalf of the caller */
	up_write(&key_construction_sem);

	/* make the call */
	ret = call_request_key(key, "create", callout_info);
	if (ret < 0)
		goto request_failed;

	/* if the key wasn't instantiated, then we want to give an error */
	ret = -ENOKEY;
	if (!(key->flags & KEY_FLAG_INSTANTIATED))
		goto request_failed;

	down_write(&key_construction_sem);
	list_del(&cons.link);
	up_write(&key_construction_sem);

	/* also give an error if the key was negatively instantiated */
 check_not_negative:
	if (key->flags & KEY_FLAG_NEGATIVE) {
		key_put(key);
		key = ERR_PTR(-ENOKEY);
	}

 out:
	return key;

 request_failed:
	/* it wasn't instantiated
	 * - remove from construction queue
	 * - mark the key as dead
	 */
	negative = 0;
	down_write(&key_construction_sem);

	list_del(&cons.link);

	write_lock(&key->lock);
	key->flags &= ~KEY_FLAG_USER_CONSTRUCT;

	/* check it didn't get instantiated between the check and the down */
	if (!(key->flags & KEY_FLAG_INSTANTIATED)) {
		key->flags |= KEY_FLAG_INSTANTIATED | KEY_FLAG_NEGATIVE;
		negative = 1;
	}

	write_unlock(&key->lock);
	up_write(&key_construction_sem);

	if (!negative)
		goto check_not_negative; /* surprisingly, the key got
					  * instantiated */

	/* set the timeout and store in the session keyring if we can */
	now = current_kernel_time();
	key->expiry = now.tv_sec + key_negative_timeout;

	if (current->signal->session_keyring) {
		unsigned long flags;
		struct key *keyring;

		spin_lock_irqsave(&current->sighand->siglock, flags);
		keyring = current->signal->session_keyring;
		atomic_inc(&keyring->usage);
		spin_unlock_irqrestore(&current->sighand->siglock, flags);

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
					    struct key_user *user,
					    const char *callout_info)
{
	struct key_construction *pcons;
	struct key *key, *ckey;

	DECLARE_WAITQUEUE(myself, current);

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
	key = __request_key_construction(type, description, callout_info);
 error:
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
		set_current_state(TASK_UNINTERRUPTIBLE);
		if (!(ckey->flags & KEY_FLAG_USER_CONSTRUCT))
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
 * request a key
 * - search the process's keyrings
 * - check the list of keys being created or updated
 * - call out to userspace for a key if requested (supplementary info can be
 *   passed)
 */
struct key *request_key(struct key_type *type,
			const char *description,
			const char *callout_info)
{
	struct key_user *user;
	struct key *key;

	/* search all the process keyrings for a key */
	key = search_process_keyrings_aux(type, description, type->match);

	if (PTR_ERR(key) == -EAGAIN) {
		/* the search failed, but the keyrings were searchable, so we
		 * should consult userspace if we can */
		key = ERR_PTR(-ENOKEY);
		if (!callout_info)
			goto error;

		/* - get hold of the user's construction queue */
		user = key_user_lookup(current->fsuid);
		if (!user) {
			key = ERR_PTR(-ENOMEM);
			goto error;
		}

		for (;;) {
			/* ask userspace (returns NULL if it waited on a key
			 * being constructed) */
			key = request_key_construction(type, description,
						       user, callout_info);
			if (key)
				break;

			/* someone else made the key we want, so we need to
			 * search again as it might now be available to us */
			key = search_process_keyrings_aux(type, description,
							  type->match);
			if (PTR_ERR(key) != -EAGAIN)
				break;
		}

		key_user_put(user);
	}

 error:
	return key;

} /* end request_key() */

EXPORT_SYMBOL(request_key);

/*****************************************************************************/
/*
 * validate a key
 */
int key_validate(struct key *key)
{
	struct timespec now;
	int ret = 0;

	if (key) {
		/* check it's still accessible */
		ret = -EKEYREVOKED;
		if (key->flags & (KEY_FLAG_REVOKED | KEY_FLAG_DEAD))
			goto error;

		/* check it hasn't expired */
		ret = 0;
		if (key->expiry) {
			now = current_kernel_time();
			if (now.tv_sec >= key->expiry)
				ret = -EKEYEXPIRED;
		}
	}

 error:
	return ret;

} /* end key_validate() */

EXPORT_SYMBOL(key_validate);
