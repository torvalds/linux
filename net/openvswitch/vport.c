// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2007-2014 Nicira, Inc.
 */

#include <linux/etherdevice.h>
#include <linux/if.h>
#include <linux/if_vlan.h>
#include <linux/jhash.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/percpu.h>
#include <linux/rcupdate.h>
#include <linux/rtnetlink.h>
#include <linux/compat.h>
#include <net/net_namespace.h>
#include <linux/module.h>

#include "datapath.h"
#include "vport.h"
#include "vport-internal_dev.h"

static LIST_HEAD(vport_ops_list);

/* Protected by RCU read lock for reading, ovs_mutex for writing. */
static struct hlist_head *dev_table;
#define VPORT_HASH_BUCKETS 1024

/**
 *	ovs_vport_init - initialize vport subsystem
 *
 * Called at module load time to initialize the vport subsystem.
 */
int ovs_vport_init(void)
{
	dev_table = kcalloc(VPORT_HASH_BUCKETS, sizeof(struct hlist_head),
			    GFP_KERNEL);
	if (!dev_table)
		return -ENOMEM;

	return 0;
}

/**
 *	ovs_vport_exit - shutdown vport subsystem
 *
 * Called at module exit time to shutdown the vport subsystem.
 */
void ovs_vport_exit(void)
{
	kfree(dev_table);
}

static struct hlist_head *hash_bucket(const struct net *net, const char *name)
{
	unsigned int hash = jhash(name, strlen(name), (unsigned long) net);
	return &dev_table[hash & (VPORT_HASH_BUCKETS - 1)];
}

int __ovs_vport_ops_register(struct vport_ops *ops)
{
	int err = -EEXIST;
	struct vport_ops *o;

	ovs_lock();
	list_for_each_entry(o, &vport_ops_list, list)
		if (ops->type == o->type)
			goto errout;

	list_add_tail(&ops->list, &vport_ops_list);
	err = 0;
errout:
	ovs_unlock();
	return err;
}
EXPORT_SYMBOL_GPL(__ovs_vport_ops_register);

void ovs_vport_ops_unregister(struct vport_ops *ops)
{
	ovs_lock();
	list_del(&ops->list);
	ovs_unlock();
}
EXPORT_SYMBOL_GPL(ovs_vport_ops_unregister);

/**
 *	ovs_vport_locate - find a port that has already been created
 *
 * @net: network namespace
 * @name: name of port to find
 *
 * Must be called with ovs or RCU read lock.
 */
struct vport *ovs_vport_locate(const struct net *net, const char *name)
{
	struct hlist_head *bucket = hash_bucket(net, name);
	struct vport *vport;

	hlist_for_each_entry_rcu(vport, bucket, hash_node,
				 lockdep_ovsl_is_held())
		if (!strcmp(name, ovs_vport_name(vport)) &&
		    net_eq(ovs_dp_get_net(vport->dp), net))
			return vport;

	return NULL;
}

/**
 *	ovs_vport_alloc - allocate and initialize new vport
 *
 * @priv_size: Size of private data area to allocate.
 * @ops: vport device ops
 * @parms: information about new vport.
 *
 * Allocate and initialize a new vport defined by @ops.  The vport will contain
 * a private data area of size @priv_size that can be accessed using
 * vport_priv().  Some parameters of the vport will be initialized from @parms.
 * @vports that are no longer needed should be released with
 * vport_free().
 */
struct vport *ovs_vport_alloc(int priv_size, const struct vport_ops *ops,
			      const struct vport_parms *parms)
{
	struct vport *vport;
	size_t alloc_size;
	int err;

	alloc_size = sizeof(struct vport);
	if (priv_size) {
		alloc_size = ALIGN(alloc_size, VPORT_ALIGN);
		alloc_size += priv_size;
	}

	vport = kzalloc(alloc_size, GFP_KERNEL);
	if (!vport)
		return ERR_PTR(-ENOMEM);

	vport->upcall_stats = netdev_alloc_pcpu_stats(struct vport_upcall_stats_percpu);
	if (!vport->upcall_stats) {
		err = -ENOMEM;
		goto err_kfree_vport;
	}

	vport->dp = parms->dp;
	vport->port_no = parms->port_no;
	vport->ops = ops;
	INIT_HLIST_NODE(&vport->dp_hash_node);

	if (ovs_vport_set_upcall_portids(vport, parms->upcall_portids)) {
		err = -EINVAL;
		goto err_free_percpu;
	}

	return vport;

err_free_percpu:
	free_percpu(vport->upcall_stats);
err_kfree_vport:
	kfree(vport);
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(ovs_vport_alloc);

/**
 *	ovs_vport_free - uninitialize and free vport
 *
 * @vport: vport to free
 *
 * Frees a vport allocated with vport_alloc() when it is no longer needed.
 *
 * The caller must ensure that an RCU grace period has passed since the last
 * time @vport was in a datapath.
 */
void ovs_vport_free(struct vport *vport)
{
	/* vport is freed from RCU callback or error path, Therefore
	 * it is safe to use raw dereference.
	 */
	kfree(rcu_dereference_raw(vport->upcall_portids));
	free_percpu(vport->upcall_stats);
	kfree(vport);
}
EXPORT_SYMBOL_GPL(ovs_vport_free);

static struct vport_ops *ovs_vport_lookup(const struct vport_parms *parms)
{
	struct vport_ops *ops;

	list_for_each_entry(ops, &vport_ops_list, list)
		if (ops->type == parms->type)
			return ops;

	return NULL;
}

/**
 *	ovs_vport_add - add vport device (for kernel callers)
 *
 * @parms: Information about new vport.
 *
 * Creates a new vport with the specified configuration (which is dependent on
 * device type).  ovs_mutex must be held.
 */
struct vport *ovs_vport_add(const struct vport_parms *parms)
{
	struct vport_ops *ops;
	struct vport *vport;

	ops = ovs_vport_lookup(parms);
	if (ops) {
		struct hlist_head *bucket;

		if (!try_module_get(ops->owner))
			return ERR_PTR(-EAFNOSUPPORT);

		vport = ops->create(parms);
		if (IS_ERR(vport)) {
			module_put(ops->owner);
			return vport;
		}

		bucket = hash_bucket(ovs_dp_get_net(vport->dp),
				     ovs_vport_name(vport));
		hlist_add_head_rcu(&vport->hash_node, bucket);
		return vport;
	}

	/* Unlock to attempt module load and return -EAGAIN if load
	 * was successful as we need to restart the port addition
	 * workflow.
	 */
	ovs_unlock();
	request_module("vport-type-%d", parms->type);
	ovs_lock();

	if (!ovs_vport_lookup(parms))
		return ERR_PTR(-EAFNOSUPPORT);
	else
		return ERR_PTR(-EAGAIN);
}

/**
 *	ovs_vport_set_options - modify existing vport device (for kernel callers)
 *
 * @vport: vport to modify.
 * @options: New configuration.
 *
 * Modifies an existing device with the specified configuration (which is
 * dependent on device type).  ovs_mutex must be held.
 */
int ovs_vport_set_options(struct vport *vport, struct nlattr *options)
{
	if (!vport->ops->set_options)
		return -EOPNOTSUPP;
	return vport->ops->set_options(vport, options);
}

/**
 *	ovs_vport_del - delete existing vport device
 *
 * @vport: vport to delete.
 *
 * Detaches @vport from its datapath and destroys it.  ovs_mutex must
 * be held.
 */
void ovs_vport_del(struct vport *vport)
{
	hlist_del_rcu(&vport->hash_node);
	module_put(vport->ops->owner);
	vport->ops->destroy(vport);
}

/**
 *	ovs_vport_get_stats - retrieve device stats
 *
 * @vport: vport from which to retrieve the stats
 * @stats: location to store stats
 *
 * Retrieves transmit, receive, and error stats for the given device.
 *
 * Must be called with ovs_mutex or rcu_read_lock.
 */
void ovs_vport_get_stats(struct vport *vport, struct ovs_vport_stats *stats)
{
	const struct rtnl_link_stats64 *dev_stats;
	struct rtnl_link_stats64 temp;

	dev_stats = dev_get_stats(vport->dev, &temp);
	stats->rx_errors  = dev_stats->rx_errors;
	stats->tx_errors  = dev_stats->tx_errors;
	stats->tx_dropped = dev_stats->tx_dropped;
	stats->rx_dropped = dev_stats->rx_dropped;

	stats->rx_bytes	  = dev_stats->rx_bytes;
	stats->rx_packets = dev_stats->rx_packets;
	stats->tx_bytes	  = dev_stats->tx_bytes;
	stats->tx_packets = dev_stats->tx_packets;
}

/**
 *	ovs_vport_get_upcall_stats - retrieve upcall stats
 *
 * @vport: vport from which to retrieve the stats.
 * @skb: sk_buff where upcall stats should be appended.
 *
 * Retrieves upcall stats for the given device.
 *
 * Must be called with ovs_mutex or rcu_read_lock.
 */
int ovs_vport_get_upcall_stats(struct vport *vport, struct sk_buff *skb)
{
	struct nlattr *nla;
	int i;

	__u64 tx_success = 0;
	__u64 tx_fail = 0;

	for_each_possible_cpu(i) {
		const struct vport_upcall_stats_percpu *stats;
		unsigned int start;

		stats = per_cpu_ptr(vport->upcall_stats, i);
		do {
			start = u64_stats_fetch_begin(&stats->syncp);
			tx_success += u64_stats_read(&stats->n_success);
			tx_fail += u64_stats_read(&stats->n_fail);
		} while (u64_stats_fetch_retry(&stats->syncp, start));
	}

	nla = nla_nest_start_noflag(skb, OVS_VPORT_ATTR_UPCALL_STATS);
	if (!nla)
		return -EMSGSIZE;

	if (nla_put_u64_64bit(skb, OVS_VPORT_UPCALL_ATTR_SUCCESS, tx_success,
			      OVS_VPORT_ATTR_PAD)) {
		nla_nest_cancel(skb, nla);
		return -EMSGSIZE;
	}

	if (nla_put_u64_64bit(skb, OVS_VPORT_UPCALL_ATTR_FAIL, tx_fail,
			      OVS_VPORT_ATTR_PAD)) {
		nla_nest_cancel(skb, nla);
		return -EMSGSIZE;
	}
	nla_nest_end(skb, nla);

	return 0;
}

/**
 *	ovs_vport_get_options - retrieve device options
 *
 * @vport: vport from which to retrieve the options.
 * @skb: sk_buff where options should be appended.
 *
 * Retrieves the configuration of the given device, appending an
 * %OVS_VPORT_ATTR_OPTIONS attribute that in turn contains nested
 * vport-specific attributes to @skb.
 *
 * Returns 0 if successful, -EMSGSIZE if @skb has insufficient room, or another
 * negative error code if a real error occurred.  If an error occurs, @skb is
 * left unmodified.
 *
 * Must be called with ovs_mutex or rcu_read_lock.
 */
int ovs_vport_get_options(const struct vport *vport, struct sk_buff *skb)
{
	struct nlattr *nla;
	int err;

	if (!vport->ops->get_options)
		return 0;

	nla = nla_nest_start_noflag(skb, OVS_VPORT_ATTR_OPTIONS);
	if (!nla)
		return -EMSGSIZE;

	err = vport->ops->get_options(vport, skb);
	if (err) {
		nla_nest_cancel(skb, nla);
		return err;
	}

	nla_nest_end(skb, nla);
	return 0;
}

/**
 *	ovs_vport_set_upcall_portids - set upcall portids of @vport.
 *
 * @vport: vport to modify.
 * @ids: new configuration, an array of port ids.
 *
 * Sets the vport's upcall_portids to @ids.
 *
 * Returns 0 if successful, -EINVAL if @ids is zero length or cannot be parsed
 * as an array of U32.
 *
 * Must be called with ovs_mutex.
 */
int ovs_vport_set_upcall_portids(struct vport *vport, const struct nlattr *ids)
{
	struct vport_portids *old, *vport_portids;

	if (!nla_len(ids) || nla_len(ids) % sizeof(u32))
		return -EINVAL;

	old = ovsl_dereference(vport->upcall_portids);

	vport_portids = kmalloc(sizeof(*vport_portids) + nla_len(ids),
				GFP_KERNEL);
	if (!vport_portids)
		return -ENOMEM;

	vport_portids->n_ids = nla_len(ids) / sizeof(u32);
	vport_portids->rn_ids = reciprocal_value(vport_portids->n_ids);
	nla_memcpy(vport_portids->ids, ids, nla_len(ids));

	rcu_assign_pointer(vport->upcall_portids, vport_portids);

	if (old)
		kfree_rcu(old, rcu);
	return 0;
}

/**
 *	ovs_vport_get_upcall_portids - get the upcall_portids of @vport.
 *
 * @vport: vport from which to retrieve the portids.
 * @skb: sk_buff where portids should be appended.
 *
 * Retrieves the configuration of the given vport, appending the
 * %OVS_VPORT_ATTR_UPCALL_PID attribute which is the array of upcall
 * portids to @skb.
 *
 * Returns 0 if successful, -EMSGSIZE if @skb has insufficient room.
 * If an error occurs, @skb is left unmodified.  Must be called with
 * ovs_mutex or rcu_read_lock.
 */
int ovs_vport_get_upcall_portids(const struct vport *vport,
				 struct sk_buff *skb)
{
	struct vport_portids *ids;

	ids = rcu_dereference_ovsl(vport->upcall_portids);

	if (vport->dp->user_features & OVS_DP_F_VPORT_PIDS)
		return nla_put(skb, OVS_VPORT_ATTR_UPCALL_PID,
			       ids->n_ids * sizeof(u32), (void *)ids->ids);
	else
		return nla_put_u32(skb, OVS_VPORT_ATTR_UPCALL_PID, ids->ids[0]);
}

/**
 *	ovs_vport_find_upcall_portid - find the upcall portid to send upcall.
 *
 * @vport: vport from which the missed packet is received.
 * @skb: skb that the missed packet was received.
 *
 * Uses the skb_get_hash() to select the upcall portid to send the
 * upcall.
 *
 * Returns the portid of the target socket.  Must be called with rcu_read_lock.
 */
u32 ovs_vport_find_upcall_portid(const struct vport *vport,
				 struct sk_buff *skb)
{
	struct vport_portids *ids;
	u32 ids_index;
	u32 hash;

	ids = rcu_dereference(vport->upcall_portids);

	/* If there is only one portid, select it in the fast-path. */
	if (ids->n_ids == 1)
		return ids->ids[0];

	hash = skb_get_hash(skb);
	ids_index = hash - ids->n_ids * reciprocal_divide(hash, ids->rn_ids);
	return ids->ids[ids_index];
}

/**
 *	ovs_vport_receive - pass up received packet to the datapath for processing
 *
 * @vport: vport that received the packet
 * @skb: skb that was received
 * @tun_info: tunnel (if any) that carried packet
 *
 * Must be called with rcu_read_lock.  The packet cannot be shared and
 * skb->data should point to the Ethernet header.
 */
int ovs_vport_receive(struct vport *vport, struct sk_buff *skb,
		      const struct ip_tunnel_info *tun_info)
{
	struct sw_flow_key key;
	int error;

	OVS_CB(skb)->input_vport = vport;
	OVS_CB(skb)->mru = 0;
	OVS_CB(skb)->cutlen = 0;
	OVS_CB(skb)->probability = 0;
	if (unlikely(dev_net(skb->dev) != ovs_dp_get_net(vport->dp))) {
		u32 mark;

		mark = skb->mark;
		skb_scrub_packet(skb, true);
		skb->mark = mark;
		tun_info = NULL;
	}

	/* Extract flow from 'skb' into 'key'. */
	error = ovs_flow_key_extract(tun_info, skb, &key);
	if (unlikely(error)) {
		kfree_skb(skb);
		return error;
	}
	ovs_dp_process_packet(skb, &key);
	return 0;
}

static int packet_length(const struct sk_buff *skb,
			 struct net_device *dev)
{
	int length = skb->len - dev->hard_header_len;

	if (!skb_vlan_tag_present(skb) &&
	    eth_type_vlan(skb->protocol))
		length -= VLAN_HLEN;

	/* Don't subtract for multiple VLAN tags. Most (all?) drivers allow
	 * (ETH_LEN + VLAN_HLEN) in addition to the mtu value, but almost none
	 * account for 802.1ad. e.g. is_skb_forwardable().
	 */

	return length > 0 ? length : 0;
}

void ovs_vport_send(struct vport *vport, struct sk_buff *skb, u8 mac_proto)
{
	int mtu = vport->dev->mtu;

	switch (vport->dev->type) {
	case ARPHRD_NONE:
		if (mac_proto == MAC_PROTO_ETHERNET) {
			skb_reset_network_header(skb);
			skb_reset_mac_len(skb);
			skb->protocol = htons(ETH_P_TEB);
		} else if (mac_proto != MAC_PROTO_NONE) {
			WARN_ON_ONCE(1);
			goto drop;
		}
		break;
	case ARPHRD_ETHER:
		if (mac_proto != MAC_PROTO_ETHERNET)
			goto drop;
		break;
	default:
		goto drop;
	}

	if (unlikely(packet_length(skb, vport->dev) > mtu &&
		     !skb_is_gso(skb))) {
		vport->dev->stats.tx_errors++;
		if (vport->dev->flags & IFF_UP)
			net_warn_ratelimited("%s: dropped over-mtu packet: "
					     "%d > %d\n", vport->dev->name,
					     packet_length(skb, vport->dev),
					     mtu);
		goto drop;
	}

	skb->dev = vport->dev;
	skb_clear_tstamp(skb);
	vport->ops->send(skb);
	return;

drop:
	kfree_skb(skb);
}
