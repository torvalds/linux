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
#include <linux/err.h>
#include <asm/uaccess.h>
#include "internal.h"

/*****************************************************************************/
/*
 * extract the description of a new key from userspace and either add it as a
 * new key to the specified keyring or update a matching key in that keyring
 * - the keyring must be writable
 * - returns the new key's serial number
 * - implements add_key()
 */
asmlinkage long sys_add_key(const char __user *_type,
			    const char __user *_description,
			    const void __user *_payload,
			    size_t plen,
			    key_serial_t ringid)
{
	key_ref_t keyring_ref, key_ref;
	char type[32], *description;
	void *payload;
	long dlen, ret;

	ret = -EINVAL;
	if (plen > 32767)
		goto error;

	/* draw all the data into kernel space */
	ret = strncpy_from_user(type, _type, sizeof(type) - 1);
	if (ret < 0)
		goto error;
	type[31] = '\0';

	ret = -EPERM;
	if (type[0] == '.')
		goto error;

	ret = -EFAULT;
	dlen = strnlen_user(_description, PAGE_SIZE - 1);
	if (dlen <= 0)
		goto error;

	ret = -EINVAL;
	if (dlen > PAGE_SIZE - 1)
		goto error;

	ret = -ENOMEM;
	description = kmalloc(dlen + 1, GFP_KERNEL);
	if (!description)
		goto error;

	ret = -EFAULT;
	if (copy_from_user(description, _description, dlen + 1) != 0)
		goto error2;

	/* pull the payload in if one was supplied */
	payload = NULL;

	if (_payload) {
		ret = -ENOMEM;
		payload = kmalloc(plen, GFP_KERNEL);
		if (!payload)
			goto error2;

		ret = -EFAULT;
		if (copy_from_user(payload, _payload, plen) != 0)
			goto error3;
	}

	/* find the target keyring (which must be writable) */
	keyring_ref = lookup_user_key(NULL, ringid, 1, 0, KEY_WRITE);
	if (IS_ERR(keyring_ref)) {
		ret = PTR_ERR(keyring_ref);
		goto error3;
	}

	/* create or update the requested key and add it to the target
	 * keyring */
	key_ref = key_create_or_update(keyring_ref, type, description,
				       payload, plen, 0);
	if (!IS_ERR(key_ref)) {
		ret = key_ref_to_ptr(key_ref)->serial;
		key_ref_put(key_ref);
	}
	else {
		ret = PTR_ERR(key_ref);
	}

	key_ref_put(keyring_ref);
 error3:
	kfree(payload);
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
asmlinkage long sys_request_key(const char __user *_type,
				const char __user *_description,
				const char __user *_callout_info,
				key_serial_t destringid)
{
	struct key_type *ktype;
	struct key *key;
	key_ref_t dest_ref;
	char type[32], *description, *callout_info;
	long dlen, ret;

	/* pull the type into kernel space */
	ret = strncpy_from_user(type, _type, sizeof(type) - 1);
	if (ret < 0)
		goto error;
	type[31] = '\0';

	ret = -EPERM;
	if (type[0] == '.')
		goto error;

	/* pull the description into kernel space */
	ret = -EFAULT;
	dlen = strnlen_user(_description, PAGE_SIZE - 1);
	if (dlen <= 0)
		goto error;

	ret = -EINVAL;
	if (dlen > PAGE_SIZE - 1)
		goto error;

	ret = -ENOMEM;
	description = kmalloc(dlen + 1, GFP_KERNEL);
	if (!description)
		goto error;

	ret = -EFAULT;
	if (copy_from_user(description, _description, dlen + 1) != 0)
		goto error2;

	/* pull the callout info into kernel space */
	callout_info = NULL;
	if (_callout_info) {
		ret = -EFAULT;
		dlen = strnlen_user(_callout_info, PAGE_SIZE - 1);
		if (dlen <= 0)
			goto error2;

		ret = -EINVAL;
		if (dlen > PAGE_SIZE - 1)
			goto error2;

		ret = -ENOMEM;
		callout_info = kmalloc(dlen + 1, GFP_KERNEL);
		if (!callout_info)
			goto error2;

		ret = -EFAULT;
		if (copy_from_user(callout_info, _callout_info, dlen + 1) != 0)
			goto error3;
	}

	/* get the destination keyring if specified */
	dest_ref = NULL;
	if (destringid) {
		dest_ref = lookup_user_key(NULL, destringid, 1, 0, KEY_WRITE);
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
				   key_ref_to_ptr(dest_ref));
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

	key_ref = lookup_user_key(NULL, id, create, 0, KEY_SEARCH);
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
	long nlen, ret;

	/* fetch the name from userspace */
	name = NULL;
	if (_name) {
		ret = -EFAULT;
		nlen = strnlen_user(_name, PAGE_SIZE - 1);
		if (nlen <= 0)
			goto error;

		ret = -EINVAL;
		if (nlen > PAGE_SIZE - 1)
			goto error;

		ret = -ENOMEM;
		name = kmalloc(nlen + 1, GFP_KERNEL);
		if (!name)
			goto error;

		ret = -EFAULT;
		if (copy_from_user(name, _name, nlen + 1) != 0)
			goto error2;
	}

	/* join the session */
	ret = join_session_keyring(name);

 error2:
	kfree(name);
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
	key_ref = lookup_user_key(NULL, id, 0, 0, KEY_WRITE);
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

	key_ref = lookup_user_key(NULL, id, 0, 0, KEY_WRITE);
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

	keyring_ref = lookup_user_key(NULL, ringid, 1, 0, KEY_WRITE);
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

	keyring_ref = lookup_user_key(NULL, ringid, 1, 0, KEY_WRITE);
	if (IS_ERR(keyring_ref)) {
		ret = PTR_ERR(keyring_ref);
		goto error;
	}

	key_ref = lookup_user_key(NULL, id, 1, 0, KEY_LINK);
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

	keyring_ref = lookup_user_key(NULL, ringid, 0, 0, KEY_WRITE);
	if (IS_ERR(keyring_ref)) {
		ret = PTR_ERR(keyring_ref);
		goto error;
	}

	key_ref = lookup_user_key(NULL, id, 0, 0, 0);
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

	key_ref = lookup_user_key(NULL, keyid, 0, 1, KEY_VIEW);
	if (IS_ERR(key_ref)) {
		/* viewing a key under construction is permitted if we have the
		 * authorisation token handy */
		if (PTR_ERR(key_ref) == -EACCES) {
			instkey = key_get_instantiation_authkey(keyid);
			if (!IS_ERR(instkey)) {
				key_put(instkey);
				key_ref = lookup_user_key(NULL, keyid,
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
	long dlen, ret;

	/* pull the type and description into kernel space */
	ret = strncpy_from_user(type, _type, sizeof(type) - 1);
	if (ret < 0)
		goto error;
	type[31] = '\0';

	ret = -EFAULT;
	dlen = strnlen_user(_description, PAGE_SIZE - 1);
	if (dlen <= 0)
		goto error;

	ret = -EINVAL;
	if (dlen > PAGE_SIZE - 1)
		goto error;

	ret = -ENOMEM;
	description = kmalloc(dlen + 1, GFP_KERNEL);
	if (!description)
		goto error;

	ret = -EFAULT;
	if (copy_from_user(description, _description, dlen + 1) != 0)
		goto error2;

	/* get the keyring at which to begin the search */
	keyring_ref = lookup_user_key(NULL, ringid, 0, 0, KEY_SEARCH);
	if (IS_ERR(keyring_ref)) {
		ret = PTR_ERR(keyring_ref);
		goto error2;
	}

	/* get the destination keyring if specified */
	dest_ref = NULL;
	if (destringid) {
		dest_ref = lookup_user_key(NULL, destringid, 1, 0, KEY_WRITE);
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
		ret = -EACCES;
		if (!key_permission(key_ref, KEY_LINK))
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
	key_ref = lookup_user_key(NULL, keyid, 0, 0, 0);
	if (IS_ERR(key_ref)) {
		ret = -ENOKEY;
		goto error;
	}

	key = key_ref_to_ptr(key_ref);

	/* see if we can read it directly */
	if (key_permission(key_ref, KEY_READ))
		goto can_read_key;

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
	struct key *key;
	key_ref_t key_ref;
	long ret;

	ret = 0;
	if (uid == (uid_t) -1 && gid == (gid_t) -1)
		goto error;

	key_ref = lookup_user_key(NULL, id, 1, 1, 0);
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
			goto no_access;

		/* only the sysadmin can set the key's GID to a group other
		 * than one of those that the current process subscribes to */
		if (gid != (gid_t) -1 && gid != key->gid && !in_group_p(gid))
			goto no_access;
	}

	/* change the UID (have to update the quotas) */
	if (uid != (uid_t) -1 && uid != key->uid) {
		/* don't support UID changing yet */
		ret = -EOPNOTSUPP;
		goto no_access;
	}

	/* change the GID */
	if (gid != (gid_t) -1)
		key->gid = gid;

	ret = 0;

 no_access:
	up_write(&key->sem);
	key_put(key);
 error:
	return ret;

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

	key_ref = lookup_user_key(NULL, id, 1, 1, 0);
	if (IS_ERR(key_ref)) {
		ret = PTR_ERR(key_ref);
		goto error;
	}

	key = key_ref_to_ptr(key_ref);

	/* make the changes with the locks held to prevent chown/chmod races */
	ret = -EACCES;
	down_write(&key->sem);

	/* if we're not the sysadmin, we can only change a key that we own */
	if (capable(CAP_SYS_ADMIN) || key->uid == current->fsuid) {
		key->perm = perm;
		ret = 0;
	}

	up_write(&key->sem);
	key_put(key);
error:
	return ret;

} /* end keyctl_setperm_key() */

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
	struct request_key_auth *rka;
	struct key *instkey;
	key_ref_t keyring_ref;
	void *payload;
	long ret;

	ret = -EINVAL;
	if (plen > 32767)
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

	/* find the instantiation authorisation key */
	instkey = key_get_instantiation_authkey(id);
	if (IS_ERR(instkey)) {
		ret = PTR_ERR(instkey);
		goto error2;
	}

	rka = instkey->payload.data;

	/* find the destination keyring amongst those belonging to the
	 * requesting task */
	keyring_ref = NULL;
	if (ringid) {
		keyring_ref = lookup_user_key(rka->context, ringid, 1, 0,
					      KEY_WRITE);
		if (IS_ERR(keyring_ref)) {
			ret = PTR_ERR(keyring_ref);
			goto error3;
		}
	}

	/* instantiate the key and link it into a keyring */
	ret = key_instantiate_and_link(rka->target_key, payload, plen,
				       key_ref_to_ptr(keyring_ref), instkey);

	key_ref_put(keyring_ref);
 error3:
	key_put(instkey);
 error2:
	kfree(payload);
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
	struct request_key_auth *rka;
	struct key *instkey;
	key_ref_t keyring_ref;
	long ret;

	/* find the instantiation authorisation key */
	instkey = key_get_instantiation_authkey(id);
	if (IS_ERR(instkey)) {
		ret = PTR_ERR(instkey);
		goto error;
	}

	rka = instkey->payload.data;

	/* find the destination keyring if present (which must also be
	 * writable) */
	keyring_ref = NULL;
	if (ringid) {
		keyring_ref = lookup_user_key(NULL, ringid, 1, 0, KEY_WRITE);
		if (IS_ERR(keyring_ref)) {
			ret = PTR_ERR(keyring_ref);
			goto error2;
		}
	}

	/* instantiate the key and link it into a keyring */
	ret = key_negate_and_link(rka->target_key, timeout,
				  key_ref_to_ptr(keyring_ref), instkey);

	key_ref_put(keyring_ref);
 error2:
	key_put(instkey);
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
	int ret;

	switch (reqkey_defl) {
	case KEY_REQKEY_DEFL_THREAD_KEYRING:
		ret = install_thread_keyring(current);
		if (ret < 0)
			return ret;
		goto set;

	case KEY_REQKEY_DEFL_PROCESS_KEYRING:
		ret = install_process_keyring(current);
		if (ret < 0)
			return ret;

	case KEY_REQKEY_DEFL_DEFAULT:
	case KEY_REQKEY_DEFL_SESSION_KEYRING:
	case KEY_REQKEY_DEFL_USER_KEYRING:
	case KEY_REQKEY_DEFL_USER_SESSION_KEYRING:
	set:
		current->jit_keyring = reqkey_defl;

	case KEY_REQKEY_DEFL_NO_CHANGE:
		return current->jit_keyring;

	case KEY_REQKEY_DEFL_GROUP_KEYRING:
	default:
		return -EINVAL;
	}

} /* end keyctl_set_reqkey_keyring() */

/*****************************************************************************/
/*
 * the key control system call
 */
asmlinkage long sys_keyctl(int option, unsigned long arg2, unsigned long arg3,
			   unsigned long arg4, unsigned long arg5)
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

	default:
		return -EOPNOTSUPP;
	}

} /* end sys_keyctl() */
