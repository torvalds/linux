/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (C) 2020 Google LLC.
 */

#ifndef _LINUX_BPF_LSM_H
#define _LINUX_BPF_LSM_H

#include <linux/bpf.h>
#include <linux/lsm_hooks.h>

#ifdef CONFIG_BPF_LSM

#define LSM_HOOK(RET, DEFAULT, NAME, ...) \
	RET bpf_lsm_##NAME(__VA_ARGS__);
#include <linux/lsm_hook_defs.h>
#undef LSM_HOOK

int bpf_lsm_verify_prog(struct bpf_verifier_log *vlog,
			const struct bpf_prog *prog);

#else /* !CONFIG_BPF_LSM */

static inline int bpf_lsm_verify_prog(struct bpf_verifier_log *vlog,
				      const struct bpf_prog *prog)
{
	return -EOPNOTSUPP;
}

#endif /* CONFIG_BPF_LSM */

#endif /* _LINUX_BPF_LSM_H */
