// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

__u64 cpu_nr_result;

SEC("raw_tp")
void call_bpf_get_smp_processor_id(void)
{
	register __u64 r0 asm("r0") = -1;
	asm volatile ("call %[bpf_get_smp_processor_id];"
		      : "+r"(r0)
		      : __imm(bpf_get_smp_processor_id)
		      : "r1", "r2", "r3", "r4", "r5", "memory");
	cpu_nr_result = r0;
}

char _license[] SEC("license") = "GPL";
