// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_DEVMAP);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(struct bpf_devmap_val));
	__uint(max_entries, 4);
} dm_ports SEC(".maps");

SEC("xdp")
int xdp_redir_prog(struct xdp_md *ctx)
{
	return bpf_redirect_map(&dm_ports, 0, 0);
}

/* invalid program on DEVMAP entry;
 * SEC name means expected attach type not set
 */
SEC("xdp")
int xdp_dummy_prog(struct xdp_md *ctx)
{
	return XDP_PASS;
}

/* valid program on DEVMAP entry via SEC name;
 * has access to egress and ingress ifindex
 */
SEC("xdp/devmap")
int xdp_dummy_dm(struct xdp_md *ctx)
{
	char fmt[] = "devmap redirect: dev %u -> dev %u len %u\n";
	void *data_end = (void *)(long)ctx->data_end;
	void *data = (void *)(long)ctx->data;
	unsigned int len = data_end - data;

	bpf_trace_printk(fmt, sizeof(fmt),
			 ctx->ingress_ifindex, ctx->egress_ifindex, len);

	return XDP_PASS;
}

SEC("xdp.frags/devmap")
int xdp_dummy_dm_frags(struct xdp_md *ctx)
{
	return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
