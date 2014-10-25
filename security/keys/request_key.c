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
 * See Documentation/security/keys-request-key.txt
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kmod.h>
#include <linux/err.h>
#include <linux/keyctl.h>
#include <linux/slab.h>
#include "internal.h"

#define key_negative_timeout	60	/* default timeout on a negative key's existence */

/**
 * complete_request_key - Complete the construction of a key.
 * @cons: The key construction record.
 * @error: The success or failute of the construction.
 *
 * Complete the attempt to construct a key.  The key will be negated
 * if an error is indicated.  The authorisation key will be revoked
 * unconditionally.
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

/*
 * Initialise a usermode helper that is going to have a specific session
 * keyring.
 *
 * This is called in context of freshly forked kthread before kernel_execve(),
 * so we can simply install the desired session_keyring at this point.
 */
static int umh_keys_init(struct subprocess_info *info, struct cred *cred)
{
	struct key *keyring = info->data;

	return install_session_keyring_to_cred(cred, keyring);
}

/*
 * Clean up a usermode helper with session keyring.
 */
static void umh_keys_cleanup(struct subprocess_info *info)
{
	struct key *keyring = info->data;
	key_put(keyring);
}

/*
 * Call a usermode helper with a specific session keyring.
 */
static int call_usermodehelper_keys(char *path, char **argv, char **envp,
					struct key *session_keyring, int wait)
{
	struct subprocess_info *info;

	info = call_usermodehelper_setup(path, argv, envp, GFP_KERNEL,
					  umh_keys_init, umh_keys_cleanup,
					  session_keyring);
	if (!info)
		return -ENOMEM;

	key_get(session_keyring);
	return call_usermodehelper_exec(info, wait);
}

/*
 * Request userspace finish the construction of a key
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
				KEY_POS_ALL | KEY_USR_VIEW | KEY_USR_READ,
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
	sprintf(uid_str, "%d", from_kuid(&init_user_ns, cred->fsuid));
	sprintf(gid_str, "%d", from_kgid(&init_user_ns, cred->fsgid));

	/* we say which key is under construction */
	sprintf(key_str, "%d", key->serial);

	/* we specify the process's default keyrings */
	sprintf(keyring_str[0], "%d",
		cred->thread_keyring ? cred->thread_keyring->serial : 0);

	prkey = 0;
	if (cred->process_keyring)
		prkey = cred->process_keyring->serial;
	sprintf(keyring_str[1], "%d", prkey);

	rcu_read_lock();
	session = rcu_dereference(cred->session_keyring);
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
 * Call out to userspace for key construction.
 *
 * Program failure is ignored in favour of key status.
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
 * Get the appropriate destination keyring for the request.
 *
 * The keyring selected is returned with an extra reference upon it which the
 * caller must release.
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
			dest_keyring = key_get(cred->process_keyring);
			if (dest_keyring)
				break;

		case KEY_REQKEY_DEFL_SESSION_KEYRING:
			rcu_read_lock();
			dest_keyring = key_get(
				rcu_dereference(cred->session_keyring));
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
 * Allocate a new key in under-construction state and attempt to link it in to
 * the requested keyring.
 *
 * May return a key that's already under construction instead if there was a
 * race between two thread calling request_key().
 */
static int construct_alloc_key(struct keyring_search_context *ctx,
			       struct key *dest_keyring,
			       unsigned long flags,
			       struct key_user *user,
			       struct key **_key)
{
	struct assoc_array_edit *edit;
	struct key *key;
	key_perm_t perm;
	key_ref_t key_ref;
	int ret;

	kenter("%s,%s,,,",
	       ctx->index_key.type->name, ctx->index_key.description);

	*_key = NULL;
	mutex_lock(&user->cons_lock);

	perm = KEY_POS_VIEW | KEY_POS_SEARCH | KEY_POS_LINK | KEY_POS_SETATTR;
	perm |= KEY_USR_VIEW;
	if (ctx->index_key.type->read)
		perm |= KEY_POS_READ;
	if (ctx->index_key.type == &key_type_keyring ||
	    ctx->index_key.type->update)
		perm |= KEY_POS_WRITE;

	key = key_alloc(ctx->index_key.type, ctx->index_key.description,
			ctx->cred->fsuid, ctx->cred->fsgid, ctx->cred,
			perm, flags);
	if (IS_ERR(key))
		goto alloc_failed;

	set_bit(KEY_FLAG_USER_CONSTRUCT, &key->flags);

	if (dest_keyring) {
		ret = __key_link_begin(dest_keyring, &ctx->index_key, &edit);
		if (ret < 0)
			goto link_prealloc_failed;
	}

	/* attach the key to the destination keyring under lock, but we do need
	 * to do another check just in case someone beat us to it whilst we
	 * waited for locks */
	mutex_lock(&key_construction_mutex);

	key_ref = search_process_keyrings(ctx);
	if (!IS_ERR(key_ref))
		goto key_already_present;

	if (dest_keyring)
		__key_link(key, &edit);

	mutex_unlock(&key_construction_mutex);
	if (dest_keyring)
		__key_link_end(dest_keyring, &ctx->index_key, edit);
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
			__key_link(key, &edit);
		__key_link_end(dest_keyring, &ctx->index_key, edit);
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
	mutex_unlock(&user->cons_lock);
	kleave(" = %d [prelink]", ret);
	return ret;

alloc_failed:
	mutex_unlock(&user->cons_lock);
	kleave(" = %ld", PTR_ERR(key));
	return PTR_ERR(key);
}

/*
 * Commence key construction.
 */
static struct key *construct_key_and_link(struct keyring_search_context *ctx,
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

	user = key_user_lookup(current_fsuid());
	if (!user)
		return ERR_PTR(-ENOMEM);

	construct_get_dest_keyring(&dest_keyring);

	ret = construct_alloc_key(ctx, dest_keyring, flags, user, &key);
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
		goto couldnt_alloc_key;
	}

	key_put(dest_keyring);
	kleave(" = key %d", key_serial(key));
	return key;

construction_failed:
	key_negate_and_link(key, key_negative_timeout, NULL, NULL);
	key_put(key);
couldnt_alloc_key:
	key_put(dest_keyring);
	kleave(" = %d", ret);
	return ERR_PTR(ret);
}

/**
 * request_key_and_link - Request a key and cache it in a keyring.
 * @type: The type of key we want.
 * @description: The searchable description of the key.
 * @callout_info: The data to pass to the instantiation upcall (or NULL).
 * @callout_len: The length of callout_info.
 * @aux: Auxiliary data for the upcall.
 * @dest_keyring: Where to cache the key.
 * @flags: Flags to key_alloc().
 *
 * A key matching the specified criteria is searched for in the process's
 * keyrings and returned with its usage count incremented if found.  Otherwise,
 * if callout_info is not NULL, a key will be allocated and some service
 * (probably in userspace) will be asked to instantiate it.
 *
 * If successfully found or created, the key will be linked to the destination
 * keyring if one is provided.
 *
 * Returns a pointer to the key if successful; -EACCES, -ENOKEY, -EKEYREVOKED
 * or -EKEYEXPIRED if an inaccessible, negative, revoked or expired key was
 * found; -ENOKEY if no key was found and no @callout_info was given; -EDQUOT
 * if insufficient key quota was available to create a new key; or -ENOMEM if
 * insufficient memory was available.
 *
 * If the returned key was created, then it may still be under construction,
 * and wait_for_key_construction() should be used to wait for that to complete.
 */
struct key *request_key_and_link(struct key_type *type,
				 const char *description,
				 const void *callout_info,
				 size_t callout_len,
				 void *aux,
				 struct key *dest_keyring,
				 unsigned long flags)
{
	struct keyring_search_context ctx = {
		.index_key.type		= type,
		.index_key.description	= description,
		.cred			= current_cred(),
		.match_data.cmp		= key_default_cmp,
		.match_data.raw_data	= description,
		.match_data.lookup_type	= KEYRING_SEARCH_LOOKUP_DIRECT,
	};
	struct key *key;
	key_ref_t key_ref;
	int ret;

	kenter("%s,%s,%p,%zu,%p,%p,%lx",
	       ctx.index_key.type->name, ctx.index_key.description,
	       callout_info, callout_len, aux, dest_keyring, flags);

	if (type->match_preparse) {
		ret = type->match_preparse(&ctx.match_data);
		if (ret < 0) {
			key = ERR_PTR(ret);
			goto error;
		}
	}

	/* search all the process keyrings for a key */
	key_ref = search_process_keyrings(&ctx);

	if (!IS_ERR(key_ref)) {
		key = key_ref_to_ptr(key_ref);
		if (dest_keyring) {
			construct_get_dest_keyring(&dest_keyring);
			ret = key_link(dest_keyring, key);
			key_put(dest_keyring);
			if (ret < 0) {
				key_put(key);
				key = ERR_PTR(ret);
				goto error_free;
			}
		}
	} else if (PTR_ERR(key_ref) != -EAGAIN) {
		key = ERR_CAST(key_ref);
	} else  {
		/* the search failed, but the keyrings were searchable, so we
		 * should consult userspace if we can */
		key = ERR_PTR(-ENOKEY);
		if (!callout_info)
			goto error_free;

		key = construct_key_and_link(&ctx, callout_info, callout_len,
					     aux, dest_keyring, flags);
	}

error_free:
	if (type->match_free)
		type->match_free(&ctx.match_data);
error:
	kleave(" = %p", key);
	return key;
}

/**
 * wait_for_key_construction - Wait for construction of a key to complete
 * @key: The key being waited for.
 * @intr: Whether to wait interruptibly.
 *
 * Wait for a key to finish being constructed.
 *
 * Returns 0 if successful; -ERESTARTSYS if the wait was interrupted; -ENOKEY
 * if the key was negated; or -EKEYREVOKED or -EKEYEXPIRED if the key was
 * revoked or expired.
 */
int wait_for_key_construction(struct key *key, bool intr)
{
	int ret;

	ret = wait_on_bit(&key->flags, KEY_FLAG_USER_CONSTRUCT,
			  intr ? TASK_INTERRUPTIBLE : TASK_UNINTERRUPTIBLE);
	if (ret)
		return -ERESTARTSYS;
	if (test_bit(KEY_FLAG_NEGATIVE, &key->flags)) {
		smp_rmb();
		return key->type_data.reject_error;
	}
	return key_validate(key);
}
EXPORT_SYMBOL(wait_for_key_construction);

/**
 * request_key - Request a key and wait for construction
 * @type: Type of key.
 * @description: The searchable description of the key.
 * @callout_info: The data to pass to the instantiation upcall (or NULL).
 *
 * As for request_key_and_link() except that it does not add the returned key
 * to a keyring if found, new keys are always allocated in the user's quota,
 * the callout_info must be a NUL-terminated string and no auxiliary data can
 * be passed.
 *
 * Furthermore, it then works as wait_for_key_construction() to wait for the
 * completion of keys undergoing construction with a non-interruptible wait.
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

/**
 * request_key_with_auxdata - Request a key with auxiliary data for the upcaller
 * @type: The type of key we want.
 * @description: The searchable description of the key.
 * @callout_info: The data to pass to the instantiation upcall (or NULL).
 * @callout_len: The length of callout_info.
 * @aux: Auxiliary data for the upcall.
 *
 * As for request_key_and_link() except that it does not add the returned key
 * to a keyring if found and new keys are always allocated in the user's quota.
 *
 * Furthermore, it then works as wait_for_key_construction() to wait for the
 * completion of keys undergoing construction with a non-interruptible wait.
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
 * request_key_async - Request a key (allow async construction)
 * @type: Type of key.
 * @description: The searchable description of the key.
 * @callout_info: The data to pass to the instantiation upcall (or NULL).
 * @callout_len: The length of callout_info.
 *
 * As for request_key_and_link() except that it does not add the returned key
 * to a keyring if found, new keys are always allocated in the user's quota and
 * no auxiliary data can be passed.
 *
 * The caller should call wait_for_key_construction() to wait for the
 * completion of the returned key if it is still undergoing construction.
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
 * @type: Type of key.
 * @description: The searchable description of the key.
 * @callout_info: The data to pass to the instantiation upcall (or NULL).
 * @callout_len: The length of callout_info.
 * @aux: Auxiliary data for the upcall.
 *
 * As for request_key_and_link() except that it does not add the returned key
 * to a keyring if found and new keys are always allocated in the user's quota.
 *
 * The caller should call wait_for_key_construction() to wait for the
 * completion of the returned key if it is still undergoing construction.
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
