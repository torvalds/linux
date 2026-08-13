// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

/* Used for testing map name. */
int loong SEC(".percpu.looooooooong");
int data3 SEC(".data.percpu");
int data2 SEC(".percpu.data");

int run;
/* cpu_id as array to verify map value resizing. */
int cpu_id[1] SEC(".percpu");
int data SEC(".percpu") = -1;
int nums[7] SEC(".percpu");
bool set SEC(".percpu") = false;
struct {
	char set;
	int i;
	int nums[7];
} struct_data SEC(".percpu") = {
	.set = 0,
	.i = -1,
};

SEC("raw_tp/task_rename")
__auxiliary
int update_percpu_data(void *ctx)
{
	struct_data.nums[6] = 0xc0de;
	struct_data.set = 1;
	struct_data.i = 1;
	nums[6] = 0xc0de;
	data = 1;
	run++;
	set = true;
	cpu_id[0] = bpf_get_smp_processor_id();
	return 0;
}

char _license[] SEC("license") = "GPL";
