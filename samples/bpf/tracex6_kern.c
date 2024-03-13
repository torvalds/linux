#include <linux/ptrace.h>
#include <linux/version.h>
#include <uapi/linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

struct {
	__uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(u32));
	__uint(max_entries, 64);
} counters SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, int);
	__type(value, u64);
	__uint(max_entries, 64);
} values SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, int);
	__type(value, struct bpf_perf_event_value);
	__uint(max_entries, 64);
} values2 SEC(".maps");

SEC("kprobe/htab_map_get_next_key")
int bpf_prog1(struct pt_regs *ctx)
{
	u32 key = bpf_get_smp_processor_id();
	u64 count, *val;
	s64 error;

	count = bpf_perf_event_read(&counters, key);
	error = (s64)count;
	if (error <= -2 && error >= -22)
		return 0;

	val = bpf_map_lookup_elem(&values, &key);
	if (val)
		*val = count;
	else
		bpf_map_update_elem(&values, &key, &count, BPF_NOEXIST);

	return 0;
}

/*
 * Since *_map_lookup_elem can't be expected to trigger bpf programs
 * due to potential deadlocks (bpf_disable_instrumentation), this bpf
 * program will be attached to bpf_map_copy_value (which is called
 * from map_lookup_elem) and will only filter the hashtable type.
 */
SEC("kprobe/bpf_map_copy_value")
int BPF_KPROBE(bpf_prog2, struct bpf_map *map)
{
	u32 key = bpf_get_smp_processor_id();
	struct bpf_perf_event_value *val, buf;
	enum bpf_map_type type;
	int error;

	type = BPF_CORE_READ(map, map_type);
	if (type != BPF_MAP_TYPE_HASH)
		return 0;

	error = bpf_perf_event_read_value(&counters, key, &buf, sizeof(buf));
	if (error)
		return 0;

	val = bpf_map_lookup_elem(&values2, &key);
	if (val)
		*val = buf;
	else
		bpf_map_update_elem(&values2, &key, &buf, BPF_NOEXIST);

	return 0;
}

char _license[] SEC("license") = "GPL";
u32 _version SEC("version") = LINUX_VERSION_CODE;
