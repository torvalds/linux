/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Kernel features the loader probes before load and hands to the BPF side in
 * one rodata word, so that a program can carry both the path a feature enables
 * and its fallback while the verifier prunes the one the running kernel cannot
 * take. scx_utils sets the word in the ops open macros. The bits are mirrored
 * in rust/scx_utils/src/compat.rs.
 *
 * Included by scx/common.bpf.h; don't include directly.
 *
 * Copyright (c) 2026 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2026 Tejun Heo <tj@kernel.org>
 */
#ifndef __SCX_FEATURES_BPF_H
#define __SCX_FEATURES_BPF_H

enum scx_lib_feature {
	/* the JIT lowers fetching AND, OR and XOR on arena pointers */
	SCX_LIB_FEAT_ARENA_FETCH_BITOPS		= 1ULL << 0,
};

const volatile u64 scx_lib_features __weak;

static __always_inline bool scx_lib_has(u64 feat)
{
	return scx_lib_features & feat;
}

#endif	/* __SCX_FEATURES_BPF_H */
