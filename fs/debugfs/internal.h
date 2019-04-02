// SPDX-License-Identifier: GPL-2.0
/*
 *  internal.h - declarations internal to defs
 *
 *  Copyright (C) 2016 Nicolai Stange <nicstange@gmail.com>
 */

#ifndef _DEFS_INTERNAL_H_
#define _DEFS_INTERNAL_H_

struct file_operations;

/* declared over in file.c */
extern const struct file_operations defs_noop_file_operations;
extern const struct file_operations defs_open_proxy_file_operations;
extern const struct file_operations defs_full_proxy_file_operations;

struct defs_fsdata {
	const struct file_operations *real_fops;
	refcount_t active_users;
	struct completion active_users_drained;
};

/*
 * A dentry's ->d_fsdata either points to the real fops or to a
 * dynamically allocated defs_fsdata instance.
 * In order to distinguish between these two cases, a real fops
 * pointer gets its lowest bit set.
 */
#define DEFS_FSDATA_IS_REAL_FOPS_BIT BIT(0)

#endif /* _DEFS_INTERNAL_H_ */
