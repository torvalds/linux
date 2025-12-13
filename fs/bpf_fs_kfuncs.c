// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Google LLC. */

#include <linux/bpf.h>
#include <linux/bpf_lsm.h>
#include <linux/btf.h>
#include <linux/btf_ids.h>
#include <linux/dcache.h>
#include <linux/fs.h>
#include <linux/fsnotify.h>
#include <linux/file.h>
#include <linux/kernfs.h>
#include <linux/mm.h>
#include <linux/xattr.h>

__bpf_kfunc_start_defs();

/**
 * bpf_get_task_exe_file - get a reference on the exe_file struct file member of
 *                         the mm_struct that is nested within the supplied
 *                         task_struct
 * @task: task_struct of which the nested mm_struct exe_file member to get a
 * reference on
 *
 * Get a reference on the exe_file struct file member field of the mm_struct
 * nested within the supplied *task*. The referenced file pointer acquired by
 * this BPF kfunc must be released using bpf_put_file(). Failing to call
 * bpf_put_file() on the returned referenced struct file pointer that has been
 * acquired by this BPF kfunc will result in the BPF program being rejected by
 * the BPF verifier.
 *
 * This BPF kfunc may only be called from BPF LSM programs.
 *
 * Internally, this BPF kfunc leans on get_task_exe_file(), such that calling
 * bpf_get_task_exe_file() would be analogous to calling get_task_exe_file()
 * directly in kernel context.
 *
 * Return: A referenced struct file pointer to the exe_file member of the
 * mm_struct that is nested within the supplied *task*. On error, NULL is
 * returned.
 */
__bpf_kfunc struct file *bpf_get_task_exe_file(struct task_struct *task)
{
	return get_task_exe_file(task);
}

/**
 * bpf_put_file - put a reference on the supplied file
 * @file: file to put a reference on
 *
 * Put a reference on the supplied *file*. Only referenced file pointers may be
 * passed to this BPF kfunc. Attempting to pass an unreferenced file pointer, or
 * any other arbitrary pointer for that matter, will result in the BPF program
 * being rejected by the BPF verifier.
 *
 * This BPF kfunc may only be called from BPF LSM programs.
 */
__bpf_kfunc void bpf_put_file(struct file *file)
{
	fput(file);
}

/**
 * bpf_path_d_path - resolve the pathname for the supplied path
 * @path: path to resolve the pathname for
 * @buf: buffer to return the resolved pathname in
 * @buf__sz: length of the supplied buffer
 *
 * Resolve the pathname for the supplied *path* and store it in *buf*. This BPF
 * kfunc is the safer variant of the legacy bpf_d_path() helper and should be
 * used in place of bpf_d_path() whenever possible. It enforces KF_TRUSTED_ARGS
 * semantics, meaning that the supplied *path* must itself hold a valid
 * reference, or else the BPF program will be outright rejected by the BPF
 * verifier.
 *
 * This BPF kfunc may only be called from BPF LSM programs.
 *
 * Return: A positive integer corresponding to the length of the resolved
 * pathname in *buf*, including the NUL termination character. On error, a
 * negative integer is returned.
 */
__bpf_kfunc int bpf_path_d_path(const struct path *path, char *buf, size_t buf__sz)
{
	int len;
	char *ret;

	if (!buf__sz)
		return -EINVAL;

	ret = d_path(path, buf, buf__sz);
	if (IS_ERR(ret))
		return PTR_ERR(ret);

	len = buf + buf__sz - ret;
	memmove(buf, ret, len);
	return len;
}

static bool match_security_bpf_prefix(const char *name__str)
{
	return !strncmp(name__str, XATTR_NAME_BPF_LSM, XATTR_NAME_BPF_LSM_LEN);
}

static int bpf_xattr_read_permission(const char *name, struct inode *inode)
{
	if (WARN_ON(!inode))
		return -EINVAL;

	/* Allow reading xattr with user. and security.bpf. prefix */
	if (strncmp(name, XATTR_USER_PREFIX, XATTR_USER_PREFIX_LEN) &&
	    !match_security_bpf_prefix(name))
		return -EPERM;

	return inode_permission(&nop_mnt_idmap, inode, MAY_READ);
}

/**
 * bpf_get_dentry_xattr - get xattr of a dentry
 * @dentry: dentry to get xattr from
 * @name__str: name of the xattr
 * @value_p: output buffer of the xattr value
 *
 * Get xattr *name__str* of *dentry* and store the output in *value_ptr*.
 *
 * For security reasons, only *name__str* with prefixes "user." or
 * "security.bpf." are allowed.
 *
 * Return: length of the xattr value on success, a negative value on error.
 */
__bpf_kfunc int bpf_get_dentry_xattr(struct dentry *dentry, const char *name__str,
				     struct bpf_dynptr *value_p)
{
	struct bpf_dynptr_kern *value_ptr = (struct bpf_dynptr_kern *)value_p;
	struct inode *inode = d_inode(dentry);
	u32 value_len;
	void *value;
	int ret;

	value_len = __bpf_dynptr_size(value_ptr);
	value = __bpf_dynptr_data_rw(value_ptr, value_len);
	if (!value)
		return -EINVAL;

	ret = bpf_xattr_read_permission(name__str, inode);
	if (ret)
		return ret;
	return __vfs_getxattr(dentry, inode, name__str, value, value_len);
}

/**
 * bpf_get_file_xattr - get xattr of a file
 * @file: file to get xattr from
 * @name__str: name of the xattr
 * @value_p: output buffer of the xattr value
 *
 * Get xattr *name__str* of *file* and store the output in *value_ptr*.
 *
 * For security reasons, only *name__str* with prefixes "user." or
 * "security.bpf." are allowed.
 *
 * Return: length of the xattr value on success, a negative value on error.
 */
__bpf_kfunc int bpf_get_file_xattr(struct file *file, const char *name__str,
				   struct bpf_dynptr *value_p)
{
	struct dentry *dentry;

	dentry = file_dentry(file);
	return bpf_get_dentry_xattr(dentry, name__str, value_p);
}

__bpf_kfunc_end_defs();

static int bpf_xattr_write_permission(const char *name, struct inode *inode)
{
	if (WARN_ON(!inode))
		return -EINVAL;

	/* Only allow setting and removing security.bpf. xattrs */
	if (!match_security_bpf_prefix(name))
		return -EPERM;

	return inode_permission(&nop_mnt_idmap, inode, MAY_WRITE);
}

/**
 * bpf_set_dentry_xattr_locked - set a xattr of a dentry
 * @dentry: dentry to get xattr from
 * @name__str: name of the xattr
 * @value_p: xattr value
 * @flags: flags to pass into filesystem operations
 *
 * Set xattr *name__str* of *dentry* to the value in *value_ptr*.
 *
 * For security reasons, only *name__str* with prefix "security.bpf."
 * is allowed.
 *
 * The caller already locked dentry->d_inode.
 *
 * Return: 0 on success, a negative value on error.
 */
int bpf_set_dentry_xattr_locked(struct dentry *dentry, const char *name__str,
				const struct bpf_dynptr *value_p, int flags)
{

	struct bpf_dynptr_kern *value_ptr = (struct bpf_dynptr_kern *)value_p;
	struct inode *inode = d_inode(dentry);
	const void *value;
	u32 value_len;
	int ret;

	value_len = __bpf_dynptr_size(value_ptr);
	value = __bpf_dynptr_data(value_ptr, value_len);
	if (!value)
		return -EINVAL;

	ret = bpf_xattr_write_permission(name__str, inode);
	if (ret)
		return ret;

	ret = __vfs_setxattr(&nop_mnt_idmap, dentry, inode, name__str,
			     value, value_len, flags);
	if (!ret) {
		fsnotify_xattr(dentry);

		/* This xattr is set by BPF LSM, so we do not call
		 * security_inode_post_setxattr. Otherwise, we would
		 * risk deadlocks by calling back to the same kfunc.
		 *
		 * This is the same as security_inode_setsecurity().
		 */
	}
	return ret;
}

/**
 * bpf_remove_dentry_xattr_locked - remove a xattr of a dentry
 * @dentry: dentry to get xattr from
 * @name__str: name of the xattr
 *
 * Rmove xattr *name__str* of *dentry*.
 *
 * For security reasons, only *name__str* with prefix "security.bpf."
 * is allowed.
 *
 * The caller already locked dentry->d_inode.
 *
 * Return: 0 on success, a negative value on error.
 */
int bpf_remove_dentry_xattr_locked(struct dentry *dentry, const char *name__str)
{
	struct inode *inode = d_inode(dentry);
	int ret;

	ret = bpf_xattr_write_permission(name__str, inode);
	if (ret)
		return ret;

	ret = __vfs_removexattr(&nop_mnt_idmap, dentry, name__str);
	if (!ret) {
		fsnotify_xattr(dentry);

		/* This xattr is removed by BPF LSM, so we do not call
		 * security_inode_post_removexattr. Otherwise, we would
		 * risk deadlocks by calling back to the same kfunc.
		 */
	}
	return ret;
}

__bpf_kfunc_start_defs();

/**
 * bpf_set_dentry_xattr - set a xattr of a dentry
 * @dentry: dentry to get xattr from
 * @name__str: name of the xattr
 * @value_p: xattr value
 * @flags: flags to pass into filesystem operations
 *
 * Set xattr *name__str* of *dentry* to the value in *value_ptr*.
 *
 * For security reasons, only *name__str* with prefix "security.bpf."
 * is allowed.
 *
 * The caller has not locked dentry->d_inode.
 *
 * Return: 0 on success, a negative value on error.
 */
__bpf_kfunc int bpf_set_dentry_xattr(struct dentry *dentry, const char *name__str,
				     const struct bpf_dynptr *value_p, int flags)
{
	struct inode *inode = d_inode(dentry);
	int ret;

	inode_lock(inode);
	ret = bpf_set_dentry_xattr_locked(dentry, name__str, value_p, flags);
	inode_unlock(inode);
	return ret;
}

/**
 * bpf_remove_dentry_xattr - remove a xattr of a dentry
 * @dentry: dentry to get xattr from
 * @name__str: name of the xattr
 *
 * Rmove xattr *name__str* of *dentry*.
 *
 * For security reasons, only *name__str* with prefix "security.bpf."
 * is allowed.
 *
 * The caller has not locked dentry->d_inode.
 *
 * Return: 0 on success, a negative value on error.
 */
__bpf_kfunc int bpf_remove_dentry_xattr(struct dentry *dentry, const char *name__str)
{
	struct inode *inode = d_inode(dentry);
	int ret;

	inode_lock(inode);
	ret = bpf_remove_dentry_xattr_locked(dentry, name__str);
	inode_unlock(inode);
	return ret;
}

#ifdef CONFIG_CGROUPS
/**
 * bpf_cgroup_read_xattr - read xattr of a cgroup's node in cgroupfs
 * @cgroup: cgroup to get xattr from
 * @name__str: name of the xattr
 * @value_p: output buffer of the xattr value
 *
 * Get xattr *name__str* of *cgroup* and store the output in *value_ptr*.
 *
 * For security reasons, only *name__str* with prefix "user." is allowed.
 *
 * Return: length of the xattr value on success, a negative value on error.
 */
__bpf_kfunc int bpf_cgroup_read_xattr(struct cgroup *cgroup, const char *name__str,
					struct bpf_dynptr *value_p)
{
	struct bpf_dynptr_kern *value_ptr = (struct bpf_dynptr_kern *)value_p;
	u32 value_len;
	void *value;

	/* Only allow reading "user.*" xattrs */
	if (strncmp(name__str, XATTR_USER_PREFIX, XATTR_USER_PREFIX_LEN))
		return -EPERM;

	value_len = __bpf_dynptr_size(value_ptr);
	value = __bpf_dynptr_data_rw(value_ptr, value_len);
	if (!value)
		return -EINVAL;

	return kernfs_xattr_get(cgroup->kn, name__str, value, value_len);
}
#endif /* CONFIG_CGROUPS */

__bpf_kfunc_end_defs();

BTF_KFUNCS_START(bpf_fs_kfunc_set_ids)
BTF_ID_FLAGS(func, bpf_get_task_exe_file,
	     KF_ACQUIRE | KF_TRUSTED_ARGS | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_put_file, KF_RELEASE)
BTF_ID_FLAGS(func, bpf_path_d_path, KF_TRUSTED_ARGS)
BTF_ID_FLAGS(func, bpf_get_dentry_xattr, KF_SLEEPABLE | KF_TRUSTED_ARGS)
BTF_ID_FLAGS(func, bpf_get_file_xattr, KF_SLEEPABLE | KF_TRUSTED_ARGS)
BTF_ID_FLAGS(func, bpf_set_dentry_xattr, KF_SLEEPABLE | KF_TRUSTED_ARGS)
BTF_ID_FLAGS(func, bpf_remove_dentry_xattr, KF_SLEEPABLE | KF_TRUSTED_ARGS)
BTF_KFUNCS_END(bpf_fs_kfunc_set_ids)

static int bpf_fs_kfuncs_filter(const struct bpf_prog *prog, u32 kfunc_id)
{
	if (!btf_id_set8_contains(&bpf_fs_kfunc_set_ids, kfunc_id) ||
	    prog->type == BPF_PROG_TYPE_LSM)
		return 0;
	return -EACCES;
}

/* bpf_[set|remove]_dentry_xattr.* hooks have KF_TRUSTED_ARGS and
 * KF_SLEEPABLE, so they are only available to sleepable hooks with
 * dentry arguments.
 *
 * Setting and removing xattr requires exclusive lock on dentry->d_inode.
 * Some hooks already locked d_inode, while some hooks have not locked
 * d_inode. Therefore, we need different kfuncs for different hooks.
 * Specifically, hooks in the following list (d_inode_locked_hooks)
 * should call bpf_[set|remove]_dentry_xattr_locked; while other hooks
 * should call bpf_[set|remove]_dentry_xattr.
 */
BTF_SET_START(d_inode_locked_hooks)
BTF_ID(func, bpf_lsm_inode_post_removexattr)
BTF_ID(func, bpf_lsm_inode_post_setattr)
BTF_ID(func, bpf_lsm_inode_post_setxattr)
BTF_ID(func, bpf_lsm_inode_removexattr)
BTF_ID(func, bpf_lsm_inode_rmdir)
BTF_ID(func, bpf_lsm_inode_setattr)
BTF_ID(func, bpf_lsm_inode_setxattr)
BTF_ID(func, bpf_lsm_inode_unlink)
#ifdef CONFIG_SECURITY_PATH
BTF_ID(func, bpf_lsm_path_unlink)
BTF_ID(func, bpf_lsm_path_rmdir)
#endif /* CONFIG_SECURITY_PATH */
BTF_SET_END(d_inode_locked_hooks)

bool bpf_lsm_has_d_inode_locked(const struct bpf_prog *prog)
{
	return btf_id_set_contains(&d_inode_locked_hooks, prog->aux->attach_btf_id);
}

static const struct btf_kfunc_id_set bpf_fs_kfunc_set = {
	.owner = THIS_MODULE,
	.set = &bpf_fs_kfunc_set_ids,
	.filter = bpf_fs_kfuncs_filter,
};

static int __init bpf_fs_kfuncs_init(void)
{
	return register_btf_kfunc_id_set(BPF_PROG_TYPE_LSM, &bpf_fs_kfunc_set);
}

late_initcall(bpf_fs_kfuncs_init);
