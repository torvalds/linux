#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/pkt_cls.h>

#include <bpf/bpf_helpers.h>

#define META_SIZE 32

#define ctx_ptr(ctx, mem) (void *)(unsigned long)ctx->mem

/* Demonstrates how metadata can be passed from an XDP program to a TC program
 * using bpf_xdp_adjust_meta.
 * For the sake of testing the metadata support in drivers, the XDP program uses
 * a fixed-size payload after the Ethernet header as metadata. The TC program
 * copies the metadata it receives into a map so it can be checked from
 * userspace.
 */

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__uint(value_size, META_SIZE);
} test_result SEC(".maps");

SEC("tc")
int ing_cls(struct __sk_buff *ctx)
{
	__u8 *data, *data_meta;
	__u32 key = 0;

	data_meta = ctx_ptr(ctx, data_meta);
	data      = ctx_ptr(ctx, data);

	if (data_meta + META_SIZE > data)
		return TC_ACT_SHOT;

	bpf_map_update_elem(&test_result, &key, data_meta, BPF_ANY);

	return TC_ACT_SHOT;
}

SEC("xdp")
int ing_xdp(struct xdp_md *ctx)
{
	__u8 *data, *data_meta, *data_end, *payload;
	struct ethhdr *eth;
	int ret;

	ret = bpf_xdp_adjust_meta(ctx, -META_SIZE);
	if (ret < 0)
		return XDP_DROP;

	data_meta = ctx_ptr(ctx, data_meta);
	data_end  = ctx_ptr(ctx, data_end);
	data      = ctx_ptr(ctx, data);

	eth = (struct ethhdr *)data;
	payload = data + sizeof(struct ethhdr);

	if (payload + META_SIZE > data_end ||
	    data_meta + META_SIZE > data)
		return XDP_DROP;

	/* The Linux networking stack may send other packets on the test
	 * interface that interfere with the test. Just drop them.
	 * The test packets can be recognized by their ethertype of zero.
	 */
	if (eth->h_proto != 0)
		return XDP_DROP;

	__builtin_memcpy(data_meta, payload, META_SIZE);
	return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
