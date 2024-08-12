// SPDX-License-Identifier: GPL-2.0-only
/* Unstable Flow Table Helpers for XDP hook
 *
 * These are called from the XDP programs.
 * Note that it is allowed to break compatibility for these functions since
 * the interface they are exposed through to BPF programs is explicitly
 * unstable.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <net/netfilter/nf_flow_table.h>
#include <linux/bpf.h>
#include <linux/btf.h>
#include <net/xdp.h>

/* bpf_flowtable_opts - options for bpf flowtable helpers
 * @error: out parameter, set for any encountered error
 */
struct bpf_flowtable_opts {
	s32 error;
};

enum {
	NF_BPF_FLOWTABLE_OPTS_SZ = 4,
};

__diag_push();
__diag_ignore_all("-Wmissing-prototypes",
		  "Global functions as their definitions will be in nf_flow_table BTF");

__bpf_kfunc_start_defs();

static struct flow_offload_tuple_rhash *
bpf_xdp_flow_tuple_lookup(struct net_device *dev,
			  struct flow_offload_tuple *tuple, __be16 proto)
{
	struct flow_offload_tuple_rhash *tuplehash;
	struct nf_flowtable *nf_flow_table;
	struct flow_offload *nf_flow;

	nf_flow_table = nf_flowtable_by_dev(dev);
	if (!nf_flow_table)
		return ERR_PTR(-ENOENT);

	tuplehash = flow_offload_lookup(nf_flow_table, tuple);
	if (!tuplehash)
		return ERR_PTR(-ENOENT);

	nf_flow = container_of(tuplehash, struct flow_offload,
			       tuplehash[tuplehash->tuple.dir]);
	flow_offload_refresh(nf_flow_table, nf_flow, false);

	return tuplehash;
}

__bpf_kfunc struct flow_offload_tuple_rhash *
bpf_xdp_flow_lookup(struct xdp_md *ctx, struct bpf_fib_lookup *fib_tuple,
		    struct bpf_flowtable_opts *opts, u32 opts_len)
{
	struct xdp_buff *xdp = (struct xdp_buff *)ctx;
	struct flow_offload_tuple tuple = {
		.iifidx = fib_tuple->ifindex,
		.l3proto = fib_tuple->family,
		.l4proto = fib_tuple->l4_protocol,
		.src_port = fib_tuple->sport,
		.dst_port = fib_tuple->dport,
	};
	struct flow_offload_tuple_rhash *tuplehash;
	__be16 proto;

	if (opts_len != NF_BPF_FLOWTABLE_OPTS_SZ) {
		opts->error = -EINVAL;
		return NULL;
	}

	switch (fib_tuple->family) {
	case AF_INET:
		tuple.src_v4.s_addr = fib_tuple->ipv4_src;
		tuple.dst_v4.s_addr = fib_tuple->ipv4_dst;
		proto = htons(ETH_P_IP);
		break;
	case AF_INET6:
		tuple.src_v6 = *(struct in6_addr *)&fib_tuple->ipv6_src;
		tuple.dst_v6 = *(struct in6_addr *)&fib_tuple->ipv6_dst;
		proto = htons(ETH_P_IPV6);
		break;
	default:
		opts->error = -EAFNOSUPPORT;
		return NULL;
	}

	tuplehash = bpf_xdp_flow_tuple_lookup(xdp->rxq->dev, &tuple, proto);
	if (IS_ERR(tuplehash)) {
		opts->error = PTR_ERR(tuplehash);
		return NULL;
	}

	return tuplehash;
}

__diag_pop()

__bpf_kfunc_end_defs();

BTF_KFUNCS_START(nf_ft_kfunc_set)
BTF_ID_FLAGS(func, bpf_xdp_flow_lookup, KF_TRUSTED_ARGS | KF_RET_NULL)
BTF_KFUNCS_END(nf_ft_kfunc_set)

static const struct btf_kfunc_id_set nf_flow_kfunc_set = {
	.owner = THIS_MODULE,
	.set   = &nf_ft_kfunc_set,
};

int nf_flow_register_bpf(void)
{
	return register_btf_kfunc_id_set(BPF_PROG_TYPE_XDP,
					 &nf_flow_kfunc_set);
}
EXPORT_SYMBOL_GPL(nf_flow_register_bpf);
