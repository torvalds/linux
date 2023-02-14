// SPDX-License-Identifier: GPL-2.0-only
/* Unstable Conntrack Helpers for XDP and TC-BPF hook
 *
 * These are called from the XDP and SCHED_CLS BPF programs. Note that it is
 * allowed to break compatibility for these functions since the interface they
 * are exposed through to BPF programs is explicitly unstable.
 */

#include <linux/bpf_verifier.h>
#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/filter.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/btf_ids.h>
#include <linux/net_namespace.h>
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
 * @dir:       - connection tracking tuple direction.
 * @reserved   - Reserved member, will be reused for more options in future
 *		 Values:
 *		   0
 */
struct bpf_ct_opts {
	s32 netns_id;
	s32 error;
	u8 l4proto;
	u8 dir;
	u8 reserved[2];
};

enum {
	NF_BPF_CT_OPTS_SZ = 12,
};

static int bpf_nf_ct_tuple_parse(struct bpf_sock_tuple *bpf_tuple,
				 u32 tuple_len, u8 protonum, u8 dir,
				 struct nf_conntrack_tuple *tuple)
{
	union nf_inet_addr *src = dir ? &tuple->dst.u3 : &tuple->src.u3;
	union nf_inet_addr *dst = dir ? &tuple->src.u3 : &tuple->dst.u3;
	union nf_conntrack_man_proto *sport = dir ? (void *)&tuple->dst.u
						  : &tuple->src.u;
	union nf_conntrack_man_proto *dport = dir ? &tuple->src.u
						  : (void *)&tuple->dst.u;

	if (unlikely(protonum != IPPROTO_TCP && protonum != IPPROTO_UDP))
		return -EPROTO;

	memset(tuple, 0, sizeof(*tuple));

	switch (tuple_len) {
	case sizeof(bpf_tuple->ipv4):
		tuple->src.l3num = AF_INET;
		src->ip = bpf_tuple->ipv4.saddr;
		sport->tcp.port = bpf_tuple->ipv4.sport;
		dst->ip = bpf_tuple->ipv4.daddr;
		dport->tcp.port = bpf_tuple->ipv4.dport;
		break;
	case sizeof(bpf_tuple->ipv6):
		tuple->src.l3num = AF_INET6;
		memcpy(src->ip6, bpf_tuple->ipv6.saddr, sizeof(bpf_tuple->ipv6.saddr));
		sport->tcp.port = bpf_tuple->ipv6.sport;
		memcpy(dst->ip6, bpf_tuple->ipv6.daddr, sizeof(bpf_tuple->ipv6.daddr));
		dport->tcp.port = bpf_tuple->ipv6.dport;
		break;
	default:
		return -EAFNOSUPPORT;
	}
	tuple->dst.protonum = protonum;
	tuple->dst.dir = dir;

	return 0;
}

static struct nf_conn *
__bpf_nf_ct_alloc_entry(struct net *net, struct bpf_sock_tuple *bpf_tuple,
			u32 tuple_len, struct bpf_ct_opts *opts, u32 opts_len,
			u32 timeout)
{
	struct nf_conntrack_tuple otuple, rtuple;
	struct nf_conn *ct;
	int err;

	if (!opts || !bpf_tuple || opts->reserved[0] || opts->reserved[1] ||
	    opts_len != NF_BPF_CT_OPTS_SZ)
		return ERR_PTR(-EINVAL);

	if (unlikely(opts->netns_id < BPF_F_CURRENT_NETNS))
		return ERR_PTR(-EINVAL);

	err = bpf_nf_ct_tuple_parse(bpf_tuple, tuple_len, opts->l4proto,
				    IP_CT_DIR_ORIGINAL, &otuple);
	if (err < 0)
		return ERR_PTR(err);

	err = bpf_nf_ct_tuple_parse(bpf_tuple, tuple_len, opts->l4proto,
				    IP_CT_DIR_REPLY, &rtuple);
	if (err < 0)
		return ERR_PTR(err);

	if (opts->netns_id >= 0) {
		net = get_net_ns_by_id(net, opts->netns_id);
		if (unlikely(!net))
			return ERR_PTR(-ENONET);
	}

	ct = nf_conntrack_alloc(net, &nf_ct_zone_dflt, &otuple, &rtuple,
				GFP_ATOMIC);
	if (IS_ERR(ct))
		goto out;

	memset(&ct->proto, 0, sizeof(ct->proto));
	__nf_ct_set_timeout(ct, timeout * HZ);

out:
	if (opts->netns_id >= 0)
		put_net(net);

	return ct;
}

static struct nf_conn *__bpf_nf_ct_lookup(struct net *net,
					  struct bpf_sock_tuple *bpf_tuple,
					  u32 tuple_len, struct bpf_ct_opts *opts,
					  u32 opts_len)
{
	struct nf_conntrack_tuple_hash *hash;
	struct nf_conntrack_tuple tuple;
	struct nf_conn *ct;
	int err;

	if (!opts || !bpf_tuple || opts->reserved[0] || opts->reserved[1] ||
	    opts_len != NF_BPF_CT_OPTS_SZ)
		return ERR_PTR(-EINVAL);
	if (unlikely(opts->l4proto != IPPROTO_TCP && opts->l4proto != IPPROTO_UDP))
		return ERR_PTR(-EPROTO);
	if (unlikely(opts->netns_id < BPF_F_CURRENT_NETNS))
		return ERR_PTR(-EINVAL);

	err = bpf_nf_ct_tuple_parse(bpf_tuple, tuple_len, opts->l4proto,
				    IP_CT_DIR_ORIGINAL, &tuple);
	if (err < 0)
		return ERR_PTR(err);

	if (opts->netns_id >= 0) {
		net = get_net_ns_by_id(net, opts->netns_id);
		if (unlikely(!net))
			return ERR_PTR(-ENONET);
	}

	hash = nf_conntrack_find_get(net, &nf_ct_zone_dflt, &tuple);
	if (opts->netns_id >= 0)
		put_net(net);
	if (!hash)
		return ERR_PTR(-ENOENT);

	ct = nf_ct_tuplehash_to_ctrack(hash);
	opts->dir = NF_CT_DIRECTION(hash);

	return ct;
}

BTF_ID_LIST(btf_nf_conn_ids)
BTF_ID(struct, nf_conn)
BTF_ID(struct, nf_conn___init)

/* Check writes into `struct nf_conn` */
static int _nf_conntrack_btf_struct_access(struct bpf_verifier_log *log,
					   const struct bpf_reg_state *reg,
					   int off, int size, enum bpf_access_type atype,
					   u32 *next_btf_id, enum bpf_type_flag *flag)
{
	const struct btf_type *ncit, *nct, *t;
	size_t end;

	ncit = btf_type_by_id(reg->btf, btf_nf_conn_ids[1]);
	nct = btf_type_by_id(reg->btf, btf_nf_conn_ids[0]);
	t = btf_type_by_id(reg->btf, reg->btf_id);
	if (t != nct && t != ncit) {
		bpf_log(log, "only read is supported\n");
		return -EACCES;
	}

	/* `struct nf_conn` and `struct nf_conn___init` have the same layout
	 * so we are safe to simply merge offset checks here
	 */
	switch (off) {
#if defined(CONFIG_NF_CONNTRACK_MARK)
	case offsetof(struct nf_conn, mark):
		end = offsetofend(struct nf_conn, mark);
		break;
#endif
	default:
		bpf_log(log, "no write support to nf_conn at off %d\n", off);
		return -EACCES;
	}

	if (off + size > end) {
		bpf_log(log,
			"write access at off %d with size %d beyond the member of nf_conn ended at %zu\n",
			off, size, end);
		return -EACCES;
	}

	return 0;
}

__diag_push();
__diag_ignore_all("-Wmissing-prototypes",
		  "Global functions as their definitions will be in nf_conntrack BTF");

/* bpf_xdp_ct_alloc - Allocate a new CT entry
 *
 * Parameters:
 * @xdp_ctx	- Pointer to ctx (xdp_md) in XDP program
 *		    Cannot be NULL
 * @bpf_tuple	- Pointer to memory representing the tuple to look up
 *		    Cannot be NULL
 * @tuple__sz	- Length of the tuple structure
 *		    Must be one of sizeof(bpf_tuple->ipv4) or
 *		    sizeof(bpf_tuple->ipv6)
 * @opts	- Additional options for allocation (documented above)
 *		    Cannot be NULL
 * @opts__sz	- Length of the bpf_ct_opts structure
 *		    Must be NF_BPF_CT_OPTS_SZ (12)
 */
struct nf_conn___init *
bpf_xdp_ct_alloc(struct xdp_md *xdp_ctx, struct bpf_sock_tuple *bpf_tuple,
		 u32 tuple__sz, struct bpf_ct_opts *opts, u32 opts__sz)
{
	struct xdp_buff *ctx = (struct xdp_buff *)xdp_ctx;
	struct nf_conn *nfct;

	nfct = __bpf_nf_ct_alloc_entry(dev_net(ctx->rxq->dev), bpf_tuple, tuple__sz,
				       opts, opts__sz, 10);
	if (IS_ERR(nfct)) {
		if (opts)
			opts->error = PTR_ERR(nfct);
		return NULL;
	}

	return (struct nf_conn___init *)nfct;
}

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

	caller_net = dev_net(ctx->rxq->dev);
	nfct = __bpf_nf_ct_lookup(caller_net, bpf_tuple, tuple__sz, opts, opts__sz);
	if (IS_ERR(nfct)) {
		if (opts)
			opts->error = PTR_ERR(nfct);
		return NULL;
	}
	return nfct;
}

/* bpf_skb_ct_alloc - Allocate a new CT entry
 *
 * Parameters:
 * @skb_ctx	- Pointer to ctx (__sk_buff) in TC program
 *		    Cannot be NULL
 * @bpf_tuple	- Pointer to memory representing the tuple to look up
 *		    Cannot be NULL
 * @tuple__sz	- Length of the tuple structure
 *		    Must be one of sizeof(bpf_tuple->ipv4) or
 *		    sizeof(bpf_tuple->ipv6)
 * @opts	- Additional options for allocation (documented above)
 *		    Cannot be NULL
 * @opts__sz	- Length of the bpf_ct_opts structure
 *		    Must be NF_BPF_CT_OPTS_SZ (12)
 */
struct nf_conn___init *
bpf_skb_ct_alloc(struct __sk_buff *skb_ctx, struct bpf_sock_tuple *bpf_tuple,
		 u32 tuple__sz, struct bpf_ct_opts *opts, u32 opts__sz)
{
	struct sk_buff *skb = (struct sk_buff *)skb_ctx;
	struct nf_conn *nfct;
	struct net *net;

	net = skb->dev ? dev_net(skb->dev) : sock_net(skb->sk);
	nfct = __bpf_nf_ct_alloc_entry(net, bpf_tuple, tuple__sz, opts, opts__sz, 10);
	if (IS_ERR(nfct)) {
		if (opts)
			opts->error = PTR_ERR(nfct);
		return NULL;
	}

	return (struct nf_conn___init *)nfct;
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

	caller_net = skb->dev ? dev_net(skb->dev) : sock_net(skb->sk);
	nfct = __bpf_nf_ct_lookup(caller_net, bpf_tuple, tuple__sz, opts, opts__sz);
	if (IS_ERR(nfct)) {
		if (opts)
			opts->error = PTR_ERR(nfct);
		return NULL;
	}
	return nfct;
}

/* bpf_ct_insert_entry - Add the provided entry into a CT map
 *
 * This must be invoked for referenced PTR_TO_BTF_ID.
 *
 * @nfct	 - Pointer to referenced nf_conn___init object, obtained
 *		   using bpf_xdp_ct_alloc or bpf_skb_ct_alloc.
 */
struct nf_conn *bpf_ct_insert_entry(struct nf_conn___init *nfct_i)
{
	struct nf_conn *nfct = (struct nf_conn *)nfct_i;
	int err;

	err = nf_conntrack_hash_check_insert(nfct);
	if (err < 0) {
		nf_conntrack_free(nfct);
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

/* bpf_ct_set_timeout - Set timeout of allocated nf_conn
 *
 * Sets the default timeout of newly allocated nf_conn before insertion.
 * This helper must be invoked for refcounted pointer to nf_conn___init.
 *
 * Parameters:
 * @nfct	 - Pointer to referenced nf_conn object, obtained using
 *                 bpf_xdp_ct_alloc or bpf_skb_ct_alloc.
 * @timeout      - Timeout in msecs.
 */
void bpf_ct_set_timeout(struct nf_conn___init *nfct, u32 timeout)
{
	__nf_ct_set_timeout((struct nf_conn *)nfct, msecs_to_jiffies(timeout));
}

/* bpf_ct_change_timeout - Change timeout of inserted nf_conn
 *
 * Change timeout associated of the inserted or looked up nf_conn.
 * This helper must be invoked for refcounted pointer to nf_conn.
 *
 * Parameters:
 * @nfct	 - Pointer to referenced nf_conn object, obtained using
 *		   bpf_ct_insert_entry, bpf_xdp_ct_lookup, or bpf_skb_ct_lookup.
 * @timeout      - New timeout in msecs.
 */
int bpf_ct_change_timeout(struct nf_conn *nfct, u32 timeout)
{
	return __nf_ct_change_timeout(nfct, msecs_to_jiffies(timeout));
}

/* bpf_ct_set_status - Set status field of allocated nf_conn
 *
 * Set the status field of the newly allocated nf_conn before insertion.
 * This must be invoked for referenced PTR_TO_BTF_ID to nf_conn___init.
 *
 * Parameters:
 * @nfct	 - Pointer to referenced nf_conn object, obtained using
 *		   bpf_xdp_ct_alloc or bpf_skb_ct_alloc.
 * @status       - New status value.
 */
int bpf_ct_set_status(const struct nf_conn___init *nfct, u32 status)
{
	return nf_ct_change_status_common((struct nf_conn *)nfct, status);
}

/* bpf_ct_change_status - Change status of inserted nf_conn
 *
 * Change the status field of the provided connection tracking entry.
 * This must be invoked for referenced PTR_TO_BTF_ID to nf_conn.
 *
 * Parameters:
 * @nfct	 - Pointer to referenced nf_conn object, obtained using
 *		   bpf_ct_insert_entry, bpf_xdp_ct_lookup or bpf_skb_ct_lookup.
 * @status       - New status value.
 */
int bpf_ct_change_status(struct nf_conn *nfct, u32 status)
{
	return nf_ct_change_status_common(nfct, status);
}

__diag_pop()

BTF_SET8_START(nf_ct_kfunc_set)
BTF_ID_FLAGS(func, bpf_xdp_ct_alloc, KF_ACQUIRE | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_xdp_ct_lookup, KF_ACQUIRE | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_skb_ct_alloc, KF_ACQUIRE | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_skb_ct_lookup, KF_ACQUIRE | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_ct_insert_entry, KF_ACQUIRE | KF_RET_NULL | KF_RELEASE)
BTF_ID_FLAGS(func, bpf_ct_release, KF_RELEASE)
BTF_ID_FLAGS(func, bpf_ct_set_timeout, KF_TRUSTED_ARGS)
BTF_ID_FLAGS(func, bpf_ct_change_timeout, KF_TRUSTED_ARGS)
BTF_ID_FLAGS(func, bpf_ct_set_status, KF_TRUSTED_ARGS)
BTF_ID_FLAGS(func, bpf_ct_change_status, KF_TRUSTED_ARGS)
BTF_SET8_END(nf_ct_kfunc_set)

static const struct btf_kfunc_id_set nf_conntrack_kfunc_set = {
	.owner = THIS_MODULE,
	.set   = &nf_ct_kfunc_set,
};

int register_nf_conntrack_bpf(void)
{
	int ret;

	ret = register_btf_kfunc_id_set(BPF_PROG_TYPE_XDP, &nf_conntrack_kfunc_set);
	ret = ret ?: register_btf_kfunc_id_set(BPF_PROG_TYPE_SCHED_CLS, &nf_conntrack_kfunc_set);
	if (!ret) {
		mutex_lock(&nf_conn_btf_access_lock);
		nfct_btf_struct_access = _nf_conntrack_btf_struct_access;
		mutex_unlock(&nf_conn_btf_access_lock);
	}

	return ret;
}

void cleanup_nf_conntrack_bpf(void)
{
	mutex_lock(&nf_conn_btf_access_lock);
	nfct_btf_struct_access = NULL;
	mutex_unlock(&nf_conn_btf_access_lock);
}
