// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023. Huawei Technologies Co., Ltd */
#include <linux/types.h>
#include <bpf/bpf_helpers.h>

#include "bpf_experimental.h"
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

struct bin_data {
	char blob[32];
};

#define private(name) SEC(".bss." #name) __hidden __attribute__((aligned(8)))
private(kptr) struct bin_data __kptr * ptr;

SEC("tc")
__naked int kptr_xchg_inline(void)
{
	asm volatile (
		"r1 = %[ptr] ll;"
		"r2 = 0;"
		"call %[bpf_kptr_xchg];"
		"if r0 == 0 goto 1f;"
		"r1 = r0;"
		"r2 = 0;"
		"call %[bpf_obj_drop_impl];"
	"1:"
		"r0 = 0;"
		"exit;"
		:
		: __imm_addr(ptr),
		  __imm(bpf_kptr_xchg),
		  __imm(bpf_obj_drop_impl)
		: __clobber_all
	);
}

/* BTF FUNC records are not generated for kfuncs referenced
 * from inline assembly. These records are necessary for
 * libbpf to link the program. The function below is a hack
 * to ensure that BTF FUNC records are generated.
 */
void __btf_root(void)
{
	bpf_obj_drop(NULL);
}
