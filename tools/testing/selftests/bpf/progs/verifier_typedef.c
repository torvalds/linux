// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

SEC("fentry/bpf_fentry_test_sinfo")
__description("typedef: resolve")
__success __retval(0)
__naked void resolve_typedef(void)
{
	asm volatile ("					\
	r1 = *(u64 *)(r1 +0);				\
	r2 = *(u64 *)(r1 +%[frags_offs]);		\
	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(frags_offs,
		      offsetof(struct skb_shared_info, frags))
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
