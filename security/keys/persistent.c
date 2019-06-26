/* General persistent per-UID keyrings register
 *
 * Copyright (C) 2013 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/user_namespace.h>
#include <linux/cred.h>

#include "internal.h"

unsigned persistent_keyring_expiry = 3 * 24 * 3600; /* Expire after 3 days of non-use */

/*
 * Create the persistent keyring register for the current user namespace.
 *
 * Called with the namespace's sem locked for writing.
 */
static int key_create_persistent_register(struct user_namespace *ns)
{
	struct key *reg = keyring_alloc(".persistent_register",
					KUIDT_INIT(0), KGIDT_INIT(0),
					current_cred(),
					((KEY_POS_ALL & ~KEY_POS_SETATTR) |
					 KEY_USR_VIEW | KEY_USR_READ),
					KEY_ALLOC_NOT_IN_QUOTA, NULL, NULL);
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	ns->persistent_keyring_register = reg;
	return 0;
}

/*
 * Create the persistent keyring for the specified user.
 *
 * Called with the namespace's sem locked for writing.
 */
static key_ref_t key_create_persistent(struct user_namespace *ns, kuid_t uid,
				       struct keyring_index_key *index_key)
{
	struct key *persistent;
	key_ref_t reg_ref, persistent_ref;

	if (!ns->persistent_keyring_register) {
		long err = key_create_persistent_register(ns);
		if (err < 0)
			return ERR_PTR(err);
	} else {
		reg_ref = make_key_ref(ns->persistent_keyring_register, true);
		persistent_ref = find_key_to_update(reg_ref, index_key);
		if (persistent_ref)
			return persistent_ref;
	}

	persistent = keyring_alloc(index_key->description,
				   uid, INVALID_GID, current_cred(),
				   ((KEY_POS_ALL & ~KEY_POS_SETATTR) |
				    KEY_USR_VIEW | KEY_USR_READ),
				   KEY_ALLOC_NOT_IN_QUOTA, NULL,
				   ns->persistent_keyring_register);
	if (IS_ERR(persistent))
		return ERR_CAST(persistent);

	return make_key_ref(persistent, true);
}

/*
 * Get the persistent keyring for a specific UID and link it to the nominated
 * keyring.
 */
static long key_get_persistent(struct user_namespace *ns, kuid_t uid,
			       key_ref_t dest_ref)
{
	struct keyring_index_key index_key;
	struct key *persistent;
	key_ref_t reg_ref, persistent_ref;
	char buf[32];
	long ret;

	/* Look in the register if it exists */
	index_key.type = &key_type_keyring;
	index_key.description = buf;
	index_key.desc_len = sprintf(buf, "_persistent.%u", from_kuid(ns, uid));
	key_set_index_key(&index_key);

	if (ns->persistent_keyring_register) {
		reg_ref = make_key_ref(ns->persistent_keyring_register, true);
		down_read(&ns->keyring_sem);
		persistent_ref = find_key_to_update(reg_ref, &index_key);
		up_read(&ns->keyring_sem);

		if (persistent_ref)
			goto found;
	}

	/* It wasn't in the register, so we'll need to create it.  We might
	 * also need to create the register.
	 */
	down_write(&ns->keyring_sem);
	persistent_ref = key_create_persistent(ns, uid, &index_key);
	up_write(&ns->keyring_sem);
	if (!IS_ERR(persistent_ref))
		goto found;

	return PTR_ERR(persistent_ref);

found:
	ret = key_task_permission(persistent_ref, current_cred(), KEY_NEED_LINK);
	if (ret == 0) {
		persistent = key_ref_to_ptr(persistent_ref);
		ret = key_link(key_ref_to_ptr(dest_ref), persistent);
		if (ret == 0) {
			key_set_timeout(persistent, persistent_keyring_expiry);
			ret = persistent->serial;
		}
	}

	key_ref_put(persistent_ref);
	return ret;
}

/*
 * Get the persistent keyring for a specific UID and link it to the nominated
 * keyring.
 */
long keyctl_get_persistent(uid_t _uid, key_serial_t destid)
{
	struct user_namespace *ns = current_user_ns();
	key_ref_t dest_ref;
	kuid_t uid;
	long ret;

	/* -1 indicates the current user */
	if (_uid == (uid_t)-1) {
		uid = current_uid();
	} else {
		uid = make_kuid(ns, _uid);
		if (!uid_valid(uid))
			return -EINVAL;

		/* You can only see your own persistent cache if you're not
		 * sufficiently privileged.
		 */
		if (!uid_eq(uid, current_uid()) &&
		    !uid_eq(uid, current_euid()) &&
		    !ns_capable(ns, CAP_SETUID))
			return -EPERM;
	}

	/* There must be a destination keyring */
	dest_ref = lookup_user_key(destid, KEY_LOOKUP_CREATE, KEY_NEED_WRITE);
	if (IS_ERR(dest_ref))
		return PTR_ERR(dest_ref);
	if (key_ref_to_ptr(dest_ref)->type != &key_type_keyring) {
		ret = -ENOTDIR;
		goto out_put_dest;
	}

	ret = key_get_persistent(ns, uid, dest_ref);

out_put_dest:
	key_ref_put(dest_ref);
	return ret;
}
