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

/* Function type. */
enum safesetid_whitelist_file_write_type {
	SAFESETID_WHITELIST_ADD, /* Add whitelist policy. */
	SAFESETID_WHITELIST_FLUSH, /* Flush whitelist policies. */
};

enum sid_policy_type {
	SIDPOL_DEFAULT, /* source ID is unaffected by policy */
	SIDPOL_CONSTRAINED, /* source ID is affected by policy */
	SIDPOL_ALLOWED /* target ID explicitly allowed */
};

/*
 * Hash table entry to store safesetid policy signifying that 'src_uid'
 * can setid to 'dst_uid'.
 */
struct entry {
	struct hlist_node next;
	struct hlist_node dlist; /* for deletion cleanup */
	kuid_t src_uid;
	kuid_t dst_uid;
};

/* Add entry to safesetid whitelist to allow 'parent' to setid to 'child'. */
int add_safesetid_whitelist_entry(kuid_t parent, kuid_t child);

void flush_safesetid_whitelist_entries(void);

#endif /* _SAFESETID_H */
