/*
 * AppArmor security module
 *
 * This file contains AppArmor file mediation function definitions.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#ifndef __AA_FILE_H
#define __AA_FILE_H

#include <linux/path.h>

#include "domain.h"
#include "match.h"

struct aa_profile;

/*
 * We use MAY_EXEC, MAY_WRITE, MAY_READ, MAY_APPEND and the following flags
 * for profile permissions
 */
#define AA_MAY_CREATE                  0x0010
#define AA_MAY_DELETE                  0x0020
#define AA_MAY_META_WRITE              0x0040
#define AA_MAY_META_READ               0x0080

#define AA_MAY_CHMOD                   0x0100
#define AA_MAY_CHOWN                   0x0200
#define AA_MAY_LOCK                    0x0400
#define AA_EXEC_MMAP                   0x0800

#define AA_MAY_LINK			0x1000
#define AA_LINK_SUBSET			AA_MAY_LOCK	/* overlaid */
#define AA_MAY_ONEXEC			0x40000000	/* exec allows onexec */
#define AA_MAY_CHANGE_PROFILE		0x80000000
#define AA_MAY_CHANGEHAT		0x80000000	/* ctrl auditing only */

#define AA_AUDIT_FILE_MASK	(MAY_READ | MAY_WRITE | MAY_EXEC | MAY_APPEND |\
				 AA_MAY_CREATE | AA_MAY_DELETE |	\
				 AA_MAY_META_READ | AA_MAY_META_WRITE | \
				 AA_MAY_CHMOD | AA_MAY_CHOWN | AA_MAY_LOCK | \
				 AA_EXEC_MMAP | AA_MAY_LINK)

/*
 * The xindex is broken into 3 parts
 * - index - an index into either the exec name table or the variable table
 * - exec type - which determines how the executable name and index are used
 * - flags - which modify how the destination name is applied
 */
#define AA_X_INDEX_MASK		0x03ff

#define AA_X_TYPE_MASK		0x0c00
#define AA_X_TYPE_SHIFT		10
#define AA_X_NONE		0x0000
#define AA_X_NAME		0x0400	/* use executable name px */
#define AA_X_TABLE		0x0800	/* use a specified name ->n# */

#define AA_X_UNSAFE		0x1000
#define AA_X_CHILD		0x2000	/* make >AA_X_NONE apply to children */
#define AA_X_INHERIT		0x4000
#define AA_X_UNCONFINED		0x8000

/* AA_SECURE_X_NEEDED - is passed in the bprm->unsafe field */
#define AA_SECURE_X_NEEDED	0x8000

/* need to make conditional which ones are being set */
struct path_cond {
	uid_t uid;
	umode_t mode;
};

/* struct file_perms - file permission
 * @allow: mask of permissions that are allowed
 * @audit: mask of permissions to force an audit message for
 * @quiet: mask of permissions to quiet audit messages for
 * @kill: mask of permissions that when matched will kill the task
 * @xindex: exec transition index if @allow contains MAY_EXEC
 *
 * The @audit and @queit mask should be mutually exclusive.
 */
struct file_perms {
	u32 allow;
	u32 audit;
	u32 quiet;
	u32 kill;
	u16 xindex;
};

extern struct file_perms nullperms;

#define COMBINED_PERM_MASK(X) ((X).allow | (X).audit | (X).quiet | (X).kill)

/* FIXME: split perms from dfa and match this to description
 *        also add delegation info.
 */
static inline u16 dfa_map_xindex(u16 mask)
{
	u16 old_index = (mask >> 10) & 0xf;
	u16 index = 0;

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
	} else {
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
#define dfa_user_audit(dfa, state) ((ACCEPT_TABLE2(dfa)[state]) & 0x7f)
#define dfa_user_quiet(dfa, state) (((ACCEPT_TABLE2(dfa)[state]) >> 7) & 0x7f)
#define dfa_user_xindex(dfa, state) \
	(dfa_map_xindex(ACCEPT_TABLE(dfa)[state] & 0x3fff))

#define dfa_other_allow(dfa, state) ((((ACCEPT_TABLE(dfa)[state]) >> 14) & \
				      0x7f) |				\
				     ((ACCEPT_TABLE(dfa)[state]) & 0x80000000))
#define dfa_other_audit(dfa, state) (((ACCEPT_TABLE2(dfa)[state]) >> 14) & 0x7f)
#define dfa_other_quiet(dfa, state) \
	((((ACCEPT_TABLE2(dfa)[state]) >> 7) >> 14) & 0x7f)
#define dfa_other_xindex(dfa, state) \
	dfa_map_xindex((ACCEPT_TABLE(dfa)[state] >> 14) & 0x3fff)

int aa_audit_file(struct aa_profile *profile, struct file_perms *perms,
		  gfp_t gfp, int op, u32 request, const char *name,
		  const char *target, uid_t ouid, const char *info, int error);

/**
 * struct aa_file_rules - components used for file rule permissions
 * @dfa: dfa to match path names and conditionals against
 * @perms: permission table indexed by the matched state accept entry of @dfa
 * @trans: transition table for indexed by named x transitions
 *
 * File permission are determined by matching a path against @dfa and then
 * then using the value of the accept entry for the matching state as
 * an index into @perms.  If a named exec transition is required it is
 * looked up in the transition table.
 */
struct aa_file_rules {
	unsigned int start;
	struct aa_dfa *dfa;
	/* struct perms perms; */
	struct aa_domain trans;
	/* TODO: add delegate table */
};

unsigned int aa_str_perms(struct aa_dfa *dfa, unsigned int start,
			  const char *name, struct path_cond *cond,
			  struct file_perms *perms);

int aa_path_perm(int op, struct aa_profile *profile, struct path *path,
		 int flags, u32 request, struct path_cond *cond);

int aa_path_link(struct aa_profile *profile, struct dentry *old_dentry,
		 struct path *new_dir, struct dentry *new_dentry);

int aa_file_perm(int op, struct aa_profile *profile, struct file *file,
		 u32 request);

static inline void aa_free_file_rules(struct aa_file_rules *rules)
{
	aa_put_dfa(rules->dfa);
	aa_free_domain_entries(&rules->trans);
}

#define ACC_FMODE(x) (("\000\004\002\006"[(x)&O_ACCMODE]) | (((x) << 1) & 0x40))

/* from namei.c */
#define MAP_OPEN_FLAGS(x) ((((x) + 1) & O_ACCMODE) ? (x) + 1 : (x))

/**
 * aa_map_file_perms - map file flags to AppArmor permissions
 * @file: open file to map flags to AppArmor permissions
 *
 * Returns: apparmor permission set for the file
 */
static inline u32 aa_map_file_to_perms(struct file *file)
{
	int flags = MAP_OPEN_FLAGS(file->f_flags);
	u32 perms = ACC_FMODE(file->f_mode);

	if ((flags & O_APPEND) && (perms & MAY_WRITE))
		perms = (perms & ~MAY_WRITE) | MAY_APPEND;
	/* trunc implies write permission */
	if (flags & O_TRUNC)
		perms |= MAY_WRITE;
	if (flags & O_CREAT)
		perms |= AA_MAY_CREATE;

	return perms;
}

#endif /* __AA_FILE_H */
