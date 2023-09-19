// SPDX-License-Identifier: GPL-2.0-or-later
/* Userspace key control operations
 *
 * Copyright (C) 2004-5 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/key.h>
#include <linux/keyctl.h>
#include <linux/fs.h>
#include <linux/capability.h>
#include <linux/cred.h>
#include <linux/string.h>
#include <linux/err.h>
#include <linux/vmalloc.h>
#include <linux/security.h>
#include <linux/uio.h>
#include <linux/uaccess.h>
#include <keys/request_key_auth-type.h>
#include "internal.h"

#define KEY_MAX_DESC_SIZE 4096

static const unsigned char keyrings_capabilities[2] = {
	[0] = (KEYCTL_CAPS0_CAPABILITIES |
	       (IS_ENABLED(CONFIG_PERSISTENT_KEYRINGS)	? KEYCTL_CAPS0_PERSISTENT_KEYRINGS : 0) |
	       (IS_ENABLED(CONFIG_KEY_DH_OPERATIONS)	? KEYCTL_CAPS0_DIFFIE_HELLMAN : 0) |
	       (IS_ENABLED(CONFIG_ASYMMETRIC_KEY_TYPE)	? KEYCTL_CAPS0_PUBLIC_KEY : 0) |
	       (IS_ENABLED(CONFIG_BIG_KEYS)		? KEYCTL_CAPS0_BIG_KEY : 0) |
	       KEYCTL_CAPS0_INVALIDATE |
	       KEYCTL_CAPS0_RESTRICT_KEYRING |
	       KEYCTL_CAPS0_MOVE
	       ),
	[1] = (KEYCTL_CAPS1_NS_KEYRING_NAME |
	       KEYCTL_CAPS1_NS_KEY_TAG |
	       (IS_ENABLED(CONFIG_KEY_NOTIFICATIONS)	? KEYCTL_CAPS1_NOTIFICATIONS : 0)
	       ),
};

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
 * If the description is NULL or an empty string, the key type is asked to
 * generate one from the payload.
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

	ret = -EINVAL;
	if (plen > 1024 * 1024 - 1)
		goto error;

	/* draw all the data into kernel space */
	ret = key_get_type_from_user(type, _type, sizeof(type));
	if (ret < 0)
		goto error;

	description = NULL;
	if (_description) {
		description = strndup_user(_description, KEY_MAX_DESC_SIZE);
		if (IS_ERR(description)) {
			ret = PTR_ERR(description);
			goto error;
		}
		if (!*description) {
			kfree(description);
			description = NULL;
		} else if ((description[0] == '.') &&
			   (strncmp(type, "keyring", 7) == 0)) {
			ret = -EPERM;
			goto error2;
		}
	}

	/* pull the payload in if one was supplied */
	payload = NULL;

	if (plen) {
		ret = -ENOMEM;
		payload = kvmalloc(plen, GFP_KERNEL);
		if (!payload)
			goto error2;

		ret = -EFAULT;
		if (copy_from_user(payload, _payload, plen) != 0)
			goto error3;
	}

	/* find the target keyring (which must be writable) */
	keyring_ref = lookup_user_key(ringid, KEY_LOOKUP_CREATE, KEY_NEED_WRITE);
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
	kvfree_sensitive(payload, plen);
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
	description = strndup_user(_description, KEY_MAX_DESC_SIZE);
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
					   KEY_NEED_WRITE);
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
	key = request_key_and_link(ktype, description, NULL, callout_info,
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
	key_ref = lookup_user_key(id, lflags, KEY_NEED_SEARCH);
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
 * be skipped over.  It is not permitted for userspace to create or join
 * keyrings whose name begin with a dot.
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
		name = strndup_user(_name, KEY_MAX_DESC_SIZE);
		if (IS_ERR(name)) {
			ret = PTR_ERR(name);
			goto error;
		}

		ret = -EPERM;
		if (name[0] == '.')
			goto error_name;
	}

	/* join the session */
	ret = join_session_keyring(name);
error_name:
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
	if (plen) {
		ret = -ENOMEM;
		payload = kvmalloc(plen, GFP_KERNEL);
		if (!payload)
			goto error;

		ret = -EFAULT;
		if (copy_from_user(payload, _payload, plen) != 0)
			goto error2;
	}

	/* find the target key (which must be writable) */
	key_ref = lookup_user_key(id, 0, KEY_NEED_WRITE);
	if (IS_ERR(key_ref)) {
		ret = PTR_ERR(key_ref);
		goto error2;
	}

	/* update the key */
	ret = key_update(key_ref, payload, plen);

	key_ref_put(key_ref);
error2:
	kvfree_sensitive(payload, plen);
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
 * Keys with KEY_FLAG_KEEP set should not be revoked.
 *
 * If successful, 0 is returned.
 */
long keyctl_revoke_key(key_serial_t id)
{
	key_ref_t key_ref;
	struct key *key;
	long ret;

	key_ref = lookup_user_key(id, 0, KEY_NEED_WRITE);
	if (IS_ERR(key_ref)) {
		ret = PTR_ERR(key_ref);
		if (ret != -EACCES)
			goto error;
		key_ref = lookup_user_key(id, 0, KEY_NEED_SETATTR);
		if (IS_ERR(key_ref)) {
			ret = PTR_ERR(key_ref);
			goto error;
		}
	}

	key = key_ref_to_ptr(key_ref);
	ret = 0;
	if (test_bit(KEY_FLAG_KEEP, &key->flags))
		ret = -EPERM;
	else
		key_revoke(key);

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
 * Keys with KEY_FLAG_KEEP set should not be invalidated.
 *
 * If successful, 0 is returned.
 */
long keyctl_invalidate_key(key_serial_t id)
{
	key_ref_t key_ref;
	struct key *key;
	long ret;

	kenter("%d", id);

	key_ref = lookup_user_key(id, 0, KEY_NEED_SEARCH);
	if (IS_ERR(key_ref)) {
		ret = PTR_ERR(key_ref);

		/* Root is permitted to invalidate certain special keys */
		if (capable(CAP_SYS_ADMIN)) {
			key_ref = lookup_user_key(id, 0, KEY_SYSADMIN_OVERRIDE);
			if (IS_ERR(key_ref))
				goto error;
			if (test_bit(KEY_FLAG_ROOT_CAN_INVAL,
				     &key_ref_to_ptr(key_ref)->flags))
				goto invalidate;
			goto error_put;
		}

		goto error;
	}

invalidate:
	key = key_ref_to_ptr(key_ref);
	ret = 0;
	if (test_bit(KEY_FLAG_KEEP, &key->flags))
		ret = -EPERM;
	else
		key_invalidate(key);
error_put:
	key_ref_put(key_ref);
error:
	kleave(" = %ld", ret);
	return ret;
}

/*
 * Clear the specified keyring, creating an empty process keyring if one of the
 * special keyring IDs is used.
 *
 * The keyring must grant the caller Write permission and not have
 * KEY_FLAG_KEEP set for this to work.  If successful, 0 will be returned.
 */
long keyctl_keyring_clear(key_serial_t ringid)
{
	key_ref_t keyring_ref;
	struct key *keyring;
	long ret;

	keyring_ref = lookup_user_key(ringid, KEY_LOOKUP_CREATE, KEY_NEED_WRITE);
	if (IS_ERR(keyring_ref)) {
		ret = PTR_ERR(keyring_ref);

		/* Root is permitted to invalidate certain special keyrings */
		if (capable(CAP_SYS_ADMIN)) {
			keyring_ref = lookup_user_key(ringid, 0,
						      KEY_SYSADMIN_OVERRIDE);
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
	keyring = key_ref_to_ptr(keyring_ref);
	if (test_bit(KEY_FLAG_KEEP, &keyring->flags))
		ret = -EPERM;
	else
		ret = keyring_clear(keyring);
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

	keyring_ref = lookup_user_key(ringid, KEY_LOOKUP_CREATE, KEY_NEED_WRITE);
	if (IS_ERR(keyring_ref)) {
		ret = PTR_ERR(keyring_ref);
		goto error;
	}

	key_ref = lookup_user_key(id, KEY_LOOKUP_CREATE, KEY_NEED_LINK);
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
 * Keys or keyrings with KEY_FLAG_KEEP set should not be unlinked.
 *
 * If successful, 0 will be returned.
 */
long keyctl_keyring_unlink(key_serial_t id, key_serial_t ringid)
{
	key_ref_t keyring_ref, key_ref;
	struct key *keyring, *key;
	long ret;

	keyring_ref = lookup_user_key(ringid, 0, KEY_NEED_WRITE);
	if (IS_ERR(keyring_ref)) {
		ret = PTR_ERR(keyring_ref);
		goto error;
	}

	key_ref = lookup_user_key(id, KEY_LOOKUP_PARTIAL, KEY_NEED_UNLINK);
	if (IS_ERR(key_ref)) {
		ret = PTR_ERR(key_ref);
		goto error2;
	}

	keyring = key_ref_to_ptr(keyring_ref);
	key = key_ref_to_ptr(key_ref);
	if (test_bit(KEY_FLAG_KEEP, &keyring->flags) &&
	    test_bit(KEY_FLAG_KEEP, &key->flags))
		ret = -EPERM;
	else
		ret = key_unlink(keyring, key);

	key_ref_put(key_ref);
error2:
	key_ref_put(keyring_ref);
error:
	return ret;
}

/*
 * Move a link to a key from one keyring to another, displacing any matching
 * key from the destination keyring.
 *
 * The key must grant the caller Link permission and both keyrings must grant
 * the caller Write permission.  There must also be a link in the from keyring
 * to the key.  If both keyrings are the same, nothing is done.
 *
 * If successful, 0 will be returned.
 */
long keyctl_keyring_move(key_serial_t id, key_serial_t from_ringid,
			 key_serial_t to_ringid, unsigned int flags)
{
	key_ref_t key_ref, from_ref, to_ref;
	long ret;

	if (flags & ~KEYCTL_MOVE_EXCL)
		return -EINVAL;

	key_ref = lookup_user_key(id, KEY_LOOKUP_CREATE, KEY_NEED_LINK);
	if (IS_ERR(key_ref))
		return PTR_ERR(key_ref);

	from_ref = lookup_user_key(from_ringid, 0, KEY_NEED_WRITE);
	if (IS_ERR(from_ref)) {
		ret = PTR_ERR(from_ref);
		goto error2;
	}

	to_ref = lookup_user_key(to_ringid, KEY_LOOKUP_CREATE, KEY_NEED_WRITE);
	if (IS_ERR(to_ref)) {
		ret = PTR_ERR(to_ref);
		goto error3;
	}

	ret = key_move(key_ref_to_ptr(key_ref), key_ref_to_ptr(from_ref),
		       key_ref_to_ptr(to_ref), flags);

	key_ref_put(to_ref);
error3:
	key_ref_put(from_ref);
error2:
	key_ref_put(key_ref);
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
	char *infobuf;
	long ret;
	int desclen, infolen;

	key_ref = lookup_user_key(keyid, KEY_LOOKUP_PARTIAL, KEY_NEED_VIEW);
	if (IS_ERR(key_ref)) {
		/* viewing a key under construction is permitted if we have the
		 * authorisation token handy */
		if (PTR_ERR(key_ref) == -EACCES) {
			instkey = key_get_instantiation_authkey(keyid);
			if (!IS_ERR(instkey)) {
				key_put(instkey);
				key_ref = lookup_user_key(keyid,
							  KEY_LOOKUP_PARTIAL,
							  KEY_AUTHTOKEN_OVERRIDE);
				if (!IS_ERR(key_ref))
					goto okay;
			}
		}

		ret = PTR_ERR(key_ref);
		goto error;
	}

okay:
	key = key_ref_to_ptr(key_ref);
	desclen = strlen(key->description);

	/* calculate how much information we're going to return */
	ret = -ENOMEM;
	infobuf = kasprintf(GFP_KERNEL,
			    "%s;%d;%d;%08x;",
			    key->type->name,
			    from_kuid_munged(current_user_ns(), key->uid),
			    from_kgid_munged(current_user_ns(), key->gid),
			    key->perm);
	if (!infobuf)
		goto error2;
	infolen = strlen(infobuf);
	ret = infolen + desclen + 1;

	/* consider returning the data */
	if (buffer && buflen >= ret) {
		if (copy_to_user(buffer, infobuf, infolen) != 0 ||
		    copy_to_user(buffer + infolen, key->description,
				 desclen + 1) != 0)
			ret = -EFAULT;
	}

	kfree(infobuf);
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

	description = strndup_user(_description, KEY_MAX_DESC_SIZE);
	if (IS_ERR(description)) {
		ret = PTR_ERR(description);
		goto error;
	}

	/* get the keyring at which to begin the search */
	keyring_ref = lookup_user_key(ringid, 0, KEY_NEED_SEARCH);
	if (IS_ERR(keyring_ref)) {
		ret = PTR_ERR(keyring_ref);
		goto error2;
	}

	/* get the destination keyring if specified */
	dest_ref = NULL;
	if (destringid) {
		dest_ref = lookup_user_key(destringid, KEY_LOOKUP_CREATE,
					   KEY_NEED_WRITE);
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
	key_ref = keyring_search(keyring_ref, ktype, description, true);
	if (IS_ERR(key_ref)) {
		ret = PTR_ERR(key_ref);

		/* treat lack or presence of a negative key the same */
		if (ret == -EAGAIN)
			ret = -ENOKEY;
		goto error5;
	}

	/* link the resulting key to the destination keyring if we can */
	if (dest_ref) {
		ret = key_permission(key_ref, KEY_NEED_LINK);
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
 * Call the read method
 */
static long __keyctl_read_key(struct key *key, char *buffer, size_t buflen)
{
	long ret;

	down_read(&key->sem);
	ret = key_validate(key);
	if (ret == 0)
		ret = key->type->read(key, buffer, buflen);
	up_read(&key->sem);
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
	char *key_data = NULL;
	size_t key_data_len;

	/* find the key first */
	key_ref = lookup_user_key(keyid, 0, KEY_DEFER_PERM_CHECK);
	if (IS_ERR(key_ref)) {
		ret = -ENOKEY;
		goto out;
	}

	key = key_ref_to_ptr(key_ref);

	ret = key_read_state(key);
	if (ret < 0)
		goto key_put_out; /* Negatively instantiated */

	/* see if we can read it directly */
	ret = key_permission(key_ref, KEY_NEED_READ);
	if (ret == 0)
		goto can_read_key;
	if (ret != -EACCES)
		goto key_put_out;

	/* we can't; see if it's searchable from this process's keyrings
	 * - we automatically take account of the fact that it may be
	 *   dangling off an instantiation key
	 */
	if (!is_key_possessed(key_ref)) {
		ret = -EACCES;
		goto key_put_out;
	}

	/* the key is probably readable - now try to read it */
can_read_key:
	if (!key->type->read) {
		ret = -EOPNOTSUPP;
		goto key_put_out;
	}

	if (!buffer || !buflen) {
		/* Get the key length from the read method */
		ret = __keyctl_read_key(key, NULL, 0);
		goto key_put_out;
	}

	/*
	 * Read the data with the semaphore held (since we might sleep)
	 * to protect against the key being updated or revoked.
	 *
	 * Allocating a temporary buffer to hold the keys before
	 * transferring them to user buffer to avoid potential
	 * deadlock involving page fault and mmap_lock.
	 *
	 * key_data_len = (buflen <= PAGE_SIZE)
	 *		? buflen : actual length of key data
	 *
	 * This prevents allocating arbitrary large buffer which can
	 * be much larger than the actual key length. In the latter case,
	 * at least 2 passes of this loop is required.
	 */
	key_data_len = (buflen <= PAGE_SIZE) ? buflen : 0;
	for (;;) {
		if (key_data_len) {
			key_data = kvmalloc(key_data_len, GFP_KERNEL);
			if (!key_data) {
				ret = -ENOMEM;
				goto key_put_out;
			}
		}

		ret = __keyctl_read_key(key, key_data, key_data_len);

		/*
		 * Read methods will just return the required length without
		 * any copying if the provided length isn't large enough.
		 */
		if (ret <= 0 || ret > buflen)
			break;

		/*
		 * The key may change (unlikely) in between 2 consecutive
		 * __keyctl_read_key() calls. In this case, we reallocate
		 * a larger buffer and redo the key read when
		 * key_data_len < ret <= buflen.
		 */
		if (ret > key_data_len) {
			if (unlikely(key_data))
				kvfree_sensitive(key_data, key_data_len);
			key_data_len = ret;
			continue;	/* Allocate buffer */
		}

		if (copy_to_user(buffer, key_data, ret))
			ret = -EFAULT;
		break;
	}
	kvfree_sensitive(key_data, key_data_len);

key_put_out:
	key_put(key);
out:
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
long keyctl_chown_key(key_serial_t id, uid_t user, gid_t group)
{
	struct key_user *newowner, *zapowner = NULL;
	struct key *key;
	key_ref_t key_ref;
	long ret;
	kuid_t uid;
	kgid_t gid;

	uid = make_kuid(current_user_ns(), user);
	gid = make_kgid(current_user_ns(), group);
	ret = -EINVAL;
	if ((user != (uid_t) -1) && !uid_valid(uid))
		goto error;
	if ((group != (gid_t) -1) && !gid_valid(gid))
		goto error;

	ret = 0;
	if (user == (uid_t) -1 && group == (gid_t) -1)
		goto error;

	key_ref = lookup_user_key(id, KEY_LOOKUP_CREATE | KEY_LOOKUP_PARTIAL,
				  KEY_NEED_SETATTR);
	if (IS_ERR(key_ref)) {
		ret = PTR_ERR(key_ref);
		goto error;
	}

	key = key_ref_to_ptr(key_ref);

	/* make the changes with the locks held to prevent chown/chown races */
	ret = -EACCES;
	down_write(&key->sem);

	{
		bool is_privileged_op = false;

		/* only the sysadmin can chown a key to some other UID */
		if (user != (uid_t) -1 && !uid_eq(key->uid, uid))
			is_privileged_op = true;

		/* only the sysadmin can set the key's GID to a group other
		 * than one of those that the current process subscribes to */
		if (group != (gid_t) -1 && !gid_eq(gid, key->gid) && !in_group_p(gid))
			is_privileged_op = true;

		if (is_privileged_op && !capable(CAP_SYS_ADMIN))
			goto error_put;
	}

	/* change the UID */
	if (user != (uid_t) -1 && !uid_eq(uid, key->uid)) {
		ret = -ENOMEM;
		newowner = key_user_lookup(uid);
		if (!newowner)
			goto error_put;

		/* transfer the quota burden to the new user */
		if (test_bit(KEY_FLAG_IN_QUOTA, &key->flags)) {
			unsigned maxkeys = uid_eq(uid, GLOBAL_ROOT_UID) ?
				key_quota_root_maxkeys : key_quota_maxkeys;
			unsigned maxbytes = uid_eq(uid, GLOBAL_ROOT_UID) ?
				key_quota_root_maxbytes : key_quota_maxbytes;

			spin_lock(&newowner->lock);
			if (newowner->qnkeys + 1 > maxkeys ||
			    newowner->qnbytes + key->quotalen > maxbytes ||
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

		if (key->state != KEY_IS_UNINSTANTIATED) {
			atomic_dec(&key->user->nikeys);
			atomic_inc(&newowner->nikeys);
		}

		zapowner = key->user;
		key->user = newowner;
		key->uid = uid;
	}

	/* change the GID */
	if (group != (gid_t) -1)
		key->gid = gid;

	notify_key(key, NOTIFY_KEY_SETATTR, 0);
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
				  KEY_NEED_SETATTR);
	if (IS_ERR(key_ref)) {
		ret = PTR_ERR(key_ref);
		goto error;
	}

	key = key_ref_to_ptr(key_ref);

	/* make the changes with the locks held to prevent chown/chmod races */
	ret = -EACCES;
	down_write(&key->sem);

	/* if we're not the sysadmin, we can only change a key that we own */
	if (uid_eq(key->uid, current_fsuid()) || capable(CAP_SYS_ADMIN)) {
		key->perm = perm;
		notify_key(key, NOTIFY_KEY_SETATTR, 0);
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
		dkref = lookup_user_key(ringid, KEY_LOOKUP_CREATE, KEY_NEED_WRITE);
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
 * Instantiate a key with the specified payload and link the key into the
 * destination keyring if one is given.
 *
 * The caller must have the appropriate instantiation permit set for this to
 * work (see keyctl_assume_authority).  No other permissions are required.
 *
 * If successful, 0 will be returned.
 */
static long keyctl_instantiate_key_common(key_serial_t id,
				   struct iov_iter *from,
				   key_serial_t ringid)
{
	const struct cred *cred = current_cred();
	struct request_key_auth *rka;
	struct key *instkey, *dest_keyring;
	size_t plen = from ? iov_iter_count(from) : 0;
	void *payload;
	long ret;

	kenter("%d,,%zu,%d", id, plen, ringid);

	if (!plen)
		from = NULL;

	ret = -EINVAL;
	if (plen > 1024 * 1024 - 1)
		goto error;

	/* the appropriate instantiation authorisation key must have been
	 * assumed before calling this */
	ret = -EPERM;
	instkey = cred->request_key_auth;
	if (!instkey)
		goto error;

	rka = instkey->payload.data[0];
	if (rka->target_key->serial != id)
		goto error;

	/* pull the payload in if one was supplied */
	payload = NULL;

	if (from) {
		ret = -ENOMEM;
		payload = kvmalloc(plen, GFP_KERNEL);
		if (!payload)
			goto error;

		ret = -EFAULT;
		if (!copy_from_iter_full(payload, plen, from))
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
	kvfree_sensitive(payload, plen);
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
		struct iovec iov;
		struct iov_iter from;
		int ret;

		ret = import_single_range(WRITE, (void __user *)_payload, plen,
					  &iov, &from);
		if (unlikely(ret))
			return ret;

		return keyctl_instantiate_key_common(id, &from, ringid);
	}

	return keyctl_instantiate_key_common(id, NULL, ringid);
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
	struct iov_iter from;
	long ret;

	if (!_payload_iov)
		ioc = 0;

	ret = import_iovec(WRITE, _payload_iov, ioc,
				    ARRAY_SIZE(iovstack), &iov, &from);
	if (ret < 0)
		return ret;
	ret = keyctl_instantiate_key_common(id, &from, ringid);
	kfree(iov);
	return ret;
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

	rka = instkey->payload.data[0];
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
 * If a thread or process keyring is specified then it will be created if it
 * doesn't yet exist.  The old setting will be returned if successful.
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
		if (ret < 0)
			goto error;
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
 * Keys with KEY_FLAG_KEEP set should not be timed out.
 *
 * If successful, 0 is returned.
 */
long keyctl_set_timeout(key_serial_t id, unsigned timeout)
{
	struct key *key, *instkey;
	key_ref_t key_ref;
	long ret;

	key_ref = lookup_user_key(id, KEY_LOOKUP_CREATE | KEY_LOOKUP_PARTIAL,
				  KEY_NEED_SETATTR);
	if (IS_ERR(key_ref)) {
		/* setting the timeout on a key under construction is permitted
		 * if we have the authorisation token handy */
		if (PTR_ERR(key_ref) == -EACCES) {
			instkey = key_get_instantiation_authkey(id);
			if (!IS_ERR(instkey)) {
				key_put(instkey);
				key_ref = lookup_user_key(id,
							  KEY_LOOKUP_PARTIAL,
							  KEY_AUTHTOKEN_OVERRIDE);
				if (!IS_ERR(key_ref))
					goto okay;
			}
		}

		ret = PTR_ERR(key_ref);
		goto error;
	}

okay:
	key = key_ref_to_ptr(key_ref);
	ret = 0;
	if (test_bit(KEY_FLAG_KEEP, &key->flags)) {
		ret = -EPERM;
	} else {
		key_set_timeout(key, timeout);
		notify_key(key, NOTIFY_KEY_SETATTR, 0);
	}
	key_put(key);

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
	if (ret == 0)
		ret = authkey->serial;
	key_put(authkey);
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

	key_ref = lookup_user_key(keyid, KEY_LOOKUP_PARTIAL, KEY_NEED_VIEW);
	if (IS_ERR(key_ref)) {
		if (PTR_ERR(key_ref) != -EACCES)
			return PTR_ERR(key_ref);

		/* viewing a key under construction is also permitted if we
		 * have the authorisation token handy */
		instkey = key_get_instantiation_authkey(keyid);
		if (IS_ERR(instkey))
			return PTR_ERR(instkey);
		key_put(instkey);

		key_ref = lookup_user_key(keyid, KEY_LOOKUP_PARTIAL,
					  KEY_AUTHTOKEN_OVERRIDE);
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
	struct task_struct *me, *parent;
	const struct cred *mycred, *pcred;
	struct callback_head *newwork, *oldwork;
	key_ref_t keyring_r;
	struct cred *cred;
	int ret;

	keyring_r = lookup_user_key(KEY_SPEC_SESSION_KEYRING, 0, KEY_NEED_LINK);
	if (IS_ERR(keyring_r))
		return PTR_ERR(keyring_r);

	ret = -ENOMEM;

	/* our parent is going to need a new cred struct, a new tgcred struct
	 * and new security data, so we allocate them here to prevent ENOMEM in
	 * our parent */
	cred = cred_alloc_blank();
	if (!cred)
		goto error_keyring;
	newwork = &cred->rcu;

	cred->session_keyring = key_ref_to_ptr(keyring_r);
	keyring_r = NULL;
	init_task_work(newwork, key_change_session_keyring);

	me = current;
	rcu_read_lock();
	write_lock_irq(&tasklist_lock);

	ret = -EPERM;
	oldwork = NULL;
	parent = rcu_dereference_protected(me->real_parent,
					   lockdep_is_held(&tasklist_lock));

	/* the parent mustn't be init and mustn't be a kernel thread */
	if (parent->pid <= 1 || !parent->mm)
		goto unlock;

	/* the parent must be single threaded */
	if (!thread_group_empty(parent))
		goto unlock;

	/* the parent and the child must have different session keyrings or
	 * there's no point */
	mycred = current_cred();
	pcred = __task_cred(parent);
	if (mycred == pcred ||
	    mycred->session_keyring == pcred->session_keyring) {
		ret = 0;
		goto unlock;
	}

	/* the parent must have the same effective ownership and mustn't be
	 * SUID/SGID */
	if (!uid_eq(pcred->uid,	 mycred->euid) ||
	    !uid_eq(pcred->euid, mycred->euid) ||
	    !uid_eq(pcred->suid, mycred->euid) ||
	    !gid_eq(pcred->gid,	 mycred->egid) ||
	    !gid_eq(pcred->egid, mycred->egid) ||
	    !gid_eq(pcred->sgid, mycred->egid))
		goto unlock;

	/* the keyrings must have the same UID */
	if ((pcred->session_keyring &&
	     !uid_eq(pcred->session_keyring->uid, mycred->euid)) ||
	    !uid_eq(mycred->session_keyring->uid, mycred->euid))
		goto unlock;

	/* cancel an already pending keyring replacement */
	oldwork = task_work_cancel(parent, key_change_session_keyring);

	/* the replacement session keyring is applied just prior to userspace
	 * restarting */
	ret = task_work_add(parent, newwork, TWA_RESUME);
	if (!ret)
		newwork = NULL;
unlock:
	write_unlock_irq(&tasklist_lock);
	rcu_read_unlock();
	if (oldwork)
		put_cred(container_of(oldwork, struct cred, rcu));
	if (newwork)
		put_cred(cred);
	return ret;

error_keyring:
	key_ref_put(keyring_r);
	return ret;
}

/*
 * Apply a restriction to a given keyring.
 *
 * The caller must have Setattr permission to change keyring restrictions.
 *
 * The requested type name may be a NULL pointer to reject all attempts
 * to link to the keyring.  In this case, _restriction must also be NULL.
 * Otherwise, both _type and _restriction must be non-NULL.
 *
 * Returns 0 if successful.
 */
long keyctl_restrict_keyring(key_serial_t id, const char __user *_type,
			     const char __user *_restriction)
{
	key_ref_t key_ref;
	char type[32];
	char *restriction = NULL;
	long ret;

	key_ref = lookup_user_key(id, 0, KEY_NEED_SETATTR);
	if (IS_ERR(key_ref))
		return PTR_ERR(key_ref);

	ret = -EINVAL;
	if (_type) {
		if (!_restriction)
			goto error;

		ret = key_get_type_from_user(type, _type, sizeof(type));
		if (ret < 0)
			goto error;

		restriction = strndup_user(_restriction, PAGE_SIZE);
		if (IS_ERR(restriction)) {
			ret = PTR_ERR(restriction);
			goto error;
		}
	} else {
		if (_restriction)
			goto error;
	}

	ret = keyring_restrict(key_ref, _type ? type : NULL, restriction);
	kfree(restriction);
error:
	key_ref_put(key_ref);
	return ret;
}

#ifdef CONFIG_KEY_NOTIFICATIONS
/*
 * Watch for changes to a key.
 *
 * The caller must have View permission to watch a key or keyring.
 */
long keyctl_watch_key(key_serial_t id, int watch_queue_fd, int watch_id)
{
	struct watch_queue *wqueue;
	struct watch_list *wlist = NULL;
	struct watch *watch = NULL;
	struct key *key;
	key_ref_t key_ref;
	long ret;

	if (watch_id < -1 || watch_id > 0xff)
		return -EINVAL;

	key_ref = lookup_user_key(id, KEY_LOOKUP_CREATE, KEY_NEED_VIEW);
	if (IS_ERR(key_ref))
		return PTR_ERR(key_ref);
	key = key_ref_to_ptr(key_ref);

	wqueue = get_watch_queue(watch_queue_fd);
	if (IS_ERR(wqueue)) {
		ret = PTR_ERR(wqueue);
		goto err_key;
	}

	if (watch_id >= 0) {
		ret = -ENOMEM;
		if (!key->watchers) {
			wlist = kzalloc(sizeof(*wlist), GFP_KERNEL);
			if (!wlist)
				goto err_wqueue;
			init_watch_list(wlist, NULL);
		}

		watch = kzalloc(sizeof(*watch), GFP_KERNEL);
		if (!watch)
			goto err_wlist;

		init_watch(watch, wqueue);
		watch->id	= key->serial;
		watch->info_id	= (u32)watch_id << WATCH_INFO_ID__SHIFT;

		ret = security_watch_key(key);
		if (ret < 0)
			goto err_watch;

		down_write(&key->sem);
		if (!key->watchers) {
			key->watchers = wlist;
			wlist = NULL;
		}

		ret = add_watch_to_object(watch, key->watchers);
		up_write(&key->sem);

		if (ret == 0)
			watch = NULL;
	} else {
		ret = -EBADSLT;
		if (key->watchers) {
			down_write(&key->sem);
			ret = remove_watch_from_object(key->watchers,
						       wqueue, key_serial(key),
						       false);
			up_write(&key->sem);
		}
	}

err_watch:
	kfree(watch);
err_wlist:
	kfree(wlist);
err_wqueue:
	put_watch_queue(wqueue);
err_key:
	key_put(key);
	return ret;
}
#endif /* CONFIG_KEY_NOTIFICATIONS */

/*
 * Get keyrings subsystem capabilities.
 */
long keyctl_capabilities(unsigned char __user *_buffer, size_t buflen)
{
	size_t size = buflen;

	if (size > 0) {
		if (size > sizeof(keyrings_capabilities))
			size = sizeof(keyrings_capabilities);
		if (copy_to_user(_buffer, keyrings_capabilities, size) != 0)
			return -EFAULT;
		if (size < buflen &&
		    clear_user(_buffer + size, buflen - size) != 0)
			return -EFAULT;
	}

	return sizeof(keyrings_capabilities);
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

	case KEYCTL_GET_PERSISTENT:
		return keyctl_get_persistent((uid_t)arg2, (key_serial_t)arg3);

	case KEYCTL_DH_COMPUTE:
		return keyctl_dh_compute((struct keyctl_dh_params __user *) arg2,
					 (char __user *) arg3, (size_t) arg4,
					 (struct keyctl_kdf_params __user *) arg5);

	case KEYCTL_RESTRICT_KEYRING:
		return keyctl_restrict_keyring((key_serial_t) arg2,
					       (const char __user *) arg3,
					       (const char __user *) arg4);

	case KEYCTL_PKEY_QUERY:
		if (arg3 != 0)
			return -EINVAL;
		return keyctl_pkey_query((key_serial_t)arg2,
					 (const char __user *)arg4,
					 (struct keyctl_pkey_query __user *)arg5);

	case KEYCTL_PKEY_ENCRYPT:
	case KEYCTL_PKEY_DECRYPT:
	case KEYCTL_PKEY_SIGN:
		return keyctl_pkey_e_d_s(
			option,
			(const struct keyctl_pkey_params __user *)arg2,
			(const char __user *)arg3,
			(const void __user *)arg4,
			(void __user *)arg5);

	case KEYCTL_PKEY_VERIFY:
		return keyctl_pkey_verify(
			(const struct keyctl_pkey_params __user *)arg2,
			(const char __user *)arg3,
			(const void __user *)arg4,
			(const void __user *)arg5);

	case KEYCTL_MOVE:
		return keyctl_keyring_move((key_serial_t)arg2,
					   (key_serial_t)arg3,
					   (key_serial_t)arg4,
					   (unsigned int)arg5);

	case KEYCTL_CAPABILITIES:
		return keyctl_capabilities((unsigned char __user *)arg2, (size_t)arg3);

	case KEYCTL_WATCH_KEY:
		return keyctl_watch_key((key_serial_t)arg2, (int)arg3, (int)arg4);

	default:
		return -EOPNOTSUPP;
	}
}
