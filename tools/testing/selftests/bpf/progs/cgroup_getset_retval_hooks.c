// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

#define BPF_RETVAL_HOOK(name, section, ctx, expected_err) \
	__attribute__((__section__("?" section))) \
	int name(struct ctx *_ctx) \
	{ \
		bpf_set_retval(bpf_get_retval()); \
		return 1; \
	}

#include "cgroup_getset_retval_hooks.h"

#undef BPF_RETVAL_HOOK
