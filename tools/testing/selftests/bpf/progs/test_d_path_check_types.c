// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

extern const int bpf_prog_active __ksym;

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 1 << 12);
} ringbuf SEC(".maps");

SEC("fentry/security_inode_getattr")
int BPF_PROG(d_path_check_rdonly_mem, struct path *path, struct kstat *stat,
	     __u32 request_mask, unsigned int query_flags)
{
	void *active;
	u32 cpu;

	cpu = bpf_get_smp_processor_id();
	active = (void *)bpf_per_cpu_ptr(&bpf_prog_active, cpu);
	if (active) {
		/* FAIL here! 'active' points to 'regular' memory. It
		 * cannot be submitted to ring buffer.
		 */
		bpf_ringbuf_submit(active, 0);
	}
	return 0;
}

char _license[] SEC("license") = "GPL";
