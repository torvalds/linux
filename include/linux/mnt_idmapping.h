/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MNT_IDMAPPING_H
#define _LINUX_MNT_IDMAPPING_H

#include <linux/types.h>
#include <linux/uidgid.h>

struct user_namespace;
extern struct user_namespace init_user_ns;

/**
 * kuid_into_mnt - map a kuid down into a mnt_userns
 * @mnt_userns: user namespace of the relevant mount
 * @kuid: kuid to be mapped
 *
 * Return: @kuid mapped according to @mnt_userns.
 * If @kuid has no mapping INVALID_UID is returned.
 */
static inline kuid_t kuid_into_mnt(struct user_namespace *mnt_userns,
				   kuid_t kuid)
{
	return make_kuid(mnt_userns, __kuid_val(kuid));
}

/**
 * kgid_into_mnt - map a kgid down into a mnt_userns
 * @mnt_userns: user namespace of the relevant mount
 * @kgid: kgid to be mapped
 *
 * Return: @kgid mapped according to @mnt_userns.
 * If @kgid has no mapping INVALID_GID is returned.
 */
static inline kgid_t kgid_into_mnt(struct user_namespace *mnt_userns,
				   kgid_t kgid)
{
	return make_kgid(mnt_userns, __kgid_val(kgid));
}

/**
 * kuid_from_mnt - map a kuid up into a mnt_userns
 * @mnt_userns: user namespace of the relevant mount
 * @kuid: kuid to be mapped
 *
 * Return: @kuid mapped up according to @mnt_userns.
 * If @kuid has no mapping INVALID_UID is returned.
 */
static inline kuid_t kuid_from_mnt(struct user_namespace *mnt_userns,
				   kuid_t kuid)
{
	return KUIDT_INIT(from_kuid(mnt_userns, kuid));
}

/**
 * kgid_from_mnt - map a kgid up into a mnt_userns
 * @mnt_userns: user namespace of the relevant mount
 * @kgid: kgid to be mapped
 *
 * Return: @kgid mapped up according to @mnt_userns.
 * If @kgid has no mapping INVALID_GID is returned.
 */
static inline kgid_t kgid_from_mnt(struct user_namespace *mnt_userns,
				   kgid_t kgid)
{
	return KGIDT_INIT(from_kgid(mnt_userns, kgid));
}

/**
 * mapped_fsuid - return caller's fsuid mapped up into a mnt_userns
 * @mnt_userns: user namespace of the relevant mount
 *
 * Use this helper to initialize a new vfs or filesystem object based on
 * the caller's fsuid. A common example is initializing the i_uid field of
 * a newly allocated inode triggered by a creation event such as mkdir or
 * O_CREAT. Other examples include the allocation of quotas for a specific
 * user.
 *
 * Return: the caller's current fsuid mapped up according to @mnt_userns.
 */
static inline kuid_t mapped_fsuid(struct user_namespace *mnt_userns)
{
	return kuid_from_mnt(mnt_userns, current_fsuid());
}

/**
 * mapped_fsgid - return caller's fsgid mapped up into a mnt_userns
 * @mnt_userns: user namespace of the relevant mount
 *
 * Use this helper to initialize a new vfs or filesystem object based on
 * the caller's fsgid. A common example is initializing the i_gid field of
 * a newly allocated inode triggered by a creation event such as mkdir or
 * O_CREAT. Other examples include the allocation of quotas for a specific
 * user.
 *
 * Return: the caller's current fsgid mapped up according to @mnt_userns.
 */
static inline kgid_t mapped_fsgid(struct user_namespace *mnt_userns)
{
	return kgid_from_mnt(mnt_userns, current_fsgid());
}

#endif /* _LINUX_MNT_IDMAPPING_H */
