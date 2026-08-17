// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

SEC("fentry/bpf_fentry_test1")
__success __retval(0)
__arch_x86_64
__jited("	addq	%gs:{{.*}}, %rax")
__arch_arm64
__jited("	mrs	x8, SP_EL0")
__arch_riscv64
__jited("	mv	a5, tp")
__arch_loongarch
__jited("	move	$a5, $tp")
int inline_bpf_get_current_task(void)
{
	bpf_get_current_task();

	return 0;
}

SEC("fentry/bpf_fentry_test2")
__success __retval(0)
__arch_loongarch
__jited("	ld.wu	$a5, $tp, 16")
int inline_bpf_get_smp_processor_id(void)
{
	bpf_get_smp_processor_id();

	return 0;
}

char _license[] SEC("license") = "GPL";
