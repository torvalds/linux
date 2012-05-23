/* Userspace key control operations
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
#include <linux/syscalls.h>
#include <linux/key.h>
#include <linux/keyctl.h>
#include <linux/fs.h>
#include <linux/capability.h>
#include <linux/string.h>
#include <linux/err.h>
#include <linux/vmalloc.h>
#include <linux/security.h>
#include <asm/uaccess.h>
#include "internal.h"

static int key_get_type_from_user(char *type,
				  const char __user *_type,
				  unsigned len)
{
	int ret;

	ret = strncpy_from_user(type, _type, len);
	if (ret < 0)
		return ret;
	if (ret == 0 || ret >= len)
		return -EINVAL;
	if (type[0] == '.')
		return -EPERM;
	type[len - 1] = '\0';
	return 0;
}

/*
 * Extract the description of a new key from userspace and either add it as a
 * new key to the specified keyring or update a matching key in that keyring.
 *
 * The keyring must be writable so that we can attach the key to it.
 *
 * If successful, the new key's serial number is returned, otherwise an error
 * code is returned.
 */
SYSCALL_DEFINE5(add_key, const char __user *, _type,
		const char __user *, _description,
		const void __user *, _payload,
		size_t, plen,
		key_serial_t, ringid)
{
	key_ref_t keyring_ref, key_ref;
	char type[32], *description;
	void *payload;
	long ret;
	bool vm;

	ret = -EINVAL;
	if (plen > 1024 * 1024 - 1)
		goto error;

	/* draw all the data into kernel space */
	ret = key_get_type_from_user(type, _type, sizeof(type));
	if (ret < 0)
		goto error;

	description = strndup_user(_description, PAGE_SIZE);
	if (IS_ERR(description)) {
		ret = PTR_ERR(description);
		goto error;
	}

	/* pull the payload in if one was supplied */
	payload = NULL;

	vm = false;
	if (_payload) {
		ret = -ENOMEM;
		payload = kmalloc(plen, GFP_KERNEL);
		if (!payload) {
			if (plen <= PAGE_SIZE)
				goto error2;
			vm = true;
			payload = vmalloc(plen);
			if (!payload)
				goto error2;
		}

		ret = -EFAULT;
		if (copy_from_user(payload, _payload, plen) != 0)
			goto error3;
	}

	/* find the target keyring (which must be writable) */
	keyring_ref = lookup_user_key(ringid, KEY_LOOKUP_CREATE, KEY_WRITE);
	if (IS_ERR(keyring_ref)) {
		ret = PTR_ERR(keyring_ref);
		goto error3;
	}

	/* create or update the requested key and add it to the target
	 * keyring */
	key_ref = key_create_or_update(keyring_ref, type, description,
				       payload, plen, KEY_PERM_UNDEF,
				       KEY_ALLOC_IN_QUOTA);
	if (!IS_ERR(key_ref)) {
		ret = key_ref_to_ptr(key_ref)->serial;
		key_ref_put(key_ref);
	}
	else {
		ret = PTR_ERR(key_ref);
	}

	key_ref_put(keyring_ref);
 error3:
	if (!vm)
		kfree(payload);
	else
		vfree(payload);
 error2:
	kfree(description);
 error:
	return ret;
}

/*
 * Search the process keyrings and keyring trees linked from those for a
 * matching key.  Keyrings must have appropriate Search permission to be
 * searched.
 *
 * If a key is found, it will be attached to the destination keyring if there's
 * one specified and the serial number of the key will be returned.
 *
 * If no key is found, /sbin/request-key will be invoked if _callout_info is
 * non-NULL in an attempt to create a key.  The _callout_info string will be
 * passed to /sbin/request-key to aid with completing the request.  If the
 * _callout_info string is "" then it will be changed to "-".
 */
SYSCALL_DEFINE4(request_key, const char __user *, _type,
		const char __user *, _description,
		const char __user *, _callout_info,
		key_serial_t, destringid)
{
	struct key_type *ktype;
	struct key *key;
	key_ref_t dest_ref;
	size_t callout_len;
	char type[32], *description, *callout_info;
	long ret;

	/* pull the type into kernel space */
	ret = key_get_type_from_user(type, _type, sizeof(type));
	if (ret < 0)
		goto error;

	/* pull the description into kernel space */
	description = strndup_user(_description, PAGE_SIZE);
	if (IS_ERR(description)) {
		ret = PTR_ERR(description);
		goto error;
	}

	/* pull the callout info into kernel space */
	callout_info = NULL;
	callout_len = 0;
	if (_callout_info) {
		callout_info = strndup_user(_callout_info, PAGE_SIZE);
		if (IS_ERR(callout_info)) {
			ret = PTR_ERR(callout_info);
			goto error2;
		}
		callout_len = strlen(callout_info);
	}

	/* get the destination keyring if specified */
	dest_ref = NULL;
	if (destringid) {
		dest_ref = lookup_user_key(destringid, KEY_LOOKUP_CREATE,
					   KEY_WRITE);
		if (IS_ERR(dest_ref)) {
			ret = PTR_ERR(dest_ref);
			goto error3;
		}
	}

	/* find the key type */
	ktype = key_type_lookup(type);
	if (IS_ERR(ktype)) {
		ret = PTR_ERR(ktype);
		goto error4;
	}

	/* do the search */
	key = request_key_and_link(ktype, description, callout_info,
				   callout_len, NULL, key_ref_to_ptr(dest_ref),
				   KEY_ALLOC_IN_QUOTA);
	if (IS_ERR(key)) {
		ret = PTR_ERR(key);
		goto error5;
	}

	/* wait for the key to finish being constructed */
	ret = wait_for_key_construction(key, 1);
	if (ret < 0)
		goto error6;

	ret = key->serial;

error6:
 	key_put(key);
error5:
	key_type_put(ktype);
error4:
	key_ref_put(dest_ref);
error3:
	kfree(callout_info);
error2:
	kfree(description);
error:
	return ret;
}

/*
 * Get the ID of the specified process keyring.
 *
 * The requested keyring must have search permission to be found.
 *
 * If successful, the ID of the requested keyring will be returned.
 */
long keyctl_get_keyring_ID(key_serial_t id, int create)
{
	key_ref_t key_ref;
	unsigned long lflags;
	long ret;

	lflags = create ? KEY_LOOKUP_CREATE : 0;
	key_ref = lookup_user_key(id, lflags, KEY_SEARCH);
	if (IS_ERR(key_ref)) {
		ret = PTR_ERR(key_ref);
		goto error;
	}

	ret = key_ref_to_ptr(key_ref)->serial;
	key_ref_put(key_ref);
error:
	return ret;
}

/*
 * Join a (named) session keyring.
 *
 * Create and join an anonymous session keyring or join a named session
 * keyring, creating it if necessary.  A named session keyring must have Search
 * permission for it to be joined.  Session keyrings without this permit will
 * be skipped over.
 *
 * If successful, the ID of the joined session keyring will be returned.
 */
long keyctl_join_session_keyring(const char __user *_name)
{
	char *name;
	long ret;

	/* fetch the name from userspace */
	name = NULL;
	if (_name) {
		name = strndup_user(_name, PAGE_SIZE);
		if (IS_ERR(name)) {
			ret = PTR_ERR(name);
			goto error;
		}
	}

	/* join the session */
	ret = join_session_keyring(name);
	kfree(name);

error:
	return ret;
}

/*
 * Update a key's data payload from the given data.
 *
 * The key must grant the caller Write permission and the key type must support
 * updating for this to work.  A negative key can be positively instantiated
 * with this call.
 *
 * If successful, 0 will be returned.  If the key type does not support
 * updating, then -EOPNOTSUPP will be returned.
 */
long keyctl_update_key(key_serial_t id,
		       const void __user *_payload,
		       size_t plen)
{
	key_ref_t key_ref;
	void *payload;
	long ret;

	ret = -EINVAL;
	if (plen > PAGE_SIZE)
		goto error;

	/* pull the payload in if one was supplied */
	payload = NULL;
	if (_payload) {
		ret = -ENOMEM;
		payload = kmalloc(plen, GFP_KERNEL);
		if (!payload)
			goto error;

		ret = -EFAULT;
		if (copy_from_user(payload, _payload, plen) != 0)
			goto error2;
	}

	/* find the target key (which must be writable) */
	key_ref = lookup_user_key(id, 0, KEY_WRITE);
	if (IS_ERR(key_ref)) {
		ret = PTR_ERR(key_ref);
		goto error2;
	}

	/* update the key */
	ret = key_update(key_ref, payload, plen);

	key_ref_put(key_ref);
error2:
	kfree(payload);
error:
	return ret;
}

/*
 * Revoke a key.
 *
 * The key must be grant the caller Write or Setattr permission for this to
 * work.  The key type should give up its quota claim when revoked.  The key
 * and any links to the key will be automatically garbage collected after a
 * certain amount of time (/proc/sys/kernel/keys/gc_delay).
 *
 * If successful, 0 is returned.
 */
long keyctl_revoke_key(key_serial_t id)
{
	key_ref_t key_ref;
	long ret;

	key_ref = lookup_user_key(id, 0, KEY_WRITE);
	if (IS_ERR(key_ref)) {
		ret = PTR_ERR(key_ref);
		if (ret != -EACCES)
			goto error;
		key_ref = lookup_user_key(id, 0, KEY_SETATTR);
		if (IS_ERR(key_ref)) {
			ret = PTR_ERR(key_ref);
			goto error;
		}
	}

	key_revoke(key_ref_to_ptr(key_ref));
	ret = 0;

	key_ref_put(key_ref);
error:
	return ret;
}

/*
 * Invalidate a key.
 *
 * The key must be grant the caller Invalidate permission for this to work.
 * The key and any links to the key will be automatically garbage collected
 * immediately.
 *
 * If successful, 0 is returned.
 */
long keyctl_invalidate_key(key_serial_t id)
{
	key_ref_t key_ref;
	long ret;

	kenter("%d", id);

	key_ref = lookup_user_key(id, 0, KEY_SEARCH);
	if (IS_ERR(key_ref)) {
		ret = PTR_ERR(key_ref);
		goto error;
	}

	key_invalidate(key_ref_to_ptr(key_ref));
	ret = 0;

	key_ref_put(key_ref);
error:
	kleave(" = %ld", ret);
	return ret;
}

/*
 * Clear the specified keyring, creating an empty process keyring if one of the
 * special keyring IDs is used.
 *
 * The keyring must grant the caller Write permission for this to work.  If
 * successful, 0 will be returned.
 */
long keyctl_keyring_clear(key_serial_t ringid)
{
	key_ref_t keyring_ref;
	long ret;

	keyring_ref = lookup_user_key(ringid, KEY_LOOKUP_CREATE, KEY_WRITE);
	if (IS_ERR(keyring_ref)) {
		ret = PTR_ERR(keyring_ref);

		/* Root is permitted to invalidate certain special keyrings */
		if (capable(CAP_SYS_ADMIN)) {
			keyring_ref = lookup_user_key(ringid, 0, 0);
			if (IS_ERR(keyring_ref))
				goto error;
			if (test_bit(KEY_FLAG_ROOT_CAN_CLEAR,
				     &key_ref_to_ptr(keyring_ref)->flags))
				goto clear;
			goto error_put;
		}

		goto error;
	}

clear:
	ret = keyring_clear(key_ref_to_ptr(keyring_ref));
error_put:
	key_ref_put(keyring_ref);
error:
	return ret;
}

/*
 * Create a link from a keyring to a key if there's no matching key in the
 * keyring, otherwise replace the link to the matching key with a link to the
 * new key.
 *
 * The key must grant the caller Link permission and the the keyring must grant
 * the caller Write permission.  Furthermore, if an additional link is created,
 * the keyring's quota will be extended.
 *
 * If successful, 0 will be returned.
 */
long keyctl_keyring_link(key_serial_t id, key_serial_t ringid)
{
	key_ref_t keyring_ref, key_ref;
	long ret;

	keyring_ref = lookup_user_key(ringid, KEY_LOOKUP_CREATE, KEY_WRITE);
	if (IS_ERR(keyring_ref)) {
		ret = PTR_ERR(keyring_ref);
		goto error;
	}

	key_ref = lookup_user_key(id, KEY_LOOKUP_CREATE, KEY_LINK);
	if (IS_ERR(key_ref)) {
		ret = PTR_ERR(key_ref);
		goto error2;
	}

	ret = key_link(key_ref_to_ptr(keyring_ref), key_ref_to_ptr(key_ref));

	key_ref_put(key_ref);
error2:
	key_ref_put(keyring_ref);
error:
	return ret;
}

/*
 * Unlink a key from a keyring.
 *
 * The keyring must grant the caller Write permission for this to work; the key
 * itself need not grant the caller anything.  If the last link to a key is
 * removed then that key will be scheduled for destruction.
 *
 * If successful, 0 will be returned.
 */
long keyctl_keyring_unlink(key_serial_t id, key_serial_t ringid)
{
	key_ref_t keyring_ref, key_ref;
	long ret;

	keyring_ref = lookup_user_key(ringid, 0, KEY_WRITE);
	if (IS_ERR(keyring_ref)) {
		ret = PTR_ERR(keyring_ref);
		goto error;
	}

	key_ref = lookup_user_key(id, KEY_LOOKUP_FOR_UNLINK, 0);
	if (IS_ERR(key_ref)) {
		ret = PTR_ERR(key_ref);
		goto error2;
	}

	ret = key_unlink(key_ref_to_ptr(keyring_ref), key_ref_to_ptr(key_ref));

	key_ref_put(key_ref);
error2:
	key_ref_put(keyring_ref);
error:
	return ret;
}

/*
 * Return a description of a key to userspace.
 *
 * The key must grant the caller View permission for this to work.
 *
 * If there's a buffer, we place up to buflen bytes of data into it formatted
 * in the following way:
 *
 *	type;uid;gid;perm;description<NUL>
 *
 * If successful, we return the amount of description available, irrespective
 * of how much we may have copied into the buffer.
 */
long keyctl_describe_key(key_serial_t keyid,
			 char __user *buffer,
			 size_t buflen)
{
	struct key *key, *instkey;
	key_ref_t key_ref;
	char *tmpbuf;
	long ret;

	key_ref = lookup_user_key(keyid, KEY_LOOKUP_PARTIAL, KEY_VIEW);
	if (IS_ERR(key_ref)) {
		/* viewing a key under construction is permitted if we have the
		 * authorisation token handy */
		if (PTR_ERR(key_ref) == -EACCES) {
			instkey = key_get_instantiation_authkey(keyid);
			if (!IS_ERR(instkey)) {
				key_put(instkey);
				key_ref = lookup_user_key(keyid,
							  KEY_LOOKUP_PARTIAL,
							  0);
				if (!IS_ERR(key_ref))
					goto okay;
			}
		}

		ret = PTR_ERR(key_ref);
		goto error;
	}

okay:
	/* calculate how much description we're going to return */
	ret = -ENOMEM;
	tmpbuf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!tmpbuf)
		goto error2;

	key = key_ref_to_ptr(key_ref);

	ret = snprintf(tmpbuf, PAGE_SIZE - 1,
		       "%s;%d;%d;%08x;%s",
		       key->type->name,
		       key->uid,
		       key->gid,
		       key->perm,
		       key->description ?: "");

	/* include a NUL char at the end of the data */
	if (ret > PAGE_SIZE - 1)
		ret = PAGE_SIZE - 1;
	tmpbuf[ret] = 0;
	ret++;

	/* consider returning the data */
	if (buffer && buflen > 0) {
		if (buflen > ret)
			buflen = ret;

		if (copy_to_user(buffer, tmpbuf, buflen) != 0)
			ret = -EFAULT;
	}

	kfree(tmpbuf);
error2:
	key_ref_put(key_ref);
error:
	return ret;
}

/*
 * Search the specified keyring and any keyrings it links to for a matching
 * key.  Only keyrings that grant the caller Search permission will be searched
 * (this includes the starting keyring).  Only keys with Search permission can
 * be found.
 *
 * If successful, the found key will be linked to the destination keyring if
 * supplied and the key has Link permission, and the found key ID will be
 * returned.
 */
long keyctl_keyring_search(key_serial_t ringid,
			   const char __user *_type,
			   const char __user *_description,
			   key_serial_t destringid)
{
	struct key_type *ktype;
	key_ref_t keyring_ref, key_ref, dest_ref;
	char type[32], *description;
	long ret;

	/* pull the type and description into kernel space */
	ret = key_get_type_from_user(type, _type, sizeof(type));
	if (ret < 0)
		goto error;

	description = strndup_user(_description, PAGE_SIZE);
	if (IS_ERR(description)) {
		ret = PTR_ERR(description);
		goto error;
	}

	/* get the keyring at which to begin the search */
	keyring_ref = lookup_user_key(ringid, 0, KEY_SEARCH);
	if (IS_ERR(keyring_ref)) {
		ret = PTR_ERR(keyring_ref);
		goto error2;
	}

	/* get the destination keyring if specified */
	dest_ref = NULL;
	if (destringid) {
		dest_ref = lookup_user_key(destringid, KEY_LOOKUP_CREATE,
					   KEY_WRITE);
		if (IS_ERR(dest_ref)) {
			ret = PTR_ERR(dest_ref);
			goto error3;
		}
	}

	/* find the key type */
	ktype = key_type_lookup(type);
	if (IS_ERR(ktype)) {
		ret = PTR_ERR(ktype);
		goto error4;
	}

	/* do the search */
	key_ref = keyring_search(keyring_ref, ktype, description);
	if (IS_ERR(key_ref)) {
		ret = PTR_ERR(key_ref);

		/* treat lack or presence of a negative key the same */
		if (ret == -EAGAIN)
			ret = -ENOKEY;
		goto error5;
	}

	/* link the resulting key to the destination keyring if we can */
	if (dest_ref) {
		ret = key_permission(key_ref, KEY_LINK);
		if (ret < 0)
			goto error6;

		ret = key_link(key_ref_to_ptr(dest_ref), key_ref_to_ptr(key_ref));
		if (ret < 0)
			goto error6;
	}

	ret = key_ref_to_ptr(key_ref)->serial;

error6:
	key_ref_put(key_ref);
error5:
	key_type_put(ktype);
error4:
	key_ref_put(dest_ref);
error3:
	key_ref_put(keyring_ref);
error2:
	kfree(description);
error:
	return ret;
}

/*
 * Read a key's payload.
 *
 * The key must either grant the caller Read permission, or it must grant the
 * caller Search permission when searched for from the process keyrings.
 *
 * If successful, we place up to buflen bytes of data into the buffer, if one
 * is provided, and return the amount of data that is available in the key,
 * irrespective of how much we copied into the buffer.
 */
long keyctl_read_key(key_serial_t keyid, char __user *buffer, size_t buflen)
{
	struct key *key;
	key_ref_t key_ref;
	long ret;

	/* find the key first */
	key_ref = lookup_user_key(keyid, 0, 0);
	if (IS_ERR(key_ref)) {
		ret = -ENOKEY;
		goto error;
	}

	key = key_ref_to_ptr(key_ref);

	/* see if we can read it directly */
	ret = key_permission(key_ref, KEY_READ);
	if (ret == 0)
		goto can_read_key;
	if (ret != -EACCES)
		goto error;

	/* we can't; see if it's searchable from this process's keyrings
	 * - we automatically take account of the fact that it may be
	 *   dangling off an instantiation key
	 */
	if (!is_key_possessed(key_ref)) {
		ret = -EACCES;
		goto error2;
	}

	/* the key is probably readable - now try to read it */
can_read_key:
	ret = key_validate(key);
	if (ret == 0) {
		ret = -EOPNOTSUPP;
		if (key->type->read) {
			/* read the data with the semaphore held (since we
			 * might sleep) */
			down_read(&key->sem);
			ret = key->type->read(key, buffer, buflen);
			up_read(&key->sem);
		}
	}

error2:
	key_put(key);
error:
	return ret;
}

/*
 * Change the ownership of a key
 *
 * The key must grant the caller Setattr permission for this to work, though
 * the key need not be fully instantiated yet.  For the UID to be changed, or
 * for the GID to be changed to a group the caller is not a member of, the
 * caller must have sysadmin capability.  If either uid or gid is -1 then that
 * attribute is not changed.
 *
 * If the UID is to be changed, the new user must have sufficient quota to
 * accept the key.  The quota deduction will be removed from the old user to
 * the new user should the attribute be changed.
 *
 * If successful, 0 will be returned.
 */
long keyctl_chown_key(key_serial_t id, uid_t uid, gid_t gid)
{
	struct key_user *newowner, *zapowner = NULL;
	struct key *key;
	key_ref_t key_ref;
	long ret;

	ret = 0;
	if (uid == (uid_t) -1 && gid == (gid_t) -1)
		goto error;

	key_ref = lookup_user_key(id, KEY_LOOKUP_CREATE | KEY_LOOKUP_PARTIAL,
				  KEY_SETATTR);
	if (IS_ERR(key_ref)) {
		ret = PTR_ERR(key_ref);
		goto error;
	}

	key = key_ref_to_ptr(key_ref);

	/* make the changes with the locks held to prevent chown/chown races */
	ret = -EACCES;
	down_write(&key->sem);

	if (!capable(CAP_SYS_ADMIN)) {
		/* only the sysadmin can chown a key to some other UID */
		if (uid != (uid_t) -1 && key->uid != uid)
			goto error_put;

		/* only the sysadmin can set the key's GID to a group other
		 * than one of those that the current process subscribes to */
		if (gid != (gid_t) -1 && gid != key->gid && !in_group_p(gid))
			goto error_put;
	}

	/* change the UID */
	if (uid != (uid_t) -1 && uid != key->uid) {
		ret = -ENOMEM;
		newowner = key_user_lookup(uid, current_user_ns());
		if (!newowner)
			goto error_put;

		/* transfer the quota burden to the new user */
		if (test_bit(KEY_FLAG_IN_QUOTA, &key->flags)) {
			unsigned maxkeys = (uid == 0) ?
				key_quota_root_maxkeys : key_quota_maxkeys;
			unsigned maxbytes = (uid == 0) ?
				key_quota_root_maxbytes : key_quota_maxbytes;

			spin_lock(&newowner->lock);
			if (newowner->qnkeys + 1 >= maxkeys ||
			    newowner->qnbytes + key->quotalen >= maxbytes ||
			    newowner->qnbytes + key->quotalen <
			    newowner->qnbytes)
				goto quota_overrun;

			newowner->qnkeys++;
			newowner->qnbytes += key->quotalen;
			spin_unlock(&newowner->lock);

			spin_lock(&key->user->lock);
			key->user->qnkeys--;
			key->user->qnbytes -= key->quotalen;
			spin_unlock(&key->user->lock);
		}

		atomic_dec(&key->user->nkeys);
		atomic_inc(&newowner->nkeys);

		if (test_bit(KEY_FLAG_INSTANTIATED, &key->flags)) {
			atomic_dec(&key->user->nikeys);
			atomic_inc(&newowner->nikeys);
		}

		zapowner = key->user;
		key->user = newowner;
		key->uid = uid;
	}

	/* change the GID */
	if (gid != (gid_t) -1)
		key->gid = gid;

	ret = 0;

error_put:
	up_write(&key->sem);
	key_put(key);
	if (zapowner)
		key_user_put(zapowner);
error:
	return ret;

quota_overrun:
	spin_unlock(&newowner->lock);
	zapowner = newowner;
	ret = -EDQUOT;
	goto error_put;
}

/*
 * Change the permission mask on a key.
 *
 * The key must grant the caller Setattr permission for this to work, though
 * the key need not be fully instantiated yet.  If the caller does not have
 * sysadmin capability, it may only change the permission on keys that it owns.
 */
long keyctl_setperm_key(key_serial_t id, key_perm_t perm)
{
	struct key *key;
	key_ref_t key_ref;
	long ret;

	ret = -EINVAL;
	if (perm & ~(KEY_POS_ALL | KEY_USR_ALL | KEY_GRP_ALL | KEY_OTH_ALL))
		goto error;

	key_ref = lookup_user_key(id, KEY_LOOKUP_CREATE | KEY_LOOKUP_PARTIAL,
				  KEY_SETATTR);
	if (IS_ERR(key_ref)) {
		ret = PTR_ERR(key_ref);
		goto error;
	}

	key = key_ref_to_ptr(key_ref);

	/* make the changes with the locks held to prevent chown/chmod races */
	ret = -EACCES;
	down_write(&key->sem);

	/* if we're not the sysadmin, we can only change a key that we own */
	if (capable(CAP_SYS_ADMIN) || key->uid == current_fsuid()) {
		key->perm = perm;
		ret = 0;
	}

	up_write(&key->sem);
	key_put(key);
error:
	return ret;
}

/*
 * Get the destination keyring for instantiation and check that the caller has
 * Write permission on it.
 */
static long get_instantiation_keyring(key_serial_t ringid,
				      struct request_key_auth *rka,
				      struct key **_dest_keyring)
{
	key_ref_t dkref;

	*_dest_keyring = NULL;

	/* just return a NULL pointer if we weren't asked to make a link */
	if (ringid == 0)
		return 0;

	/* if a specific keyring is nominated by ID, then use that */
	if (ringid > 0) {
		dkref = lookup_user_key(ringid, KEY_LOOKUP_CREATE, KEY_WRITE);
		if (IS_ERR(dkref))
			return PTR_ERR(dkref);
		*_dest_keyring = key_ref_to_ptr(dkref);
		return 0;
	}

	if (ringid == KEY_SPEC_REQKEY_AUTH_KEY)
		return -EINVAL;

	/* otherwise specify the destination keyring recorded in the
	 * authorisation key (any KEY_SPEC_*_KEYRING) */
	if (ringid >= KEY_SPEC_REQUESTOR_KEYRING) {
		*_dest_keyring = key_get(rka->dest_keyring);
		return 0;
	}

	return -ENOKEY;
}

/*
 * Change the request_key authorisation key on the current process.
 */
static int keyctl_change_reqkey_auth(struct key *key)
{
	struct cred *new;

	new = prepare_creds();
	if (!new)
		return -ENOMEM;

	key_put(new->request_key_auth);
	new->request_key_auth = key_get(key);

	return commit_creds(new);
}

/*
 * Copy the iovec data from userspace
 */
static long copy_from_user_iovec(void *buffer, const struct iovec *iov,
				 unsigned ioc)
{
	for (; ioc > 0; ioc--) {
		if (copy_from_user(buffer, iov->iov_base, iov->iov_len) != 0)
			return -EFAULT;
		buffer += iov->iov_len;
		iov++;
	}
	return 0;
}

/*
 * Instantiate a key with the specified payload and link the key into the
 * destination keyring if one is given.
 *
 * The caller must have the appropriate instantiation permit set for this to
 * work (see keyctl_assume_authority).  No other permissions are required.
 *
 * If successful, 0 will be returned.
 */
long keyctl_instantiate_key_common(key_serial_t id,
				   const struct iovec *payload_iov,
				   unsigned ioc,
				   size_t plen,
				   key_serial_t ringid)
{
	const struct cred *cred = current_cred();
	struct request_key_auth *rka;
	struct key *instkey, *dest_keyring;
	void *payload;
	long ret;
	bool vm = false;

	kenter("%d,,%zu,%d", id, plen, ringid);

	ret = -EINVAL;
	if (plen > 1024 * 1024 - 1)
		goto error;

	/* the appropriate instantiation authorisation key must have been
	 * assumed before calling this */
	ret = -EPERM;
	instkey = cred->request_key_auth;
	if (!instkey)
		goto error;

	rka = instkey->payload.data;
	if (rka->target_key->serial != id)
		goto error;

	/* pull the payload in if one was supplied */
	payload = NULL;

	if (payload_iov) {
		ret = -ENOMEM;
		payload = kmalloc(plen, GFP_KERNEL);
		if (!payload) {
			if (plen <= PAGE_SIZE)
				goto error;
			vm = true;
			payload = vmalloc(plen);
			if (!payload)
				goto error;
		}

		ret = copy_from_user_iovec(payload, payload_iov, ioc);
		if (ret < 0)
			goto error2;
	}

	/* find the destination keyring amongst those belonging to the
	 * requesting task */
	ret = get_instantiation_keyring(ringid, rka, &dest_keyring);
	if (ret < 0)
		goto error2;

	/* instantiate the key and link it into a keyring */
	ret = key_instantiate_and_link(rka->target_key, payload, plen,
				       dest_keyring, instkey);

	key_put(dest_keyring);

	/* discard the assumed authority if it's just been disabled by
	 * instantiation of the key */
	if (ret == 0)
		keyctl_change_reqkey_auth(NULL);

error2:
	if (!vm)
		kfree(payload);
	else
		vfree(payload);
error:
	return ret;
}

/*
 * Instantiate a key with the specified payload and link the key into the
 * destination keyring if one is given.
 *
 * The caller must have the appropriate instantiation permit set for this to
 * work (see keyctl_assume_authority).  No other permissions are required.
 *
 * If successful, 0 will be returned.
 */
long keyctl_instantiate_key(key_serial_t id,
			    const void __user *_payload,
			    size_t plen,
			    key_serial_t ringid)
{
	if (_payload && plen) {
		struct iovec iov[1] = {
			[0].iov_base = (void __user *)_payload,
			[0].iov_len  = plen
		};

		return keyctl_instantiate_key_common(id, iov, 1, plen, ringid);
	}

	return keyctl_instantiate_key_common(id, NULL, 0, 0, ringid);
}

/*
 * Instantiate a key with the specified multipart payload and link the key into
 * the destination keyring if one is given.
 *
 * The caller must have the appropriate instantiation permit set for this to
 * work (see keyctl_assume_authority).  No other permissions are required.
 *
 * If successful, 0 will be returned.
 */
long keyctl_instantiate_key_iov(key_serial_t id,
				const struct iovec __user *_payload_iov,
				unsigned ioc,
				key_serial_t ringid)
{
	struct iovec iovstack[UIO_FASTIOV], *iov = iovstack;
	long ret;

	if (_payload_iov == 0 || ioc == 0)
		goto no_payload;

	ret = rw_copy_check_uvector(WRITE, _payload_iov, ioc,
				    ARRAY_SIZE(iovstack), iovstack, &iov, 1);
	if (ret < 0)
		return ret;
	if (ret == 0)
		goto no_payload_free;

	ret = keyctl_instantiate_key_common(id, iov, ioc, ret, ringid);

	if (iov != iovstack)
		kfree(iov);
	return ret;

no_payload_free:
	if (iov != iovstack)
		kfree(iov);
no_payload:
	return keyctl_instantiate_key_common(id, NULL, 0, 0, ringid);
}

/*
 * Negatively instantiate the key with the given timeout (in seconds) and link
 * the key into the destination keyring if one is given.
 *
 * The caller must have the appropriate instantiation permit set for this to
 * work (see keyctl_assume_authority).  No other permissions are required.
 *
 * The key and any links to the key will be automatically garbage collected
 * after the timeout expires.
 *
 * Negative keys are used to rate limit repeated request_key() calls by causing
 * them to return -ENOKEY until the negative key expires.
 *
 * If successful, 0 will be returned.
 */
long keyctl_negate_key(key_serial_t id, unsigned timeout, key_serial_t ringid)
{
	return keyctl_reject_key(id, timeout, ENOKEY, ringid);
}

/*
 * Negatively instantiate the key with the given timeout (in seconds) and error
 * code and link the key into the destination keyring if one is given.
 *
 * The caller must have the appropriate instantiation permit set for this to
 * work (see keyctl_assume_authority).  No other permissions are required.
 *
 * The key and any links to the key will be automatically garbage collected
 * after the timeout expires.
 *
 * Negative keys are used to rate limit repeated request_key() calls by causing
 * them to return the specified error code until the negative key expires.
 *
 * If successful, 0 will be returned.
 */
long keyctl_reject_key(key_serial_t id, unsigned timeout, unsigned error,
		       key_serial_t ringid)
{
	const struct cred *cred = current_cred();
	struct request_key_auth *rka;
	struct key *instkey, *dest_keyring;
	long ret;

	kenter("%d,%u,%u,%d", id, timeout, error, ringid);

	/* must be a valid error code and mustn't be a kernel special */
	if (error <= 0 ||
	    error >= MAX_ERRNO ||
	    error == ERESTARTSYS ||
	    error == ERESTARTNOINTR ||
	    error == ERESTARTNOHAND ||
	    error == ERESTART_RESTARTBLOCK)
		return -EINVAL;

	/* the appropriate instantiation authorisation key must have been
	 * assumed before calling this */
	ret = -EPERM;
	instkey = cred->request_key_auth;
	if (!instkey)
		goto error;

	rka = instkey->payload.data;
	if (rka->target_key->serial != id)
		goto error;

	/* find the destination keyring if present (which must also be
	 * writable) */
	ret = get_instantiation_keyring(ringid, rka, &dest_keyring);
	if (ret < 0)
		goto error;

	/* instantiate the key and link it into a keyring */
	ret = key_reject_and_link(rka->target_key, timeout, error,
				  dest_keyring, instkey);

	key_put(dest_keyring);

	/* discard the assumed authority if it's just been disabled by
	 * instantiation of the key */
	if (ret == 0)
		keyctl_change_reqkey_auth(NULL);

error:
	return ret;
}

/*
 * Read or set the default keyring in which request_key() will cache keys and
 * return the old setting.
 *
 * If a process keyring is specified then this will be created if it doesn't
 * yet exist.  The old setting will be returned if successful.
 */
long keyctl_set_reqkey_keyring(int reqkey_defl)
{
	struct cred *new;
	int ret, old_setting;

	old_setting = current_cred_xxx(jit_keyring);

	if (reqkey_defl == KEY_REQKEY_DEFL_NO_CHANGE)
		return old_setting;

	new = prepare_creds();
	if (!new)
		return -ENOMEM;

	switch (reqkey_defl) {
	case KEY_REQKEY_DEFL_THREAD_KEYRING:
		ret = install_thread_keyring_to_cred(new);
		if (ret < 0)
			goto error;
		goto set;

	case KEY_REQKEY_DEFL_PROCESS_KEYRING:
		ret = install_process_keyring_to_cred(new);
		if (ret < 0) {
			if (ret != -EEXIST)
				goto error;
			ret = 0;
		}
		goto set;

	case KEY_REQKEY_DEFL_DEFAULT:
	case KEY_REQKEY_DEFL_SESSION_KEYRING:
	case KEY_REQKEY_DEFL_USER_KEYRING:
	case KEY_REQKEY_DEFL_USER_SESSION_KEYRING:
	case KEY_REQKEY_DEFL_REQUESTOR_KEYRING:
		goto set;

	case KEY_REQKEY_DEFL_NO_CHANGE:
	case KEY_REQKEY_DEFL_GROUP_KEYRING:
	default:
		ret = -EINVAL;
		goto error;
	}

set:
	new->jit_keyring = reqkey_defl;
	commit_creds(new);
	return old_setting;
error:
	abort_creds(new);
	return ret;
}

/*
 * Set or clear the timeout on a key.
 *
 * Either the key must grant the caller Setattr permission or else the caller
 * must hold an instantiation authorisation token for the key.
 *
 * The timeout is either 0 to clear the timeout, or a number of seconds from
 * the current time.  The key and any links to the key will be automatically
 * garbage collected after the timeout expires.
 *
 * If successful, 0 is returned.
 */
long keyctl_set_timeout(key_serial_t id, unsigned timeout)
{
	struct key *key, *instkey;
	key_ref_t key_ref;
	long ret;

	key_ref = lookup_user_key(id, KEY_LOOKUP_CREATE | KEY_LOOKUP_PARTIAL,
				  KEY_SETATTR);
	if (IS_ERR(key_ref)) {
		/* setting the timeout on a key under construction is permitted
		 * if we have the authorisation token handy */
		if (PTR_ERR(key_ref) == -EACCES) {
			instkey = key_get_instantiation_authkey(id);
			if (!IS_ERR(instkey)) {
				key_put(instkey);
				key_ref = lookup_user_key(id,
							  KEY_LOOKUP_PARTIAL,
							  0);
				if (!IS_ERR(key_ref))
					goto okay;
			}
		}

		ret = PTR_ERR(key_ref);
		goto error;
	}

okay:
	key = key_ref_to_ptr(key_ref);
	key_set_timeout(key, timeout);
	key_put(key);

	ret = 0;
error:
	return ret;
}

/*
 * Assume (or clear) the authority to instantiate the specified key.
 *
 * This sets the authoritative token currently in force for key instantiation.
 * This must be done for a key to be instantiated.  It has the effect of making
 * available all the keys from the caller of the request_key() that created a
 * key to request_key() calls made by the caller of this function.
 *
 * The caller must have the instantiation key in their process keyrings with a
 * Search permission grant available to the caller.
 *
 * If the ID given is 0, then the setting will be cleared and 0 returned.
 *
 * If the ID given has a matching an authorisation key, then that key will be
 * set and its ID will be returned.  The authorisation key can be read to get
 * the callout information passed to request_key().
 */
long keyctl_assume_authority(key_serial_t id)
{
	struct key *authkey;
	long ret;

	/* special key IDs aren't permitted */
	ret = -EINVAL;
	if (id < 0)
		goto error;

	/* we divest ourselves of authority if given an ID of 0 */
	if (id == 0) {
		ret = keyctl_change_reqkey_auth(NULL);
		goto error;
	}

	/* attempt to assume the authority temporarily granted to us whilst we
	 * instantiate the specified key
	 * - the authorisation key must be in the current task's keyrings
	 *   somewhere
	 */
	authkey = key_get_instantiation_authkey(id);
	if (IS_ERR(authkey)) {
		ret = PTR_ERR(authkey);
		goto error;
	}

	ret = keyctl_change_reqkey_auth(authkey);
	if (ret < 0)
		goto error;
	key_put(authkey);

	ret = authkey->serial;
error:
	return ret;
}

/*
 * Get a key's the LSM security label.
 *
 * The key must grant the caller View permission for this to work.
 *
 * If there's a buffer, then up to buflen bytes of data will be placed into it.
 *
 * If successful, the amount of information available will be returned,
 * irrespective of how much was copied (including the terminal NUL).
 */
long keyctl_get_security(key_serial_t keyid,
			 char __user *buffer,
			 size_t buflen)
{
	struct key *key, *instkey;
	key_ref_t key_ref;
	char *context;
	long ret;

	key_ref = lookup_user_key(keyid, KEY_LOOKUP_PARTIAL, KEY_VIEW);
	if (IS_ERR(key_ref)) {
		if (PTR_ERR(key_ref) != -EACCES)
			return PTR_ERR(key_ref);

		/* viewing a key under construction is also permitted if we
		 * have the authorisation token handy */
		instkey = key_get_instantiation_authkey(keyid);
		if (IS_ERR(instkey))
			return PTR_ERR(instkey);
		key_put(instkey);

		key_ref = lookup_user_key(keyid, KEY_LOOKUP_PARTIAL, 0);
		if (IS_ERR(key_ref))
			return PTR_ERR(key_ref);
	}

	key = key_ref_to_ptr(key_ref);
	ret = security_key_getsecurity(key, &context);
	if (ret == 0) {
		/* if no information was returned, give userspace an empty
		 * string */
		ret = 1;
		if (buffer && buflen > 0 &&
		    copy_to_user(buffer, "", 1) != 0)
			ret = -EFAULT;
	} else if (ret > 0) {
		/* return as much data as there's room for */
		if (buffer && buflen > 0) {
			if (buflen > ret)
				buflen = ret;

			if (copy_to_user(buffer, context, buflen) != 0)
				ret = -EFAULT;
		}

		kfree(context);
	}

	key_ref_put(key_ref);
	return ret;
}

/*
 * Attempt to install the calling process's session keyring on the process's
 * parent process.
 *
 * The keyring must exist and must grant the caller LINK permission, and the
 * parent process must be single-threaded and must have the same effective
 * ownership as this process and mustn't be SUID/SGID.
 *
 * The keyring will be emplaced on the parent when it next resumes userspace.
 *
 * If successful, 0 will be returned.
 */
long keyctl_session_to_parent(void)
{
#ifdef TIF_NOTIFY_RESUME
	struct task_struct *me, *parent;
	const struct cred *mycred, *pcred;
	struct cred *cred, *oldcred;
	key_ref_t keyring_r;
	int ret;

	keyring_r = lookup_user_key(KEY_SPEC_SESSION_KEYRING, 0, KEY_LINK);
	if (IS_ERR(keyring_r))
		return PTR_ERR(keyring_r);

	/* our parent is going to need a new cred struct, a new tgcred struct
	 * and new security data, so we allocate them here to prevent ENOMEM in
	 * our parent */
	ret = -ENOMEM;
	cred = cred_alloc_blank();
	if (!cred)
		goto error_keyring;

	cred->tgcred->session_keyring = key_ref_to_ptr(keyring_r);
	keyring_r = NULL;

	me = current;
	rcu_read_lock();
	write_lock_irq(&tasklist_lock);

	parent = me->real_parent;
	ret = -EPERM;

	/* the parent mustn't be init and mustn't be a kernel thread */
	if (parent->pid <= 1 || !parent->mm)
		goto not_permitted;

	/* the parent must be single threaded */
	if (!thread_group_empty(parent))
		goto not_permitted;

	/* the parent and the child must have different session keyrings or
	 * there's no point */
	mycred = current_cred();
	pcred = __task_cred(parent);
	if (mycred == pcred ||
	    mycred->tgcred->session_keyring == pcred->tgcred->session_keyring)
		goto already_same;

	/* the parent must have the same effective ownership and mustn't be
	 * SUID/SGID */
	if (pcred->uid	!= mycred->euid	||
	    pcred->euid	!= mycred->euid	||
	    pcred->suid	!= mycred->euid	||
	    pcred->gid	!= mycred->egid	||
	    pcred->egid	!= mycred->egid	||
	    pcred->sgid	!= mycred->egid)
		goto not_permitted;

	/* the keyrings must have the same UID */
	if ((pcred->tgcred->session_keyring &&
	     pcred->tgcred->session_keyring->uid != mycred->euid) ||
	    mycred->tgcred->session_keyring->uid != mycred->euid)
		goto not_permitted;

	/* if there's an already pending keyring replacement, then we replace
	 * that */
	oldcred = parent->replacement_session_keyring;

	/* the replacement session keyring is applied just prior to userspace
	 * restarting */
	parent->replacement_session_keyring = cred;
	cred = NULL;
	set_ti_thread_flag(task_thread_info(parent), TIF_NOTIFY_RESUME);

	write_unlock_irq(&tasklist_lock);
	rcu_read_unlock();
	if (oldcred)
		put_cred(oldcred);
	return 0;

already_same:
	ret = 0;
not_permitted:
	write_unlock_irq(&tasklist_lock);
	rcu_read_unlock();
	put_cred(cred);
	return ret;

error_keyring:
	key_ref_put(keyring_r);
	return ret;

#else /* !TIF_NOTIFY_RESUME */
	/*
	 * To be removed when TIF_NOTIFY_RESUME has been implemented on
	 * m68k/xtensa
	 */
#warning TIF_NOTIFY_RESUME not implemented
	return -EOPNOTSUPP;
#endif /* !TIF_NOTIFY_RESUME */
}

/*
 * The key control system call
 */
SYSCALL_DEFINE5(keyctl, int, option, unsigned long, arg2, unsigned long, arg3,
		unsigned long, arg4, unsigned long, arg5)
{
	switch (option) {
	case KEYCTL_GET_KEYRING_ID:
		return keyctl_get_keyring_ID((key_serial_t) arg2,
					     (int) arg3);

	case KEYCTL_JOIN_SESSION_KEYRING:
		return keyctl_join_session_keyring((const char __user *) arg2);

	case KEYCTL_UPDATE:
		return keyctl_update_key((key_serial_t) arg2,
					 (const void __user *) arg3,
					 (size_t) arg4);

	case KEYCTL_REVOKE:
		return keyctl_revoke_key((key_serial_t) arg2);

	case KEYCTL_DESCRIBE:
		return keyctl_describe_key((key_serial_t) arg2,
					   (char __user *) arg3,
					   (unsigned) arg4);

	case KEYCTL_CLEAR:
		return keyctl_keyring_clear((key_serial_t) arg2);

	case KEYCTL_LINK:
		return keyctl_keyring_link((key_serial_t) arg2,
					   (key_serial_t) arg3);

	case KEYCTL_UNLINK:
		return keyctl_keyring_unlink((key_serial_t) arg2,
					     (key_serial_t) arg3);

	case KEYCTL_SEARCH:
		return keyctl_keyring_search((key_serial_t) arg2,
					     (const char __user *) arg3,
					     (const char __user *) arg4,
					     (key_serial_t) arg5);

	case KEYCTL_READ:
		return keyctl_read_key((key_serial_t) arg2,
				       (char __user *) arg3,
				       (size_t) arg4);

	case KEYCTL_CHOWN:
		return keyctl_chown_key((key_serial_t) arg2,
					(uid_t) arg3,
					(gid_t) arg4);

	case KEYCTL_SETPERM:
		return keyctl_setperm_key((key_serial_t) arg2,
					  (key_perm_t) arg3);

	case KEYCTL_INSTANTIATE:
		return keyctl_instantiate_key((key_serial_t) arg2,
					      (const void __user *) arg3,
					      (size_t) arg4,
					      (key_serial_t) arg5);

	case KEYCTL_NEGATE:
		return keyctl_negate_key((key_serial_t) arg2,
					 (unsigned) arg3,
					 (key_serial_t) arg4);

	case KEYCTL_SET_REQKEY_KEYRING:
		return keyctl_set_reqkey_keyring(arg2);

	case KEYCTL_SET_TIMEOUT:
		return keyctl_set_timeout((key_serial_t) arg2,
					  (unsigned) arg3);

	case KEYCTL_ASSUME_AUTHORITY:
		return keyctl_assume_authority((key_serial_t) arg2);

	case KEYCTL_GET_SECURITY:
		return keyctl_get_security((key_serial_t) arg2,
					   (char __user *) arg3,
					   (size_t) arg4);

	case KEYCTL_SESSION_TO_PARENT:
		return keyctl_session_to_parent();

	case KEYCTL_REJECT:
		return keyctl_reject_key((key_serial_t) arg2,
					 (unsigned) arg3,
					 (unsigned) arg4,
					 (key_serial_t) arg5);

	case KEYCTL_INSTANTIATE_IOV:
		return keyctl_instantiate_key_iov(
			(key_serial_t) arg2,
			(const struct iovec __user *) arg3,
			(unsigned) arg4,
			(key_serial_t) arg5);

	case KEYCTL_INVALIDATE:
		return keyctl_invalidate_key((key_serial_t) arg2);

	default:
		return -EOPNOTSUPP;
	}
}
