// SPDX-License-Identifier: GPL-2.0-only
/* Unstable Conntrack Helpers for XDP and TC-BPF hook
 *
 * These are called from the XDP and SCHED_CLS BPF programs. Note that it is
 * allowed to break compatibility for these functions since the interface they
 * are exposed through to BPF programs is explicitly unstable.
 */

#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/types.h>
#include <linux/btf_ids.h>
#include <linux/net_namespace.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_bpf.h>
#include <net/netfilter/nf_conntrack_core.h>

/* bpf_ct_opts - Options for CT lookup helpers
 *
 * Members:
 * @netns_id   - Specify the network namespace for lookup
 *		 Values:
 *		   BPF_F_CURRENT_NETNS (-1)
 *		     Use namespace associated with ctx (xdp_md, __sk_buff)
 *		   [0, S32_MAX]
 *		     Network Namespace ID
 * @error      - Out parameter, set for any errors encountered
 *		 Values:
 *		   -EINVAL - Passed NULL for bpf_tuple pointer
 *		   -EINVAL - opts->reserved is not 0
 *		   -EINVAL - netns_id is less than -1
 *		   -EINVAL - opts__sz isn't NF_BPF_CT_OPTS_SZ (12)
 *		   -EPROTO - l4proto isn't one of IPPROTO_TCP or IPPROTO_UDP
 *		   -ENONET - No network namespace found for netns_id
 *		   -ENOENT - Conntrack lookup could not find entry for tuple
 *		   -EAFNOSUPPORT - tuple__sz isn't one of sizeof(tuple->ipv4)
 *				   or sizeof(tuple->ipv6)
 * @l4proto    - Layer 4 protocol
 *		 Values:
 *		   IPPROTO_TCP, IPPROTO_UDP
 * @reserved   - Reserved member, will be reused for more options in future
 *		 Values:
 *		   0
 */
struct bpf_ct_opts {
	s32 netns_id;
	s32 error;
	u8 l4proto;
	u8 reserved[3];
};

enum {
	NF_BPF_CT_OPTS_SZ = 12,
};

static struct nf_conn *__bpf_nf_ct_lookup(struct net *net,
					  struct bpf_sock_tuple *bpf_tuple,
					  u32 tuple_len, u8 protonum,
					  s32 netns_id)
{
	struct nf_conntrack_tuple_hash *hash;
	struct nf_conntrack_tuple tuple;

	if (unlikely(protonum != IPPROTO_TCP && protonum != IPPROTO_UDP))
		return ERR_PTR(-EPROTO);
	if (unlikely(netns_id < BPF_F_CURRENT_NETNS))
		return ERR_PTR(-EINVAL);

	memset(&tuple, 0, sizeof(tuple));
	switch (tuple_len) {
	case sizeof(bpf_tuple->ipv4):
		tuple.src.l3num = AF_INET;
		tuple.src.u3.ip = bpf_tuple->ipv4.saddr;
		tuple.src.u.tcp.port = bpf_tuple->ipv4.sport;
		tuple.dst.u3.ip = bpf_tuple->ipv4.daddr;
		tuple.dst.u.tcp.port = bpf_tuple->ipv4.dport;
		break;
	case sizeof(bpf_tuple->ipv6):
		tuple.src.l3num = AF_INET6;
		memcpy(tuple.src.u3.ip6, bpf_tuple->ipv6.saddr, sizeof(bpf_tuple->ipv6.saddr));
		tuple.src.u.tcp.port = bpf_tuple->ipv6.sport;
		memcpy(tuple.dst.u3.ip6, bpf_tuple->ipv6.daddr, sizeof(bpf_tuple->ipv6.daddr));
		tuple.dst.u.tcp.port = bpf_tuple->ipv6.dport;
		break;
	default:
		return ERR_PTR(-EAFNOSUPPORT);
	}

	tuple.dst.protonum = protonum;

	if (netns_id >= 0) {
		net = get_net_ns_by_id(net, netns_id);
		if (unlikely(!net))
			return ERR_PTR(-ENONET);
	}

	hash = nf_conntrack_find_get(net, &nf_ct_zone_dflt, &tuple);
	if (netns_id >= 0)
		put_net(net);
	if (!hash)
		return ERR_PTR(-ENOENT);
	return nf_ct_tuplehash_to_ctrack(hash);
}

__diag_push();
__diag_ignore_all("-Wmissing-prototypes",
		  "Global functions as their definitions will be in nf_conntrack BTF");

/* bpf_xdp_ct_lookup - Lookup CT entry for the given tuple, and acquire a
 *		       reference to it
 *
 * Parameters:
 * @xdp_ctx	- Pointer to ctx (xdp_md) in XDP program
 *		    Cannot be NULL
 * @bpf_tuple	- Pointer to memory representing the tuple to look up
 *		    Cannot be NULL
 * @tuple__sz	- Length of the tuple structure
 *		    Must be one of sizeof(bpf_tuple->ipv4) or
 *		    sizeof(bpf_tuple->ipv6)
 * @opts	- Additional options for lookup (documented above)
 *		    Cannot be NULL
 * @opts__sz	- Length of the bpf_ct_opts structure
 *		    Must be NF_BPF_CT_OPTS_SZ (12)
 */
struct nf_conn *
bpf_xdp_ct_lookup(struct xdp_md *xdp_ctx, struct bpf_sock_tuple *bpf_tuple,
		  u32 tuple__sz, struct bpf_ct_opts *opts, u32 opts__sz)
{
	struct xdp_buff *ctx = (struct xdp_buff *)xdp_ctx;
	struct net *caller_net;
	struct nf_conn *nfct;

	BUILD_BUG_ON(sizeof(struct bpf_ct_opts) != NF_BPF_CT_OPTS_SZ);

	if (!opts)
		return NULL;
	if (!bpf_tuple || opts->reserved[0] || opts->reserved[1] ||
	    opts->reserved[2] || opts__sz != NF_BPF_CT_OPTS_SZ) {
		opts->error = -EINVAL;
		return NULL;
	}
	caller_net = dev_net(ctx->rxq->dev);
	nfct = __bpf_nf_ct_lookup(caller_net, bpf_tuple, tuple__sz, opts->l4proto,
				  opts->netns_id);
	if (IS_ERR(nfct)) {
		opts->error = PTR_ERR(nfct);
		return NULL;
	}
	return nfct;
}

/* bpf_skb_ct_lookup - Lookup CT entry for the given tuple, and acquire a
 *		       reference to it
 *
 * Parameters:
 * @skb_ctx	- Pointer to ctx (__sk_buff) in TC program
 *		    Cannot be NULL
 * @bpf_tuple	- Pointer to memory representing the tuple to look up
 *		    Cannot be NULL
 * @tuple__sz	- Length of the tuple structure
 *		    Must be one of sizeof(bpf_tuple->ipv4) or
 *		    sizeof(bpf_tuple->ipv6)
 * @opts	- Additional options for lookup (documented above)
 *		    Cannot be NULL
 * @opts__sz	- Length of the bpf_ct_opts structure
 *		    Must be NF_BPF_CT_OPTS_SZ (12)
 */
struct nf_conn *
bpf_skb_ct_lookup(struct __sk_buff *skb_ctx, struct bpf_sock_tuple *bpf_tuple,
		  u32 tuple__sz, struct bpf_ct_opts *opts, u32 opts__sz)
{
	struct sk_buff *skb = (struct sk_buff *)skb_ctx;
	struct net *caller_net;
	struct nf_conn *nfct;

	BUILD_BUG_ON(sizeof(struct bpf_ct_opts) != NF_BPF_CT_OPTS_SZ);

	if (!opts)
		return NULL;
	if (!bpf_tuple || opts->reserved[0] || opts->reserved[1] ||
	    opts->reserved[2] || opts__sz != NF_BPF_CT_OPTS_SZ) {
		opts->error = -EINVAL;
		return NULL;
	}
	caller_net = skb->dev ? dev_net(skb->dev) : sock_net(skb->sk);
	nfct = __bpf_nf_ct_lookup(caller_net, bpf_tuple, tuple__sz, opts->l4proto,
				  opts->netns_id);
	if (IS_ERR(nfct)) {
		opts->error = PTR_ERR(nfct);
		return NULL;
	}
	return nfct;
}

/* bpf_ct_release - Release acquired nf_conn object
 *
 * This must be invoked for referenced PTR_TO_BTF_ID, and the verifier rejects
 * the program if any references remain in the program in all of the explored
 * states.
 *
 * Parameters:
 * @nf_conn	 - Pointer to referenced nf_conn object, obtained using
 *		   bpf_xdp_ct_lookup or bpf_skb_ct_lookup.
 */
void bpf_ct_release(struct nf_conn *nfct)
{
	if (!nfct)
		return;
	nf_ct_put(nfct);
}

__diag_pop()

BTF_SET_START(nf_ct_xdp_check_kfunc_ids)
BTF_ID(func, bpf_xdp_ct_lookup)
BTF_ID(func, bpf_ct_release)
BTF_SET_END(nf_ct_xdp_check_kfunc_ids)

BTF_SET_START(nf_ct_tc_check_kfunc_ids)
BTF_ID(func, bpf_skb_ct_lookup)
BTF_ID(func, bpf_ct_release)
BTF_SET_END(nf_ct_tc_check_kfunc_ids)

BTF_SET_START(nf_ct_acquire_kfunc_ids)
BTF_ID(func, bpf_xdp_ct_lookup)
BTF_ID(func, bpf_skb_ct_lookup)
BTF_SET_END(nf_ct_acquire_kfunc_ids)

BTF_SET_START(nf_ct_release_kfunc_ids)
BTF_ID(func, bpf_ct_release)
BTF_SET_END(nf_ct_release_kfunc_ids)

/* Both sets are identical */
#define nf_ct_ret_null_kfunc_ids nf_ct_acquire_kfunc_ids

static const struct btf_kfunc_id_set nf_conntrack_xdp_kfunc_set = {
	.owner        = THIS_MODULE,
	.check_set    = &nf_ct_xdp_check_kfunc_ids,
	.acquire_set  = &nf_ct_acquire_kfunc_ids,
	.release_set  = &nf_ct_release_kfunc_ids,
	.ret_null_set = &nf_ct_ret_null_kfunc_ids,
};

static const struct btf_kfunc_id_set nf_conntrack_tc_kfunc_set = {
	.owner        = THIS_MODULE,
	.check_set    = &nf_ct_tc_check_kfunc_ids,
	.acquire_set  = &nf_ct_acquire_kfunc_ids,
	.release_set  = &nf_ct_release_kfunc_ids,
	.ret_null_set = &nf_ct_ret_null_kfunc_ids,
};

int register_nf_conntrack_bpf(void)
{
	int ret;

	ret = register_btf_kfunc_id_set(BPF_PROG_TYPE_XDP, &nf_conntrack_xdp_kfunc_set);
	return ret ?: register_btf_kfunc_id_set(BPF_PROG_TYPE_SCHED_CLS, &nf_conntrack_tc_kfunc_set);
}
