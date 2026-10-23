// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

SEC("tc/ingress")
__description("uninit/mtu: write rejected")
__success
__caps_unpriv(CAP_BPF|CAP_NET_ADMIN)
__failure_unpriv __msg_unpriv("invalid read from stack")
int tc_uninit_mtu(struct __sk_buff *ctx)
{
	__u32 mtu;

	bpf_check_mtu(ctx, 0, &mtu, 0, 0);
	return TCX_PASS;
}

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__uint(map_flags, BPF_F_WRONLY_PROG);
	__type(key, __u32);
	__type(value, __u32);
} map_mtu_wo SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u32);
} map_mtu_rw SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__uint(map_flags, BPF_F_RDONLY_PROG);
	__type(key, __u32);
	__type(value, __u32);
} map_mtu_ro SEC(".maps");

SEC("tc/ingress")
__failure __msg("read from map forbidden")
int tc_writeonly_mtu(struct __sk_buff *ctx)
{
	__u32 key = 0;
	__u32 *mtu;

	mtu = bpf_map_lookup_elem(&map_mtu_wo, &key);
	if (mtu)
		bpf_check_mtu(ctx, 0, mtu, 0, 0);
	return TCX_PASS;
}

SEC("tc/ingress")
__success
int tc_readwrite_mtu(struct __sk_buff *ctx)
{
	__u32 key = 0;
	__u32 *mtu;

	mtu = bpf_map_lookup_elem(&map_mtu_rw, &key);
	if (mtu)
		bpf_check_mtu(ctx, 0, mtu, 0, 0);
	return TCX_PASS;
}

SEC("xdp")
__failure __msg("read from map forbidden")
int xdp_writeonly_mtu(struct xdp_md *ctx)
{
	__u32 key = 0;
	__u32 *mtu;

	mtu = bpf_map_lookup_elem(&map_mtu_wo, &key);
	if (mtu)
		bpf_check_mtu(ctx, 0, mtu, 0, 0);
	return XDP_PASS;
}

SEC("tc/ingress")
__failure __msg("write into map forbidden")
int tc_readonly_mtu(struct __sk_buff *ctx)
{
	__u32 key = 0;
	__u32 *mtu;

	mtu = bpf_map_lookup_elem(&map_mtu_ro, &key);
	if (mtu)
		bpf_check_mtu(ctx, 0, mtu, 0, 0);
	return TCX_PASS;
}

SEC("tc/ingress")
__success
int tc_writeonly_output(struct __sk_buff *ctx)
{
	__u32 key = 0;
	__u32 *out;

	out = bpf_map_lookup_elem(&map_mtu_wo, &key);
	if (out)
		bpf_get_current_comm(out, sizeof(*out));
	return TCX_PASS;
}

char LICENSE[] SEC("license") = "GPL";
