/* key-ui.h: key userspace interface stuff
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
	struct rcu_head	rcu;		/* RCU deletion hook */
	unsigned short	maxkeys;	/* max keys this list can hold */
	unsigned short	nkeys;		/* number of keys currently held */
	unsigned short	delkey;		/* key to be unlinked by RCU */
	struct key	*keys[0];
};

/*
 * check to see whether permission is granted to use a key in the desired way
 */
extern int key_task_permission(const key_ref_t key_ref,
			       struct task_struct *context,
			       key_perm_t perm);

static inline int key_permission(const key_ref_t key_ref, key_perm_t perm)
{
	return key_task_permission(key_ref, current, perm);
}

extern key_ref_t lookup_user_key(struct task_struct *context,
				 key_serial_t id, int create, int partial,
				 key_perm_t perm);

extern long join_session_keyring(const char *name);

extern struct key_type *key_type_lookup(const char *type);
extern void key_type_put(struct key_type *ktype);

#define key_negative_timeout	60	/* default timeout on a negative key's existence */


#endif /* _LINUX_KEY_UI_H */
