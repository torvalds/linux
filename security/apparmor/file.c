/*
 * AppArmor security module
 *
 * This file contains AppArmor mediation of files
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#include "include/apparmor.h"
#include "include/audit.h"
#include "include/file.h"
#include "include/match.h"
#include "include/path.h"
#include "include/policy.h"

struct file_perms nullperms;


/**
 * audit_file_mask - convert mask to permission string
 * @buffer: buffer to write string to (NOT NULL)
 * @mask: permission mask to convert
 */
static void audit_file_mask(struct audit_buffer *ab, u32 mask)
{
	char str[10];

	char *m = str;

	if (mask & AA_EXEC_MMAP)
		*m++ = 'm';
	if (mask & (MAY_READ | AA_MAY_META_READ))
		*m++ = 'r';
	if (mask & (MAY_WRITE | AA_MAY_META_WRITE | AA_MAY_CHMOD |
		    AA_MAY_CHOWN))
		*m++ = 'w';
	else if (mask & MAY_APPEND)
		*m++ = 'a';
	if (mask & AA_MAY_CREATE)
		*m++ = 'c';
	if (mask & AA_MAY_DELETE)
		*m++ = 'd';
	if (mask & AA_MAY_LINK)
		*m++ = 'l';
	if (mask & AA_MAY_LOCK)
		*m++ = 'k';
	if (mask & MAY_EXEC)
		*m++ = 'x';
	*m = '\0';

	audit_log_string(ab, str);
}

/**
 * file_audit_cb - call back for file specific audit fields
 * @ab: audit_buffer  (NOT NULL)
 * @va: audit struct to audit values of  (NOT NULL)
 */
static void file_audit_cb(struct audit_buffer *ab, void *va)
{
	struct common_audit_data *sa = va;
	kuid_t fsuid = current_fsuid();

	if (aad(sa)->fs.request & AA_AUDIT_FILE_MASK) {
		audit_log_format(ab, " requested_mask=");
		audit_file_mask(ab, aad(sa)->fs.request);
	}
	if (aad(sa)->fs.denied & AA_AUDIT_FILE_MASK) {
		audit_log_format(ab, " denied_mask=");
		audit_file_mask(ab, aad(sa)->fs.denied);
	}
	if (aad(sa)->fs.request & AA_AUDIT_FILE_MASK) {
		audit_log_format(ab, " fsuid=%d",
				 from_kuid(&init_user_ns, fsuid));
		audit_log_format(ab, " ouid=%d",
				 from_kuid(&init_user_ns, aad(sa)->fs.ouid));
	}

	if (aad(sa)->fs.target) {
		audit_log_format(ab, " target=");
		audit_log_untrustedstring(ab, aad(sa)->fs.target);
	}
}

/**
 * aa_audit_file - handle the auditing of file operations
 * @profile: the profile being enforced  (NOT NULL)
 * @perms: the permissions computed for the request (NOT NULL)
 * @gfp: allocation flags
 * @op: operation being mediated
 * @request: permissions requested
 * @name: name of object being mediated (MAYBE NULL)
 * @target: name of target (MAYBE NULL)
 * @ouid: object uid
 * @info: extra information message (MAYBE NULL)
 * @error: 0 if operation allowed else failure error code
 *
 * Returns: %0 or error on failure
 */
int aa_audit_file(struct aa_profile *profile, struct file_perms *perms,
		  const char *op, u32 request, const char *name,
		  const char *target, kuid_t ouid, const char *info, int error)
{
	int type = AUDIT_APPARMOR_AUTO;
	DEFINE_AUDIT_DATA(sa, LSM_AUDIT_DATA_TASK, op);

	sa.u.tsk = NULL;
	aad(&sa)->fs.request = request;
	aad(&sa)->name = name;
	aad(&sa)->fs.target = target;
	aad(&sa)->fs.ouid = ouid;
	aad(&sa)->info = info;
	aad(&sa)->error = error;
	sa.u.tsk = NULL;

	if (likely(!aad(&sa)->error)) {
		u32 mask = perms->audit;

		if (unlikely(AUDIT_MODE(profile) == AUDIT_ALL))
			mask = 0xffff;

		/* mask off perms that are not being force audited */
		aad(&sa)->fs.request &= mask;

		if (likely(!aad(&sa)->fs.request))
			return 0;
		type = AUDIT_APPARMOR_AUDIT;
	} else {
		/* only report permissions that were denied */
		aad(&sa)->fs.request = aad(&sa)->fs.request & ~perms->allow;
		AA_BUG(!aad(&sa)->fs.request);

		if (aad(&sa)->fs.request & perms->kill)
			type = AUDIT_APPARMOR_KILL;

		/* quiet known rejects, assumes quiet and kill do not overlap */
		if ((aad(&sa)->fs.request & perms->quiet) &&
		    AUDIT_MODE(profile) != AUDIT_NOQUIET &&
		    AUDIT_MODE(profile) != AUDIT_ALL)
			aad(&sa)->fs.request &= ~perms->quiet;

		if (!aad(&sa)->fs.request)
			return COMPLAIN_MODE(profile) ? 0 : aad(&sa)->error;
	}

	aad(&sa)->fs.denied = aad(&sa)->fs.request & ~perms->allow;
	return aa_audit(type, profile, &sa, file_audit_cb);
}

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
		new |= AA_MAY_META_READ;
	if (old & MAY_WRITE)
		new |= AA_MAY_META_WRITE | AA_MAY_CREATE | AA_MAY_DELETE |
			AA_MAY_CHMOD | AA_MAY_CHOWN;
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

/**
 * compute_perms - convert dfa compressed perms to internal perms
 * @dfa: dfa to compute perms for   (NOT NULL)
 * @state: state in dfa
 * @cond:  conditions to consider  (NOT NULL)
 *
 * TODO: convert from dfa + state to permission entry, do computation conversion
 *       at load time.
 *
 * Returns: computed permission set
 */
static struct file_perms compute_perms(struct aa_dfa *dfa, unsigned int state,
				       struct path_cond *cond)
{
	struct file_perms perms;

	/* FIXME: change over to new dfa format
	 * currently file perms are encoded in the dfa, new format
	 * splits the permissions from the dfa.  This mapping can be
	 * done at profile load
	 */
	perms.kill = 0;

	if (uid_eq(current_fsuid(), cond->uid)) {
		perms.allow = map_old_perms(dfa_user_allow(dfa, state));
		perms.audit = map_old_perms(dfa_user_audit(dfa, state));
		perms.quiet = map_old_perms(dfa_user_quiet(dfa, state));
		perms.xindex = dfa_user_xindex(dfa, state);
	} else {
		perms.allow = map_old_perms(dfa_other_allow(dfa, state));
		perms.audit = map_old_perms(dfa_other_audit(dfa, state));
		perms.quiet = map_old_perms(dfa_other_quiet(dfa, state));
		perms.xindex = dfa_other_xindex(dfa, state);
	}
	perms.allow |= AA_MAY_META_READ;

	/* change_profile wasn't determined by ownership in old mapping */
	if (ACCEPT_TABLE(dfa)[state] & 0x80000000)
		perms.allow |= AA_MAY_CHANGE_PROFILE;
	if (ACCEPT_TABLE(dfa)[state] & 0x40000000)
		perms.allow |= AA_MAY_ONEXEC;

	return perms;
}

/**
 * aa_str_perms - find permission that match @name
 * @dfa: to match against  (MAYBE NULL)
 * @state: state to start matching in
 * @name: string to match against dfa  (NOT NULL)
 * @cond: conditions to consider for permission set computation  (NOT NULL)
 * @perms: Returns - the permissions found when matching @name
 *
 * Returns: the final state in @dfa when beginning @start and walking @name
 */
unsigned int aa_str_perms(struct aa_dfa *dfa, unsigned int start,
			  const char *name, struct path_cond *cond,
			  struct file_perms *perms)
{
	unsigned int state;
	if (!dfa) {
		*perms = nullperms;
		return DFA_NOMATCH;
	}

	state = aa_dfa_match(dfa, start, name);
	*perms = compute_perms(dfa, state, cond);

	return state;
}

/**
 * is_deleted - test if a file has been completely unlinked
 * @dentry: dentry of file to test for deletion  (NOT NULL)
 *
 * Returns: %1 if deleted else %0
 */
static inline bool is_deleted(struct dentry *dentry)
{
	if (d_unlinked(dentry) && d_backing_inode(dentry)->i_nlink == 0)
		return 1;
	return 0;
}

/**
 * aa_path_perm - do permissions check & audit for @path
 * @op: operation being checked
 * @profile: profile being enforced  (NOT NULL)
 * @path: path to check permissions of  (NOT NULL)
 * @flags: any additional path flags beyond what the profile specifies
 * @request: requested permissions
 * @cond: conditional info for this request  (NOT NULL)
 *
 * Returns: %0 else error if access denied or other error
 */
int aa_path_perm(const char *op, struct aa_profile *profile,
		 const struct path *path, int flags, u32 request,
		 struct path_cond *cond)
{
	char *buffer = NULL;
	struct file_perms perms = {};
	const char *name, *info = NULL;
	int error;

	flags |= profile->path_flags | (S_ISDIR(cond->mode) ? PATH_IS_DIR : 0);
	error = aa_path_name(path, flags, &buffer, &name, &info);
	if (error) {
		if (error == -ENOENT && is_deleted(path->dentry)) {
			/* Access to open files that are deleted are
			 * give a pass (implicit delegation)
			 */
			error = 0;
			info = NULL;
			perms.allow = request;
		}
	} else {
		aa_str_perms(profile->file.dfa, profile->file.start, name, cond,
			     &perms);
		if (request & ~perms.allow)
			error = -EACCES;
	}
	error = aa_audit_file(profile, &perms, op, request, name, NULL,
			      cond->uid, info, error);
	kfree(buffer);

	return error;
}

/**
 * xindex_is_subset - helper for aa_path_link
 * @link: link permission set
 * @target: target permission set
 *
 * test target x permissions are equal OR a subset of link x permissions
 * this is done as part of the subset test, where a hardlink must have
 * a subset of permissions that the target has.
 *
 * Returns: %1 if subset else %0
 */
static inline bool xindex_is_subset(u32 link, u32 target)
{
	if (((link & ~AA_X_UNSAFE) != (target & ~AA_X_UNSAFE)) ||
	    ((link & AA_X_UNSAFE) && !(target & AA_X_UNSAFE)))
		return 0;

	return 1;
}

/**
 * aa_path_link - Handle hard link permission check
 * @profile: the profile being enforced  (NOT NULL)
 * @old_dentry: the target dentry  (NOT NULL)
 * @new_dir: directory the new link will be created in  (NOT NULL)
 * @new_dentry: the link being created  (NOT NULL)
 *
 * Handle the permission test for a link & target pair.  Permission
 * is encoded as a pair where the link permission is determined
 * first, and if allowed, the target is tested.  The target test
 * is done from the point of the link match (not start of DFA)
 * making the target permission dependent on the link permission match.
 *
 * The subset test if required forces that permissions granted
 * on link are a subset of the permission granted to target.
 *
 * Returns: %0 if allowed else error
 */
int aa_path_link(struct aa_profile *profile, struct dentry *old_dentry,
		 const struct path *new_dir, struct dentry *new_dentry)
{
	struct path link = { .mnt = new_dir->mnt, .dentry = new_dentry };
	struct path target = { .mnt = new_dir->mnt, .dentry = old_dentry };
	struct path_cond cond = {
		d_backing_inode(old_dentry)->i_uid,
		d_backing_inode(old_dentry)->i_mode
	};
	char *buffer = NULL, *buffer2 = NULL;
	const char *lname, *tname = NULL, *info = NULL;
	struct file_perms lperms, perms;
	u32 request = AA_MAY_LINK;
	unsigned int state;
	int error;

	lperms = nullperms;

	/* buffer freed below, lname is pointer in buffer */
	error = aa_path_name(&link, profile->path_flags, &buffer, &lname,
			     &info);
	if (error)
		goto audit;

	/* buffer2 freed below, tname is pointer in buffer2 */
	error = aa_path_name(&target, profile->path_flags, &buffer2, &tname,
			     &info);
	if (error)
		goto audit;

	error = -EACCES;
	/* aa_str_perms - handles the case of the dfa being NULL */
	state = aa_str_perms(profile->file.dfa, profile->file.start, lname,
			     &cond, &lperms);

	if (!(lperms.allow & AA_MAY_LINK))
		goto audit;

	/* test to see if target can be paired with link */
	state = aa_dfa_null_transition(profile->file.dfa, state);
	aa_str_perms(profile->file.dfa, state, tname, &cond, &perms);

	/* force audit/quiet masks for link are stored in the second entry
	 * in the link pair.
	 */
	lperms.audit = perms.audit;
	lperms.quiet = perms.quiet;
	lperms.kill = perms.kill;

	if (!(perms.allow & AA_MAY_LINK)) {
		info = "target restricted";
		goto audit;
	}

	/* done if link subset test is not required */
	if (!(perms.allow & AA_LINK_SUBSET))
		goto done_tests;

	/* Do link perm subset test requiring allowed permission on link are a
	 * subset of the allowed permissions on target.
	 */
	aa_str_perms(profile->file.dfa, profile->file.start, tname, &cond,
		     &perms);

	/* AA_MAY_LINK is not considered in the subset test */
	request = lperms.allow & ~AA_MAY_LINK;
	lperms.allow &= perms.allow | AA_MAY_LINK;

	request |= AA_AUDIT_FILE_MASK & (lperms.allow & ~perms.allow);
	if (request & ~lperms.allow) {
		goto audit;
	} else if ((lperms.allow & MAY_EXEC) &&
		   !xindex_is_subset(lperms.xindex, perms.xindex)) {
		lperms.allow &= ~MAY_EXEC;
		request |= MAY_EXEC;
		info = "link not subset of target";
		goto audit;
	}

done_tests:
	error = 0;

audit:
	error = aa_audit_file(profile, &lperms, OP_LINK, request,
			      lname, tname, cond.uid, info, error);
	kfree(buffer);
	kfree(buffer2);

	return error;
}

/**
 * aa_file_perm - do permission revalidation check & audit for @file
 * @op: operation being checked
 * @profile: profile being enforced   (NOT NULL)
 * @file: file to revalidate access permissions on  (NOT NULL)
 * @request: requested permissions
 *
 * Returns: %0 if access allowed else error
 */
int aa_file_perm(const char *op, struct aa_profile *profile, struct file *file,
		 u32 request)
{
	struct path_cond cond = {
		.uid = file_inode(file)->i_uid,
		.mode = file_inode(file)->i_mode
	};

	return aa_path_perm(op, profile, &file->f_path, PATH_DELEGATE_DELETED,
			    request, &cond);
}
