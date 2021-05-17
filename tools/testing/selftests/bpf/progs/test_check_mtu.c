// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Jesper Dangaard Brouer */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <linux/if_ether.h>

#include <stddef.h>
#include <stdint.h>

char _license[] SEC("license") = "GPL";

/* Userspace will update with MTU it can see on device */
static volatile const int GLOBAL_USER_MTU;
static volatile const __u32 GLOBAL_USER_IFINDEX;

/* BPF-prog will update these with MTU values it can see */
__u32 global_bpf_mtu_xdp = 0;
__u32 global_bpf_mtu_tc  = 0;

SEC("xdp")
int xdp_use_helper_basic(struct xdp_md *ctx)
{
	__u32 mtu_len = 0;

	if (bpf_check_mtu(ctx, 0, &mtu_len, 0, 0))
		return XDP_ABORTED;

	return XDP_PASS;
}

SEC("xdp")
int xdp_use_helper(struct xdp_md *ctx)
{
	int retval = XDP_PASS; /* Expected retval on successful test */
	__u32 mtu_len = 0;
	__u32 ifindex = 0;
	int delta = 0;

	/* When ifindex is zero, save net_device lookup and use ctx netdev */
	if (GLOBAL_USER_IFINDEX > 0)
		ifindex = GLOBAL_USER_IFINDEX;

	if (bpf_check_mtu(ctx, ifindex, &mtu_len, delta, 0)) {
		/* mtu_len is also valid when check fail */
		retval = XDP_ABORTED;
		goto out;
	}

	if (mtu_len != GLOBAL_USER_MTU)
		retval = XDP_DROP;

out:
	global_bpf_mtu_xdp = mtu_len;
	return retval;
}

SEC("xdp")
int xdp_exceed_mtu(struct xdp_md *ctx)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data = (void *)(long)ctx->data;
	__u32 ifindex = GLOBAL_USER_IFINDEX;
	__u32 data_len = data_end - data;
	int retval = XDP_ABORTED; /* Fail */
	__u32 mtu_len = 0;
	int delta;
	int err;

	/* Exceed MTU with 1 via delta adjust */
	delta = GLOBAL_USER_MTU - (data_len - ETH_HLEN) + 1;

	err = bpf_check_mtu(ctx, ifindex, &mtu_len, delta, 0);
	if (err) {
		retval = XDP_PASS; /* Success in exceeding MTU check */
		if (err != BPF_MTU_CHK_RET_FRAG_NEEDED)
			retval = XDP_DROP;
	}

	global_bpf_mtu_xdp = mtu_len;
	return retval;
}

SEC("xdp")
int xdp_minus_delta(struct xdp_md *ctx)
{
	int retval = XDP_PASS; /* Expected retval on successful test */
	void *data_end = (void *)(long)ctx->data_end;
	void *data = (void *)(long)ctx->data;
	__u32 ifindex = GLOBAL_USER_IFINDEX;
	__u32 data_len = data_end - data;
	__u32 mtu_len = 0;
	int delta;

	/* Borderline test case: Minus delta exceeding packet length allowed */
	delta = -((data_len - ETH_HLEN) + 1);

	/* Minus length (adjusted via delta) still pass MTU check, other helpers
	 * are responsible for catching this, when doing actual size adjust
	 */
	if (bpf_check_mtu(ctx, ifindex, &mtu_len, delta, 0))
		retval = XDP_ABORTED;

	global_bpf_mtu_xdp = mtu_len;
	return retval;
}

SEC("xdp")
int xdp_input_len(struct xdp_md *ctx)
{
	int retval = XDP_PASS; /* Expected retval on successful test */
	void *data_end = (void *)(long)ctx->data_end;
	void *data = (void *)(long)ctx->data;
	__u32 ifindex = GLOBAL_USER_IFINDEX;
	__u32 data_len = data_end - data;

	/* API allow user give length to check as input via mtu_len param,
	 * resulting MTU value is still output in mtu_len param after call.
	 *
	 * Input len is L3, like MTU and iph->tot_len.
	 * Remember XDP data_len is L2.
	 */
	__u32 mtu_len = data_len - ETH_HLEN;

	if (bpf_check_mtu(ctx, ifindex, &mtu_len, 0, 0))
		retval = XDP_ABORTED;

	global_bpf_mtu_xdp = mtu_len;
	return retval;
}

SEC("xdp")
int xdp_input_len_exceed(struct xdp_md *ctx)
{
	int retval = XDP_ABORTED; /* Fail */
	__u32 ifindex = GLOBAL_USER_IFINDEX;
	int err;

	/* API allow user give length to check as input via mtu_len param,
	 * resulting MTU value is still output in mtu_len param after call.
	 *
	 * Input length value is L3 size like MTU.
	 */
	__u32 mtu_len = GLOBAL_USER_MTU;

	mtu_len += 1; /* Exceed with 1 */

	err = bpf_check_mtu(ctx, ifindex, &mtu_len, 0, 0);
	if (err == BPF_MTU_CHK_RET_FRAG_NEEDED)
		retval = XDP_PASS ; /* Success in exceeding MTU check */

	global_bpf_mtu_xdp = mtu_len;
	return retval;
}

SEC("classifier")
int tc_use_helper(struct __sk_buff *ctx)
{
	int retval = BPF_OK; /* Expected retval on successful test */
	__u32 mtu_len = 0;
	int delta = 0;

	if (bpf_check_mtu(ctx, 0, &mtu_len, delta, 0)) {
		retval = BPF_DROP;
		goto out;
	}

	if (mtu_len != GLOBAL_USER_MTU)
		retval = BPF_REDIRECT;
out:
	global_bpf_mtu_tc = mtu_len;
	return retval;
}

SEC("classifier")
int tc_exceed_mtu(struct __sk_buff *ctx)
{
	__u32 ifindex = GLOBAL_USER_IFINDEX;
	int retval = BPF_DROP; /* Fail */
	__u32 skb_len = ctx->len;
	__u32 mtu_len = 0;
	int delta;
	int err;

	/* Exceed MTU with 1 via delta adjust */
	delta = GLOBAL_USER_MTU - (skb_len - ETH_HLEN) + 1;

	err = bpf_check_mtu(ctx, ifindex, &mtu_len, delta, 0);
	if (err) {
		retval = BPF_OK; /* Success in exceeding MTU check */
		if (err != BPF_MTU_CHK_RET_FRAG_NEEDED)
			retval = BPF_DROP;
	}

	global_bpf_mtu_tc = mtu_len;
	return retval;
}

SEC("classifier")
int tc_exceed_mtu_da(struct __sk_buff *ctx)
{
	/* SKB Direct-Access variant */
	void *data_end = (void *)(long)ctx->data_end;
	void *data = (void *)(long)ctx->data;
	__u32 ifindex = GLOBAL_USER_IFINDEX;
	__u32 data_len = data_end - data;
	int retval = BPF_DROP; /* Fail */
	__u32 mtu_len = 0;
	int delta;
	int err;

	/* Exceed MTU with 1 via delta adjust */
	delta = GLOBAL_USER_MTU - (data_len - ETH_HLEN) + 1;

	err = bpf_check_mtu(ctx, ifindex, &mtu_len, delta, 0);
	if (err) {
		retval = BPF_OK; /* Success in exceeding MTU check */
		if (err != BPF_MTU_CHK_RET_FRAG_NEEDED)
			retval = BPF_DROP;
	}

	global_bpf_mtu_tc = mtu_len;
	return retval;
}

SEC("classifier")
int tc_minus_delta(struct __sk_buff *ctx)
{
	int retval = BPF_OK; /* Expected retval on successful test */
	__u32 ifindex = GLOBAL_USER_IFINDEX;
	__u32 skb_len = ctx->len;
	__u32 mtu_len = 0;
	int delta;

	/* Borderline test case: Minus delta exceeding packet length allowed */
	delta = -((skb_len - ETH_HLEN) + 1);

	/* Minus length (adjusted via delta) still pass MTU check, other helpers
	 * are responsible for catching this, when doing actual size adjust
	 */
	if (bpf_check_mtu(ctx, ifindex, &mtu_len, delta, 0))
		retval = BPF_DROP;

	global_bpf_mtu_xdp = mtu_len;
	return retval;
}

SEC("classifier")
int tc_input_len(struct __sk_buff *ctx)
{
	int retval = BPF_OK; /* Expected retval on successful test */
	__u32 ifindex = GLOBAL_USER_IFINDEX;

	/* API allow user give length to check as input via mtu_len param,
	 * resulting MTU value is still output in mtu_len param after call.
	 *
	 * Input length value is L3 size.
	 */
	__u32 mtu_len = GLOBAL_USER_MTU;

	if (bpf_check_mtu(ctx, ifindex, &mtu_len, 0, 0))
		retval = BPF_DROP;

	global_bpf_mtu_xdp = mtu_len;
	return retval;
}

SEC("classifier")
int tc_input_len_exceed(struct __sk_buff *ctx)
{
	int retval = BPF_DROP; /* Fail */
	__u32 ifindex = GLOBAL_USER_IFINDEX;
	int err;

	/* API allow user give length to check as input via mtu_len param,
	 * resulting MTU value is still output in mtu_len param after call.
	 *
	 * Input length value is L3 size like MTU.
	 */
	__u32 mtu_len = GLOBAL_USER_MTU;

	mtu_len += 1; /* Exceed with 1 */

	err = bpf_check_mtu(ctx, ifindex, &mtu_len, 0, 0);
	if (err == BPF_MTU_CHK_RET_FRAG_NEEDED)
		retval = BPF_OK; /* Success in exceeding MTU check */

	global_bpf_mtu_xdp = mtu_len;
	return retval;
}
