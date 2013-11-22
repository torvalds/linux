/*
 * Security plug functions
 *
 * Copyright (C) 2001 WireX Communications, Inc <chris@wirex.com>
 * Copyright (C) 2001-2002 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (C) 2001 Networks Associates Technology, Inc <ssmalley@nai.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 */

#include <linux/capability.h>
#include <linux/dcache.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/security.h>
#include <linux/integrity.h>
#include <linux/ima.h>
#include <linux/evm.h>
#include <linux/fsnotify.h>
#include <linux/mman.h>
#include <linux/mount.h>
#include <linux/personality.h>
#include <linux/backing-dev.h>
#include <net/flow.h>

#define MAX_LSM_EVM_XATTR	2

/* Boot-time LSM user choice */
static __initdata char chosen_lsm[SECURITY_NAME_MAX + 1] =
	CONFIG_DEFAULT_SECURITY;

static struct security_operations *security_ops;
static struct security_operations default_security_ops = {
	.name	= "default",
};

static inline int __init verify(struct security_operations *ops)
{
	/* verify the security_operations structure exists */
	if (!ops)
		return -EINVAL;
	security_fixup_ops(ops);
	return 0;
}

static void __init do_security_initcalls(void)
{
	initcall_t *call;
	call = __security_initcall_start;
	while (call < __security_initcall_end) {
		(*call) ();
		call++;
	}
}

/**
 * security_init - initializes the security framework
 *
 * This should be called early in the kernel initialization sequence.
 */
int __init security_init(void)
{
	printk(KERN_INFO "Security Framework initialized\n");

	security_fixup_ops(&default_security_ops);
	security_ops = &default_security_ops;
	do_security_initcalls();

	return 0;
}

void reset_security_ops(void)
{
	security_ops = &default_security_ops;
}

/* Save user chosen LSM */
static int __init choose_lsm(char *str)
{
	strncpy(chosen_lsm, str, SECURITY_NAME_MAX);
	return 1;
}
__setup("security=", choose_lsm);

/**
 * security_module_enable - Load given security module on boot ?
 * @ops: a pointer to the struct security_operations that is to be checked.
 *
 * Each LSM must pass this method before registering its own operations
 * to avoid security registration races. This method may also be used
 * to check if your LSM is currently loaded during kernel initialization.
 *
 * Return true if:
 *	-The passed LSM is the one chosen by user at boot time,
 *	-or the passed LSM is configured as the default and the user did not
 *	 choose an alternate LSM at boot time.
 * Otherwise, return false.
 */
int __init security_module_enable(struct security_operations *ops)
{
	return !strcmp(ops->name, chosen_lsm);
}

/**
 * register_security - registers a security framework with the kernel
 * @ops: a pointer to the struct security_options that is to be registered
 *
 * This function allows a security module to register itself with the
 * kernel security subsystem.  Some rudimentary checking is done on the @ops
 * value passed to this function. You'll need to check first if your LSM
 * is allowed to register its @ops by calling security_module_enable(@ops).
 *
 * If there is already a security module registered with the kernel,
 * an error will be returned.  Otherwise %0 is returned on success.
 */
int __init register_security(struct security_operations *ops)
{
	if (verify(ops)) {
		printk(KERN_DEBUG "%s could not verify "
		       "security_operations structure.\n", __func__);
		return -EINVAL;
	}

	if (security_ops != &default_security_ops)
		return -EAGAIN;

	security_ops = ops;

	return 0;
}

/* Security operations */

int security_ptrace_access_check(struct task_struct *child, unsigned int mode)
{
#ifdef CONFIG_SECURITY_YAMA_STACKED
	int rc;
	rc = yama_ptrace_access_check(child, mode);
	if (rc)
		return rc;
#endif
	return security_ops->ptrace_access_check(child, mode);
}

int security_ptrace_traceme(struct task_struct *parent)
{
#ifdef CONFIG_SECURITY_YAMA_STACKED
	int rc;
	rc = yama_ptrace_traceme(parent);
	if (rc)
		return rc;
#endif
	return security_ops->ptrace_traceme(parent);
}

int security_capget(struct task_struct *target,
		     kernel_cap_t *effective,
		     kernel_cap_t *inheritable,
		     kernel_cap_t *permitted)
{
	return security_ops->capget(target, effective, inheritable, permitted);
}

int security_capset(struct cred *new, const struct cred *old,
		    const kernel_cap_t *effective,
		    const kernel_cap_t *inheritable,
		    const kernel_cap_t *permitted)
{
	return security_ops->capset(new, old,
				    effective, inheritable, permitted);
}

int security_capable(const struct cred *cred, struct user_namespace *ns,
		     int cap)
{
	return security_ops->capable(cred, ns, cap, SECURITY_CAP_AUDIT);
}

int security_capable_noaudit(const struct cred *cred, struct user_namespace *ns,
			     int cap)
{
	return security_ops->capable(cred, ns, cap, SECURITY_CAP_NOAUDIT);
}

int security_quotactl(int cmds, int type, int id, struct super_block *sb)
{
	return security_ops->quotactl(cmds, type, id, sb);
}

int security_quota_on(struct dentry *dentry)
{
	return security_ops->quota_on(dentry);
}

int security_syslog(int type)
{
	return security_ops->syslog(type);
}

int security_settime(const struct timespec *ts, const struct timezone *tz)
{
	return security_ops->settime(ts, tz);
}

int security_vm_enough_memory_mm(struct mm_struct *mm, long pages)
{
	return security_ops->vm_enough_memory(mm, pages);
}

int security_bprm_set_creds(struct linux_binprm *bprm)
{
	return security_ops->bprm_set_creds(bprm);
}

int security_bprm_check(struct linux_binprm *bprm)
{
	int ret;

	ret = security_ops->bprm_check_security(bprm);
	if (ret)
		return ret;
	return ima_bprm_check(bprm);
}

void security_bprm_committing_creds(struct linux_binprm *bprm)
{
	security_ops->bprm_committing_creds(bprm);
}

void security_bprm_committed_creds(struct linux_binprm *bprm)
{
	security_ops->bprm_committed_creds(bprm);
}

int security_bprm_secureexec(struct linux_binprm *bprm)
{
	return security_ops->bprm_secureexec(bprm);
}

int security_sb_alloc(struct super_block *sb)
{
	return security_ops->sb_alloc_security(sb);
}

void security_sb_free(struct super_block *sb)
{
	security_ops->sb_free_security(sb);
}

int security_sb_copy_data(char *orig, char *copy)
{
	return security_ops->sb_copy_data(orig, copy);
}
EXPORT_SYMBOL(security_sb_copy_data);

int security_sb_remount(struct super_block *sb, void *data)
{
	return security_ops->sb_remount(sb, data);
}

int security_sb_kern_mount(struct super_block *sb, int flags, void *data)
{
	return security_ops->sb_kern_mount(sb, flags, data);
}

int security_sb_show_options(struct seq_file *m, struct super_block *sb)
{
	return security_ops->sb_show_options(m, sb);
}

int security_sb_statfs(struct dentry *dentry)
{
	return security_ops->sb_statfs(dentry);
}

int security_sb_mount(const char *dev_name, struct path *path,
                       const char *type, unsigned long flags, void *data)
{
	return security_ops->sb_mount(dev_name, path, type, flags, data);
}

int security_sb_umount(struct vfsmount *mnt, int flags)
{
	return security_ops->sb_umount(mnt, flags);
}

int security_sb_pivotroot(struct path *old_path, struct path *new_path)
{
	return security_ops->sb_pivotroot(old_path, new_path);
}

int security_sb_set_mnt_opts(struct super_block *sb,
				struct security_mnt_opts *opts,
				unsigned long kern_flags,
				unsigned long *set_kern_flags)
{
	return security_ops->sb_set_mnt_opts(sb, opts, kern_flags,
						set_kern_flags);
}
EXPORT_SYMBOL(security_sb_set_mnt_opts);

int security_sb_clone_mnt_opts(const struct super_block *oldsb,
				struct super_block *newsb)
{
	return security_ops->sb_clone_mnt_opts(oldsb, newsb);
}
EXPORT_SYMBOL(security_sb_clone_mnt_opts);

int security_sb_parse_opts_str(char *options, struct security_mnt_opts *opts)
{
	return security_ops->sb_parse_opts_str(options, opts);
}
EXPORT_SYMBOL(security_sb_parse_opts_str);

int security_inode_alloc(struct inode *inode)
{
	inode->i_security = NULL;
	return security_ops->inode_alloc_security(inode);
}

void security_inode_free(struct inode *inode)
{
	integrity_inode_free(inode);
	security_ops->inode_free_security(inode);
}

int security_dentry_init_security(struct dentry *dentry, int mode,
					struct qstr *name, void **ctx,
					u32 *ctxlen)
{
	return security_ops->dentry_init_security(dentry, mode, name,
							ctx, ctxlen);
}
EXPORT_SYMBOL(security_dentry_init_security);

int security_inode_init_security(struct inode *inode, struct inode *dir,
				 const struct qstr *qstr,
				 const initxattrs initxattrs, void *fs_data)
{
	struct xattr new_xattrs[MAX_LSM_EVM_XATTR + 1];
	struct xattr *lsm_xattr, *evm_xattr, *xattr;
	int ret;

	if (unlikely(IS_PRIVATE(inode)))
		return 0;

	if (!initxattrs)
		return security_ops->inode_init_security(inode, dir, qstr,
							 NULL, NULL, NULL);
	memset(new_xattrs, 0, sizeof(new_xattrs));
	lsm_xattr = new_xattrs;
	ret = security_ops->inode_init_security(inode, dir, qstr,
						&lsm_xattr->name,
						&lsm_xattr->value,
						&lsm_xattr->value_len);
	if (ret)
		goto out;

	evm_xattr = lsm_xattr + 1;
	ret = evm_inode_init_security(inode, lsm_xattr, evm_xattr);
	if (ret)
		goto out;
	ret = initxattrs(inode, new_xattrs, fs_data);
out:
	for (xattr = new_xattrs; xattr->value != NULL; xattr++)
		kfree(xattr->value);
	return (ret == -EOPNOTSUPP) ? 0 : ret;
}
EXPORT_SYMBOL(security_inode_init_security);

int security_old_inode_init_security(struct inode *inode, struct inode *dir,
				     const struct qstr *qstr, const char **name,
				     void **value, size_t *len)
{
	if (unlikely(IS_PRIVATE(inode)))
		return -EOPNOTSUPP;
	return security_ops->inode_init_security(inode, dir, qstr, name, value,
						 len);
}
EXPORT_SYMBOL(security_old_inode_init_security);

#ifdef CONFIG_SECURITY_PATH
int security_path_mknod(struct path *dir, struct dentry *dentry, umode_t mode,
			unsigned int dev)
{
	if (unlikely(IS_PRIVATE(dir->dentry->d_inode)))
		return 0;
	return security_ops->path_mknod(dir, dentry, mode, dev);
}
EXPORT_SYMBOL(security_path_mknod);

int security_path_mkdir(struct path *dir, struct dentry *dentry, umode_t mode)
{
	if (unlikely(IS_PRIVATE(dir->dentry->d_inode)))
		return 0;
	return security_ops->path_mkdir(dir, dentry, mode);
}
EXPORT_SYMBOL(security_path_mkdir);

int security_path_rmdir(struct path *dir, struct dentry *dentry)
{
	if (unlikely(IS_PRIVATE(dir->dentry->d_inode)))
		return 0;
	return security_ops->path_rmdir(dir, dentry);
}

int security_path_unlink(struct path *dir, struct dentry *dentry)
{
	if (unlikely(IS_PRIVATE(dir->dentry->d_inode)))
		return 0;
	return security_ops->path_unlink(dir, dentry);
}
EXPORT_SYMBOL(security_path_unlink);

int security_path_symlink(struct path *dir, struct dentry *dentry,
			  const char *old_name)
{
	if (unlikely(IS_PRIVATE(dir->dentry->d_inode)))
		return 0;
	return security_ops->path_symlink(dir, dentry, old_name);
}

int security_path_link(struct dentry *old_dentry, struct path *new_dir,
		       struct dentry *new_dentry)
{
	if (unlikely(IS_PRIVATE(old_dentry->d_inode)))
		return 0;
	return security_ops->path_link(old_dentry, new_dir, new_dentry);
}

int security_path_rename(struct path *old_dir, struct dentry *old_dentry,
			 struct path *new_dir, struct dentry *new_dentry)
{
	if (unlikely(IS_PRIVATE(old_dentry->d_inode) ||
		     (new_dentry->d_inode && IS_PRIVATE(new_dentry->d_inode))))
		return 0;
	return security_ops->path_rename(old_dir, old_dentry, new_dir,
					 new_dentry);
}
EXPORT_SYMBOL(security_path_rename);

int security_path_truncate(struct path *path)
{
	if (unlikely(IS_PRIVATE(path->dentry->d_inode)))
		return 0;
	return security_ops->path_truncate(path);
}

int security_path_chmod(struct path *path, umode_t mode)
{
	if (unlikely(IS_PRIVATE(path->dentry->d_inode)))
		return 0;
	return security_ops->path_chmod(path, mode);
}

int security_path_chown(struct path *path, kuid_t uid, kgid_t gid)
{
	if (unlikely(IS_PRIVATE(path->dentry->d_inode)))
		return 0;
	return security_ops->path_chown(path, uid, gid);
}

int security_path_chroot(struct path *path)
{
	return security_ops->path_chroot(path);
}
#endif

int security_inode_create(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	if (unlikely(IS_PRIVATE(dir)))
		return 0;
	return security_ops->inode_create(dir, dentry, mode);
}
EXPORT_SYMBOL_GPL(security_inode_create);

int security_inode_link(struct dentry *old_dentry, struct inode *dir,
			 struct dentry *new_dentry)
{
	if (unlikely(IS_PRIVATE(old_dentry->d_inode)))
		return 0;
	return security_ops->inode_link(old_dentry, dir, new_dentry);
}

int security_inode_unlink(struct inode *dir, struct dentry *dentry)
{
	if (unlikely(IS_PRIVATE(dentry->d_inode)))
		return 0;
	return security_ops->inode_unlink(dir, dentry);
}

int security_inode_symlink(struct inode *dir, struct dentry *dentry,
			    const char *old_name)
{
	if (unlikely(IS_PRIVATE(dir)))
		return 0;
	return security_ops->inode_symlink(dir, dentry, old_name);
}

int security_inode_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	if (unlikely(IS_PRIVATE(dir)))
		return 0;
	return security_ops->inode_mkdir(dir, dentry, mode);
}
EXPORT_SYMBOL_GPL(security_inode_mkdir);

int security_inode_rmdir(struct inode *dir, struct dentry *dentry)
{
	if (unlikely(IS_PRIVATE(dentry->d_inode)))
		return 0;
	return security_ops->inode_rmdir(dir, dentry);
}

int security_inode_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev)
{
	if (unlikely(IS_PRIVATE(dir)))
		return 0;
	return security_ops->inode_mknod(dir, dentry, mode, dev);
}

int security_inode_rename(struct inode *old_dir, struct dentry *old_dentry,
			   struct inode *new_dir, struct dentry *new_dentry)
{
        if (unlikely(IS_PRIVATE(old_dentry->d_inode) ||
            (new_dentry->d_inode && IS_PRIVATE(new_dentry->d_inode))))
		return 0;
	return security_ops->inode_rename(old_dir, old_dentry,
					   new_dir, new_dentry);
}

int security_inode_readlink(struct dentry *dentry)
{
	if (unlikely(IS_PRIVATE(dentry->d_inode)))
		return 0;
	return security_ops->inode_readlink(dentry);
}

int security_inode_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	if (unlikely(IS_PRIVATE(dentry->d_inode)))
		return 0;
	return security_ops->inode_follow_link(dentry, nd);
}

int security_inode_permission(struct inode *inode, int mask)
{
	if (unlikely(IS_PRIVATE(inode)))
		return 0;
	return security_ops->inode_permission(inode, mask);
}

int security_inode_setattr(struct dentry *dentry, struct iattr *attr)
{
	int ret;

	if (unlikely(IS_PRIVATE(dentry->d_inode)))
		return 0;
	ret = security_ops->inode_setattr(dentry, attr);
	if (ret)
		return ret;
	return evm_inode_setattr(dentry, attr);
}
EXPORT_SYMBOL_GPL(security_inode_setattr);

int security_inode_getattr(struct vfsmount *mnt, struct dentry *dentry)
{
	if (unlikely(IS_PRIVATE(dentry->d_inode)))
		return 0;
	return security_ops->inode_getattr(mnt, dentry);
}

int security_inode_setxattr(struct dentry *dentry, const char *name,
			    const void *value, size_t size, int flags)
{
	int ret;

	if (unlikely(IS_PRIVATE(dentry->d_inode)))
		return 0;
	ret = security_ops->inode_setxattr(dentry, name, value, size, flags);
	if (ret)
		return ret;
	ret = ima_inode_setxattr(dentry, name, value, size);
	if (ret)
		return ret;
	return evm_inode_setxattr(dentry, name, value, size);
}

void security_inode_post_setxattr(struct dentry *dentry, const char *name,
				  const void *value, size_t size, int flags)
{
	if (unlikely(IS_PRIVATE(dentry->d_inode)))
		return;
	security_ops->inode_post_setxattr(dentry, name, value, size, flags);
	evm_inode_post_setxattr(dentry, name, value, size);
}

int security_inode_getxattr(struct dentry *dentry, const char *name)
{
	if (unlikely(IS_PRIVATE(dentry->d_inode)))
		return 0;
	return security_ops->inode_getxattr(dentry, name);
}

int security_inode_listxattr(struct dentry *dentry)
{
	if (unlikely(IS_PRIVATE(dentry->d_inode)))
		return 0;
	return security_ops->inode_listxattr(dentry);
}

int security_inode_removexattr(struct dentry *dentry, const char *name)
{
	int ret;

	if (unlikely(IS_PRIVATE(dentry->d_inode)))
		return 0;
	ret = security_ops->inode_removexattr(dentry, name);
	if (ret)
		return ret;
	ret = ima_inode_removexattr(dentry, name);
	if (ret)
		return ret;
	return evm_inode_removexattr(dentry, name);
}

int security_inode_need_killpriv(struct dentry *dentry)
{
	return security_ops->inode_need_killpriv(dentry);
}

int security_inode_killpriv(struct dentry *dentry)
{
	return security_ops->inode_killpriv(dentry);
}

int security_inode_getsecurity(const struct inode *inode, const char *name, void **buffer, bool alloc)
{
	if (unlikely(IS_PRIVATE(inode)))
		return -EOPNOTSUPP;
	return security_ops->inode_getsecurity(inode, name, buffer, alloc);
}

int security_inode_setsecurity(struct inode *inode, const char *name, const void *value, size_t size, int flags)
{
	if (unlikely(IS_PRIVATE(inode)))
		return -EOPNOTSUPP;
	return security_ops->inode_setsecurity(inode, name, value, size, flags);
}

int security_inode_listsecurity(struct inode *inode, char *buffer, size_t buffer_size)
{
	if (unlikely(IS_PRIVATE(inode)))
		return 0;
	return security_ops->inode_listsecurity(inode, buffer, buffer_size);
}
EXPORT_SYMBOL(security_inode_listsecurity);

void security_inode_getsecid(const struct inode *inode, u32 *secid)
{
	security_ops->inode_getsecid(inode, secid);
}

int security_file_permission(struct file *file, int mask)
{
	int ret;

	ret = security_ops->file_permission(file, mask);
	if (ret)
		return ret;

	return fsnotify_perm(file, mask);
}

int security_file_alloc(struct file *file)
{
	return security_ops->file_alloc_security(file);
}

void security_file_free(struct file *file)
{
	security_ops->file_free_security(file);
}

int security_file_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return security_ops->file_ioctl(file, cmd, arg);
}

static inline unsigned long mmap_prot(struct file *file, unsigned long prot)
{
	/*
	 * Does we have PROT_READ and does the application expect
	 * it to imply PROT_EXEC?  If not, nothing to talk about...
	 */
	if ((prot & (PROT_READ | PROT_EXEC)) != PROT_READ)
		return prot;
	if (!(current->personality & READ_IMPLIES_EXEC))
		return prot;
	/*
	 * if that's an anonymous mapping, let it.
	 */
	if (!file)
		return prot | PROT_EXEC;
	/*
	 * ditto if it's not on noexec mount, except that on !MMU we need
	 * BDI_CAP_EXEC_MMAP (== VM_MAYEXEC) in this case
	 */
	if (!(file->f_path.mnt->mnt_flags & MNT_NOEXEC)) {
#ifndef CONFIG_MMU
		unsigned long caps = 0;
		struct address_space *mapping = file->f_mapping;
		if (mapping && mapping->backing_dev_info)
			caps = mapping->backing_dev_info->capabilities;
		if (!(caps & BDI_CAP_EXEC_MAP))
			return prot;
#endif
		return prot | PROT_EXEC;
	}
	/* anything on noexec mount won't get PROT_EXEC */
	return prot;
}

int security_mmap_file(struct file *file, unsigned long prot,
			unsigned long flags)
{
	int ret;
	ret = security_ops->mmap_file(file, prot,
					mmap_prot(file, prot), flags);
	if (ret)
		return ret;
	return ima_file_mmap(file, prot);
}

int security_mmap_addr(unsigned long addr)
{
	return security_ops->mmap_addr(addr);
}

int security_file_mprotect(struct vm_area_struct *vma, unsigned long reqprot,
			    unsigned long prot)
{
	return security_ops->file_mprotect(vma, reqprot, prot);
}

int security_file_lock(struct file *file, unsigned int cmd)
{
	return security_ops->file_lock(file, cmd);
}

int security_file_fcntl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return security_ops->file_fcntl(file, cmd, arg);
}

int security_file_set_fowner(struct file *file)
{
	return security_ops->file_set_fowner(file);
}

int security_file_send_sigiotask(struct task_struct *tsk,
				  struct fown_struct *fown, int sig)
{
	return security_ops->file_send_sigiotask(tsk, fown, sig);
}

int security_file_receive(struct file *file)
{
	return security_ops->file_receive(file);
}

int security_file_open(struct file *file, const struct cred *cred)
{
	int ret;

	ret = security_ops->file_open(file, cred);
	if (ret)
		return ret;

	return fsnotify_perm(file, MAY_OPEN);
}

int security_task_create(unsigned long clone_flags)
{
	return security_ops->task_create(clone_flags);
}

void security_task_free(struct task_struct *task)
{
#ifdef CONFIG_SECURITY_YAMA_STACKED
	yama_task_free(task);
#endif
	security_ops->task_free(task);
}

int security_cred_alloc_blank(struct cred *cred, gfp_t gfp)
{
	return security_ops->cred_alloc_blank(cred, gfp);
}

void security_cred_free(struct cred *cred)
{
	security_ops->cred_free(cred);
}

int security_prepare_creds(struct cred *new, const struct cred *old, gfp_t gfp)
{
	return security_ops->cred_prepare(new, old, gfp);
}

void security_transfer_creds(struct cred *new, const struct cred *old)
{
	security_ops->cred_transfer(new, old);
}

int security_kernel_act_as(struct cred *new, u32 secid)
{
	return security_ops->kernel_act_as(new, secid);
}

int security_kernel_create_files_as(struct cred *new, struct inode *inode)
{
	return security_ops->kernel_create_files_as(new, inode);
}

int security_kernel_module_request(char *kmod_name)
{
	return security_ops->kernel_module_request(kmod_name);
}

int security_kernel_module_from_file(struct file *file)
{
	int ret;

	ret = security_ops->kernel_module_from_file(file);
	if (ret)
		return ret;
	return ima_module_check(file);
}

int security_task_fix_setuid(struct cred *new, const struct cred *old,
			     int flags)
{
	return security_ops->task_fix_setuid(new, old, flags);
}

int security_task_setpgid(struct task_struct *p, pid_t pgid)
{
	return security_ops->task_setpgid(p, pgid);
}

int security_task_getpgid(struct task_struct *p)
{
	return security_ops->task_getpgid(p);
}

int security_task_getsid(struct task_struct *p)
{
	return security_ops->task_getsid(p);
}

void security_task_getsecid(struct task_struct *p, u32 *secid)
{
	security_ops->task_getsecid(p, secid);
}
EXPORT_SYMBOL(security_task_getsecid);

int security_task_setnice(struct task_struct *p, int nice)
{
	return security_ops->task_setnice(p, nice);
}

int security_task_setioprio(struct task_struct *p, int ioprio)
{
	return security_ops->task_setioprio(p, ioprio);
}

int security_task_getioprio(struct task_struct *p)
{
	return security_ops->task_getioprio(p);
}

int security_task_setrlimit(struct task_struct *p, unsigned int resource,
		struct rlimit *new_rlim)
{
	return security_ops->task_setrlimit(p, resource, new_rlim);
}

int security_task_setscheduler(struct task_struct *p)
{
	return security_ops->task_setscheduler(p);
}

int security_task_getscheduler(struct task_struct *p)
{
	return security_ops->task_getscheduler(p);
}

int security_task_movememory(struct task_struct *p)
{
	return security_ops->task_movememory(p);
}

int security_task_kill(struct task_struct *p, struct siginfo *info,
			int sig, u32 secid)
{
	return security_ops->task_kill(p, info, sig, secid);
}

int security_task_wait(struct task_struct *p)
{
	return security_ops->task_wait(p);
}

int security_task_prctl(int option, unsigned long arg2, unsigned long arg3,
			 unsigned long arg4, unsigned long arg5)
{
#ifdef CONFIG_SECURITY_YAMA_STACKED
	int rc;
	rc = yama_task_prctl(option, arg2, arg3, arg4, arg5);
	if (rc != -ENOSYS)
		return rc;
#endif
	return security_ops->task_prctl(option, arg2, arg3, arg4, arg5);
}

void security_task_to_inode(struct task_struct *p, struct inode *inode)
{
	security_ops->task_to_inode(p, inode);
}

int security_ipc_permission(struct kern_ipc_perm *ipcp, short flag)
{
	return security_ops->ipc_permission(ipcp, flag);
}

void security_ipc_getsecid(struct kern_ipc_perm *ipcp, u32 *secid)
{
	security_ops->ipc_getsecid(ipcp, secid);
}

int security_msg_msg_alloc(struct msg_msg *msg)
{
	return security_ops->msg_msg_alloc_security(msg);
}

void security_msg_msg_free(struct msg_msg *msg)
{
	security_ops->msg_msg_free_security(msg);
}

int security_msg_queue_alloc(struct msg_queue *msq)
{
	return security_ops->msg_queue_alloc_security(msq);
}

void security_msg_queue_free(struct msg_queue *msq)
{
	security_ops->msg_queue_free_security(msq);
}

int security_msg_queue_associate(struct msg_queue *msq, int msqflg)
{
	return security_ops->msg_queue_associate(msq, msqflg);
}

int security_msg_queue_msgctl(struct msg_queue *msq, int cmd)
{
	return security_ops->msg_queue_msgctl(msq, cmd);
}

int security_msg_queue_msgsnd(struct msg_queue *msq,
			       struct msg_msg *msg, int msqflg)
{
	return security_ops->msg_queue_msgsnd(msq, msg, msqflg);
}

int security_msg_queue_msgrcv(struct msg_queue *msq, struct msg_msg *msg,
			       struct task_struct *target, long type, int mode)
{
	return security_ops->msg_queue_msgrcv(msq, msg, target, type, mode);
}

int security_shm_alloc(struct shmid_kernel *shp)
{
	return security_ops->shm_alloc_security(shp);
}

void security_shm_free(struct shmid_kernel *shp)
{
	security_ops->shm_free_security(shp);
}

int security_shm_associate(struct shmid_kernel *shp, int shmflg)
{
	return security_ops->shm_associate(shp, shmflg);
}

int security_shm_shmctl(struct shmid_kernel *shp, int cmd)
{
	return security_ops->shm_shmctl(shp, cmd);
}

int security_shm_shmat(struct shmid_kernel *shp, char __user *shmaddr, int shmflg)
{
	return security_ops->shm_shmat(shp, shmaddr, shmflg);
}

int security_sem_alloc(struct sem_array *sma)
{
	return security_ops->sem_alloc_security(sma);
}

void security_sem_free(struct sem_array *sma)
{
	security_ops->sem_free_security(sma);
}

int security_sem_associate(struct sem_array *sma, int semflg)
{
	return security_ops->sem_associate(sma, semflg);
}

int security_sem_semctl(struct sem_array *sma, int cmd)
{
	return security_ops->sem_semctl(sma, cmd);
}

int security_sem_semop(struct sem_array *sma, struct sembuf *sops,
			unsigned nsops, int alter)
{
	return security_ops->sem_semop(sma, sops, nsops, alter);
}

void security_d_instantiate(struct dentry *dentry, struct inode *inode)
{
	if (unlikely(inode && IS_PRIVATE(inode)))
		return;
	security_ops->d_instantiate(dentry, inode);
}
EXPORT_SYMBOL(security_d_instantiate);

int security_getprocattr(struct task_struct *p, char *name, char **value)
{
	return security_ops->getprocattr(p, name, value);
}

int security_setprocattr(struct task_struct *p, char *name, void *value, size_t size)
{
	return security_ops->setprocattr(p, name, value, size);
}

int security_netlink_send(struct sock *sk, struct sk_buff *skb)
{
	return security_ops->netlink_send(sk, skb);
}

int security_ismaclabel(const char *name)
{
	return security_ops->ismaclabel(name);
}
EXPORT_SYMBOL(security_ismaclabel);

int security_secid_to_secctx(u32 secid, char **secdata, u32 *seclen)
{
	return security_ops->secid_to_secctx(secid, secdata, seclen);
}
EXPORT_SYMBOL(security_secid_to_secctx);

int security_secctx_to_secid(const char *secdata, u32 seclen, u32 *secid)
{
	return security_ops->secctx_to_secid(secdata, seclen, secid);
}
EXPORT_SYMBOL(security_secctx_to_secid);

void security_release_secctx(char *secdata, u32 seclen)
{
	security_ops->release_secctx(secdata, seclen);
}
EXPORT_SYMBOL(security_release_secctx);

int security_inode_notifysecctx(struct inode *inode, void *ctx, u32 ctxlen)
{
	return security_ops->inode_notifysecctx(inode, ctx, ctxlen);
}
EXPORT_SYMBOL(security_inode_notifysecctx);

int security_inode_setsecctx(struct dentry *dentry, void *ctx, u32 ctxlen)
{
	return security_ops->inode_setsecctx(dentry, ctx, ctxlen);
}
EXPORT_SYMBOL(security_inode_setsecctx);

int security_inode_getsecctx(struct inode *inode, void **ctx, u32 *ctxlen)
{
	return security_ops->inode_getsecctx(inode, ctx, ctxlen);
}
EXPORT_SYMBOL(security_inode_getsecctx);

#ifdef CONFIG_SECURITY_NETWORK

int security_unix_stream_connect(struct sock *sock, struct sock *other, struct sock *newsk)
{
	return security_ops->unix_stream_connect(sock, other, newsk);
}
EXPORT_SYMBOL(security_unix_stream_connect);

int security_unix_may_send(struct socket *sock,  struct socket *other)
{
	return security_ops->unix_may_send(sock, other);
}
EXPORT_SYMBOL(security_unix_may_send);

int security_socket_create(int family, int type, int protocol, int kern)
{
	return security_ops->socket_create(family, type, protocol, kern);
}

int security_socket_post_create(struct socket *sock, int family,
				int type, int protocol, int kern)
{
	return security_ops->socket_post_create(sock, family, type,
						protocol, kern);
}

int security_socket_bind(struct socket *sock, struct sockaddr *address, int addrlen)
{
	return security_ops->socket_bind(sock, address, addrlen);
}

int security_socket_connect(struct socket *sock, struct sockaddr *address, int addrlen)
{
	return security_ops->socket_connect(sock, address, addrlen);
}

int security_socket_listen(struct socket *sock, int backlog)
{
	return security_ops->socket_listen(sock, backlog);
}

int security_socket_accept(struct socket *sock, struct socket *newsock)
{
	return security_ops->socket_accept(sock, newsock);
}

int security_socket_sendmsg(struct socket *sock, struct msghdr *msg, int size)
{
	return security_ops->socket_sendmsg(sock, msg, size);
}

int security_socket_recvmsg(struct socket *sock, struct msghdr *msg,
			    int size, int flags)
{
	return security_ops->socket_recvmsg(sock, msg, size, flags);
}

int security_socket_getsockname(struct socket *sock)
{
	return security_ops->socket_getsockname(sock);
}

int security_socket_getpeername(struct socket *sock)
{
	return security_ops->socket_getpeername(sock);
}

int security_socket_getsockopt(struct socket *sock, int level, int optname)
{
	return security_ops->socket_getsockopt(sock, level, optname);
}

int security_socket_setsockopt(struct socket *sock, int level, int optname)
{
	return security_ops->socket_setsockopt(sock, level, optname);
}

int security_socket_shutdown(struct socket *sock, int how)
{
	return security_ops->socket_shutdown(sock, how);
}

int security_sock_rcv_skb(struct sock *sk, struct sk_buff *skb)
{
	return security_ops->socket_sock_rcv_skb(sk, skb);
}
EXPORT_SYMBOL(security_sock_rcv_skb);

int security_socket_getpeersec_stream(struct socket *sock, char __user *optval,
				      int __user *optlen, unsigned len)
{
	return security_ops->socket_getpeersec_stream(sock, optval, optlen, len);
}

int security_socket_getpeersec_dgram(struct socket *sock, struct sk_buff *skb, u32 *secid)
{
	return security_ops->socket_getpeersec_dgram(sock, skb, secid);
}
EXPORT_SYMBOL(security_socket_getpeersec_dgram);

int security_sk_alloc(struct sock *sk, int family, gfp_t priority)
{
	return security_ops->sk_alloc_security(sk, family, priority);
}

void security_sk_free(struct sock *sk)
{
	security_ops->sk_free_security(sk);
}

void security_sk_clone(const struct sock *sk, struct sock *newsk)
{
	security_ops->sk_clone_security(sk, newsk);
}
EXPORT_SYMBOL(security_sk_clone);

void security_sk_classify_flow(struct sock *sk, struct flowi *fl)
{
	security_ops->sk_getsecid(sk, &fl->flowi_secid);
}
EXPORT_SYMBOL(security_sk_classify_flow);

void security_req_classify_flow(const struct request_sock *req, struct flowi *fl)
{
	security_ops->req_classify_flow(req, fl);
}
EXPORT_SYMBOL(security_req_classify_flow);

void security_sock_graft(struct sock *sk, struct socket *parent)
{
	security_ops->sock_graft(sk, parent);
}
EXPORT_SYMBOL(security_sock_graft);

int security_inet_conn_request(struct sock *sk,
			struct sk_buff *skb, struct request_sock *req)
{
	return security_ops->inet_conn_request(sk, skb, req);
}
EXPORT_SYMBOL(security_inet_conn_request);

void security_inet_csk_clone(struct sock *newsk,
			const struct request_sock *req)
{
	security_ops->inet_csk_clone(newsk, req);
}

void security_inet_conn_established(struct sock *sk,
			struct sk_buff *skb)
{
	security_ops->inet_conn_established(sk, skb);
}

int security_secmark_relabel_packet(u32 secid)
{
	return security_ops->secmark_relabel_packet(secid);
}
EXPORT_SYMBOL(security_secmark_relabel_packet);

void security_secmark_refcount_inc(void)
{
	security_ops->secmark_refcount_inc();
}
EXPORT_SYMBOL(security_secmark_refcount_inc);

void security_secmark_refcount_dec(void)
{
	security_ops->secmark_refcount_dec();
}
EXPORT_SYMBOL(security_secmark_refcount_dec);

int security_tun_dev_alloc_security(void **security)
{
	return security_ops->tun_dev_alloc_security(security);
}
EXPORT_SYMBOL(security_tun_dev_alloc_security);

void security_tun_dev_free_security(void *security)
{
	security_ops->tun_dev_free_security(security);
}
EXPORT_SYMBOL(security_tun_dev_free_security);

int security_tun_dev_create(void)
{
	return security_ops->tun_dev_create();
}
EXPORT_SYMBOL(security_tun_dev_create);

int security_tun_dev_attach_queue(void *security)
{
	return security_ops->tun_dev_attach_queue(security);
}
EXPORT_SYMBOL(security_tun_dev_attach_queue);

int security_tun_dev_attach(struct sock *sk, void *security)
{
	return security_ops->tun_dev_attach(sk, security);
}
EXPORT_SYMBOL(security_tun_dev_attach);

int security_tun_dev_open(void *security)
{
	return security_ops->tun_dev_open(security);
}
EXPORT_SYMBOL(security_tun_dev_open);

void security_skb_owned_by(struct sk_buff *skb, struct sock *sk)
{
	security_ops->skb_owned_by(skb, sk);
}

#endif	/* CONFIG_SECURITY_NETWORK */

#ifdef CONFIG_SECURITY_NETWORK_XFRM

int security_xfrm_policy_alloc(struct xfrm_sec_ctx **ctxp, struct xfrm_user_sec_ctx *sec_ctx)
{
	return security_ops->xfrm_policy_alloc_security(ctxp, sec_ctx);
}
EXPORT_SYMBOL(security_xfrm_policy_alloc);

int security_xfrm_policy_clone(struct xfrm_sec_ctx *old_ctx,
			      struct xfrm_sec_ctx **new_ctxp)
{
	return security_ops->xfrm_policy_clone_security(old_ctx, new_ctxp);
}

void security_xfrm_policy_free(struct xfrm_sec_ctx *ctx)
{
	security_ops->xfrm_policy_free_security(ctx);
}
EXPORT_SYMBOL(security_xfrm_policy_free);

int security_xfrm_policy_delete(struct xfrm_sec_ctx *ctx)
{
	return security_ops->xfrm_policy_delete_security(ctx);
}

int security_xfrm_state_alloc(struct xfrm_state *x,
			      struct xfrm_user_sec_ctx *sec_ctx)
{
	return security_ops->xfrm_state_alloc(x, sec_ctx);
}
EXPORT_SYMBOL(security_xfrm_state_alloc);

int security_xfrm_state_alloc_acquire(struct xfrm_state *x,
				      struct xfrm_sec_ctx *polsec, u32 secid)
{
	return security_ops->xfrm_state_alloc_acquire(x, polsec, secid);
}

int security_xfrm_state_delete(struct xfrm_state *x)
{
	return security_ops->xfrm_state_delete_security(x);
}
EXPORT_SYMBOL(security_xfrm_state_delete);

void security_xfrm_state_free(struct xfrm_state *x)
{
	security_ops->xfrm_state_free_security(x);
}

int security_xfrm_policy_lookup(struct xfrm_sec_ctx *ctx, u32 fl_secid, u8 dir)
{
	return security_ops->xfrm_policy_lookup(ctx, fl_secid, dir);
}

int security_xfrm_state_pol_flow_match(struct xfrm_state *x,
				       struct xfrm_policy *xp,
				       const struct flowi *fl)
{
	return security_ops->xfrm_state_pol_flow_match(x, xp, fl);
}

int security_xfrm_decode_session(struct sk_buff *skb, u32 *secid)
{
	return security_ops->xfrm_decode_session(skb, secid, 1);
}

void security_skb_classify_flow(struct sk_buff *skb, struct flowi *fl)
{
	int rc = security_ops->xfrm_decode_session(skb, &fl->flowi_secid, 0);

	BUG_ON(rc);
}
EXPORT_SYMBOL(security_skb_classify_flow);

#endif	/* CONFIG_SECURITY_NETWORK_XFRM */

#ifdef CONFIG_KEYS

int security_key_alloc(struct key *key, const struct cred *cred,
		       unsigned long flags)
{
	return security_ops->key_alloc(key, cred, flags);
}

void security_key_free(struct key *key)
{
	security_ops->key_free(key);
}

int security_key_permission(key_ref_t key_ref,
			    const struct cred *cred, key_perm_t perm)
{
	return security_ops->key_permission(key_ref, cred, perm);
}

int security_key_getsecurity(struct key *key, char **_buffer)
{
	return security_ops->key_getsecurity(key, _buffer);
}

#endif	/* CONFIG_KEYS */

#ifdef CONFIG_AUDIT

int security_audit_rule_init(u32 field, u32 op, char *rulestr, void **lsmrule)
{
	return security_ops->audit_rule_init(field, op, rulestr, lsmrule);
}

int security_audit_rule_known(struct audit_krule *krule)
{
	return security_ops->audit_rule_known(krule);
}

void security_audit_rule_free(void *lsmrule)
{
	security_ops->audit_rule_free(lsmrule);
}

int security_audit_rule_match(u32 secid, u32 field, u32 op, void *lsmrule,
			      struct audit_context *actx)
{
	return security_ops->audit_rule_match(secid, field, op, lsmrule, actx);
}

#endif /* CONFIG_AUDIT */
