// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * LSM functions
 */

#ifndef _LSM_H_
#define _LSM_H_

#include <linux/printk.h>
#include <linux/lsm_hooks.h>
#include <linux/lsm_count.h>

/* LSM debugging */
extern bool lsm_debug;
#define lsm_pr(...)		pr_info(__VA_ARGS__)
#define lsm_pr_cont(...)	pr_cont(__VA_ARGS__)
#define lsm_pr_dbg(...)							\
	do {								\
		if (lsm_debug)						\
			pr_info(__VA_ARGS__);				\
	} while (0)

/* List of configured LSMs */
extern unsigned int lsm_active_cnt;
extern const struct lsm_id *lsm_idlist[];

/* LSM blob configuration */
extern struct lsm_blob_sizes blob_sizes;

/* LSM blob caches */
extern struct kmem_cache *lsm_file_cache;
extern struct kmem_cache *lsm_inode_cache;

/* LSM blob allocators */
int lsm_cred_alloc(struct cred *cred, gfp_t gfp);
int lsm_task_alloc(struct task_struct *task);

/* LSM framework initializers */

#ifdef CONFIG_SECURITYFS
int securityfs_init(void);
#else
static inline int securityfs_init(void)
{
	return 0;
}
#endif /* CONFIG_SECURITYFS */

#endif /* _LSM_H_ */
