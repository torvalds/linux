// SPDX-License-Identifier: GPL-2.0
#include <linux/jhash.h>
#include <linux/netfilter.h>
#include <linux/rcupdate.h>
#include <linux/rhashtable.h>
#include <linux/vmalloc.h>
#include <net/genetlink.h>
#include <net/ila.h>
#include <net/netns/generic.h>
#include <uapi/linux/genetlink.h>
#include "ila.h"

struct ila_xlat_params {
	struct ila_params ip;
	int ifindex;
};

struct ila_map {
	struct ila_xlat_params xp;
	struct rhash_head node;
	struct ila_map __rcu *next;
	struct rcu_head rcu;
};

#define MAX_LOCKS 1024
#define	LOCKS_PER_CPU 10

static int alloc_ila_locks(struct ila_net *ilan)
{
	return alloc_bucket_spinlocks(&ilan->xlat.locks, &ilan->xlat.locks_mask,
				      MAX_LOCKS, LOCKS_PER_CPU,
				      GFP_KERNEL);
}

static u32 hashrnd __read_mostly;
static __always_inline void __ila_hash_secret_init(void)
{
	net_get_random_once(&hashrnd, sizeof(hashrnd));
}

static inline u32 ila_locator_hash(struct ila_locator loc)
{
	u32 *v = (u32 *)loc.v32;

	__ila_hash_secret_init();
	return jhash_2words(v[0], v[1], hashrnd);
}

static inline spinlock_t *ila_get_lock(struct ila_net *ilan,
				       struct ila_locator loc)
{
	return &ilan->xlat.locks[ila_locator_hash(loc) & ilan->xlat.locks_mask];
}

static inline int ila_cmp_wildcards(struct ila_map *ila,
				    struct ila_addr *iaddr, int ifindex)
{
	return (ila->xp.ifindex && ila->xp.ifindex != ifindex);
}

static inline int ila_cmp_params(struct ila_map *ila,
				 struct ila_xlat_params *xp)
{
	return (ila->xp.ifindex != xp->ifindex);
}

static int ila_cmpfn(struct rhashtable_compare_arg *arg,
		     const void *obj)
{
	const struct ila_map *ila = obj;

	return (ila->xp.ip.locator_match.v64 != *(__be64 *)arg->key);
}

static inline int ila_order(struct ila_map *ila)
{
	int score = 0;

	if (ila->xp.ifindex)
		score += 1 << 1;

	return score;
}

static const struct rhashtable_params rht_params = {
	.nelem_hint = 1024,
	.head_offset = offsetof(struct ila_map, node),
	.key_offset = offsetof(struct ila_map, xp.ip.locator_match),
	.key_len = sizeof(u64), /* identifier */
	.max_size = 1048576,
	.min_size = 256,
	.automatic_shrinking = true,
	.obj_cmpfn = ila_cmpfn,
};

static int parse_nl_config(struct genl_info *info,
			   struct ila_xlat_params *xp)
{
	memset(xp, 0, sizeof(*xp));

	if (info->attrs[ILA_ATTR_LOCATOR])
		xp->ip.locator.v64 = (__force __be64)nla_get_u64(
			info->attrs[ILA_ATTR_LOCATOR]);

	if (info->attrs[ILA_ATTR_LOCATOR_MATCH])
		xp->ip.locator_match.v64 = (__force __be64)nla_get_u64(
			info->attrs[ILA_ATTR_LOCATOR_MATCH]);

	if (info->attrs[ILA_ATTR_CSUM_MODE])
		xp->ip.csum_mode = nla_get_u8(info->attrs[ILA_ATTR_CSUM_MODE]);
	else
		xp->ip.csum_mode = ILA_CSUM_NO_ACTION;

	if (info->attrs[ILA_ATTR_IDENT_TYPE])
		xp->ip.ident_type = nla_get_u8(
				info->attrs[ILA_ATTR_IDENT_TYPE]);
	else
		xp->ip.ident_type = ILA_ATYPE_USE_FORMAT;

	if (info->attrs[ILA_ATTR_IFINDEX])
		xp->ifindex = nla_get_s32(info->attrs[ILA_ATTR_IFINDEX]);

	return 0;
}

/* Must be called with rcu readlock */
static inline struct ila_map *ila_lookup_wildcards(struct ila_addr *iaddr,
						   int ifindex,
						   struct ila_net *ilan)
{
	struct ila_map *ila;

	ila = rhashtable_lookup_fast(&ilan->xlat.rhash_table, &iaddr->loc,
				     rht_params);
	while (ila) {
		if (!ila_cmp_wildcards(ila, iaddr, ifindex))
			return ila;
		ila = rcu_access_pointer(ila->next);
	}

	return NULL;
}

/* Must be called with rcu readlock */
static inline struct ila_map *ila_lookup_by_params(struct ila_xlat_params *xp,
						   struct ila_net *ilan)
{
	struct ila_map *ila;

	ila = rhashtable_lookup_fast(&ilan->xlat.rhash_table,
				     &xp->ip.locator_match,
				     rht_params);
	while (ila) {
		if (!ila_cmp_params(ila, xp))
			return ila;
		ila = rcu_access_pointer(ila->next);
	}

	return NULL;
}

static inline void ila_release(struct ila_map *ila)
{
	kfree_rcu(ila, rcu);
}

static void ila_free_node(struct ila_map *ila)
{
	struct ila_map *next;

	/* Assume rcu_readlock held */
	while (ila) {
		next = rcu_access_pointer(ila->next);
		ila_release(ila);
		ila = next;
	}
}

static void ila_free_cb(void *ptr, void *arg)
{
	ila_free_node((struct ila_map *)ptr);
}

static int ila_xlat_addr(struct sk_buff *skb, bool sir2ila);

static unsigned int
ila_nf_input(void *priv,
	     struct sk_buff *skb,
	     const struct nf_hook_state *state)
{
	ila_xlat_addr(skb, false);
	return NF_ACCEPT;
}

static const struct nf_hook_ops ila_nf_hook_ops[] = {
	{
		.hook = ila_nf_input,
		.pf = NFPROTO_IPV6,
		.hooknum = NF_INET_PRE_ROUTING,
		.priority = -1,
	},
};

static int ila_add_mapping(struct net *net, struct ila_xlat_params *xp)
{
	struct ila_net *ilan = net_generic(net, ila_net_id);
	struct ila_map *ila, *head;
	spinlock_t *lock = ila_get_lock(ilan, xp->ip.locator_match);
	int err = 0, order;

	if (!ilan->xlat.hooks_registered) {
		/* We defer registering net hooks in the namespace until the
		 * first mapping is added.
		 */
		err = nf_register_net_hooks(net, ila_nf_hook_ops,
					    ARRAY_SIZE(ila_nf_hook_ops));
		if (err)
			return err;

		ilan->xlat.hooks_registered = true;
	}

	ila = kzalloc(sizeof(*ila), GFP_KERNEL);
	if (!ila)
		return -ENOMEM;

	ila_init_saved_csum(&xp->ip);

	ila->xp = *xp;

	order = ila_order(ila);

	spin_lock(lock);

	head = rhashtable_lookup_fast(&ilan->xlat.rhash_table,
				      &xp->ip.locator_match,
				      rht_params);
	if (!head) {
		/* New entry for the rhash_table */
		err = rhashtable_lookup_insert_fast(&ilan->xlat.rhash_table,
						    &ila->node, rht_params);
	} else {
		struct ila_map *tila = head, *prev = NULL;

		do {
			if (!ila_cmp_params(tila, xp)) {
				err = -EEXIST;
				goto out;
			}

			if (order > ila_order(tila))
				break;

			prev = tila;
			tila = rcu_dereference_protected(tila->next,
				lockdep_is_held(lock));
		} while (tila);

		if (prev) {
			/* Insert in sub list of head */
			RCU_INIT_POINTER(ila->next, tila);
			rcu_assign_pointer(prev->next, ila);
		} else {
			/* Make this ila new head */
			RCU_INIT_POINTER(ila->next, head);
			err = rhashtable_replace_fast(&ilan->xlat.rhash_table,
						      &head->node,
						      &ila->node, rht_params);
			if (err)
				goto out;
		}
	}

out:
	spin_unlock(lock);

	if (err)
		kfree(ila);

	return err;
}

static int ila_del_mapping(struct net *net, struct ila_xlat_params *xp)
{
	struct ila_net *ilan = net_generic(net, ila_net_id);
	struct ila_map *ila, *head, *prev;
	spinlock_t *lock = ila_get_lock(ilan, xp->ip.locator_match);
	int err = -ENOENT;

	spin_lock(lock);

	head = rhashtable_lookup_fast(&ilan->xlat.rhash_table,
				      &xp->ip.locator_match, rht_params);
	ila = head;

	prev = NULL;

	while (ila) {
		if (ila_cmp_params(ila, xp)) {
			prev = ila;
			ila = rcu_dereference_protected(ila->next,
							lockdep_is_held(lock));
			continue;
		}

		err = 0;

		if (prev) {
			/* Not head, just delete from list */
			rcu_assign_pointer(prev->next, ila->next);
		} else {
			/* It is the head. If there is something in the
			 * sublist we need to make a new head.
			 */
			head = rcu_dereference_protected(ila->next,
							 lockdep_is_held(lock));
			if (head) {
				/* Put first entry in the sublist into the
				 * table
				 */
				err = rhashtable_replace_fast(
					&ilan->xlat.rhash_table, &ila->node,
					&head->node, rht_params);
				if (err)
					goto out;
			} else {
				/* Entry no longer used */
				err = rhashtable_remove_fast(
						&ilan->xlat.rhash_table,
						&ila->node, rht_params);
			}
		}

		ila_release(ila);

		break;
	}

out:
	spin_unlock(lock);

	return err;
}

int ila_xlat_nl_cmd_add_mapping(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = genl_info_net(info);
	struct ila_xlat_params p;
	int err;

	err = parse_nl_config(info, &p);
	if (err)
		return err;

	return ila_add_mapping(net, &p);
}

int ila_xlat_nl_cmd_del_mapping(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = genl_info_net(info);
	struct ila_xlat_params xp;
	int err;

	err = parse_nl_config(info, &xp);
	if (err)
		return err;

	ila_del_mapping(net, &xp);

	return 0;
}

static inline spinlock_t *lock_from_ila_map(struct ila_net *ilan,
					    struct ila_map *ila)
{
	return ila_get_lock(ilan, ila->xp.ip.locator_match);
}

int ila_xlat_nl_cmd_flush(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = genl_info_net(info);
	struct ila_net *ilan = net_generic(net, ila_net_id);
	struct rhashtable_iter iter;
	struct ila_map *ila;
	spinlock_t *lock;
	int ret = 0;

	rhashtable_walk_enter(&ilan->xlat.rhash_table, &iter);
	rhashtable_walk_start(&iter);

	for (;;) {
		ila = rhashtable_walk_next(&iter);

		if (IS_ERR(ila)) {
			if (PTR_ERR(ila) == -EAGAIN)
				continue;
			ret = PTR_ERR(ila);
			goto done;
		} else if (!ila) {
			break;
		}

		lock = lock_from_ila_map(ilan, ila);

		spin_lock(lock);

		ret = rhashtable_remove_fast(&ilan->xlat.rhash_table,
					     &ila->node, rht_params);
		if (!ret)
			ila_free_node(ila);

		spin_unlock(lock);

		if (ret)
			break;
	}

done:
	rhashtable_walk_stop(&iter);
	rhashtable_walk_exit(&iter);
	return ret;
}

static int ila_fill_info(struct ila_map *ila, struct sk_buff *msg)
{
	if (nla_put_u64_64bit(msg, ILA_ATTR_LOCATOR,
			      (__force u64)ila->xp.ip.locator.v64,
			      ILA_ATTR_PAD) ||
	    nla_put_u64_64bit(msg, ILA_ATTR_LOCATOR_MATCH,
			      (__force u64)ila->xp.ip.locator_match.v64,
			      ILA_ATTR_PAD) ||
	    nla_put_s32(msg, ILA_ATTR_IFINDEX, ila->xp.ifindex) ||
	    nla_put_u8(msg, ILA_ATTR_CSUM_MODE, ila->xp.ip.csum_mode) ||
	    nla_put_u8(msg, ILA_ATTR_IDENT_TYPE, ila->xp.ip.ident_type))
		return -1;

	return 0;
}

static int ila_dump_info(struct ila_map *ila,
			 u32 portid, u32 seq, u32 flags,
			 struct sk_buff *skb, u8 cmd)
{
	void *hdr;

	hdr = genlmsg_put(skb, portid, seq, &ila_nl_family, flags, cmd);
	if (!hdr)
		return -ENOMEM;

	if (ila_fill_info(ila, skb) < 0)
		goto nla_put_failure;

	genlmsg_end(skb, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(skb, hdr);
	return -EMSGSIZE;
}

int ila_xlat_nl_cmd_get_mapping(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = genl_info_net(info);
	struct ila_net *ilan = net_generic(net, ila_net_id);
	struct sk_buff *msg;
	struct ila_xlat_params xp;
	struct ila_map *ila;
	int ret;

	ret = parse_nl_config(info, &xp);
	if (ret)
		return ret;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	rcu_read_lock();

	ila = ila_lookup_by_params(&xp, ilan);
	if (ila) {
		ret = ila_dump_info(ila,
				    info->snd_portid,
				    info->snd_seq, 0, msg,
				    info->genlhdr->cmd);
	}

	rcu_read_unlock();

	if (ret < 0)
		goto out_free;

	return genlmsg_reply(msg, info);

out_free:
	nlmsg_free(msg);
	return ret;
}

struct ila_dump_iter {
	struct rhashtable_iter rhiter;
	int skip;
};

int ila_xlat_nl_dump_start(struct netlink_callback *cb)
{
	struct net *net = sock_net(cb->skb->sk);
	struct ila_net *ilan = net_generic(net, ila_net_id);
	struct ila_dump_iter *iter;

	iter = kmalloc(sizeof(*iter), GFP_KERNEL);
	if (!iter)
		return -ENOMEM;

	rhashtable_walk_enter(&ilan->xlat.rhash_table, &iter->rhiter);

	iter->skip = 0;
	cb->args[0] = (long)iter;

	return 0;
}

int ila_xlat_nl_dump_done(struct netlink_callback *cb)
{
	struct ila_dump_iter *iter = (struct ila_dump_iter *)cb->args[0];

	rhashtable_walk_exit(&iter->rhiter);

	kfree(iter);

	return 0;
}

int ila_xlat_nl_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct ila_dump_iter *iter = (struct ila_dump_iter *)cb->args[0];
	struct rhashtable_iter *rhiter = &iter->rhiter;
	int skip = iter->skip;
	struct ila_map *ila;
	int ret;

	rhashtable_walk_start(rhiter);

	/* Get first entry */
	ila = rhashtable_walk_peek(rhiter);

	if (ila && !IS_ERR(ila) && skip) {
		/* Skip over visited entries */

		while (ila && skip) {
			/* Skip over any ila entries in this list that we
			 * have already dumped.
			 */
			ila = rcu_access_pointer(ila->next);
			skip--;
		}
	}

	skip = 0;

	for (;;) {
		if (IS_ERR(ila)) {
			ret = PTR_ERR(ila);
			if (ret == -EAGAIN) {
				/* Table has changed and iter has reset. Return
				 * -EAGAIN to the application even if we have
				 * written data to the skb. The application
				 * needs to deal with this.
				 */

				goto out_ret;
			} else {
				break;
			}
		} else if (!ila) {
			ret = 0;
			break;
		}

		while (ila) {
			ret =  ila_dump_info(ila, NETLINK_CB(cb->skb).portid,
					     cb->nlh->nlmsg_seq, NLM_F_MULTI,
					     skb, ILA_CMD_GET);
			if (ret)
				goto out;

			skip++;
			ila = rcu_access_pointer(ila->next);
		}

		skip = 0;
		ila = rhashtable_walk_next(rhiter);
	}

out:
	iter->skip = skip;
	ret = (skb->len ? : ret);

out_ret:
	rhashtable_walk_stop(rhiter);
	return ret;
}

int ila_xlat_init_net(struct net *net)
{
	struct ila_net *ilan = net_generic(net, ila_net_id);
	int err;

	err = alloc_ila_locks(ilan);
	if (err)
		return err;

	err = rhashtable_init(&ilan->xlat.rhash_table, &rht_params);
	if (err) {
		free_bucket_spinlocks(ilan->xlat.locks);
		return err;
	}

	return 0;
}

void ila_xlat_exit_net(struct net *net)
{
	struct ila_net *ilan = net_generic(net, ila_net_id);

	rhashtable_free_and_destroy(&ilan->xlat.rhash_table, ila_free_cb, NULL);

	free_bucket_spinlocks(ilan->xlat.locks);

	if (ilan->xlat.hooks_registered)
		nf_unregister_net_hooks(net, ila_nf_hook_ops,
					ARRAY_SIZE(ila_nf_hook_ops));
}

static int ila_xlat_addr(struct sk_buff *skb, bool sir2ila)
{
	struct ila_map *ila;
	struct ipv6hdr *ip6h = ipv6_hdr(skb);
	struct net *net = dev_net(skb->dev);
	struct ila_net *ilan = net_generic(net, ila_net_id);
	struct ila_addr *iaddr = ila_a2i(&ip6h->daddr);

	/* Assumes skb contains a valid IPv6 header that is pulled */

	/* No check here that ILA type in the mapping matches what is in the
	 * address. We assume that whatever sender gaves us can be translated.
	 * The checksum mode however is relevant.
	 */

	rcu_read_lock();

	ila = ila_lookup_wildcards(iaddr, skb->dev->ifindex, ilan);
	if (ila)
		ila_update_ipv6_locator(skb, &ila->xp.ip, sir2ila);

	rcu_read_unlock();

	return 0;
}
