// SPDX-License-Identifier: GPL-2.0-only

#include <stdbool.h>

#define UNPRIV_SYSCTL "kernel/unprivileged_bpf_disabled"

bool get_unpriv_disabled(void);
bool get_unpriv_sysctl_disabled(void);
bool get_unpriv_mitigations_disabled(void);
bool get_kasan_jit_enabled(void);
bool get_kasan_multi_shot_enabled(void);
