/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Linux Security Module infrastructure tests
 *
 * Copyright © 2023 Casey Schaufler <casey@schaufler-ca.com>
 */

#ifndef lsm_get_self_attr
static inline int lsm_get_self_attr(unsigned int attr, struct lsm_ctx *ctx,
				    __u32 *size, __u32 flags)
{
	return syscall(__NR_lsm_get_self_attr, attr, ctx, size, flags);
}
#endif

#ifndef lsm_set_self_attr
static inline int lsm_set_self_attr(unsigned int attr, struct lsm_ctx *ctx,
				    __u32 size, __u32 flags)
{
	return syscall(__NR_lsm_set_self_attr, attr, ctx, size, flags);
}
#endif

#ifndef lsm_list_modules
static inline int lsm_list_modules(__u64 *ids, __u32 *size, __u32 flags)
{
	return syscall(__NR_lsm_list_modules, ids, size, flags);
}
#endif

extern int read_proc_attr(const char *attr, char *value, size_t size);
extern int read_sysfs_lsms(char *lsms, size_t size);
int attr_lsm_count(void);
