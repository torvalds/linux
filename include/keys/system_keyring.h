/* System keyring containing trusted public keys.
 *
 * Copyright (C) 2013 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef _KEYS_SYSTEM_KEYRING_H
#define _KEYS_SYSTEM_KEYRING_H

#ifdef CONFIG_SYSTEM_TRUSTED_KEYRING

#include <linux/key.h>
#include <crypto/public_key.h>

extern struct key *system_trusted_keyring;
static inline struct key *get_system_trusted_keyring(void)
{
	return system_trusted_keyring;
}
#else
static inline struct key *get_system_trusted_keyring(void)
{
	return NULL;
}
#endif

#ifdef CONFIG_SYSTEM_DATA_VERIFICATION
extern int system_verify_data(const void *data, unsigned long len,
			      const void *raw_pkcs7, size_t pkcs7_len,
			      enum key_being_used_for usage);
#endif

#ifdef CONFIG_IMA_MOK_KEYRING
extern struct key *ima_mok_keyring;
extern struct key *ima_blacklist_keyring;

static inline struct key *get_ima_mok_keyring(void)
{
	return ima_mok_keyring;
}
static inline struct key *get_ima_blacklist_keyring(void)
{
	return ima_blacklist_keyring;
}
#else
static inline struct key *get_ima_mok_keyring(void)
{
	return NULL;
}
static inline struct key *get_ima_blacklist_keyring(void)
{
	return NULL;
}
#endif /* CONFIG_IMA_MOK_KEYRING */


#endif /* _KEYS_SYSTEM_KEYRING_H */
