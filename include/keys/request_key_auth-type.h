/* SPDX-License-Identifier: GPL-2.0-or-later */
/* request_key authorisation token key type
 *
 * Copyright (C) 2005 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#ifndef _KEYS_REQUEST_KEY_AUTH_TYPE_H
#define _KEYS_REQUEST_KEY_AUTH_TYPE_H

#include <linux/key.h>

/*
 * Authorisation record for request_key().
 */
struct request_key_auth {
	struct key		*target_key;
	struct key		*dest_keyring;
	const struct cred	*cred;
	void			*callout_info;
	size_t			callout_len;
	pid_t			pid;
	char			op[8];
} __randomize_layout;

static inline struct request_key_auth *get_request_key_auth(const struct key *key)
{
	return key->payload.data[0];
}


#endif /* _KEYS_REQUEST_KEY_AUTH_TYPE_H */
