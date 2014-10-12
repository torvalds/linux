/* user-type.h: User-defined key type
 *
 * Copyright (C) 2005 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _KEYS_USER_TYPE_H
#define _KEYS_USER_TYPE_H

#include <linux/key.h>
#include <linux/rcupdate.h>

/*****************************************************************************/
/*
 * the payload for a key of type "user" or "logon"
 * - once filled in and attached to a key:
 *   - the payload struct is invariant may not be changed, only replaced
 *   - the payload must be read with RCU procedures or with the key semaphore
 *     held
 *   - the payload may only be replaced with the key semaphore write-locked
 * - the key's data length is the size of the actual data, not including the
 *   payload wrapper
 */
struct user_key_payload {
	struct rcu_head	rcu;		/* RCU destructor */
	unsigned short	datalen;	/* length of this data */
	char		data[0];	/* actual data */
};

extern struct key_type key_type_user;
extern struct key_type key_type_logon;

struct key_preparsed_payload;

extern int user_preparse(struct key_preparsed_payload *prep);
extern void user_free_preparse(struct key_preparsed_payload *prep);
extern int user_update(struct key *key, struct key_preparsed_payload *prep);
extern void user_revoke(struct key *key);
extern void user_destroy(struct key *key);
extern void user_describe(const struct key *user, struct seq_file *m);
extern long user_read(const struct key *key,
		      char __user *buffer, size_t buflen);


#endif /* _KEYS_USER_TYPE_H */
