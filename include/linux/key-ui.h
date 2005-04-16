/* key-ui.h: key userspace interface stuff for use by keyfs
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_KEY_UI_H
#define _LINUX_KEY_UI_H

#include <linux/key.h>

/* the key tree */
extern struct rb_root key_serial_tree;
extern spinlock_t key_serial_lock;

/* required permissions */
#define	KEY_VIEW	0x01	/* require permission to view attributes */
#define	KEY_READ	0x02	/* require permission to read content */
#define	KEY_WRITE	0x04	/* require permission to update / modify */
#define	KEY_SEARCH	0x08	/* require permission to search (keyring) or find (key) */
#define	KEY_LINK	0x10	/* require permission to link */
#define	KEY_ALL		0x1f	/* all the above permissions */

/*
 * the keyring payload contains a list of the keys to which the keyring is
 * subscribed
 */
struct keyring_list {
	unsigned	maxkeys;	/* max keys this list can hold */
	unsigned	nkeys;		/* number of keys currently held */
	struct key	*keys[0];
};


/*
 * check to see whether permission is granted to use a key in the desired way
 */
static inline int key_permission(const struct key *key, key_perm_t perm)
{
	key_perm_t kperm;

	if (key->uid == current->fsuid)
		kperm = key->perm >> 16;
	else if (key->gid != -1 &&
		 key->perm & KEY_GRP_ALL &&
		 in_group_p(key->gid)
		 )
		kperm = key->perm >> 8;
	else
		kperm = key->perm;

	kperm = kperm & perm & KEY_ALL;

	return kperm == perm;
}

/*
 * check to see whether permission is granted to use a key in at least one of
 * the desired ways
 */
static inline int key_any_permission(const struct key *key, key_perm_t perm)
{
	key_perm_t kperm;

	if (key->uid == current->fsuid)
		kperm = key->perm >> 16;
	else if (key->gid != -1 &&
		 key->perm & KEY_GRP_ALL &&
		 in_group_p(key->gid)
		 )
		kperm = key->perm >> 8;
	else
		kperm = key->perm;

	kperm = kperm & perm & KEY_ALL;

	return kperm != 0;
}


extern struct key *lookup_user_key(key_serial_t id, int create, int part,
				   key_perm_t perm);

extern long join_session_keyring(const char *name);

extern struct key_type *key_type_lookup(const char *type);
extern void key_type_put(struct key_type *ktype);

#define key_negative_timeout	60	/* default timeout on a negative key's existence */


#endif /* _LINUX_KEY_UI_H */
