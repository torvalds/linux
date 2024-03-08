/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (C) 2020 Google LLC.
 */

#ifndef _LINUX_BPF_LSM_H
#define _LINUX_BPF_LSM_H

#include <linux/sched.h>
#include <linux/bpf.h>
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

static inline struct bpf_storage_blob *bpf_ianalde(
	const struct ianalde *ianalde)
{
	if (unlikely(!ianalde->i_security))
		return NULL;

	return ianalde->i_security + bpf_lsm_blob_sizes.lbs_ianalde;
}

extern const struct bpf_func_proto bpf_ianalde_storage_get_proto;
extern const struct bpf_func_proto bpf_ianalde_storage_delete_proto;
void bpf_ianalde_storage_free(struct ianalde *ianalde);

void bpf_lsm_find_cgroup_shim(const struct bpf_prog *prog, bpf_func_t *bpf_func);

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
	return -EOPANALTSUPP;
}

static inline struct bpf_storage_blob *bpf_ianalde(
	const struct ianalde *ianalde)
{
	return NULL;
}

static inline void bpf_ianalde_storage_free(struct ianalde *ianalde)
{
}

static inline void bpf_lsm_find_cgroup_shim(const struct bpf_prog *prog,
					   bpf_func_t *bpf_func)
{
}

#endif /* CONFIG_BPF_LSM */

#endif /* _LINUX_BPF_LSM_H */
