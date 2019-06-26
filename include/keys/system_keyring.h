/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System keyring containing trusted public keys.
 *
 * Copyright (C) 2013 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#ifndef _KEYS_SYSTEM_KEYRING_H
#define _KEYS_SYSTEM_KEYRING_H

#include <linux/key.h>

#ifdef CONFIG_SYSTEM_TRUSTED_KEYRING

extern int restrict_link_by_builtin_trusted(struct key *keyring,
					    const struct key_type *type,
					    const union key_payload *payload,
					    struct key *restriction_key);

#else
#define restrict_link_by_builtin_trusted restrict_link_reject
#endif

#ifdef CONFIG_SECONDARY_TRUSTED_KEYRING
extern int restrict_link_by_builtin_and_secondary_trusted(
	struct key *keyring,
	const struct key_type *type,
	const union key_payload *payload,
	struct key *restriction_key);
#else
#define restrict_link_by_builtin_and_secondary_trusted restrict_link_by_builtin_trusted
#endif

#ifdef CONFIG_SYSTEM_BLACKLIST_KEYRING
extern int mark_hash_blacklisted(const char *hash);
extern int is_hash_blacklisted(const u8 *hash, size_t hash_len,
			       const char *type);
#else
static inline int is_hash_blacklisted(const u8 *hash, size_t hash_len,
				      const char *type)
{
	return 0;
}
#endif

#ifdef CONFIG_IMA_BLACKLIST_KEYRING
extern struct key *ima_blacklist_keyring;

static inline struct key *get_ima_blacklist_keyring(void)
{
	return ima_blacklist_keyring;
}
#else
static inline struct key *get_ima_blacklist_keyring(void)
{
	return NULL;
}
#endif /* CONFIG_IMA_BLACKLIST_KEYRING */

#if defined(CONFIG_INTEGRITY_PLATFORM_KEYRING) && \
	defined(CONFIG_SYSTEM_TRUSTED_KEYRING)
extern void __init set_platform_trusted_keys(struct key *keyring);
#else
static inline void set_platform_trusted_keys(struct key *keyring)
{
}
#endif

#endif /* _KEYS_SYSTEM_KEYRING_H */
