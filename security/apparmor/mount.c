// SPDX-License-Identifier: GPL-2.0-only
/*
 * AppArmor security module
 *
 * This file contains AppArmor mediation of files
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2017 Canonical Ltd.
 */

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <uapi/linux/mount.h>

#include "include/apparmor.h"
#include "include/audit.h"
#include "include/cred.h"
#include "include/domain.h"
#include "include/file.h"
#include "include/match.h"
#include "include/mount.h"
#include "include/path.h"
#include "include/policy.h"


#define DEFINE_AUDIT_MOUNT(NAME, OP, CRED)				\
	DEFINE_AUDIT_DATA(NAME, LSM_AUDIT_DATA_NONE, AA_CLASS_MOUNT, OP);\
	NAME.subj_cred = (CRED)

static void audit_mnt_flags(struct audit_buffer *ab, unsigned long flags)
{
	if (flags & MS_RDONLY)
		audit_log_format(ab, "ro");
	else
		audit_log_format(ab, "rw");
	if (flags & MS_NOSUID)
		audit_log_format(ab, ", nosuid");
	if (flags & MS_NODEV)
		audit_log_format(ab, ", nodev");
	if (flags & MS_NOEXEC)
		audit_log_format(ab, ", noexec");
	if (flags & MS_SYNCHRONOUS)
		audit_log_format(ab, ", sync");
	if (flags & MS_REMOUNT)
		audit_log_format(ab, ", remount");
	if (flags & MS_MANDLOCK)
		audit_log_format(ab, ", mand");
	if (flags & MS_DIRSYNC)
		audit_log_format(ab, ", dirsync");
	if (flags & MS_NOSYMFOLLOW)
		audit_log_format(ab, ", nosymfollow");
	if (flags & MS_NOATIME)
		audit_log_format(ab, ", noatime");
	if (flags & MS_NODIRATIME)
		audit_log_format(ab, ", nodiratime");
	if (flags & MS_BIND)
		audit_log_format(ab, flags & MS_REC ? ", rbind" : ", bind");
	if (flags & MS_MOVE)
		audit_log_format(ab, ", move");
	if (flags & MS_SILENT)
		audit_log_format(ab, ", silent");
	if (flags & MS_POSIXACL)
		audit_log_format(ab, ", acl");
	if (flags & MS_UNBINDABLE)
		audit_log_format(ab, flags & MS_REC ? ", runbindable" :
				 ", unbindable");
	if (flags & MS_PRIVATE)
		audit_log_format(ab, flags & MS_REC ? ", rprivate" :
				 ", private");
	if (flags & MS_SLAVE)
		audit_log_format(ab, flags & MS_REC ? ", rslave" :
				 ", slave");
	if (flags & MS_SHARED)
		audit_log_format(ab, flags & MS_REC ? ", rshared" :
				 ", shared");
	if (flags & MS_RELATIME)
		audit_log_format(ab, ", relatime");
	if (flags & MS_I_VERSION)
		audit_log_format(ab, ", iversion");
	if (flags & MS_STRICTATIME)
		audit_log_format(ab, ", strictatime");
	if (flags & MS_NOUSER)
		audit_log_format(ab, ", nouser");
}

/**
 * audit_cb - call back for mount specific audit fields
 * @ab: audit_buffer  (NOT NULL)
 * @va: audit struct to audit values of  (NOT NULL)
 */
static void audit_cb(struct audit_buffer *ab, void *va)
{
	struct common_audit_data *sa = va;
	struct apparmor_audit_data *ad = aad(sa);

	if (ad->mnt.type) {
		audit_log_format(ab, " fstype=");
		audit_log_untrustedstring(ab, ad->mnt.type);
	}
	if (ad->mnt.src_name) {
		audit_log_format(ab, " srcname=");
		audit_log_untrustedstring(ab, ad->mnt.src_name);
	}
	if (ad->mnt.trans) {
		audit_log_format(ab, " trans=");
		audit_log_untrustedstring(ab, ad->mnt.trans);
	}
	if (ad->mnt.flags) {
		audit_log_format(ab, " flags=\"");
		audit_mnt_flags(ab, ad->mnt.flags);
		audit_log_format(ab, "\"");
	}
	if (ad->mnt.data) {
		audit_log_format(ab, " options=");
		audit_log_untrustedstring(ab, ad->mnt.data);
	}
}

/**
 * match_mnt_flags - Do an ordered match on mount flags
 * @dfa: dfa to match against
 * @state: state to start in
 * @flags: mount flags to match against
 *
 * Mount flags are encoded as an ordered match. This is done instead of
 * checking against a simple bitmask, to allow for logical operations
 * on the flags.
 *
 * Returns: next state after flags match
 */
static aa_state_t match_mnt_flags(const struct aa_dfa *dfa, aa_state_t state,
				    unsigned long flags)
{
	unsigned int i;

	for (i = 0; i <= 31 ; ++i) {
		if ((1 << i) & flags)
			state = aa_dfa_next(dfa, state, i + 1);
	}

	return state;
}

static const char * const mnt_info_table[] = {
	"match succeeded",
	"failed mntpnt match",
	"failed srcname match",
	"failed type match",
	"failed flags match",
	"failed data match",
	"failed perms check"
};

/*
 * Returns 0 on success else element that match failed in, this is the
 * index into the mnt_info_table above
 */
static int do_match_mnt(struct aa_policydb *policy, aa_state_t start,
			const char *mntpnt, const char *devname,
			const char *type, unsigned long flags,
			void *data, bool binary, struct aa_perms *perms)
{
	aa_state_t state;

	AA_BUG(!policy);
	AA_BUG(!policy->dfa);
	AA_BUG(!policy->perms);
	AA_BUG(!perms);

	state = aa_dfa_match(policy->dfa, start, mntpnt);
	state = aa_dfa_null_transition(policy->dfa, state);
	if (!state)
		return 1;

	if (devname)
		state = aa_dfa_match(policy->dfa, state, devname);
	state = aa_dfa_null_transition(policy->dfa, state);
	if (!state)
		return 2;

	if (type)
		state = aa_dfa_match(policy->dfa, state, type);
	state = aa_dfa_null_transition(policy->dfa, state);
	if (!state)
		return 3;

	state = match_mnt_flags(policy->dfa, state, flags);
	if (!state)
		return 4;
	*perms = *aa_lookup_perms(policy, state);
	if (perms->allow & AA_MAY_MOUNT)
		return 0;

	/* only match data if not binary and the DFA flags data is expected */
	if (data && !binary && (perms->allow & AA_MNT_CONT_MATCH)) {
		state = aa_dfa_null_transition(policy->dfa, state);
		if (!state)
			return 4;

		state = aa_dfa_match(policy->dfa, state, data);
		if (!state)
			return 5;
		*perms = *aa_lookup_perms(policy, state);
		if (perms->allow & AA_MAY_MOUNT)
			return 0;
	}

	/* failed at perms check, don't confuse with flags match */
	return 6;
}


static int path_flags(const struct aa_profile *profile, const struct path *path)
{
	AA_BUG(!profile);
	AA_BUG(!path);

	return profile->path_flags |
		(S_ISDIR(path->dentry->d_inode->i_mode) ? PATH_IS_DIR : 0);
}

/**
 * match_mnt_path_str - handle path matching for mount
 * @profile: the confining profile
 * @mntpath: for the mntpnt (NOT NULL)
 * @buffer: buffer to be used to lookup mntpath
 * @devname: string for the devname/src_name (MAY BE NULL OR ERRPTR)
 * @type: string for the dev type (MAYBE NULL)
 * @flags: mount flags to match
 * @data: fs mount data (MAYBE NULL)
 * @binary: whether @data is binary
 * @devinfo: error str if (IS_ERR(@devname))
 * @ad: apparmor audit data structure
 *
 * Returns: 0 on success else error
 */
static int match_mnt_path_str(struct aa_profile *profile,
			      const struct path *mntpath, char *buffer,
			      const char *devname, const char *type,
			      unsigned long flags, void *data, bool binary,
			      const char *devinfo,
			      struct apparmor_audit_data *ad)
{
	struct aa_perms perms = { };
	const char *mntpnt = NULL;
	struct aa_ruleset *rules = profile->label.rules[0];
	int pos, error;

	AA_BUG(!profile);
	AA_BUG(!mntpath);
	AA_BUG(!buffer);

	if (!RULE_MEDIATES(rules, AA_CLASS_MOUNT))
		return 0;

	ad->mnt.type = type;

	error = aa_path_name(mntpath, path_flags(profile, mntpath), buffer,
			     &mntpnt, &ad->info, profile->disconnected);
	if (error)
		return aa_audit_perm_error(&profile->label, AA_MAY_MOUNT,
					   error, ad, audit_cb);
	ad->name = mntpnt;

	if (IS_ERR(devname)) {
		error = PTR_ERR(devname);
		ad->info = devinfo;
		return aa_audit_perm_error(&profile->label, AA_MAY_MOUNT,
					   error, ad, audit_cb);
	}
	ad->mnt.src_name = devname;

	pos = do_match_mnt(rules->policy,
			   rules->policy->start[AA_CLASS_MOUNT],
			   mntpnt, devname, type, flags, data, binary, &perms);
	if (pos)
		ad->info = mnt_info_table[pos];

	aa_apply_modes_to_perms(profile, &perms);
	if (data && !binary && (perms.audit & AA_AUDIT_DATA))
		ad->mnt.data = data;
	return aa_check_perms(profile, &perms, AA_MAY_MOUNT, ad, audit_cb);
}

/**
 * match_mnt - handle path matching for mount
 * @profile: the confining profile
 * @path: for the mntpnt (NOT NULL)
 * @buffer: buffer to be used to lookup mntpath
 * @devpath: path devname/src_name (MAYBE NULL)
 * @devbuffer: buffer to be used to lookup devname/src_name
 * @type: string for the dev type (MAYBE NULL)
 * @flags: mount flags to match
 * @data: fs mount data (MAYBE NULL)
 * @binary: whether @data is binary
 * @ad: apparmor audit data structure
 *
 * Returns: 0 on success else error
 */
static int match_mnt(struct aa_profile *profile, const struct path *path,
		     char *buffer, const struct path *devpath, char *devbuffer,
		     const char *type, unsigned long flags, void *data,
		     bool binary, struct apparmor_audit_data *ad)
{
	const char *devname = NULL, *info = NULL;
	struct aa_ruleset *rules = profile->label.rules[0];
	int error = -EACCES;

	AA_BUG(!profile);
	AA_BUG(devpath && !devbuffer);

	if (!RULE_MEDIATES(rules, AA_CLASS_MOUNT))
		return 0;

	if (devpath) {
		error = aa_path_name(devpath, path_flags(profile, devpath),
				     devbuffer, &devname, &info,
				     profile->disconnected);
		if (error)
			devname = ERR_PTR(error);
	}

	return match_mnt_path_str(profile, path, buffer, devname,
				  type, flags, data, binary, info, ad);
}

int aa_remount(const struct cred *subj_cred,
	       struct aa_label *label, const struct path *path,
	       unsigned long flags, void *data)
{
	struct aa_profile *profile;
	char *buffer = NULL;
	bool binary;
	int error;
	DEFINE_AUDIT_MOUNT(ad, OP_MOUNT, subj_cred);
	ad.mnt.flags = flags;

	AA_BUG(!label);
	AA_BUG(!path);

	binary = path->dentry->d_sb->s_type->fs_flags & FS_BINARY_MOUNTDATA;

	buffer = aa_get_buffer(false);
	if (!buffer)
		return -ENOMEM;
	error = fn_for_each(label, profile,
			match_mnt(profile, path, buffer, NULL, NULL, NULL,
				  flags, data, binary, &ad));
	aa_put_buffer(buffer);

	return error;
}

int aa_bind_mount(const struct cred *subj_cred,
		  struct aa_label *label, const struct path *path,
		  const char *dev_name, unsigned long flags)
{
	struct aa_profile *profile;
	char *buffer = NULL, *old_buffer = NULL;
	struct path old_path;
	int error;
	DEFINE_AUDIT_MOUNT(ad, OP_MOUNT, subj_cred);

	AA_BUG(!label);
	AA_BUG(!path);

	if (!dev_name || !*dev_name)
		return -EINVAL;

	flags &= MS_REC | MS_BIND;
	ad.mnt.flags = flags;

	error = kern_path(dev_name, LOOKUP_FOLLOW|LOOKUP_AUTOMOUNT, &old_path);
	if (error)
		return aa_audit_perm_error(label, AA_MAY_MOUNT, error, &ad,
			audit_cb);

	buffer = aa_get_buffer(false);
	old_buffer = aa_get_buffer(false);
	error = -ENOMEM;
	if (!buffer || !old_buffer)
		goto out;

	error = fn_for_each(label, profile,
			match_mnt(profile, path, buffer, &old_path,
				  old_buffer, NULL, flags, NULL, false, &ad));
out:
	aa_put_buffer(buffer);
	aa_put_buffer(old_buffer);
	path_put(&old_path);

	return error;
}

int aa_mount_change_type(const struct cred *subj_cred,
			 struct aa_label *label, const struct path *path,
			 unsigned long flags)
{
	struct aa_profile *profile;
	char *buffer = NULL;
	int error;
	DEFINE_AUDIT_MOUNT(ad, OP_MOUNT, subj_cred);

	AA_BUG(!label);
	AA_BUG(!path);

	/* These are the flags allowed by do_change_type() */
	flags &= (MS_REC | MS_SILENT | MS_SHARED | MS_PRIVATE | MS_SLAVE |
		  MS_UNBINDABLE);
	ad.mnt.flags = flags;

	buffer = aa_get_buffer(false);
	if (!buffer)
		return -ENOMEM;
	error = fn_for_each(label, profile,
			match_mnt(profile, path, buffer, NULL, NULL, NULL,
				  flags, NULL, false, &ad));
	aa_put_buffer(buffer);

	return error;
}

int aa_move_mount(const struct cred *subj_cred,
		  struct aa_label *label, const struct path *from_path,
		  const struct path *to_path)
{
	struct aa_profile *profile;
	char *to_buffer = NULL, *from_buffer = NULL;
	int error;
	DEFINE_AUDIT_MOUNT(ad, OP_MOUNT, subj_cred);
	ad.mnt.flags = MS_MOVE;

	AA_BUG(!label);
	AA_BUG(!from_path);
	AA_BUG(!to_path);

	to_buffer = aa_get_buffer(false);
	from_buffer = aa_get_buffer(false);
	error = -ENOMEM;
	if (!to_buffer || !from_buffer)
		goto out;

	if (!our_mnt(from_path->mnt))
		/* moving a mount detached from the namespace */
		from_path = NULL;
	error = fn_for_each(label, profile,
			match_mnt(profile, to_path, to_buffer, from_path,
				  from_buffer, NULL, MS_MOVE, NULL, false,
				  &ad));
out:
	aa_put_buffer(to_buffer);
	aa_put_buffer(from_buffer);

	return error;
}

int aa_move_mount_old(const struct cred *subj_cred, struct aa_label *label,
		      const struct path *path, const char *orig_name)
{
	struct path old_path;
	int error;

	if (!orig_name || !*orig_name)
		return -EINVAL;
	error = kern_path(orig_name, LOOKUP_FOLLOW, &old_path);
	if (error)
		return error;

	error = aa_move_mount(subj_cred, label, &old_path, path);
	path_put(&old_path);

	return error;
}

int aa_new_mount(const struct cred *subj_cred, struct aa_label *label,
		 const char *dev_name, const struct path *path,
		 const char *type, unsigned long flags, void *data)
{
	struct aa_profile *profile;
	char *buffer = NULL, *dev_buffer = NULL;
	bool binary = true;
	int error;
	int requires_dev = 0;
	struct path tmp_path, *dev_path = NULL;
	DEFINE_AUDIT_MOUNT(ad, OP_MOUNT, subj_cred);
	ad.mnt.flags = flags;

	AA_BUG(!label);
	AA_BUG(!path);

	if (type) {
		struct file_system_type *fstype;

		fstype = get_fs_type(type);
		if (!fstype)
			return -ENODEV;
		binary = fstype->fs_flags & FS_BINARY_MOUNTDATA;
		requires_dev = fstype->fs_flags & FS_REQUIRES_DEV;
		put_filesystem(fstype);

		if (requires_dev) {
			if (!dev_name || !*dev_name)
				return -ENOENT;

			error = kern_path(dev_name, LOOKUP_FOLLOW, &tmp_path);
			if (error)
				return error;
			dev_path = &tmp_path;
		}
	}

	buffer = aa_get_buffer(false);
	if (!buffer) {
		error = -ENOMEM;
		goto out;
	}
	if (dev_path) {
		dev_buffer = aa_get_buffer(false);
		if (!dev_buffer) {
			error = -ENOMEM;
			goto out;
		}
		error = fn_for_each(label, profile,
				match_mnt(profile, path, buffer, dev_path,
					  dev_buffer, type, flags, data,
					  binary, &ad));
	} else {
		error = fn_for_each(label, profile,
				match_mnt_path_str(profile, path, buffer,
						   dev_name, type, flags, data,
						   binary, NULL, &ad));
	}

out:
	aa_put_buffer(buffer);
	aa_put_buffer(dev_buffer);
	if (dev_path)
		path_put(dev_path);

	return error;
}

static int profile_umount(struct aa_profile *profile, const struct path *path,
			  char *buffer, struct apparmor_audit_data *ad)
{
	struct aa_ruleset *rules = profile->label.rules[0];
	struct aa_perms perms = { };
	const char *name = NULL;
	aa_state_t state;
	int error;

	AA_BUG(!profile);
	AA_BUG(!path);

	if (!RULE_MEDIATES(rules, AA_CLASS_MOUNT))
		return 0;

	/* TODO: lift path_name, need to separate profile path_flags from
	 * the lookup
	 */
	error = aa_path_name(path, path_flags(profile, path), buffer, &name,
			     &ad->info, profile->disconnected);
	if (error)
		return aa_audit_perm_error(&profile->label, AA_MAY_UMOUNT,
					   error, ad, audit_cb);

	ad->name = name;
	state = aa_dfa_match(rules->policy->dfa,
			     rules->policy->start[AA_CLASS_MOUNT],
			     name);
	perms = *aa_lookup_perms(rules->policy, state);

	aa_apply_modes_to_perms(profile, &perms);
	return aa_check_perms(profile, &perms, AA_MAY_UMOUNT, ad, audit_cb);
}

int aa_umount(const struct cred *subj_cred, struct aa_label *label,
	      struct vfsmount *mnt, int flags)
{
	struct aa_profile *profile;
	char *buffer = NULL;
	int error;
	struct path path = { .mnt = mnt, .dentry = mnt->mnt_root };
	DEFINE_AUDIT_MOUNT(ad, OP_UMOUNT, subj_cred);

	AA_BUG(!label);
	AA_BUG(!mnt);

	buffer = aa_get_buffer(false);
	if (!buffer)
		return -ENOMEM;

	error = fn_for_each(label, profile,
			profile_umount(profile, &path, buffer, &ad));
	aa_put_buffer(buffer);

	return error;
}

/* helper fn for transition on pivotroot
 *
 * Returns: label for transition or ERR_PTR. Does not return NULL
 */
static struct aa_label *build_pivotroot(struct aa_profile *profile,
					const struct path *new_path,
					char *new_buffer,
					const struct path *old_path,
					char *old_buffer,
					struct apparmor_audit_data *ad)
{
	struct aa_ruleset *rules = profile->label.rules[0];
	const char *old_name, *new_name = NULL;
	struct aa_perms perms = { };
	aa_state_t state;
	int error;

	AA_BUG(!profile);
	AA_BUG(!new_path);
	AA_BUG(!old_path);

	if (profile_unconfined(profile) ||
	    !RULE_MEDIATES(rules, AA_CLASS_MOUNT))
		return aa_get_newest_label(&profile->label);

	error = aa_path_name(old_path, path_flags(profile, old_path),
			     old_buffer, &old_name, &ad->info,
			     profile->disconnected);
	if (error)
		goto err;
	ad->mnt.src_name = old_name;
	error = aa_path_name(new_path, path_flags(profile, new_path),
			     new_buffer, &new_name, &ad->info,
			     profile->disconnected);
	if (error)
		goto err;
	ad->name = new_name;

	state = aa_dfa_match(rules->policy->dfa,
			     rules->policy->start[AA_CLASS_MOUNT],
			     new_name);
	state = aa_dfa_null_transition(rules->policy->dfa, state);
	state = aa_dfa_match(rules->policy->dfa, state, old_name);
	perms = *aa_lookup_perms(rules->policy, state);
	/* todo: allow pivotroot to specify a transition other than profile */
	ad->mnt.trans = profile->label.hname;

	aa_apply_modes_to_perms(profile, &perms);
	error = aa_check_perms(profile, &perms, AA_MAY_PIVOTROOT, ad, audit_cb);

out:
	if (error)
		return ERR_PTR(error);

	return aa_get_newest_label(&profile->label);

err:
	error = aa_audit_perm_error(&profile->label, AA_MAY_PIVOTROOT, error,
				    ad, audit_cb);
	goto out;
}

int aa_pivotroot(const struct cred *subj_cred, struct aa_label *label,
		 const struct path *old_path,
		 const struct path *new_path)
{
	struct aa_profile *profile;
	struct aa_label *target = NULL;
	char *old_buffer = NULL, *new_buffer = NULL;
	int error;
	DEFINE_AUDIT_MOUNT(ad, OP_PIVOTROOT, subj_cred);

	AA_BUG(!label);
	AA_BUG(!old_path);
	AA_BUG(!new_path);

	old_buffer = aa_get_buffer(false);
	new_buffer = aa_get_buffer(false);
	error = -ENOMEM;
	if (!old_buffer || !new_buffer)
		goto out;
	target = fn_label_build(label, profile, GFP_KERNEL,
			build_pivotroot(profile, new_path, new_buffer,
					old_path, old_buffer, &ad));
	AA_BUG(!target);
	if (!IS_ERR(target)) {
		error = aa_replace_current_label(target);
		if (error)
			goto fail;
		aa_put_label(target);
	} else
		/* already audited error in build_pivotroot */
		error = PTR_ERR(target);
out:
	aa_put_buffer(old_buffer);
	aa_put_buffer(new_buffer);

	return error;

fail:
	/* TODO: add back in auditing of new_name and old_name,
	 * needs lifting of name lookup out of profile cb
	 */
	ad.mnt.trans = target->hname;
	error = aa_audit_perm_error(label, AA_MAY_PIVOTROOT, error, &ad,
				    audit_cb);
	aa_put_label(target);
	goto out;
}
