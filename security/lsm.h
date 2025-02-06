// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * LSM functions
 */

#ifndef _LSM_H_
#define _LSM_H_

#include <linux/lsm_hooks.h>

/* LSM blob configuration */
extern struct lsm_blob_sizes blob_sizes;

/* LSM blob caches */
extern struct kmem_cache *lsm_file_cache;
extern struct kmem_cache *lsm_inode_cache;

/* LSM blob allocators */
int lsm_cred_alloc(struct cred *cred, gfp_t gfp);
int lsm_task_alloc(struct task_struct *task);

#endif /* _LSM_H_ */
