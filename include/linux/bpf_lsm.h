/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (C) 2020 Google LLC.
 */

#ifndef _LINUX_BPF_LSM_H
#define _LINUX_BPF_LSM_H

#include <linux/sched.h>
#include <linux/bpf.h>
#include <linux/bpf_verifier.h>
#include <linux/lsm_hooks.h>

#ifdef CONFIG_BPF_LSM

#define LSM_HOOK(RET, DEFAULT, NAME, ...) \
	RET bpf_lsm_##NAME(__VA_ARGS__);
#include <linux/lsm_hook_defs.h>
#undef LSM_HOOK

struct bpf_storage_blob {
	struct bpf_local_storage __rcu *storage;
};

extern struct lsm_blob_sizes bpf_lsm_blob_sizes;

int bpf_lsm_verify_prog(struct bpf_verifier_log *vlog,
			const struct bpf_prog *prog);

bool bpf_lsm_is_sleepable_hook(u32 btf_id);
bool bpf_lsm_is_trusted(const struct bpf_prog *prog);

static inline struct bpf_storage_blob *bpf_inode(
	const struct inode *inode)
{
	if (unlikely(!inode->i_security))
		return NULL;

	return inode->i_security + bpf_lsm_blob_sizes.lbs_inode;
}

extern const struct bpf_func_proto bpf_inode_storage_get_proto;
extern const struct bpf_func_proto bpf_inode_storage_delete_proto;
void bpf_inode_storage_free(struct inode *inode);

void bpf_lsm_find_cgroup_shim(const struct bpf_prog *prog, bpf_func_t *bpf_func);

int bpf_lsm_get_retval_range(const struct bpf_prog *prog,
			     struct bpf_retval_range *range);
int bpf_set_dentry_xattr_locked(struct dentry *dentry, const char *name__str,
				const struct bpf_dynptr *value_p, int flags);
int bpf_remove_dentry_xattr_locked(struct dentry *dentry, const char *name__str);
bool bpf_lsm_has_d_inode_locked(const struct bpf_prog *prog);

#else /* !CONFIG_BPF_LSM */

static inline bool bpf_lsm_is_sleepable_hook(u32 btf_id)
{
	return false;
}

static inline bool bpf_lsm_is_trusted(const struct bpf_prog *prog)
{
	return false;
}

static inline int bpf_lsm_verify_prog(struct bpf_verifier_log *vlog,
				      const struct bpf_prog *prog)
{
	return -EOPNOTSUPP;
}

static inline struct bpf_storage_blob *bpf_inode(
	const struct inode *inode)
{
	return NULL;
}

static inline void bpf_inode_storage_free(struct inode *inode)
{
}

static inline void bpf_lsm_find_cgroup_shim(const struct bpf_prog *prog,
					   bpf_func_t *bpf_func)
{
}

static inline int bpf_lsm_get_retval_range(const struct bpf_prog *prog,
					   struct bpf_retval_range *range)
{
	return -EOPNOTSUPP;
}
static inline int bpf_set_dentry_xattr_locked(struct dentry *dentry, const char *name__str,
					      const struct bpf_dynptr *value_p, int flags)
{
	return -EOPNOTSUPP;
}
static inline int bpf_remove_dentry_xattr_locked(struct dentry *dentry, const char *name__str)
{
	return -EOPNOTSUPP;
}
static inline bool bpf_lsm_has_d_inode_locked(const struct bpf_prog *prog)
{
	return false;
}
#endif /* CONFIG_BPF_LSM */

#endif /* _LINUX_BPF_LSM_H */
