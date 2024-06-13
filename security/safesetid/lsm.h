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
extern int safesetid_initialized __initdata;

enum sid_policy_type {
	SIDPOL_DEFAULT, /* source ID is unaffected by policy */
	SIDPOL_CONSTRAINED, /* source ID is affected by policy */
	SIDPOL_ALLOWED /* target ID explicitly allowed */
};

typedef union {
	kuid_t uid;
	kgid_t gid;
} kid_t;

enum setid_type {
	UID,
	GID
};

/*
 * Hash table entry to store safesetid policy signifying that 'src_id'
 * can set*id to 'dst_id'.
 */
struct setid_rule {
	struct hlist_node next;
	kid_t src_id;
	kid_t dst_id;

	/* Flag to signal if rule is for UID's or GID's */
	enum setid_type type;
};

#define SETID_HASH_BITS 8 /* 256 buckets in hash table */

/* Extension of INVALID_UID/INVALID_GID for kid_t type */
#define INVALID_ID (kid_t){.uid = INVALID_UID}

struct setid_ruleset {
	DECLARE_HASHTABLE(rules, SETID_HASH_BITS);
	char *policy_str;
	struct rcu_head rcu;

	//Flag to signal if ruleset is for UID's or GID's
	enum setid_type type;
};

enum sid_policy_type _setid_policy_lookup(struct setid_ruleset *policy,
		kid_t src, kid_t dst);

extern struct setid_ruleset __rcu *safesetid_setuid_rules;
extern struct setid_ruleset __rcu *safesetid_setgid_rules;

#endif /* _SAFESETID_H */
