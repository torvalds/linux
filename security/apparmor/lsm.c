/*
 * AppArmor security module
 *
 * This file contains AppArmor LSM hooks.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#include <linux/security.h>
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
#include <net/sock.h>

#include "include/apparmor.h"
#include "include/apparmorfs.h"
#include "include/audit.h"
#include "include/capability.h"
#include "include/context.h"
#include "include/file.h"
#include "include/ipc.h"
#include "include/path.h"
#include "include/policy.h"
#include "include/procattr.h"

/* Flag indicating whether initialization completed */
int apparmor_initialized __initdata;

/*
 * LSM hook functions
 */

/*
 * free the associated aa_task_cxt and put its profiles
 */
static void apparmor_cred_free(struct cred *cred)
{
	aa_free_task_context(cred->security);
	cred->security = NULL;
}

/*
 * allocate the apparmor part of blank credentials
 */
static int apparmor_cred_alloc_blank(struct cred *cred, gfp_t gfp)
{
	/* freed by apparmor_cred_free */
	struct aa_task_cxt *cxt = aa_alloc_task_context(gfp);
	if (!cxt)
		return -ENOMEM;

	cred->security = cxt;
	return 0;
}

/*
 * prepare new aa_task_cxt for modification by prepare_cred block
 */
static int apparmor_cred_prepare(struct cred *new, const struct cred *old,
				 gfp_t gfp)
{
	/* freed by apparmor_cred_free */
	struct aa_task_cxt *cxt = aa_alloc_task_context(gfp);
	if (!cxt)
		return -ENOMEM;

	aa_dup_task_context(cxt, old->security);
	new->security = cxt;
	return 0;
}

/*
 * transfer the apparmor data to a blank set of creds
 */
static void apparmor_cred_transfer(struct cred *new, const struct cred *old)
{
	const struct aa_task_cxt *old_cxt = old->security;
	struct aa_task_cxt *new_cxt = new->security;

	aa_dup_task_context(new_cxt, old_cxt);
}

static int apparmor_ptrace_access_check(struct task_struct *child,
					unsigned int mode)
{
	int error = cap_ptrace_access_check(child, mode);
	if (error)
		return error;

	return aa_ptrace(current, child, mode);
}

static int apparmor_ptrace_traceme(struct task_struct *parent)
{
	int error = cap_ptrace_traceme(parent);
	if (error)
		return error;

	return aa_ptrace(parent, current, PTRACE_MODE_ATTACH);
}

/* Derived from security/commoncap.c:cap_capget */
static int apparmor_capget(struct task_struct *target, kernel_cap_t *effective,
			   kernel_cap_t *inheritable, kernel_cap_t *permitted)
{
	struct aa_profile *profile;
	const struct cred *cred;

	rcu_read_lock();
	cred = __task_cred(target);
	profile = aa_cred_profile(cred);

	*effective = cred->cap_effective;
	*inheritable = cred->cap_inheritable;
	*permitted = cred->cap_permitted;

	if (!unconfined(profile) && !COMPLAIN_MODE(profile)) {
		*effective = cap_intersect(*effective, profile->caps.allow);
		*permitted = cap_intersect(*permitted, profile->caps.allow);
	}
	rcu_read_unlock();

	return 0;
}

static int apparmor_capable(const struct cred *cred, struct user_namespace *ns,
			    int cap, int audit)
{
	struct aa_profile *profile;
	/* cap_capable returns 0 on success, else -EPERM */
	int error = cap_capable(cred, ns, cap, audit);
	if (!error) {
		profile = aa_cred_profile(cred);
		if (!unconfined(profile))
			error = aa_capable(current, profile, cap, audit);
	}
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
static int common_perm(int op, struct path *path, u32 mask,
		       struct path_cond *cond)
{
	struct aa_profile *profile;
	int error = 0;

	profile = __aa_current_profile();
	if (!unconfined(profile))
		error = aa_path_perm(op, profile, path, 0, mask, cond);

	return error;
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
static int common_perm_dir_dentry(int op, struct path *dir,
				  struct dentry *dentry, u32 mask,
				  struct path_cond *cond)
{
	struct path path = { dir->mnt, dentry };

	return common_perm(op, &path, mask, cond);
}

/**
 * common_perm_mnt_dentry - common permission wrapper when mnt, dentry
 * @op: operation being checked
 * @mnt: mount point of dentry (NOT NULL)
 * @dentry: dentry to check  (NOT NULL)
 * @mask: requested permissions mask
 *
 * Returns: %0 else error code if error or permission denied
 */
static int common_perm_mnt_dentry(int op, struct vfsmount *mnt,
				  struct dentry *dentry, u32 mask)
{
	struct path path = { mnt, dentry };
	struct path_cond cond = { dentry->d_inode->i_uid,
				  dentry->d_inode->i_mode
	};

	return common_perm(op, &path, mask, &cond);
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
static int common_perm_rm(int op, struct path *dir,
			  struct dentry *dentry, u32 mask)
{
	struct inode *inode = dentry->d_inode;
	struct path_cond cond = { };

	if (!inode || !dir->mnt || !mediated_filesystem(inode))
		return 0;

	cond.uid = inode->i_uid;
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
static int common_perm_create(int op, struct path *dir, struct dentry *dentry,
			      u32 mask, umode_t mode)
{
	struct path_cond cond = { current_fsuid(), mode };

	if (!dir->mnt || !mediated_filesystem(dir->dentry->d_inode))
		return 0;

	return common_perm_dir_dentry(op, dir, dentry, mask, &cond);
}

static int apparmor_path_unlink(struct path *dir, struct dentry *dentry)
{
	return common_perm_rm(OP_UNLINK, dir, dentry, AA_MAY_DELETE);
}

static int apparmor_path_mkdir(struct path *dir, struct dentry *dentry,
			       umode_t mode)
{
	return common_perm_create(OP_MKDIR, dir, dentry, AA_MAY_CREATE,
				  S_IFDIR);
}

static int apparmor_path_rmdir(struct path *dir, struct dentry *dentry)
{
	return common_perm_rm(OP_RMDIR, dir, dentry, AA_MAY_DELETE);
}

static int apparmor_path_mknod(struct path *dir, struct dentry *dentry,
			       umode_t mode, unsigned int dev)
{
	return common_perm_create(OP_MKNOD, dir, dentry, AA_MAY_CREATE, mode);
}

static int apparmor_path_truncate(struct path *path)
{
	struct path_cond cond = { path->dentry->d_inode->i_uid,
				  path->dentry->d_inode->i_mode
	};

	if (!path->mnt || !mediated_filesystem(path->dentry->d_inode))
		return 0;

	return common_perm(OP_TRUNC, path, MAY_WRITE | AA_MAY_META_WRITE,
			   &cond);
}

static int apparmor_path_symlink(struct path *dir, struct dentry *dentry,
				 const char *old_name)
{
	return common_perm_create(OP_SYMLINK, dir, dentry, AA_MAY_CREATE,
				  S_IFLNK);
}

static int apparmor_path_link(struct dentry *old_dentry, struct path *new_dir,
			      struct dentry *new_dentry)
{
	struct aa_profile *profile;
	int error = 0;

	if (!mediated_filesystem(old_dentry->d_inode))
		return 0;

	profile = aa_current_profile();
	if (!unconfined(profile))
		error = aa_path_link(profile, old_dentry, new_dir, new_dentry);
	return error;
}

static int apparmor_path_rename(struct path *old_dir, struct dentry *old_dentry,
				struct path *new_dir, struct dentry *new_dentry)
{
	struct aa_profile *profile;
	int error = 0;

	if (!mediated_filesystem(old_dentry->d_inode))
		return 0;

	profile = aa_current_profile();
	if (!unconfined(profile)) {
		struct path old_path = { old_dir->mnt, old_dentry };
		struct path new_path = { new_dir->mnt, new_dentry };
		struct path_cond cond = { old_dentry->d_inode->i_uid,
					  old_dentry->d_inode->i_mode
		};

		error = aa_path_perm(OP_RENAME_SRC, profile, &old_path, 0,
				     MAY_READ | AA_MAY_META_READ | MAY_WRITE |
				     AA_MAY_META_WRITE | AA_MAY_DELETE,
				     &cond);
		if (!error)
			error = aa_path_perm(OP_RENAME_DEST, profile, &new_path,
					     0, MAY_WRITE | AA_MAY_META_WRITE |
					     AA_MAY_CREATE, &cond);

	}
	return error;
}

static int apparmor_path_chmod(struct path *path, umode_t mode)
{
	if (!mediated_filesystem(path->dentry->d_inode))
		return 0;

	return common_perm_mnt_dentry(OP_CHMOD, path->mnt, path->dentry, AA_MAY_CHMOD);
}

static int apparmor_path_chown(struct path *path, kuid_t uid, kgid_t gid)
{
	struct path_cond cond =  { path->dentry->d_inode->i_uid,
				   path->dentry->d_inode->i_mode
	};

	if (!mediated_filesystem(path->dentry->d_inode))
		return 0;

	return common_perm(OP_CHOWN, path, AA_MAY_CHOWN, &cond);
}

static int apparmor_inode_getattr(struct vfsmount *mnt, struct dentry *dentry)
{
	if (!mediated_filesystem(dentry->d_inode))
		return 0;

	return common_perm_mnt_dentry(OP_GETATTR, mnt, dentry,
				      AA_MAY_META_READ);
}

static int apparmor_file_open(struct file *file, const struct cred *cred)
{
	struct aa_file_cxt *fcxt = file->f_security;
	struct aa_profile *profile;
	int error = 0;

	if (!mediated_filesystem(file->f_path.dentry->d_inode))
		return 0;

	/* If in exec, permission is handled by bprm hooks.
	 * Cache permissions granted by the previous exec check, with
	 * implicit read and executable mmap which are required to
	 * actually execute the image.
	 */
	if (current->in_execve) {
		fcxt->allow = MAY_EXEC | MAY_READ | AA_EXEC_MMAP;
		return 0;
	}

	profile = aa_cred_profile(cred);
	if (!unconfined(profile)) {
		struct inode *inode = file->f_path.dentry->d_inode;
		struct path_cond cond = { inode->i_uid, inode->i_mode };

		error = aa_path_perm(OP_OPEN, profile, &file->f_path, 0,
				     aa_map_file_to_perms(file), &cond);
		/* todo cache full allowed permissions set and state */
		fcxt->allow = aa_map_file_to_perms(file);
	}

	return error;
}

static int apparmor_file_alloc_security(struct file *file)
{
	/* freed by apparmor_file_free_security */
	file->f_security = aa_alloc_file_context(GFP_KERNEL);
	if (!file->f_security)
		return -ENOMEM;
	return 0;

}

static void apparmor_file_free_security(struct file *file)
{
	struct aa_file_cxt *cxt = file->f_security;

	aa_free_file_context(cxt);
}

static int common_file_perm(int op, struct file *file, u32 mask)
{
	struct aa_file_cxt *fcxt = file->f_security;
	struct aa_profile *profile, *fprofile = aa_cred_profile(file->f_cred);
	int error = 0;

	BUG_ON(!fprofile);

	if (!file->f_path.mnt ||
	    !mediated_filesystem(file->f_path.dentry->d_inode))
		return 0;

	profile = __aa_current_profile();

	/* revalidate access, if task is unconfined, or the cached cred
	 * doesn't match or if the request is for more permissions than
	 * was granted.
	 *
	 * Note: the test for !unconfined(fprofile) is to handle file
	 *       delegation from unconfined tasks
	 */
	if (!unconfined(profile) && !unconfined(fprofile) &&
	    ((fprofile != profile) || (mask & ~fcxt->allow)))
		error = aa_file_perm(op, profile, file, mask);

	return error;
}

static int apparmor_file_permission(struct file *file, int mask)
{
	return common_file_perm(OP_FPERM, file, mask);
}

static int apparmor_file_lock(struct file *file, unsigned int cmd)
{
	u32 mask = AA_MAY_LOCK;

	if (cmd == F_WRLCK)
		mask |= MAY_WRITE;

	return common_file_perm(OP_FLOCK, file, mask);
}

static int common_mmap(int op, struct file *file, unsigned long prot,
		       unsigned long flags)
{
	struct dentry *dentry;
	int mask = 0;

	if (!file || !file->f_security)
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

	dentry = file->f_path.dentry;
	return common_file_perm(op, file, mask);
}

static int apparmor_mmap_file(struct file *file, unsigned long reqprot,
			      unsigned long prot, unsigned long flags)
{
	return common_mmap(OP_FMMAP, file, prot, flags);
}

static int apparmor_file_mprotect(struct vm_area_struct *vma,
				  unsigned long reqprot, unsigned long prot)
{
	return common_mmap(OP_FMPROT, vma->vm_file, prot,
			   !(vma->vm_flags & VM_SHARED) ? MAP_PRIVATE : 0);
}

static int apparmor_getprocattr(struct task_struct *task, char *name,
				char **value)
{
	int error = -ENOENT;
	struct aa_profile *profile;
	/* released below */
	const struct cred *cred = get_task_cred(task);
	struct aa_task_cxt *cxt = cred->security;
	profile = aa_cred_profile(cred);

	if (strcmp(name, "current") == 0)
		error = aa_getprocattr(aa_newest_version(cxt->profile),
				       value);
	else if (strcmp(name, "prev") == 0  && cxt->previous)
		error = aa_getprocattr(aa_newest_version(cxt->previous),
				       value);
	else if (strcmp(name, "exec") == 0 && cxt->onexec)
		error = aa_getprocattr(aa_newest_version(cxt->onexec),
				       value);
	else
		error = -EINVAL;

	put_cred(cred);

	return error;
}

static int apparmor_setprocattr(struct task_struct *task, char *name,
				void *value, size_t size)
{
	char *command, *args = value;
	size_t arg_size;
	int error;

	if (size == 0)
		return -EINVAL;
	/* args points to a PAGE_SIZE buffer, AppArmor requires that
	 * the buffer must be null terminated or have size <= PAGE_SIZE -1
	 * so that AppArmor can null terminate them
	 */
	if (args[size - 1] != '\0') {
		if (size == PAGE_SIZE)
			return -EINVAL;
		args[size] = '\0';
	}

	/* task can only write its own attributes */
	if (current != task)
		return -EACCES;

	args = value;
	args = strim(args);
	command = strsep(&args, " ");
	if (!args)
		return -EINVAL;
	args = skip_spaces(args);
	if (!*args)
		return -EINVAL;

	arg_size = size - (args - (char *) value);
	if (strcmp(name, "current") == 0) {
		if (strcmp(command, "changehat") == 0) {
			error = aa_setprocattr_changehat(args, arg_size,
							 !AA_DO_TEST);
		} else if (strcmp(command, "permhat") == 0) {
			error = aa_setprocattr_changehat(args, arg_size,
							 AA_DO_TEST);
		} else if (strcmp(command, "changeprofile") == 0) {
			error = aa_setprocattr_changeprofile(args, !AA_ONEXEC,
							     !AA_DO_TEST);
		} else if (strcmp(command, "permprofile") == 0) {
			error = aa_setprocattr_changeprofile(args, !AA_ONEXEC,
							     AA_DO_TEST);
		} else if (strcmp(command, "permipc") == 0) {
			error = aa_setprocattr_permipc(args);
		} else {
			struct common_audit_data sa;
			struct apparmor_audit_data aad = {0,};
			sa.type = LSM_AUDIT_DATA_NONE;
			sa.aad = &aad;
			aad.op = OP_SETPROCATTR;
			aad.info = name;
			aad.error = -EINVAL;
			return aa_audit(AUDIT_APPARMOR_DENIED,
					__aa_current_profile(), GFP_KERNEL,
					&sa, NULL);
		}
	} else if (strcmp(name, "exec") == 0) {
		error = aa_setprocattr_changeprofile(args, AA_ONEXEC,
						     !AA_DO_TEST);
	} else {
		/* only support the "current" and "exec" process attributes */
		return -EINVAL;
	}
	if (!error)
		error = size;
	return error;
}

static int apparmor_task_setrlimit(struct task_struct *task,
		unsigned int resource, struct rlimit *new_rlim)
{
	struct aa_profile *profile = __aa_current_profile();
	int error = 0;

	if (!unconfined(profile))
		error = aa_task_setrlimit(profile, task, resource, new_rlim);

	return error;
}

static struct security_operations apparmor_ops = {
	.name =				"apparmor",

	.ptrace_access_check =		apparmor_ptrace_access_check,
	.ptrace_traceme =		apparmor_ptrace_traceme,
	.capget =			apparmor_capget,
	.capable =			apparmor_capable,

	.path_link =			apparmor_path_link,
	.path_unlink =			apparmor_path_unlink,
	.path_symlink =			apparmor_path_symlink,
	.path_mkdir =			apparmor_path_mkdir,
	.path_rmdir =			apparmor_path_rmdir,
	.path_mknod =			apparmor_path_mknod,
	.path_rename =			apparmor_path_rename,
	.path_chmod =			apparmor_path_chmod,
	.path_chown =			apparmor_path_chown,
	.path_truncate =		apparmor_path_truncate,
	.inode_getattr =                apparmor_inode_getattr,

	.file_open =			apparmor_file_open,
	.file_permission =		apparmor_file_permission,
	.file_alloc_security =		apparmor_file_alloc_security,
	.file_free_security =		apparmor_file_free_security,
	.mmap_file =			apparmor_mmap_file,
	.mmap_addr =			cap_mmap_addr,
	.file_mprotect =		apparmor_file_mprotect,
	.file_lock =			apparmor_file_lock,

	.getprocattr =			apparmor_getprocattr,
	.setprocattr =			apparmor_setprocattr,

	.cred_alloc_blank =		apparmor_cred_alloc_blank,
	.cred_free =			apparmor_cred_free,
	.cred_prepare =			apparmor_cred_prepare,
	.cred_transfer =		apparmor_cred_transfer,

	.bprm_set_creds =		apparmor_bprm_set_creds,
	.bprm_committing_creds =	apparmor_bprm_committing_creds,
	.bprm_committed_creds =		apparmor_bprm_committed_creds,
	.bprm_secureexec =		apparmor_bprm_secureexec,

	.task_setrlimit =		apparmor_task_setrlimit,
};

/*
 * AppArmor sysfs module parameters
 */

static int param_set_aabool(const char *val, const struct kernel_param *kp);
static int param_get_aabool(char *buffer, const struct kernel_param *kp);
#define param_check_aabool param_check_bool
static struct kernel_param_ops param_ops_aabool = {
	.set = param_set_aabool,
	.get = param_get_aabool
};

static int param_set_aauint(const char *val, const struct kernel_param *kp);
static int param_get_aauint(char *buffer, const struct kernel_param *kp);
#define param_check_aauint param_check_uint
static struct kernel_param_ops param_ops_aauint = {
	.set = param_set_aauint,
	.get = param_get_aauint
};

static int param_set_aalockpolicy(const char *val, const struct kernel_param *kp);
static int param_get_aalockpolicy(char *buffer, const struct kernel_param *kp);
#define param_check_aalockpolicy param_check_bool
static struct kernel_param_ops param_ops_aalockpolicy = {
	.set = param_set_aalockpolicy,
	.get = param_get_aalockpolicy
};

static int param_set_audit(const char *val, struct kernel_param *kp);
static int param_get_audit(char *buffer, struct kernel_param *kp);

static int param_set_mode(const char *val, struct kernel_param *kp);
static int param_get_mode(char *buffer, struct kernel_param *kp);

/* Flag values, also controllable via /sys/module/apparmor/parameters
 * We define special types as we want to do additional mediation.
 */

/* AppArmor global enforcement switch - complain, enforce, kill */
enum profile_mode aa_g_profile_mode = APPARMOR_ENFORCE;
module_param_call(mode, param_set_mode, param_get_mode,
		  &aa_g_profile_mode, S_IRUSR | S_IWUSR);

/* Debug mode */
bool aa_g_debug;
module_param_named(debug, aa_g_debug, aabool, S_IRUSR | S_IWUSR);

/* Audit mode */
enum audit_mode aa_g_audit;
module_param_call(audit, param_set_audit, param_get_audit,
		  &aa_g_audit, S_IRUSR | S_IWUSR);

/* Determines if audit header is included in audited messages.  This
 * provides more context if the audit daemon is not running
 */
bool aa_g_audit_header = 1;
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
module_param_named(path_max, aa_g_path_max, aauint, S_IRUSR | S_IWUSR);

/* Determines how paranoid loading of policy is and how much verification
 * on the loaded policy is done.
 */
bool aa_g_paranoid_load = 1;
module_param_named(paranoid_load, aa_g_paranoid_load, aabool,
		   S_IRUSR | S_IWUSR);

/* Boot time disable flag */
static bool apparmor_enabled = CONFIG_SECURITY_APPARMOR_BOOTPARAM_VALUE;
module_param_named(enabled, apparmor_enabled, aabool, S_IRUSR);

static int __init apparmor_enabled_setup(char *str)
{
	unsigned long enabled;
	int error = strict_strtoul(str, 0, &enabled);
	if (!error)
		apparmor_enabled = enabled ? 1 : 0;
	return 1;
}

__setup("apparmor=", apparmor_enabled_setup);

/* set global flag turning off the ability to load policy */
static int param_set_aalockpolicy(const char *val, const struct kernel_param *kp)
{
	if (!capable(CAP_MAC_ADMIN))
		return -EPERM;
	if (aa_g_lock_policy)
		return -EACCES;
	return param_set_bool(val, kp);
}

static int param_get_aalockpolicy(char *buffer, const struct kernel_param *kp)
{
	if (!capable(CAP_MAC_ADMIN))
		return -EPERM;
	return param_get_bool(buffer, kp);
}

static int param_set_aabool(const char *val, const struct kernel_param *kp)
{
	if (!capable(CAP_MAC_ADMIN))
		return -EPERM;
	return param_set_bool(val, kp);
}

static int param_get_aabool(char *buffer, const struct kernel_param *kp)
{
	if (!capable(CAP_MAC_ADMIN))
		return -EPERM;
	return param_get_bool(buffer, kp);
}

static int param_set_aauint(const char *val, const struct kernel_param *kp)
{
	if (!capable(CAP_MAC_ADMIN))
		return -EPERM;
	return param_set_uint(val, kp);
}

static int param_get_aauint(char *buffer, const struct kernel_param *kp)
{
	if (!capable(CAP_MAC_ADMIN))
		return -EPERM;
	return param_get_uint(buffer, kp);
}

static int param_get_audit(char *buffer, struct kernel_param *kp)
{
	if (!capable(CAP_MAC_ADMIN))
		return -EPERM;

	if (!apparmor_enabled)
		return -EINVAL;

	return sprintf(buffer, "%s", audit_mode_names[aa_g_audit]);
}

static int param_set_audit(const char *val, struct kernel_param *kp)
{
	int i;
	if (!capable(CAP_MAC_ADMIN))
		return -EPERM;

	if (!apparmor_enabled)
		return -EINVAL;

	if (!val)
		return -EINVAL;

	for (i = 0; i < AUDIT_MAX_INDEX; i++) {
		if (strcmp(val, audit_mode_names[i]) == 0) {
			aa_g_audit = i;
			return 0;
		}
	}

	return -EINVAL;
}

static int param_get_mode(char *buffer, struct kernel_param *kp)
{
	if (!capable(CAP_MAC_ADMIN))
		return -EPERM;

	if (!apparmor_enabled)
		return -EINVAL;

	return sprintf(buffer, "%s", profile_mode_names[aa_g_profile_mode]);
}

static int param_set_mode(const char *val, struct kernel_param *kp)
{
	int i;
	if (!capable(CAP_MAC_ADMIN))
		return -EPERM;

	if (!apparmor_enabled)
		return -EINVAL;

	if (!val)
		return -EINVAL;

	for (i = 0; i < APPARMOR_NAMES_MAX_INDEX; i++) {
		if (strcmp(val, profile_mode_names[i]) == 0) {
			aa_g_profile_mode = i;
			return 0;
		}
	}

	return -EINVAL;
}

/*
 * AppArmor init functions
 */

/**
 * set_init_cxt - set a task context and profile on the first task.
 *
 * TODO: allow setting an alternate profile than unconfined
 */
static int __init set_init_cxt(void)
{
	struct cred *cred = (struct cred *)current->real_cred;
	struct aa_task_cxt *cxt;

	cxt = aa_alloc_task_context(GFP_KERNEL);
	if (!cxt)
		return -ENOMEM;

	cxt->profile = aa_get_profile(root_ns->unconfined);
	cred->security = cxt;

	return 0;
}

static int __init apparmor_init(void)
{
	int error;

	if (!apparmor_enabled || !security_module_enable(&apparmor_ops)) {
		aa_info_message("AppArmor disabled by boot time parameter");
		apparmor_enabled = 0;
		return 0;
	}

	error = aa_alloc_root_ns();
	if (error) {
		AA_ERROR("Unable to allocate default profile namespace\n");
		goto alloc_out;
	}

	error = set_init_cxt();
	if (error) {
		AA_ERROR("Failed to set context on init task\n");
		goto register_security_out;
	}

	error = register_security(&apparmor_ops);
	if (error) {
		AA_ERROR("Unable to register AppArmor\n");
		goto set_init_cxt_out;
	}

	/* Report that AppArmor successfully initialized */
	apparmor_initialized = 1;
	if (aa_g_profile_mode == APPARMOR_COMPLAIN)
		aa_info_message("AppArmor initialized: complain mode enabled");
	else if (aa_g_profile_mode == APPARMOR_KILL)
		aa_info_message("AppArmor initialized: kill mode enabled");
	else
		aa_info_message("AppArmor initialized");

	return error;

set_init_cxt_out:
	aa_free_task_context(current->real_cred->security);

register_security_out:
	aa_free_root_ns();

alloc_out:
	aa_destroy_aafs();

	apparmor_enabled = 0;
	return error;
}

security_initcall(apparmor_init);
