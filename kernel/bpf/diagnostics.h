/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#ifndef __BPF_DIAGNOSTICS_H
#define __BPF_DIAGNOSTICS_H

#include <linux/compiler_attributes.h>
#include <linux/stdarg.h>
#include <linux/types.h>

struct bpf_verifier_env;

bool bpf_diag_enabled(const struct bpf_verifier_env *env);
int bpf_diag_init(struct bpf_verifier_env *env);
char *bpf_diag_fmt_buf(struct bpf_verifier_env *env, size_t size);
const char *bpf_diag_vfmt(struct bpf_verifier_env *env, const char *fmt, va_list args)
	__printf(2, 0);
const char *bpf_diag_fmt(struct bpf_verifier_env *env, const char *fmt, ...) __printf(2, 3);
u64 bpf_diag_event_log_save(struct bpf_verifier_env *env);
void bpf_diag_event_log_restore(struct bpf_verifier_env *env, u64 log_pos);
void bpf_diag_free(struct bpf_verifier_env *env);
void bpf_diag_record_branch(struct bpf_verifier_env *env, u32 insn_idx, bool cond_true);

#endif /* __BPF_DIAGNOSTICS_H */
