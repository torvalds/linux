/* Request a key from userspace
 *
 * Copyright (C) 2004-2007 Red Hat, Inc. All Rights Reserved.
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
#include <linux/slab.h>
#include "internal.h"

#define key_negative_timeout	60	/* default timeout on a negative key's existence */

/*
 * wait_on_bit() sleep function for uninterruptible waiting
 */
static int key_wait_bit(void *flags)
{
	schedule();
	return 0;
}

/*
 * wait_on_bit() sleep function for interruptible waiting
 */
static int key_wait_bit_intr(void *flags)
{
	schedule();
	return signal_pending(current) ? -ERESTARTSYS : 0;
}

/*
 * call to complete the construction of a key
 */
void complete_request_key(struct key_construction *cons, int error)
{
	kenter("{%d,%d},%d", cons->key->serial, cons->authkey->serial, error);

	if (error < 0)
		key_negate_and_link(cons->key, key_negative_timeout, NULL,
				    cons->authkey);
	else
		key_revoke(cons->authkey);

	key_put(cons->key);
	key_put(cons->authkey);
	kfree(cons);
}
EXPORT_SYMBOL(complete_request_key);

static int umh_keys_init(struct subprocess_info *info)
{
	struct cred *cred = (struct cred*)current_cred();
	struct key *keyring = info->data;
	/*
	 * This is called in context of freshly forked kthread before
	 * kernel_execve(), we can just change our ->session_keyring.
	 */
	return install_session_keyring_to_cred(cred, keyring);
}

static void umh_keys_cleanup(struct subprocess_info *info)
{
	struct key *keyring = info->data;
	key_put(keyring);
}

static int call_usermodehelper_keys(char *path, char **argv, char **envp,
			 struct key *session_keyring, enum umh_wait wait)
{
	gfp_t gfp_mask = (wait == UMH_NO_WAIT) ? GFP_ATOMIC : GFP_KERNEL;
	struct subprocess_info *info =
		call_usermodehelper_setup(path, argv, envp, gfp_mask);

	if (!info)
		return -ENOMEM;

	call_usermodehelper_setfns(info, umh_keys_init, umh_keys_cleanup,
					key_get(session_keyring));
	return call_usermodehelper_exec(info, wait);
}

/*
 * request userspace finish the construction of a key
 * - execute "/sbin/request-key <op> <key> <uid> <gid> <keyring> <keyring> <keyring>"
 */
static int call_sbin_request_key(struct key_construction *cons,
				 const char *op,
				 void *aux)
{
	const struct cred *cred = current_cred();
	key_serial_t prkey, sskey;
	struct key *key = cons->key, *authkey = cons->authkey, *keyring,
		*session;
	char *argv[9], *envp[3], uid_str[12], gid_str[12];
	char key_str[12], keyring_str[3][12];
	char desc[20];
	int ret, i;

	kenter("{%d},{%d},%s", key->serial, authkey->serial, op);

	ret = install_user_keyrings();
	if (ret < 0)
		goto error_alloc;

	/* allocate a new session keyring */
	sprintf(desc, "_req.%u", key->serial);

	cred = get_current_cred();
	keyring = keyring_alloc(desc, cred->fsuid, cred->fsgid, cred,
				KEY_ALLOC_QUOTA_OVERRUN, NULL);
	put_cred(cred);
	if (IS_ERR(keyring)) {
		ret = PTR_ERR(keyring);
		goto error_alloc;
	}

	/* attach the auth key to the session keyring */
	ret = key_link(keyring, authkey);
	if (ret < 0)
		goto error_link;

	/* record the UID and GID */
	sprintf(uid_str, "%d", cred->fsuid);
	sprintf(gid_str, "%d", cred->fsgid);

	/* we say which key is under construction */
	sprintf(key_str, "%d", key->serial);

	/* we specify the process's default keyrings */
	sprintf(keyring_str[0], "%d",
		cred->thread_keyring ? cred->thread_keyring->serial : 0);

	prkey = 0;
	if (cred->tgcred->process_keyring)
		prkey = cred->tgcred->process_keyring->serial;

	rcu_read_lock();
	session = rcu_dereference(cred->tgcred->session_keyring);
	if (!session)
		session = cred->user->session_keyring;
	sskey = session->serial;
	rcu_read_unlock();

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
	ret = call_usermodehelper_keys(argv[0], argv, envp, keyring,
				       UMH_WAIT_PROC);
	kdebug("usermode -> 0x%x", ret);
	if (ret >= 0) {
		/* ret is the exit/wait code */
		if (test_bit(KEY_FLAG_USER_CONSTRUCT, &key->flags) ||
		    key_validate(key) < 0)
			ret = -ENOKEY;
		else
			/* ignore any errors from userspace if the key was
			 * instantiated */
			ret = 0;
	}

error_link:
	key_put(keyring);

error_alloc:
	complete_request_key(cons, ret);
	kleave(" = %d", ret);
	return ret;
}

/*
 * call out to userspace for key construction
 * - we ignore program failure and go on key status instead
 */
static int construct_key(struct key *key, const void *callout_info,
			 size_t callout_len, void *aux,
			 struct key *dest_keyring)
{
	struct key_construction *cons;
	request_key_actor_t actor;
	struct key *authkey;
	int ret;

	kenter("%d,%p,%zu,%p", key->serial, callout_info, callout_len, aux);

	cons = kmalloc(sizeof(*cons), GFP_KERNEL);
	if (!cons)
		return -ENOMEM;

	/* allocate an authorisation key */
	authkey = request_key_auth_new(key, callout_info, callout_len,
				       dest_keyring);
	if (IS_ERR(authkey)) {
		kfree(cons);
		ret = PTR_ERR(authkey);
		authkey = NULL;
	} else {
		cons->authkey = key_get(authkey);
		cons->key = key_get(key);

		/* make the call */
		actor = call_sbin_request_key;
		if (key->type->request_key)
			actor = key->type->request_key;

		ret = actor(cons, "create", aux);

		/* check that the actor called complete_request_key() prior to
		 * returning an error */
		WARN_ON(ret < 0 &&
			!test_bit(KEY_FLAG_REVOKED, &authkey->flags));
		key_put(authkey);
	}

	kleave(" = %d", ret);
	return ret;
}

/*
 * get the appropriate destination keyring for the request
 * - we return whatever keyring we select with an extra reference upon it which
 *   the caller must release
 */
static void construct_get_dest_keyring(struct key **_dest_keyring)
{
	struct request_key_auth *rka;
	const struct cred *cred = current_cred();
	struct key *dest_keyring = *_dest_keyring, *authkey;

	kenter("%p", dest_keyring);

	/* find the appropriate keyring */
	if (dest_keyring) {
		/* the caller supplied one */
		key_get(dest_keyring);
	} else {
		/* use a default keyring; falling through the cases until we
		 * find one that we actually have */
		switch (cred->jit_keyring) {
		case KEY_REQKEY_DEFL_DEFAULT:
		case KEY_REQKEY_DEFL_REQUESTOR_KEYRING:
			if (cred->request_key_auth) {
				authkey = cred->request_key_auth;
				down_read(&authkey->sem);
				rka = authkey->payload.data;
				if (!test_bit(KEY_FLAG_REVOKED,
					      &authkey->flags))
					dest_keyring =
						key_get(rka->dest_keyring);
				up_read(&authkey->sem);
				if (dest_keyring)
					break;
			}

		case KEY_REQKEY_DEFL_THREAD_KEYRING:
			dest_keyring = key_get(cred->thread_keyring);
			if (dest_keyring)
				break;

		case KEY_REQKEY_DEFL_PROCESS_KEYRING:
			dest_keyring = key_get(cred->tgcred->process_keyring);
			if (dest_keyring)
				break;

		case KEY_REQKEY_DEFL_SESSION_KEYRING:
			rcu_read_lock();
			dest_keyring = key_get(
				rcu_dereference(cred->tgcred->session_keyring));
			rcu_read_unlock();

			if (dest_keyring)
				break;

		case KEY_REQKEY_DEFL_USER_SESSION_KEYRING:
			dest_keyring =
				key_get(cred->user->session_keyring);
			break;

		case KEY_REQKEY_DEFL_USER_KEYRING:
			dest_keyring = key_get(cred->user->uid_keyring);
			break;

		case KEY_REQKEY_DEFL_GROUP_KEYRING:
		default:
			BUG();
		}
	}

	*_dest_keyring = dest_keyring;
	kleave(" [dk %d]", key_serial(dest_keyring));
	return;
}

/*
 * allocate a new key in under-construction state and attempt to link it in to
 * the requested place
 * - may return a key that's already under construction instead
 */
static int construct_alloc_key(struct key_type *type,
			       const char *description,
			       struct key *dest_keyring,
			       unsigned long flags,
			       struct key_user *user,
			       struct key **_key)
{
	struct keyring_list *prealloc;
	const struct cred *cred = current_cred();
	struct key *key;
	key_ref_t key_ref;
	int ret;

	kenter("%s,%s,,,", type->name, description);

	*_key = NULL;
	mutex_lock(&user->cons_lock);

	key = key_alloc(type, description, cred->fsuid, cred->fsgid, cred,
			KEY_POS_ALL, flags);
	if (IS_ERR(key))
		goto alloc_failed;

	set_bit(KEY_FLAG_USER_CONSTRUCT, &key->flags);

	if (dest_keyring) {
		ret = __key_link_begin(dest_keyring, type, description,
				       &prealloc);
		if (ret < 0)
			goto link_prealloc_failed;
	}

	/* attach the key to the destination keyring under lock, but we do need
	 * to do another check just in case someone beat us to it whilst we
	 * waited for locks */
	mutex_lock(&key_construction_mutex);

	key_ref = search_process_keyrings(type, description, type->match, cred);
	if (!IS_ERR(key_ref))
		goto key_already_present;

	if (dest_keyring)
		__key_link(dest_keyring, key, &prealloc);

	mutex_unlock(&key_construction_mutex);
	if (dest_keyring)
		__key_link_end(dest_keyring, type, prealloc);
	mutex_unlock(&user->cons_lock);
	*_key = key;
	kleave(" = 0 [%d]", key_serial(key));
	return 0;

	/* the key is now present - we tell the caller that we found it by
	 * returning -EINPROGRESS  */
key_already_present:
	key_put(key);
	mutex_unlock(&key_construction_mutex);
	key = key_ref_to_ptr(key_ref);
	if (dest_keyring) {
		ret = __key_link_check_live_key(dest_keyring, key);
		if (ret == 0)
			__key_link(dest_keyring, key, &prealloc);
		__key_link_end(dest_keyring, type, prealloc);
		if (ret < 0)
			goto link_check_failed;
	}
	mutex_unlock(&user->cons_lock);
	*_key = key;
	kleave(" = -EINPROGRESS [%d]", key_serial(key));
	return -EINPROGRESS;

link_check_failed:
	mutex_unlock(&user->cons_lock);
	key_put(key);
	kleave(" = %d [linkcheck]", ret);
	return ret;

link_prealloc_failed:
	up_write(&dest_keyring->sem);
	mutex_unlock(&user->cons_lock);
	kleave(" = %d [prelink]", ret);
	return ret;

alloc_failed:
	mutex_unlock(&user->cons_lock);
	kleave(" = %ld", PTR_ERR(key));
	return PTR_ERR(key);
}

/*
 * commence key construction
 */
static struct key *construct_key_and_link(struct key_type *type,
					  const char *description,
					  const char *callout_info,
					  size_t callout_len,
					  void *aux,
					  struct key *dest_keyring,
					  unsigned long flags)
{
	struct key_user *user;
	struct key *key;
	int ret;

	kenter("");

	user = key_user_lookup(current_fsuid(), current_user_ns());
	if (!user)
		return ERR_PTR(-ENOMEM);

	construct_get_dest_keyring(&dest_keyring);

	ret = construct_alloc_key(type, description, dest_keyring, flags, user,
				  &key);
	key_user_put(user);

	if (ret == 0) {
		ret = construct_key(key, callout_info, callout_len, aux,
				    dest_keyring);
		if (ret < 0) {
			kdebug("cons failed");
			goto construction_failed;
		}
	} else if (ret == -EINPROGRESS) {
		ret = 0;
	} else {
		key = ERR_PTR(ret);
	}

	key_put(dest_keyring);
	kleave(" = key %d", key_serial(key));
	return key;

construction_failed:
	key_negate_and_link(key, key_negative_timeout, NULL, NULL);
	key_put(key);
	key_put(dest_keyring);
	kleave(" = %d", ret);
	return ERR_PTR(ret);
}

/*
 * request a key
 * - search the process's keyrings
 * - check the list of keys being created or updated
 * - call out to userspace for a key if supplementary info was provided
 * - cache the key in an appropriate keyring
 */
struct key *request_key_and_link(struct key_type *type,
				 const char *description,
				 const void *callout_info,
				 size_t callout_len,
				 void *aux,
				 struct key *dest_keyring,
				 unsigned long flags)
{
	const struct cred *cred = current_cred();
	struct key *key;
	key_ref_t key_ref;
	int ret;

	kenter("%s,%s,%p,%zu,%p,%p,%lx",
	       type->name, description, callout_info, callout_len, aux,
	       dest_keyring, flags);

	/* search all the process keyrings for a key */
	key_ref = search_process_keyrings(type, description, type->match,
					  cred);

	if (!IS_ERR(key_ref)) {
		key = key_ref_to_ptr(key_ref);
		if (dest_keyring) {
			construct_get_dest_keyring(&dest_keyring);
			ret = key_link(dest_keyring, key);
			key_put(dest_keyring);
			if (ret < 0) {
				key_put(key);
				key = ERR_PTR(ret);
				goto error;
			}
		}
	} else if (PTR_ERR(key_ref) != -EAGAIN) {
		key = ERR_CAST(key_ref);
	} else  {
		/* the search failed, but the keyrings were searchable, so we
		 * should consult userspace if we can */
		key = ERR_PTR(-ENOKEY);
		if (!callout_info)
			goto error;

		key = construct_key_and_link(type, description, callout_info,
					     callout_len, aux, dest_keyring,
					     flags);
	}

error:
	kleave(" = %p", key);
	return key;
}

/*
 * wait for construction of a key to complete
 */
int wait_for_key_construction(struct key *key, bool intr)
{
	int ret;

	ret = wait_on_bit(&key->flags, KEY_FLAG_USER_CONSTRUCT,
			  intr ? key_wait_bit_intr : key_wait_bit,
			  intr ? TASK_INTERRUPTIBLE : TASK_UNINTERRUPTIBLE);
	if (ret < 0)
		return ret;
	return key_validate(key);
}
EXPORT_SYMBOL(wait_for_key_construction);

/*
 * request a key
 * - search the process's keyrings
 * - check the list of keys being created or updated
 * - call out to userspace for a key if supplementary info was provided
 * - waits uninterruptible for creation to complete
 */
struct key *request_key(struct key_type *type,
			const char *description,
			const char *callout_info)
{
	struct key *key;
	size_t callout_len = 0;
	int ret;

	if (callout_info)
		callout_len = strlen(callout_info);
	key = request_key_and_link(type, description, callout_info, callout_len,
				   NULL, NULL, KEY_ALLOC_IN_QUOTA);
	if (!IS_ERR(key)) {
		ret = wait_for_key_construction(key, false);
		if (ret < 0) {
			key_put(key);
			return ERR_PTR(ret);
		}
	}
	return key;
}
EXPORT_SYMBOL(request_key);

/*
 * request a key with auxiliary data for the upcaller
 * - search the process's keyrings
 * - check the list of keys being created or updated
 * - call out to userspace for a key if supplementary info was provided
 * - waits uninterruptible for creation to complete
 */
struct key *request_key_with_auxdata(struct key_type *type,
				     const char *description,
				     const void *callout_info,
				     size_t callout_len,
				     void *aux)
{
	struct key *key;
	int ret;

	key = request_key_and_link(type, description, callout_info, callout_len,
				   aux, NULL, KEY_ALLOC_IN_QUOTA);
	if (!IS_ERR(key)) {
		ret = wait_for_key_construction(key, false);
		if (ret < 0) {
			key_put(key);
			return ERR_PTR(ret);
		}
	}
	return key;
}
EXPORT_SYMBOL(request_key_with_auxdata);

/*
 * request a key (allow async construction)
 * - search the process's keyrings
 * - check the list of keys being created or updated
 * - call out to userspace for a key if supplementary info was provided
 */
struct key *request_key_async(struct key_type *type,
			      const char *description,
			      const void *callout_info,
			      size_t callout_len)
{
	return request_key_and_link(type, description, callout_info,
				    callout_len, NULL, NULL,
				    KEY_ALLOC_IN_QUOTA);
}
EXPORT_SYMBOL(request_key_async);

/*
 * request a key with auxiliary data for the upcaller (allow async construction)
 * - search the process's keyrings
 * - check the list of keys being created or updated
 * - call out to userspace for a key if supplementary info was provided
 */
struct key *request_key_async_with_auxdata(struct key_type *type,
					   const char *description,
					   const void *callout_info,
					   size_t callout_len,
					   void *aux)
{
	return request_key_and_link(type, description, callout_info,
				    callout_len, aux, NULL, KEY_ALLOC_IN_QUOTA);
}
EXPORT_SYMBOL(request_key_async_with_auxdata);
