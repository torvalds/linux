#include <linux/ptrace.h>
#include <linux/version.h>
#include <uapi/linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "trace_common.h"

struct {
	__uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(u32));
	__uint(max_entries, 2);
} my_map SEC(".maps");

SEC("kprobe/" SYSCALL(sys_write))
int bpf_prog1(struct pt_regs *ctx)
{
	struct S {
		u64 pid;
		u64 cookie;
	} data;

	data.pid = bpf_get_current_pid_tgid();
	data.cookie = 0x12345678;

	bpf_perf_event_output(ctx, &my_map, 0, &data, sizeof(data));

	return 0;
}

char _license[] SEC("license") = "GPL";
u32 _version SEC("version") = LINUX_VERSION_CODE;
