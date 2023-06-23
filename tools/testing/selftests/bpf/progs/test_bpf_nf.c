// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#define EAFNOSUPPORT 97
#define EPROTO 71
#define ENONET 64
#define EINVAL 22
#define ENOENT 2

extern unsigned long CONFIG_HZ __kconfig;

int test_einval_bpf_tuple = 0;
int test_einval_reserved = 0;
int test_einval_netns_id = 0;
int test_einval_len_opts = 0;
int test_eproto_l4proto = 0;
int test_enonet_netns_id = 0;
int test_enoent_lookup = 0;
int test_eafnosupport = 0;
int test_alloc_entry = -EINVAL;
int test_insert_entry = -EAFNOSUPPORT;
int test_succ_lookup = -ENOENT;
u32 test_delta_timeout = 0;
u32 test_status = 0;
u32 test_insert_lookup_mark = 0;
int test_snat_addr = -EINVAL;
int test_dnat_addr = -EINVAL;
__be32 saddr = 0;
__be16 sport = 0;
__be32 daddr = 0;
__be16 dport = 0;
int test_exist_lookup = -ENOENT;
u32 test_exist_lookup_mark = 0;

struct nf_conn;

struct bpf_ct_opts___local {
	s32 netns_id;
	s32 error;
	u8 l4proto;
	u8 reserved[3];
} __attribute__((preserve_access_index));

struct nf_conn *bpf_xdp_ct_alloc(struct xdp_md *, struct bpf_sock_tuple *, u32,
				 struct bpf_ct_opts___local *, u32) __ksym;
struct nf_conn *bpf_xdp_ct_lookup(struct xdp_md *, struct bpf_sock_tuple *, u32,
				  struct bpf_ct_opts___local *, u32) __ksym;
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
int bpf_ct_set_nat_info(struct nf_conn *, union nf_inet_addr *,
			int port, enum nf_nat_manip_type) __ksym;

static __always_inline void
nf_ct_test(struct nf_conn *(*lookup_fn)(void *, struct bpf_sock_tuple *, u32,
					struct bpf_ct_opts___local *, u32),
	   struct nf_conn *(*alloc_fn)(void *, struct bpf_sock_tuple *, u32,
				       struct bpf_ct_opts___local *, u32),
	   void *ctx)
{
	struct bpf_ct_opts___local opts_def = { .l4proto = IPPROTO_TCP, .netns_id = -1 };
	struct bpf_sock_tuple bpf_tuple;
	struct nf_conn *ct;
	int err;

	__builtin_memset(&bpf_tuple, 0, sizeof(bpf_tuple.ipv4));

	ct = lookup_fn(ctx, NULL, 0, &opts_def, sizeof(opts_def));
	if (ct)
		bpf_ct_release(ct);
	else
		test_einval_bpf_tuple = opts_def.error;

	opts_def.reserved[0] = 1;
	ct = lookup_fn(ctx, &bpf_tuple, sizeof(bpf_tuple.ipv4), &opts_def,
		       sizeof(opts_def));
	opts_def.reserved[0] = 0;
	opts_def.l4proto = IPPROTO_TCP;
	if (ct)
		bpf_ct_release(ct);
	else
		test_einval_reserved = opts_def.error;

	opts_def.netns_id = -2;
	ct = lookup_fn(ctx, &bpf_tuple, sizeof(bpf_tuple.ipv4), &opts_def,
		       sizeof(opts_def));
	opts_def.netns_id = -1;
	if (ct)
		bpf_ct_release(ct);
	else
		test_einval_netns_id = opts_def.error;

	ct = lookup_fn(ctx, &bpf_tuple, sizeof(bpf_tuple.ipv4), &opts_def,
		       sizeof(opts_def) - 1);
	if (ct)
		bpf_ct_release(ct);
	else
		test_einval_len_opts = opts_def.error;

	opts_def.l4proto = IPPROTO_ICMP;
	ct = lookup_fn(ctx, &bpf_tuple, sizeof(bpf_tuple.ipv4), &opts_def,
		       sizeof(opts_def));
	opts_def.l4proto = IPPROTO_TCP;
	if (ct)
		bpf_ct_release(ct);
	else
		test_eproto_l4proto = opts_def.error;

	opts_def.netns_id = 0xf00f;
	ct = lookup_fn(ctx, &bpf_tuple, sizeof(bpf_tuple.ipv4), &opts_def,
		       sizeof(opts_def));
	opts_def.netns_id = -1;
	if (ct)
		bpf_ct_release(ct);
	else
		test_enonet_netns_id = opts_def.error;

	ct = lookup_fn(ctx, &bpf_tuple, sizeof(bpf_tuple.ipv4), &opts_def,
		       sizeof(opts_def));
	if (ct)
		bpf_ct_release(ct);
	else
		test_enoent_lookup = opts_def.error;

	ct = lookup_fn(ctx, &bpf_tuple, sizeof(bpf_tuple.ipv4) - 1, &opts_def,
		       sizeof(opts_def));
	if (ct)
		bpf_ct_release(ct);
	else
		test_eafnosupport = opts_def.error;

	bpf_tuple.ipv4.saddr = bpf_get_prandom_u32(); /* src IP */
	bpf_tuple.ipv4.daddr = bpf_get_prandom_u32(); /* dst IP */
	bpf_tuple.ipv4.sport = bpf_get_prandom_u32(); /* src port */
	bpf_tuple.ipv4.dport = bpf_get_prandom_u32(); /* dst port */

	ct = alloc_fn(ctx, &bpf_tuple, sizeof(bpf_tuple.ipv4), &opts_def,
		      sizeof(opts_def));
	if (ct) {
		__u16 sport = bpf_get_prandom_u32();
		__u16 dport = bpf_get_prandom_u32();
		union nf_inet_addr saddr = {};
		union nf_inet_addr daddr = {};
		struct nf_conn *ct_ins;

		bpf_ct_set_timeout(ct, 10000);
		ct->mark = 77;

		/* snat */
		saddr.ip = bpf_get_prandom_u32();
		bpf_ct_set_nat_info(ct, &saddr, sport, NF_NAT_MANIP_SRC);
		/* dnat */
		daddr.ip = bpf_get_prandom_u32();
		bpf_ct_set_nat_info(ct, &daddr, dport, NF_NAT_MANIP_DST);

		ct_ins = bpf_ct_insert_entry(ct);
		if (ct_ins) {
			struct nf_conn *ct_lk;

			ct_lk = lookup_fn(ctx, &bpf_tuple, sizeof(bpf_tuple.ipv4),
					  &opts_def, sizeof(opts_def));
			if (ct_lk) {
				struct nf_conntrack_tuple *tuple;

				/* check snat and dnat addresses */
				tuple = &ct_lk->tuplehash[IP_CT_DIR_REPLY].tuple;
				if (tuple->dst.u3.ip == saddr.ip &&
				    tuple->dst.u.all == bpf_htons(sport))
					test_snat_addr = 0;
				if (tuple->src.u3.ip == daddr.ip &&
				    tuple->src.u.all == bpf_htons(dport))
					test_dnat_addr = 0;

				/* update ct entry timeout */
				bpf_ct_change_timeout(ct_lk, 10000);
				test_delta_timeout = ct_lk->timeout - bpf_jiffies64();
				test_delta_timeout /= CONFIG_HZ;
				test_insert_lookup_mark = ct_lk->mark;
				bpf_ct_change_status(ct_lk,
						     IPS_CONFIRMED | IPS_SEEN_REPLY);
				test_status = ct_lk->status;

				bpf_ct_release(ct_lk);
				test_succ_lookup = 0;
			}
			bpf_ct_release(ct_ins);
			test_insert_entry = 0;
		}
		test_alloc_entry = 0;
	}

	bpf_tuple.ipv4.saddr = saddr;
	bpf_tuple.ipv4.daddr = daddr;
	bpf_tuple.ipv4.sport = sport;
	bpf_tuple.ipv4.dport = dport;
	ct = lookup_fn(ctx, &bpf_tuple, sizeof(bpf_tuple.ipv4), &opts_def,
		       sizeof(opts_def));
	if (ct) {
		test_exist_lookup = 0;
		if (ct->mark == 42) {
			ct->mark++;
			test_exist_lookup_mark = ct->mark;
		}
		bpf_ct_release(ct);
	} else {
		test_exist_lookup = opts_def.error;
	}
}

SEC("xdp")
int nf_xdp_ct_test(struct xdp_md *ctx)
{
	nf_ct_test((void *)bpf_xdp_ct_lookup, (void *)bpf_xdp_ct_alloc, ctx);
	return 0;
}

SEC("tc")
int nf_skb_ct_test(struct __sk_buff *ctx)
{
	nf_ct_test((void *)bpf_skb_ct_lookup, (void *)bpf_skb_ct_alloc, ctx);
	return 0;
}

char _license[] SEC("license") = "GPL";
