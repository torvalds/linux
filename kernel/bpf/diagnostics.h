/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#ifndef __BPF_DIAGNOSTICS_H
#define __BPF_DIAGNOSTICS_H

#include <linux/compiler_attributes.h>
#include <linux/types.h>

struct bpf_verifier_env;

bool bpf_diag_enabled(const struct bpf_verifier_env *env);

#endif /* __BPF_DIAGNOSTICS_H */
