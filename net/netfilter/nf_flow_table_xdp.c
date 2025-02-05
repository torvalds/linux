// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/rhashtable.h>
#include <linux/netdevice.h>
#include <net/flow_offload.h>
#include <net/netfilter/nf_flow_table.h>

struct flow_offload_xdp_ft {
	struct list_head head;
	struct nf_flowtable *ft;
	struct rcu_head rcuhead;
};

struct flow_offload_xdp {
	struct hlist_node hnode;
	unsigned long net_device_addr;
	struct list_head head;
};

#define NF_XDP_HT_BITS	4
static DEFINE_HASHTABLE(nf_xdp_hashtable, NF_XDP_HT_BITS);
static DEFINE_MUTEX(nf_xdp_hashtable_lock);

/* caller must hold rcu read lock */
struct nf_flowtable *nf_flowtable_by_dev(const struct net_device *dev)
{
	unsigned long key = (unsigned long)dev;
	struct flow_offload_xdp *iter;

	hash_for_each_possible_rcu(nf_xdp_hashtable, iter, hnode, key) {
		if (key == iter->net_device_addr) {
			struct flow_offload_xdp_ft *ft_elem;

			/* The user is supposed to insert a given net_device
			 * just into a single nf_flowtable so we always return
			 * the first element here.
			 */
			ft_elem = list_first_or_null_rcu(&iter->head,
							 struct flow_offload_xdp_ft,
							 head);
			return ft_elem ? ft_elem->ft : NULL;
		}
	}

	return NULL;
}

static int nf_flowtable_by_dev_insert(struct nf_flowtable *ft,
				      const struct net_device *dev)
{
	struct flow_offload_xdp *iter, *elem = NULL;
	unsigned long key = (unsigned long)dev;
	struct flow_offload_xdp_ft *ft_elem;

	ft_elem = kzalloc(sizeof(*ft_elem), GFP_KERNEL_ACCOUNT);
	if (!ft_elem)
		return -ENOMEM;

	ft_elem->ft = ft;

	mutex_lock(&nf_xdp_hashtable_lock);

	hash_for_each_possible(nf_xdp_hashtable, iter, hnode, key) {
		if (key == iter->net_device_addr) {
			elem = iter;
			break;
		}
	}

	if (!elem) {
		elem = kzalloc(sizeof(*elem), GFP_KERNEL_ACCOUNT);
		if (!elem)
			goto err_unlock;

		elem->net_device_addr = key;
		INIT_LIST_HEAD(&elem->head);
		hash_add_rcu(nf_xdp_hashtable, &elem->hnode, key);
	}
	list_add_tail_rcu(&ft_elem->head, &elem->head);

	mutex_unlock(&nf_xdp_hashtable_lock);

	return 0;

err_unlock:
	mutex_unlock(&nf_xdp_hashtable_lock);
	kfree(ft_elem);

	return -ENOMEM;
}

static void nf_flowtable_by_dev_remove(struct nf_flowtable *ft,
				       const struct net_device *dev)
{
	struct flow_offload_xdp *iter, *elem = NULL;
	unsigned long key = (unsigned long)dev;

	mutex_lock(&nf_xdp_hashtable_lock);

	hash_for_each_possible(nf_xdp_hashtable, iter, hnode, key) {
		if (key == iter->net_device_addr) {
			elem = iter;
			break;
		}
	}

	if (elem) {
		struct flow_offload_xdp_ft *ft_elem, *ft_next;

		list_for_each_entry_safe(ft_elem, ft_next, &elem->head, head) {
			if (ft_elem->ft == ft) {
				list_del_rcu(&ft_elem->head);
				kfree_rcu(ft_elem, rcuhead);
			}
		}

		if (list_empty(&elem->head))
			hash_del_rcu(&elem->hnode);
		else
			elem = NULL;
	}

	mutex_unlock(&nf_xdp_hashtable_lock);

	if (elem) {
		synchronize_rcu();
		kfree(elem);
	}
}

int nf_flow_offload_xdp_setup(struct nf_flowtable *flowtable,
			      struct net_device *dev,
			      enum flow_block_command cmd)
{
	switch (cmd) {
	case FLOW_BLOCK_BIND:
		return nf_flowtable_by_dev_insert(flowtable, dev);
	case FLOW_BLOCK_UNBIND:
		nf_flowtable_by_dev_remove(flowtable, dev);
		return 0;
	}

	WARN_ON_ONCE(1);
	return 0;
}
