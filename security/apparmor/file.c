// SPDX-License-Identifier: GPL-2.0-only
/*
 * AppArmor security module
 *
 * This file contains AppArmor mediation of files
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 */

#include <linux/tty.h>
#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/mount.h>

#include "include/apparmor.h"
#include "include/audit.h"
#include "include/cred.h"
#include "include/file.h"
#include "include/match.h"
#include "include/net.h"
#include "include/path.h"
#include "include/policy.h"
#include "include/label.h"

static u32 map_mask_to_chr_mask(u32 mask)
{
	u32 m = mask & PERMS_CHRS_MASK;

	if (mask & AA_MAY_GETATTR)
		m |= MAY_READ;
	if (mask & (AA_MAY_SETATTR | AA_MAY_CHMOD | AA_MAY_CHOWN))
		m |= MAY_WRITE;

	return m;
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
	char str[10];

	if (aad(sa)->request & AA_AUDIT_FILE_MASK) {
		aa_perm_mask_to_str(str, sizeof(str), aa_file_perm_chrs,
				    map_mask_to_chr_mask(aad(sa)->request));
		audit_log_format(ab, " requested_mask=\"%s\"", str);
	}
	if (aad(sa)->denied & AA_AUDIT_FILE_MASK) {
		aa_perm_mask_to_str(str, sizeof(str), aa_file_perm_chrs,
				    map_mask_to_chr_mask(aad(sa)->denied));
		audit_log_format(ab, " denied_mask=\"%s\"", str);
	}
	if (aad(sa)->request & AA_AUDIT_FILE_MASK) {
		audit_log_format(ab, " fsuid=%d",
				 from_kuid(&init_user_ns, fsuid));
		audit_log_format(ab, " ouid=%d",
				 from_kuid(&init_user_ns, aad(sa)->fs.ouid));
	}

	if (aad(sa)->peer) {
		audit_log_format(ab, " target=");
		aa_label_xaudit(ab, labels_ns(aad(sa)->label), aad(sa)->peer,
				FLAG_VIEW_SUBNS, GFP_KERNEL);
	} else if (aad(sa)->fs.target) {
		audit_log_format(ab, " target=");
		audit_log_untrustedstring(ab, aad(sa)->fs.target);
	}
}

/**
 * aa_audit_file - handle the auditing of file operations
 * @profile: the profile being enforced  (NOT NULL)
 * @perms: the permissions computed for the request (NOT NULL)
 * @op: operation being mediated
 * @request: permissions requested
 * @name: name of object being mediated (MAYBE NULL)
 * @target: name of target (MAYBE NULL)
 * @tlabel: target label (MAY BE NULL)
 * @ouid: object uid
 * @info: extra information message (MAYBE NULL)
 * @error: 0 if operation allowed else failure error code
 *
 * Returns: %0 or error on failure
 */
int aa_audit_file(struct aa_profile *profile, struct aa_perms *perms,
		  const char *op, u32 request, const char *name,
		  const char *target, struct aa_label *tlabel,
		  kuid_t ouid, const char *info, int error)
{
	int type = AUDIT_APPARMOR_AUTO;
	DEFINE_AUDIT_DATA(sa, LSM_AUDIT_DATA_TASK, AA_CLASS_FILE, op);

	sa.u.tsk = NULL;
	aad(&sa)->request = request;
	aad(&sa)->name = name;
	aad(&sa)->fs.target = target;
	aad(&sa)->peer = tlabel;
	aad(&sa)->fs.ouid = ouid;
	aad(&sa)->info = info;
	aad(&sa)->error = error;
	sa.u.tsk = NULL;

	if (likely(!aad(&sa)->error)) {
		u32 mask = perms->audit;

		if (unlikely(AUDIT_MODE(profile) == AUDIT_ALL))
			mask = 0xffff;

		/* mask off perms that are not being force audited */
		aad(&sa)->request &= mask;

		if (likely(!aad(&sa)->request))
			return 0;
		type = AUDIT_APPARMOR_AUDIT;
	} else {
		/* only report permissions that were denied */
		aad(&sa)->request = aad(&sa)->request & ~perms->allow;
		AA_BUG(!aad(&sa)->request);

		if (aad(&sa)->request & perms->kill)
			type = AUDIT_APPARMOR_KILL;

		/* quiet known rejects, assumes quiet and kill do not overlap */
		if ((aad(&sa)->request & perms->quiet) &&
		    AUDIT_MODE(profile) != AUDIT_NOQUIET &&
		    AUDIT_MODE(profile) != AUDIT_ALL)
			aad(&sa)->request &= ~perms->quiet;

		if (!aad(&sa)->request)
			return aad(&sa)->error;
	}

	aad(&sa)->denied = aad(&sa)->request & ~perms->allow;
	return aa_audit(type, profile, &sa, file_audit_cb);
}

static int path_name(const char *op, struct aa_label *label,
		     const struct path *path, int flags, char *buffer,
		     const char **name, struct path_cond *cond, u32 request)
{
	struct aa_profile *profile;
	const char *info = NULL;
	int error;

	error = aa_path_name(path, flags, buffer, name, &info,
			     labels_profile(label)->disconnected);
	if (error) {
		fn_for_each_confined(label, profile,
			aa_audit_file(profile, &nullperms, op, request, *name,
				      NULL, NULL, cond->uid, info, error));
		return error;
	}

	return 0;
}

struct aa_perms default_perms = {};
/**
 * aa_lookup_fperms - convert dfa compressed perms to internal perms
 * @file_rules: the aa_policydb to lookup perms for  (NOT NULL)
 * @state: state in dfa
 * @cond:  conditions to consider  (NOT NULL)
 *
 * TODO: convert from dfa + state to permission entry
 *
 * Returns: a pointer to a file permission set
 */
struct aa_perms *aa_lookup_fperms(struct aa_policydb *file_rules,
				 aa_state_t state, struct path_cond *cond)
{
	unsigned int index = ACCEPT_TABLE(file_rules->dfa)[state];

	if (!(file_rules->perms))
		return &default_perms;

	if (uid_eq(current_fsuid(), cond->uid))
		return &(file_rules->perms[index]);

	return &(file_rules->perms[index + 1]);
}

/**
 * aa_str_perms - find permission that match @name
 * @file_rules: the aa_policydb to match against  (NOT NULL)
 * @start: state to start matching in
 * @name: string to match against dfa  (NOT NULL)
 * @cond: conditions to consider for permission set computation  (NOT NULL)
 * @perms: Returns - the permissions found when matching @name
 *
 * Returns: the final state in @dfa when beginning @start and walking @name
 */
aa_state_t aa_str_perms(struct aa_policydb *file_rules, aa_state_t start,
			const char *name, struct path_cond *cond,
			struct aa_perms *perms)
{
	aa_state_t state;
	state = aa_dfa_match(file_rules->dfa, start, name);
	*perms = *(aa_lookup_fperms(file_rules, state, cond));

	return state;
}

static int __aa_path_perm(const char *op, struct aa_profile *profile,
			  const char *name, u32 request,
			  struct path_cond *cond, int flags,
			  struct aa_perms *perms)
{
	struct aa_ruleset *rules = list_first_entry(&profile->rules,
						    typeof(*rules), list);
	int e = 0;

	if (profile_unconfined(profile))
		return 0;
	aa_str_perms(&(rules->file), rules->file.start[AA_CLASS_FILE],
		     name, cond, perms);
	if (request & ~perms->allow)
		e = -EACCES;
	return aa_audit_file(profile, perms, op, request, name, NULL, NULL,
			     cond->uid, NULL, e);
}


static int profile_path_perm(const char *op, struct aa_profile *profile,
			     const struct path *path, char *buffer, u32 request,
			     struct path_cond *cond, int flags,
			     struct aa_perms *perms)
{
	const char *name;
	int error;

	if (profile_unconfined(profile))
		return 0;

	error = path_name(op, &profile->label, path,
			  flags | profile->path_flags, buffer, &name, cond,
			  request);
	if (error)
		return error;
	return __aa_path_perm(op, profile, name, request, cond, flags,
			      perms);
}

/**
 * aa_path_perm - do permissions check & audit for @path
 * @op: operation being checked
 * @label: profile being enforced  (NOT NULL)
 * @path: path to check permissions of  (NOT NULL)
 * @flags: any additional path flags beyond what the profile specifies
 * @request: requested permissions
 * @cond: conditional info for this request  (NOT NULL)
 *
 * Returns: %0 else error if access denied or other error
 */
int aa_path_perm(const char *op, struct aa_label *label,
		 const struct path *path, int flags, u32 request,
		 struct path_cond *cond)
{
	struct aa_perms perms = {};
	struct aa_profile *profile;
	char *buffer = NULL;
	int error;

	flags |= PATH_DELEGATE_DELETED | (S_ISDIR(cond->mode) ? PATH_IS_DIR :
								0);
	buffer = aa_get_buffer(false);
	if (!buffer)
		return -ENOMEM;
	error = fn_for_each_confined(label, profile,
			profile_path_perm(op, profile, path, buffer, request,
					  cond, flags, &perms));

	aa_put_buffer(buffer);

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
 * Returns: true if subset else false
 */
static inline bool xindex_is_subset(u32 link, u32 target)
{
	if (((link & ~AA_X_UNSAFE) != (target & ~AA_X_UNSAFE)) ||
	    ((link & AA_X_UNSAFE) && !(target & AA_X_UNSAFE)))
		return false;

	return true;
}

static int profile_path_link(struct aa_profile *profile,
			     const struct path *link, char *buffer,
			     const struct path *target, char *buffer2,
			     struct path_cond *cond)
{
	struct aa_ruleset *rules = list_first_entry(&profile->rules,
						    typeof(*rules), list);
	const char *lname, *tname = NULL;
	struct aa_perms lperms = {}, perms;
	const char *info = NULL;
	u32 request = AA_MAY_LINK;
	aa_state_t state;
	int error;

	error = path_name(OP_LINK, &profile->label, link, profile->path_flags,
			  buffer, &lname, cond, AA_MAY_LINK);
	if (error)
		goto audit;

	/* buffer2 freed below, tname is pointer in buffer2 */
	error = path_name(OP_LINK, &profile->label, target, profile->path_flags,
			  buffer2, &tname, cond, AA_MAY_LINK);
	if (error)
		goto audit;

	error = -EACCES;
	/* aa_str_perms - handles the case of the dfa being NULL */
	state = aa_str_perms(&(rules->file),
			     rules->file.start[AA_CLASS_FILE], lname,
			     cond, &lperms);

	if (!(lperms.allow & AA_MAY_LINK))
		goto audit;

	/* test to see if target can be paired with link */
	state = aa_dfa_null_transition(rules->file.dfa, state);
	aa_str_perms(&(rules->file), state, tname, cond, &perms);

	/* force audit/quiet masks for link are stored in the second entry
	 * in the link pair.
	 */
	lperms.audit = perms.audit;
	lperms.quiet = perms.quiet;
	lperms.kill = perms.kill;

	if (!(perms.allow & AA_MAY_LINK)) {
		info = "target restricted";
		lperms = perms;
		goto audit;
	}

	/* done if link subset test is not required */
	if (!(perms.allow & AA_LINK_SUBSET))
		goto done_tests;

	/* Do link perm subset test requiring allowed permission on link are
	 * a subset of the allowed permissions on target.
	 */
	aa_str_perms(&(rules->file), rules->file.start[AA_CLASS_FILE],
		     tname, cond, &perms);

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
	return aa_audit_file(profile, &lperms, OP_LINK, request, lname, tname,
			     NULL, cond->uid, info, error);
}

/**
 * aa_path_link - Handle hard link permission check
 * @label: the label being enforced  (NOT NULL)
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
int aa_path_link(struct aa_label *label, struct dentry *old_dentry,
		 const struct path *new_dir, struct dentry *new_dentry)
{
	struct path link = { .mnt = new_dir->mnt, .dentry = new_dentry };
	struct path target = { .mnt = new_dir->mnt, .dentry = old_dentry };
	struct path_cond cond = {
		d_backing_inode(old_dentry)->i_uid,
		d_backing_inode(old_dentry)->i_mode
	};
	char *buffer = NULL, *buffer2 = NULL;
	struct aa_profile *profile;
	int error;

	/* buffer freed below, lname is pointer in buffer */
	buffer = aa_get_buffer(false);
	buffer2 = aa_get_buffer(false);
	error = -ENOMEM;
	if (!buffer || !buffer2)
		goto out;

	error = fn_for_each_confined(label, profile,
			profile_path_link(profile, &link, buffer, &target,
					  buffer2, &cond));
out:
	aa_put_buffer(buffer);
	aa_put_buffer(buffer2);
	return error;
}

static void update_file_ctx(struct aa_file_ctx *fctx, struct aa_label *label,
			    u32 request)
{
	struct aa_label *l, *old;

	/* update caching of label on file_ctx */
	spin_lock(&fctx->lock);
	old = rcu_dereference_protected(fctx->label,
					lockdep_is_held(&fctx->lock));
	l = aa_label_merge(old, label, GFP_ATOMIC);
	if (l) {
		if (l != old) {
			rcu_assign_pointer(fctx->label, l);
			aa_put_label(old);
		} else
			aa_put_label(l);
		fctx->allow |= request;
	}
	spin_unlock(&fctx->lock);
}

static int __file_path_perm(const char *op, struct aa_label *label,
			    struct aa_label *flabel, struct file *file,
			    u32 request, u32 denied, bool in_atomic)
{
	struct aa_profile *profile;
	struct aa_perms perms = {};
	vfsuid_t vfsuid = i_uid_into_vfsuid(file_mnt_idmap(file),
					    file_inode(file));
	struct path_cond cond = {
		.uid = vfsuid_into_kuid(vfsuid),
		.mode = file_inode(file)->i_mode
	};
	char *buffer;
	int flags, error;

	/* revalidation due to label out of date. No revocation at this time */
	if (!denied && aa_label_is_subset(flabel, label))
		/* TODO: check for revocation on stale profiles */
		return 0;

	flags = PATH_DELEGATE_DELETED | (S_ISDIR(cond.mode) ? PATH_IS_DIR : 0);
	buffer = aa_get_buffer(in_atomic);
	if (!buffer)
		return -ENOMEM;

	/* check every profile in task label not in current cache */
	error = fn_for_each_not_in_set(flabel, label, profile,
			profile_path_perm(op, profile, &file->f_path, buffer,
					  request, &cond, flags, &perms));
	if (denied && !error) {
		/*
		 * check every profile in file label that was not tested
		 * in the initial check above.
		 *
		 * TODO: cache full perms so this only happens because of
		 * conditionals
		 * TODO: don't audit here
		 */
		if (label == flabel)
			error = fn_for_each(label, profile,
				profile_path_perm(op, profile, &file->f_path,
						  buffer, request, &cond, flags,
						  &perms));
		else
			error = fn_for_each_not_in_set(label, flabel, profile,
				profile_path_perm(op, profile, &file->f_path,
						  buffer, request, &cond, flags,
						  &perms));
	}
	if (!error)
		update_file_ctx(file_ctx(file), label, request);

	aa_put_buffer(buffer);

	return error;
}

static int __file_sock_perm(const char *op, struct aa_label *label,
			    struct aa_label *flabel, struct file *file,
			    u32 request, u32 denied)
{
	struct socket *sock = (struct socket *) file->private_data;
	int error;

	AA_BUG(!sock);

	/* revalidation due to label out of date. No revocation at this time */
	if (!denied && aa_label_is_subset(flabel, label))
		return 0;

	/* TODO: improve to skip profiles cached in flabel */
	error = aa_sock_file_perm(label, op, request, sock);
	if (denied) {
		/* TODO: improve to skip profiles checked above */
		/* check every profile in file label to is cached */
		last_error(error, aa_sock_file_perm(flabel, op, request, sock));
	}
	if (!error)
		update_file_ctx(file_ctx(file), label, request);

	return error;
}

/**
 * aa_file_perm - do permission revalidation check & audit for @file
 * @op: operation being checked
 * @label: label being enforced   (NOT NULL)
 * @file: file to revalidate access permissions on  (NOT NULL)
 * @request: requested permissions
 * @in_atomic: whether allocations need to be done in atomic context
 *
 * Returns: %0 if access allowed else error
 */
int aa_file_perm(const char *op, struct aa_label *label, struct file *file,
		 u32 request, bool in_atomic)
{
	struct aa_file_ctx *fctx;
	struct aa_label *flabel;
	u32 denied;
	int error = 0;

	AA_BUG(!label);
	AA_BUG(!file);

	fctx = file_ctx(file);

	rcu_read_lock();
	flabel  = rcu_dereference(fctx->label);
	AA_BUG(!flabel);

	/* revalidate access, if task is unconfined, or the cached cred
	 * doesn't match or if the request is for more permissions than
	 * was granted.
	 *
	 * Note: the test for !unconfined(flabel) is to handle file
	 *       delegation from unconfined tasks
	 */
	denied = request & ~fctx->allow;
	if (unconfined(label) || unconfined(flabel) ||
	    (!denied && aa_label_is_subset(flabel, label))) {
		rcu_read_unlock();
		goto done;
	}

	flabel  = aa_get_newest_label(flabel);
	rcu_read_unlock();
	/* TODO: label cross check */

	if (file->f_path.mnt && path_mediated_fs(file->f_path.dentry))
		error = __file_path_perm(op, label, flabel, file, request,
					 denied, in_atomic);

	else if (S_ISSOCK(file_inode(file)->i_mode))
		error = __file_sock_perm(op, label, flabel, file, request,
					 denied);
	aa_put_label(flabel);

done:
	return error;
}

static void revalidate_tty(struct aa_label *label)
{
	struct tty_struct *tty;
	int drop_tty = 0;

	tty = get_current_tty();
	if (!tty)
		return;

	spin_lock(&tty->files_lock);
	if (!list_empty(&tty->tty_files)) {
		struct tty_file_private *file_priv;
		struct file *file;
		/* TODO: Revalidate access to controlling tty. */
		file_priv = list_first_entry(&tty->tty_files,
					     struct tty_file_private, list);
		file = file_priv->file;

		if (aa_file_perm(OP_INHERIT, label, file, MAY_READ | MAY_WRITE,
				 IN_ATOMIC))
			drop_tty = 1;
	}
	spin_unlock(&tty->files_lock);
	tty_kref_put(tty);

	if (drop_tty)
		no_tty();
}

static int match_file(const void *p, struct file *file, unsigned int fd)
{
	struct aa_label *label = (struct aa_label *)p;

	if (aa_file_perm(OP_INHERIT, label, file, aa_map_file_to_perms(file),
			 IN_ATOMIC))
		return fd + 1;
	return 0;
}


/* based on selinux's flush_unauthorized_files */
void aa_inherit_files(const struct cred *cred, struct files_struct *files)
{
	struct aa_label *label = aa_get_newest_cred_label(cred);
	struct file *devnull = NULL;
	unsigned int n;

	revalidate_tty(label);

	/* Revalidate access to inherited open files. */
	n = iterate_fd(files, 0, match_file, label);
	if (!n) /* none found? */
		goto out;

	devnull = dentry_open(&aa_null, O_RDWR, cred);
	if (IS_ERR(devnull))
		devnull = NULL;
	/* replace all the matching ones with this */
	do {
		replace_fd(n - 1, devnull, 0);
	} while ((n = iterate_fd(files, n, match_file, label)) != 0);
	if (devnull)
		fput(devnull);
out:
	aa_put_label(label);
}
