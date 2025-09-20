// SPDX-License-Identifier: GPL-2.0
#define BPF_NO_KFUNC_PROTOTYPES
#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>

struct nf_conn;

struct bpf_ct_opts___local {
	s32 netns_id;
	s32 error;
	u8 l4proto;
	u8 reserved[3];
} __attribute__((preserve_access_index));

struct nf_conn *bpf_skb_ct_alloc(struct __sk_buff *, struct bpf_sock_tuple *, u32,
				 struct bpf_ct_opts___local *, u32) __ksym;
struct nf_conn *bpf_skb_ct_lookup(struct __sk_buff *, struct bpf_sock_tuple *, u32,
				  struct bpf_ct_opts___local *, u32) __ksym;
struct nf_conn *bpf_ct_insert_entry(struct nf_conn *) __ksym;
void bpf_ct_release(struct nf_conn *) __ksym;
void bpf_ct_set_timeout(struct nf_conn *, u32) __ksym;
int bpf_ct_change_timeout(struct nf_conn *, u32) __ksym;
int bpf_ct_set_status(struct nf_conn *, u32) __ksym;
int bpf_ct_change_status(struct nf_conn *, u32) __ksym;

SEC("?tc")
int alloc_release(struct __sk_buff *ctx)
{
	struct bpf_ct_opts___local opts = {};
	struct bpf_sock_tuple tup = {};
	struct nf_conn *ct;

	ct = bpf_skb_ct_alloc(ctx, &tup, sizeof(tup.ipv4), &opts, sizeof(opts));
	if (!ct)
		return 0;
	bpf_ct_release(ct);
	return 0;
}

SEC("?tc")
int insert_insert(struct __sk_buff *ctx)
{
	struct bpf_ct_opts___local opts = {};
	struct bpf_sock_tuple tup = {};
	struct nf_conn *ct;

	ct = bpf_skb_ct_alloc(ctx, &tup, sizeof(tup.ipv4), &opts, sizeof(opts));
	if (!ct)
		return 0;
	ct = bpf_ct_insert_entry(ct);
	if (!ct)
		return 0;
	ct = bpf_ct_insert_entry(ct);
	return 0;
}

SEC("?tc")
int lookup_insert(struct __sk_buff *ctx)
{
	struct bpf_ct_opts___local opts = {};
	struct bpf_sock_tuple tup = {};
	struct nf_conn *ct;

	ct = bpf_skb_ct_lookup(ctx, &tup, sizeof(tup.ipv4), &opts, sizeof(opts));
	if (!ct)
		return 0;
	bpf_ct_insert_entry(ct);
	return 0;
}

SEC("?tc")
int write_not_allowlisted_field(struct __sk_buff *ctx)
{
	struct bpf_ct_opts___local opts = {};
	struct bpf_sock_tuple tup = {};
	struct nf_conn *ct;

	ct = bpf_skb_ct_lookup(ctx, &tup, sizeof(tup.ipv4), &opts, sizeof(opts));
	if (!ct)
		return 0;
	ct->status = 0xF00;
	return 0;
}

SEC("?tc")
int set_timeout_after_insert(struct __sk_buff *ctx)
{
	struct bpf_ct_opts___local opts = {};
	struct bpf_sock_tuple tup = {};
	struct nf_conn *ct;

	ct = bpf_skb_ct_alloc(ctx, &tup, sizeof(tup.ipv4), &opts, sizeof(opts));
	if (!ct)
		return 0;
	ct = bpf_ct_insert_entry(ct);
	if (!ct)
		return 0;
	bpf_ct_set_timeout(ct, 0);
	return 0;
}

SEC("?tc")
int set_status_after_insert(struct __sk_buff *ctx)
{
	struct bpf_ct_opts___local opts = {};
	struct bpf_sock_tuple tup = {};
	struct nf_conn *ct;

	ct = bpf_skb_ct_alloc(ctx, &tup, sizeof(tup.ipv4), &opts, sizeof(opts));
	if (!ct)
		return 0;
	ct = bpf_ct_insert_entry(ct);
	if (!ct)
		return 0;
	bpf_ct_set_status(ct, 0);
	return 0;
}

SEC("?tc")
int change_timeout_after_alloc(struct __sk_buff *ctx)
{
	struct bpf_ct_opts___local opts = {};
	struct bpf_sock_tuple tup = {};
	struct nf_conn *ct;

	ct = bpf_skb_ct_alloc(ctx, &tup, sizeof(tup.ipv4), &opts, sizeof(opts));
	if (!ct)
		return 0;
	bpf_ct_change_timeout(ct, 0);
	return 0;
}

SEC("?tc")
int change_status_after_alloc(struct __sk_buff *ctx)
{
	struct bpf_ct_opts___local opts = {};
	struct bpf_sock_tuple tup = {};
	struct nf_conn *ct;

	ct = bpf_skb_ct_alloc(ctx, &tup, sizeof(tup.ipv4), &opts, sizeof(opts));
	if (!ct)
		return 0;
	bpf_ct_change_status(ct, 0);
	return 0;
}

char _license[] SEC("license") = "GPL";
