/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AppArmor security module
 *
 * This file contains AppArmor file mediation function definitions.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 */

#ifndef __AA_FILE_H
#define __AA_FILE_H

#include <linux/spinlock.h>

#include "domain.h"
#include "match.h"
#include "perms.h"

struct aa_policydb;
struct aa_profile;
struct path;

#define mask_mode_t(X) (X & (MAY_EXEC | MAY_WRITE | MAY_READ | MAY_APPEND))

#define AA_AUDIT_FILE_MASK	(MAY_READ | MAY_WRITE | MAY_EXEC | MAY_APPEND |\
				 AA_MAY_CREATE | AA_MAY_DELETE |	\
				 AA_MAY_GETATTR | AA_MAY_SETATTR | \
				 AA_MAY_CHMOD | AA_MAY_CHOWN | AA_MAY_LOCK | \
				 AA_EXEC_MMAP | AA_MAY_LINK)

static inline struct aa_file_ctx *file_ctx(struct file *file)
{
	return file->f_security + apparmor_blob_sizes.lbs_file;
}

/* struct aa_file_ctx - the AppArmor context the file was opened in
 * @lock: lock to update the ctx
 * @label: label currently cached on the ctx
 * @perms: the permission the file was opened with
 */
struct aa_file_ctx {
	spinlock_t lock;
	struct aa_label __rcu *label;
	u32 allow;
};

/*
 * The xindex is broken into 3 parts
 * - index - an index into either the exec name table or the variable table
 * - exec type - which determines how the executable name and index are used
 * - flags - which modify how the destination name is applied
 */
#define AA_X_INDEX_MASK		AA_INDEX_MASK

#define AA_X_TYPE_MASK		0x0c000000
#define AA_X_NONE		AA_INDEX_NONE
#define AA_X_NAME		0x04000000 /* use executable name px */
#define AA_X_TABLE		0x08000000 /* use a specified name ->n# */

#define AA_X_UNSAFE		0x10000000
#define AA_X_CHILD		0x20000000
#define AA_X_INHERIT		0x40000000
#define AA_X_UNCONFINED		0x80000000

/* need to make conditional which ones are being set */
struct path_cond {
	kuid_t uid;
	umode_t mode;
};

#define COMBINED_PERM_MASK(X) ((X).allow | (X).audit | (X).quiet | (X).kill)

int aa_audit_file(const struct cred *cred,
		  struct aa_profile *profile, struct aa_perms *perms,
		  const char *op, u32 request, const char *name,
		  const char *target, struct aa_label *tlabel, kuid_t ouid,
		  const char *info, int error);

struct aa_perms *aa_lookup_fperms(struct aa_policydb *file_rules,
				  aa_state_t state, struct path_cond *cond);
aa_state_t aa_str_perms(struct aa_policydb *file_rules, aa_state_t start,
			const char *name, struct path_cond *cond,
			struct aa_perms *perms);

int aa_path_perm(const char *op, const struct cred *subj_cred,
		 struct aa_label *label, const struct path *path,
		 int flags, u32 request, struct path_cond *cond);

int aa_path_link(const struct cred *subj_cred, struct aa_label *label,
		 struct dentry *old_dentry, const struct path *new_dir,
		 struct dentry *new_dentry);

int aa_file_perm(const char *op, const struct cred *subj_cred,
		 struct aa_label *label, struct file *file,
		 u32 request, bool in_atomic);

void aa_inherit_files(const struct cred *cred, struct files_struct *files);


/**
 * aa_map_file_perms - map file flags to AppArmor permissions
 * @file: open file to map flags to AppArmor permissions
 *
 * Returns: apparmor permission set for the file
 */
static inline u32 aa_map_file_to_perms(struct file *file)
{
	int flags = file->f_flags;
	u32 perms = 0;

	if (file->f_mode & FMODE_WRITE)
		perms |= MAY_WRITE;
	if (file->f_mode & FMODE_READ)
		perms |= MAY_READ;

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
