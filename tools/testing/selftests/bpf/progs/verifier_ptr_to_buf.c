// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

SEC("iter/bpf_map_elem")
__description("PTR_TO_BUF: reject negative const offset")
__failure
__msg("invalid negative rdwr buffer offset")
__naked void ptr_to_buf_reject_negative_const_offset(void)
{
	asm volatile ("r0 = 0;					\
	 r2 = *(u64 *)(r1 + %[value_off]);			\
	 if r2 == 0 goto l0_%=;					\
	 r2 += -8;						\
	 r0 = *(u64 *)(r2 + 0);					\
l0_%=:								\
	 exit;							\
	"
	:
	: __imm_const(value_off,
		      offsetof(struct bpf_iter__bpf_map_elem, value))
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
