#include <linux/ptrace.h>
#include <linux/version.h>
#include <uapi/linux/bpf.h>
#include "bpf_helpers.h"

struct bpf_map_def SEC("maps") my_map = {
	.type = BPF_MAP_TYPE_PERF_EVENT_ARRAY,
	.key_size = sizeof(int),
	.value_size = sizeof(u32),
	.max_entries = 32,
};

SEC("kprobe/sys_write")
int bpf_prog1(struct pt_regs *ctx)
{
	u64 count;
	u32 key = bpf_get_smp_processor_id();
	char fmt[] = "CPU-%d   %llu\n";

	count = bpf_perf_event_read(&my_map, key);
	bpf_trace_printk(fmt, sizeof(fmt), key, count);

	return 0;
}

char _license[] SEC("license") = "GPL";
u32 _version SEC("version") = LINUX_VERSION_CODE;
