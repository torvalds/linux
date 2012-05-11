/* Keyring key type
 *
 * Copyright (C) 2008 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _KEYS_KEYRING_TYPE_H
#define _KEYS_KEYRING_TYPE_H

#include <linux/key.h>
#include <linux/rcupdate.h>

/*
 * the keyring payload contains a list of the keys to which the keyring is
 * subscribed
 */
struct keyring_list {
	struct rcu_head	rcu;		/* RCU deletion hook */
	unsigned short	maxkeys;	/* max keys this list can hold */
	unsigned short	nkeys;		/* number of keys currently held */
	unsigned short	delkey;		/* key to be unlinked by RCU */
	struct key __rcu *keys[0];
};


#endif /* _KEYS_KEYRING_TYPE_H */
