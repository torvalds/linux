// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
} ringbuf SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u32));
	__uint(max_entries, 1);
} perfbuf SEC(".maps");

/*
 * Include the u32 raw size in perf's 8-byte alignment to avoid padding.
 * Keep plain output large enough to flush stdio before the consumer exits.
 */
const unsigned char perf_payload[4092] = { 0x00, 0xff };

int record;
int output_err;

SEC("socket")
int produce(struct __sk_buff *skb)
{
	unsigned char first[] = { 0x00, 0xff };
	unsigned char second[] = { 1, 2, 3, 4, 5 };
	unsigned char third[] = {
		0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
		0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20,
	};

	if (record == 0)
		output_err = bpf_ringbuf_output(&ringbuf, first, sizeof(first), 0);
	else if (record == 1)
		output_err = bpf_ringbuf_output(&ringbuf, second, sizeof(second), 0);
	else if (record == 2)
		output_err = bpf_ringbuf_output(&ringbuf, third, sizeof(third), 0);
	else
		output_err = bpf_perf_event_output(skb, &perfbuf, BPF_F_CURRENT_CPU,
						   (void *)perf_payload, sizeof(perf_payload));
	return 0;
}

char LICENSE[] SEC("license") = "GPL";
