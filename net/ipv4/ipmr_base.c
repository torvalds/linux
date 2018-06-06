/* Linux multicast routing support
 * Common logic shared by IPv4 [ipmr] and IPv6 [ip6mr] implementation
 */

#include <linux/mroute_base.h>

/* Sets everything common except 'dev', since that is done under locking */
void vif_device_init(struct vif_device *v,
		     struct net_device *dev,
		     unsigned long rate_limit,
		     unsigned char threshold,
		     unsigned short flags,
		     unsigned short get_iflink_mask)
{
	v->dev = NULL;
	v->bytes_in = 0;
	v->bytes_out = 0;
	v->pkt_in = 0;
	v->pkt_out = 0;
	v->rate_limit = rate_limit;
	v->flags = flags;
	v->threshold = threshold;
	if (v->flags & get_iflink_mask)
		v->link = dev_get_iflink(dev);
	else
		v->link = dev->ifindex;
}
EXPORT_SYMBOL(vif_device_init);

struct mr_table *
mr_table_alloc(struct net *net, u32 id,
	       struct mr_table_ops *ops,
	       void (*expire_func)(struct timer_list *t),
	       void (*table_set)(struct mr_table *mrt,
				 struct net *net))
{
	struct mr_table *mrt;

	mrt = kzalloc(sizeof(*mrt), GFP_KERNEL);
	if (!mrt)
		return NULL;
	mrt->id = id;
	write_pnet(&mrt->net, net);

	mrt->ops = *ops;
	if (rhltable_init(&mrt->mfc_hash, mrt->ops.rht_params)) {
		kfree(mrt);
		return NULL;
	}
	INIT_LIST_HEAD(&mrt->mfc_cache_list);
	INIT_LIST_HEAD(&mrt->mfc_unres_queue);

	timer_setup(&mrt->ipmr_expire_timer, expire_func, 0);

	mrt->mroute_reg_vif_num = -1;
	table_set(mrt, net);
	return mrt;
}
EXPORT_SYMBOL(mr_table_alloc);

void *mr_mfc_find_parent(struct mr_table *mrt, void *hasharg, int parent)
{
	struct rhlist_head *tmp, *list;
	struct mr_mfc *c;

	list = rhltable_lookup(&mrt->mfc_hash, hasharg, *mrt->ops.rht_params);
	rhl_for_each_entry_rcu(c, tmp, list, mnode)
		if (parent == -1 || parent == c->mfc_parent)
			return c;

	return NULL;
}
EXPORT_SYMBOL(mr_mfc_find_parent);

void *mr_mfc_find_any_parent(struct mr_table *mrt, int vifi)
{
	struct rhlist_head *tmp, *list;
	struct mr_mfc *c;

	list = rhltable_lookup(&mrt->mfc_hash, mrt->ops.cmparg_any,
			       *mrt->ops.rht_params);
	rhl_for_each_entry_rcu(c, tmp, list, mnode)
		if (c->mfc_un.res.ttls[vifi] < 255)
			return c;

	return NULL;
}
EXPORT_SYMBOL(mr_mfc_find_any_parent);

void *mr_mfc_find_any(struct mr_table *mrt, int vifi, void *hasharg)
{
	struct rhlist_head *tmp, *list;
	struct mr_mfc *c, *proxy;

	list = rhltable_lookup(&mrt->mfc_hash, hasharg, *mrt->ops.rht_params);
	rhl_for_each_entry_rcu(c, tmp, list, mnode) {
		if (c->mfc_un.res.ttls[vifi] < 255)
			return c;

		/* It's ok if the vifi is part of the static tree */
		proxy = mr_mfc_find_any_parent(mrt, c->mfc_parent);
		if (proxy && proxy->mfc_un.res.ttls[vifi] < 255)
			return c;
	}

	return mr_mfc_find_any_parent(mrt, vifi);
}
EXPORT_SYMBOL(mr_mfc_find_any);

#ifdef CONFIG_PROC_FS
void *mr_vif_seq_idx(struct net *net, struct mr_vif_iter *iter, loff_t pos)
{
	struct mr_table *mrt = iter->mrt;

	for (iter->ct = 0; iter->ct < mrt->maxvif; ++iter->ct) {
		if (!VIF_EXISTS(mrt, iter->ct))
			continue;
		if (pos-- == 0)
			return &mrt->vif_table[iter->ct];
	}
	return NULL;
}
EXPORT_SYMBOL(mr_vif_seq_idx);

void *mr_vif_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct mr_vif_iter *iter = seq->private;
	struct net *net = seq_file_net(seq);
	struct mr_table *mrt = iter->mrt;

	++*pos;
	if (v == SEQ_START_TOKEN)
		return mr_vif_seq_idx(net, iter, 0);

	while (++iter->ct < mrt->maxvif) {
		if (!VIF_EXISTS(mrt, iter->ct))
			continue;
		return &mrt->vif_table[iter->ct];
	}
	return NULL;
}
EXPORT_SYMBOL(mr_vif_seq_next);

void *mr_mfc_seq_idx(struct net *net,
		     struct mr_mfc_iter *it, loff_t pos)
{
	struct mr_table *mrt = it->mrt;
	struct mr_mfc *mfc;

	rcu_read_lock();
	it->cache = &mrt->mfc_cache_list;
	list_for_each_entry_rcu(mfc, &mrt->mfc_cache_list, list)
		if (pos-- == 0)
			return mfc;
	rcu_read_unlock();

	spin_lock_bh(it->lock);
	it->cache = &mrt->mfc_unres_queue;
	list_for_each_entry(mfc, it->cache, list)
		if (pos-- == 0)
			return mfc;
	spin_unlock_bh(it->lock);

	it->cache = NULL;
	return NULL;
}
EXPORT_SYMBOL(mr_mfc_seq_idx);

void *mr_mfc_seq_next(struct seq_file *seq, void *v,
		      loff_t *pos)
{
	struct mr_mfc_iter *it = seq->private;
	struct net *net = seq_file_net(seq);
	struct mr_table *mrt = it->mrt;
	struct mr_mfc *c = v;

	++*pos;

	if (v == SEQ_START_TOKEN)
		return mr_mfc_seq_idx(net, seq->private, 0);

	if (c->list.next != it->cache)
		return list_entry(c->list.next, struct mr_mfc, list);

	if (it->cache == &mrt->mfc_unres_queue)
		goto end_of_list;

	/* exhausted cache_array, show unresolved */
	rcu_read_unlock();
	it->cache = &mrt->mfc_unres_queue;

	spin_lock_bh(it->lock);
	if (!list_empty(it->cache))
		return list_first_entry(it->cache, struct mr_mfc, list);

end_of_list:
	spin_unlock_bh(it->lock);
	it->cache = NULL;

	return NULL;
}
EXPORT_SYMBOL(mr_mfc_seq_next);
#endif

int mr_fill_mroute(struct mr_table *mrt, struct sk_buff *skb,
		   struct mr_mfc *c, struct rtmsg *rtm)
{
	struct rta_mfc_stats mfcs;
	struct nlattr *mp_attr;
	struct rtnexthop *nhp;
	unsigned long lastuse;
	int ct;

	/* If cache is unresolved, don't try to parse IIF and OIF */
	if (c->mfc_parent >= MAXVIFS) {
		rtm->rtm_flags |= RTNH_F_UNRESOLVED;
		return -ENOENT;
	}

	if (VIF_EXISTS(mrt, c->mfc_parent) &&
	    nla_put_u32(skb, RTA_IIF,
			mrt->vif_table[c->mfc_parent].dev->ifindex) < 0)
		return -EMSGSIZE;

	if (c->mfc_flags & MFC_OFFLOAD)
		rtm->rtm_flags |= RTNH_F_OFFLOAD;

	mp_attr = nla_nest_start(skb, RTA_MULTIPATH);
	if (!mp_attr)
		return -EMSGSIZE;

	for (ct = c->mfc_un.res.minvif; ct < c->mfc_un.res.maxvif; ct++) {
		if (VIF_EXISTS(mrt, ct) && c->mfc_un.res.ttls[ct] < 255) {
			struct vif_device *vif;

			nhp = nla_reserve_nohdr(skb, sizeof(*nhp));
			if (!nhp) {
				nla_nest_cancel(skb, mp_attr);
				return -EMSGSIZE;
			}

			nhp->rtnh_flags = 0;
			nhp->rtnh_hops = c->mfc_un.res.ttls[ct];
			vif = &mrt->vif_table[ct];
			nhp->rtnh_ifindex = vif->dev->ifindex;
			nhp->rtnh_len = sizeof(*nhp);
		}
	}

	nla_nest_end(skb, mp_attr);

	lastuse = READ_ONCE(c->mfc_un.res.lastuse);
	lastuse = time_after_eq(jiffies, lastuse) ? jiffies - lastuse : 0;

	mfcs.mfcs_packets = c->mfc_un.res.pkt;
	mfcs.mfcs_bytes = c->mfc_un.res.bytes;
	mfcs.mfcs_wrong_if = c->mfc_un.res.wrong_if;
	if (nla_put_64bit(skb, RTA_MFC_STATS, sizeof(mfcs), &mfcs, RTA_PAD) ||
	    nla_put_u64_64bit(skb, RTA_EXPIRES, jiffies_to_clock_t(lastuse),
			      RTA_PAD))
		return -EMSGSIZE;

	rtm->rtm_type = RTN_MULTICAST;
	return 1;
}
EXPORT_SYMBOL(mr_fill_mroute);

int mr_rtm_dumproute(struct sk_buff *skb, struct netlink_callback *cb,
		     struct mr_table *(*iter)(struct net *net,
					      struct mr_table *mrt),
		     int (*fill)(struct mr_table *mrt,
				 struct sk_buff *skb,
				 u32 portid, u32 seq, struct mr_mfc *c,
				 int cmd, int flags),
		     spinlock_t *lock)
{
	unsigned int t = 0, e = 0, s_t = cb->args[0], s_e = cb->args[1];
	struct net *net = sock_net(skb->sk);
	struct mr_table *mrt;
	struct mr_mfc *mfc;

	rcu_read_lock();
	for (mrt = iter(net, NULL); mrt; mrt = iter(net, mrt)) {
		if (t < s_t)
			goto next_table;
		list_for_each_entry_rcu(mfc, &mrt->mfc_cache_list, list) {
			if (e < s_e)
				goto next_entry;
			if (fill(mrt, skb, NETLINK_CB(cb->skb).portid,
				 cb->nlh->nlmsg_seq, mfc,
				 RTM_NEWROUTE, NLM_F_MULTI) < 0)
				goto done;
next_entry:
			e++;
		}
		e = 0;
		s_e = 0;

		spin_lock_bh(lock);
		list_for_each_entry(mfc, &mrt->mfc_unres_queue, list) {
			if (e < s_e)
				goto next_entry2;
			if (fill(mrt, skb, NETLINK_CB(cb->skb).portid,
				 cb->nlh->nlmsg_seq, mfc,
				 RTM_NEWROUTE, NLM_F_MULTI) < 0) {
				spin_unlock_bh(lock);
				goto done;
			}
next_entry2:
			e++;
		}
		spin_unlock_bh(lock);
		e = 0;
		s_e = 0;
next_table:
		t++;
	}
done:
	rcu_read_unlock();

	cb->args[1] = e;
	cb->args[0] = t;

	return skb->len;
}
EXPORT_SYMBOL(mr_rtm_dumproute);

int mr_dump(struct net *net, struct notifier_block *nb, unsigned short family,
	    int (*rules_dump)(struct net *net,
			      struct notifier_block *nb),
	    struct mr_table *(*mr_iter)(struct net *net,
					struct mr_table *mrt),
	    rwlock_t *mrt_lock)
{
	struct mr_table *mrt;
	int err;

	err = rules_dump(net, nb);
	if (err)
		return err;

	for (mrt = mr_iter(net, NULL); mrt; mrt = mr_iter(net, mrt)) {
		struct vif_device *v = &mrt->vif_table[0];
		struct mr_mfc *mfc;
		int vifi;

		/* Notifiy on table VIF entries */
		read_lock(mrt_lock);
		for (vifi = 0; vifi < mrt->maxvif; vifi++, v++) {
			if (!v->dev)
				continue;

			mr_call_vif_notifier(nb, net, family,
					     FIB_EVENT_VIF_ADD,
					     v, vifi, mrt->id);
		}
		read_unlock(mrt_lock);

		/* Notify on table MFC entries */
		list_for_each_entry_rcu(mfc, &mrt->mfc_cache_list, list)
			mr_call_mfc_notifier(nb, net, family,
					     FIB_EVENT_ENTRY_ADD,
					     mfc, mrt->id);
	}

	return 0;
}
EXPORT_SYMBOL(mr_dump);
