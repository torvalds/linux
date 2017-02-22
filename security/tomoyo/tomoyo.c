/*
 * security/tomoyo/tomoyo.c
 *
 * Copyright (C) 2005-2011  NTT DATA CORPORATION
 */

#include <linux/lsm_hooks.h>
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
static int tomoyo_inode_getattr(const struct path *path)
{
	return tomoyo_path_perm(TOMOYO_TYPE_GETATTR, path, NULL);
}

/**
 * tomoyo_path_truncate - Target for security_path_truncate().
 *
 * @path: Pointer to "struct path".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_path_truncate(const struct path *path)
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
static int tomoyo_path_unlink(const struct path *parent, struct dentry *dentry)
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
static int tomoyo_path_mkdir(const struct path *parent, struct dentry *dentry,
			     umode_t mode)
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
static int tomoyo_path_rmdir(const struct path *parent, struct dentry *dentry)
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
static int tomoyo_path_symlink(const struct path *parent, struct dentry *dentry,
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
static int tomoyo_path_mknod(const struct path *parent, struct dentry *dentry,
			     umode_t mode, unsigned int dev)
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
static int tomoyo_path_link(struct dentry *old_dentry, const struct path *new_dir,
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
static int tomoyo_path_rename(const struct path *old_parent,
			      struct dentry *old_dentry,
			      const struct path *new_parent,
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
 * tomoyo_file_open - Target for security_file_open().
 *
 * @f:    Pointer to "struct file".
 * @cred: Pointer to "struct cred".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_file_open(struct file *f, const struct cred *cred)
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
 * @path: Pointer to "struct path".
 * @mode: DAC permission mode.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_path_chmod(const struct path *path, umode_t mode)
{
	return tomoyo_path_number_perm(TOMOYO_TYPE_CHMOD, path,
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
static int tomoyo_path_chown(const struct path *path, kuid_t uid, kgid_t gid)
{
	int error = 0;
	if (uid_valid(uid))
		error = tomoyo_path_number_perm(TOMOYO_TYPE_CHOWN, path,
						from_kuid(&init_user_ns, uid));
	if (!error && gid_valid(gid))
		error = tomoyo_path_number_perm(TOMOYO_TYPE_CHGRP, path,
						from_kgid(&init_user_ns, gid));
	return error;
}

/**
 * tomoyo_path_chroot - Target for security_path_chroot().
 *
 * @path: Pointer to "struct path".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_path_chroot(const struct path *path)
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
static int tomoyo_sb_mount(const char *dev_name, const struct path *path,
			   const char *type, unsigned long flags, void *data)
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
static int tomoyo_sb_pivotroot(const struct path *old_path, const struct path *new_path)
{
	return tomoyo_path2_perm(TOMOYO_TYPE_PIVOT_ROOT, new_path, old_path);
}

/**
 * tomoyo_socket_listen - Check permission for listen().
 *
 * @sock:    Pointer to "struct socket".
 * @backlog: Backlog parameter.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_socket_listen(struct socket *sock, int backlog)
{
	return tomoyo_socket_listen_permission(sock);
}

/**
 * tomoyo_socket_connect - Check permission for connect().
 *
 * @sock:     Pointer to "struct socket".
 * @addr:     Pointer to "struct sockaddr".
 * @addr_len: Size of @addr.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_socket_connect(struct socket *sock, struct sockaddr *addr,
				 int addr_len)
{
	return tomoyo_socket_connect_permission(sock, addr, addr_len);
}

/**
 * tomoyo_socket_bind - Check permission for bind().
 *
 * @sock:     Pointer to "struct socket".
 * @addr:     Pointer to "struct sockaddr".
 * @addr_len: Size of @addr.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_socket_bind(struct socket *sock, struct sockaddr *addr,
			      int addr_len)
{
	return tomoyo_socket_bind_permission(sock, addr, addr_len);
}

/**
 * tomoyo_socket_sendmsg - Check permission for sendmsg().
 *
 * @sock: Pointer to "struct socket".
 * @msg:  Pointer to "struct msghdr".
 * @size: Size of message.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_socket_sendmsg(struct socket *sock, struct msghdr *msg,
				 int size)
{
	return tomoyo_socket_sendmsg_permission(sock, msg, size);
}

/*
 * tomoyo_security_ops is a "struct security_operations" which is used for
 * registering TOMOYO.
 */
static struct security_hook_list tomoyo_hooks[] = {
	LSM_HOOK_INIT(cred_alloc_blank, tomoyo_cred_alloc_blank),
	LSM_HOOK_INIT(cred_prepare, tomoyo_cred_prepare),
	LSM_HOOK_INIT(cred_transfer, tomoyo_cred_transfer),
	LSM_HOOK_INIT(cred_free, tomoyo_cred_free),
	LSM_HOOK_INIT(bprm_set_creds, tomoyo_bprm_set_creds),
	LSM_HOOK_INIT(bprm_check_security, tomoyo_bprm_check_security),
	LSM_HOOK_INIT(file_fcntl, tomoyo_file_fcntl),
	LSM_HOOK_INIT(file_open, tomoyo_file_open),
	LSM_HOOK_INIT(path_truncate, tomoyo_path_truncate),
	LSM_HOOK_INIT(path_unlink, tomoyo_path_unlink),
	LSM_HOOK_INIT(path_mkdir, tomoyo_path_mkdir),
	LSM_HOOK_INIT(path_rmdir, tomoyo_path_rmdir),
	LSM_HOOK_INIT(path_symlink, tomoyo_path_symlink),
	LSM_HOOK_INIT(path_mknod, tomoyo_path_mknod),
	LSM_HOOK_INIT(path_link, tomoyo_path_link),
	LSM_HOOK_INIT(path_rename, tomoyo_path_rename),
	LSM_HOOK_INIT(inode_getattr, tomoyo_inode_getattr),
	LSM_HOOK_INIT(file_ioctl, tomoyo_file_ioctl),
	LSM_HOOK_INIT(path_chmod, tomoyo_path_chmod),
	LSM_HOOK_INIT(path_chown, tomoyo_path_chown),
	LSM_HOOK_INIT(path_chroot, tomoyo_path_chroot),
	LSM_HOOK_INIT(sb_mount, tomoyo_sb_mount),
	LSM_HOOK_INIT(sb_umount, tomoyo_sb_umount),
	LSM_HOOK_INIT(sb_pivotroot, tomoyo_sb_pivotroot),
	LSM_HOOK_INIT(socket_bind, tomoyo_socket_bind),
	LSM_HOOK_INIT(socket_connect, tomoyo_socket_connect),
	LSM_HOOK_INIT(socket_listen, tomoyo_socket_listen),
	LSM_HOOK_INIT(socket_sendmsg, tomoyo_socket_sendmsg),
};

/* Lock for GC. */
DEFINE_SRCU(tomoyo_ss);

/**
 * tomoyo_init - Register TOMOYO Linux as a LSM module.
 *
 * Returns 0.
 */
static int __init tomoyo_init(void)
{
	struct cred *cred = (struct cred *) current_cred();

	if (!security_module_enable("tomoyo"))
		return 0;
	/* register ourselves with the security framework */
	security_add_hooks(tomoyo_hooks, ARRAY_SIZE(tomoyo_hooks), "tomoyo");
	printk(KERN_INFO "TOMOYO Linux initialized\n");
	cred->security = &tomoyo_kernel_domain;
	tomoyo_mm_init();
	return 0;
}

security_initcall(tomoyo_init);
