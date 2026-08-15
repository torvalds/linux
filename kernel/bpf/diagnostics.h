/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#ifndef __BPF_DIAGNOSTICS_H
#define __BPF_DIAGNOSTICS_H

#include <linux/compiler_attributes.h>
#include <linux/stdarg.h>
#include <linux/types.h>

struct bpf_func_state;
struct bpf_reg_state;
struct bpf_verifier_env;
struct bpf_verifier_state;
struct btf;

enum bpf_diag_mod_reason {
	BPF_DIAG_MOD_WRITE,
	BPF_DIAG_MOD_SPILL,
	BPF_DIAG_MOD_VAR_WRITE,
	BPF_DIAG_MOD_REF_RELEASE,
	BPF_DIAG_MOD_PKT_DATA_CHANGE,
	BPF_DIAG_MOD_NON_OWN_REF,
	BPF_DIAG_MOD_CALLER_SAVED,
};

enum bpf_diag_context_kind {
	BPF_DIAG_CONTEXT_NONE,
	BPF_DIAG_CONTEXT_RCU,
	BPF_DIAG_CONTEXT_PREEMPT,
	BPF_DIAG_CONTEXT_IRQ,
	BPF_DIAG_CONTEXT_LOCK,
};

bool bpf_diag_enabled(const struct bpf_verifier_env *env);
int bpf_diag_init(struct bpf_verifier_env *env);
void bpf_diag_init_frame(struct bpf_verifier_env *env, struct bpf_func_state *state);
char *bpf_diag_fmt_buf(struct bpf_verifier_env *env, size_t size);
const char *bpf_diag_vfmt(struct bpf_verifier_env *env, const char *fmt, va_list args)
	__printf(2, 0);
const char *bpf_diag_fmt(struct bpf_verifier_env *env, const char *fmt, ...) __printf(2, 3);
const char *bpf_diag_fmt_btf_type(struct bpf_verifier_env *env, const struct btf *btf, u32 type_id);
u64 bpf_diag_event_log_save(struct bpf_verifier_env *env);
void bpf_diag_event_log_restore(struct bpf_verifier_env *env, u64 log_pos);
u32 bpf_diag_irq_depth(const struct bpf_verifier_state *state);
void bpf_diag_free(struct bpf_verifier_env *env);
void bpf_diag_record_branch(struct bpf_verifier_env *env, u32 insn_idx, bool cond_true);
void bpf_diag_mod_begin(struct bpf_verifier_env *env, const struct bpf_reg_state *reg,
			const struct bpf_reg_state *origin, enum bpf_diag_mod_reason reason);
void bpf_diag_mod_end(struct bpf_verifier_env *env);
void bpf_diag_record_scrub(struct bpf_verifier_env *env, const struct bpf_reg_state *reg,
			   enum bpf_diag_mod_reason reason);
void bpf_diag_record_scrub_stack(struct bpf_verifier_env *env,
				 const struct bpf_func_state *state, s16 min_off, s16 max_off,
				 enum bpf_diag_mod_reason reason);
void bpf_diag_record_ref_acquire(struct bpf_verifier_env *env, u32 insn_idx, u32 ref_id);
void bpf_diag_record_ref_release(struct bpf_verifier_env *env, u32 insn_idx, u32 ref_id);
void bpf_diag_record_context(struct bpf_verifier_env *env, u32 insn_idx,
			     enum bpf_diag_context_kind ctx_kind, bool enter, u32 depth);

#endif /* __BPF_DIAGNOSTICS_H */
