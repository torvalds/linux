// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#include "bpf_misc.h"

#define __always_unused __attribute__((unused))

char _license[] SEC("license") = "GPL";

struct sock {
} __attribute__((preserve_access_index));

struct bpf_iter__sockmap {
	union {
		struct sock *sk;
	};
} __attribute__((preserve_access_index));

struct {
	__uint(type, BPF_MAP_TYPE_SOCKHASH);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, int);
} sockhash SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_SOCKMAP);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, int);
} sockmap SEC(".maps");

enum { CG_OK = 1 };

int zero = 0;

static __always_inline void test_sockmap_delete(void)
{
	bpf_map_delete_elem(&sockmap, &zero);
	bpf_map_delete_elem(&sockhash, &zero);
}

static __always_inline void test_sockmap_update(void *sk)
{
	if (sk) {
		bpf_map_update_elem(&sockmap, &zero, sk, BPF_ANY);
		bpf_map_update_elem(&sockhash, &zero, sk, BPF_ANY);
	}
}

static __always_inline void test_sockmap_lookup_and_update(void)
{
	struct bpf_sock *sk = bpf_map_lookup_elem(&sockmap, &zero);

	if (sk) {
		test_sockmap_update(sk);
		bpf_sk_release(sk);
	}
}

static __always_inline void test_sockmap_mutate(void *sk)
{
	test_sockmap_delete();
	test_sockmap_update(sk);
}

static __always_inline void test_sockmap_lookup_and_mutate(void)
{
	test_sockmap_delete();
	test_sockmap_lookup_and_update();
}

SEC("action")
__success
int test_sched_act(struct __sk_buff *skb)
{
	test_sockmap_mutate(skb->sk);
	return 0;
}

SEC("classifier")
__success
int test_sched_cls(struct __sk_buff *skb)
{
	test_sockmap_mutate(skb->sk);
	return 0;
}

SEC("flow_dissector")
__success
int test_flow_dissector_delete(struct __sk_buff *skb __always_unused)
{
	test_sockmap_delete();
	return 0;
}

SEC("flow_dissector")
__failure __msg("program of this type cannot use helper bpf_sk_release")
int test_flow_dissector_update(struct __sk_buff *skb __always_unused)
{
	test_sockmap_lookup_and_update(); /* no access to skb->sk */
	return 0;
}

SEC("iter/sockmap")
__success
int test_trace_iter(struct bpf_iter__sockmap *ctx)
{
	test_sockmap_mutate(ctx->sk);
	return 0;
}

SEC("raw_tp/kfree")
__failure __msg("cannot update sockmap in this context")
int test_raw_tp_delete(const void *ctx __always_unused)
{
	test_sockmap_delete();
	return 0;
}

SEC("raw_tp/kfree")
__failure __msg("cannot update sockmap in this context")
int test_raw_tp_update(const void *ctx __always_unused)
{
	test_sockmap_lookup_and_update();
	return 0;
}

SEC("sk_lookup")
__success
int test_sk_lookup(struct bpf_sk_lookup *ctx)
{
	test_sockmap_mutate(ctx->sk);
	return 0;
}

SEC("sk_reuseport")
__success
int test_sk_reuseport(struct sk_reuseport_md *ctx)
{
	test_sockmap_mutate(ctx->sk);
	return 0;
}

SEC("socket")
__success
int test_socket_filter(struct __sk_buff *skb)
{
	test_sockmap_mutate(skb->sk);
	return 0;
}

SEC("sockops")
__success
int test_sockops_delete(struct bpf_sock_ops *ctx __always_unused)
{
	test_sockmap_delete();
	return CG_OK;
}

SEC("sockops")
__failure __msg("cannot update sockmap in this context")
int test_sockops_update(struct bpf_sock_ops *ctx)
{
	test_sockmap_update(ctx->sk);
	return CG_OK;
}

SEC("sockops")
__success
int test_sockops_update_dedicated(struct bpf_sock_ops *ctx)
{
	bpf_sock_map_update(ctx, &sockmap, &zero, BPF_ANY);
	bpf_sock_hash_update(ctx, &sockhash, &zero, BPF_ANY);
	return CG_OK;
}

SEC("xdp")
__success
int test_xdp(struct xdp_md *ctx __always_unused)
{
	test_sockmap_lookup_and_mutate();
	return XDP_PASS;
}
