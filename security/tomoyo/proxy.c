// SPDX-License-Identifier: GPL-2.0
/*
 * security/tomoyo/proxy.c
 *
 * Copyright (C) 2005-2011  NTT DATA CORPORATION
 */

#include <linux/security.h>
#include "common.h"

#ifdef CONFIG_SECURITY_TOMOYO_LKM

struct tomoyo_task *tomoyo_task(struct task_struct *task)
{
	struct tomoyo_task *s = task->security + tomoyo_blob_sizes.lbs_task;

	if (unlikely(!s->domain_info)) {
		if (likely(task == current)) {
			s->domain_info = &tomoyo_kernel_domain;
			atomic_inc(&tomoyo_kernel_domain.users);
		} else {
			/* Caller handles s->domain_info == NULL case. */
		}
	}
	return s;
}

#include "hooks.h"

/**
 * tomoyo_runtime_init - Register TOMOYO Linux as a loadable LSM module.
 *
 * Returns 0 if TOMOYO is enabled, -EINVAL otherwise.
 */
static int __init tomoyo_runtime_init(void)
{
	const struct tomoyo_hooks tomoyo_hooks = {
		.cred_prepare = tomoyo_cred_prepare,
		.bprm_committed_creds = tomoyo_bprm_committed_creds,
		.task_alloc = tomoyo_task_alloc,
		.task_free = tomoyo_task_free,
		.bprm_check_security = tomoyo_bprm_check_security,
		.file_fcntl = tomoyo_file_fcntl,
		.file_open = tomoyo_file_open,
		.file_truncate = tomoyo_file_truncate,
		.path_truncate = tomoyo_path_truncate,
		.path_unlink = tomoyo_path_unlink,
		.path_mkdir = tomoyo_path_mkdir,
		.path_rmdir = tomoyo_path_rmdir,
		.path_symlink = tomoyo_path_symlink,
		.path_mknod = tomoyo_path_mknod,
		.path_link = tomoyo_path_link,
		.path_rename = tomoyo_path_rename,
		.inode_getattr = tomoyo_inode_getattr,
		.file_ioctl = tomoyo_file_ioctl,
		.file_ioctl_compat = tomoyo_file_ioctl,
		.path_chmod = tomoyo_path_chmod,
		.path_chown = tomoyo_path_chown,
		.path_chroot = tomoyo_path_chroot,
		.sb_mount = tomoyo_sb_mount,
		.sb_umount = tomoyo_sb_umount,
		.sb_pivotroot = tomoyo_sb_pivotroot,
		.socket_bind = tomoyo_socket_bind,
		.socket_connect = tomoyo_socket_connect,
		.socket_listen = tomoyo_socket_listen,
		.socket_sendmsg = tomoyo_socket_sendmsg,
	};

	if (!tomoyo_ops.enabled)
		return -EINVAL;
	tomoyo_ops.check_profile = tomoyo_check_profile;
	pr_info("TOMOYO Linux initialized\n");
	tomoyo_task(current);
	tomoyo_mm_init();
	tomoyo_interface_init();
	tomoyo_register_hooks(&tomoyo_hooks);
	return 0;
}
module_init(tomoyo_runtime_init);
MODULE_LICENSE("GPL");

#endif
