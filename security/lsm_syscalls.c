// SPDX-License-Identifier: GPL-2.0-only
/*
 * System calls implementing the Linux Security Module API.
 *
 *  Copyright (C) 2022 Casey Schaufler <casey@schaufler-ca.com>
 *  Copyright (C) 2022 Intel Corporation
 */

#include <asm/current.h>
#include <linux/compiler_types.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/security.h>
#include <linux/stddef.h>
#include <linux/syscalls.h>
#include <linux/types.h>
#include <linux/lsm_hooks.h>
#include <uapi/linux/lsm.h>

/**
 * lsm_name_to_attr - map an LSM attribute name to its ID
 * @name: name of the attribute
 *
 * Returns the LSM attribute value associated with @name, or 0 if
 * there is no mapping.
 */
u64 lsm_name_to_attr(const char *name)
{
	if (!strcmp(name, "current"))
		return LSM_ATTR_CURRENT;
	if (!strcmp(name, "exec"))
		return LSM_ATTR_EXEC;
	if (!strcmp(name, "fscreate"))
		return LSM_ATTR_FSCREATE;
	if (!strcmp(name, "keycreate"))
		return LSM_ATTR_KEYCREATE;
	if (!strcmp(name, "prev"))
		return LSM_ATTR_PREV;
	if (!strcmp(name, "sockcreate"))
		return LSM_ATTR_SOCKCREATE;
	return LSM_ATTR_UNDEF;
}

/**
 * sys_lsm_set_self_attr - Set current task's security module attribute
 * @attr: which attribute to set
 * @ctx: the LSM contexts
 * @size: size of @ctx
 * @flags: reserved for future use
 *
 * Sets the calling task's LSM context. On success this function
 * returns 0. If the attribute specified cannot be set a negative
 * value indicating the reason for the error is returned.
 */
SYSCALL_DEFINE4(lsm_set_self_attr, unsigned int, attr, struct lsm_ctx __user *,
		ctx, u32, size, u32, flags)
{
	return security_setselfattr(attr, ctx, size, flags);
}

/**
 * sys_lsm_get_self_attr - Return current task's security module attributes
 * @attr: which attribute to return
 * @ctx: the user-space destination for the information, or NULL
 * @size: pointer to the size of space available to receive the data
 * @flags: special handling options. LSM_FLAG_SINGLE indicates that only
 * attributes associated with the LSM identified in the passed @ctx be
 * reported.
 *
 * Returns the calling task's LSM contexts. On success this
 * function returns the number of @ctx array elements. This value
 * may be zero if there are no LSM contexts assigned. If @size is
 * insufficient to contain the return data -E2BIG is returned and
 * @size is set to the minimum required size. In all other cases
 * a negative value indicating the error is returned.
 */
SYSCALL_DEFINE4(lsm_get_self_attr, unsigned int, attr, struct lsm_ctx __user *,
		ctx, u32 __user *, size, u32, flags)
{
	return security_getselfattr(attr, ctx, size, flags);
}

/**
 * sys_lsm_list_modules - Return a list of the active security modules
 * @ids: the LSM module ids
 * @size: pointer to size of @ids, updated on return
 * @flags: reserved for future use, must be zero
 *
 * Returns a list of the active LSM ids. On success this function
 * returns the number of @ids array elements. This value may be zero
 * if there are no LSMs active. If @size is insufficient to contain
 * the return data -E2BIG is returned and @size is set to the minimum
 * required size. In all other cases a negative value indicating the
 * error is returned.
 */
SYSCALL_DEFINE3(lsm_list_modules, u64 __user *, ids, u32 __user *, size,
		u32, flags)
{
	u32 total_size = lsm_active_cnt * sizeof(*ids);
	u32 usize;
	int i;

	if (flags)
		return -EINVAL;

	if (get_user(usize, size))
		return -EFAULT;

	if (put_user(total_size, size) != 0)
		return -EFAULT;

	if (usize < total_size)
		return -E2BIG;

	for (i = 0; i < lsm_active_cnt; i++)
		if (put_user(lsm_idlist[i]->id, ids++))
			return -EFAULT;

	return lsm_active_cnt;
}
