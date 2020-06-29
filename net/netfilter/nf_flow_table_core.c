// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/rhashtable.h>
#include <linux/netdevice.h>
#include <net/ip.h>
#include <net/ip6_route.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nf_flow_table.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_tuple.h>

static DEFINE_MUTEX(flowtable_lock);
static LIST_HEAD(flowtables);

static void
flow_offload_fill_dir(struct flow_offload *flow,
		      enum flow_offload_tuple_dir dir)
{
	struct flow_offload_tuple *ft = &flow->tuplehash[dir].tuple;
	struct nf_conntrack_tuple *ctt = &flow->ct->tuplehash[dir].tuple;

	ft->dir = dir;

	switch (ctt->src.l3num) {
	case NFPROTO_IPV4:
		ft->src_v4 = ctt->src.u3.in;
		ft->dst_v4 = ctt->dst.u3.in;
		break;
	case NFPROTO_IPV6:
		ft->src_v6 = ctt->src.u3.in6;
		ft->dst_v6 = ctt->dst.u3.in6;
		break;
	}

	ft->l3proto = ctt->src.l3num;
	ft->l4proto = ctt->dst.protonum;
	ft->src_port = ctt->src.u.tcp.port;
	ft->dst_port = ctt->dst.u.tcp.port;
}

struct flow_offload *flow_offload_alloc(struct nf_conn *ct)
{
	struct flow_offload *flow;

	if (unlikely(nf_ct_is_dying(ct) ||
	    !atomic_inc_not_zero(&ct->ct_general.use)))
		return NULL;

	flow = kzalloc(sizeof(*flow), GFP_ATOMIC);
	if (!flow)
		goto err_ct_refcnt;

	flow->ct = ct;

	flow_offload_fill_dir(flow, FLOW_OFFLOAD_DIR_ORIGINAL);
	flow_offload_fill_dir(flow, FLOW_OFFLOAD_DIR_REPLY);

	if (ct->status & IPS_SRC_NAT)
		__set_bit(NF_FLOW_SNAT, &flow->flags);
	if (ct->status & IPS_DST_NAT)
		__set_bit(NF_FLOW_DNAT, &flow->flags);

	return flow;

err_ct_refcnt:
	nf_ct_put(ct);

	return NULL;
}
EXPORT_SYMBOL_GPL(flow_offload_alloc);

static int flow_offload_fill_route(struct flow_offload *flow,
				   const struct nf_flow_route *route,
				   enum flow_offload_tuple_dir dir)
{
	struct flow_offload_tuple *flow_tuple = &flow->tuplehash[dir].tuple;
	struct dst_entry *other_dst = route->tuple[!dir].dst;
	struct dst_entry *dst = route->tuple[dir].dst;

	if (!dst_hold_safe(route->tuple[dir].dst))
		return -1;

	switch (flow_tuple->l3proto) {
	case NFPROTO_IPV4:
		flow_tuple->mtu = ip_dst_mtu_maybe_forward(dst, true);
		break;
	case NFPROTO_IPV6:
		flow_tuple->mtu = ip6_dst_mtu_forward(dst);
		break;
	}

	flow_tuple->iifidx = other_dst->dev->ifindex;
	flow_tuple->dst_cache = dst;

	return 0;
}

int flow_offload_route_init(struct flow_offload *flow,
			    const struct nf_flow_route *route)
{
	int err;

	err = flow_offload_fill_route(flow, route, FLOW_OFFLOAD_DIR_ORIGINAL);
	if (err < 0)
		return err;

	err = flow_offload_fill_route(flow, route, FLOW_OFFLOAD_DIR_REPLY);
	if (err < 0)
		goto err_route_reply;

	flow->type = NF_FLOW_OFFLOAD_ROUTE;

	return 0;

err_route_reply:
	dst_release(route->tuple[FLOW_OFFLOAD_DIR_ORIGINAL].dst);

	return err;
}
EXPORT_SYMBOL_GPL(flow_offload_route_init);

static void flow_offload_fixup_tcp(struct ip_ct_tcp *tcp)
{
	tcp->state = TCP_CONNTRACK_ESTABLISHED;
	tcp->seen[0].td_maxwin = 0;
	tcp->seen[1].td_maxwin = 0;
}

#define NF_FLOWTABLE_TCP_PICKUP_TIMEOUT	(120 * HZ)
#define NF_FLOWTABLE_UDP_PICKUP_TIMEOUT	(30 * HZ)

static void flow_offload_fixup_ct_timeout(struct nf_conn *ct)
{
	const struct nf_conntrack_l4proto *l4proto;
	int l4num = nf_ct_protonum(ct);
	unsigned int timeout;

	l4proto = nf_ct_l4proto_find(l4num);
	if (!l4proto)
		return;

	if (l4num == IPPROTO_TCP)
		timeout = NF_FLOWTABLE_TCP_PICKUP_TIMEOUT;
	else if (l4num == IPPROTO_UDP)
		timeout = NF_FLOWTABLE_UDP_PICKUP_TIMEOUT;
	else
		return;

	if (nf_flow_timeout_delta(ct->timeout) > (__s32)timeout)
		ct->timeout = nfct_time_stamp + timeout;
}

static void flow_offload_fixup_ct_state(struct nf_conn *ct)
{
	if (nf_ct_protonum(ct) == IPPROTO_TCP)
		flow_offload_fixup_tcp(&ct->proto.tcp);
}

static void flow_offload_fixup_ct(struct nf_conn *ct)
{
	flow_offload_fixup_ct_state(ct);
	flow_offload_fixup_ct_timeout(ct);
}

static void flow_offload_route_release(struct flow_offload *flow)
{
	dst_release(flow->tuplehash[FLOW_OFFLOAD_DIR_ORIGINAL].tuple.dst_cache);
	dst_release(flow->tuplehash[FLOW_OFFLOAD_DIR_REPLY].tuple.dst_cache);
}

void flow_offload_free(struct flow_offload *flow)
{
	switch (flow->type) {
	case NF_FLOW_OFFLOAD_ROUTE:
		flow_offload_route_release(flow);
		break;
	default:
		break;
	}
	nf_ct_put(flow->ct);
	kfree_rcu(flow, rcu_head);
}
EXPORT_SYMBOL_GPL(flow_offload_free);

static u32 flow_offload_hash(const void *data, u32 len, u32 seed)
{
	const struct flow_offload_tuple *tuple = data;

	return jhash(tuple, offsetof(struct flow_offload_tuple, dir), seed);
}

static u32 flow_offload_hash_obj(const void *data, u32 len, u32 seed)
{
	const struct flow_offload_tuple_rhash *tuplehash = data;

	return jhash(&tuplehash->tuple, offsetof(struct flow_offload_tuple, dir), seed);
}

static int flow_offload_hash_cmp(struct rhashtable_compare_arg *arg,
					const void *ptr)
{
	const struct flow_offload_tuple *tuple = arg->key;
	const struct flow_offload_tuple_rhash *x = ptr;

	if (memcmp(&x->tuple, tuple, offsetof(struct flow_offload_tuple, dir)))
		return 1;

	return 0;
}

static const struct rhashtable_params nf_flow_offload_rhash_params = {
	.head_offset		= offsetof(struct flow_offload_tuple_rhash, node),
	.hashfn			= flow_offload_hash,
	.obj_hashfn		= flow_offload_hash_obj,
	.obj_cmpfn		= flow_offload_hash_cmp,
	.automatic_shrinking	= true,
};

int flow_offload_add(struct nf_flowtable *flow_table, struct flow_offload *flow)
{
	int err;

	flow->timeout = nf_flowtable_time_stamp + NF_FLOW_TIMEOUT;

	err = rhashtable_insert_fast(&flow_table->rhashtable,
				     &flow->tuplehash[0].node,
				     nf_flow_offload_rhash_params);
	if (err < 0)
		return err;

	err = rhashtable_insert_fast(&flow_table->rhashtable,
				     &flow->tuplehash[1].node,
				     nf_flow_offload_rhash_params);
	if (err < 0) {
		rhashtable_remove_fast(&flow_table->rhashtable,
				       &flow->tuplehash[0].node,
				       nf_flow_offload_rhash_params);
		return err;
	}

	if (nf_flowtable_hw_offload(flow_table)) {
		__set_bit(NF_FLOW_HW, &flow->flags);
		nf_flow_offload_add(flow_table, flow);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(flow_offload_add);

void flow_offload_refresh(struct nf_flowtable *flow_table,
			  struct flow_offload *flow)
{
	flow->timeout = nf_flowtable_time_stamp + NF_FLOW_TIMEOUT;

	if (likely(!nf_flowtable_hw_offload(flow_table) ||
		   !test_and_clear_bit(NF_FLOW_HW_REFRESH, &flow->flags)))
		return;

	nf_flow_offload_add(flow_table, flow);
}
EXPORT_SYMBOL_GPL(flow_offload_refresh);

static inline bool nf_flow_has_expired(const struct flow_offload *flow)
{
	return nf_flow_timeout_delta(flow->timeout) <= 0;
}

static void flow_offload_del(struct nf_flowtable *flow_table,
			     struct flow_offload *flow)
{
	rhashtable_remove_fast(&flow_table->rhashtable,
			       &flow->tuplehash[FLOW_OFFLOAD_DIR_ORIGINAL].node,
			       nf_flow_offload_rhash_params);
	rhashtable_remove_fast(&flow_table->rhashtable,
			       &flow->tuplehash[FLOW_OFFLOAD_DIR_REPLY].node,
			       nf_flow_offload_rhash_params);

	clear_bit(IPS_OFFLOAD_BIT, &flow->ct->status);

	if (nf_flow_has_expired(flow))
		flow_offload_fixup_ct(flow->ct);
	else
		flow_offload_fixup_ct_timeout(flow->ct);

	flow_offload_free(flow);
}

void flow_offload_teardown(struct flow_offload *flow)
{
	set_bit(NF_FLOW_TEARDOWN, &flow->flags);

	flow_offload_fixup_ct_state(flow->ct);
}
EXPORT_SYMBOL_GPL(flow_offload_teardown);

struct flow_offload_tuple_rhash *
flow_offload_lookup(struct nf_flowtable *flow_table,
		    struct flow_offload_tuple *tuple)
{
	struct flow_offload_tuple_rhash *tuplehash;
	struct flow_offload *flow;
	int dir;

	tuplehash = rhashtable_lookup(&flow_table->rhashtable, tuple,
				      nf_flow_offload_rhash_params);
	if (!tuplehash)
		return NULL;

	dir = tuplehash->tuple.dir;
	flow = container_of(tuplehash, struct flow_offload, tuplehash[dir]);
	if (test_bit(NF_FLOW_TEARDOWN, &flow->flags))
		return NULL;

	if (unlikely(nf_ct_is_dying(flow->ct)))
		return NULL;

	return tuplehash;
}
EXPORT_SYMBOL_GPL(flow_offload_lookup);

static int
nf_flow_table_iterate(struct nf_flowtable *flow_table,
		      void (*iter)(struct flow_offload *flow, void *data),
		      void *data)
{
	struct flow_offload_tuple_rhash *tuplehash;
	struct rhashtable_iter hti;
	struct flow_offload *flow;
	int err = 0;

	rhashtable_walk_enter(&flow_table->rhashtable, &hti);
	rhashtable_walk_start(&hti);

	while ((tuplehash = rhashtable_walk_next(&hti))) {
		if (IS_ERR(tuplehash)) {
			if (PTR_ERR(tuplehash) != -EAGAIN) {
				err = PTR_ERR(tuplehash);
				break;
			}
			continue;
		}
		if (tuplehash->tuple.dir)
			continue;

		flow = container_of(tuplehash, struct flow_offload, tuplehash[0]);

		iter(flow, data);
	}
	rhashtable_walk_stop(&hti);
	rhashtable_walk_exit(&hti);

	return err;
}

static void nf_flow_offload_gc_step(struct flow_offload *flow, void *data)
{
	struct nf_flowtable *flow_table = data;

	if (nf_flow_has_expired(flow) || nf_ct_is_dying(flow->ct))
		set_bit(NF_FLOW_TEARDOWN, &flow->flags);

	if (test_bit(NF_FLOW_TEARDOWN, &flow->flags)) {
		if (test_bit(NF_FLOW_HW, &flow->flags)) {
			if (!test_bit(NF_FLOW_HW_DYING, &flow->flags))
				nf_flow_offload_del(flow_table, flow);
			else if (test_bit(NF_FLOW_HW_DEAD, &flow->flags))
				flow_offload_del(flow_table, flow);
		} else {
			flow_offload_del(flow_table, flow);
		}
	} else if (test_bit(NF_FLOW_HW, &flow->flags)) {
		nf_flow_offload_stats(flow_table, flow);
	}
}

static void nf_flow_offload_work_gc(struct work_struct *work)
{
	struct nf_flowtable *flow_table;

	flow_table = container_of(work, struct nf_flowtable, gc_work.work);
	nf_flow_table_iterate(flow_table, nf_flow_offload_gc_step, flow_table);
	queue_delayed_work(system_power_efficient_wq, &flow_table->gc_work, HZ);
}


static int nf_flow_nat_port_tcp(struct sk_buff *skb, unsigned int thoff,
				__be16 port, __be16 new_port)
{
	struct tcphdr *tcph;

	if (!pskb_may_pull(skb, thoff + sizeof(*tcph)) ||
	    skb_try_make_writable(skb, thoff + sizeof(*tcph)))
		return -1;

	tcph = (void *)(skb_network_header(skb) + thoff);
	inet_proto_csum_replace2(&tcph->check, skb, port, new_port, true);

	return 0;
}

static int nf_flow_nat_port_udp(struct sk_buff *skb, unsigned int thoff,
				__be16 port, __be16 new_port)
{
	struct udphdr *udph;

	if (!pskb_may_pull(skb, thoff + sizeof(*udph)) ||
	    skb_try_make_writable(skb, thoff + sizeof(*udph)))
		return -1;

	udph = (void *)(skb_network_header(skb) + thoff);
	if (udph->check || skb->ip_summed == CHECKSUM_PARTIAL) {
		inet_proto_csum_replace2(&udph->check, skb, port,
					 new_port, true);
		if (!udph->check)
			udph->check = CSUM_MANGLED_0;
	}

	return 0;
}

static int nf_flow_nat_port(struct sk_buff *skb, unsigned int thoff,
			    u8 protocol, __be16 port, __be16 new_port)
{
	switch (protocol) {
	case IPPROTO_TCP:
		if (nf_flow_nat_port_tcp(skb, thoff, port, new_port) < 0)
			return NF_DROP;
		break;
	case IPPROTO_UDP:
		if (nf_flow_nat_port_udp(skb, thoff, port, new_port) < 0)
			return NF_DROP;
		break;
	}

	return 0;
}

int nf_flow_snat_port(const struct flow_offload *flow,
		      struct sk_buff *skb, unsigned int thoff,
		      u8 protocol, enum flow_offload_tuple_dir dir)
{
	struct flow_ports *hdr;
	__be16 port, new_port;

	if (!pskb_may_pull(skb, thoff + sizeof(*hdr)) ||
	    skb_try_make_writable(skb, thoff + sizeof(*hdr)))
		return -1;

	hdr = (void *)(skb_network_header(skb) + thoff);

	switch (dir) {
	case FLOW_OFFLOAD_DIR_ORIGINAL:
		port = hdr->source;
		new_port = flow->tuplehash[FLOW_OFFLOAD_DIR_REPLY].tuple.dst_port;
		hdr->source = new_port;
		break;
	case FLOW_OFFLOAD_DIR_REPLY:
		port = hdr->dest;
		new_port = flow->tuplehash[FLOW_OFFLOAD_DIR_ORIGINAL].tuple.src_port;
		hdr->dest = new_port;
		break;
	default:
		return -1;
	}

	return nf_flow_nat_port(skb, thoff, protocol, port, new_port);
}
EXPORT_SYMBOL_GPL(nf_flow_snat_port);

int nf_flow_dnat_port(const struct flow_offload *flow,
		      struct sk_buff *skb, unsigned int thoff,
		      u8 protocol, enum flow_offload_tuple_dir dir)
{
	struct flow_ports *hdr;
	__be16 port, new_port;

	if (!pskb_may_pull(skb, thoff + sizeof(*hdr)) ||
	    skb_try_make_writable(skb, thoff + sizeof(*hdr)))
		return -1;

	hdr = (void *)(skb_network_header(skb) + thoff);

	switch (dir) {
	case FLOW_OFFLOAD_DIR_ORIGINAL:
		port = hdr->dest;
		new_port = flow->tuplehash[FLOW_OFFLOAD_DIR_REPLY].tuple.src_port;
		hdr->dest = new_port;
		break;
	case FLOW_OFFLOAD_DIR_REPLY:
		port = hdr->source;
		new_port = flow->tuplehash[FLOW_OFFLOAD_DIR_ORIGINAL].tuple.dst_port;
		hdr->source = new_port;
		break;
	default:
		return -1;
	}

	return nf_flow_nat_port(skb, thoff, protocol, port, new_port);
}
EXPORT_SYMBOL_GPL(nf_flow_dnat_port);

int nf_flow_table_init(struct nf_flowtable *flowtable)
{
	int err;

	INIT_DEFERRABLE_WORK(&flowtable->gc_work, nf_flow_offload_work_gc);
	flow_block_init(&flowtable->flow_block);
	init_rwsem(&flowtable->flow_block_lock);

	err = rhashtable_init(&flowtable->rhashtable,
			      &nf_flow_offload_rhash_params);
	if (err < 0)
		return err;

	queue_delayed_work(system_power_efficient_wq,
			   &flowtable->gc_work, HZ);

	mutex_lock(&flowtable_lock);
	list_add(&flowtable->list, &flowtables);
	mutex_unlock(&flowtable_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(nf_flow_table_init);

static void nf_flow_table_do_cleanup(struct flow_offload *flow, void *data)
{
	struct net_device *dev = data;

	if (!dev) {
		flow_offload_teardown(flow);
		return;
	}

	if (net_eq(nf_ct_net(flow->ct), dev_net(dev)) &&
	    (flow->tuplehash[0].tuple.iifidx == dev->ifindex ||
	     flow->tuplehash[1].tuple.iifidx == dev->ifindex))
		flow_offload_teardown(flow);
}

void nf_flow_table_gc_cleanup(struct nf_flowtable *flowtable,
			      struct net_device *dev)
{
	nf_flow_table_iterate(flowtable, nf_flow_table_do_cleanup, dev);
	flush_delayed_work(&flowtable->gc_work);
	nf_flow_table_offload_flush(flowtable);
}

void nf_flow_table_cleanup(struct net_device *dev)
{
	struct nf_flowtable *flowtable;

	mutex_lock(&flowtable_lock);
	list_for_each_entry(flowtable, &flowtables, list)
		nf_flow_table_gc_cleanup(flowtable, dev);
	mutex_unlock(&flowtable_lock);
}
EXPORT_SYMBOL_GPL(nf_flow_table_cleanup);

void nf_flow_table_free(struct nf_flowtable *flow_table)
{
	mutex_lock(&flowtable_lock);
	list_del(&flow_table->list);
	mutex_unlock(&flowtable_lock);

	cancel_delayed_work_sync(&flow_table->gc_work);
	nf_flow_table_iterate(flow_table, nf_flow_table_do_cleanup, NULL);
	nf_flow_table_iterate(flow_table, nf_flow_offload_gc_step, flow_table);
	nf_flow_table_offload_flush(flow_table);
	if (nf_flowtable_hw_offload(flow_table))
		nf_flow_table_iterate(flow_table, nf_flow_offload_gc_step,
				      flow_table);
	rhashtable_destroy(&flow_table->rhashtable);
}
EXPORT_SYMBOL_GPL(nf_flow_table_free);

static int __init nf_flow_table_module_init(void)
{
	return nf_flow_table_offload_init();
}

static void __exit nf_flow_table_module_exit(void)
{
	nf_flow_table_offload_exit();
}

module_init(nf_flow_table_module_init);
module_exit(nf_flow_table_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pablo Neira Ayuso <pablo@netfilter.org>");
