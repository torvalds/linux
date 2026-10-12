// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

/* Used for testing map name. */
int loong SEC(".percpu.looooooooong");
int data3 SEC(".data.percpu");
int data2 SEC(".percpu.data");

/* Used for testing bpf_map__set_value_size(). */
int arr[1] SEC(".percpu.arr");
int arr_sum;

int run;
int cpu_id SEC(".percpu");
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
	cpu_id = bpf_get_smp_processor_id();
	arr_sum += arr[0];
	return 0;
}

static const char fmt[] SEC(".percpu.fmt") = "data %d\n";

SEC("?kprobe")
__failure __msg("R{{[0-9]+}} points to percpu_array map which cannot be used as const string")
int verifier_strncmp(void *ctx)
{
	return bpf_strncmp("test", 5, fmt);
}

SEC("?kprobe")
__failure __msg("R{{[0-9]+}} points to percpu_array map which cannot be used as const string")
int verifier_snprintf(void *ctx)
{
	u64 args[] = { data };
	char buf[128];
	int len;

	len = bpf_snprintf(buf, sizeof(buf), fmt, args, sizeof(args));
	if (len > 0)
		bpf_printk("snprintf: %s\n", buf);
	return 0;
}

volatile const __u32 num_cpus = 0;
volatile const int num_off;
volatile const int elem_sz;
__u32 sum = 0;
bool run_iter = false;

SEC("iter/bpf_map_elem")
__auxiliary
int dump_percpu_data(struct bpf_iter__bpf_map_elem *ctx)
{
	void *pptr = ctx->value;
	int i;

	if (!pptr)
		return 0;

	run_iter = true;

	for (i = 0; i < num_cpus; i++) {
		sum += *(int *) (pptr + num_off);
		pptr += elem_sz;
	}
	return 0;
}

char _license[] SEC("license") = "GPL";
