/* keyctl.c: userspace keyctl operations
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
		return -EFAULT;

	if (ret == 0 || ret >= len)
		return -EINVAL;

	if (type[0] == '.')
		return -EPERM;

	type[len - 1] = '\0';

	return 0;
}

/*****************************************************************************/
/*
 * extract the description of a new key from userspace and either add it as a
 * new key to the specified keyring or update a matching key in that keyring
 * - the keyring must be writable
 * - returns the new key's serial number
 * - implements add_key()
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
	keyring_ref = lookup_user_key(ringid, 1, 0, KEY_WRITE);
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

} /* end sys_add_key() */

/*****************************************************************************/
/*
 * search the process keyrings for a matching key
 * - nested keyrings may also be searched if they have Search permission
 * - if a key is found, it will be attached to the destination keyring if
 *   there's one specified
 * - /sbin/request-key will be invoked if _callout_info is non-NULL
 *   - the _callout_info string will be passed to /sbin/request-key
 *   - if the _callout_info string is empty, it will be rendered as "-"
 * - implements request_key()
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
		dest_ref = lookup_user_key(destringid, 1, 0, KEY_WRITE);
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

	ret = key->serial;

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

} /* end sys_request_key() */

/*****************************************************************************/
/*
 * get the ID of the specified process keyring
 * - the keyring must have search permission to be found
 * - implements keyctl(KEYCTL_GET_KEYRING_ID)
 */
long keyctl_get_keyring_ID(key_serial_t id, int create)
{
	key_ref_t key_ref;
	long ret;

	key_ref = lookup_user_key(id, create, 0, KEY_SEARCH);
	if (IS_ERR(key_ref)) {
		ret = PTR_ERR(key_ref);
		goto error;
	}

	ret = key_ref_to_ptr(key_ref)->serial;
	key_ref_put(key_ref);
 error:
	return ret;

} /* end keyctl_get_keyring_ID() */

/*****************************************************************************/
/*
 * join the session keyring
 * - implements keyctl(KEYCTL_JOIN_SESSION_KEYRING)
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

 error:
	return ret;

} /* end keyctl_join_session_keyring() */

/*****************************************************************************/
/*
 * update a key's data payload
 * - the key must be writable
 * - implements keyctl(KEYCTL_UPDATE)
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
	key_ref = lookup_user_key(id, 0, 0, KEY_WRITE);
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

} /* end keyctl_update_key() */

/*****************************************************************************/
/*
 * revoke a key
 * - the key must be writable
 * - implements keyctl(KEYCTL_REVOKE)
 */
long keyctl_revoke_key(key_serial_t id)
{
	key_ref_t key_ref;
	long ret;

	key_ref = lookup_user_key(id, 0, 0, KEY_WRITE);
	if (IS_ERR(key_ref)) {
		ret = PTR_ERR(key_ref);
		goto error;
	}

	key_revoke(key_ref_to_ptr(key_ref));
	ret = 0;

	key_ref_put(key_ref);
 error:
	return ret;

} /* end keyctl_revoke_key() */

/*****************************************************************************/
/*
 * clear the specified process keyring
 * - the keyring must be writable
 * - implements keyctl(KEYCTL_CLEAR)
 */
long keyctl_keyring_clear(key_serial_t ringid)
{
	key_ref_t keyring_ref;
	long ret;

	keyring_ref = lookup_user_key(ringid, 1, 0, KEY_WRITE);
	if (IS_ERR(keyring_ref)) {
		ret = PTR_ERR(keyring_ref);
		goto error;
	}

	ret = keyring_clear(key_ref_to_ptr(keyring_ref));

	key_ref_put(keyring_ref);
 error:
	return ret;

} /* end keyctl_keyring_clear() */

/*****************************************************************************/
/*
 * link a key into a keyring
 * - the keyring must be writable
 * - the key must be linkable
 * - implements keyctl(KEYCTL_LINK)
 */
long keyctl_keyring_link(key_serial_t id, key_serial_t ringid)
{
	key_ref_t keyring_ref, key_ref;
	long ret;

	keyring_ref = lookup_user_key(ringid, 1, 0, KEY_WRITE);
	if (IS_ERR(keyring_ref)) {
		ret = PTR_ERR(keyring_ref);
		goto error;
	}

	key_ref = lookup_user_key(id, 1, 0, KEY_LINK);
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

} /* end keyctl_keyring_link() */

/*****************************************************************************/
/*
 * unlink the first attachment of a key from a keyring
 * - the keyring must be writable
 * - we don't need any permissions on the key
 * - implements keyctl(KEYCTL_UNLINK)
 */
long keyctl_keyring_unlink(key_serial_t id, key_serial_t ringid)
{
	key_ref_t keyring_ref, key_ref;
	long ret;

	keyring_ref = lookup_user_key(ringid, 0, 0, KEY_WRITE);
	if (IS_ERR(keyring_ref)) {
		ret = PTR_ERR(keyring_ref);
		goto error;
	}

	key_ref = lookup_user_key(id, 0, 0, 0);
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

} /* end keyctl_keyring_unlink() */

/*****************************************************************************/
/*
 * describe a user key
 * - the key must have view permission
 * - if there's a buffer, we place up to buflen bytes of data into it
 * - unless there's an error, we return the amount of description available,
 *   irrespective of how much we may have copied
 * - the description is formatted thus:
 *	type;uid;gid;perm;description<NUL>
 * - implements keyctl(KEYCTL_DESCRIBE)
 */
long keyctl_describe_key(key_serial_t keyid,
			 char __user *buffer,
			 size_t buflen)
{
	struct key *key, *instkey;
	key_ref_t key_ref;
	char *tmpbuf;
	long ret;

	key_ref = lookup_user_key(keyid, 0, 1, KEY_VIEW);
	if (IS_ERR(key_ref)) {
		/* viewing a key under construction is permitted if we have the
		 * authorisation token handy */
		if (PTR_ERR(key_ref) == -EACCES) {
			instkey = key_get_instantiation_authkey(keyid);
			if (!IS_ERR(instkey)) {
				key_put(instkey);
				key_ref = lookup_user_key(keyid,
							  0, 1, 0);
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
		       key_ref_to_ptr(key_ref)->type->name,
		       key_ref_to_ptr(key_ref)->uid,
		       key_ref_to_ptr(key_ref)->gid,
		       key_ref_to_ptr(key_ref)->perm,
		       key_ref_to_ptr(key_ref)->description ?
		       key_ref_to_ptr(key_ref)->description : ""
		       );

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

} /* end keyctl_describe_key() */

/*****************************************************************************/
/*
 * search the specified keyring for a matching key
 * - the start keyring must be searchable
 * - nested keyrings may also be searched if they are searchable
 * - only keys with search permission may be found
 * - if a key is found, it will be attached to the destination keyring if
 *   there's one specified
 * - implements keyctl(KEYCTL_SEARCH)
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
	keyring_ref = lookup_user_key(ringid, 0, 0, KEY_SEARCH);
	if (IS_ERR(keyring_ref)) {
		ret = PTR_ERR(keyring_ref);
		goto error2;
	}

	/* get the destination keyring if specified */
	dest_ref = NULL;
	if (destringid) {
		dest_ref = lookup_user_key(destringid, 1, 0, KEY_WRITE);
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

} /* end keyctl_keyring_search() */

/*****************************************************************************/
/*
 * read a user key's payload
 * - the keyring must be readable or the key must be searchable from the
 *   process's keyrings
 * - if there's a buffer, we place up to buflen bytes of data into it
 * - unless there's an error, we return the amount of data in the key,
 *   irrespective of how much we may have copied
 * - implements keyctl(KEYCTL_READ)
 */
long keyctl_read_key(key_serial_t keyid, char __user *buffer, size_t buflen)
{
	struct key *key;
	key_ref_t key_ref;
	long ret;

	/* find the key first */
	key_ref = lookup_user_key(keyid, 0, 0, 0);
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

} /* end keyctl_read_key() */

/*****************************************************************************/
/*
 * change the ownership of a key
 * - the keyring owned by the changer
 * - if the uid or gid is -1, then that parameter is not changed
 * - implements keyctl(KEYCTL_CHOWN)
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

	key_ref = lookup_user_key(id, 1, 1, KEY_SETATTR);
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
		newowner = key_user_lookup(uid);
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

} /* end keyctl_chown_key() */

/*****************************************************************************/
/*
 * change the permission mask on a key
 * - the keyring owned by the changer
 * - implements keyctl(KEYCTL_SETPERM)
 */
long keyctl_setperm_key(key_serial_t id, key_perm_t perm)
{
	struct key *key;
	key_ref_t key_ref;
	long ret;

	ret = -EINVAL;
	if (perm & ~(KEY_POS_ALL | KEY_USR_ALL | KEY_GRP_ALL | KEY_OTH_ALL))
		goto error;

	key_ref = lookup_user_key(id, 1, 1, KEY_SETATTR);
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

} /* end keyctl_setperm_key() */

/*
 * get the destination keyring for instantiation
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
		dkref = lookup_user_key(ringid, 1, 0, KEY_WRITE);
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
		*_dest_keyring = rka->dest_keyring;
		return 0;
	}

	return -ENOKEY;
}

/*
 * change the request_key authorisation key on the current process
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

/*****************************************************************************/
/*
 * instantiate the key with the specified payload, and, if one is given, link
 * the key into the keyring
 */
long keyctl_instantiate_key(key_serial_t id,
			    const void __user *_payload,
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

	if (_payload) {
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

		ret = -EFAULT;
		if (copy_from_user(payload, _payload, plen) != 0)
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

} /* end keyctl_instantiate_key() */

/*****************************************************************************/
/*
 * negatively instantiate the key with the given timeout (in seconds), and, if
 * one is given, link the key into the keyring
 */
long keyctl_negate_key(key_serial_t id, unsigned timeout, key_serial_t ringid)
{
	const struct cred *cred = current_cred();
	struct request_key_auth *rka;
	struct key *instkey, *dest_keyring;
	long ret;

	kenter("%d,%u,%d", id, timeout, ringid);

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
	ret = key_negate_and_link(rka->target_key, timeout,
				  dest_keyring, instkey);

	key_put(dest_keyring);

	/* discard the assumed authority if it's just been disabled by
	 * instantiation of the key */
	if (ret == 0)
		keyctl_change_reqkey_auth(NULL);

error:
	return ret;

} /* end keyctl_negate_key() */

/*****************************************************************************/
/*
 * set the default keyring in which request_key() will cache keys
 * - return the old setting
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
	return -EINVAL;

} /* end keyctl_set_reqkey_keyring() */

/*****************************************************************************/
/*
 * set or clear the timeout for a key
 */
long keyctl_set_timeout(key_serial_t id, unsigned timeout)
{
	struct timespec now;
	struct key *key;
	key_ref_t key_ref;
	time_t expiry;
	long ret;

	key_ref = lookup_user_key(id, 1, 1, KEY_SETATTR);
	if (IS_ERR(key_ref)) {
		ret = PTR_ERR(key_ref);
		goto error;
	}

	key = key_ref_to_ptr(key_ref);

	/* make the changes with the locks held to prevent races */
	down_write(&key->sem);

	expiry = 0;
	if (timeout > 0) {
		now = current_kernel_time();
		expiry = now.tv_sec + timeout;
	}

	key->expiry = expiry;

	up_write(&key->sem);
	key_put(key);

	ret = 0;
error:
	return ret;

} /* end keyctl_set_timeout() */

/*****************************************************************************/
/*
 * assume the authority to instantiate the specified key
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

} /* end keyctl_assume_authority() */

/*
 * get the security label of a key
 * - the key must grant us view permission
 * - if there's a buffer, we place up to buflen bytes of data into it
 * - unless there's an error, we return the amount of information available,
 *   irrespective of how much we may have copied (including the terminal NUL)
 * - implements keyctl(KEYCTL_GET_SECURITY)
 */
long keyctl_get_security(key_serial_t keyid,
			 char __user *buffer,
			 size_t buflen)
{
	struct key *key, *instkey;
	key_ref_t key_ref;
	char *context;
	long ret;

	key_ref = lookup_user_key(keyid, 0, 1, KEY_VIEW);
	if (IS_ERR(key_ref)) {
		if (PTR_ERR(key_ref) != -EACCES)
			return PTR_ERR(key_ref);

		/* viewing a key under construction is also permitted if we
		 * have the authorisation token handy */
		instkey = key_get_instantiation_authkey(keyid);
		if (IS_ERR(instkey))
			return PTR_ERR(key_ref);
		key_put(instkey);

		key_ref = lookup_user_key(keyid, 0, 1, 0);
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

/*****************************************************************************/
/*
 * the key control system call
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

	default:
		return -EOPNOTSUPP;
	}

} /* end sys_keyctl() */
