// SPDX-License-Identifier: GPL-2.0-only
/*
 * AppArmor security module
 *
 * This file contains AppArmor LSM hooks.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 */

#include <linux/lsm_hooks.h>
#include <linux/moduleparam.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/ptrace.h>
#include <linux/ctype.h>
#include <linux/sysctl.h>
#include <linux/audit.h>
#include <linux/user_namespace.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <linux/zstd.h>
#include <net/sock.h>
#include <uapi/linux/mount.h>
#include <uapi/linux/lsm.h>

#include "include/apparmor.h"
#include "include/apparmorfs.h"
#include "include/audit.h"
#include "include/capability.h"
#include "include/cred.h"
#include "include/file.h"
#include "include/ipc.h"
#include "include/net.h"
#include "include/path.h"
#include "include/label.h"
#include "include/policy.h"
#include "include/policy_ns.h"
#include "include/procattr.h"
#include "include/mount.h"
#include "include/secid.h"

/* Flag indicating whether initialization completed */
int apparmor_initialized;

union aa_buffer {
	struct list_head list;
	DECLARE_FLEX_ARRAY(char, buffer);
};

struct aa_local_cache {
	unsigned int hold;
	unsigned int count;
	struct list_head head;
};

#define RESERVE_COUNT 2
static int reserve_count = RESERVE_COUNT;
static int buffer_count;

static LIST_HEAD(aa_global_buffers);
static DEFINE_SPINLOCK(aa_buffers_lock);
static DEFINE_PER_CPU(struct aa_local_cache, aa_local_buffers);

/*
 * LSM hook functions
 */

/*
 * put the associated labels
 */
static void apparmor_cred_free(struct cred *cred)
{
	aa_put_label(cred_label(cred));
	set_cred_label(cred, NULL);
}

/*
 * allocate the apparmor part of blank credentials
 */
static int apparmor_cred_alloc_blank(struct cred *cred, gfp_t gfp)
{
	set_cred_label(cred, NULL);
	return 0;
}

/*
 * prepare new cred label for modification by prepare_cred block
 */
static int apparmor_cred_prepare(struct cred *new, const struct cred *old,
				 gfp_t gfp)
{
	set_cred_label(new, aa_get_newest_label(cred_label(old)));
	return 0;
}

/*
 * transfer the apparmor data to a blank set of creds
 */
static void apparmor_cred_transfer(struct cred *new, const struct cred *old)
{
	set_cred_label(new, aa_get_newest_label(cred_label(old)));
}

static void apparmor_task_free(struct task_struct *task)
{

	aa_free_task_ctx(task_ctx(task));
}

static int apparmor_task_alloc(struct task_struct *task,
			       unsigned long clone_flags)
{
	struct aa_task_ctx *new = task_ctx(task);

	aa_dup_task_ctx(new, task_ctx(current));

	return 0;
}

static int apparmor_ptrace_access_check(struct task_struct *child,
					unsigned int mode)
{
	struct aa_label *tracer, *tracee;
	const struct cred *cred;
	int error;

	cred = get_task_cred(child);
	tracee = cred_label(cred);	/* ref count on cred */
	tracer = __begin_current_label_crit_section();
	error = aa_may_ptrace(current_cred(), tracer, cred, tracee,
			(mode & PTRACE_MODE_READ) ? AA_PTRACE_READ
						  : AA_PTRACE_TRACE);
	__end_current_label_crit_section(tracer);
	put_cred(cred);

	return error;
}

static int apparmor_ptrace_traceme(struct task_struct *parent)
{
	struct aa_label *tracer, *tracee;
	const struct cred *cred;
	int error;

	tracee = __begin_current_label_crit_section();
	cred = get_task_cred(parent);
	tracer = cred_label(cred);	/* ref count on cred */
	error = aa_may_ptrace(cred, tracer, current_cred(), tracee,
			      AA_PTRACE_TRACE);
	put_cred(cred);
	__end_current_label_crit_section(tracee);

	return error;
}

/* Derived from security/commoncap.c:cap_capget */
static int apparmor_capget(const struct task_struct *target, kernel_cap_t *effective,
			   kernel_cap_t *inheritable, kernel_cap_t *permitted)
{
	struct aa_label *label;
	const struct cred *cred;

	rcu_read_lock();
	cred = __task_cred(target);
	label = aa_get_newest_cred_label(cred);

	/*
	 * cap_capget is stacked ahead of this and will
	 * initialize effective and permitted.
	 */
	if (!unconfined(label)) {
		struct aa_profile *profile;
		struct label_it i;

		label_for_each_confined(i, label, profile) {
			struct aa_ruleset *rules;
			if (COMPLAIN_MODE(profile))
				continue;
			rules = list_first_entry(&profile->rules,
						 typeof(*rules), list);
			*effective = cap_intersect(*effective,
						   rules->caps.allow);
			*permitted = cap_intersect(*permitted,
						   rules->caps.allow);
		}
	}
	rcu_read_unlock();
	aa_put_label(label);

	return 0;
}

static int apparmor_capable(const struct cred *cred, struct user_namespace *ns,
			    int cap, unsigned int opts)
{
	struct aa_label *label;
	int error = 0;

	label = aa_get_newest_cred_label(cred);
	if (!unconfined(label))
		error = aa_capable(cred, label, cap, opts);
	aa_put_label(label);

	return error;
}

/**
 * common_perm - basic common permission check wrapper fn for paths
 * @op: operation being checked
 * @path: path to check permission of  (NOT NULL)
 * @mask: requested permissions mask
 * @cond: conditional info for the permission request  (NOT NULL)
 *
 * Returns: %0 else error code if error or permission denied
 */
static int common_perm(const char *op, const struct path *path, u32 mask,
		       struct path_cond *cond)
{
	struct aa_label *label;
	int error = 0;

	label = __begin_current_label_crit_section();
	if (!unconfined(label))
		error = aa_path_perm(op, current_cred(), label, path, 0, mask,
				     cond);
	__end_current_label_crit_section(label);

	return error;
}

/**
 * common_perm_cond - common permission wrapper around inode cond
 * @op: operation being checked
 * @path: location to check (NOT NULL)
 * @mask: requested permissions mask
 *
 * Returns: %0 else error code if error or permission denied
 */
static int common_perm_cond(const char *op, const struct path *path, u32 mask)
{
	vfsuid_t vfsuid = i_uid_into_vfsuid(mnt_idmap(path->mnt),
					    d_backing_inode(path->dentry));
	struct path_cond cond = {
		vfsuid_into_kuid(vfsuid),
		d_backing_inode(path->dentry)->i_mode
	};

	if (!path_mediated_fs(path->dentry))
		return 0;

	return common_perm(op, path, mask, &cond);
}

/**
 * common_perm_dir_dentry - common permission wrapper when path is dir, dentry
 * @op: operation being checked
 * @dir: directory of the dentry  (NOT NULL)
 * @dentry: dentry to check  (NOT NULL)
 * @mask: requested permissions mask
 * @cond: conditional info for the permission request  (NOT NULL)
 *
 * Returns: %0 else error code if error or permission denied
 */
static int common_perm_dir_dentry(const char *op, const struct path *dir,
				  struct dentry *dentry, u32 mask,
				  struct path_cond *cond)
{
	struct path path = { .mnt = dir->mnt, .dentry = dentry };

	return common_perm(op, &path, mask, cond);
}

/**
 * common_perm_rm - common permission wrapper for operations doing rm
 * @op: operation being checked
 * @dir: directory that the dentry is in  (NOT NULL)
 * @dentry: dentry being rm'd  (NOT NULL)
 * @mask: requested permission mask
 *
 * Returns: %0 else error code if error or permission denied
 */
static int common_perm_rm(const char *op, const struct path *dir,
			  struct dentry *dentry, u32 mask)
{
	struct inode *inode = d_backing_inode(dentry);
	struct path_cond cond = { };
	vfsuid_t vfsuid;

	if (!inode || !path_mediated_fs(dentry))
		return 0;

	vfsuid = i_uid_into_vfsuid(mnt_idmap(dir->mnt), inode);
	cond.uid = vfsuid_into_kuid(vfsuid);
	cond.mode = inode->i_mode;

	return common_perm_dir_dentry(op, dir, dentry, mask, &cond);
}

/**
 * common_perm_create - common permission wrapper for operations doing create
 * @op: operation being checked
 * @dir: directory that dentry will be created in  (NOT NULL)
 * @dentry: dentry to create   (NOT NULL)
 * @mask: request permission mask
 * @mode: created file mode
 *
 * Returns: %0 else error code if error or permission denied
 */
static int common_perm_create(const char *op, const struct path *dir,
			      struct dentry *dentry, u32 mask, umode_t mode)
{
	struct path_cond cond = { current_fsuid(), mode };

	if (!path_mediated_fs(dir->dentry))
		return 0;

	return common_perm_dir_dentry(op, dir, dentry, mask, &cond);
}

static int apparmor_path_unlink(const struct path *dir, struct dentry *dentry)
{
	return common_perm_rm(OP_UNLINK, dir, dentry, AA_MAY_DELETE);
}

static int apparmor_path_mkdir(const struct path *dir, struct dentry *dentry,
			       umode_t mode)
{
	return common_perm_create(OP_MKDIR, dir, dentry, AA_MAY_CREATE,
				  S_IFDIR);
}

static int apparmor_path_rmdir(const struct path *dir, struct dentry *dentry)
{
	return common_perm_rm(OP_RMDIR, dir, dentry, AA_MAY_DELETE);
}

static int apparmor_path_mknod(const struct path *dir, struct dentry *dentry,
			       umode_t mode, unsigned int dev)
{
	return common_perm_create(OP_MKNOD, dir, dentry, AA_MAY_CREATE, mode);
}

static int apparmor_path_truncate(const struct path *path)
{
	return common_perm_cond(OP_TRUNC, path, MAY_WRITE | AA_MAY_SETATTR);
}

static int apparmor_file_truncate(struct file *file)
{
	return apparmor_path_truncate(&file->f_path);
}

static int apparmor_path_symlink(const struct path *dir, struct dentry *dentry,
				 const char *old_name)
{
	return common_perm_create(OP_SYMLINK, dir, dentry, AA_MAY_CREATE,
				  S_IFLNK);
}

static int apparmor_path_link(struct dentry *old_dentry, const struct path *new_dir,
			      struct dentry *new_dentry)
{
	struct aa_label *label;
	int error = 0;

	if (!path_mediated_fs(old_dentry))
		return 0;

	label = begin_current_label_crit_section();
	if (!unconfined(label))
		error = aa_path_link(current_cred(), label, old_dentry, new_dir,
				     new_dentry);
	end_current_label_crit_section(label);

	return error;
}

static int apparmor_path_rename(const struct path *old_dir, struct dentry *old_dentry,
				const struct path *new_dir, struct dentry *new_dentry,
				const unsigned int flags)
{
	struct aa_label *label;
	int error = 0;

	if (!path_mediated_fs(old_dentry))
		return 0;
	if ((flags & RENAME_EXCHANGE) && !path_mediated_fs(new_dentry))
		return 0;

	label = begin_current_label_crit_section();
	if (!unconfined(label)) {
		struct mnt_idmap *idmap = mnt_idmap(old_dir->mnt);
		vfsuid_t vfsuid;
		struct path old_path = { .mnt = old_dir->mnt,
					 .dentry = old_dentry };
		struct path new_path = { .mnt = new_dir->mnt,
					 .dentry = new_dentry };
		struct path_cond cond = {
			.mode = d_backing_inode(old_dentry)->i_mode
		};
		vfsuid = i_uid_into_vfsuid(idmap, d_backing_inode(old_dentry));
		cond.uid = vfsuid_into_kuid(vfsuid);

		if (flags & RENAME_EXCHANGE) {
			struct path_cond cond_exchange = {
				.mode = d_backing_inode(new_dentry)->i_mode,
			};
			vfsuid = i_uid_into_vfsuid(idmap, d_backing_inode(old_dentry));
			cond_exchange.uid = vfsuid_into_kuid(vfsuid);

			error = aa_path_perm(OP_RENAME_SRC, current_cred(),
					     label, &new_path, 0,
					     MAY_READ | AA_MAY_GETATTR | MAY_WRITE |
					     AA_MAY_SETATTR | AA_MAY_DELETE,
					     &cond_exchange);
			if (!error)
				error = aa_path_perm(OP_RENAME_DEST, current_cred(),
						     label, &old_path,
						     0, MAY_WRITE | AA_MAY_SETATTR |
						     AA_MAY_CREATE, &cond_exchange);
		}

		if (!error)
			error = aa_path_perm(OP_RENAME_SRC, current_cred(),
					     label, &old_path, 0,
					     MAY_READ | AA_MAY_GETATTR | MAY_WRITE |
					     AA_MAY_SETATTR | AA_MAY_DELETE,
					     &cond);
		if (!error)
			error = aa_path_perm(OP_RENAME_DEST, current_cred(),
					     label, &new_path,
					     0, MAY_WRITE | AA_MAY_SETATTR |
					     AA_MAY_CREATE, &cond);

	}
	end_current_label_crit_section(label);

	return error;
}

static int apparmor_path_chmod(const struct path *path, umode_t mode)
{
	return common_perm_cond(OP_CHMOD, path, AA_MAY_CHMOD);
}

static int apparmor_path_chown(const struct path *path, kuid_t uid, kgid_t gid)
{
	return common_perm_cond(OP_CHOWN, path, AA_MAY_CHOWN);
}

static int apparmor_inode_getattr(const struct path *path)
{
	return common_perm_cond(OP_GETATTR, path, AA_MAY_GETATTR);
}

static int apparmor_file_open(struct file *file)
{
	struct aa_file_ctx *fctx = file_ctx(file);
	struct aa_label *label;
	int error = 0;
	bool needput;

	if (!path_mediated_fs(file->f_path.dentry))
		return 0;

	/* If in exec, permission is handled by bprm hooks.
	 * Cache permissions granted by the previous exec check, with
	 * implicit read and executable mmap which are required to
	 * actually execute the image.
	 *
	 * Illogically, FMODE_EXEC is in f_flags, not f_mode.
	 */
	if (file->f_flags & __FMODE_EXEC) {
		fctx->allow = MAY_EXEC | MAY_READ | AA_EXEC_MMAP;
		return 0;
	}

	label = aa_get_newest_cred_label_condref(file->f_cred, &needput);
	if (!unconfined(label)) {
		struct mnt_idmap *idmap = file_mnt_idmap(file);
		struct inode *inode = file_inode(file);
		vfsuid_t vfsuid;
		struct path_cond cond = {
			.mode = inode->i_mode,
		};
		vfsuid = i_uid_into_vfsuid(idmap, inode);
		cond.uid = vfsuid_into_kuid(vfsuid);

		error = aa_path_perm(OP_OPEN, file->f_cred,
				     label, &file->f_path, 0,
				     aa_map_file_to_perms(file), &cond);
		/* todo cache full allowed permissions set and state */
		fctx->allow = aa_map_file_to_perms(file);
	}
	aa_put_label_condref(label, needput);

	return error;
}

static int apparmor_file_alloc_security(struct file *file)
{
	struct aa_file_ctx *ctx = file_ctx(file);
	struct aa_label *label = begin_current_label_crit_section();

	spin_lock_init(&ctx->lock);
	rcu_assign_pointer(ctx->label, aa_get_label(label));
	end_current_label_crit_section(label);
	return 0;
}

static void apparmor_file_free_security(struct file *file)
{
	struct aa_file_ctx *ctx = file_ctx(file);

	if (ctx)
		aa_put_label(rcu_access_pointer(ctx->label));
}

static int common_file_perm(const char *op, struct file *file, u32 mask,
			    bool in_atomic)
{
	struct aa_label *label;
	int error = 0;

	/* don't reaudit files closed during inheritance */
	if (file->f_path.dentry == aa_null.dentry)
		return -EACCES;

	label = __begin_current_label_crit_section();
	error = aa_file_perm(op, current_cred(), label, file, mask, in_atomic);
	__end_current_label_crit_section(label);

	return error;
}

static int apparmor_file_receive(struct file *file)
{
	return common_file_perm(OP_FRECEIVE, file, aa_map_file_to_perms(file),
				false);
}

static int apparmor_file_permission(struct file *file, int mask)
{
	return common_file_perm(OP_FPERM, file, mask, false);
}

static int apparmor_file_lock(struct file *file, unsigned int cmd)
{
	u32 mask = AA_MAY_LOCK;

	if (cmd == F_WRLCK)
		mask |= MAY_WRITE;

	return common_file_perm(OP_FLOCK, file, mask, false);
}

static int common_mmap(const char *op, struct file *file, unsigned long prot,
		       unsigned long flags, bool in_atomic)
{
	int mask = 0;

	if (!file || !file_ctx(file))
		return 0;

	if (prot & PROT_READ)
		mask |= MAY_READ;
	/*
	 * Private mappings don't require write perms since they don't
	 * write back to the files
	 */
	if ((prot & PROT_WRITE) && !(flags & MAP_PRIVATE))
		mask |= MAY_WRITE;
	if (prot & PROT_EXEC)
		mask |= AA_EXEC_MMAP;

	return common_file_perm(op, file, mask, in_atomic);
}

static int apparmor_mmap_file(struct file *file, unsigned long reqprot,
			      unsigned long prot, unsigned long flags)
{
	return common_mmap(OP_FMMAP, file, prot, flags, GFP_ATOMIC);
}

static int apparmor_file_mprotect(struct vm_area_struct *vma,
				  unsigned long reqprot, unsigned long prot)
{
	return common_mmap(OP_FMPROT, vma->vm_file, prot,
			   !(vma->vm_flags & VM_SHARED) ? MAP_PRIVATE : 0,
			   false);
}

#ifdef CONFIG_IO_URING
static const char *audit_uring_mask(u32 mask)
{
	if (mask & AA_MAY_CREATE_SQPOLL)
		return "sqpoll";
	if (mask & AA_MAY_OVERRIDE_CRED)
		return "override_creds";
	return "";
}

static void audit_uring_cb(struct audit_buffer *ab, void *va)
{
	struct apparmor_audit_data *ad = aad_of_va(va);

	if (ad->request & AA_URING_PERM_MASK) {
		audit_log_format(ab, " requested=\"%s\"",
				 audit_uring_mask(ad->request));
		if (ad->denied & AA_URING_PERM_MASK) {
			audit_log_format(ab, " denied=\"%s\"",
					 audit_uring_mask(ad->denied));
		}
	}
	if (ad->uring.target) {
		audit_log_format(ab, " tcontext=");
		aa_label_xaudit(ab, labels_ns(ad->subj_label),
				ad->uring.target,
				FLAGS_NONE, GFP_ATOMIC);
	}
}

static int profile_uring(struct aa_profile *profile, u32 request,
			 struct aa_label *new, int cap,
			 struct apparmor_audit_data *ad)
{
	unsigned int state;
	struct aa_ruleset *rules;
	int error = 0;

	AA_BUG(!profile);

	rules = list_first_entry(&profile->rules, typeof(*rules), list);
	state = RULE_MEDIATES(rules, AA_CLASS_IO_URING);
	if (state) {
		struct aa_perms perms = { };

		if (new) {
			aa_label_match(profile, rules, new, state,
				       false, request, &perms);
		} else {
			perms = *aa_lookup_perms(rules->policy, state);
		}
		aa_apply_modes_to_perms(profile, &perms);
		error = aa_check_perms(profile, &perms, request, ad,
				       audit_uring_cb);
	}

	return error;
}

/**
 * apparmor_uring_override_creds - check the requested cred override
 * @new: the target creds
 *
 * Check to see if the current task is allowed to override it's credentials
 * to service an io_uring operation.
 */
static int apparmor_uring_override_creds(const struct cred *new)
{
	struct aa_profile *profile;
	struct aa_label *label;
	int error;
	DEFINE_AUDIT_DATA(ad, LSM_AUDIT_DATA_NONE, AA_CLASS_IO_URING,
			  OP_URING_OVERRIDE);

	ad.uring.target = cred_label(new);
	label = __begin_current_label_crit_section();
	error = fn_for_each(label, profile,
			profile_uring(profile, AA_MAY_OVERRIDE_CRED,
				      cred_label(new), CAP_SYS_ADMIN, &ad));
	__end_current_label_crit_section(label);

	return error;
}

/**
 * apparmor_uring_sqpoll - check if a io_uring polling thread can be created
 *
 * Check to see if the current task is allowed to create a new io_uring
 * kernel polling thread.
 */
static int apparmor_uring_sqpoll(void)
{
	struct aa_profile *profile;
	struct aa_label *label;
	int error;
	DEFINE_AUDIT_DATA(ad, LSM_AUDIT_DATA_NONE, AA_CLASS_IO_URING,
			  OP_URING_SQPOLL);

	label = __begin_current_label_crit_section();
	error = fn_for_each(label, profile,
			profile_uring(profile, AA_MAY_CREATE_SQPOLL,
				      NULL, CAP_SYS_ADMIN, &ad));
	__end_current_label_crit_section(label);

	return error;
}
#endif /* CONFIG_IO_URING */

static int apparmor_sb_mount(const char *dev_name, const struct path *path,
			     const char *type, unsigned long flags, void *data)
{
	struct aa_label *label;
	int error = 0;

	/* Discard magic */
	if ((flags & MS_MGC_MSK) == MS_MGC_VAL)
		flags &= ~MS_MGC_MSK;

	flags &= ~AA_MS_IGNORE_MASK;

	label = __begin_current_label_crit_section();
	if (!unconfined(label)) {
		if (flags & MS_REMOUNT)
			error = aa_remount(current_cred(), label, path, flags,
					   data);
		else if (flags & MS_BIND)
			error = aa_bind_mount(current_cred(), label, path,
					      dev_name, flags);
		else if (flags & (MS_SHARED | MS_PRIVATE | MS_SLAVE |
				  MS_UNBINDABLE))
			error = aa_mount_change_type(current_cred(), label,
						     path, flags);
		else if (flags & MS_MOVE)
			error = aa_move_mount_old(current_cred(), label, path,
						  dev_name);
		else
			error = aa_new_mount(current_cred(), label, dev_name,
					     path, type, flags, data);
	}
	__end_current_label_crit_section(label);

	return error;
}

static int apparmor_move_mount(const struct path *from_path,
			       const struct path *to_path)
{
	struct aa_label *label;
	int error = 0;

	label = __begin_current_label_crit_section();
	if (!unconfined(label))
		error = aa_move_mount(current_cred(), label, from_path,
				      to_path);
	__end_current_label_crit_section(label);

	return error;
}

static int apparmor_sb_umount(struct vfsmount *mnt, int flags)
{
	struct aa_label *label;
	int error = 0;

	label = __begin_current_label_crit_section();
	if (!unconfined(label))
		error = aa_umount(current_cred(), label, mnt, flags);
	__end_current_label_crit_section(label);

	return error;
}

static int apparmor_sb_pivotroot(const struct path *old_path,
				 const struct path *new_path)
{
	struct aa_label *label;
	int error = 0;

	label = aa_get_current_label();
	if (!unconfined(label))
		error = aa_pivotroot(current_cred(), label, old_path, new_path);
	aa_put_label(label);

	return error;
}

static int apparmor_getselfattr(unsigned int attr, struct lsm_ctx __user *lx,
				u32 *size, u32 flags)
{
	int error = -ENOENT;
	struct aa_task_ctx *ctx = task_ctx(current);
	struct aa_label *label = NULL;
	char *value = NULL;

	switch (attr) {
	case LSM_ATTR_CURRENT:
		label = aa_get_newest_label(cred_label(current_cred()));
		break;
	case LSM_ATTR_PREV:
		if (ctx->previous)
			label = aa_get_newest_label(ctx->previous);
		break;
	case LSM_ATTR_EXEC:
		if (ctx->onexec)
			label = aa_get_newest_label(ctx->onexec);
		break;
	default:
		error = -EOPNOTSUPP;
		break;
	}

	if (label) {
		error = aa_getprocattr(label, &value, false);
		if (error > 0)
			error = lsm_fill_user_ctx(lx, size, value, error,
						  LSM_ID_APPARMOR, 0);
		kfree(value);
	}

	aa_put_label(label);

	if (error < 0)
		return error;
	return 1;
}

static int apparmor_getprocattr(struct task_struct *task, const char *name,
				char **value)
{
	int error = -ENOENT;
	/* released below */
	const struct cred *cred = get_task_cred(task);
	struct aa_task_ctx *ctx = task_ctx(current);
	struct aa_label *label = NULL;

	if (strcmp(name, "current") == 0)
		label = aa_get_newest_label(cred_label(cred));
	else if (strcmp(name, "prev") == 0  && ctx->previous)
		label = aa_get_newest_label(ctx->previous);
	else if (strcmp(name, "exec") == 0 && ctx->onexec)
		label = aa_get_newest_label(ctx->onexec);
	else
		error = -EINVAL;

	if (label)
		error = aa_getprocattr(label, value, true);

	aa_put_label(label);
	put_cred(cred);

	return error;
}

static int do_setattr(u64 attr, void *value, size_t size)
{
	char *command, *largs = NULL, *args = value;
	size_t arg_size;
	int error;
	DEFINE_AUDIT_DATA(ad, LSM_AUDIT_DATA_NONE, AA_CLASS_NONE,
			  OP_SETPROCATTR);

	if (size == 0)
		return -EINVAL;

	/* AppArmor requires that the buffer must be null terminated atm */
	if (args[size - 1] != '\0') {
		/* null terminate */
		largs = args = kmalloc(size + 1, GFP_KERNEL);
		if (!args)
			return -ENOMEM;
		memcpy(args, value, size);
		args[size] = '\0';
	}

	error = -EINVAL;
	args = strim(args);
	command = strsep(&args, " ");
	if (!args)
		goto out;
	args = skip_spaces(args);
	if (!*args)
		goto out;

	arg_size = size - (args - (largs ? largs : (char *) value));
	if (attr == LSM_ATTR_CURRENT) {
		if (strcmp(command, "changehat") == 0) {
			error = aa_setprocattr_changehat(args, arg_size,
							 AA_CHANGE_NOFLAGS);
		} else if (strcmp(command, "permhat") == 0) {
			error = aa_setprocattr_changehat(args, arg_size,
							 AA_CHANGE_TEST);
		} else if (strcmp(command, "changeprofile") == 0) {
			error = aa_change_profile(args, AA_CHANGE_NOFLAGS);
		} else if (strcmp(command, "permprofile") == 0) {
			error = aa_change_profile(args, AA_CHANGE_TEST);
		} else if (strcmp(command, "stack") == 0) {
			error = aa_change_profile(args, AA_CHANGE_STACK);
		} else
			goto fail;
	} else if (attr == LSM_ATTR_EXEC) {
		if (strcmp(command, "exec") == 0)
			error = aa_change_profile(args, AA_CHANGE_ONEXEC);
		else if (strcmp(command, "stack") == 0)
			error = aa_change_profile(args, (AA_CHANGE_ONEXEC |
							 AA_CHANGE_STACK));
		else
			goto fail;
	} else
		/* only support the "current" and "exec" process attributes */
		goto fail;

	if (!error)
		error = size;
out:
	kfree(largs);
	return error;

fail:
	ad.subj_label = begin_current_label_crit_section();
	if (attr == LSM_ATTR_CURRENT)
		ad.info = "current";
	else if (attr == LSM_ATTR_EXEC)
		ad.info = "exec";
	else
		ad.info = "invalid";
	ad.error = error = -EINVAL;
	aa_audit_msg(AUDIT_APPARMOR_DENIED, &ad, NULL);
	end_current_label_crit_section(ad.subj_label);
	goto out;
}

static int apparmor_setselfattr(unsigned int attr, struct lsm_ctx *ctx,
				u32 size, u32 flags)
{
	int rc;

	if (attr != LSM_ATTR_CURRENT && attr != LSM_ATTR_EXEC)
		return -EOPNOTSUPP;

	rc = do_setattr(attr, ctx->ctx, ctx->ctx_len);
	if (rc > 0)
		return 0;
	return rc;
}

static int apparmor_setprocattr(const char *name, void *value,
				size_t size)
{
	int attr = lsm_name_to_attr(name);

	if (attr)
		return do_setattr(attr, value, size);
	return -EINVAL;
}

/**
 * apparmor_bprm_committing_creds - do task cleanup on committing new creds
 * @bprm: binprm for the exec  (NOT NULL)
 */
static void apparmor_bprm_committing_creds(const struct linux_binprm *bprm)
{
	struct aa_label *label = aa_current_raw_label();
	struct aa_label *new_label = cred_label(bprm->cred);

	/* bail out if unconfined or not changing profile */
	if ((new_label->proxy == label->proxy) ||
	    (unconfined(new_label)))
		return;

	aa_inherit_files(bprm->cred, current->files);

	current->pdeath_signal = 0;

	/* reset soft limits and set hard limits for the new label */
	__aa_transition_rlimits(label, new_label);
}

/**
 * apparmor_bprm_committed_creds() - do cleanup after new creds committed
 * @bprm: binprm for the exec  (NOT NULL)
 */
static void apparmor_bprm_committed_creds(const struct linux_binprm *bprm)
{
	/* clear out temporary/transitional state from the context */
	aa_clear_task_ctx_trans(task_ctx(current));

	return;
}

static void apparmor_current_getlsmprop_subj(struct lsm_prop *prop)
{
	struct aa_label *label = __begin_current_label_crit_section();

	prop->apparmor.label = label;
	__end_current_label_crit_section(label);
}

static void apparmor_task_getlsmprop_obj(struct task_struct *p,
					  struct lsm_prop *prop)
{
	struct aa_label *label = aa_get_task_label(p);

	prop->apparmor.label = label;
	aa_put_label(label);
}

static int apparmor_task_setrlimit(struct task_struct *task,
		unsigned int resource, struct rlimit *new_rlim)
{
	struct aa_label *label = __begin_current_label_crit_section();
	int error = 0;

	if (!unconfined(label))
		error = aa_task_setrlimit(current_cred(), label, task,
					  resource, new_rlim);
	__end_current_label_crit_section(label);

	return error;
}

static int apparmor_task_kill(struct task_struct *target, struct kernel_siginfo *info,
			      int sig, const struct cred *cred)
{
	const struct cred *tc;
	struct aa_label *cl, *tl;
	int error;

	tc = get_task_cred(target);
	tl = aa_get_newest_cred_label(tc);
	if (cred) {
		/*
		 * Dealing with USB IO specific behavior
		 */
		cl = aa_get_newest_cred_label(cred);
		error = aa_may_signal(cred, cl, tc, tl, sig);
		aa_put_label(cl);
	} else {
		cl = __begin_current_label_crit_section();
		error = aa_may_signal(current_cred(), cl, tc, tl, sig);
		__end_current_label_crit_section(cl);
	}
	aa_put_label(tl);
	put_cred(tc);

	return error;
}

static int apparmor_userns_create(const struct cred *cred)
{
	struct aa_label *label;
	struct aa_profile *profile;
	int error = 0;
	DEFINE_AUDIT_DATA(ad, LSM_AUDIT_DATA_TASK, AA_CLASS_NS,
			  OP_USERNS_CREATE);

	ad.subj_cred = current_cred();

	label = begin_current_label_crit_section();
	if (!unconfined(label)) {
		error = fn_for_each(label, profile,
				    aa_profile_ns_perm(profile, &ad,
						       AA_USERNS_CREATE));
	}
	end_current_label_crit_section(label);

	return error;
}

static void apparmor_sk_free_security(struct sock *sk)
{
	struct aa_sk_ctx *ctx = aa_sock(sk);

	aa_put_label(ctx->label);
	aa_put_label(ctx->peer);
}

/**
 * apparmor_sk_clone_security - clone the sk_security field
 * @sk: sock to have security cloned
 * @newsk: sock getting clone
 */
static void apparmor_sk_clone_security(const struct sock *sk,
				       struct sock *newsk)
{
	struct aa_sk_ctx *ctx = aa_sock(sk);
	struct aa_sk_ctx *new = aa_sock(newsk);

	if (new->label)
		aa_put_label(new->label);
	new->label = aa_get_label(ctx->label);

	if (new->peer)
		aa_put_label(new->peer);
	new->peer = aa_get_label(ctx->peer);
}

static int apparmor_socket_create(int family, int type, int protocol, int kern)
{
	struct aa_label *label;
	int error = 0;

	AA_BUG(in_interrupt());

	label = begin_current_label_crit_section();
	if (!(kern || unconfined(label)))
		error = af_select(family,
				  create_perm(label, family, type, protocol),
				  aa_af_perm(current_cred(), label,
					     OP_CREATE, AA_MAY_CREATE,
					     family, type, protocol));
	end_current_label_crit_section(label);

	return error;
}

/**
 * apparmor_socket_post_create - setup the per-socket security struct
 * @sock: socket that is being setup
 * @family: family of socket being created
 * @type: type of the socket
 * @protocol: protocol of the socket
 * @kern: socket is a special kernel socket
 *
 * Note:
 * -   kernel sockets labeled kernel_t used to use unconfined
 * -   socket may not have sk here if created with sock_create_lite or
 *     sock_alloc. These should be accept cases which will be handled in
 *     sock_graft.
 */
static int apparmor_socket_post_create(struct socket *sock, int family,
				       int type, int protocol, int kern)
{
	struct aa_label *label;

	if (kern) {
		label = aa_get_label(kernel_t);
	} else
		label = aa_get_current_label();

	if (sock->sk) {
		struct aa_sk_ctx *ctx = aa_sock(sock->sk);

		aa_put_label(ctx->label);
		ctx->label = aa_get_label(label);
	}
	aa_put_label(label);

	return 0;
}

static int apparmor_socket_bind(struct socket *sock,
				struct sockaddr *address, int addrlen)
{
	AA_BUG(!sock);
	AA_BUG(!sock->sk);
	AA_BUG(!address);
	AA_BUG(in_interrupt());

	return af_select(sock->sk->sk_family,
			 bind_perm(sock, address, addrlen),
			 aa_sk_perm(OP_BIND, AA_MAY_BIND, sock->sk));
}

static int apparmor_socket_connect(struct socket *sock,
				   struct sockaddr *address, int addrlen)
{
	AA_BUG(!sock);
	AA_BUG(!sock->sk);
	AA_BUG(!address);
	AA_BUG(in_interrupt());

	return af_select(sock->sk->sk_family,
			 connect_perm(sock, address, addrlen),
			 aa_sk_perm(OP_CONNECT, AA_MAY_CONNECT, sock->sk));
}

static int apparmor_socket_listen(struct socket *sock, int backlog)
{
	AA_BUG(!sock);
	AA_BUG(!sock->sk);
	AA_BUG(in_interrupt());

	return af_select(sock->sk->sk_family,
			 listen_perm(sock, backlog),
			 aa_sk_perm(OP_LISTEN, AA_MAY_LISTEN, sock->sk));
}

/*
 * Note: while @newsock is created and has some information, the accept
 *       has not been done.
 */
static int apparmor_socket_accept(struct socket *sock, struct socket *newsock)
{
	AA_BUG(!sock);
	AA_BUG(!sock->sk);
	AA_BUG(!newsock);
	AA_BUG(in_interrupt());

	return af_select(sock->sk->sk_family,
			 accept_perm(sock, newsock),
			 aa_sk_perm(OP_ACCEPT, AA_MAY_ACCEPT, sock->sk));
}

static int aa_sock_msg_perm(const char *op, u32 request, struct socket *sock,
			    struct msghdr *msg, int size)
{
	AA_BUG(!sock);
	AA_BUG(!sock->sk);
	AA_BUG(!msg);
	AA_BUG(in_interrupt());

	return af_select(sock->sk->sk_family,
			 msg_perm(op, request, sock, msg, size),
			 aa_sk_perm(op, request, sock->sk));
}

static int apparmor_socket_sendmsg(struct socket *sock,
				   struct msghdr *msg, int size)
{
	return aa_sock_msg_perm(OP_SENDMSG, AA_MAY_SEND, sock, msg, size);
}

static int apparmor_socket_recvmsg(struct socket *sock,
				   struct msghdr *msg, int size, int flags)
{
	return aa_sock_msg_perm(OP_RECVMSG, AA_MAY_RECEIVE, sock, msg, size);
}

/* revaliation, get/set attr, shutdown */
static int aa_sock_perm(const char *op, u32 request, struct socket *sock)
{
	AA_BUG(!sock);
	AA_BUG(!sock->sk);
	AA_BUG(in_interrupt());

	return af_select(sock->sk->sk_family,
			 sock_perm(op, request, sock),
			 aa_sk_perm(op, request, sock->sk));
}

static int apparmor_socket_getsockname(struct socket *sock)
{
	return aa_sock_perm(OP_GETSOCKNAME, AA_MAY_GETATTR, sock);
}

static int apparmor_socket_getpeername(struct socket *sock)
{
	return aa_sock_perm(OP_GETPEERNAME, AA_MAY_GETATTR, sock);
}

/* revaliation, get/set attr, opt */
static int aa_sock_opt_perm(const char *op, u32 request, struct socket *sock,
			    int level, int optname)
{
	AA_BUG(!sock);
	AA_BUG(!sock->sk);
	AA_BUG(in_interrupt());

	return af_select(sock->sk->sk_family,
			 opt_perm(op, request, sock, level, optname),
			 aa_sk_perm(op, request, sock->sk));
}

static int apparmor_socket_getsockopt(struct socket *sock, int level,
				      int optname)
{
	return aa_sock_opt_perm(OP_GETSOCKOPT, AA_MAY_GETOPT, sock,
				level, optname);
}

static int apparmor_socket_setsockopt(struct socket *sock, int level,
				      int optname)
{
	return aa_sock_opt_perm(OP_SETSOCKOPT, AA_MAY_SETOPT, sock,
				level, optname);
}

static int apparmor_socket_shutdown(struct socket *sock, int how)
{
	return aa_sock_perm(OP_SHUTDOWN, AA_MAY_SHUTDOWN, sock);
}

#ifdef CONFIG_NETWORK_SECMARK
/**
 * apparmor_socket_sock_rcv_skb - check perms before associating skb to sk
 * @sk: sk to associate @skb with
 * @skb: skb to check for perms
 *
 * Note: can not sleep may be called with locks held
 *
 * dont want protocol specific in __skb_recv_datagram()
 * to deny an incoming connection  socket_sock_rcv_skb()
 */
static int apparmor_socket_sock_rcv_skb(struct sock *sk, struct sk_buff *skb)
{
	struct aa_sk_ctx *ctx = aa_sock(sk);

	if (!skb->secmark)
		return 0;

	/*
	 * If reach here before socket_post_create hook is called, in which
	 * case label is null, drop the packet.
	 */
	if (!ctx->label)
		return -EACCES;

	return apparmor_secmark_check(ctx->label, OP_RECVMSG, AA_MAY_RECEIVE,
				      skb->secmark, sk);
}
#endif


static struct aa_label *sk_peer_label(struct sock *sk)
{
	struct aa_sk_ctx *ctx = aa_sock(sk);

	if (ctx->peer)
		return ctx->peer;

	return ERR_PTR(-ENOPROTOOPT);
}

/**
 * apparmor_socket_getpeersec_stream - get security context of peer
 * @sock: socket that we are trying to get the peer context of
 * @optval: output - buffer to copy peer name to
 * @optlen: output - size of copied name in @optval
 * @len: size of @optval buffer
 * Returns: 0 on success, -errno of failure
 *
 * Note: for tcp only valid if using ipsec or cipso on lan
 */
static int apparmor_socket_getpeersec_stream(struct socket *sock,
					     sockptr_t optval, sockptr_t optlen,
					     unsigned int len)
{
	char *name = NULL;
	int slen, error = 0;
	struct aa_label *label;
	struct aa_label *peer;

	label = begin_current_label_crit_section();
	peer = sk_peer_label(sock->sk);
	if (IS_ERR(peer)) {
		error = PTR_ERR(peer);
		goto done;
	}
	slen = aa_label_asxprint(&name, labels_ns(label), peer,
				 FLAG_SHOW_MODE | FLAG_VIEW_SUBNS |
				 FLAG_HIDDEN_UNCONFINED, GFP_KERNEL);
	/* don't include terminating \0 in slen, it breaks some apps */
	if (slen < 0) {
		error = -ENOMEM;
		goto done;
	}
	if (slen > len) {
		error = -ERANGE;
		goto done_len;
	}

	if (copy_to_sockptr(optval, name, slen))
		error = -EFAULT;
done_len:
	if (copy_to_sockptr(optlen, &slen, sizeof(slen)))
		error = -EFAULT;
done:
	end_current_label_crit_section(label);
	kfree(name);
	return error;
}

/**
 * apparmor_socket_getpeersec_dgram - get security label of packet
 * @sock: the peer socket
 * @skb: packet data
 * @secid: pointer to where to put the secid of the packet
 *
 * Sets the netlabel socket state on sk from parent
 */
static int apparmor_socket_getpeersec_dgram(struct socket *sock,
					    struct sk_buff *skb, u32 *secid)

{
	/* TODO: requires secid support */
	return -ENOPROTOOPT;
}

/**
 * apparmor_sock_graft - Initialize newly created socket
 * @sk: child sock
 * @parent: parent socket
 *
 * Note: could set off of SOCK_CTX(parent) but need to track inode and we can
 *       just set sk security information off of current creating process label
 *       Labeling of sk for accept case - probably should be sock based
 *       instead of task, because of the case where an implicitly labeled
 *       socket is shared by different tasks.
 */
static void apparmor_sock_graft(struct sock *sk, struct socket *parent)
{
	struct aa_sk_ctx *ctx = aa_sock(sk);

	if (!ctx->label)
		ctx->label = aa_get_current_label();
}

#ifdef CONFIG_NETWORK_SECMARK
static int apparmor_inet_conn_request(const struct sock *sk, struct sk_buff *skb,
				      struct request_sock *req)
{
	struct aa_sk_ctx *ctx = aa_sock(sk);

	if (!skb->secmark)
		return 0;

	return apparmor_secmark_check(ctx->label, OP_CONNECT, AA_MAY_CONNECT,
				      skb->secmark, sk);
}
#endif

/*
 * The cred blob is a pointer to, not an instance of, an aa_label.
 */
struct lsm_blob_sizes apparmor_blob_sizes __ro_after_init = {
	.lbs_cred = sizeof(struct aa_label *),
	.lbs_file = sizeof(struct aa_file_ctx),
	.lbs_task = sizeof(struct aa_task_ctx),
	.lbs_sock = sizeof(struct aa_sk_ctx),
};

static const struct lsm_id apparmor_lsmid = {
	.name = "apparmor",
	.id = LSM_ID_APPARMOR,
};

static struct security_hook_list apparmor_hooks[] __ro_after_init = {
	LSM_HOOK_INIT(ptrace_access_check, apparmor_ptrace_access_check),
	LSM_HOOK_INIT(ptrace_traceme, apparmor_ptrace_traceme),
	LSM_HOOK_INIT(capget, apparmor_capget),
	LSM_HOOK_INIT(capable, apparmor_capable),

	LSM_HOOK_INIT(move_mount, apparmor_move_mount),
	LSM_HOOK_INIT(sb_mount, apparmor_sb_mount),
	LSM_HOOK_INIT(sb_umount, apparmor_sb_umount),
	LSM_HOOK_INIT(sb_pivotroot, apparmor_sb_pivotroot),

	LSM_HOOK_INIT(path_link, apparmor_path_link),
	LSM_HOOK_INIT(path_unlink, apparmor_path_unlink),
	LSM_HOOK_INIT(path_symlink, apparmor_path_symlink),
	LSM_HOOK_INIT(path_mkdir, apparmor_path_mkdir),
	LSM_HOOK_INIT(path_rmdir, apparmor_path_rmdir),
	LSM_HOOK_INIT(path_mknod, apparmor_path_mknod),
	LSM_HOOK_INIT(path_rename, apparmor_path_rename),
	LSM_HOOK_INIT(path_chmod, apparmor_path_chmod),
	LSM_HOOK_INIT(path_chown, apparmor_path_chown),
	LSM_HOOK_INIT(path_truncate, apparmor_path_truncate),
	LSM_HOOK_INIT(inode_getattr, apparmor_inode_getattr),

	LSM_HOOK_INIT(file_open, apparmor_file_open),
	LSM_HOOK_INIT(file_receive, apparmor_file_receive),
	LSM_HOOK_INIT(file_permission, apparmor_file_permission),
	LSM_HOOK_INIT(file_alloc_security, apparmor_file_alloc_security),
	LSM_HOOK_INIT(file_free_security, apparmor_file_free_security),
	LSM_HOOK_INIT(mmap_file, apparmor_mmap_file),
	LSM_HOOK_INIT(file_mprotect, apparmor_file_mprotect),
	LSM_HOOK_INIT(file_lock, apparmor_file_lock),
	LSM_HOOK_INIT(file_truncate, apparmor_file_truncate),

	LSM_HOOK_INIT(getselfattr, apparmor_getselfattr),
	LSM_HOOK_INIT(setselfattr, apparmor_setselfattr),
	LSM_HOOK_INIT(getprocattr, apparmor_getprocattr),
	LSM_HOOK_INIT(setprocattr, apparmor_setprocattr),

	LSM_HOOK_INIT(sk_free_security, apparmor_sk_free_security),
	LSM_HOOK_INIT(sk_clone_security, apparmor_sk_clone_security),

	LSM_HOOK_INIT(socket_create, apparmor_socket_create),
	LSM_HOOK_INIT(socket_post_create, apparmor_socket_post_create),
	LSM_HOOK_INIT(socket_bind, apparmor_socket_bind),
	LSM_HOOK_INIT(socket_connect, apparmor_socket_connect),
	LSM_HOOK_INIT(socket_listen, apparmor_socket_listen),
	LSM_HOOK_INIT(socket_accept, apparmor_socket_accept),
	LSM_HOOK_INIT(socket_sendmsg, apparmor_socket_sendmsg),
	LSM_HOOK_INIT(socket_recvmsg, apparmor_socket_recvmsg),
	LSM_HOOK_INIT(socket_getsockname, apparmor_socket_getsockname),
	LSM_HOOK_INIT(socket_getpeername, apparmor_socket_getpeername),
	LSM_HOOK_INIT(socket_getsockopt, apparmor_socket_getsockopt),
	LSM_HOOK_INIT(socket_setsockopt, apparmor_socket_setsockopt),
	LSM_HOOK_INIT(socket_shutdown, apparmor_socket_shutdown),
#ifdef CONFIG_NETWORK_SECMARK
	LSM_HOOK_INIT(socket_sock_rcv_skb, apparmor_socket_sock_rcv_skb),
#endif
	LSM_HOOK_INIT(socket_getpeersec_stream,
		      apparmor_socket_getpeersec_stream),
	LSM_HOOK_INIT(socket_getpeersec_dgram,
		      apparmor_socket_getpeersec_dgram),
	LSM_HOOK_INIT(sock_graft, apparmor_sock_graft),
#ifdef CONFIG_NETWORK_SECMARK
	LSM_HOOK_INIT(inet_conn_request, apparmor_inet_conn_request),
#endif

	LSM_HOOK_INIT(cred_alloc_blank, apparmor_cred_alloc_blank),
	LSM_HOOK_INIT(cred_free, apparmor_cred_free),
	LSM_HOOK_INIT(cred_prepare, apparmor_cred_prepare),
	LSM_HOOK_INIT(cred_transfer, apparmor_cred_transfer),

	LSM_HOOK_INIT(bprm_creds_for_exec, apparmor_bprm_creds_for_exec),
	LSM_HOOK_INIT(bprm_committing_creds, apparmor_bprm_committing_creds),
	LSM_HOOK_INIT(bprm_committed_creds, apparmor_bprm_committed_creds),

	LSM_HOOK_INIT(task_free, apparmor_task_free),
	LSM_HOOK_INIT(task_alloc, apparmor_task_alloc),
	LSM_HOOK_INIT(current_getlsmprop_subj,
		      apparmor_current_getlsmprop_subj),
	LSM_HOOK_INIT(task_getlsmprop_obj, apparmor_task_getlsmprop_obj),
	LSM_HOOK_INIT(task_setrlimit, apparmor_task_setrlimit),
	LSM_HOOK_INIT(task_kill, apparmor_task_kill),
	LSM_HOOK_INIT(userns_create, apparmor_userns_create),

#ifdef CONFIG_AUDIT
	LSM_HOOK_INIT(audit_rule_init, aa_audit_rule_init),
	LSM_HOOK_INIT(audit_rule_known, aa_audit_rule_known),
	LSM_HOOK_INIT(audit_rule_match, aa_audit_rule_match),
	LSM_HOOK_INIT(audit_rule_free, aa_audit_rule_free),
#endif

	LSM_HOOK_INIT(secid_to_secctx, apparmor_secid_to_secctx),
	LSM_HOOK_INIT(lsmprop_to_secctx, apparmor_lsmprop_to_secctx),
	LSM_HOOK_INIT(secctx_to_secid, apparmor_secctx_to_secid),
	LSM_HOOK_INIT(release_secctx, apparmor_release_secctx),

#ifdef CONFIG_IO_URING
	LSM_HOOK_INIT(uring_override_creds, apparmor_uring_override_creds),
	LSM_HOOK_INIT(uring_sqpoll, apparmor_uring_sqpoll),
#endif
};

/*
 * AppArmor sysfs module parameters
 */

static int param_set_aabool(const char *val, const struct kernel_param *kp);
static int param_get_aabool(char *buffer, const struct kernel_param *kp);
#define param_check_aabool param_check_bool
static const struct kernel_param_ops param_ops_aabool = {
	.flags = KERNEL_PARAM_OPS_FL_NOARG,
	.set = param_set_aabool,
	.get = param_get_aabool
};

static int param_set_aauint(const char *val, const struct kernel_param *kp);
static int param_get_aauint(char *buffer, const struct kernel_param *kp);
#define param_check_aauint param_check_uint
static const struct kernel_param_ops param_ops_aauint = {
	.set = param_set_aauint,
	.get = param_get_aauint
};

static int param_set_aacompressionlevel(const char *val,
					const struct kernel_param *kp);
static int param_get_aacompressionlevel(char *buffer,
					const struct kernel_param *kp);
#define param_check_aacompressionlevel param_check_int
static const struct kernel_param_ops param_ops_aacompressionlevel = {
	.set = param_set_aacompressionlevel,
	.get = param_get_aacompressionlevel
};

static int param_set_aalockpolicy(const char *val, const struct kernel_param *kp);
static int param_get_aalockpolicy(char *buffer, const struct kernel_param *kp);
#define param_check_aalockpolicy param_check_bool
static const struct kernel_param_ops param_ops_aalockpolicy = {
	.flags = KERNEL_PARAM_OPS_FL_NOARG,
	.set = param_set_aalockpolicy,
	.get = param_get_aalockpolicy
};

static int param_set_audit(const char *val, const struct kernel_param *kp);
static int param_get_audit(char *buffer, const struct kernel_param *kp);

static int param_set_mode(const char *val, const struct kernel_param *kp);
static int param_get_mode(char *buffer, const struct kernel_param *kp);

/* Flag values, also controllable via /sys/module/apparmor/parameters
 * We define special types as we want to do additional mediation.
 */

/* AppArmor global enforcement switch - complain, enforce, kill */
enum profile_mode aa_g_profile_mode = APPARMOR_ENFORCE;
module_param_call(mode, param_set_mode, param_get_mode,
		  &aa_g_profile_mode, S_IRUSR | S_IWUSR);

/* whether policy verification hashing is enabled */
bool aa_g_hash_policy = IS_ENABLED(CONFIG_SECURITY_APPARMOR_HASH_DEFAULT);
#ifdef CONFIG_SECURITY_APPARMOR_HASH
module_param_named(hash_policy, aa_g_hash_policy, aabool, S_IRUSR | S_IWUSR);
#endif

/* whether policy exactly as loaded is retained for debug and checkpointing */
bool aa_g_export_binary = IS_ENABLED(CONFIG_SECURITY_APPARMOR_EXPORT_BINARY);
#ifdef CONFIG_SECURITY_APPARMOR_EXPORT_BINARY
module_param_named(export_binary, aa_g_export_binary, aabool, 0600);
#endif

/* policy loaddata compression level */
int aa_g_rawdata_compression_level = AA_DEFAULT_CLEVEL;
module_param_named(rawdata_compression_level, aa_g_rawdata_compression_level,
		   aacompressionlevel, 0400);

/* Debug mode */
bool aa_g_debug = IS_ENABLED(CONFIG_SECURITY_APPARMOR_DEBUG_MESSAGES);
module_param_named(debug, aa_g_debug, aabool, S_IRUSR | S_IWUSR);

/* Audit mode */
enum audit_mode aa_g_audit;
module_param_call(audit, param_set_audit, param_get_audit,
		  &aa_g_audit, S_IRUSR | S_IWUSR);

/* Determines if audit header is included in audited messages.  This
 * provides more context if the audit daemon is not running
 */
bool aa_g_audit_header = true;
module_param_named(audit_header, aa_g_audit_header, aabool,
		   S_IRUSR | S_IWUSR);

/* lock out loading/removal of policy
 * TODO: add in at boot loading of policy, which is the only way to
 *       load policy, if lock_policy is set
 */
bool aa_g_lock_policy;
module_param_named(lock_policy, aa_g_lock_policy, aalockpolicy,
		   S_IRUSR | S_IWUSR);

/* Syscall logging mode */
bool aa_g_logsyscall;
module_param_named(logsyscall, aa_g_logsyscall, aabool, S_IRUSR | S_IWUSR);

/* Maximum pathname length before accesses will start getting rejected */
unsigned int aa_g_path_max = 2 * PATH_MAX;
module_param_named(path_max, aa_g_path_max, aauint, S_IRUSR);

/* Determines how paranoid loading of policy is and how much verification
 * on the loaded policy is done.
 * DEPRECATED: read only as strict checking of load is always done now
 * that none root users (user namespaces) can load policy.
 */
bool aa_g_paranoid_load = IS_ENABLED(CONFIG_SECURITY_APPARMOR_PARANOID_LOAD);
module_param_named(paranoid_load, aa_g_paranoid_load, aabool, S_IRUGO);

static int param_get_aaintbool(char *buffer, const struct kernel_param *kp);
static int param_set_aaintbool(const char *val, const struct kernel_param *kp);
#define param_check_aaintbool param_check_int
static const struct kernel_param_ops param_ops_aaintbool = {
	.set = param_set_aaintbool,
	.get = param_get_aaintbool
};
/* Boot time disable flag */
static int apparmor_enabled __ro_after_init = 1;
module_param_named(enabled, apparmor_enabled, aaintbool, 0444);

static int __init apparmor_enabled_setup(char *str)
{
	unsigned long enabled;
	int error = kstrtoul(str, 0, &enabled);
	if (!error)
		apparmor_enabled = enabled ? 1 : 0;
	return 1;
}

__setup("apparmor=", apparmor_enabled_setup);

/* set global flag turning off the ability to load policy */
static int param_set_aalockpolicy(const char *val, const struct kernel_param *kp)
{
	if (!apparmor_enabled)
		return -EINVAL;
	if (apparmor_initialized && !aa_current_policy_admin_capable(NULL))
		return -EPERM;
	return param_set_bool(val, kp);
}

static int param_get_aalockpolicy(char *buffer, const struct kernel_param *kp)
{
	if (!apparmor_enabled)
		return -EINVAL;
	if (apparmor_initialized && !aa_current_policy_view_capable(NULL))
		return -EPERM;
	return param_get_bool(buffer, kp);
}

static int param_set_aabool(const char *val, const struct kernel_param *kp)
{
	if (!apparmor_enabled)
		return -EINVAL;
	if (apparmor_initialized && !aa_current_policy_admin_capable(NULL))
		return -EPERM;
	return param_set_bool(val, kp);
}

static int param_get_aabool(char *buffer, const struct kernel_param *kp)
{
	if (!apparmor_enabled)
		return -EINVAL;
	if (apparmor_initialized && !aa_current_policy_view_capable(NULL))
		return -EPERM;
	return param_get_bool(buffer, kp);
}

static int param_set_aauint(const char *val, const struct kernel_param *kp)
{
	int error;

	if (!apparmor_enabled)
		return -EINVAL;
	/* file is ro but enforce 2nd line check */
	if (apparmor_initialized)
		return -EPERM;

	error = param_set_uint(val, kp);
	aa_g_path_max = max_t(uint32_t, aa_g_path_max, sizeof(union aa_buffer));
	pr_info("AppArmor: buffer size set to %d bytes\n", aa_g_path_max);

	return error;
}

static int param_get_aauint(char *buffer, const struct kernel_param *kp)
{
	if (!apparmor_enabled)
		return -EINVAL;
	if (apparmor_initialized && !aa_current_policy_view_capable(NULL))
		return -EPERM;
	return param_get_uint(buffer, kp);
}

/* Can only be set before AppArmor is initialized (i.e. on boot cmdline). */
static int param_set_aaintbool(const char *val, const struct kernel_param *kp)
{
	struct kernel_param kp_local;
	bool value;
	int error;

	if (apparmor_initialized)
		return -EPERM;

	/* Create local copy, with arg pointing to bool type. */
	value = !!*((int *)kp->arg);
	memcpy(&kp_local, kp, sizeof(kp_local));
	kp_local.arg = &value;

	error = param_set_bool(val, &kp_local);
	if (!error)
		*((int *)kp->arg) = *((bool *)kp_local.arg);
	return error;
}

/*
 * To avoid changing /sys/module/apparmor/parameters/enabled from Y/N to
 * 1/0, this converts the "int that is actually bool" back to bool for
 * display in the /sys filesystem, while keeping it "int" for the LSM
 * infrastructure.
 */
static int param_get_aaintbool(char *buffer, const struct kernel_param *kp)
{
	struct kernel_param kp_local;
	bool value;

	/* Create local copy, with arg pointing to bool type. */
	value = !!*((int *)kp->arg);
	memcpy(&kp_local, kp, sizeof(kp_local));
	kp_local.arg = &value;

	return param_get_bool(buffer, &kp_local);
}

static int param_set_aacompressionlevel(const char *val,
					const struct kernel_param *kp)
{
	int error;

	if (!apparmor_enabled)
		return -EINVAL;
	if (apparmor_initialized)
		return -EPERM;

	error = param_set_int(val, kp);

	aa_g_rawdata_compression_level = clamp(aa_g_rawdata_compression_level,
					       AA_MIN_CLEVEL, AA_MAX_CLEVEL);
	pr_info("AppArmor: policy rawdata compression level set to %d\n",
		aa_g_rawdata_compression_level);

	return error;
}

static int param_get_aacompressionlevel(char *buffer,
					const struct kernel_param *kp)
{
	if (!apparmor_enabled)
		return -EINVAL;
	if (apparmor_initialized && !aa_current_policy_view_capable(NULL))
		return -EPERM;
	return param_get_int(buffer, kp);
}

static int param_get_audit(char *buffer, const struct kernel_param *kp)
{
	if (!apparmor_enabled)
		return -EINVAL;
	if (apparmor_initialized && !aa_current_policy_view_capable(NULL))
		return -EPERM;
	return sprintf(buffer, "%s", audit_mode_names[aa_g_audit]);
}

static int param_set_audit(const char *val, const struct kernel_param *kp)
{
	int i;

	if (!apparmor_enabled)
		return -EINVAL;
	if (!val)
		return -EINVAL;
	if (apparmor_initialized && !aa_current_policy_admin_capable(NULL))
		return -EPERM;

	i = match_string(audit_mode_names, AUDIT_MAX_INDEX, val);
	if (i < 0)
		return -EINVAL;

	aa_g_audit = i;
	return 0;
}

static int param_get_mode(char *buffer, const struct kernel_param *kp)
{
	if (!apparmor_enabled)
		return -EINVAL;
	if (apparmor_initialized && !aa_current_policy_view_capable(NULL))
		return -EPERM;

	return sprintf(buffer, "%s", aa_profile_mode_names[aa_g_profile_mode]);
}

static int param_set_mode(const char *val, const struct kernel_param *kp)
{
	int i;

	if (!apparmor_enabled)
		return -EINVAL;
	if (!val)
		return -EINVAL;
	if (apparmor_initialized && !aa_current_policy_admin_capable(NULL))
		return -EPERM;

	i = match_string(aa_profile_mode_names, APPARMOR_MODE_NAMES_MAX_INDEX,
			 val);
	if (i < 0)
		return -EINVAL;

	aa_g_profile_mode = i;
	return 0;
}

char *aa_get_buffer(bool in_atomic)
{
	union aa_buffer *aa_buf;
	struct aa_local_cache *cache;
	bool try_again = true;
	gfp_t flags = (GFP_KERNEL | __GFP_RETRY_MAYFAIL | __GFP_NOWARN);

	/* use per cpu cached buffers first */
	cache = get_cpu_ptr(&aa_local_buffers);
	if (!list_empty(&cache->head)) {
		aa_buf = list_first_entry(&cache->head, union aa_buffer, list);
		list_del(&aa_buf->list);
		cache->hold--;
		cache->count--;
		put_cpu_ptr(&aa_local_buffers);
		return &aa_buf->buffer[0];
	}
	put_cpu_ptr(&aa_local_buffers);

	if (!spin_trylock(&aa_buffers_lock)) {
		cache = get_cpu_ptr(&aa_local_buffers);
		cache->hold += 1;
		put_cpu_ptr(&aa_local_buffers);
		spin_lock(&aa_buffers_lock);
	} else {
		cache = get_cpu_ptr(&aa_local_buffers);
		put_cpu_ptr(&aa_local_buffers);
	}
retry:
	if (buffer_count > reserve_count ||
	    (in_atomic && !list_empty(&aa_global_buffers))) {
		aa_buf = list_first_entry(&aa_global_buffers, union aa_buffer,
					  list);
		list_del(&aa_buf->list);
		buffer_count--;
		spin_unlock(&aa_buffers_lock);
		return aa_buf->buffer;
	}
	if (in_atomic) {
		/*
		 * out of reserve buffers and in atomic context so increase
		 * how many buffers to keep in reserve
		 */
		reserve_count++;
		flags = GFP_ATOMIC;
	}
	spin_unlock(&aa_buffers_lock);

	if (!in_atomic)
		might_sleep();
	aa_buf = kmalloc(aa_g_path_max, flags);
	if (!aa_buf) {
		if (try_again) {
			try_again = false;
			spin_lock(&aa_buffers_lock);
			goto retry;
		}
		pr_warn_once("AppArmor: Failed to allocate a memory buffer.\n");
		return NULL;
	}
	return aa_buf->buffer;
}

void aa_put_buffer(char *buf)
{
	union aa_buffer *aa_buf;
	struct aa_local_cache *cache;

	if (!buf)
		return;
	aa_buf = container_of(buf, union aa_buffer, buffer[0]);

	cache = get_cpu_ptr(&aa_local_buffers);
	if (!cache->hold) {
		put_cpu_ptr(&aa_local_buffers);

		if (spin_trylock(&aa_buffers_lock)) {
			/* put back on global list */
			list_add(&aa_buf->list, &aa_global_buffers);
			buffer_count++;
			spin_unlock(&aa_buffers_lock);
			cache = get_cpu_ptr(&aa_local_buffers);
			put_cpu_ptr(&aa_local_buffers);
			return;
		}
		/* contention on global list, fallback to percpu */
		cache = get_cpu_ptr(&aa_local_buffers);
		cache->hold += 1;
	}

	/* cache in percpu list */
	list_add(&aa_buf->list, &cache->head);
	cache->count++;
	put_cpu_ptr(&aa_local_buffers);
}

/*
 * AppArmor init functions
 */

/**
 * set_init_ctx - set a task context and profile on the first task.
 *
 * TODO: allow setting an alternate profile than unconfined
 */
static int __init set_init_ctx(void)
{
	struct cred *cred = (__force struct cred *)current->real_cred;

	set_cred_label(cred, aa_get_label(ns_unconfined(root_ns)));

	return 0;
}

static void destroy_buffers(void)
{
	union aa_buffer *aa_buf;

	spin_lock(&aa_buffers_lock);
	while (!list_empty(&aa_global_buffers)) {
		aa_buf = list_first_entry(&aa_global_buffers, union aa_buffer,
					 list);
		list_del(&aa_buf->list);
		spin_unlock(&aa_buffers_lock);
		kfree(aa_buf);
		spin_lock(&aa_buffers_lock);
	}
	spin_unlock(&aa_buffers_lock);
}

static int __init alloc_buffers(void)
{
	union aa_buffer *aa_buf;
	int i, num;

	/*
	 * per cpu set of cached allocated buffers used to help reduce
	 * lock contention
	 */
	for_each_possible_cpu(i) {
		per_cpu(aa_local_buffers, i).hold = 0;
		per_cpu(aa_local_buffers, i).count = 0;
		INIT_LIST_HEAD(&per_cpu(aa_local_buffers, i).head);
	}
	/*
	 * A function may require two buffers at once. Usually the buffers are
	 * used for a short period of time and are shared. On UP kernel buffers
	 * two should be enough, with more CPUs it is possible that more
	 * buffers will be used simultaneously. The preallocated pool may grow.
	 * This preallocation has also the side-effect that AppArmor will be
	 * disabled early at boot if aa_g_path_max is extremly high.
	 */
	if (num_online_cpus() > 1)
		num = 4 + RESERVE_COUNT;
	else
		num = 2 + RESERVE_COUNT;

	for (i = 0; i < num; i++) {

		aa_buf = kmalloc(aa_g_path_max, GFP_KERNEL |
				 __GFP_RETRY_MAYFAIL | __GFP_NOWARN);
		if (!aa_buf) {
			destroy_buffers();
			return -ENOMEM;
		}
		aa_put_buffer(aa_buf->buffer);
	}
	return 0;
}

#ifdef CONFIG_SYSCTL
static int apparmor_dointvec(const struct ctl_table *table, int write,
			     void *buffer, size_t *lenp, loff_t *ppos)
{
	if (!aa_current_policy_admin_capable(NULL))
		return -EPERM;
	if (!apparmor_enabled)
		return -EINVAL;

	return proc_dointvec(table, write, buffer, lenp, ppos);
}

static const struct ctl_table apparmor_sysctl_table[] = {
#ifdef CONFIG_USER_NS
	{
		.procname       = "unprivileged_userns_apparmor_policy",
		.data           = &unprivileged_userns_apparmor_policy,
		.maxlen         = sizeof(int),
		.mode           = 0600,
		.proc_handler   = apparmor_dointvec,
	},
#endif /* CONFIG_USER_NS */
	{
		.procname       = "apparmor_display_secid_mode",
		.data           = &apparmor_display_secid_mode,
		.maxlen         = sizeof(int),
		.mode           = 0600,
		.proc_handler   = apparmor_dointvec,
	},
	{
		.procname       = "apparmor_restrict_unprivileged_unconfined",
		.data           = &aa_unprivileged_unconfined_restricted,
		.maxlen         = sizeof(int),
		.mode           = 0600,
		.proc_handler   = apparmor_dointvec,
	},
};

static int __init apparmor_init_sysctl(void)
{
	return register_sysctl("kernel", apparmor_sysctl_table) ? 0 : -ENOMEM;
}
#else
static inline int apparmor_init_sysctl(void)
{
	return 0;
}
#endif /* CONFIG_SYSCTL */

#if defined(CONFIG_NETFILTER) && defined(CONFIG_NETWORK_SECMARK)
static unsigned int apparmor_ip_postroute(void *priv,
					  struct sk_buff *skb,
					  const struct nf_hook_state *state)
{
	struct aa_sk_ctx *ctx;
	struct sock *sk;

	if (!skb->secmark)
		return NF_ACCEPT;

	sk = skb_to_full_sk(skb);
	if (sk == NULL)
		return NF_ACCEPT;

	ctx = aa_sock(sk);
	if (!apparmor_secmark_check(ctx->label, OP_SENDMSG, AA_MAY_SEND,
				    skb->secmark, sk))
		return NF_ACCEPT;

	return NF_DROP_ERR(-ECONNREFUSED);

}

static const struct nf_hook_ops apparmor_nf_ops[] = {
	{
		.hook =         apparmor_ip_postroute,
		.pf =           NFPROTO_IPV4,
		.hooknum =      NF_INET_POST_ROUTING,
		.priority =     NF_IP_PRI_SELINUX_FIRST,
	},
#if IS_ENABLED(CONFIG_IPV6)
	{
		.hook =         apparmor_ip_postroute,
		.pf =           NFPROTO_IPV6,
		.hooknum =      NF_INET_POST_ROUTING,
		.priority =     NF_IP6_PRI_SELINUX_FIRST,
	},
#endif
};

static int __net_init apparmor_nf_register(struct net *net)
{
	return nf_register_net_hooks(net, apparmor_nf_ops,
				    ARRAY_SIZE(apparmor_nf_ops));
}

static void __net_exit apparmor_nf_unregister(struct net *net)
{
	nf_unregister_net_hooks(net, apparmor_nf_ops,
				ARRAY_SIZE(apparmor_nf_ops));
}

static struct pernet_operations apparmor_net_ops = {
	.init = apparmor_nf_register,
	.exit = apparmor_nf_unregister,
};

static int __init apparmor_nf_ip_init(void)
{
	int err;

	if (!apparmor_enabled)
		return 0;

	err = register_pernet_subsys(&apparmor_net_ops);
	if (err)
		panic("Apparmor: register_pernet_subsys: error %d\n", err);

	return 0;
}
__initcall(apparmor_nf_ip_init);
#endif

static char nulldfa_src[] = {
	#include "nulldfa.in"
};
static struct aa_dfa *nulldfa;

static char stacksplitdfa_src[] = {
	#include "stacksplitdfa.in"
};
struct aa_dfa *stacksplitdfa;
struct aa_policydb *nullpdb;

static int __init aa_setup_dfa_engine(void)
{
	int error = -ENOMEM;

	nullpdb = aa_alloc_pdb(GFP_KERNEL);
	if (!nullpdb)
		return -ENOMEM;

	nulldfa = aa_dfa_unpack(nulldfa_src, sizeof(nulldfa_src),
			    TO_ACCEPT1_FLAG(YYTD_DATA32) |
			    TO_ACCEPT2_FLAG(YYTD_DATA32));
	if (IS_ERR(nulldfa)) {
		error = PTR_ERR(nulldfa);
		goto fail;
	}
	nullpdb->dfa = aa_get_dfa(nulldfa);
	nullpdb->perms = kcalloc(2, sizeof(struct aa_perms), GFP_KERNEL);
	if (!nullpdb->perms)
		goto fail;
	nullpdb->size = 2;

	stacksplitdfa = aa_dfa_unpack(stacksplitdfa_src,
				      sizeof(stacksplitdfa_src),
				      TO_ACCEPT1_FLAG(YYTD_DATA32) |
				      TO_ACCEPT2_FLAG(YYTD_DATA32));
	if (IS_ERR(stacksplitdfa)) {
		error = PTR_ERR(stacksplitdfa);
		goto fail;
	}

	return 0;

fail:
	aa_put_pdb(nullpdb);
	aa_put_dfa(nulldfa);
	nullpdb = NULL;
	nulldfa = NULL;
	stacksplitdfa = NULL;

	return error;
}

static void __init aa_teardown_dfa_engine(void)
{
	aa_put_dfa(stacksplitdfa);
	aa_put_dfa(nulldfa);
	aa_put_pdb(nullpdb);
	nullpdb = NULL;
	stacksplitdfa = NULL;
	nulldfa = NULL;
}

static int __init apparmor_init(void)
{
	int error;

	error = aa_setup_dfa_engine();
	if (error) {
		AA_ERROR("Unable to setup dfa engine\n");
		goto alloc_out;
	}

	error = aa_alloc_root_ns();
	if (error) {
		AA_ERROR("Unable to allocate default profile namespace\n");
		goto alloc_out;
	}

	error = apparmor_init_sysctl();
	if (error) {
		AA_ERROR("Unable to register sysctls\n");
		goto alloc_out;

	}

	error = alloc_buffers();
	if (error) {
		AA_ERROR("Unable to allocate work buffers\n");
		goto alloc_out;
	}

	error = set_init_ctx();
	if (error) {
		AA_ERROR("Failed to set context on init task\n");
		aa_free_root_ns();
		goto buffers_out;
	}
	security_add_hooks(apparmor_hooks, ARRAY_SIZE(apparmor_hooks),
				&apparmor_lsmid);

	/* Report that AppArmor successfully initialized */
	apparmor_initialized = 1;
	if (aa_g_profile_mode == APPARMOR_COMPLAIN)
		aa_info_message("AppArmor initialized: complain mode enabled");
	else if (aa_g_profile_mode == APPARMOR_KILL)
		aa_info_message("AppArmor initialized: kill mode enabled");
	else
		aa_info_message("AppArmor initialized");

	return error;

buffers_out:
	destroy_buffers();
alloc_out:
	aa_destroy_aafs();
	aa_teardown_dfa_engine();

	apparmor_enabled = false;
	return error;
}

DEFINE_LSM(apparmor) = {
	.name = "apparmor",
	.flags = LSM_FLAG_LEGACY_MAJOR | LSM_FLAG_EXCLUSIVE,
	.enabled = &apparmor_enabled,
	.blobs = &apparmor_blob_sizes,
	.init = apparmor_init,
};
