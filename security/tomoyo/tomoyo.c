/*
 * security/tomoyo/tomoyo.c
 *
 * Copyright (C) 2005-2011  NTT DATA CORPORATION
 */

#include <linux/security.h>
#include "common.h"

/**
 * tomoyo_cred_alloc_blank - Target for security_cred_alloc_blank().
 *
 * @new: Pointer to "struct cred".
 * @gfp: Memory allocation flags.
 *
 * Returns 0.
 */
static int tomoyo_cred_alloc_blank(struct cred *new, gfp_t gfp)
{
	new->security = NULL;
	return 0;
}

/**
 * tomoyo_cred_prepare - Target for security_prepare_creds().
 *
 * @new: Pointer to "struct cred".
 * @old: Pointer to "struct cred".
 * @gfp: Memory allocation flags.
 *
 * Returns 0.
 */
static int tomoyo_cred_prepare(struct cred *new, const struct cred *old,
			       gfp_t gfp)
{
	struct tomoyo_domain_info *domain = old->security;
	new->security = domain;
	if (domain)
		atomic_inc(&domain->users);
	return 0;
}

/**
 * tomoyo_cred_transfer - Target for security_transfer_creds().
 *
 * @new: Pointer to "struct cred".
 * @old: Pointer to "struct cred".
 */
static void tomoyo_cred_transfer(struct cred *new, const struct cred *old)
{
	tomoyo_cred_prepare(new, old, 0);
}

/**
 * tomoyo_cred_free - Target for security_cred_free().
 *
 * @cred: Pointer to "struct cred".
 */
static void tomoyo_cred_free(struct cred *cred)
{
	struct tomoyo_domain_info *domain = cred->security;
	if (domain)
		atomic_dec(&domain->users);
}

/**
 * tomoyo_bprm_set_creds - Target for security_bprm_set_creds().
 *
 * @bprm: Pointer to "struct linux_binprm".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_bprm_set_creds(struct linux_binprm *bprm)
{
	int rc;

	rc = cap_bprm_set_creds(bprm);
	if (rc)
		return rc;

	/*
	 * Do only if this function is called for the first time of an execve
	 * operation.
	 */
	if (bprm->cred_prepared)
		return 0;
#ifndef CONFIG_SECURITY_TOMOYO_OMIT_USERSPACE_LOADER
	/*
	 * Load policy if /sbin/tomoyo-init exists and /sbin/init is requested
	 * for the first time.
	 */
	if (!tomoyo_policy_loaded)
		tomoyo_load_policy(bprm->filename);
#endif
	/*
	 * Release reference to "struct tomoyo_domain_info" stored inside
	 * "bprm->cred->security". New reference to "struct tomoyo_domain_info"
	 * stored inside "bprm->cred->security" will be acquired later inside
	 * tomoyo_find_next_domain().
	 */
	atomic_dec(&((struct tomoyo_domain_info *)
		     bprm->cred->security)->users);
	/*
	 * Tell tomoyo_bprm_check_security() is called for the first time of an
	 * execve operation.
	 */
	bprm->cred->security = NULL;
	return 0;
}

/**
 * tomoyo_bprm_check_security - Target for security_bprm_check().
 *
 * @bprm: Pointer to "struct linux_binprm".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_bprm_check_security(struct linux_binprm *bprm)
{
	struct tomoyo_domain_info *domain = bprm->cred->security;

	/*
	 * Execute permission is checked against pathname passed to do_execve()
	 * using current domain.
	 */
	if (!domain) {
		const int idx = tomoyo_read_lock();
		const int err = tomoyo_find_next_domain(bprm);
		tomoyo_read_unlock(idx);
		return err;
	}
	/*
	 * Read permission is checked against interpreters using next domain.
	 */
	return tomoyo_check_open_permission(domain, &bprm->file->f_path,
					    O_RDONLY);
}

/**
 * tomoyo_inode_getattr - Target for security_inode_getattr().
 *
 * @mnt:    Pointer to "struct vfsmount".
 * @dentry: Pointer to "struct dentry".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_inode_getattr(struct vfsmount *mnt, struct dentry *dentry)
{
	struct path path = { mnt, dentry };
	return tomoyo_path_perm(TOMOYO_TYPE_GETATTR, &path, NULL);
}

/**
 * tomoyo_path_truncate - Target for security_path_truncate().
 *
 * @path: Pointer to "struct path".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_path_truncate(struct path *path)
{
	return tomoyo_path_perm(TOMOYO_TYPE_TRUNCATE, path, NULL);
}

/**
 * tomoyo_path_unlink - Target for security_path_unlink().
 *
 * @parent: Pointer to "struct path".
 * @dentry: Pointer to "struct dentry".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_path_unlink(struct path *parent, struct dentry *dentry)
{
	struct path path = { parent->mnt, dentry };
	return tomoyo_path_perm(TOMOYO_TYPE_UNLINK, &path, NULL);
}

/**
 * tomoyo_path_mkdir - Target for security_path_mkdir().
 *
 * @parent: Pointer to "struct path".
 * @dentry: Pointer to "struct dentry".
 * @mode:   DAC permission mode.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_path_mkdir(struct path *parent, struct dentry *dentry,
			     int mode)
{
	struct path path = { parent->mnt, dentry };
	return tomoyo_path_number_perm(TOMOYO_TYPE_MKDIR, &path,
				       mode & S_IALLUGO);
}

/**
 * tomoyo_path_rmdir - Target for security_path_rmdir().
 *
 * @parent: Pointer to "struct path".
 * @dentry: Pointer to "struct dentry".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_path_rmdir(struct path *parent, struct dentry *dentry)
{
	struct path path = { parent->mnt, dentry };
	return tomoyo_path_perm(TOMOYO_TYPE_RMDIR, &path, NULL);
}

/**
 * tomoyo_path_symlink - Target for security_path_symlink().
 *
 * @parent:   Pointer to "struct path".
 * @dentry:   Pointer to "struct dentry".
 * @old_name: Symlink's content.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_path_symlink(struct path *parent, struct dentry *dentry,
			       const char *old_name)
{
	struct path path = { parent->mnt, dentry };
	return tomoyo_path_perm(TOMOYO_TYPE_SYMLINK, &path, old_name);
}

/**
 * tomoyo_path_mknod - Target for security_path_mknod().
 *
 * @parent: Pointer to "struct path".
 * @dentry: Pointer to "struct dentry".
 * @mode:   DAC permission mode.
 * @dev:    Device attributes.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_path_mknod(struct path *parent, struct dentry *dentry,
			     int mode, unsigned int dev)
{
	struct path path = { parent->mnt, dentry };
	int type = TOMOYO_TYPE_CREATE;
	const unsigned int perm = mode & S_IALLUGO;

	switch (mode & S_IFMT) {
	case S_IFCHR:
		type = TOMOYO_TYPE_MKCHAR;
		break;
	case S_IFBLK:
		type = TOMOYO_TYPE_MKBLOCK;
		break;
	default:
		goto no_dev;
	}
	return tomoyo_mkdev_perm(type, &path, perm, dev);
 no_dev:
	switch (mode & S_IFMT) {
	case S_IFIFO:
		type = TOMOYO_TYPE_MKFIFO;
		break;
	case S_IFSOCK:
		type = TOMOYO_TYPE_MKSOCK;
		break;
	}
	return tomoyo_path_number_perm(type, &path, perm);
}

/**
 * tomoyo_path_link - Target for security_path_link().
 *
 * @old_dentry: Pointer to "struct dentry".
 * @new_dir:    Pointer to "struct path".
 * @new_dentry: Pointer to "struct dentry".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_path_link(struct dentry *old_dentry, struct path *new_dir,
			    struct dentry *new_dentry)
{
	struct path path1 = { new_dir->mnt, old_dentry };
	struct path path2 = { new_dir->mnt, new_dentry };
	return tomoyo_path2_perm(TOMOYO_TYPE_LINK, &path1, &path2);
}

/**
 * tomoyo_path_rename - Target for security_path_rename().
 *
 * @old_parent: Pointer to "struct path".
 * @old_dentry: Pointer to "struct dentry".
 * @new_parent: Pointer to "struct path".
 * @new_dentry: Pointer to "struct dentry".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_path_rename(struct path *old_parent,
			      struct dentry *old_dentry,
			      struct path *new_parent,
			      struct dentry *new_dentry)
{
	struct path path1 = { old_parent->mnt, old_dentry };
	struct path path2 = { new_parent->mnt, new_dentry };
	return tomoyo_path2_perm(TOMOYO_TYPE_RENAME, &path1, &path2);
}

/**
 * tomoyo_file_fcntl - Target for security_file_fcntl().
 *
 * @file: Pointer to "struct file".
 * @cmd:  Command for fcntl().
 * @arg:  Argument for @cmd.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_file_fcntl(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	if (!(cmd == F_SETFL && ((arg ^ file->f_flags) & O_APPEND)))
		return 0;
	return tomoyo_check_open_permission(tomoyo_domain(), &file->f_path,
					    O_WRONLY | (arg & O_APPEND));
}

/**
 * tomoyo_dentry_open - Target for security_dentry_open().
 *
 * @f:    Pointer to "struct file".
 * @cred: Pointer to "struct cred".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_dentry_open(struct file *f, const struct cred *cred)
{
	int flags = f->f_flags;
	/* Don't check read permission here if called from do_execve(). */
	if (current->in_execve)
		return 0;
	return tomoyo_check_open_permission(tomoyo_domain(), &f->f_path, flags);
}

/**
 * tomoyo_file_ioctl - Target for security_file_ioctl().
 *
 * @file: Pointer to "struct file".
 * @cmd:  Command for ioctl().
 * @arg:  Argument for @cmd.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_file_ioctl(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	return tomoyo_path_number_perm(TOMOYO_TYPE_IOCTL, &file->f_path, cmd);
}

/**
 * tomoyo_path_chmod - Target for security_path_chmod().
 *
 * @dentry: Pointer to "struct dentry".
 * @mnt:    Pointer to "struct vfsmount".
 * @mode:   DAC permission mode.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_path_chmod(struct dentry *dentry, struct vfsmount *mnt,
			     mode_t mode)
{
	struct path path = { mnt, dentry };
	return tomoyo_path_number_perm(TOMOYO_TYPE_CHMOD, &path,
				       mode & S_IALLUGO);
}

/**
 * tomoyo_path_chown - Target for security_path_chown().
 *
 * @path: Pointer to "struct path".
 * @uid:  Owner ID.
 * @gid:  Group ID.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_path_chown(struct path *path, uid_t uid, gid_t gid)
{
	int error = 0;
	if (uid != (uid_t) -1)
		error = tomoyo_path_number_perm(TOMOYO_TYPE_CHOWN, path, uid);
	if (!error && gid != (gid_t) -1)
		error = tomoyo_path_number_perm(TOMOYO_TYPE_CHGRP, path, gid);
	return error;
}

/**
 * tomoyo_path_chroot - Target for security_path_chroot().
 *
 * @path: Pointer to "struct path".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_path_chroot(struct path *path)
{
	return tomoyo_path_perm(TOMOYO_TYPE_CHROOT, path, NULL);
}

/**
 * tomoyo_sb_mount - Target for security_sb_mount().
 *
 * @dev_name: Name of device file. Maybe NULL.
 * @path:     Pointer to "struct path".
 * @type:     Name of filesystem type. Maybe NULL.
 * @flags:    Mount options.
 * @data:     Optional data. Maybe NULL.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_sb_mount(char *dev_name, struct path *path,
			   char *type, unsigned long flags, void *data)
{
	return tomoyo_mount_permission(dev_name, path, type, flags, data);
}

/**
 * tomoyo_sb_umount - Target for security_sb_umount().
 *
 * @mnt:   Pointer to "struct vfsmount".
 * @flags: Unmount options.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_sb_umount(struct vfsmount *mnt, int flags)
{
	struct path path = { mnt, mnt->mnt_root };
	return tomoyo_path_perm(TOMOYO_TYPE_UMOUNT, &path, NULL);
}

/**
 * tomoyo_sb_pivotroot - Target for security_sb_pivotroot().
 *
 * @old_path: Pointer to "struct path".
 * @new_path: Pointer to "struct path".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_sb_pivotroot(struct path *old_path, struct path *new_path)
{
	return tomoyo_path2_perm(TOMOYO_TYPE_PIVOT_ROOT, new_path, old_path);
}

/*
 * tomoyo_security_ops is a "struct security_operations" which is used for
 * registering TOMOYO.
 */
static struct security_operations tomoyo_security_ops = {
	.name                = "tomoyo",
	.cred_alloc_blank    = tomoyo_cred_alloc_blank,
	.cred_prepare        = tomoyo_cred_prepare,
	.cred_transfer	     = tomoyo_cred_transfer,
	.cred_free           = tomoyo_cred_free,
	.bprm_set_creds      = tomoyo_bprm_set_creds,
	.bprm_check_security = tomoyo_bprm_check_security,
	.file_fcntl          = tomoyo_file_fcntl,
	.dentry_open         = tomoyo_dentry_open,
	.path_truncate       = tomoyo_path_truncate,
	.path_unlink         = tomoyo_path_unlink,
	.path_mkdir          = tomoyo_path_mkdir,
	.path_rmdir          = tomoyo_path_rmdir,
	.path_symlink        = tomoyo_path_symlink,
	.path_mknod          = tomoyo_path_mknod,
	.path_link           = tomoyo_path_link,
	.path_rename         = tomoyo_path_rename,
	.inode_getattr       = tomoyo_inode_getattr,
	.file_ioctl          = tomoyo_file_ioctl,
	.path_chmod          = tomoyo_path_chmod,
	.path_chown          = tomoyo_path_chown,
	.path_chroot         = tomoyo_path_chroot,
	.sb_mount            = tomoyo_sb_mount,
	.sb_umount           = tomoyo_sb_umount,
	.sb_pivotroot        = tomoyo_sb_pivotroot,
};

/* Lock for GC. */
struct srcu_struct tomoyo_ss;

/**
 * tomoyo_init - Register TOMOYO Linux as a LSM module.
 *
 * Returns 0.
 */
static int __init tomoyo_init(void)
{
	struct cred *cred = (struct cred *) current_cred();

	if (!security_module_enable(&tomoyo_security_ops))
		return 0;
	/* register ourselves with the security framework */
	if (register_security(&tomoyo_security_ops) ||
	    init_srcu_struct(&tomoyo_ss))
		panic("Failure registering TOMOYO Linux");
	printk(KERN_INFO "TOMOYO Linux initialized\n");
	cred->security = &tomoyo_kernel_domain;
	tomoyo_mm_init();
	return 0;
}

security_initcall(tomoyo_init);
