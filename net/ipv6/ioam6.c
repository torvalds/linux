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
#include <linux/ioam6_genl.h>
#include <linux/rhashtable.h>
#include <linux/netdevice.h>

#include <net/addrconf.h>
#include <net/genetlink.h>
#include <net/ioam6.h>
#include <net/sch_generic.h>

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

static struct genl_family ioam6_genl_family;

static const struct nla_policy ioam6_genl_policy_addns[] = {
	[IOAM6_ATTR_NS_ID]	= { .type = NLA_U16 },
	[IOAM6_ATTR_NS_DATA]	= { .type = NLA_U32 },
	[IOAM6_ATTR_NS_DATA_WIDE] = { .type = NLA_U64 },
};

static const struct nla_policy ioam6_genl_policy_delns[] = {
	[IOAM6_ATTR_NS_ID]	= { .type = NLA_U16 },
};

static const struct nla_policy ioam6_genl_policy_addsc[] = {
	[IOAM6_ATTR_SC_ID]	= { .type = NLA_U32 },
	[IOAM6_ATTR_SC_DATA]	= { .type = NLA_BINARY,
				    .len = IOAM6_MAX_SCHEMA_DATA_LEN },
};

static const struct nla_policy ioam6_genl_policy_delsc[] = {
	[IOAM6_ATTR_SC_ID]	= { .type = NLA_U32 },
};

static const struct nla_policy ioam6_genl_policy_ns_sc[] = {
	[IOAM6_ATTR_NS_ID]	= { .type = NLA_U16 },
	[IOAM6_ATTR_SC_ID]	= { .type = NLA_U32 },
	[IOAM6_ATTR_SC_NONE]	= { .type = NLA_FLAG },
};

static int ioam6_genl_addns(struct sk_buff *skb, struct genl_info *info)
{
	struct ioam6_pernet_data *nsdata;
	struct ioam6_namespace *ns;
	u64 data64;
	u32 data32;
	__be16 id;
	int err;

	if (!info->attrs[IOAM6_ATTR_NS_ID])
		return -EINVAL;

	id = cpu_to_be16(nla_get_u16(info->attrs[IOAM6_ATTR_NS_ID]));
	nsdata = ioam6_pernet(genl_info_net(info));

	mutex_lock(&nsdata->lock);

	ns = rhashtable_lookup_fast(&nsdata->namespaces, &id, rht_ns_params);
	if (ns) {
		err = -EEXIST;
		goto out_unlock;
	}

	ns = kzalloc(sizeof(*ns), GFP_KERNEL);
	if (!ns) {
		err = -ENOMEM;
		goto out_unlock;
	}

	ns->id = id;

	if (!info->attrs[IOAM6_ATTR_NS_DATA])
		data32 = IOAM6_U32_UNAVAILABLE;
	else
		data32 = nla_get_u32(info->attrs[IOAM6_ATTR_NS_DATA]);

	if (!info->attrs[IOAM6_ATTR_NS_DATA_WIDE])
		data64 = IOAM6_U64_UNAVAILABLE;
	else
		data64 = nla_get_u64(info->attrs[IOAM6_ATTR_NS_DATA_WIDE]);

	ns->data = cpu_to_be32(data32);
	ns->data_wide = cpu_to_be64(data64);

	err = rhashtable_lookup_insert_fast(&nsdata->namespaces, &ns->head,
					    rht_ns_params);
	if (err)
		kfree(ns);

out_unlock:
	mutex_unlock(&nsdata->lock);
	return err;
}

static int ioam6_genl_delns(struct sk_buff *skb, struct genl_info *info)
{
	struct ioam6_pernet_data *nsdata;
	struct ioam6_namespace *ns;
	struct ioam6_schema *sc;
	__be16 id;
	int err;

	if (!info->attrs[IOAM6_ATTR_NS_ID])
		return -EINVAL;

	id = cpu_to_be16(nla_get_u16(info->attrs[IOAM6_ATTR_NS_ID]));
	nsdata = ioam6_pernet(genl_info_net(info));

	mutex_lock(&nsdata->lock);

	ns = rhashtable_lookup_fast(&nsdata->namespaces, &id, rht_ns_params);
	if (!ns) {
		err = -ENOENT;
		goto out_unlock;
	}

	sc = rcu_dereference_protected(ns->schema,
				       lockdep_is_held(&nsdata->lock));

	err = rhashtable_remove_fast(&nsdata->namespaces, &ns->head,
				     rht_ns_params);
	if (err)
		goto out_unlock;

	if (sc)
		rcu_assign_pointer(sc->ns, NULL);

	ioam6_ns_release(ns);

out_unlock:
	mutex_unlock(&nsdata->lock);
	return err;
}

static int __ioam6_genl_dumpns_element(struct ioam6_namespace *ns,
				       u32 portid,
				       u32 seq,
				       u32 flags,
				       struct sk_buff *skb,
				       u8 cmd)
{
	struct ioam6_schema *sc;
	u64 data64;
	u32 data32;
	void *hdr;

	hdr = genlmsg_put(skb, portid, seq, &ioam6_genl_family, flags, cmd);
	if (!hdr)
		return -ENOMEM;

	data32 = be32_to_cpu(ns->data);
	data64 = be64_to_cpu(ns->data_wide);

	if (nla_put_u16(skb, IOAM6_ATTR_NS_ID, be16_to_cpu(ns->id)) ||
	    (data32 != IOAM6_U32_UNAVAILABLE &&
	     nla_put_u32(skb, IOAM6_ATTR_NS_DATA, data32)) ||
	    (data64 != IOAM6_U64_UNAVAILABLE &&
	     nla_put_u64_64bit(skb, IOAM6_ATTR_NS_DATA_WIDE,
			       data64, IOAM6_ATTR_PAD)))
		goto nla_put_failure;

	rcu_read_lock();

	sc = rcu_dereference(ns->schema);
	if (sc && nla_put_u32(skb, IOAM6_ATTR_SC_ID, sc->id)) {
		rcu_read_unlock();
		goto nla_put_failure;
	}

	rcu_read_unlock();

	genlmsg_end(skb, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(skb, hdr);
	return -EMSGSIZE;
}

static int ioam6_genl_dumpns_start(struct netlink_callback *cb)
{
	struct ioam6_pernet_data *nsdata = ioam6_pernet(sock_net(cb->skb->sk));
	struct rhashtable_iter *iter = (struct rhashtable_iter *)cb->args[0];

	if (!iter) {
		iter = kmalloc(sizeof(*iter), GFP_KERNEL);
		if (!iter)
			return -ENOMEM;

		cb->args[0] = (long)iter;
	}

	rhashtable_walk_enter(&nsdata->namespaces, iter);

	return 0;
}

static int ioam6_genl_dumpns_done(struct netlink_callback *cb)
{
	struct rhashtable_iter *iter = (struct rhashtable_iter *)cb->args[0];

	rhashtable_walk_exit(iter);
	kfree(iter);

	return 0;
}

static int ioam6_genl_dumpns(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct rhashtable_iter *iter;
	struct ioam6_namespace *ns;
	int err;

	iter = (struct rhashtable_iter *)cb->args[0];
	rhashtable_walk_start(iter);

	for (;;) {
		ns = rhashtable_walk_next(iter);

		if (IS_ERR(ns)) {
			if (PTR_ERR(ns) == -EAGAIN)
				continue;
			err = PTR_ERR(ns);
			goto done;
		} else if (!ns) {
			break;
		}

		err = __ioam6_genl_dumpns_element(ns,
						  NETLINK_CB(cb->skb).portid,
						  cb->nlh->nlmsg_seq,
						  NLM_F_MULTI,
						  skb,
						  IOAM6_CMD_DUMP_NAMESPACES);
		if (err)
			goto done;
	}

	err = skb->len;

done:
	rhashtable_walk_stop(iter);
	return err;
}

static int ioam6_genl_addsc(struct sk_buff *skb, struct genl_info *info)
{
	struct ioam6_pernet_data *nsdata;
	int len, len_aligned, err;
	struct ioam6_schema *sc;
	u32 id;

	if (!info->attrs[IOAM6_ATTR_SC_ID] || !info->attrs[IOAM6_ATTR_SC_DATA])
		return -EINVAL;

	id = nla_get_u32(info->attrs[IOAM6_ATTR_SC_ID]);
	nsdata = ioam6_pernet(genl_info_net(info));

	mutex_lock(&nsdata->lock);

	sc = rhashtable_lookup_fast(&nsdata->schemas, &id, rht_sc_params);
	if (sc) {
		err = -EEXIST;
		goto out_unlock;
	}

	len = nla_len(info->attrs[IOAM6_ATTR_SC_DATA]);
	len_aligned = ALIGN(len, 4);

	sc = kzalloc(sizeof(*sc) + len_aligned, GFP_KERNEL);
	if (!sc) {
		err = -ENOMEM;
		goto out_unlock;
	}

	sc->id = id;
	sc->len = len_aligned;
	sc->hdr = cpu_to_be32(sc->id | ((u8)(sc->len / 4) << 24));
	nla_memcpy(sc->data, info->attrs[IOAM6_ATTR_SC_DATA], len);

	err = rhashtable_lookup_insert_fast(&nsdata->schemas, &sc->head,
					    rht_sc_params);
	if (err)
		goto free_sc;

out_unlock:
	mutex_unlock(&nsdata->lock);
	return err;
free_sc:
	kfree(sc);
	goto out_unlock;
}

static int ioam6_genl_delsc(struct sk_buff *skb, struct genl_info *info)
{
	struct ioam6_pernet_data *nsdata;
	struct ioam6_namespace *ns;
	struct ioam6_schema *sc;
	int err;
	u32 id;

	if (!info->attrs[IOAM6_ATTR_SC_ID])
		return -EINVAL;

	id = nla_get_u32(info->attrs[IOAM6_ATTR_SC_ID]);
	nsdata = ioam6_pernet(genl_info_net(info));

	mutex_lock(&nsdata->lock);

	sc = rhashtable_lookup_fast(&nsdata->schemas, &id, rht_sc_params);
	if (!sc) {
		err = -ENOENT;
		goto out_unlock;
	}

	ns = rcu_dereference_protected(sc->ns, lockdep_is_held(&nsdata->lock));

	err = rhashtable_remove_fast(&nsdata->schemas, &sc->head,
				     rht_sc_params);
	if (err)
		goto out_unlock;

	if (ns)
		rcu_assign_pointer(ns->schema, NULL);

	ioam6_sc_release(sc);

out_unlock:
	mutex_unlock(&nsdata->lock);
	return err;
}

static int __ioam6_genl_dumpsc_element(struct ioam6_schema *sc,
				       u32 portid, u32 seq, u32 flags,
				       struct sk_buff *skb, u8 cmd)
{
	struct ioam6_namespace *ns;
	void *hdr;

	hdr = genlmsg_put(skb, portid, seq, &ioam6_genl_family, flags, cmd);
	if (!hdr)
		return -ENOMEM;

	if (nla_put_u32(skb, IOAM6_ATTR_SC_ID, sc->id) ||
	    nla_put(skb, IOAM6_ATTR_SC_DATA, sc->len, sc->data))
		goto nla_put_failure;

	rcu_read_lock();

	ns = rcu_dereference(sc->ns);
	if (ns && nla_put_u16(skb, IOAM6_ATTR_NS_ID, be16_to_cpu(ns->id))) {
		rcu_read_unlock();
		goto nla_put_failure;
	}

	rcu_read_unlock();

	genlmsg_end(skb, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(skb, hdr);
	return -EMSGSIZE;
}

static int ioam6_genl_dumpsc_start(struct netlink_callback *cb)
{
	struct ioam6_pernet_data *nsdata = ioam6_pernet(sock_net(cb->skb->sk));
	struct rhashtable_iter *iter = (struct rhashtable_iter *)cb->args[0];

	if (!iter) {
		iter = kmalloc(sizeof(*iter), GFP_KERNEL);
		if (!iter)
			return -ENOMEM;

		cb->args[0] = (long)iter;
	}

	rhashtable_walk_enter(&nsdata->schemas, iter);

	return 0;
}

static int ioam6_genl_dumpsc_done(struct netlink_callback *cb)
{
	struct rhashtable_iter *iter = (struct rhashtable_iter *)cb->args[0];

	rhashtable_walk_exit(iter);
	kfree(iter);

	return 0;
}

static int ioam6_genl_dumpsc(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct rhashtable_iter *iter;
	struct ioam6_schema *sc;
	int err;

	iter = (struct rhashtable_iter *)cb->args[0];
	rhashtable_walk_start(iter);

	for (;;) {
		sc = rhashtable_walk_next(iter);

		if (IS_ERR(sc)) {
			if (PTR_ERR(sc) == -EAGAIN)
				continue;
			err = PTR_ERR(sc);
			goto done;
		} else if (!sc) {
			break;
		}

		err = __ioam6_genl_dumpsc_element(sc,
						  NETLINK_CB(cb->skb).portid,
						  cb->nlh->nlmsg_seq,
						  NLM_F_MULTI,
						  skb,
						  IOAM6_CMD_DUMP_SCHEMAS);
		if (err)
			goto done;
	}

	err = skb->len;

done:
	rhashtable_walk_stop(iter);
	return err;
}

static int ioam6_genl_ns_set_schema(struct sk_buff *skb, struct genl_info *info)
{
	struct ioam6_namespace *ns, *ns_ref;
	struct ioam6_schema *sc, *sc_ref;
	struct ioam6_pernet_data *nsdata;
	__be16 ns_id;
	u32 sc_id;
	int err;

	if (!info->attrs[IOAM6_ATTR_NS_ID] ||
	    (!info->attrs[IOAM6_ATTR_SC_ID] &&
	     !info->attrs[IOAM6_ATTR_SC_NONE]))
		return -EINVAL;

	ns_id = cpu_to_be16(nla_get_u16(info->attrs[IOAM6_ATTR_NS_ID]));
	nsdata = ioam6_pernet(genl_info_net(info));

	mutex_lock(&nsdata->lock);

	ns = rhashtable_lookup_fast(&nsdata->namespaces, &ns_id, rht_ns_params);
	if (!ns) {
		err = -ENOENT;
		goto out_unlock;
	}

	if (info->attrs[IOAM6_ATTR_SC_NONE]) {
		sc = NULL;
	} else {
		sc_id = nla_get_u32(info->attrs[IOAM6_ATTR_SC_ID]);
		sc = rhashtable_lookup_fast(&nsdata->schemas, &sc_id,
					    rht_sc_params);
		if (!sc) {
			err = -ENOENT;
			goto out_unlock;
		}
	}

	sc_ref = rcu_dereference_protected(ns->schema,
					   lockdep_is_held(&nsdata->lock));
	if (sc_ref)
		rcu_assign_pointer(sc_ref->ns, NULL);
	rcu_assign_pointer(ns->schema, sc);

	if (sc) {
		ns_ref = rcu_dereference_protected(sc->ns,
						   lockdep_is_held(&nsdata->lock));
		if (ns_ref)
			rcu_assign_pointer(ns_ref->schema, NULL);
		rcu_assign_pointer(sc->ns, ns);
	}

	err = 0;

out_unlock:
	mutex_unlock(&nsdata->lock);
	return err;
}

static const struct genl_ops ioam6_genl_ops[] = {
	{
		.cmd	= IOAM6_CMD_ADD_NAMESPACE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit	= ioam6_genl_addns,
		.flags	= GENL_ADMIN_PERM,
		.policy	= ioam6_genl_policy_addns,
		.maxattr = ARRAY_SIZE(ioam6_genl_policy_addns) - 1,
	},
	{
		.cmd	= IOAM6_CMD_DEL_NAMESPACE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit	= ioam6_genl_delns,
		.flags	= GENL_ADMIN_PERM,
		.policy	= ioam6_genl_policy_delns,
		.maxattr = ARRAY_SIZE(ioam6_genl_policy_delns) - 1,
	},
	{
		.cmd	= IOAM6_CMD_DUMP_NAMESPACES,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.start	= ioam6_genl_dumpns_start,
		.dumpit	= ioam6_genl_dumpns,
		.done	= ioam6_genl_dumpns_done,
		.flags	= GENL_ADMIN_PERM,
	},
	{
		.cmd	= IOAM6_CMD_ADD_SCHEMA,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit	= ioam6_genl_addsc,
		.flags	= GENL_ADMIN_PERM,
		.policy	= ioam6_genl_policy_addsc,
		.maxattr = ARRAY_SIZE(ioam6_genl_policy_addsc) - 1,
	},
	{
		.cmd	= IOAM6_CMD_DEL_SCHEMA,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit	= ioam6_genl_delsc,
		.flags	= GENL_ADMIN_PERM,
		.policy	= ioam6_genl_policy_delsc,
		.maxattr = ARRAY_SIZE(ioam6_genl_policy_delsc) - 1,
	},
	{
		.cmd	= IOAM6_CMD_DUMP_SCHEMAS,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.start	= ioam6_genl_dumpsc_start,
		.dumpit	= ioam6_genl_dumpsc,
		.done	= ioam6_genl_dumpsc_done,
		.flags	= GENL_ADMIN_PERM,
	},
	{
		.cmd	= IOAM6_CMD_NS_SET_SCHEMA,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit	= ioam6_genl_ns_set_schema,
		.flags	= GENL_ADMIN_PERM,
		.policy	= ioam6_genl_policy_ns_sc,
		.maxattr = ARRAY_SIZE(ioam6_genl_policy_ns_sc) - 1,
	},
};

static struct genl_family ioam6_genl_family __ro_after_init = {
	.name		= IOAM6_GENL_NAME,
	.version	= IOAM6_GENL_VERSION,
	.netnsok	= true,
	.parallel_ops	= true,
	.ops		= ioam6_genl_ops,
	.n_ops		= ARRAY_SIZE(ioam6_genl_ops),
	.module		= THIS_MODULE,
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
				    u8 sclen, bool is_input)
{
	struct timespec64 ts;
	ktime_t tstamp;
	u64 raw64;
	u32 raw32;
	u16 raw16;
	u8 *data;
	u8 byte;

	data = trace->data + trace->remlen * 4 - trace->nodelen * 4 - sclen * 4;

	/* hop_lim and node_id */
	if (trace->type.bit0) {
		byte = ipv6_hdr(skb)->hop_limit;
		if (is_input)
			byte--;

		raw32 = dev_net(skb_dst(skb)->dev)->ipv6.sysctl.ioam6_id;

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
		if (!skb->dev) {
			*(__be32 *)data = cpu_to_be32(IOAM6_U32_UNAVAILABLE);
		} else {
			tstamp = skb_tstamp_cond(skb, true);
			ts = ktime_to_timespec64(tstamp);

			*(__be32 *)data = cpu_to_be32((u32)ts.tv_sec);
		}
		data += sizeof(__be32);
	}

	/* timestamp subseconds */
	if (trace->type.bit3) {
		if (!skb->dev) {
			*(__be32 *)data = cpu_to_be32(IOAM6_U32_UNAVAILABLE);
		} else {
			if (!trace->type.bit2) {
				tstamp = skb_tstamp_cond(skb, true);
				ts = ktime_to_timespec64(tstamp);
			}

			*(__be32 *)data = cpu_to_be32((u32)(ts.tv_nsec / NSEC_PER_USEC));
		}
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
		struct netdev_queue *queue;
		struct Qdisc *qdisc;
		__u32 qlen, backlog;

		if (skb_dst(skb)->dev->flags & IFF_LOOPBACK) {
			*(__be32 *)data = cpu_to_be32(IOAM6_U32_UNAVAILABLE);
		} else {
			queue = skb_get_tx_queue(skb_dst(skb)->dev, skb);
			qdisc = rcu_dereference(queue->qdisc);
			qdisc_qstats_qlen_backlog(qdisc, &qlen, &backlog);

			*(__be32 *)data = cpu_to_be32(backlog);
		}
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
		if (is_input)
			byte--;

		raw64 = dev_net(skb_dst(skb)->dev)->ipv6.sysctl.ioam6_id_wide;

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

	/* bit12 undefined: filled with empty value */
	if (trace->type.bit12) {
		*(__be32 *)data = cpu_to_be32(IOAM6_U32_UNAVAILABLE);
		data += sizeof(__be32);
	}

	/* bit13 undefined: filled with empty value */
	if (trace->type.bit13) {
		*(__be32 *)data = cpu_to_be32(IOAM6_U32_UNAVAILABLE);
		data += sizeof(__be32);
	}

	/* bit14 undefined: filled with empty value */
	if (trace->type.bit14) {
		*(__be32 *)data = cpu_to_be32(IOAM6_U32_UNAVAILABLE);
		data += sizeof(__be32);
	}

	/* bit15 undefined: filled with empty value */
	if (trace->type.bit15) {
		*(__be32 *)data = cpu_to_be32(IOAM6_U32_UNAVAILABLE);
		data += sizeof(__be32);
	}

	/* bit16 undefined: filled with empty value */
	if (trace->type.bit16) {
		*(__be32 *)data = cpu_to_be32(IOAM6_U32_UNAVAILABLE);
		data += sizeof(__be32);
	}

	/* bit17 undefined: filled with empty value */
	if (trace->type.bit17) {
		*(__be32 *)data = cpu_to_be32(IOAM6_U32_UNAVAILABLE);
		data += sizeof(__be32);
	}

	/* bit18 undefined: filled with empty value */
	if (trace->type.bit18) {
		*(__be32 *)data = cpu_to_be32(IOAM6_U32_UNAVAILABLE);
		data += sizeof(__be32);
	}

	/* bit19 undefined: filled with empty value */
	if (trace->type.bit19) {
		*(__be32 *)data = cpu_to_be32(IOAM6_U32_UNAVAILABLE);
		data += sizeof(__be32);
	}

	/* bit20 undefined: filled with empty value */
	if (trace->type.bit20) {
		*(__be32 *)data = cpu_to_be32(IOAM6_U32_UNAVAILABLE);
		data += sizeof(__be32);
	}

	/* bit21 undefined: filled with empty value */
	if (trace->type.bit21) {
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
			   struct ioam6_trace_hdr *trace,
			   bool is_input)
{
	struct ioam6_schema *sc;
	u8 sclen = 0;

	/* Skip if Overflow flag is set
	 */
	if (trace->overflow)
		return;

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

	__ioam6_fill_trace_data(skb, ns, trace, sc, sclen, is_input);
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
		goto out;

	err = genl_register_family(&ioam6_genl_family);
	if (err)
		goto out_unregister_pernet_subsys;

#ifdef CONFIG_IPV6_IOAM6_LWTUNNEL
	err = ioam6_iptunnel_init();
	if (err)
		goto out_unregister_genl;
#endif

	pr_info("In-situ OAM (IOAM) with IPv6\n");

out:
	return err;
#ifdef CONFIG_IPV6_IOAM6_LWTUNNEL
out_unregister_genl:
	genl_unregister_family(&ioam6_genl_family);
#endif
out_unregister_pernet_subsys:
	unregister_pernet_subsys(&ioam6_net_ops);
	goto out;
}

void ioam6_exit(void)
{
#ifdef CONFIG_IPV6_IOAM6_LWTUNNEL
	ioam6_iptunnel_exit();
#endif
	genl_unregister_family(&ioam6_genl_family);
	unregister_pernet_subsys(&ioam6_net_ops);
}
