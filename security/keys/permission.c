/* Key permission checking
 *
 * Copyright (C) 2005 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/export.h>
#include <linux/security.h>
#include <linux/user_namespace.h>
#include <linux/uaccess.h>
#include "internal.h"

struct key_acl default_key_acl = {
	.usage	= REFCOUNT_INIT(1),
	.nr_ace	= 2,
	.possessor_viewable = true,
	.aces = {
		KEY_POSSESSOR_ACE(KEY_ACE__PERMS & ~KEY_ACE_JOIN),
		KEY_OWNER_ACE(KEY_ACE_VIEW),
	}
};
EXPORT_SYMBOL(default_key_acl);

struct key_acl joinable_keyring_acl = {
	.usage	= REFCOUNT_INIT(1),
	.nr_ace	= 2,
	.possessor_viewable = true,
	.aces	= {
		KEY_POSSESSOR_ACE(KEY_ACE__PERMS & ~KEY_ACE_JOIN),
		KEY_OWNER_ACE(KEY_ACE_VIEW | KEY_ACE_READ | KEY_ACE_LINK | KEY_ACE_JOIN),
	}
};
EXPORT_SYMBOL(joinable_keyring_acl);

struct key_acl internal_key_acl = {
	.usage	= REFCOUNT_INIT(1),
	.nr_ace	= 2,
	.aces = {
		KEY_POSSESSOR_ACE(KEY_ACE_SEARCH),
		KEY_OWNER_ACE(KEY_ACE_VIEW | KEY_ACE_READ | KEY_ACE_SEARCH),
	}
};
EXPORT_SYMBOL(internal_key_acl);

struct key_acl internal_keyring_acl = {
	.usage	= REFCOUNT_INIT(1),
	.nr_ace	= 2,
	.aces = {
		KEY_POSSESSOR_ACE(KEY_ACE_SEARCH),
		KEY_OWNER_ACE(KEY_ACE_VIEW | KEY_ACE_READ | KEY_ACE_SEARCH),
	}
};
EXPORT_SYMBOL(internal_keyring_acl);

struct key_acl internal_writable_keyring_acl = {
	.usage	= REFCOUNT_INIT(1),
	.nr_ace	= 2,
	.aces = {
		KEY_POSSESSOR_ACE(KEY_ACE_SEARCH | KEY_ACE_WRITE),
		KEY_OWNER_ACE(KEY_ACE_VIEW | KEY_ACE_READ | KEY_ACE_WRITE | KEY_ACE_SEARCH),
	}
};
EXPORT_SYMBOL(internal_writable_keyring_acl);

/**
 * key_task_permission - Check a key can be used
 * @key_ref: The key to check.
 * @cred: The credentials to use.
 * @desired_perm: The permission to check for.
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
			unsigned int desired_perm)
{
	const struct key_acl *acl;
	const struct key *key;
	unsigned int allow = 0;
	int i;

	BUILD_BUG_ON(KEY_NEED_VIEW	!= KEY_ACE_VIEW		||
		     KEY_NEED_READ	!= KEY_ACE_READ		||
		     KEY_NEED_WRITE	!= KEY_ACE_WRITE	||
		     KEY_NEED_SEARCH	!= KEY_ACE_SEARCH	||
		     KEY_NEED_LINK	!= KEY_ACE_LINK		||
		     KEY_NEED_SETSEC	!= KEY_ACE_SET_SECURITY	||
		     KEY_NEED_INVAL	!= KEY_ACE_INVAL	||
		     KEY_NEED_REVOKE	!= KEY_ACE_REVOKE	||
		     KEY_NEED_JOIN	!= KEY_ACE_JOIN		||
		     KEY_NEED_CLEAR	!= KEY_ACE_CLEAR);

	key = key_ref_to_ptr(key_ref);

	rcu_read_lock();

	acl = rcu_dereference(key->acl);
	if (!acl || acl->nr_ace == 0)
		goto no_access_rcu;

	for (i = 0; i < acl->nr_ace; i++) {
		const struct key_ace *ace = &acl->aces[i];

		switch (ace->type) {
		case KEY_ACE_SUBJ_STANDARD:
			switch (ace->subject_id) {
			case KEY_ACE_POSSESSOR:
				if (is_key_possessed(key_ref))
					allow |= ace->perm;
				break;
			case KEY_ACE_OWNER:
				if (uid_eq(key->uid, cred->fsuid))
					allow |= ace->perm;
				break;
			case KEY_ACE_GROUP:
				if (gid_valid(key->gid)) {
					if (gid_eq(key->gid, cred->fsgid))
						allow |= ace->perm;
					else if (groups_search(cred->group_info, key->gid))
						allow |= ace->perm;
				}
				break;
			case KEY_ACE_EVERYONE:
				allow |= ace->perm;
				break;
			}
			break;
		}
	}

	rcu_read_unlock();

	if (!(allow & desired_perm))
		goto no_access;

	return security_key_permission(key_ref, cred, desired_perm);

no_access_rcu:
	rcu_read_unlock();
no_access:
	return -EACCES;
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

/*
 * Roughly render an ACL to an old-style permissions mask.  We cannot
 * accurately render what the ACL, particularly if it has ACEs that represent
 * subjects outside of { poss, user, group, other }.
 */
unsigned int key_acl_to_perm(const struct key_acl *acl)
{
	unsigned int perm = 0, tperm;
	int i;

	BUILD_BUG_ON(KEY_OTH_VIEW	!= KEY_ACE_VIEW		||
		     KEY_OTH_READ	!= KEY_ACE_READ		||
		     KEY_OTH_WRITE	!= KEY_ACE_WRITE	||
		     KEY_OTH_SEARCH	!= KEY_ACE_SEARCH	||
		     KEY_OTH_LINK	!= KEY_ACE_LINK		||
		     KEY_OTH_SETATTR	!= KEY_ACE_SET_SECURITY);

	if (!acl || acl->nr_ace == 0)
		return 0;

	for (i = 0; i < acl->nr_ace; i++) {
		const struct key_ace *ace = &acl->aces[i];

		switch (ace->type) {
		case KEY_ACE_SUBJ_STANDARD:
			tperm = ace->perm & KEY_OTH_ALL;

			/* Invalidation and joining were allowed by SEARCH */
			if (ace->perm & (KEY_ACE_INVAL | KEY_ACE_JOIN))
				tperm |= KEY_OTH_SEARCH;

			/* Revocation was allowed by either SETATTR or WRITE */
			if ((ace->perm & KEY_ACE_REVOKE) && !(tperm & KEY_OTH_SETATTR))
				tperm |= KEY_OTH_WRITE;

			/* Clearing was allowed by WRITE */
			if (ace->perm & KEY_ACE_CLEAR)
				tperm |= KEY_OTH_WRITE;

			switch (ace->subject_id) {
			case KEY_ACE_POSSESSOR:
				perm |= tperm << 24;
				break;
			case KEY_ACE_OWNER:
				perm |= tperm << 16;
				break;
			case KEY_ACE_GROUP:
				perm |= tperm << 8;
				break;
			case KEY_ACE_EVERYONE:
				perm |= tperm << 0;
				break;
			}
		}
	}

	return perm;
}

/*
 * Destroy a key's ACL.
 */
void key_put_acl(struct key_acl *acl)
{
	if (acl && refcount_dec_and_test(&acl->usage))
		kfree_rcu(acl, rcu);
}

/*
 * Try to set the ACL.  This either attaches or discards the proposed ACL.
 */
long key_set_acl(struct key *key, struct key_acl *acl)
{
	int i;

	/* If we're not the sysadmin, we can only change a key that we own. */
	if (!capable(CAP_SYS_ADMIN) && !uid_eq(key->uid, current_fsuid())) {
		key_put_acl(acl);
		return -EACCES;
	}

	for (i = 0; i < acl->nr_ace; i++) {
		const struct key_ace *ace = &acl->aces[i];
		if (ace->type == KEY_ACE_SUBJ_STANDARD &&
		    ace->subject_id == KEY_ACE_POSSESSOR) {
			if (ace->perm & KEY_ACE_VIEW)
				acl->possessor_viewable = true;
			break;
		}
	}

	rcu_swap_protected(key->acl, acl, lockdep_is_held(&key->sem));
	key_put_acl(acl);
	return 0;
}
