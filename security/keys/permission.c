/* permission.c: key permission determination
 *
 * Copyright (C) 2005 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/security.h>
#include "internal.h"

/*****************************************************************************/
/**
 * key_task_permission - Check a key can be used
 * @key_ref: The key to check
 * @cred: The credentials to use
 * @perm: The permissions to check for
 *
 * Check to see whether permission is granted to use a key in the desired way,
 * but permit the security modules to override.
 *
 * The caller must hold either a ref on cred or must hold the RCU readlock or a
 * spinlock.
 */
int key_task_permission(const key_ref_t key_ref, const struct cred *cred,
			key_perm_t perm)
{
	struct key *key;
	key_perm_t kperm;
	int ret;

	key = key_ref_to_ptr(key_ref);

	if (key->user->user_ns != cred->user->user_ns)
		goto use_other_perms;

	/* use the second 8-bits of permissions for keys the caller owns */
	if (key->uid == cred->fsuid) {
		kperm = key->perm >> 16;
		goto use_these_perms;
	}

	/* use the third 8-bits of permissions for keys the caller has a group
	 * membership in common with */
	if (key->gid != -1 && key->perm & KEY_GRP_ALL) {
		if (key->gid == cred->fsgid) {
			kperm = key->perm >> 8;
			goto use_these_perms;
		}

		ret = groups_search(cred->group_info, key->gid);
		if (ret) {
			kperm = key->perm >> 8;
			goto use_these_perms;
		}
	}

use_other_perms:

	/* otherwise use the least-significant 8-bits */
	kperm = key->perm;

use_these_perms:

	/* use the top 8-bits of permissions for keys the caller possesses
	 * - possessor permissions are additive with other permissions
	 */
	if (is_key_possessed(key_ref))
		kperm |= key->perm >> 24;

	kperm = kperm & perm & KEY_ALL;

	if (kperm != perm)
		return -EACCES;

	/* let LSM be the final arbiter */
	return security_key_permission(key_ref, cred, perm);

} /* end key_task_permission() */

EXPORT_SYMBOL(key_task_permission);

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
		if (test_bit(KEY_FLAG_REVOKED, &key->flags) ||
		    test_bit(KEY_FLAG_DEAD, &key->flags))
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
