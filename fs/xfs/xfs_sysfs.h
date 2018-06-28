// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2014 Red Hat, Inc.
 * All Rights Reserved.
 */

#ifndef __XFS_SYSFS_H__
#define __XFS_SYSFS_H__

extern struct kobj_type xfs_mp_ktype;	/* xfs_mount */
extern struct kobj_type xfs_dbg_ktype;	/* debug */
extern struct kobj_type xfs_log_ktype;	/* xlog */
extern struct kobj_type xfs_stats_ktype;	/* stats */

static inline struct xfs_kobj *
to_kobj(struct kobject *kobject)
{
	return container_of(kobject, struct xfs_kobj, kobject);
}

static inline void
xfs_sysfs_release(struct kobject *kobject)
{
	struct xfs_kobj *kobj = to_kobj(kobject);
	complete(&kobj->complete);
}

static inline int
xfs_sysfs_init(
	struct xfs_kobj		*kobj,
	struct kobj_type	*ktype,
	struct xfs_kobj		*parent_kobj,
	const char		*name)
{
	init_completion(&kobj->complete);
	return kobject_init_and_add(&kobj->kobject, ktype,
				    &parent_kobj->kobject, "%s", name);
}

static inline void
xfs_sysfs_del(
	struct xfs_kobj	*kobj)
{
	kobject_del(&kobj->kobject);
	kobject_put(&kobj->kobject);
	wait_for_completion(&kobj->complete);
}

int	xfs_error_sysfs_init(struct xfs_mount *mp);
void	xfs_error_sysfs_del(struct xfs_mount *mp);

#endif	/* __XFS_SYSFS_H__ */
