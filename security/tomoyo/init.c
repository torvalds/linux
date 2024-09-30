// SPDX-License-Identifier: GPL-2.0
/*
 * security/tomoyo/init.c
 *
 * Copyright (C) 2005-2011  NTT DATA CORPORATION
 */

#include <linux/lsm_hooks.h>
#include <uapi/linux/lsm.h>
#include "common.h"

#ifndef CONFIG_SECURITY_TOMOYO_LKM

#include "hooks.h"

#else

#define DEFINE_STATIC_CALL_PROXY(NAME)				\
	static NAME##_t tomoyo_##NAME;				\
	DEFINE_STATIC_CALL_RET0(tomoyo_##NAME, tomoyo_##NAME);
DEFINE_STATIC_CALL_PROXY(cred_prepare)
DEFINE_STATIC_CALL_PROXY(bprm_committed_creds)
DEFINE_STATIC_CALL_PROXY(bprm_check_security)
DEFINE_STATIC_CALL_PROXY(inode_getattr)
DEFINE_STATIC_CALL_PROXY(path_truncate)
DEFINE_STATIC_CALL_PROXY(file_truncate)
DEFINE_STATIC_CALL_PROXY(path_unlink)
DEFINE_STATIC_CALL_PROXY(path_mkdir)
DEFINE_STATIC_CALL_PROXY(path_rmdir)
DEFINE_STATIC_CALL_PROXY(path_symlink)
DEFINE_STATIC_CALL_PROXY(path_mknod)
DEFINE_STATIC_CALL_PROXY(path_link)
DEFINE_STATIC_CALL_PROXY(path_rename)
DEFINE_STATIC_CALL_PROXY(file_fcntl)
DEFINE_STATIC_CALL_PROXY(file_open)
DEFINE_STATIC_CALL_PROXY(file_ioctl)
DEFINE_STATIC_CALL_PROXY(path_chmod)
DEFINE_STATIC_CALL_PROXY(path_chown)
DEFINE_STATIC_CALL_PROXY(path_chroot)
DEFINE_STATIC_CALL_PROXY(sb_mount)
DEFINE_STATIC_CALL_PROXY(sb_umount)
DEFINE_STATIC_CALL_PROXY(sb_pivotroot)
DEFINE_STATIC_CALL_PROXY(socket_listen)
DEFINE_STATIC_CALL_PROXY(socket_connect)
DEFINE_STATIC_CALL_PROXY(socket_bind)
DEFINE_STATIC_CALL_PROXY(socket_sendmsg)
DEFINE_STATIC_CALL_PROXY(task_alloc)
DEFINE_STATIC_CALL_PROXY(task_free)
#undef DEFINE_STATIC_CALL_PROXY

static int tomoyo_cred_prepare(struct cred *new, const struct cred *old, gfp_t gfp)
{
	return static_call(tomoyo_cred_prepare)(new, old, gfp);
}

static void tomoyo_bprm_committed_creds(const struct linux_binprm *bprm)
{
	static_call(tomoyo_bprm_committed_creds)(bprm);
}

static int tomoyo_bprm_check_security(struct linux_binprm *bprm)
{
	return static_call(tomoyo_bprm_check_security)(bprm);
}

static int tomoyo_inode_getattr(const struct path *path)
{
	return static_call(tomoyo_inode_getattr)(path);
}

static int tomoyo_path_truncate(const struct path *path)
{
	return static_call(tomoyo_path_truncate)(path);
}

static int tomoyo_file_truncate(struct file *file)
{
	return static_call(tomoyo_file_truncate)(file);
}

static int tomoyo_path_unlink(const struct path *parent, struct dentry *dentry)
{
	return static_call(tomoyo_path_unlink)(parent, dentry);
}

static int tomoyo_path_mkdir(const struct path *parent, struct dentry *dentry, umode_t mode)
{
	return static_call(tomoyo_path_mkdir)(parent, dentry, mode);
}

static int tomoyo_path_rmdir(const struct path *parent, struct dentry *dentry)
{
	return static_call(tomoyo_path_rmdir)(parent, dentry);
}

static int tomoyo_path_symlink(const struct path *parent, struct dentry *dentry,
			       const char *old_name)
{
	return static_call(tomoyo_path_symlink)(parent, dentry, old_name);
}

static int tomoyo_path_mknod(const struct path *parent, struct dentry *dentry,
			     umode_t mode, unsigned int dev)
{
	return static_call(tomoyo_path_mknod)(parent, dentry, mode, dev);
}

static int tomoyo_path_link(struct dentry *old_dentry, const struct path *new_dir,
			    struct dentry *new_dentry)
{
	return static_call(tomoyo_path_link)(old_dentry, new_dir, new_dentry);
}

static int tomoyo_path_rename(const struct path *old_parent, struct dentry *old_dentry,
			      const struct path *new_parent, struct dentry *new_dentry,
			      const unsigned int flags)
{
	return static_call(tomoyo_path_rename)(old_parent, old_dentry, new_parent, new_dentry, flags);
}

static int tomoyo_file_fcntl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return static_call(tomoyo_file_fcntl)(file, cmd, arg);
}

static int tomoyo_file_open(struct file *f)
{
	return static_call(tomoyo_file_open)(f);
}

static int tomoyo_file_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return static_call(tomoyo_file_ioctl)(file, cmd, arg);
}

static int tomoyo_path_chmod(const struct path *path, umode_t mode)
{
	return static_call(tomoyo_path_chmod)(path, mode);
}

static int tomoyo_path_chown(const struct path *path, kuid_t uid, kgid_t gid)
{
	return static_call(tomoyo_path_chown)(path, uid, gid);
}

static int tomoyo_path_chroot(const struct path *path)
{
	return static_call(tomoyo_path_chroot)(path);
}

static int tomoyo_sb_mount(const char *dev_name, const struct path *path,
			   const char *type, unsigned long flags, void *data)
{
	return static_call(tomoyo_sb_mount)(dev_name, path, type, flags, data);
}

static int tomoyo_sb_umount(struct vfsmount *mnt, int flags)
{
	return static_call(tomoyo_sb_umount)(mnt, flags);
}

static int tomoyo_sb_pivotroot(const struct path *old_path, const struct path *new_path)
{
	return static_call(tomoyo_sb_pivotroot)(old_path, new_path);
}

static int tomoyo_socket_listen(struct socket *sock, int backlog)
{
	return static_call(tomoyo_socket_listen)(sock, backlog);
}

static int tomoyo_socket_connect(struct socket *sock, struct sockaddr *addr, int addr_len)
{
	return static_call(tomoyo_socket_connect)(sock, addr, addr_len);
}

static int tomoyo_socket_bind(struct socket *sock, struct sockaddr *addr, int addr_len)
{
	return static_call(tomoyo_socket_bind)(sock, addr, addr_len);
}

static int tomoyo_socket_sendmsg(struct socket *sock, struct msghdr *msg, int size)
{
	return static_call(tomoyo_socket_sendmsg)(sock, msg, size);
}

static int tomoyo_task_alloc(struct task_struct *task, unsigned long clone_flags)
{
	return static_call(tomoyo_task_alloc)(task, clone_flags);
}

static void tomoyo_task_free(struct task_struct *task)
{
	static_call(tomoyo_task_free)(task);
}

void tomoyo_register_hooks(const struct tomoyo_hooks *tomoyo_hooks)
{
	static void *registered;

	if (cmpxchg(&registered, NULL, &registered))
		panic("%s was called twice!\n", __func__);
	static_call_update(tomoyo_task_free, tomoyo_hooks->task_free);
	static_call_update(tomoyo_task_alloc, tomoyo_hooks->task_alloc);
	static_call_update(tomoyo_cred_prepare, tomoyo_hooks->cred_prepare);
	static_call_update(tomoyo_bprm_committed_creds, tomoyo_hooks->bprm_committed_creds);
	static_call_update(tomoyo_bprm_check_security, tomoyo_hooks->bprm_check_security);
	static_call_update(tomoyo_inode_getattr, tomoyo_hooks->inode_getattr);
	static_call_update(tomoyo_path_truncate, tomoyo_hooks->path_truncate);
	static_call_update(tomoyo_file_truncate, tomoyo_hooks->file_truncate);
	static_call_update(tomoyo_path_unlink, tomoyo_hooks->path_unlink);
	static_call_update(tomoyo_path_mkdir, tomoyo_hooks->path_mkdir);
	static_call_update(tomoyo_path_rmdir, tomoyo_hooks->path_rmdir);
	static_call_update(tomoyo_path_symlink, tomoyo_hooks->path_symlink);
	static_call_update(tomoyo_path_mknod, tomoyo_hooks->path_mknod);
	static_call_update(tomoyo_path_link, tomoyo_hooks->path_link);
	static_call_update(tomoyo_path_rename, tomoyo_hooks->path_rename);
	static_call_update(tomoyo_file_fcntl, tomoyo_hooks->file_fcntl);
	static_call_update(tomoyo_file_open, tomoyo_hooks->file_open);
	static_call_update(tomoyo_file_ioctl, tomoyo_hooks->file_ioctl);
	static_call_update(tomoyo_path_chmod, tomoyo_hooks->path_chmod);
	static_call_update(tomoyo_path_chown, tomoyo_hooks->path_chown);
	static_call_update(tomoyo_path_chroot, tomoyo_hooks->path_chroot);
	static_call_update(tomoyo_sb_mount, tomoyo_hooks->sb_mount);
	static_call_update(tomoyo_sb_umount, tomoyo_hooks->sb_umount);
	static_call_update(tomoyo_sb_pivotroot, tomoyo_hooks->sb_pivotroot);
	static_call_update(tomoyo_socket_listen, tomoyo_hooks->socket_listen);
	static_call_update(tomoyo_socket_connect, tomoyo_hooks->socket_connect);
	static_call_update(tomoyo_socket_bind, tomoyo_hooks->socket_bind);
	static_call_update(tomoyo_socket_sendmsg, tomoyo_hooks->socket_sendmsg);
}
EXPORT_SYMBOL_GPL(tomoyo_register_hooks);

/*
 * Temporary hack: functions needed by tomoyo.ko . This hack will be removed
 * after all functions are marked as EXPORT_STMBOL_GPL().
 */
#undef find_task_by_vpid
#undef find_task_by_pid_ns
#undef put_filesystem
#undef get_mm_exe_file
#undef d_absolute_path
const struct tomoyo_tmp_exports tomoyo_tmp_exports = {
	.find_task_by_vpid = find_task_by_vpid,
	.find_task_by_pid_ns = find_task_by_pid_ns,
	.put_filesystem = put_filesystem,
	.get_mm_exe_file = get_mm_exe_file,
	.d_absolute_path = d_absolute_path,
};
EXPORT_SYMBOL_GPL(tomoyo_tmp_exports);

#endif

#ifndef CONFIG_SECURITY_TOMOYO_OMIT_USERSPACE_LOADER
static int tomoyo_bprm_creds_for_exec(struct linux_binprm *bprm)
{
	/*
	 * Load policy if /sbin/tomoyo-init exists and /sbin/init is requested
	 * for the first time.
	 */
	if (!tomoyo_policy_loaded)
		tomoyo_load_policy(bprm->filename);
	return 0;
}
#endif

struct lsm_blob_sizes tomoyo_blob_sizes __ro_after_init = {
	.lbs_task = sizeof(struct tomoyo_task),
};

static const struct lsm_id tomoyo_lsmid = {
	.name = "tomoyo",
	.id = LSM_ID_TOMOYO,
};

/* tomoyo_hooks is used for registering TOMOYO. */
static struct security_hook_list tomoyo_hooks[] __ro_after_init = {
	LSM_HOOK_INIT(cred_prepare, tomoyo_cred_prepare),
	LSM_HOOK_INIT(bprm_committed_creds, tomoyo_bprm_committed_creds),
	LSM_HOOK_INIT(task_alloc, tomoyo_task_alloc),
	LSM_HOOK_INIT(task_free, tomoyo_task_free),
#ifndef CONFIG_SECURITY_TOMOYO_OMIT_USERSPACE_LOADER
	LSM_HOOK_INIT(bprm_creds_for_exec, tomoyo_bprm_creds_for_exec),
#endif
	LSM_HOOK_INIT(bprm_check_security, tomoyo_bprm_check_security),
	LSM_HOOK_INIT(file_fcntl, tomoyo_file_fcntl),
	LSM_HOOK_INIT(file_open, tomoyo_file_open),
	LSM_HOOK_INIT(file_truncate, tomoyo_file_truncate),
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
	LSM_HOOK_INIT(file_ioctl_compat, tomoyo_file_ioctl),
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

int tomoyo_enabled __ro_after_init = 1;

/* Has /sbin/init started? */
bool tomoyo_policy_loaded;

#ifdef CONFIG_SECURITY_TOMOYO_LKM
EXPORT_SYMBOL_GPL(tomoyo_blob_sizes);
EXPORT_SYMBOL_GPL(tomoyo_policy_loaded);

struct tomoyo_operations tomoyo_ops;
EXPORT_SYMBOL_GPL(tomoyo_ops);

/**
 * tomoyo_init - Reserve hooks for TOMOYO Linux.
 *
 * Returns 0.
 */
static int __init tomoyo_init(void)
{
	/* register ourselves with the security framework */
	security_add_hooks(tomoyo_hooks, ARRAY_SIZE(tomoyo_hooks), &tomoyo_lsmid);
	tomoyo_ops.enabled = tomoyo_enabled;
	pr_info("Hooks for initializing TOMOYO Linux are ready\n");
	return 0;
}
#else
/**
 * tomoyo_init - Register TOMOYO Linux as a LSM module.
 *
 * Returns 0.
 */
static int __init tomoyo_init(void)
{
	struct tomoyo_task *s = tomoyo_task(current);

	/* register ourselves with the security framework */
	security_add_hooks(tomoyo_hooks, ARRAY_SIZE(tomoyo_hooks),
			   &tomoyo_lsmid);
	pr_info("TOMOYO Linux initialized\n");
	s->domain_info = &tomoyo_kernel_domain;
	atomic_inc(&tomoyo_kernel_domain.users);
	s->old_domain_info = NULL;
	tomoyo_mm_init();

	return 0;
}
#endif

DEFINE_LSM(tomoyo) = {
	.name = "tomoyo",
	.enabled = &tomoyo_enabled,
	.flags = LSM_FLAG_LEGACY_MAJOR,
	.blobs = &tomoyo_blob_sizes,
	.init = tomoyo_init,
};
