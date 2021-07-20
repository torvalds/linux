// SPDX-License-Identifier: GPL-2.0+
/*
 *  IPv6 IOAM implementation
 *
 *  Author:
 *  Justin Iurman <justin.iurman@uliege.be>
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/net.h>
#include <linux/ioam6.h>
#include <linux/rhashtable.h>

#include <net/addrconf.h>
#include <net/ioam6.h>

static void ioam6_ns_release(struct ioam6_namespace *ns)
{
	kfree_rcu(ns, rcu);
}

static void ioam6_sc_release(struct ioam6_schema *sc)
{
	kfree_rcu(sc, rcu);
}

static void ioam6_free_ns(void *ptr, void *arg)
{
	struct ioam6_namespace *ns = (struct ioam6_namespace *)ptr;

	if (ns)
		ioam6_ns_release(ns);
}

static void ioam6_free_sc(void *ptr, void *arg)
{
	struct ioam6_schema *sc = (struct ioam6_schema *)ptr;

	if (sc)
		ioam6_sc_release(sc);
}

static int ioam6_ns_cmpfn(struct rhashtable_compare_arg *arg, const void *obj)
{
	const struct ioam6_namespace *ns = obj;

	return (ns->id != *(__be16 *)arg->key);
}

static int ioam6_sc_cmpfn(struct rhashtable_compare_arg *arg, const void *obj)
{
	const struct ioam6_schema *sc = obj;

	return (sc->id != *(u32 *)arg->key);
}

static const struct rhashtable_params rht_ns_params = {
	.key_len		= sizeof(__be16),
	.key_offset		= offsetof(struct ioam6_namespace, id),
	.head_offset		= offsetof(struct ioam6_namespace, head),
	.automatic_shrinking	= true,
	.obj_cmpfn		= ioam6_ns_cmpfn,
};

static const struct rhashtable_params rht_sc_params = {
	.key_len		= sizeof(u32),
	.key_offset		= offsetof(struct ioam6_schema, id),
	.head_offset		= offsetof(struct ioam6_schema, head),
	.automatic_shrinking	= true,
	.obj_cmpfn		= ioam6_sc_cmpfn,
};

struct ioam6_namespace *ioam6_namespace(struct net *net, __be16 id)
{
	struct ioam6_pernet_data *nsdata = ioam6_pernet(net);

	return rhashtable_lookup_fast(&nsdata->namespaces, &id, rht_ns_params);
}

static void __ioam6_fill_trace_data(struct sk_buff *skb,
				    struct ioam6_namespace *ns,
				    struct ioam6_trace_hdr *trace,
				    struct ioam6_schema *sc,
				    u8 sclen)
{
	struct __kernel_sock_timeval ts;
	u64 raw64;
	u32 raw32;
	u16 raw16;
	u8 *data;
	u8 byte;

	data = trace->data + trace->remlen * 4 - trace->nodelen * 4 - sclen * 4;

	/* hop_lim and node_id */
	if (trace->type.bit0) {
		byte = ipv6_hdr(skb)->hop_limit;
		if (skb->dev)
			byte--;

		raw32 = dev_net(skb->dev)->ipv6.sysctl.ioam6_id;

		*(__be32 *)data = cpu_to_be32((byte << 24) | raw32);
		data += sizeof(__be32);
	}

	/* ingress_if_id and egress_if_id */
	if (trace->type.bit1) {
		if (!skb->dev)
			raw16 = IOAM6_U16_UNAVAILABLE;
		else
			raw16 = (__force u16)__in6_dev_get(skb->dev)->cnf.ioam6_id;

		*(__be16 *)data = cpu_to_be16(raw16);
		data += sizeof(__be16);

		if (skb_dst(skb)->dev->flags & IFF_LOOPBACK)
			raw16 = IOAM6_U16_UNAVAILABLE;
		else
			raw16 = (__force u16)__in6_dev_get(skb_dst(skb)->dev)->cnf.ioam6_id;

		*(__be16 *)data = cpu_to_be16(raw16);
		data += sizeof(__be16);
	}

	/* timestamp seconds */
	if (trace->type.bit2) {
		if (!skb->tstamp)
			__net_timestamp(skb);

		skb_get_new_timestamp(skb, &ts);

		*(__be32 *)data = cpu_to_be32((u32)ts.tv_sec);
		data += sizeof(__be32);
	}

	/* timestamp subseconds */
	if (trace->type.bit3) {
		if (!skb->tstamp)
			__net_timestamp(skb);

		if (!trace->type.bit2)
			skb_get_new_timestamp(skb, &ts);

		*(__be32 *)data = cpu_to_be32((u32)ts.tv_usec);
		data += sizeof(__be32);
	}

	/* transit delay */
	if (trace->type.bit4) {
		*(__be32 *)data = cpu_to_be32(IOAM6_U32_UNAVAILABLE);
		data += sizeof(__be32);
	}

	/* namespace data */
	if (trace->type.bit5) {
		*(__be32 *)data = ns->data;
		data += sizeof(__be32);
	}

	/* queue depth */
	if (trace->type.bit6) {
		*(__be32 *)data = cpu_to_be32(IOAM6_U32_UNAVAILABLE);
		data += sizeof(__be32);
	}

	/* checksum complement */
	if (trace->type.bit7) {
		*(__be32 *)data = cpu_to_be32(IOAM6_U32_UNAVAILABLE);
		data += sizeof(__be32);
	}

	/* hop_lim and node_id (wide) */
	if (trace->type.bit8) {
		byte = ipv6_hdr(skb)->hop_limit;
		if (skb->dev)
			byte--;

		raw64 = dev_net(skb->dev)->ipv6.sysctl.ioam6_id_wide;

		*(__be64 *)data = cpu_to_be64(((u64)byte << 56) | raw64);
		data += sizeof(__be64);
	}

	/* ingress_if_id and egress_if_id (wide) */
	if (trace->type.bit9) {
		if (!skb->dev)
			raw32 = IOAM6_U32_UNAVAILABLE;
		else
			raw32 = __in6_dev_get(skb->dev)->cnf.ioam6_id_wide;

		*(__be32 *)data = cpu_to_be32(raw32);
		data += sizeof(__be32);

		if (skb_dst(skb)->dev->flags & IFF_LOOPBACK)
			raw32 = IOAM6_U32_UNAVAILABLE;
		else
			raw32 = __in6_dev_get(skb_dst(skb)->dev)->cnf.ioam6_id_wide;

		*(__be32 *)data = cpu_to_be32(raw32);
		data += sizeof(__be32);
	}

	/* namespace data (wide) */
	if (trace->type.bit10) {
		*(__be64 *)data = ns->data_wide;
		data += sizeof(__be64);
	}

	/* buffer occupancy */
	if (trace->type.bit11) {
		*(__be32 *)data = cpu_to_be32(IOAM6_U32_UNAVAILABLE);
		data += sizeof(__be32);
	}

	/* opaque state snapshot */
	if (trace->type.bit22) {
		if (!sc) {
			*(__be32 *)data = cpu_to_be32(IOAM6_U32_UNAVAILABLE >> 8);
		} else {
			*(__be32 *)data = sc->hdr;
			data += sizeof(__be32);

			memcpy(data, sc->data, sc->len);
		}
	}
}

/* called with rcu_read_lock() */
void ioam6_fill_trace_data(struct sk_buff *skb,
			   struct ioam6_namespace *ns,
			   struct ioam6_trace_hdr *trace)
{
	struct ioam6_schema *sc;
	u8 sclen = 0;

	/* Skip if Overflow flag is set OR
	 * if an unknown type (bit 12-21) is set
	 */
	if (trace->overflow ||
	    trace->type.bit12 | trace->type.bit13 | trace->type.bit14 |
	    trace->type.bit15 | trace->type.bit16 | trace->type.bit17 |
	    trace->type.bit18 | trace->type.bit19 | trace->type.bit20 |
	    trace->type.bit21) {
		return;
	}

	/* NodeLen does not include Opaque State Snapshot length. We need to
	 * take it into account if the corresponding bit is set (bit 22) and
	 * if the current IOAM namespace has an active schema attached to it
	 */
	sc = rcu_dereference(ns->schema);
	if (trace->type.bit22) {
		sclen = sizeof_field(struct ioam6_schema, hdr) / 4;

		if (sc)
			sclen += sc->len / 4;
	}

	/* If there is no space remaining, we set the Overflow flag and we
	 * skip without filling the trace
	 */
	if (!trace->remlen || trace->remlen < trace->nodelen + sclen) {
		trace->overflow = 1;
		return;
	}

	__ioam6_fill_trace_data(skb, ns, trace, sc, sclen);
	trace->remlen -= trace->nodelen + sclen;
}

static int __net_init ioam6_net_init(struct net *net)
{
	struct ioam6_pernet_data *nsdata;
	int err = -ENOMEM;

	nsdata = kzalloc(sizeof(*nsdata), GFP_KERNEL);
	if (!nsdata)
		goto out;

	mutex_init(&nsdata->lock);
	net->ipv6.ioam6_data = nsdata;

	err = rhashtable_init(&nsdata->namespaces, &rht_ns_params);
	if (err)
		goto free_nsdata;

	err = rhashtable_init(&nsdata->schemas, &rht_sc_params);
	if (err)
		goto free_rht_ns;

out:
	return err;
free_rht_ns:
	rhashtable_destroy(&nsdata->namespaces);
free_nsdata:
	kfree(nsdata);
	net->ipv6.ioam6_data = NULL;
	goto out;
}

static void __net_exit ioam6_net_exit(struct net *net)
{
	struct ioam6_pernet_data *nsdata = ioam6_pernet(net);

	rhashtable_free_and_destroy(&nsdata->namespaces, ioam6_free_ns, NULL);
	rhashtable_free_and_destroy(&nsdata->schemas, ioam6_free_sc, NULL);

	kfree(nsdata);
}

static struct pernet_operations ioam6_net_ops = {
	.init = ioam6_net_init,
	.exit = ioam6_net_exit,
};

int __init ioam6_init(void)
{
	int err = register_pernet_subsys(&ioam6_net_ops);

	if (err)
		return err;

	pr_info("In-situ OAM (IOAM) with IPv6\n");
	return 0;
}

void ioam6_exit(void)
{
	unregister_pernet_subsys(&ioam6_net_ops);
}
