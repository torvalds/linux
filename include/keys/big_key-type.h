/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Big capacity key type.
 *
 * Copyright (C) 2013 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#ifndef _KEYS_BIG_KEY_TYPE_H
#define _KEYS_BIG_KEY_TYPE_H

#include <linux/key-type.h>

extern struct key_type key_type_big_key;

extern int big_key_preparse(struct key_preparsed_payload *prep);
extern void big_key_free_preparse(struct key_preparsed_payload *prep);
extern void big_key_revoke(struct key *key);
extern void big_key_destroy(struct key *key);
extern void big_key_describe(const struct key *big_key, struct seq_file *m);
extern long big_key_read(const struct key *key, char __user *buffer, size_t buflen);

#endif /* _KEYS_BIG_KEY_TYPE_H */
