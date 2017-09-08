/*
 * Security plug functions
 *
 * Copyright (C) 2001 WireX Communications, Inc <chris@wirex.com>
 * Copyright (C) 2001-2002 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (C) 2001 Networks Associates Technology, Inc <ssmalley@nai.com>
 * Copyright (C) 2016 Mellanox Technologies
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 */

#include <linux/bpf.h>
#include <linux/capability.h>
#include <linux/dcache.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/lsm_hooks.h>
#include <linux/integrity.h>
#include <linux/ima.h>
#include <linux/evm.h>
#include <linux/fsnotify.h>
#include <linux/mman.h>
#include <linux/mount.h>
#include <linux/personality.h>
#include <linux/backing-dev.h>
#include <linux/string.h>
#include <net/flow.h>

#define MAX_LSM_EVM_XATTR	2

/* Maximum number of letters for an LSM name string */
#define SECURITY_NAME_MAX	10

struct security_hook_heads security_hook_heads __lsm_ro_after_init;
static ATOMIC_NOTIFIER_HEAD(lsm_notifier_chain);

char *lsm_names;
/* Boot-time LSM user choice */
static __initdata char chosen_lsm[SECURITY_NAME_MAX + 1] =
	CONFIG_DEFAULT_SECURITY;

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
	int i;
	struct list_head *list = (struct list_head *) &security_hook_heads;

	for (i = 0; i < sizeof(security_hook_heads) / sizeof(struct list_head);
	     i++)
		INIT_LIST_HEAD(&list[i]);
	pr_info("Security Framework initialized\n");

	/*
	 * Load minor LSMs, with the capability module always first.
	 */
	capability_add_hooks();
	yama_add_hooks();
	loadpin_add_hooks();

	/*
	 * Load all the remaining security modules.
	 */
	do_security_initcalls();

	return 0;
}

/* Save user chosen LSM */
static int __init choose_lsm(char *str)
{
	strncpy(chosen_lsm, str, SECURITY_NAME_MAX);
	return 1;
}
__setup("security=", choose_lsm);

static bool match_last_lsm(const char *list, const char *lsm)
{
	const char *last;

	if (WARN_ON(!list || !lsm))
		return false;
	last = strrchr(list, ',');
	if (last)
		/* Pass the comma, strcmp() will check for '\0' */
		last++;
	else
		last = list;
	return !strcmp(last, lsm);
}

static int lsm_append(char *new, char **result)
{
	char *cp;

	if (*result == NULL) {
		*result = kstrdup(new, GFP_KERNEL);
	} else {
		/* Check if it is the last registered name */
		if (match_last_lsm(*result, new))
			return 0;
		cp = kasprintf(GFP_KERNEL, "%s,%s", *result, new);
		if (cp == NULL)
			return -ENOMEM;
		kfree(*result);
		*result = cp;
	}
	return 0;
}

/**
 * security_module_enable - Load given security module on boot ?
 * @module: the name of the module
 *
 * Each LSM must pass this method before registering its own operations
 * to avoid security registration races. This method may also be used
 * to check if your LSM is currently loaded during kernel initialization.
 *
 * Returns:
 *
 * true if:
 *
 * - The passed LSM is the one chosen by user at boot time,
 * - or the passed LSM is configured as the default and the user did not
 *   choose an alternate LSM at boot time.
 *
 * Otherwise, return false.
 */
int __init security_module_enable(const char *module)
{
	return !strcmp(module, chosen_lsm);
}

/**
 * security_add_hooks - Add a modules hooks to the hook lists.
 * @hooks: the hooks to add
 * @count: the number of hooks to add
 * @lsm: the name of the security module
 *
 * Each LSM has to register its hooks with the infrastructure.
 */
void __init security_add_hooks(struct security_hook_list *hooks, int count,
				char *lsm)
{
	int i;

	for (i = 0; i < count; i++) {
		hooks[i].lsm = lsm;
		list_add_tail_rcu(&hooks[i].list, hooks[i].head);
	}
	if (lsm_append(lsm, &lsm_names) < 0)
		panic("%s - Cannot get early memory.\n", __func__);
}

int call_lsm_notifier(enum lsm_event event, void *data)
{
	return atomic_notifier_call_chain(&lsm_notifier_chain, event, data);
}
EXPORT_SYMBOL(call_lsm_notifier);

int register_lsm_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&lsm_notifier_chain, nb);
}
EXPORT_SYMBOL(register_lsm_notifier);

int unregister_lsm_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&lsm_notifier_chain, nb);
}
EXPORT_SYMBOL(unregister_lsm_notifier);

/*
 * Hook list operation macros.
 *
 * call_void_hook:
 *	This is a hook that does not return a value.
 *
 * call_int_hook:
 *	This is a hook that returns a value.
 */

#define call_void_hook(FUNC, ...)				\
	do {							\
		struct security_hook_list *P;			\
								\
		list_for_each_entry(P, &security_hook_heads.FUNC, list)	\
			P->hook.FUNC(__VA_ARGS__);		\
	} while (0)

#define call_int_hook(FUNC, IRC, ...) ({			\
	int RC = IRC;						\
	do {							\
		struct security_hook_list *P;			\
								\
		list_for_each_entry(P, &security_hook_heads.FUNC, list) { \
			RC = P->hook.FUNC(__VA_ARGS__);		\
			if (RC != 0)				\
				break;				\
		}						\
	} while (0);						\
	RC;							\
})

/* Security operations */

int security_binder_set_context_mgr(struct task_struct *mgr)
{
	return call_int_hook(binder_set_context_mgr, 0, mgr);
}

int security_binder_transaction(struct task_struct *from,
				struct task_struct *to)
{
	return call_int_hook(binder_transaction, 0, from, to);
}

int security_binder_transfer_binder(struct task_struct *from,
				    struct task_struct *to)
{
	return call_int_hook(binder_transfer_binder, 0, from, to);
}

int security_binder_transfer_file(struct task_struct *from,
				  struct task_struct *to, struct file *file)
{
	return call_int_hook(binder_transfer_file, 0, from, to, file);
}

int security_ptrace_access_check(struct task_struct *child, unsigned int mode)
{
	return call_int_hook(ptrace_access_check, 0, child, mode);
}

int security_ptrace_traceme(struct task_struct *parent)
{
	return call_int_hook(ptrace_traceme, 0, parent);
}

int security_capget(struct task_struct *target,
		     kernel_cap_t *effective,
		     kernel_cap_t *inheritable,
		     kernel_cap_t *permitted)
{
	return call_int_hook(capget, 0, target,
				effective, inheritable, permitted);
}

int security_capset(struct cred *new, const struct cred *old,
		    const kernel_cap_t *effective,
		    const kernel_cap_t *inheritable,
		    const kernel_cap_t *permitted)
{
	return call_int_hook(capset, 0, new, old,
				effective, inheritable, permitted);
}

int security_capable(const struct cred *cred, struct user_namespace *ns,
		     int cap)
{
	return call_int_hook(capable, 0, cred, ns, cap, SECURITY_CAP_AUDIT);
}

int security_capable_noaudit(const struct cred *cred, struct user_namespace *ns,
			     int cap)
{
	return call_int_hook(capable, 0, cred, ns, cap, SECURITY_CAP_NOAUDIT);
}

int security_quotactl(int cmds, int type, int id, struct super_block *sb)
{
	return call_int_hook(quotactl, 0, cmds, type, id, sb);
}

int security_quota_on(struct dentry *dentry)
{
	return call_int_hook(quota_on, 0, dentry);
}

int security_syslog(int type)
{
	return call_int_hook(syslog, 0, type);
}

int security_settime64(const struct timespec64 *ts, const struct timezone *tz)
{
	return call_int_hook(settime, 0, ts, tz);
}

int security_vm_enough_memory_mm(struct mm_struct *mm, long pages)
{
	struct security_hook_list *hp;
	int cap_sys_admin = 1;
	int rc;

	/*
	 * The module will respond with a positive value if
	 * it thinks the __vm_enough_memory() call should be
	 * made with the cap_sys_admin set. If all of the modules
	 * agree that it should be set it will. If any module
	 * thinks it should not be set it won't.
	 */
	list_for_each_entry(hp, &security_hook_heads.vm_enough_memory, list) {
		rc = hp->hook.vm_enough_memory(mm, pages);
		if (rc <= 0) {
			cap_sys_admin = 0;
			break;
		}
	}
	return __vm_enough_memory(mm, pages, cap_sys_admin);
}

int security_bprm_set_creds(struct linux_binprm *bprm)
{
	return call_int_hook(bprm_set_creds, 0, bprm);
}

int security_bprm_check(struct linux_binprm *bprm)
{
	int ret;

	ret = call_int_hook(bprm_check_security, 0, bprm);
	if (ret)
		return ret;
	return ima_bprm_check(bprm);
}

void security_bprm_committing_creds(struct linux_binprm *bprm)
{
	call_void_hook(bprm_committing_creds, bprm);
}

void security_bprm_committed_creds(struct linux_binprm *bprm)
{
	call_void_hook(bprm_committed_creds, bprm);
}

int security_sb_alloc(struct super_block *sb)
{
	return call_int_hook(sb_alloc_security, 0, sb);
}

void security_sb_free(struct super_block *sb)
{
	call_void_hook(sb_free_security, sb);
}

int security_sb_copy_data(char *orig, char *copy)
{
	return call_int_hook(sb_copy_data, 0, orig, copy);
}
EXPORT_SYMBOL(security_sb_copy_data);

int security_sb_remount(struct super_block *sb, void *data)
{
	return call_int_hook(sb_remount, 0, sb, data);
}

int security_sb_kern_mount(struct super_block *sb, int flags, void *data)
{
	return call_int_hook(sb_kern_mount, 0, sb, flags, data);
}

int security_sb_show_options(struct seq_file *m, struct super_block *sb)
{
	return call_int_hook(sb_show_options, 0, m, sb);
}

int security_sb_statfs(struct dentry *dentry)
{
	return call_int_hook(sb_statfs, 0, dentry);
}

int security_sb_mount(const char *dev_name, const struct path *path,
                       const char *type, unsigned long flags, void *data)
{
	return call_int_hook(sb_mount, 0, dev_name, path, type, flags, data);
}

int security_sb_umount(struct vfsmount *mnt, int flags)
{
	return call_int_hook(sb_umount, 0, mnt, flags);
}

int security_sb_pivotroot(const struct path *old_path, const struct path *new_path)
{
	return call_int_hook(sb_pivotroot, 0, old_path, new_path);
}

int security_sb_set_mnt_opts(struct super_block *sb,
				struct security_mnt_opts *opts,
				unsigned long kern_flags,
				unsigned long *set_kern_flags)
{
	return call_int_hook(sb_set_mnt_opts,
				opts->num_mnt_opts ? -EOPNOTSUPP : 0, sb,
				opts, kern_flags, set_kern_flags);
}
EXPORT_SYMBOL(security_sb_set_mnt_opts);

int security_sb_clone_mnt_opts(const struct super_block *oldsb,
				struct super_block *newsb,
				unsigned long kern_flags,
				unsigned long *set_kern_flags)
{
	return call_int_hook(sb_clone_mnt_opts, 0, oldsb, newsb,
				kern_flags, set_kern_flags);
}
EXPORT_SYMBOL(security_sb_clone_mnt_opts);

int security_sb_parse_opts_str(char *options, struct security_mnt_opts *opts)
{
	return call_int_hook(sb_parse_opts_str, 0, options, opts);
}
EXPORT_SYMBOL(security_sb_parse_opts_str);

int security_inode_alloc(struct inode *inode)
{
	inode->i_security = NULL;
	return call_int_hook(inode_alloc_security, 0, inode);
}

void security_inode_free(struct inode *inode)
{
	integrity_inode_free(inode);
	call_void_hook(inode_free_security, inode);
}

int security_dentry_init_security(struct dentry *dentry, int mode,
					const struct qstr *name, void **ctx,
					u32 *ctxlen)
{
	return call_int_hook(dentry_init_security, -EOPNOTSUPP, dentry, mode,
				name, ctx, ctxlen);
}
EXPORT_SYMBOL(security_dentry_init_security);

int security_dentry_create_files_as(struct dentry *dentry, int mode,
				    struct qstr *name,
				    const struct cred *old, struct cred *new)
{
	return call_int_hook(dentry_create_files_as, 0, dentry, mode,
				name, old, new);
}
EXPORT_SYMBOL(security_dentry_create_files_as);

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
		return call_int_hook(inode_init_security, -EOPNOTSUPP, inode,
				     dir, qstr, NULL, NULL, NULL);
	memset(new_xattrs, 0, sizeof(new_xattrs));
	lsm_xattr = new_xattrs;
	ret = call_int_hook(inode_init_security, -EOPNOTSUPP, inode, dir, qstr,
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
	return call_int_hook(inode_init_security, -EOPNOTSUPP, inode, dir,
			     qstr, name, value, len);
}
EXPORT_SYMBOL(security_old_inode_init_security);

#ifdef CONFIG_SECURITY_PATH
int security_path_mknod(const struct path *dir, struct dentry *dentry, umode_t mode,
			unsigned int dev)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(dir->dentry))))
		return 0;
	return call_int_hook(path_mknod, 0, dir, dentry, mode, dev);
}
EXPORT_SYMBOL(security_path_mknod);

int security_path_mkdir(const struct path *dir, struct dentry *dentry, umode_t mode)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(dir->dentry))))
		return 0;
	return call_int_hook(path_mkdir, 0, dir, dentry, mode);
}
EXPORT_SYMBOL(security_path_mkdir);

int security_path_rmdir(const struct path *dir, struct dentry *dentry)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(dir->dentry))))
		return 0;
	return call_int_hook(path_rmdir, 0, dir, dentry);
}

int security_path_unlink(const struct path *dir, struct dentry *dentry)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(dir->dentry))))
		return 0;
	return call_int_hook(path_unlink, 0, dir, dentry);
}
EXPORT_SYMBOL(security_path_unlink);

int security_path_symlink(const struct path *dir, struct dentry *dentry,
			  const char *old_name)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(dir->dentry))))
		return 0;
	return call_int_hook(path_symlink, 0, dir, dentry, old_name);
}

int security_path_link(struct dentry *old_dentry, const struct path *new_dir,
		       struct dentry *new_dentry)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(old_dentry))))
		return 0;
	return call_int_hook(path_link, 0, old_dentry, new_dir, new_dentry);
}

int security_path_rename(const struct path *old_dir, struct dentry *old_dentry,
			 const struct path *new_dir, struct dentry *new_dentry,
			 unsigned int flags)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(old_dentry)) ||
		     (d_is_positive(new_dentry) && IS_PRIVATE(d_backing_inode(new_dentry)))))
		return 0;

	if (flags & RENAME_EXCHANGE) {
		int err = call_int_hook(path_rename, 0, new_dir, new_dentry,
					old_dir, old_dentry);
		if (err)
			return err;
	}

	return call_int_hook(path_rename, 0, old_dir, old_dentry, new_dir,
				new_dentry);
}
EXPORT_SYMBOL(security_path_rename);

int security_path_truncate(const struct path *path)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(path->dentry))))
		return 0;
	return call_int_hook(path_truncate, 0, path);
}

int security_path_chmod(const struct path *path, umode_t mode)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(path->dentry))))
		return 0;
	return call_int_hook(path_chmod, 0, path, mode);
}

int security_path_chown(const struct path *path, kuid_t uid, kgid_t gid)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(path->dentry))))
		return 0;
	return call_int_hook(path_chown, 0, path, uid, gid);
}

int security_path_chroot(const struct path *path)
{
	return call_int_hook(path_chroot, 0, path);
}
#endif

int security_inode_create(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	if (unlikely(IS_PRIVATE(dir)))
		return 0;
	return call_int_hook(inode_create, 0, dir, dentry, mode);
}
EXPORT_SYMBOL_GPL(security_inode_create);

int security_inode_link(struct dentry *old_dentry, struct inode *dir,
			 struct dentry *new_dentry)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(old_dentry))))
		return 0;
	return call_int_hook(inode_link, 0, old_dentry, dir, new_dentry);
}

int security_inode_unlink(struct inode *dir, struct dentry *dentry)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(dentry))))
		return 0;
	return call_int_hook(inode_unlink, 0, dir, dentry);
}

int security_inode_symlink(struct inode *dir, struct dentry *dentry,
			    const char *old_name)
{
	if (unlikely(IS_PRIVATE(dir)))
		return 0;
	return call_int_hook(inode_symlink, 0, dir, dentry, old_name);
}

int security_inode_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	if (unlikely(IS_PRIVATE(dir)))
		return 0;
	return call_int_hook(inode_mkdir, 0, dir, dentry, mode);
}
EXPORT_SYMBOL_GPL(security_inode_mkdir);

int security_inode_rmdir(struct inode *dir, struct dentry *dentry)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(dentry))))
		return 0;
	return call_int_hook(inode_rmdir, 0, dir, dentry);
}

int security_inode_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev)
{
	if (unlikely(IS_PRIVATE(dir)))
		return 0;
	return call_int_hook(inode_mknod, 0, dir, dentry, mode, dev);
}

int security_inode_rename(struct inode *old_dir, struct dentry *old_dentry,
			   struct inode *new_dir, struct dentry *new_dentry,
			   unsigned int flags)
{
        if (unlikely(IS_PRIVATE(d_backing_inode(old_dentry)) ||
            (d_is_positive(new_dentry) && IS_PRIVATE(d_backing_inode(new_dentry)))))
		return 0;

	if (flags & RENAME_EXCHANGE) {
		int err = call_int_hook(inode_rename, 0, new_dir, new_dentry,
						     old_dir, old_dentry);
		if (err)
			return err;
	}

	return call_int_hook(inode_rename, 0, old_dir, old_dentry,
					   new_dir, new_dentry);
}

int security_inode_readlink(struct dentry *dentry)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(dentry))))
		return 0;
	return call_int_hook(inode_readlink, 0, dentry);
}

int security_inode_follow_link(struct dentry *dentry, struct inode *inode,
			       bool rcu)
{
	if (unlikely(IS_PRIVATE(inode)))
		return 0;
	return call_int_hook(inode_follow_link, 0, dentry, inode, rcu);
}

int security_inode_permission(struct inode *inode, int mask)
{
	if (unlikely(IS_PRIVATE(inode)))
		return 0;
	return call_int_hook(inode_permission, 0, inode, mask);
}

int security_inode_setattr(struct dentry *dentry, struct iattr *attr)
{
	int ret;

	if (unlikely(IS_PRIVATE(d_backing_inode(dentry))))
		return 0;
	ret = call_int_hook(inode_setattr, 0, dentry, attr);
	if (ret)
		return ret;
	return evm_inode_setattr(dentry, attr);
}
EXPORT_SYMBOL_GPL(security_inode_setattr);

int security_inode_getattr(const struct path *path)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(path->dentry))))
		return 0;
	return call_int_hook(inode_getattr, 0, path);
}

int security_inode_setxattr(struct dentry *dentry, const char *name,
			    const void *value, size_t size, int flags)
{
	int ret;

	if (unlikely(IS_PRIVATE(d_backing_inode(dentry))))
		return 0;
	/*
	 * SELinux and Smack integrate the cap call,
	 * so assume that all LSMs supplying this call do so.
	 */
	ret = call_int_hook(inode_setxattr, 1, dentry, name, value, size,
				flags);

	if (ret == 1)
		ret = cap_inode_setxattr(dentry, name, value, size, flags);
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
	if (unlikely(IS_PRIVATE(d_backing_inode(dentry))))
		return;
	call_void_hook(inode_post_setxattr, dentry, name, value, size, flags);
	evm_inode_post_setxattr(dentry, name, value, size);
}

int security_inode_getxattr(struct dentry *dentry, const char *name)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(dentry))))
		return 0;
	return call_int_hook(inode_getxattr, 0, dentry, name);
}

int security_inode_listxattr(struct dentry *dentry)
{
	if (unlikely(IS_PRIVATE(d_backing_inode(dentry))))
		return 0;
	return call_int_hook(inode_listxattr, 0, dentry);
}

int security_inode_removexattr(struct dentry *dentry, const char *name)
{
	int ret;

	if (unlikely(IS_PRIVATE(d_backing_inode(dentry))))
		return 0;
	/*
	 * SELinux and Smack integrate the cap call,
	 * so assume that all LSMs supplying this call do so.
	 */
	ret = call_int_hook(inode_removexattr, 1, dentry, name);
	if (ret == 1)
		ret = cap_inode_removexattr(dentry, name);
	if (ret)
		return ret;
	ret = ima_inode_removexattr(dentry, name);
	if (ret)
		return ret;
	return evm_inode_removexattr(dentry, name);
}

int security_inode_need_killpriv(struct dentry *dentry)
{
	return call_int_hook(inode_need_killpriv, 0, dentry);
}

int security_inode_killpriv(struct dentry *dentry)
{
	return call_int_hook(inode_killpriv, 0, dentry);
}

int security_inode_getsecurity(struct inode *inode, const char *name, void **buffer, bool alloc)
{
	struct security_hook_list *hp;
	int rc;

	if (unlikely(IS_PRIVATE(inode)))
		return -EOPNOTSUPP;
	/*
	 * Only one module will provide an attribute with a given name.
	 */
	list_for_each_entry(hp, &security_hook_heads.inode_getsecurity, list) {
		rc = hp->hook.inode_getsecurity(inode, name, buffer, alloc);
		if (rc != -EOPNOTSUPP)
			return rc;
	}
	return -EOPNOTSUPP;
}

int security_inode_setsecurity(struct inode *inode, const char *name, const void *value, size_t size, int flags)
{
	struct security_hook_list *hp;
	int rc;

	if (unlikely(IS_PRIVATE(inode)))
		return -EOPNOTSUPP;
	/*
	 * Only one module will provide an attribute with a given name.
	 */
	list_for_each_entry(hp, &security_hook_heads.inode_setsecurity, list) {
		rc = hp->hook.inode_setsecurity(inode, name, value, size,
								flags);
		if (rc != -EOPNOTSUPP)
			return rc;
	}
	return -EOPNOTSUPP;
}

int security_inode_listsecurity(struct inode *inode, char *buffer, size_t buffer_size)
{
	if (unlikely(IS_PRIVATE(inode)))
		return 0;
	return call_int_hook(inode_listsecurity, 0, inode, buffer, buffer_size);
}
EXPORT_SYMBOL(security_inode_listsecurity);

void security_inode_getsecid(struct inode *inode, u32 *secid)
{
	call_void_hook(inode_getsecid, inode, secid);
}

int security_inode_copy_up(struct dentry *src, struct cred **new)
{
	return call_int_hook(inode_copy_up, 0, src, new);
}
EXPORT_SYMBOL(security_inode_copy_up);

int security_inode_copy_up_xattr(const char *name)
{
	return call_int_hook(inode_copy_up_xattr, -EOPNOTSUPP, name);
}
EXPORT_SYMBOL(security_inode_copy_up_xattr);

int security_file_permission(struct file *file, int mask)
{
	int ret;

	ret = call_int_hook(file_permission, 0, file, mask);
	if (ret)
		return ret;

	return fsnotify_perm(file, mask);
}

int security_file_alloc(struct file *file)
{
	return call_int_hook(file_alloc_security, 0, file);
}

void security_file_free(struct file *file)
{
	call_void_hook(file_free_security, file);
}

int security_file_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return call_int_hook(file_ioctl, 0, file, cmd, arg);
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
	 * NOMMU_MAP_EXEC (== VM_MAYEXEC) in this case
	 */
	if (!path_noexec(&file->f_path)) {
#ifndef CONFIG_MMU
		if (file->f_op->mmap_capabilities) {
			unsigned caps = file->f_op->mmap_capabilities(file);
			if (!(caps & NOMMU_MAP_EXEC))
				return prot;
		}
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
	ret = call_int_hook(mmap_file, 0, file, prot,
					mmap_prot(file, prot), flags);
	if (ret)
		return ret;
	return ima_file_mmap(file, prot);
}

int security_mmap_addr(unsigned long addr)
{
	return call_int_hook(mmap_addr, 0, addr);
}

int security_file_mprotect(struct vm_area_struct *vma, unsigned long reqprot,
			    unsigned long prot)
{
	return call_int_hook(file_mprotect, 0, vma, reqprot, prot);
}

int security_file_lock(struct file *file, unsigned int cmd)
{
	return call_int_hook(file_lock, 0, file, cmd);
}

int security_file_fcntl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return call_int_hook(file_fcntl, 0, file, cmd, arg);
}

void security_file_set_fowner(struct file *file)
{
	call_void_hook(file_set_fowner, file);
}

int security_file_send_sigiotask(struct task_struct *tsk,
				  struct fown_struct *fown, int sig)
{
	return call_int_hook(file_send_sigiotask, 0, tsk, fown, sig);
}

int security_file_receive(struct file *file)
{
	return call_int_hook(file_receive, 0, file);
}

int security_file_open(struct file *file, const struct cred *cred)
{
	int ret;

	ret = call_int_hook(file_open, 0, file, cred);
	if (ret)
		return ret;

	return fsnotify_perm(file, MAY_OPEN);
}

int security_task_alloc(struct task_struct *task, unsigned long clone_flags)
{
	return call_int_hook(task_alloc, 0, task, clone_flags);
}

void security_task_free(struct task_struct *task)
{
	call_void_hook(task_free, task);
}

int security_cred_alloc_blank(struct cred *cred, gfp_t gfp)
{
	return call_int_hook(cred_alloc_blank, 0, cred, gfp);
}

void security_cred_free(struct cred *cred)
{
	call_void_hook(cred_free, cred);
}

int security_prepare_creds(struct cred *new, const struct cred *old, gfp_t gfp)
{
	return call_int_hook(cred_prepare, 0, new, old, gfp);
}

void security_transfer_creds(struct cred *new, const struct cred *old)
{
	call_void_hook(cred_transfer, new, old);
}

int security_kernel_act_as(struct cred *new, u32 secid)
{
	return call_int_hook(kernel_act_as, 0, new, secid);
}

int security_kernel_create_files_as(struct cred *new, struct inode *inode)
{
	return call_int_hook(kernel_create_files_as, 0, new, inode);
}

int security_kernel_module_request(char *kmod_name)
{
	return call_int_hook(kernel_module_request, 0, kmod_name);
}

int security_kernel_read_file(struct file *file, enum kernel_read_file_id id)
{
	int ret;

	ret = call_int_hook(kernel_read_file, 0, file, id);
	if (ret)
		return ret;
	return ima_read_file(file, id);
}
EXPORT_SYMBOL_GPL(security_kernel_read_file);

int security_kernel_post_read_file(struct file *file, char *buf, loff_t size,
				   enum kernel_read_file_id id)
{
	int ret;

	ret = call_int_hook(kernel_post_read_file, 0, file, buf, size, id);
	if (ret)
		return ret;
	return ima_post_read_file(file, buf, size, id);
}
EXPORT_SYMBOL_GPL(security_kernel_post_read_file);

int security_task_fix_setuid(struct cred *new, const struct cred *old,
			     int flags)
{
	return call_int_hook(task_fix_setuid, 0, new, old, flags);
}

int security_task_setpgid(struct task_struct *p, pid_t pgid)
{
	return call_int_hook(task_setpgid, 0, p, pgid);
}

int security_task_getpgid(struct task_struct *p)
{
	return call_int_hook(task_getpgid, 0, p);
}

int security_task_getsid(struct task_struct *p)
{
	return call_int_hook(task_getsid, 0, p);
}

void security_task_getsecid(struct task_struct *p, u32 *secid)
{
	*secid = 0;
	call_void_hook(task_getsecid, p, secid);
}
EXPORT_SYMBOL(security_task_getsecid);

int security_task_setnice(struct task_struct *p, int nice)
{
	return call_int_hook(task_setnice, 0, p, nice);
}

int security_task_setioprio(struct task_struct *p, int ioprio)
{
	return call_int_hook(task_setioprio, 0, p, ioprio);
}

int security_task_getioprio(struct task_struct *p)
{
	return call_int_hook(task_getioprio, 0, p);
}

int security_task_prlimit(const struct cred *cred, const struct cred *tcred,
			  unsigned int flags)
{
	return call_int_hook(task_prlimit, 0, cred, tcred, flags);
}

int security_task_setrlimit(struct task_struct *p, unsigned int resource,
		struct rlimit *new_rlim)
{
	return call_int_hook(task_setrlimit, 0, p, resource, new_rlim);
}

int security_task_setscheduler(struct task_struct *p)
{
	return call_int_hook(task_setscheduler, 0, p);
}

int security_task_getscheduler(struct task_struct *p)
{
	return call_int_hook(task_getscheduler, 0, p);
}

int security_task_movememory(struct task_struct *p)
{
	return call_int_hook(task_movememory, 0, p);
}

int security_task_kill(struct task_struct *p, struct siginfo *info,
			int sig, const struct cred *cred)
{
	return call_int_hook(task_kill, 0, p, info, sig, cred);
}

int security_task_prctl(int option, unsigned long arg2, unsigned long arg3,
			 unsigned long arg4, unsigned long arg5)
{
	int thisrc;
	int rc = -ENOSYS;
	struct security_hook_list *hp;

	list_for_each_entry(hp, &security_hook_heads.task_prctl, list) {
		thisrc = hp->hook.task_prctl(option, arg2, arg3, arg4, arg5);
		if (thisrc != -ENOSYS) {
			rc = thisrc;
			if (thisrc != 0)
				break;
		}
	}
	return rc;
}

void security_task_to_inode(struct task_struct *p, struct inode *inode)
{
	call_void_hook(task_to_inode, p, inode);
}

int security_ipc_permission(struct kern_ipc_perm *ipcp, short flag)
{
	return call_int_hook(ipc_permission, 0, ipcp, flag);
}

void security_ipc_getsecid(struct kern_ipc_perm *ipcp, u32 *secid)
{
	*secid = 0;
	call_void_hook(ipc_getsecid, ipcp, secid);
}

int security_msg_msg_alloc(struct msg_msg *msg)
{
	return call_int_hook(msg_msg_alloc_security, 0, msg);
}

void security_msg_msg_free(struct msg_msg *msg)
{
	call_void_hook(msg_msg_free_security, msg);
}

int security_msg_queue_alloc(struct msg_queue *msq)
{
	return call_int_hook(msg_queue_alloc_security, 0, msq);
}

void security_msg_queue_free(struct msg_queue *msq)
{
	call_void_hook(msg_queue_free_security, msq);
}

int security_msg_queue_associate(struct msg_queue *msq, int msqflg)
{
	return call_int_hook(msg_queue_associate, 0, msq, msqflg);
}

int security_msg_queue_msgctl(struct msg_queue *msq, int cmd)
{
	return call_int_hook(msg_queue_msgctl, 0, msq, cmd);
}

int security_msg_queue_msgsnd(struct msg_queue *msq,
			       struct msg_msg *msg, int msqflg)
{
	return call_int_hook(msg_queue_msgsnd, 0, msq, msg, msqflg);
}

int security_msg_queue_msgrcv(struct msg_queue *msq, struct msg_msg *msg,
			       struct task_struct *target, long type, int mode)
{
	return call_int_hook(msg_queue_msgrcv, 0, msq, msg, target, type, mode);
}

int security_shm_alloc(struct shmid_kernel *shp)
{
	return call_int_hook(shm_alloc_security, 0, shp);
}

void security_shm_free(struct shmid_kernel *shp)
{
	call_void_hook(shm_free_security, shp);
}

int security_shm_associate(struct shmid_kernel *shp, int shmflg)
{
	return call_int_hook(shm_associate, 0, shp, shmflg);
}

int security_shm_shmctl(struct shmid_kernel *shp, int cmd)
{
	return call_int_hook(shm_shmctl, 0, shp, cmd);
}

int security_shm_shmat(struct shmid_kernel *shp, char __user *shmaddr, int shmflg)
{
	return call_int_hook(shm_shmat, 0, shp, shmaddr, shmflg);
}

int security_sem_alloc(struct sem_array *sma)
{
	return call_int_hook(sem_alloc_security, 0, sma);
}

void security_sem_free(struct sem_array *sma)
{
	call_void_hook(sem_free_security, sma);
}

int security_sem_associate(struct sem_array *sma, int semflg)
{
	return call_int_hook(sem_associate, 0, sma, semflg);
}

int security_sem_semctl(struct sem_array *sma, int cmd)
{
	return call_int_hook(sem_semctl, 0, sma, cmd);
}

int security_sem_semop(struct sem_array *sma, struct sembuf *sops,
			unsigned nsops, int alter)
{
	return call_int_hook(sem_semop, 0, sma, sops, nsops, alter);
}

void security_d_instantiate(struct dentry *dentry, struct inode *inode)
{
	if (unlikely(inode && IS_PRIVATE(inode)))
		return;
	call_void_hook(d_instantiate, dentry, inode);
}
EXPORT_SYMBOL(security_d_instantiate);

int security_getprocattr(struct task_struct *p, char *name, char **value)
{
	return call_int_hook(getprocattr, -EINVAL, p, name, value);
}

int security_setprocattr(const char *name, void *value, size_t size)
{
	return call_int_hook(setprocattr, -EINVAL, name, value, size);
}

int security_netlink_send(struct sock *sk, struct sk_buff *skb)
{
	return call_int_hook(netlink_send, 0, sk, skb);
}

int security_ismaclabel(const char *name)
{
	return call_int_hook(ismaclabel, 0, name);
}
EXPORT_SYMBOL(security_ismaclabel);

int security_secid_to_secctx(u32 secid, char **secdata, u32 *seclen)
{
	return call_int_hook(secid_to_secctx, -EOPNOTSUPP, secid, secdata,
				seclen);
}
EXPORT_SYMBOL(security_secid_to_secctx);

int security_secctx_to_secid(const char *secdata, u32 seclen, u32 *secid)
{
	*secid = 0;
	return call_int_hook(secctx_to_secid, 0, secdata, seclen, secid);
}
EXPORT_SYMBOL(security_secctx_to_secid);

void security_release_secctx(char *secdata, u32 seclen)
{
	call_void_hook(release_secctx, secdata, seclen);
}
EXPORT_SYMBOL(security_release_secctx);

void security_inode_invalidate_secctx(struct inode *inode)
{
	call_void_hook(inode_invalidate_secctx, inode);
}
EXPORT_SYMBOL(security_inode_invalidate_secctx);

int security_inode_notifysecctx(struct inode *inode, void *ctx, u32 ctxlen)
{
	return call_int_hook(inode_notifysecctx, 0, inode, ctx, ctxlen);
}
EXPORT_SYMBOL(security_inode_notifysecctx);

int security_inode_setsecctx(struct dentry *dentry, void *ctx, u32 ctxlen)
{
	return call_int_hook(inode_setsecctx, 0, dentry, ctx, ctxlen);
}
EXPORT_SYMBOL(security_inode_setsecctx);

int security_inode_getsecctx(struct inode *inode, void **ctx, u32 *ctxlen)
{
	return call_int_hook(inode_getsecctx, -EOPNOTSUPP, inode, ctx, ctxlen);
}
EXPORT_SYMBOL(security_inode_getsecctx);

#ifdef CONFIG_SECURITY_NETWORK

int security_unix_stream_connect(struct sock *sock, struct sock *other, struct sock *newsk)
{
	return call_int_hook(unix_stream_connect, 0, sock, other, newsk);
}
EXPORT_SYMBOL(security_unix_stream_connect);

int security_unix_may_send(struct socket *sock,  struct socket *other)
{
	return call_int_hook(unix_may_send, 0, sock, other);
}
EXPORT_SYMBOL(security_unix_may_send);

int security_socket_create(int family, int type, int protocol, int kern)
{
	return call_int_hook(socket_create, 0, family, type, protocol, kern);
}

int security_socket_post_create(struct socket *sock, int family,
				int type, int protocol, int kern)
{
	return call_int_hook(socket_post_create, 0, sock, family, type,
						protocol, kern);
}

int security_socket_bind(struct socket *sock, struct sockaddr *address, int addrlen)
{
	return call_int_hook(socket_bind, 0, sock, address, addrlen);
}

int security_socket_connect(struct socket *sock, struct sockaddr *address, int addrlen)
{
	return call_int_hook(socket_connect, 0, sock, address, addrlen);
}

int security_socket_listen(struct socket *sock, int backlog)
{
	return call_int_hook(socket_listen, 0, sock, backlog);
}

int security_socket_accept(struct socket *sock, struct socket *newsock)
{
	return call_int_hook(socket_accept, 0, sock, newsock);
}

int security_socket_sendmsg(struct socket *sock, struct msghdr *msg, int size)
{
	return call_int_hook(socket_sendmsg, 0, sock, msg, size);
}

int security_socket_recvmsg(struct socket *sock, struct msghdr *msg,
			    int size, int flags)
{
	return call_int_hook(socket_recvmsg, 0, sock, msg, size, flags);
}

int security_socket_getsockname(struct socket *sock)
{
	return call_int_hook(socket_getsockname, 0, sock);
}

int security_socket_getpeername(struct socket *sock)
{
	return call_int_hook(socket_getpeername, 0, sock);
}

int security_socket_getsockopt(struct socket *sock, int level, int optname)
{
	return call_int_hook(socket_getsockopt, 0, sock, level, optname);
}

int security_socket_setsockopt(struct socket *sock, int level, int optname)
{
	return call_int_hook(socket_setsockopt, 0, sock, level, optname);
}

int security_socket_shutdown(struct socket *sock, int how)
{
	return call_int_hook(socket_shutdown, 0, sock, how);
}

int security_sock_rcv_skb(struct sock *sk, struct sk_buff *skb)
{
	return call_int_hook(socket_sock_rcv_skb, 0, sk, skb);
}
EXPORT_SYMBOL(security_sock_rcv_skb);

int security_socket_getpeersec_stream(struct socket *sock, char __user *optval,
				      int __user *optlen, unsigned len)
{
	return call_int_hook(socket_getpeersec_stream, -ENOPROTOOPT, sock,
				optval, optlen, len);
}

int security_socket_getpeersec_dgram(struct socket *sock, struct sk_buff *skb, u32 *secid)
{
	return call_int_hook(socket_getpeersec_dgram, -ENOPROTOOPT, sock,
			     skb, secid);
}
EXPORT_SYMBOL(security_socket_getpeersec_dgram);

int security_sk_alloc(struct sock *sk, int family, gfp_t priority)
{
	return call_int_hook(sk_alloc_security, 0, sk, family, priority);
}

void security_sk_free(struct sock *sk)
{
	call_void_hook(sk_free_security, sk);
}

void security_sk_clone(const struct sock *sk, struct sock *newsk)
{
	call_void_hook(sk_clone_security, sk, newsk);
}
EXPORT_SYMBOL(security_sk_clone);

void security_sk_classify_flow(struct sock *sk, struct flowi *fl)
{
	call_void_hook(sk_getsecid, sk, &fl->flowi_secid);
}
EXPORT_SYMBOL(security_sk_classify_flow);

void security_req_classify_flow(const struct request_sock *req, struct flowi *fl)
{
	call_void_hook(req_classify_flow, req, fl);
}
EXPORT_SYMBOL(security_req_classify_flow);

void security_sock_graft(struct sock *sk, struct socket *parent)
{
	call_void_hook(sock_graft, sk, parent);
}
EXPORT_SYMBOL(security_sock_graft);

int security_inet_conn_request(struct sock *sk,
			struct sk_buff *skb, struct request_sock *req)
{
	return call_int_hook(inet_conn_request, 0, sk, skb, req);
}
EXPORT_SYMBOL(security_inet_conn_request);

void security_inet_csk_clone(struct sock *newsk,
			const struct request_sock *req)
{
	call_void_hook(inet_csk_clone, newsk, req);
}

void security_inet_conn_established(struct sock *sk,
			struct sk_buff *skb)
{
	call_void_hook(inet_conn_established, sk, skb);
}

int security_secmark_relabel_packet(u32 secid)
{
	return call_int_hook(secmark_relabel_packet, 0, secid);
}
EXPORT_SYMBOL(security_secmark_relabel_packet);

void security_secmark_refcount_inc(void)
{
	call_void_hook(secmark_refcount_inc);
}
EXPORT_SYMBOL(security_secmark_refcount_inc);

void security_secmark_refcount_dec(void)
{
	call_void_hook(secmark_refcount_dec);
}
EXPORT_SYMBOL(security_secmark_refcount_dec);

int security_tun_dev_alloc_security(void **security)
{
	return call_int_hook(tun_dev_alloc_security, 0, security);
}
EXPORT_SYMBOL(security_tun_dev_alloc_security);

void security_tun_dev_free_security(void *security)
{
	call_void_hook(tun_dev_free_security, security);
}
EXPORT_SYMBOL(security_tun_dev_free_security);

int security_tun_dev_create(void)
{
	return call_int_hook(tun_dev_create, 0);
}
EXPORT_SYMBOL(security_tun_dev_create);

int security_tun_dev_attach_queue(void *security)
{
	return call_int_hook(tun_dev_attach_queue, 0, security);
}
EXPORT_SYMBOL(security_tun_dev_attach_queue);

int security_tun_dev_attach(struct sock *sk, void *security)
{
	return call_int_hook(tun_dev_attach, 0, sk, security);
}
EXPORT_SYMBOL(security_tun_dev_attach);

int security_tun_dev_open(void *security)
{
	return call_int_hook(tun_dev_open, 0, security);
}
EXPORT_SYMBOL(security_tun_dev_open);

#endif	/* CONFIG_SECURITY_NETWORK */

#ifdef CONFIG_SECURITY_INFINIBAND

int security_ib_pkey_access(void *sec, u64 subnet_prefix, u16 pkey)
{
	return call_int_hook(ib_pkey_access, 0, sec, subnet_prefix, pkey);
}
EXPORT_SYMBOL(security_ib_pkey_access);

int security_ib_endport_manage_subnet(void *sec, const char *dev_name, u8 port_num)
{
	return call_int_hook(ib_endport_manage_subnet, 0, sec, dev_name, port_num);
}
EXPORT_SYMBOL(security_ib_endport_manage_subnet);

int security_ib_alloc_security(void **sec)
{
	return call_int_hook(ib_alloc_security, 0, sec);
}
EXPORT_SYMBOL(security_ib_alloc_security);

void security_ib_free_security(void *sec)
{
	call_void_hook(ib_free_security, sec);
}
EXPORT_SYMBOL(security_ib_free_security);
#endif	/* CONFIG_SECURITY_INFINIBAND */

#ifdef CONFIG_SECURITY_NETWORK_XFRM

int security_xfrm_policy_alloc(struct xfrm_sec_ctx **ctxp,
			       struct xfrm_user_sec_ctx *sec_ctx,
			       gfp_t gfp)
{
	return call_int_hook(xfrm_policy_alloc_security, 0, ctxp, sec_ctx, gfp);
}
EXPORT_SYMBOL(security_xfrm_policy_alloc);

int security_xfrm_policy_clone(struct xfrm_sec_ctx *old_ctx,
			      struct xfrm_sec_ctx **new_ctxp)
{
	return call_int_hook(xfrm_policy_clone_security, 0, old_ctx, new_ctxp);
}

void security_xfrm_policy_free(struct xfrm_sec_ctx *ctx)
{
	call_void_hook(xfrm_policy_free_security, ctx);
}
EXPORT_SYMBOL(security_xfrm_policy_free);

int security_xfrm_policy_delete(struct xfrm_sec_ctx *ctx)
{
	return call_int_hook(xfrm_policy_delete_security, 0, ctx);
}

int security_xfrm_state_alloc(struct xfrm_state *x,
			      struct xfrm_user_sec_ctx *sec_ctx)
{
	return call_int_hook(xfrm_state_alloc, 0, x, sec_ctx);
}
EXPORT_SYMBOL(security_xfrm_state_alloc);

int security_xfrm_state_alloc_acquire(struct xfrm_state *x,
				      struct xfrm_sec_ctx *polsec, u32 secid)
{
	return call_int_hook(xfrm_state_alloc_acquire, 0, x, polsec, secid);
}

int security_xfrm_state_delete(struct xfrm_state *x)
{
	return call_int_hook(xfrm_state_delete_security, 0, x);
}
EXPORT_SYMBOL(security_xfrm_state_delete);

void security_xfrm_state_free(struct xfrm_state *x)
{
	call_void_hook(xfrm_state_free_security, x);
}

int security_xfrm_policy_lookup(struct xfrm_sec_ctx *ctx, u32 fl_secid, u8 dir)
{
	return call_int_hook(xfrm_policy_lookup, 0, ctx, fl_secid, dir);
}

int security_xfrm_state_pol_flow_match(struct xfrm_state *x,
				       struct xfrm_policy *xp,
				       const struct flowi *fl)
{
	struct security_hook_list *hp;
	int rc = 1;

	/*
	 * Since this function is expected to return 0 or 1, the judgment
	 * becomes difficult if multiple LSMs supply this call. Fortunately,
	 * we can use the first LSM's judgment because currently only SELinux
	 * supplies this call.
	 *
	 * For speed optimization, we explicitly break the loop rather than
	 * using the macro
	 */
	list_for_each_entry(hp, &security_hook_heads.xfrm_state_pol_flow_match,
				list) {
		rc = hp->hook.xfrm_state_pol_flow_match(x, xp, fl);
		break;
	}
	return rc;
}

int security_xfrm_decode_session(struct sk_buff *skb, u32 *secid)
{
	return call_int_hook(xfrm_decode_session, 0, skb, secid, 1);
}

void security_skb_classify_flow(struct sk_buff *skb, struct flowi *fl)
{
	int rc = call_int_hook(xfrm_decode_session, 0, skb, &fl->flowi_secid,
				0);

	BUG_ON(rc);
}
EXPORT_SYMBOL(security_skb_classify_flow);

#endif	/* CONFIG_SECURITY_NETWORK_XFRM */

#ifdef CONFIG_KEYS

int security_key_alloc(struct key *key, const struct cred *cred,
		       unsigned long flags)
{
	return call_int_hook(key_alloc, 0, key, cred, flags);
}

void security_key_free(struct key *key)
{
	call_void_hook(key_free, key);
}

int security_key_permission(key_ref_t key_ref,
			    const struct cred *cred, unsigned perm)
{
	return call_int_hook(key_permission, 0, key_ref, cred, perm);
}

int security_key_getsecurity(struct key *key, char **_buffer)
{
	*_buffer = NULL;
	return call_int_hook(key_getsecurity, 0, key, _buffer);
}

#endif	/* CONFIG_KEYS */

#ifdef CONFIG_AUDIT

int security_audit_rule_init(u32 field, u32 op, char *rulestr, void **lsmrule)
{
	return call_int_hook(audit_rule_init, 0, field, op, rulestr, lsmrule);
}

int security_audit_rule_known(struct audit_krule *krule)
{
	return call_int_hook(audit_rule_known, 0, krule);
}

void security_audit_rule_free(void *lsmrule)
{
	call_void_hook(audit_rule_free, lsmrule);
}

int security_audit_rule_match(u32 secid, u32 field, u32 op, void *lsmrule,
			      struct audit_context *actx)
{
	return call_int_hook(audit_rule_match, 0, secid, field, op, lsmrule,
				actx);
}
#endif /* CONFIG_AUDIT */

#ifdef CONFIG_BPF_SYSCALL
int security_bpf(int cmd, union bpf_attr *attr, unsigned int size)
{
	return call_int_hook(bpf, 0, cmd, attr, size);
}
int security_bpf_map(struct bpf_map *map, fmode_t fmode)
{
	return call_int_hook(bpf_map, 0, map, fmode);
}
int security_bpf_prog(struct bpf_prog *prog)
{
	return call_int_hook(bpf_prog, 0, prog);
}
int security_bpf_map_alloc(struct bpf_map *map)
{
	return call_int_hook(bpf_map_alloc_security, 0, map);
}
int security_bpf_prog_alloc(struct bpf_prog_aux *aux)
{
	return call_int_hook(bpf_prog_alloc_security, 0, aux);
}
void security_bpf_map_free(struct bpf_map *map)
{
	call_void_hook(bpf_map_free_security, map);
}
void security_bpf_prog_free(struct bpf_prog_aux *aux)
{
	call_void_hook(bpf_prog_free_security, aux);
}
#endif /* CONFIG_BPF_SYSCALL */
