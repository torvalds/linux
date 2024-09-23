// SPDX-License-Identifier: GPL-2.0
/*
 * security/tomoyo/init.c
 *
 * Copyright (C) 2005-2011  NTT DATA CORPORATION
 */

#include <linux/lsm_hooks.h>
#include <uapi/linux/lsm.h>
#include "common.h"

#include "hooks.h"

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

DEFINE_LSM(tomoyo) = {
	.name = "tomoyo",
	.enabled = &tomoyo_enabled,
	.flags = LSM_FLAG_LEGACY_MAJOR,
	.blobs = &tomoyo_blob_sizes,
	.init = tomoyo_init,
};
