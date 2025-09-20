// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u32);
} map_array SEC(".maps");

SEC("socket")
__description("invalid map type for tail call")
__failure __msg("expected prog array map for tail call")
__failure_unpriv
__naked void invalid_map_for_tail_call(void)
{
	asm volatile ("			\
	r2 = %[map_array] ll;	\
	r3 = 0;				\
	call %[bpf_tail_call];		\
	exit;				\
"	:
	: __imm(bpf_tail_call),
	  __imm_addr(map_array)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
