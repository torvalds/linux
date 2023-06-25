// SPDX-License-Identifier: GPL-2.0-only
/*
 * AppArmor security module
 *
 * This file contains AppArmor functions for unpacking policy loaded
 * from userspace.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2022 Canonical Ltd.
 *
 * Code to provide backwards compatibility with older policy versions,
 * by converting/mapping older policy formats into the newer internal
 * formats.
 */

#include <linux/ctype.h>
#include <linux/errno.h>

#include "include/lib.h"
#include "include/policy_unpack.h"
#include "include/policy_compat.h"

/* remap old accept table embedded permissions to separate permission table */
static u32 dfa_map_xindex(u16 mask)
{
	u16 old_index = (mask >> 10) & 0xf;
	u32 index = 0;

	if (mask & 0x100)
		index |= AA_X_UNSAFE;
	if (mask & 0x200)
		index |= AA_X_INHERIT;
	if (mask & 0x80)
		index |= AA_X_UNCONFINED;

	if (old_index == 1) {
		index |= AA_X_UNCONFINED;
	} else if (old_index == 2) {
		index |= AA_X_NAME;
	} else if (old_index == 3) {
		index |= AA_X_NAME | AA_X_CHILD;
	} else if (old_index) {
		index |= AA_X_TABLE;
		index |= old_index - 4;
	}

	return index;
}

/*
 * map old dfa inline permissions to new format
 */
#define dfa_user_allow(dfa, state) (((ACCEPT_TABLE(dfa)[state]) & 0x7f) | \
				    ((ACCEPT_TABLE(dfa)[state]) & 0x80000000))
#define dfa_user_xbits(dfa, state) (((ACCEPT_TABLE(dfa)[state]) >> 7) & 0x7f)
#define dfa_user_audit(dfa, state) ((ACCEPT_TABLE2(dfa)[state]) & 0x7f)
#define dfa_user_quiet(dfa, state) (((ACCEPT_TABLE2(dfa)[state]) >> 7) & 0x7f)
#define dfa_user_xindex(dfa, state) \
	(dfa_map_xindex(ACCEPT_TABLE(dfa)[state] & 0x3fff))

#define dfa_other_allow(dfa, state) ((((ACCEPT_TABLE(dfa)[state]) >> 14) & \
				      0x7f) |				\
				     ((ACCEPT_TABLE(dfa)[state]) & 0x80000000))
#define dfa_other_xbits(dfa, state) \
	((((ACCEPT_TABLE(dfa)[state]) >> 7) >> 14) & 0x7f)
#define dfa_other_audit(dfa, state) (((ACCEPT_TABLE2(dfa)[state]) >> 14) & 0x7f)
#define dfa_other_quiet(dfa, state) \
	((((ACCEPT_TABLE2(dfa)[state]) >> 7) >> 14) & 0x7f)
#define dfa_other_xindex(dfa, state) \
	dfa_map_xindex((ACCEPT_TABLE(dfa)[state] >> 14) & 0x3fff)

/**
 * map_old_perms - map old file perms layout to the new layout
 * @old: permission set in old mapping
 *
 * Returns: new permission mapping
 */
static u32 map_old_perms(u32 old)
{
	u32 new = old & 0xf;

	if (old & MAY_READ)
		new |= AA_MAY_GETATTR | AA_MAY_OPEN;
	if (old & MAY_WRITE)
		new |= AA_MAY_SETATTR | AA_MAY_CREATE | AA_MAY_DELETE |
		       AA_MAY_CHMOD | AA_MAY_CHOWN | AA_MAY_OPEN;
	if (old & 0x10)
		new |= AA_MAY_LINK;
	/* the old mapping lock and link_subset flags where overlaid
	 * and use was determined by part of a pair that they were in
	 */
	if (old & 0x20)
		new |= AA_MAY_LOCK | AA_LINK_SUBSET;
	if (old & 0x40)	/* AA_EXEC_MMAP */
		new |= AA_EXEC_MMAP;

	return new;
}

static void compute_fperms_allow(struct aa_perms *perms, struct aa_dfa *dfa,
				 aa_state_t state)
{
	perms->allow |= AA_MAY_GETATTR;

	/* change_profile wasn't determined by ownership in old mapping */
	if (ACCEPT_TABLE(dfa)[state] & 0x80000000)
		perms->allow |= AA_MAY_CHANGE_PROFILE;
	if (ACCEPT_TABLE(dfa)[state] & 0x40000000)
		perms->allow |= AA_MAY_ONEXEC;
}

static struct aa_perms compute_fperms_user(struct aa_dfa *dfa,
					   aa_state_t state)
{
	struct aa_perms perms = { };

	perms.allow = map_old_perms(dfa_user_allow(dfa, state));
	perms.audit = map_old_perms(dfa_user_audit(dfa, state));
	perms.quiet = map_old_perms(dfa_user_quiet(dfa, state));
	perms.xindex = dfa_user_xindex(dfa, state);

	compute_fperms_allow(&perms, dfa, state);

	return perms;
}

static struct aa_perms compute_fperms_other(struct aa_dfa *dfa,
					    aa_state_t state)
{
	struct aa_perms perms = { };

	perms.allow = map_old_perms(dfa_other_allow(dfa, state));
	perms.audit = map_old_perms(dfa_other_audit(dfa, state));
	perms.quiet = map_old_perms(dfa_other_quiet(dfa, state));
	perms.xindex = dfa_other_xindex(dfa, state);

	compute_fperms_allow(&perms, dfa, state);

	return perms;
}

/**
 * compute_fperms - convert dfa compressed perms to internal perms and store
 *		    them so they can be retrieved later.
 * @dfa: a dfa using fperms to remap to internal permissions
 * @size: the permission table size
 *
 * Returns: remapped perm table
 */
static struct aa_perms *compute_fperms(struct aa_dfa *dfa,
				       u32 *size)
{
	aa_state_t state;
	unsigned int state_count;
	struct aa_perms *table;

	AA_BUG(!dfa);

	state_count = dfa->tables[YYTD_ID_BASE]->td_lolen;
	/* DFAs are restricted from having a state_count of less than 2 */
	table = kvcalloc(state_count * 2, sizeof(struct aa_perms), GFP_KERNEL);
	if (!table)
		return NULL;
	*size = state_count * 2;

	for (state = 0; state < state_count; state++) {
		table[state * 2] = compute_fperms_user(dfa, state);
		table[state * 2 + 1] = compute_fperms_other(dfa, state);
	}

	return table;
}

static struct aa_perms *compute_xmatch_perms(struct aa_dfa *xmatch,
				      u32 *size)
{
	struct aa_perms *perms;
	int state;
	int state_count;

	AA_BUG(!xmatch);

	state_count = xmatch->tables[YYTD_ID_BASE]->td_lolen;
	/* DFAs are restricted from having a state_count of less than 2 */
	perms = kvcalloc(state_count, sizeof(struct aa_perms), GFP_KERNEL);
	if (!perms)
		return NULL;
	*size = state_count;

	/* zero init so skip the trap state (state == 0) */
	for (state = 1; state < state_count; state++)
		perms[state].allow = dfa_user_allow(xmatch, state);

	return perms;
}

static u32 map_other(u32 x)
{
	return ((x & 0x3) << 8) |	/* SETATTR/GETATTR */
		((x & 0x1c) << 18) |	/* ACCEPT/BIND/LISTEN */
		((x & 0x60) << 19);	/* SETOPT/GETOPT */
}

static u32 map_xbits(u32 x)
{
	return ((x & 0x1) << 7) |
		((x & 0x7e) << 9);
}

static struct aa_perms compute_perms_entry(struct aa_dfa *dfa,
					   aa_state_t state,
					   u32 version)
{
	struct aa_perms perms = { };

	perms.allow = dfa_user_allow(dfa, state);
	perms.audit = dfa_user_audit(dfa, state);
	perms.quiet = dfa_user_quiet(dfa, state);

	/*
	 * This mapping is convulated due to history.
	 * v1-v4: only file perms, which are handled by compute_fperms
	 * v5: added policydb which dropped user conditional to gain new
	 *     perm bits, but had to map around the xbits because the
	 *     userspace compiler was still munging them.
	 * v9: adds using the xbits in policydb because the compiler now
	 *     supports treating policydb permission bits different.
	 *     Unfortunately there is no way to force auditing on the
	 *     perms represented by the xbits
	 */
	perms.allow |= map_other(dfa_other_allow(dfa, state));
	if (VERSION_LE(version, v8))
		perms.allow |= AA_MAY_LOCK;
	else
		perms.allow |= map_xbits(dfa_user_xbits(dfa, state));

	/*
	 * for v5-v9 perm mapping in the policydb, the other set is used
	 * to extend the general perm set
	 */
	perms.audit |= map_other(dfa_other_audit(dfa, state));
	perms.quiet |= map_other(dfa_other_quiet(dfa, state));
	if (VERSION_GT(version, v8))
		perms.quiet |= map_xbits(dfa_other_xbits(dfa, state));

	return perms;
}

static struct aa_perms *compute_perms(struct aa_dfa *dfa, u32 version,
				      u32 *size)
{
	unsigned int state;
	unsigned int state_count;
	struct aa_perms *table;

	AA_BUG(!dfa);

	state_count = dfa->tables[YYTD_ID_BASE]->td_lolen;
	/* DFAs are restricted from having a state_count of less than 2 */
	table = kvcalloc(state_count, sizeof(struct aa_perms), GFP_KERNEL);
	if (!table)
		return NULL;
	*size = state_count;

	/* zero init so skip the trap state (state == 0) */
	for (state = 1; state < state_count; state++)
		table[state] = compute_perms_entry(dfa, state, version);

	return table;
}

/**
 * remap_dfa_accept - remap old dfa accept table to be an index
 * @dfa: dfa to do the remapping on
 * @factor: scaling factor for the index conversion.
 *
 * Used in conjunction with compute_Xperms, it converts old style perms
 * that are encoded in the dfa accept tables to the new style where
 * there is a permission table and the accept table is an index into
 * the permission table.
 */
static void remap_dfa_accept(struct aa_dfa *dfa, unsigned int factor)
{
	unsigned int state;
	unsigned int state_count = dfa->tables[YYTD_ID_BASE]->td_lolen;

	AA_BUG(!dfa);

	for (state = 0; state < state_count; state++)
		ACCEPT_TABLE(dfa)[state] = state * factor;
	kvfree(dfa->tables[YYTD_ID_ACCEPT2]);
	dfa->tables[YYTD_ID_ACCEPT2] = NULL;
}

/* TODO: merge different dfa mappings into single map_policy fn */
int aa_compat_map_xmatch(struct aa_policydb *policy)
{
	policy->perms = compute_xmatch_perms(policy->dfa, &policy->size);
	if (!policy->perms)
		return -ENOMEM;

	remap_dfa_accept(policy->dfa, 1);

	return 0;
}

int aa_compat_map_policy(struct aa_policydb *policy, u32 version)
{
	policy->perms = compute_perms(policy->dfa, version, &policy->size);
	if (!policy->perms)
		return -ENOMEM;

	remap_dfa_accept(policy->dfa, 1);

	return 0;
}

int aa_compat_map_file(struct aa_policydb *policy)
{
	policy->perms = compute_fperms(policy->dfa, &policy->size);
	if (!policy->perms)
		return -ENOMEM;

	remap_dfa_accept(policy->dfa, 2);

	return 0;
}
