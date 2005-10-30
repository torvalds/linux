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
/*
 * check to see whether permission is granted to use a key in the desired way,
 * but permit the security modules to override
 */
int key_task_permission(const key_ref_t key_ref,
			struct task_struct *context,
			key_perm_t perm)
{
	struct key *key;
	key_perm_t kperm;
	int ret;

	key = key_ref_to_ptr(key_ref);

	/* use the second 8-bits of permissions for keys the caller owns */
	if (key->uid == context->fsuid) {
		kperm = key->perm >> 16;
		goto use_these_perms;
	}

	/* use the third 8-bits of permissions for keys the caller has a group
	 * membership in common with */
	if (key->gid != -1 && key->perm & KEY_GRP_ALL) {
		if (key->gid == context->fsgid) {
			kperm = key->perm >> 8;
			goto use_these_perms;
		}

		task_lock(context);
		ret = groups_search(context->group_info, key->gid);
		task_unlock(context);

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

	kperm = kperm & perm & KEY_ALL;

	if (kperm != perm)
		return -EACCES;

	/* let LSM be the final arbiter */
	return security_key_permission(key_ref, context, perm);

} /* end key_task_permission() */

EXPORT_SYMBOL(key_task_permission);
