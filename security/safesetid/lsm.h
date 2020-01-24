/* SPDX-License-Identifier: GPL-2.0 */
/*
 * SafeSetID Linux Security Module
 *
 * Author: Micah Morton <mortonm@chromium.org>
 *
 * Copyright (C) 2018 The Chromium OS Authors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 */
#ifndef _SAFESETID_H
#define _SAFESETID_H

#include <linux/types.h>
#include <linux/uidgid.h>
#include <linux/hashtable.h>

/* Flag indicating whether initialization completed */
extern int safesetid_initialized;

enum sid_policy_type {
	SIDPOL_DEFAULT, /* source ID is unaffected by policy */
	SIDPOL_CONSTRAINED, /* source ID is affected by policy */
	SIDPOL_ALLOWED /* target ID explicitly allowed */
};

/*
 * Hash table entry to store safesetid policy signifying that 'src_uid'
 * can setuid to 'dst_uid'.
 */
struct setuid_rule {
	struct hlist_node next;
	kuid_t src_uid;
	kuid_t dst_uid;
};

#define SETID_HASH_BITS 8 /* 256 buckets in hash table */

struct setuid_ruleset {
	DECLARE_HASHTABLE(rules, SETID_HASH_BITS);
	char *policy_str;
	struct rcu_head rcu;
};

enum sid_policy_type _setuid_policy_lookup(struct setuid_ruleset *policy,
		kuid_t src, kuid_t dst);

extern struct setuid_ruleset __rcu *safesetid_setuid_rules;

#endif /* _SAFESETID_H */
