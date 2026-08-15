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
void bpf_diag_free(struct bpf_verifier_env *env);

#endif /* __BPF_DIAGNOSTICS_H */
