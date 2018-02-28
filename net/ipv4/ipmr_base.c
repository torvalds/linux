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
	rhltable_init(&mrt->mfc_hash, mrt->ops.rht_params);
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
