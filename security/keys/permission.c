// SPDX-License-Identifier: GPL-2.0-or-later
/* Key permission checking
 *
 * Copyright (C) 2005 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/export.h>
#include <linux/security.h>
#include "internal.h"

/**
 * key_task_permission - Check a key can be used
 * @key_ref: The key to check.
 * @cred: The credentials to use.
 * @need_perm: The permission required.
 *
 * Check to see whether permission is granted to use a key in the desired way,
 * but permit the security modules to override.
 *
 * The caller must hold either a ref on cred or must hold the RCU readlock.
 *
 * Returns 0 if successful, -EACCES if access is denied based on the
 * permissions bits or the LSM check.
 */
int key_task_permission(const key_ref_t key_ref, const struct cred *cred,
			enum key_need_perm need_perm)
{
	struct key *key;
	key_perm_t kperm, mask;
	int ret;

	switch (need_perm) {
	default:
		WARN_ON(1);
		return -EACCES;
	case KEY_NEED_UNLINK:
	case KEY_SYSADMIN_OVERRIDE:
	case KEY_AUTHTOKEN_OVERRIDE:
	case KEY_DEFER_PERM_CHECK:
		goto lsm;

	case KEY_NEED_VIEW:	mask = KEY_OTH_VIEW;	break;
	case KEY_NEED_READ:	mask = KEY_OTH_READ;	break;
	case KEY_NEED_WRITE:	mask = KEY_OTH_WRITE;	break;
	case KEY_NEED_SEARCH:	mask = KEY_OTH_SEARCH;	break;
	case KEY_NEED_LINK:	mask = KEY_OTH_LINK;	break;
	case KEY_NEED_SETATTR:	mask = KEY_OTH_SETATTR;	break;
	}

	key = key_ref_to_ptr(key_ref);

	/* use the second 8-bits of permissions for keys the caller owns */
	if (uid_eq(key->uid, cred->fsuid)) {
		kperm = key->perm >> 16;
		goto use_these_perms;
	}

	/* use the third 8-bits of permissions for keys the caller has a group
	 * membership in common with */
	if (gid_valid(key->gid) && key->perm & KEY_GRP_ALL) {
		if (gid_eq(key->gid, cred->fsgid)) {
			kperm = key->perm >> 8;
			goto use_these_perms;
		}

		ret = groups_search(cred->group_info, key->gid);
		if (ret) {
			kperm = key->perm >> 8;
			goto use_these_perms;
		}
	}

	/* otherwise use the least-significant 8-bits */
	kperm = key->perm;

use_these_perms:

	/* use the top 8-bits of permissions for keys the caller possesses
	 * - possessor permissions are additive with other permissions
	 */
	if (is_key_possessed(key_ref))
		kperm |= key->perm >> 24;

	if ((kperm & mask) != mask)
		return -EACCES;

	/* let LSM be the final arbiter */
lsm:
	return security_key_permission(key_ref, cred, need_perm);
}
EXPORT_SYMBOL(key_task_permission);

/**
 * key_validate - Validate a key.
 * @key: The key to be validated.
 *
 * Check that a key is valid, returning 0 if the key is okay, -ENOKEY if the
 * key is invalidated, -EKEYREVOKED if the key's type has been removed or if
 * the key has been revoked or -EKEYEXPIRED if the key has expired.
 */
int key_validate(const struct key *key)
{
	unsigned long flags = READ_ONCE(key->flags);
	time64_t expiry = READ_ONCE(key->expiry);

	if (flags & (1 << KEY_FLAG_INVALIDATED))
		return -ENOKEY;

	/* check it's still accessible */
	if (flags & ((1 << KEY_FLAG_REVOKED) |
		     (1 << KEY_FLAG_DEAD)))
		return -EKEYREVOKED;

	/* check it hasn't expired */
	if (expiry) {
		if (ktime_get_real_seconds() >= expiry)
			return -EKEYEXPIRED;
	}

	return 0;
}
EXPORT_SYMBOL(key_validate);
