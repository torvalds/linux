/*
 * Copyright (c) 2007, 2014 Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/compat.h>
#ifdef CONFIG_NET_RX_BUSY_POLL
#include <net/busy_poll.h>
#endif

#include <linux/list.h>
#include <linux/if_ether.h>

#include <dev/mlx4/driver.h>
#include <dev/mlx4/device.h>
#include <dev/mlx4/cmd.h>
#include <dev/mlx4/cq.h>

#include <sys/sockio.h>
#include <sys/sysctl.h>

#include "en.h"
#include "en_port.h"

NETDUMP_DEFINE(mlx4_en);

static void mlx4_en_sysctl_stat(struct mlx4_en_priv *priv);
static void mlx4_en_sysctl_conf(struct mlx4_en_priv *priv);

#ifdef CONFIG_NET_RX_BUSY_POLL
/* must be called with local_bh_disable()d */
static int mlx4_en_low_latency_recv(struct napi_struct *napi)
{
	struct mlx4_en_cq *cq = container_of(napi, struct mlx4_en_cq, napi);
	struct net_device *dev = cq->dev;
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_rx_ring *rx_ring = priv->rx_ring[cq->ring];
	int done;

	if (!priv->port_up)
		return LL_FLUSH_FAILED;

	if (!mlx4_en_cq_lock_poll(cq))
		return LL_FLUSH_BUSY;

	done = mlx4_en_process_rx_cq(dev, cq, 4);
#ifdef LL_EXTENDED_STATS
	if (likely(done))
		rx_ring->cleaned += done;
	else
		rx_ring->misses++;
#endif

	mlx4_en_cq_unlock_poll(cq);

	return done;
}
#endif	/* CONFIG_NET_RX_BUSY_POLL */

#ifdef CONFIG_RFS_ACCEL

struct mlx4_en_filter {
	struct list_head next;
	struct work_struct work;

	u8     ip_proto;
	__be32 src_ip;
	__be32 dst_ip;
	__be16 src_port;
	__be16 dst_port;

	int rxq_index;
	struct mlx4_en_priv *priv;
	u32 flow_id;			/* RFS infrastructure id */
	int id;				/* mlx4_en driver id */
	u64 reg_id;			/* Flow steering API id */
	u8 activated;			/* Used to prevent expiry before filter
					 * is attached
					 */
	struct hlist_node filter_chain;
};

static void mlx4_en_filter_rfs_expire(struct mlx4_en_priv *priv);

static enum mlx4_net_trans_rule_id mlx4_ip_proto_to_trans_rule_id(u8 ip_proto)
{
	switch (ip_proto) {
	case IPPROTO_UDP:
		return MLX4_NET_TRANS_RULE_ID_UDP;
	case IPPROTO_TCP:
		return MLX4_NET_TRANS_RULE_ID_TCP;
	default:
		return MLX4_NET_TRANS_RULE_NUM;
	}
};

static void mlx4_en_filter_work(struct work_struct *work)
{
	struct mlx4_en_filter *filter = container_of(work,
						     struct mlx4_en_filter,
						     work);
	struct mlx4_en_priv *priv = filter->priv;
	struct mlx4_spec_list spec_tcp_udp = {
		.id = mlx4_ip_proto_to_trans_rule_id(filter->ip_proto),
		{
			.tcp_udp = {
				.dst_port = filter->dst_port,
				.dst_port_msk = (__force __be16)-1,
				.src_port = filter->src_port,
				.src_port_msk = (__force __be16)-1,
			},
		},
	};
	struct mlx4_spec_list spec_ip = {
		.id = MLX4_NET_TRANS_RULE_ID_IPV4,
		{
			.ipv4 = {
				.dst_ip = filter->dst_ip,
				.dst_ip_msk = (__force __be32)-1,
				.src_ip = filter->src_ip,
				.src_ip_msk = (__force __be32)-1,
			},
		},
	};
	struct mlx4_spec_list spec_eth = {
		.id = MLX4_NET_TRANS_RULE_ID_ETH,
	};
	struct mlx4_net_trans_rule rule = {
		.list = LIST_HEAD_INIT(rule.list),
		.queue_mode = MLX4_NET_TRANS_Q_LIFO,
		.exclusive = 1,
		.allow_loopback = 1,
		.promisc_mode = MLX4_FS_REGULAR,
		.port = priv->port,
		.priority = MLX4_DOMAIN_RFS,
	};
	int rc;
	__be64 mac_mask = cpu_to_be64(MLX4_MAC_MASK << 16);

	if (spec_tcp_udp.id >= MLX4_NET_TRANS_RULE_NUM) {
		en_warn(priv, "RFS: ignoring unsupported ip protocol (%d)\n",
			filter->ip_proto);
		goto ignore;
	}
	list_add_tail(&spec_eth.list, &rule.list);
	list_add_tail(&spec_ip.list, &rule.list);
	list_add_tail(&spec_tcp_udp.list, &rule.list);

	rule.qpn = priv->rss_map.qps[filter->rxq_index].qpn;
	memcpy(spec_eth.eth.dst_mac, priv->dev->dev_addr, ETH_ALEN);
	memcpy(spec_eth.eth.dst_mac_msk, &mac_mask, ETH_ALEN);

	filter->activated = 0;

	if (filter->reg_id) {
		rc = mlx4_flow_detach(priv->mdev->dev, filter->reg_id);
		if (rc && rc != -ENOENT)
			en_err(priv, "Error detaching flow. rc = %d\n", rc);
	}

	rc = mlx4_flow_attach(priv->mdev->dev, &rule, &filter->reg_id);
	if (rc)
		en_err(priv, "Error attaching flow. err = %d\n", rc);

ignore:
	mlx4_en_filter_rfs_expire(priv);

	filter->activated = 1;
}

static inline struct hlist_head *
filter_hash_bucket(struct mlx4_en_priv *priv, __be32 src_ip, __be32 dst_ip,
		   __be16 src_port, __be16 dst_port)
{
	unsigned long l;
	int bucket_idx;

	l = (__force unsigned long)src_port |
	    ((__force unsigned long)dst_port << 2);
	l ^= (__force unsigned long)(src_ip ^ dst_ip);

	bucket_idx = hash_long(l, MLX4_EN_FILTER_HASH_SHIFT);

	return &priv->filter_hash[bucket_idx];
}

static struct mlx4_en_filter *
mlx4_en_filter_alloc(struct mlx4_en_priv *priv, int rxq_index, __be32 src_ip,
		     __be32 dst_ip, u8 ip_proto, __be16 src_port,
		     __be16 dst_port, u32 flow_id)
{
	struct mlx4_en_filter *filter = NULL;

	filter = kzalloc(sizeof(struct mlx4_en_filter), GFP_ATOMIC);
	if (!filter)
		return NULL;

	filter->priv = priv;
	filter->rxq_index = rxq_index;
	INIT_WORK(&filter->work, mlx4_en_filter_work);

	filter->src_ip = src_ip;
	filter->dst_ip = dst_ip;
	filter->ip_proto = ip_proto;
	filter->src_port = src_port;
	filter->dst_port = dst_port;

	filter->flow_id = flow_id;

	filter->id = priv->last_filter_id++ % RPS_NO_FILTER;

	list_add_tail(&filter->next, &priv->filters);
	hlist_add_head(&filter->filter_chain,
		       filter_hash_bucket(priv, src_ip, dst_ip, src_port,
					  dst_port));

	return filter;
}

static void mlx4_en_filter_free(struct mlx4_en_filter *filter)
{
	struct mlx4_en_priv *priv = filter->priv;
	int rc;

	list_del(&filter->next);

	rc = mlx4_flow_detach(priv->mdev->dev, filter->reg_id);
	if (rc && rc != -ENOENT)
		en_err(priv, "Error detaching flow. rc = %d\n", rc);

	kfree(filter);
}

static inline struct mlx4_en_filter *
mlx4_en_filter_find(struct mlx4_en_priv *priv, __be32 src_ip, __be32 dst_ip,
		    u8 ip_proto, __be16 src_port, __be16 dst_port)
{
	struct mlx4_en_filter *filter;
	struct mlx4_en_filter *ret = NULL;

	hlist_for_each_entry(filter,
			     filter_hash_bucket(priv, src_ip, dst_ip,
						src_port, dst_port),
			     filter_chain) {
		if (filter->src_ip == src_ip &&
		    filter->dst_ip == dst_ip &&
		    filter->ip_proto == ip_proto &&
		    filter->src_port == src_port &&
		    filter->dst_port == dst_port) {
			ret = filter;
			break;
		}
	}

	return ret;
}

static int
mlx4_en_filter_rfs(struct net_device *net_dev, const struct sk_buff *skb,
		   u16 rxq_index, u32 flow_id)
{
	struct mlx4_en_priv *priv = netdev_priv(net_dev);
	struct mlx4_en_filter *filter;
	const struct iphdr *ip;
	const __be16 *ports;
	u8 ip_proto;
	__be32 src_ip;
	__be32 dst_ip;
	__be16 src_port;
	__be16 dst_port;
	int nhoff = skb_network_offset(skb);
	int ret = 0;

	if (skb->protocol != htons(ETH_P_IP))
		return -EPROTONOSUPPORT;

	ip = (const struct iphdr *)(skb->data + nhoff);
	if (ip_is_fragment(ip))
		return -EPROTONOSUPPORT;

	if ((ip->protocol != IPPROTO_TCP) && (ip->protocol != IPPROTO_UDP))
		return -EPROTONOSUPPORT;
	ports = (const __be16 *)(skb->data + nhoff + 4 * ip->ihl);

	ip_proto = ip->protocol;
	src_ip = ip->saddr;
	dst_ip = ip->daddr;
	src_port = ports[0];
	dst_port = ports[1];

	spin_lock_bh(&priv->filters_lock);
	filter = mlx4_en_filter_find(priv, src_ip, dst_ip, ip_proto,
				     src_port, dst_port);
	if (filter) {
		if (filter->rxq_index == rxq_index)
			goto out;

		filter->rxq_index = rxq_index;
	} else {
		filter = mlx4_en_filter_alloc(priv, rxq_index,
					      src_ip, dst_ip, ip_proto,
					      src_port, dst_port, flow_id);
		if (!filter) {
			ret = -ENOMEM;
			goto err;
		}
	}

	queue_work(priv->mdev->workqueue, &filter->work);

out:
	ret = filter->id;
err:
	spin_unlock_bh(&priv->filters_lock);

	return ret;
}

void mlx4_en_cleanup_filters(struct mlx4_en_priv *priv)
{
	struct mlx4_en_filter *filter, *tmp;
	LIST_HEAD(del_list);

	spin_lock_bh(&priv->filters_lock);
	list_for_each_entry_safe(filter, tmp, &priv->filters, next) {
		list_move(&filter->next, &del_list);
		hlist_del(&filter->filter_chain);
	}
	spin_unlock_bh(&priv->filters_lock);

	list_for_each_entry_safe(filter, tmp, &del_list, next) {
		cancel_work_sync(&filter->work);
		mlx4_en_filter_free(filter);
	}
}

static void mlx4_en_filter_rfs_expire(struct mlx4_en_priv *priv)
{
	struct mlx4_en_filter *filter = NULL, *tmp, *last_filter = NULL;
	LIST_HEAD(del_list);
	int i = 0;

	spin_lock_bh(&priv->filters_lock);
	list_for_each_entry_safe(filter, tmp, &priv->filters, next) {
		if (i > MLX4_EN_FILTER_EXPIRY_QUOTA)
			break;

		if (filter->activated &&
		    !work_pending(&filter->work) &&
		    rps_may_expire_flow(priv->dev,
					filter->rxq_index, filter->flow_id,
					filter->id)) {
			list_move(&filter->next, &del_list);
			hlist_del(&filter->filter_chain);
		} else
			last_filter = filter;

		i++;
	}

	if (last_filter && (&last_filter->next != priv->filters.next))
		list_move(&priv->filters, &last_filter->next);

	spin_unlock_bh(&priv->filters_lock);

	list_for_each_entry_safe(filter, tmp, &del_list, next)
		mlx4_en_filter_free(filter);
}
#endif

static void mlx4_en_vlan_rx_add_vid(void *arg, struct net_device *dev, u16 vid)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	int err;
	int idx;

	if (arg != priv)
		return;

	en_dbg(HW, priv, "adding VLAN:%d\n", vid);

	set_bit(vid, priv->active_vlans);

	/* Add VID to port VLAN filter */
	mutex_lock(&mdev->state_lock);
	if (mdev->device_up && priv->port_up) {
		err = mlx4_SET_VLAN_FLTR(mdev->dev, priv);
		if (err)
			en_err(priv, "Failed configuring VLAN filter\n");
	}
	if (mlx4_register_vlan(mdev->dev, priv->port, vid, &idx))
		en_dbg(HW, priv, "failed adding vlan %d\n", vid);
	mutex_unlock(&mdev->state_lock);

}

static void mlx4_en_vlan_rx_kill_vid(void *arg, struct net_device *dev, u16 vid)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	int err;

	if (arg != priv)
		return;

	en_dbg(HW, priv, "Killing VID:%d\n", vid);

	clear_bit(vid, priv->active_vlans);

	/* Remove VID from port VLAN filter */
	mutex_lock(&mdev->state_lock);
	mlx4_unregister_vlan(mdev->dev, priv->port, vid);

	if (mdev->device_up && priv->port_up) {
		err = mlx4_SET_VLAN_FLTR(mdev->dev, priv);
		if (err)
			en_err(priv, "Failed configuring VLAN filter\n");
	}
	mutex_unlock(&mdev->state_lock);

}

static int mlx4_en_tunnel_steer_add(struct mlx4_en_priv *priv, unsigned char *addr,
				    int qpn, u64 *reg_id)
{
	int err;

	if (priv->mdev->dev->caps.tunnel_offload_mode != MLX4_TUNNEL_OFFLOAD_MODE_VXLAN ||
	    priv->mdev->dev->caps.dmfs_high_steer_mode == MLX4_STEERING_DMFS_A0_STATIC)
		return 0; /* do nothing */

	err = mlx4_tunnel_steer_add(priv->mdev->dev, addr, priv->port, qpn,
				    MLX4_DOMAIN_NIC, reg_id);
	if (err) {
		en_err(priv, "failed to add vxlan steering rule, err %d\n", err);
		return err;
	}
	en_dbg(DRV, priv, "added vxlan steering rule, mac %pM reg_id %llx\n", addr, (long long)*reg_id);
	return 0;
}

static int mlx4_en_uc_steer_add(struct mlx4_en_priv *priv,
				unsigned char *mac, int *qpn, u64 *reg_id)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_dev *dev = mdev->dev;
	int err;

	switch (dev->caps.steering_mode) {
	case MLX4_STEERING_MODE_B0: {
		struct mlx4_qp qp;
		u8 gid[16] = {0};

		qp.qpn = *qpn;
		memcpy(&gid[10], mac, ETH_ALEN);
		gid[5] = priv->port;

		err = mlx4_unicast_attach(dev, &qp, gid, 0, MLX4_PROT_ETH);
		break;
	}
	case MLX4_STEERING_MODE_DEVICE_MANAGED: {
		struct mlx4_spec_list spec_eth = { {NULL} };
		__be64 mac_mask = cpu_to_be64(MLX4_MAC_MASK << 16);

		struct mlx4_net_trans_rule rule = {
			.queue_mode = MLX4_NET_TRANS_Q_FIFO,
			.exclusive = 0,
			.allow_loopback = 1,
			.promisc_mode = MLX4_FS_REGULAR,
			.priority = MLX4_DOMAIN_NIC,
		};

		rule.port = priv->port;
		rule.qpn = *qpn;
		INIT_LIST_HEAD(&rule.list);

		spec_eth.id = MLX4_NET_TRANS_RULE_ID_ETH;
		memcpy(spec_eth.eth.dst_mac, mac, ETH_ALEN);
		memcpy(spec_eth.eth.dst_mac_msk, &mac_mask, ETH_ALEN);
		list_add_tail(&spec_eth.list, &rule.list);

		err = mlx4_flow_attach(dev, &rule, reg_id);
		break;
	}
	default:
		return -EINVAL;
	}
	if (err)
		en_warn(priv, "Failed Attaching Unicast\n");

	return err;
}

static void mlx4_en_uc_steer_release(struct mlx4_en_priv *priv,
				     unsigned char *mac, int qpn, u64 reg_id)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_dev *dev = mdev->dev;

	switch (dev->caps.steering_mode) {
	case MLX4_STEERING_MODE_B0: {
		struct mlx4_qp qp;
		u8 gid[16] = {0};

		qp.qpn = qpn;
		memcpy(&gid[10], mac, ETH_ALEN);
		gid[5] = priv->port;

		mlx4_unicast_detach(dev, &qp, gid, MLX4_PROT_ETH);
		break;
	}
	case MLX4_STEERING_MODE_DEVICE_MANAGED: {
		mlx4_flow_detach(dev, reg_id);
		break;
	}
	default:
		en_err(priv, "Invalid steering mode.\n");
	}
}

static int mlx4_en_get_qp(struct mlx4_en_priv *priv)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_dev *dev = mdev->dev;
	int index = 0;
	int err = 0;
	int *qpn = &priv->base_qpn;
	u64 mac = mlx4_mac_to_u64(IF_LLADDR(priv->dev));

	en_dbg(DRV, priv, "Registering MAC: %pM for adding\n",
	       IF_LLADDR(priv->dev));
	index = mlx4_register_mac(dev, priv->port, mac);
	if (index < 0) {
		err = index;
		en_err(priv, "Failed adding MAC: %pM\n",
		       IF_LLADDR(priv->dev));
		return err;
	}

	if (dev->caps.steering_mode == MLX4_STEERING_MODE_A0) {
		int base_qpn = mlx4_get_base_qpn(dev, priv->port);
		*qpn = base_qpn + index;
		return 0;
	}

	err = mlx4_qp_reserve_range(dev, 1, 1, qpn, MLX4_RESERVE_A0_QP);
	en_dbg(DRV, priv, "Reserved qp %d\n", *qpn);
	if (err) {
		en_err(priv, "Failed to reserve qp for mac registration\n");
		mlx4_unregister_mac(dev, priv->port, mac);
		return err;
	}

	return 0;
}

static void mlx4_en_put_qp(struct mlx4_en_priv *priv)
{
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_dev *dev = mdev->dev;
	int qpn = priv->base_qpn;

	if (dev->caps.steering_mode == MLX4_STEERING_MODE_A0) {
		u64 mac = mlx4_mac_to_u64(IF_LLADDR(priv->dev));
		en_dbg(DRV, priv, "Registering MAC: %pM for deleting\n",
		       IF_LLADDR(priv->dev));
		mlx4_unregister_mac(dev, priv->port, mac);
	} else {
		en_dbg(DRV, priv, "Releasing qp: port %d, qpn %d\n",
		       priv->port, qpn);
		mlx4_qp_release_range(dev, qpn, 1);
		priv->flags &= ~MLX4_EN_FLAG_FORCE_PROMISC;
	}
}

static void mlx4_en_clear_uclist(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_addr_list *tmp, *uc_to_del;

	list_for_each_entry_safe(uc_to_del, tmp, &priv->uc_list, list) {
		list_del(&uc_to_del->list);
		kfree(uc_to_del);
	}
}

static void mlx4_en_cache_uclist(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_addr_list *tmp;
	struct ifaddr *ifa;

	mlx4_en_clear_uclist(dev);

	if_addr_rlock(dev);
	CK_STAILQ_FOREACH(ifa, &dev->if_addrhead, ifa_link) {
		if (ifa->ifa_addr->sa_family != AF_LINK)
			continue;
		if (((struct sockaddr_dl *)ifa->ifa_addr)->sdl_alen !=
				ETHER_ADDR_LEN)
			continue;
		tmp = kzalloc(sizeof(struct mlx4_en_addr_list), GFP_ATOMIC);
		if (tmp == NULL) {
			en_err(priv, "Failed to allocate address list\n");
			break;
		}
		memcpy(tmp->addr,
			LLADDR((struct sockaddr_dl *)ifa->ifa_addr), ETH_ALEN);
		list_add_tail(&tmp->list, &priv->uc_list);
	}
	if_addr_runlock(dev);
}

static void mlx4_en_clear_mclist(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_addr_list *tmp, *mc_to_del;

	list_for_each_entry_safe(mc_to_del, tmp, &priv->mc_list, list) {
		list_del(&mc_to_del->list);
		kfree(mc_to_del);
	}
}

static void mlx4_en_cache_mclist(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_addr_list *tmp;
	struct ifmultiaddr *ifma;

	mlx4_en_clear_mclist(dev);

	if_maddr_rlock(dev);
	CK_STAILQ_FOREACH(ifma, &dev->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		if (((struct sockaddr_dl *)ifma->ifma_addr)->sdl_alen !=
				ETHER_ADDR_LEN)
			continue;
		tmp = kzalloc(sizeof(struct mlx4_en_addr_list), GFP_ATOMIC);
		if (tmp == NULL) {
			en_err(priv, "Failed to allocate address list\n");
			break;
		}
		memcpy(tmp->addr,
			LLADDR((struct sockaddr_dl *)ifma->ifma_addr), ETH_ALEN);
		list_add_tail(&tmp->list, &priv->mc_list);
	}
	if_maddr_runlock(dev);
}

static void update_addr_list_flags(struct mlx4_en_priv *priv,
				struct list_head *dst,
				struct list_head *src)
{
	struct mlx4_en_addr_list *dst_tmp, *src_tmp, *new_mc;
	bool found;

	/* Find all the entries that should be removed from dst,
	 * These are the entries that are not found in src
	 */
	list_for_each_entry(dst_tmp, dst, list) {
		found = false;
		list_for_each_entry(src_tmp, src, list) {
			if (!memcmp(dst_tmp->addr, src_tmp->addr, ETH_ALEN)) {
				found = true;
				break;
			}
		}
		if (!found)
			dst_tmp->action = MLX4_ADDR_LIST_REM;
	}

	/* Add entries that exist in src but not in dst
	 * mark them as need to add
	 */
	list_for_each_entry(src_tmp, src, list) {
		found = false;
		list_for_each_entry(dst_tmp, dst, list) {
			if (!memcmp(dst_tmp->addr, src_tmp->addr, ETH_ALEN)) {
				dst_tmp->action = MLX4_ADDR_LIST_NONE;
				found = true;
				break;
			}
		}
		if (!found) {
			new_mc = kmalloc(sizeof(struct mlx4_en_addr_list),
					 GFP_KERNEL);
			if (!new_mc) {
				en_err(priv, "Failed to allocate current multicast list\n");
				return;
			}
			memcpy(new_mc, src_tmp,
			       sizeof(struct mlx4_en_addr_list));
			new_mc->action = MLX4_ADDR_LIST_ADD;
			list_add_tail(&new_mc->list, dst);
		}
	}
}

static void mlx4_en_set_rx_mode(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);

	if (!priv->port_up)
		return;

	queue_work(priv->mdev->workqueue, &priv->rx_mode_task);
}

static void mlx4_en_set_promisc_mode(struct mlx4_en_priv *priv,
				     struct mlx4_en_dev *mdev)
{
	int err = 0;

	if (!(priv->flags & MLX4_EN_FLAG_PROMISC)) {
		priv->flags |= MLX4_EN_FLAG_PROMISC;

		/* Enable promiscouos mode */
		switch (mdev->dev->caps.steering_mode) {
		case MLX4_STEERING_MODE_DEVICE_MANAGED:
			err = mlx4_flow_steer_promisc_add(mdev->dev,
							  priv->port,
							  priv->base_qpn,
							  MLX4_FS_ALL_DEFAULT);
			if (err)
				en_err(priv, "Failed enabling promiscuous mode\n");
			priv->flags |= MLX4_EN_FLAG_MC_PROMISC;
			break;

		case MLX4_STEERING_MODE_B0:
			err = mlx4_unicast_promisc_add(mdev->dev,
						       priv->base_qpn,
						       priv->port);
			if (err)
				en_err(priv, "Failed enabling unicast promiscuous mode\n");

			/* Add the default qp number as multicast
			 * promisc
			 */
			if (!(priv->flags & MLX4_EN_FLAG_MC_PROMISC)) {
				err = mlx4_multicast_promisc_add(mdev->dev,
								 priv->base_qpn,
								 priv->port);
				if (err)
					en_err(priv, "Failed enabling multicast promiscuous mode\n");
				priv->flags |= MLX4_EN_FLAG_MC_PROMISC;
			}
			break;

		case MLX4_STEERING_MODE_A0:
			err = mlx4_SET_PORT_qpn_calc(mdev->dev,
						     priv->port,
						     priv->base_qpn,
						     1);
			if (err)
				en_err(priv, "Failed enabling promiscuous mode\n");
			break;
		}

		/* Disable port multicast filter (unconditionally) */
		err = mlx4_SET_MCAST_FLTR(mdev->dev, priv->port, 0,
					  0, MLX4_MCAST_DISABLE);
		if (err)
			en_err(priv, "Failed disabling multicast filter\n");
	}
}

static void mlx4_en_clear_promisc_mode(struct mlx4_en_priv *priv,
				       struct mlx4_en_dev *mdev)
{
	int err = 0;

	priv->flags &= ~MLX4_EN_FLAG_PROMISC;

	/* Disable promiscouos mode */
	switch (mdev->dev->caps.steering_mode) {
	case MLX4_STEERING_MODE_DEVICE_MANAGED:
		err = mlx4_flow_steer_promisc_remove(mdev->dev,
						     priv->port,
						     MLX4_FS_ALL_DEFAULT);
		if (err)
			en_err(priv, "Failed disabling promiscuous mode\n");
		priv->flags &= ~MLX4_EN_FLAG_MC_PROMISC;
		break;

	case MLX4_STEERING_MODE_B0:
		err = mlx4_unicast_promisc_remove(mdev->dev,
						  priv->base_qpn,
						  priv->port);
		if (err)
			en_err(priv, "Failed disabling unicast promiscuous mode\n");
		/* Disable Multicast promisc */
		if (priv->flags & MLX4_EN_FLAG_MC_PROMISC) {
			err = mlx4_multicast_promisc_remove(mdev->dev,
							    priv->base_qpn,
							    priv->port);
			if (err)
				en_err(priv, "Failed disabling multicast promiscuous mode\n");
			priv->flags &= ~MLX4_EN_FLAG_MC_PROMISC;
		}
		break;

	case MLX4_STEERING_MODE_A0:
		err = mlx4_SET_PORT_qpn_calc(mdev->dev,
					     priv->port,
					     priv->base_qpn, 0);
		if (err)
			en_err(priv, "Failed disabling promiscuous mode\n");
		break;
	}
}

static void mlx4_en_do_multicast(struct mlx4_en_priv *priv,
				 struct net_device *dev,
				 struct mlx4_en_dev *mdev)
{
	struct mlx4_en_addr_list *addr_list, *tmp;
	u8 mc_list[16] = {0};
	int err = 0;
	u64 mcast_addr = 0;


	/* Enable/disable the multicast filter according to IFF_ALLMULTI */
	if (dev->if_flags & IFF_ALLMULTI) {
		err = mlx4_SET_MCAST_FLTR(mdev->dev, priv->port, 0,
					  0, MLX4_MCAST_DISABLE);
		if (err)
			en_err(priv, "Failed disabling multicast filter\n");

		/* Add the default qp number as multicast promisc */
		if (!(priv->flags & MLX4_EN_FLAG_MC_PROMISC)) {
			switch (mdev->dev->caps.steering_mode) {
			case MLX4_STEERING_MODE_DEVICE_MANAGED:
				err = mlx4_flow_steer_promisc_add(mdev->dev,
								  priv->port,
								  priv->base_qpn,
								  MLX4_FS_MC_DEFAULT);
				break;

			case MLX4_STEERING_MODE_B0:
				err = mlx4_multicast_promisc_add(mdev->dev,
								 priv->base_qpn,
								 priv->port);
				break;

			case MLX4_STEERING_MODE_A0:
				break;
			}
			if (err)
				en_err(priv, "Failed entering multicast promisc mode\n");
			priv->flags |= MLX4_EN_FLAG_MC_PROMISC;
		}
	} else {
		/* Disable Multicast promisc */
		if (priv->flags & MLX4_EN_FLAG_MC_PROMISC) {
			switch (mdev->dev->caps.steering_mode) {
			case MLX4_STEERING_MODE_DEVICE_MANAGED:
				err = mlx4_flow_steer_promisc_remove(mdev->dev,
								     priv->port,
								     MLX4_FS_MC_DEFAULT);
				break;

			case MLX4_STEERING_MODE_B0:
				err = mlx4_multicast_promisc_remove(mdev->dev,
								    priv->base_qpn,
								    priv->port);
				break;

			case MLX4_STEERING_MODE_A0:
				break;
			}
			if (err)
				en_err(priv, "Failed disabling multicast promiscuous mode\n");
			priv->flags &= ~MLX4_EN_FLAG_MC_PROMISC;
		}

		err = mlx4_SET_MCAST_FLTR(mdev->dev, priv->port, 0,
					  0, MLX4_MCAST_DISABLE);
		if (err)
			en_err(priv, "Failed disabling multicast filter\n");

		/* Flush mcast filter and init it with broadcast address */
		mlx4_SET_MCAST_FLTR(mdev->dev, priv->port, ETH_BCAST,
				    1, MLX4_MCAST_CONFIG);

		/* Update multicast list - we cache all addresses so they won't
		 * change while HW is updated holding the command semaphor */
		mlx4_en_cache_mclist(dev);
		list_for_each_entry(addr_list, &priv->mc_list, list) {
			mcast_addr = mlx4_mac_to_u64(addr_list->addr);
			mlx4_SET_MCAST_FLTR(mdev->dev, priv->port,
					mcast_addr, 0, MLX4_MCAST_CONFIG);
		}
		err = mlx4_SET_MCAST_FLTR(mdev->dev, priv->port, 0,
					  0, MLX4_MCAST_ENABLE);
		if (err)
			en_err(priv, "Failed enabling multicast filter\n");

		update_addr_list_flags(priv, &priv->curr_mc_list, &priv->mc_list);

		list_for_each_entry_safe(addr_list, tmp, &priv->curr_mc_list, list) {
			if (addr_list->action == MLX4_ADDR_LIST_REM) {
				/* detach this address and delete from list */
				memcpy(&mc_list[10], addr_list->addr, ETH_ALEN);
				mc_list[5] = priv->port;
				err = mlx4_multicast_detach(mdev->dev,
							    &priv->rss_map.indir_qp,
							    mc_list,
							    MLX4_PROT_ETH,
							    addr_list->reg_id);
				if (err)
					en_err(priv, "Fail to detach multicast address\n");

				if (addr_list->tunnel_reg_id) {
					err = mlx4_flow_detach(priv->mdev->dev, addr_list->tunnel_reg_id);
					if (err)
						en_err(priv, "Failed to detach multicast address\n");
				}

				/* remove from list */
				list_del(&addr_list->list);
				kfree(addr_list);
			} else if (addr_list->action == MLX4_ADDR_LIST_ADD) {
				/* attach the address */
				memcpy(&mc_list[10], addr_list->addr, ETH_ALEN);
				/* needed for B0 steering support */
				mc_list[5] = priv->port;
				err = mlx4_multicast_attach(mdev->dev,
							    &priv->rss_map.indir_qp,
							    mc_list,
							    priv->port, 0,
							    MLX4_PROT_ETH,
							    &addr_list->reg_id);
				if (err)
					en_err(priv, "Fail to attach multicast address\n");

				err = mlx4_en_tunnel_steer_add(priv, &mc_list[10], priv->base_qpn,
							       &addr_list->tunnel_reg_id);
				if (err)
					en_err(priv, "Failed to attach multicast address\n");
			}
		}
	}
}

static void mlx4_en_do_unicast(struct mlx4_en_priv *priv,
			       struct net_device *dev,
			       struct mlx4_en_dev *mdev)
{
	struct mlx4_en_addr_list *addr_list, *tmp;
	int err;

	/* Update unicast list */
	mlx4_en_cache_uclist(dev);

	update_addr_list_flags(priv, &priv->curr_uc_list, &priv->uc_list);

	list_for_each_entry_safe(addr_list, tmp, &priv->curr_uc_list, list) {
		if (addr_list->action == MLX4_ADDR_LIST_REM) {
			mlx4_en_uc_steer_release(priv, addr_list->addr,
						 priv->rss_map.indir_qp.qpn,
						 addr_list->reg_id);
			/* remove from list */
			list_del(&addr_list->list);
			kfree(addr_list);
		} else if (addr_list->action == MLX4_ADDR_LIST_ADD) {
			err = mlx4_en_uc_steer_add(priv, addr_list->addr,
						   &priv->rss_map.indir_qp.qpn,
						   &addr_list->reg_id);
			if (err)
				en_err(priv, "Fail to add unicast address\n");
		}
	}
}

static void mlx4_en_do_set_rx_mode(struct work_struct *work)
{
	struct mlx4_en_priv *priv = container_of(work, struct mlx4_en_priv,
						 rx_mode_task);
	struct mlx4_en_dev *mdev = priv->mdev;
	struct net_device *dev = priv->dev;

	mutex_lock(&mdev->state_lock);
	if (!mdev->device_up) {
		en_dbg(HW, priv, "Card is not up, ignoring rx mode change.\n");
		goto out;
	}
	if (!priv->port_up) {
		en_dbg(HW, priv, "Port is down, ignoring rx mode change.\n");
		goto out;
	}
	if (!mlx4_en_QUERY_PORT(mdev, priv->port)) {
		if (priv->port_state.link_state) {
			priv->last_link_state = MLX4_DEV_EVENT_PORT_UP;
			/* update netif baudrate */
			priv->dev->if_baudrate =
			    IF_Mbps(priv->port_state.link_speed);
			/* Important note: the following call for if_link_state_change
			 * is needed for interface up scenario (start port, link state
			 * change) */
			if_link_state_change(priv->dev, LINK_STATE_UP);
			en_dbg(HW, priv, "Link Up\n");
		}
	}

	/* Set unicast rules */
	mlx4_en_do_unicast(priv, dev, mdev);

	/* Promsicuous mode: disable all filters */
	if ((dev->if_flags & IFF_PROMISC) ||
	    (priv->flags & MLX4_EN_FLAG_FORCE_PROMISC)) {
		mlx4_en_set_promisc_mode(priv, mdev);
	} else if (priv->flags & MLX4_EN_FLAG_PROMISC) {
		/* Not in promiscuous mode */
		mlx4_en_clear_promisc_mode(priv, mdev);
	}

	/* Set multicast rules */
	mlx4_en_do_multicast(priv, dev, mdev);
out:
	mutex_unlock(&mdev->state_lock);
}

static void mlx4_en_watchdog_timeout(void *arg)
{
        struct mlx4_en_priv *priv = arg;
        struct mlx4_en_dev *mdev = priv->mdev;

        en_dbg(DRV, priv, "Scheduling watchdog\n");
        queue_work(mdev->workqueue, &priv->watchdog_task);
        if (priv->port_up)
                callout_reset(&priv->watchdog_timer, MLX4_EN_WATCHDOG_TIMEOUT,
                                mlx4_en_watchdog_timeout, priv);
}



static void mlx4_en_set_default_moderation(struct mlx4_en_priv *priv)
{
	struct mlx4_en_cq *cq;
	int i;

	/* If we haven't received a specific coalescing setting
	 * (module param), we set the moderation parameters as follows:
	 * - moder_cnt is set to the number of mtu sized packets to
	 *   satisfy our coalescing target.
	 * - moder_time is set to a fixed value.
	 */
	priv->rx_frames = MLX4_EN_RX_COAL_TARGET;
	priv->rx_usecs = MLX4_EN_RX_COAL_TIME;
	priv->tx_frames = MLX4_EN_TX_COAL_PKTS;
	priv->tx_usecs = MLX4_EN_TX_COAL_TIME;
	en_dbg(INTR, priv, "Default coalesing params for mtu: %u - "
	       "rx_frames:%d rx_usecs:%d\n",
	       (unsigned)priv->dev->if_mtu, priv->rx_frames, priv->rx_usecs);

	/* Setup cq moderation params */
	for (i = 0; i < priv->rx_ring_num; i++) {
		cq = priv->rx_cq[i];
		cq->moder_cnt = priv->rx_frames;
		cq->moder_time = priv->rx_usecs;
		priv->last_moder_time[i] = MLX4_EN_AUTO_CONF;
		priv->last_moder_packets[i] = 0;
		priv->last_moder_bytes[i] = 0;
	}

	for (i = 0; i < priv->tx_ring_num; i++) {
		cq = priv->tx_cq[i];
		cq->moder_cnt = priv->tx_frames;
		cq->moder_time = priv->tx_usecs;
	}

	/* Reset auto-moderation params */
	priv->pkt_rate_low = MLX4_EN_RX_RATE_LOW;
	priv->rx_usecs_low = MLX4_EN_RX_COAL_TIME_LOW;
	priv->pkt_rate_high = MLX4_EN_RX_RATE_HIGH;
	priv->rx_usecs_high = MLX4_EN_RX_COAL_TIME_HIGH;
	priv->sample_interval = MLX4_EN_SAMPLE_INTERVAL;
	priv->adaptive_rx_coal = 1;
	priv->last_moder_jiffies = 0;
	priv->last_moder_tx_packets = 0;
}

static void mlx4_en_auto_moderation(struct mlx4_en_priv *priv)
{
	unsigned long period = (unsigned long) (jiffies - priv->last_moder_jiffies);
	struct mlx4_en_cq *cq;
	unsigned long packets;
	unsigned long rate;
	unsigned long avg_pkt_size;
	unsigned long rx_packets;
	unsigned long rx_bytes;
	unsigned long rx_pkt_diff;
	int moder_time;
	int ring, err;

	if (!priv->adaptive_rx_coal || period < priv->sample_interval * HZ)
		return;

	for (ring = 0; ring < priv->rx_ring_num; ring++) {
                spin_lock(&priv->stats_lock);
		rx_packets = priv->rx_ring[ring]->packets;
		rx_bytes = priv->rx_ring[ring]->bytes;
		spin_unlock(&priv->stats_lock);

		rx_pkt_diff = ((unsigned long) (rx_packets -
				priv->last_moder_packets[ring]));
		packets = rx_pkt_diff;
		rate = packets * HZ / period;
		avg_pkt_size = packets ? ((unsigned long) (rx_bytes -
				priv->last_moder_bytes[ring])) / packets : 0;

		/* Apply auto-moderation only when packet rate
		 * exceeds a rate that it matters */
		if (rate > (MLX4_EN_RX_RATE_THRESH / priv->rx_ring_num) &&
		    avg_pkt_size > MLX4_EN_AVG_PKT_SMALL) {
			if (rate < priv->pkt_rate_low)
				moder_time = priv->rx_usecs_low;
			else if (rate > priv->pkt_rate_high)
				moder_time = priv->rx_usecs_high;
			else
				moder_time = (rate - priv->pkt_rate_low) *
					(priv->rx_usecs_high - priv->rx_usecs_low) /
					(priv->pkt_rate_high - priv->pkt_rate_low) +
					priv->rx_usecs_low;
		} else {
			moder_time = priv->rx_usecs_low;
		}

		if (moder_time != priv->last_moder_time[ring]) {
			priv->last_moder_time[ring] = moder_time;
			cq = priv->rx_cq[ring];
			cq->moder_time = moder_time;
			cq->moder_cnt = priv->rx_frames;
			err = mlx4_en_set_cq_moder(priv, cq);
			if (err)
				en_err(priv, "Failed modifying moderation for cq:%d\n",
				       ring);
		}
		priv->last_moder_packets[ring] = rx_packets;
		priv->last_moder_bytes[ring] = rx_bytes;
	}

	priv->last_moder_jiffies = jiffies;
}

static void mlx4_en_do_get_stats(struct work_struct *work)
{
	struct delayed_work *delay = to_delayed_work(work);
	struct mlx4_en_priv *priv = container_of(delay, struct mlx4_en_priv,
						 stats_task);
	struct mlx4_en_dev *mdev = priv->mdev;
	int err;

	mutex_lock(&mdev->state_lock);
	if (mdev->device_up) {
		if (priv->port_up) {
			if (mlx4_is_slave(mdev->dev))
				err = mlx4_en_get_vport_stats(mdev, priv->port);
			else
				err = mlx4_en_DUMP_ETH_STATS(mdev, priv->port, 0);
			if (err)
				en_dbg(HW, priv, "Could not update stats\n");

			mlx4_en_auto_moderation(priv);
		}

		queue_delayed_work(mdev->workqueue, &priv->stats_task, STATS_DELAY);
	}
	mutex_unlock(&mdev->state_lock);
}

/* mlx4_en_service_task - Run service task for tasks that needed to be done
 * periodically
 */
static void mlx4_en_service_task(struct work_struct *work)
{
	struct delayed_work *delay = to_delayed_work(work);
	struct mlx4_en_priv *priv = container_of(delay, struct mlx4_en_priv,
						 service_task);
	struct mlx4_en_dev *mdev = priv->mdev;

	mutex_lock(&mdev->state_lock);
	if (mdev->device_up) {
		queue_delayed_work(mdev->workqueue, &priv->service_task,
				   SERVICE_TASK_DELAY);
	}
	mutex_unlock(&mdev->state_lock);
}

static void mlx4_en_linkstate(struct work_struct *work)
{
	struct mlx4_en_priv *priv = container_of(work, struct mlx4_en_priv,
						 linkstate_task);
	struct mlx4_en_dev *mdev = priv->mdev;
	int linkstate = priv->link_state;

	mutex_lock(&mdev->state_lock);
	/* If observable port state changed set carrier state and
	 * report to system log */
	if (priv->last_link_state != linkstate) {
		if (linkstate == MLX4_DEV_EVENT_PORT_DOWN) {
			en_info(priv, "Link Down\n");
			if_link_state_change(priv->dev, LINK_STATE_DOWN);
			/* update netif baudrate */
			priv->dev->if_baudrate = 0;

		/* make sure the port is up before notifying the OS.
		 * This is tricky since we get here on INIT_PORT and
		 * in such case we can't tell the OS the port is up.
		 * To solve this there is a call to if_link_state_change
		 * in set_rx_mode.
		 * */
		} else if (priv->port_up && (linkstate == MLX4_DEV_EVENT_PORT_UP)){
			if (mlx4_en_QUERY_PORT(priv->mdev, priv->port))
				en_info(priv, "Query port failed\n");
			priv->dev->if_baudrate =
			    IF_Mbps(priv->port_state.link_speed);
			en_info(priv, "Link Up\n");
			if_link_state_change(priv->dev, LINK_STATE_UP);
		}
	}
	priv->last_link_state = linkstate;
	mutex_unlock(&mdev->state_lock);
}


int mlx4_en_start_port(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_en_cq *cq;
	struct mlx4_en_tx_ring *tx_ring;
	int rx_index = 0;
	int tx_index = 0;
	int err = 0;
	int i;
	int j;
	u8 mc_list[16] = {0};


	if (priv->port_up) {
		en_dbg(DRV, priv, "start port called while port already up\n");
		return 0;
	}

	INIT_LIST_HEAD(&priv->mc_list);
	INIT_LIST_HEAD(&priv->uc_list);
	INIT_LIST_HEAD(&priv->curr_mc_list);
	INIT_LIST_HEAD(&priv->curr_uc_list);
	INIT_LIST_HEAD(&priv->ethtool_list);

	/* Calculate Rx buf size */
	dev->if_mtu = min(dev->if_mtu, priv->max_mtu);
        mlx4_en_calc_rx_buf(dev);
	en_dbg(DRV, priv, "Rx buf size:%d\n", priv->rx_mb_size);

	/* Configure rx cq's and rings */
	err = mlx4_en_activate_rx_rings(priv);
	if (err) {
		en_err(priv, "Failed to activate RX rings\n");
		return err;
	}
	for (i = 0; i < priv->rx_ring_num; i++) {
		cq = priv->rx_cq[i];

		mlx4_en_cq_init_lock(cq);
		err = mlx4_en_activate_cq(priv, cq, i);
		if (err) {
			en_err(priv, "Failed activating Rx CQ\n");
			goto cq_err;
		}
		for (j = 0; j < cq->size; j++)
			cq->buf[j].owner_sr_opcode = MLX4_CQE_OWNER_MASK;
		err = mlx4_en_set_cq_moder(priv, cq);
		if (err) {
			en_err(priv, "Failed setting cq moderation parameters");
			mlx4_en_deactivate_cq(priv, cq);
			goto cq_err;
		}
		mlx4_en_arm_cq(priv, cq);
		priv->rx_ring[i]->cqn = cq->mcq.cqn;
		++rx_index;
	}

	/* Set qp number */
	en_dbg(DRV, priv, "Getting qp number for port %d\n", priv->port);
	err = mlx4_en_get_qp(priv);
	if (err) {
		en_err(priv, "Failed getting eth qp\n");
		goto cq_err;
	}
	mdev->mac_removed[priv->port] = 0;

	priv->counter_index =
			mlx4_get_default_counter_index(mdev->dev, priv->port);

	err = mlx4_en_config_rss_steer(priv);
	if (err) {
		en_err(priv, "Failed configuring rss steering\n");
		goto mac_err;
	}

	err = mlx4_en_create_drop_qp(priv);
	if (err)
		goto rss_err;

	/* Configure tx cq's and rings */
	for (i = 0; i < priv->tx_ring_num; i++) {
		/* Configure cq */
		cq = priv->tx_cq[i];
		err = mlx4_en_activate_cq(priv, cq, i);
		if (err) {
			en_err(priv, "Failed activating Tx CQ\n");
			goto tx_err;
		}
		err = mlx4_en_set_cq_moder(priv, cq);
		if (err) {
			en_err(priv, "Failed setting cq moderation parameters");
			mlx4_en_deactivate_cq(priv, cq);
			goto tx_err;
		}
		en_dbg(DRV, priv, "Resetting index of collapsed CQ:%d to -1\n", i);
		cq->buf->wqe_index = cpu_to_be16(0xffff);

		/* Configure ring */
		tx_ring = priv->tx_ring[i];

		err = mlx4_en_activate_tx_ring(priv, tx_ring, cq->mcq.cqn,
					       i / priv->num_tx_rings_p_up);
		if (err) {
			en_err(priv, "Failed activating Tx ring %d\n", i);
			mlx4_en_deactivate_cq(priv, cq);
			goto tx_err;
		}

		/* Arm CQ for TX completions */
		mlx4_en_arm_cq(priv, cq);

		/* Set initial ownership of all Tx TXBBs to SW (1) */
		for (j = 0; j < tx_ring->buf_size; j += STAMP_STRIDE)
			*((u32 *) (tx_ring->buf + j)) = INIT_OWNER_BIT;
		++tx_index;
	}

	/* Configure port */
	err = mlx4_SET_PORT_general(mdev->dev, priv->port,
				    priv->rx_mb_size,
				    priv->prof->tx_pause,
				    priv->prof->tx_ppp,
				    priv->prof->rx_pause,
				    priv->prof->rx_ppp);
	if (err) {
		en_err(priv, "Failed setting port general configurations for port %d, with error %d\n",
		       priv->port, err);
		goto tx_err;
	}
	/* Set default qp number */
	err = mlx4_SET_PORT_qpn_calc(mdev->dev, priv->port, priv->base_qpn, 0);
	if (err) {
		en_err(priv, "Failed setting default qp numbers\n");
		goto tx_err;
	}

	/* Init port */
	en_dbg(HW, priv, "Initializing port\n");
	err = mlx4_INIT_PORT(mdev->dev, priv->port);
	if (err) {
		en_err(priv, "Failed Initializing port\n");
		goto tx_err;
	}

	/* Attach rx QP to bradcast address */
	memset(&mc_list[10], 0xff, ETH_ALEN);
	mc_list[5] = priv->port; /* needed for B0 steering support */
	if (mlx4_multicast_attach(mdev->dev, &priv->rss_map.indir_qp, mc_list,
				  priv->port, 0, MLX4_PROT_ETH,
				  &priv->broadcast_id))
		mlx4_warn(mdev, "Failed Attaching Broadcast\n");

	/* Must redo promiscuous mode setup. */
	priv->flags &= ~(MLX4_EN_FLAG_PROMISC | MLX4_EN_FLAG_MC_PROMISC);

	/* Schedule multicast task to populate multicast list */
	queue_work(mdev->workqueue, &priv->rx_mode_task);

	priv->port_up = true;

        /* Enable the queues. */
        dev->if_drv_flags &= ~IFF_DRV_OACTIVE;
        dev->if_drv_flags |= IFF_DRV_RUNNING;
#ifdef CONFIG_DEBUG_FS
	mlx4_en_create_debug_files(priv);
#endif
        callout_reset(&priv->watchdog_timer, MLX4_EN_WATCHDOG_TIMEOUT,
                    mlx4_en_watchdog_timeout, priv);


	return 0;

tx_err:
	while (tx_index--) {
		mlx4_en_deactivate_tx_ring(priv, priv->tx_ring[tx_index]);
		mlx4_en_deactivate_cq(priv, priv->tx_cq[tx_index]);
	}
	mlx4_en_destroy_drop_qp(priv);
rss_err:
	mlx4_en_release_rss_steer(priv);
mac_err:
	mlx4_en_put_qp(priv);
cq_err:
	while (rx_index--)
		mlx4_en_deactivate_cq(priv, priv->rx_cq[rx_index]);
	for (i = 0; i < priv->rx_ring_num; i++)
		mlx4_en_deactivate_rx_ring(priv, priv->rx_ring[i]);

	return err; /* need to close devices */
}


void mlx4_en_stop_port(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	struct mlx4_en_addr_list *addr_list, *tmp;
	int i;
	u8 mc_list[16] = {0};

	if (!priv->port_up) {
		en_dbg(DRV, priv, "stop port called while port already down\n");
		return;
	}

#ifdef CONFIG_DEBUG_FS
	mlx4_en_delete_debug_files(priv);
#endif

	/* close port*/
	mlx4_CLOSE_PORT(mdev->dev, priv->port);

	/* Set port as not active */
	priv->port_up = false;
	priv->counter_index = MLX4_SINK_COUNTER_INDEX(mdev->dev);

	/* Promsicuous mode */
	if (mdev->dev->caps.steering_mode ==
	    MLX4_STEERING_MODE_DEVICE_MANAGED) {
		priv->flags &= ~(MLX4_EN_FLAG_PROMISC |
				 MLX4_EN_FLAG_MC_PROMISC);
		mlx4_flow_steer_promisc_remove(mdev->dev,
					       priv->port,
					       MLX4_FS_ALL_DEFAULT);
		mlx4_flow_steer_promisc_remove(mdev->dev,
					       priv->port,
					       MLX4_FS_MC_DEFAULT);
	} else if (priv->flags & MLX4_EN_FLAG_PROMISC) {
		priv->flags &= ~MLX4_EN_FLAG_PROMISC;

		/* Disable promiscouos mode */
		mlx4_unicast_promisc_remove(mdev->dev, priv->base_qpn,
					    priv->port);

		/* Disable Multicast promisc */
		if (priv->flags & MLX4_EN_FLAG_MC_PROMISC) {
			mlx4_multicast_promisc_remove(mdev->dev, priv->base_qpn,
						      priv->port);
			priv->flags &= ~MLX4_EN_FLAG_MC_PROMISC;
		}
	}

	/* Detach All unicasts */
	list_for_each_entry(addr_list, &priv->curr_uc_list, list) {
		mlx4_en_uc_steer_release(priv, addr_list->addr,
					 priv->rss_map.indir_qp.qpn,
					 addr_list->reg_id);
	}
	mlx4_en_clear_uclist(dev);
	list_for_each_entry_safe(addr_list, tmp, &priv->curr_uc_list, list) {
		list_del(&addr_list->list);
		kfree(addr_list);
	}

	/* Detach All multicasts */
	memset(&mc_list[10], 0xff, ETH_ALEN);
	mc_list[5] = priv->port; /* needed for B0 steering support */
	mlx4_multicast_detach(mdev->dev, &priv->rss_map.indir_qp, mc_list,
			      MLX4_PROT_ETH, priv->broadcast_id);
	list_for_each_entry(addr_list, &priv->curr_mc_list, list) {
		memcpy(&mc_list[10], addr_list->addr, ETH_ALEN);
		mc_list[5] = priv->port;
		mlx4_multicast_detach(mdev->dev, &priv->rss_map.indir_qp,
				      mc_list, MLX4_PROT_ETH, addr_list->reg_id);
	}
	mlx4_en_clear_mclist(dev);
	list_for_each_entry_safe(addr_list, tmp, &priv->curr_mc_list, list) {
		list_del(&addr_list->list);
		kfree(addr_list);
	}

	/* Flush multicast filter */
	mlx4_SET_MCAST_FLTR(mdev->dev, priv->port, 0, 1, MLX4_MCAST_CONFIG);
	mlx4_en_destroy_drop_qp(priv);

	/* Free TX Rings */
	for (i = 0; i < priv->tx_ring_num; i++) {
		mlx4_en_deactivate_tx_ring(priv, priv->tx_ring[i]);
		mlx4_en_deactivate_cq(priv, priv->tx_cq[i]);
	}
	msleep(10);

	for (i = 0; i < priv->tx_ring_num; i++)
		mlx4_en_free_tx_buf(dev, priv->tx_ring[i]);

	/* Free RSS qps */
	mlx4_en_release_rss_steer(priv);

	/* Unregister Mac address for the port */
	mlx4_en_put_qp(priv);
	mdev->mac_removed[priv->port] = 1;

	/* Free RX Rings */
	for (i = 0; i < priv->rx_ring_num; i++) {
		struct mlx4_en_cq *cq = priv->rx_cq[i];
		mlx4_en_deactivate_rx_ring(priv, priv->rx_ring[i]);
		mlx4_en_deactivate_cq(priv, cq);
	}

        callout_stop(&priv->watchdog_timer);

        dev->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
}

static void mlx4_en_restart(struct work_struct *work)
{
	struct mlx4_en_priv *priv = container_of(work, struct mlx4_en_priv,
						 watchdog_task);
	struct mlx4_en_dev *mdev = priv->mdev;
	struct net_device *dev = priv->dev;
	struct mlx4_en_tx_ring *ring;
	int i;


	if (priv->blocked == 0 || priv->port_up == 0)
		return;
	for (i = 0; i < priv->tx_ring_num; i++) {
		int watchdog_time;

		ring = priv->tx_ring[i];
		watchdog_time = READ_ONCE(ring->watchdog_time);
		if (watchdog_time != 0 &&
		    time_after(ticks, ring->watchdog_time))
			goto reset;
	}
	return;

reset:
	priv->port_stats.tx_timeout++;
	en_dbg(DRV, priv, "Watchdog task called for port %d\n", priv->port);

	mutex_lock(&mdev->state_lock);
	if (priv->port_up) {
		mlx4_en_stop_port(dev);
                //for (i = 0; i < priv->tx_ring_num; i++)         
                //        netdev_tx_reset_queue(priv->tx_ring[i]->tx_queue);
		if (mlx4_en_start_port(dev))
			en_err(priv, "Failed restarting port %d\n", priv->port);
	}
	mutex_unlock(&mdev->state_lock);
}

static void mlx4_en_clear_stats(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	int i;

	if (!mlx4_is_slave(mdev->dev))
		if (mlx4_en_DUMP_ETH_STATS(mdev, priv->port, 1))
			en_dbg(HW, priv, "Failed dumping statistics\n");

	memset(&priv->pstats, 0, sizeof(priv->pstats));
	memset(&priv->pkstats, 0, sizeof(priv->pkstats));
	memset(&priv->port_stats, 0, sizeof(priv->port_stats));
	memset(&priv->vport_stats, 0, sizeof(priv->vport_stats));

	for (i = 0; i < priv->tx_ring_num; i++) {
		priv->tx_ring[i]->bytes = 0;
		priv->tx_ring[i]->packets = 0;
		priv->tx_ring[i]->tx_csum = 0;
		priv->tx_ring[i]->oversized_packets = 0;
	}
	for (i = 0; i < priv->rx_ring_num; i++) {
		priv->rx_ring[i]->bytes = 0;
		priv->rx_ring[i]->packets = 0;
		priv->rx_ring[i]->csum_ok = 0;
		priv->rx_ring[i]->csum_none = 0;
	}
}

static void mlx4_en_open(void* arg)
{

        struct mlx4_en_priv *priv;
        struct mlx4_en_dev *mdev;
        struct net_device *dev;
        int err = 0;

        priv = arg;
        mdev = priv->mdev;
        dev = priv->dev;


	mutex_lock(&mdev->state_lock);

	if (!mdev->device_up) {
		en_err(priv, "Cannot open - device down/disabled\n");
		goto out;
	}

	/* Reset HW statistics and SW counters */
	mlx4_en_clear_stats(dev);

	err = mlx4_en_start_port(dev);
	if (err)
		en_err(priv, "Failed starting port:%d\n", priv->port);

out:
	mutex_unlock(&mdev->state_lock);
	return;
}

void mlx4_en_free_resources(struct mlx4_en_priv *priv)
{
	int i;

#ifdef CONFIG_RFS_ACCEL
	if (priv->dev->rx_cpu_rmap) {
		free_irq_cpu_rmap(priv->dev->rx_cpu_rmap);
		priv->dev->rx_cpu_rmap = NULL;
	}
#endif

	for (i = 0; i < priv->tx_ring_num; i++) {
		if (priv->tx_ring && priv->tx_ring[i])
			mlx4_en_destroy_tx_ring(priv, &priv->tx_ring[i]);
		if (priv->tx_cq && priv->tx_cq[i])
			mlx4_en_destroy_cq(priv, &priv->tx_cq[i]);
	}

	for (i = 0; i < priv->rx_ring_num; i++) {
		if (priv->rx_ring[i])
			mlx4_en_destroy_rx_ring(priv, &priv->rx_ring[i],
				priv->prof->rx_ring_size);
		if (priv->rx_cq[i])
			mlx4_en_destroy_cq(priv, &priv->rx_cq[i]);
	}

	if (priv->stat_sysctl != NULL)
		sysctl_ctx_free(&priv->stat_ctx);
}

int mlx4_en_alloc_resources(struct mlx4_en_priv *priv)
{
	struct mlx4_en_port_profile *prof = priv->prof;
	int i;
	int node = 0;

	/* Create rx Rings */
	for (i = 0; i < priv->rx_ring_num; i++) {
		if (mlx4_en_create_cq(priv, &priv->rx_cq[i],
				      prof->rx_ring_size, i, RX, node))
			goto err;

		if (mlx4_en_create_rx_ring(priv, &priv->rx_ring[i],
					   prof->rx_ring_size, node))
			goto err;
	}

	/* Create tx Rings */
	for (i = 0; i < priv->tx_ring_num; i++) {
		if (mlx4_en_create_cq(priv, &priv->tx_cq[i],
				      prof->tx_ring_size, i, TX, node))
			goto err;

		if (mlx4_en_create_tx_ring(priv, &priv->tx_ring[i],
					   prof->tx_ring_size, TXBB_SIZE, node, i))
			goto err;
	}

#ifdef CONFIG_RFS_ACCEL
	priv->dev->rx_cpu_rmap = alloc_irq_cpu_rmap(priv->rx_ring_num);
	if (!priv->dev->rx_cpu_rmap)
		goto err;
#endif
        /* Re-create stat sysctls in case the number of rings changed. */
	mlx4_en_sysctl_stat(priv);
	return 0;

err:
	en_err(priv, "Failed to allocate NIC resources\n");
	for (i = 0; i < priv->rx_ring_num; i++) {
		if (priv->rx_ring[i])
			mlx4_en_destroy_rx_ring(priv, &priv->rx_ring[i],
						prof->rx_ring_size);
		if (priv->rx_cq[i])
			mlx4_en_destroy_cq(priv, &priv->rx_cq[i]);
	}
	for (i = 0; i < priv->tx_ring_num; i++) {
		if (priv->tx_ring[i])
			mlx4_en_destroy_tx_ring(priv, &priv->tx_ring[i]);
		if (priv->tx_cq[i])
			mlx4_en_destroy_cq(priv, &priv->tx_cq[i]);
	}
	priv->port_up = false;
	return -ENOMEM;
}

struct en_port_attribute {
	struct attribute attr;
	ssize_t (*show)(struct en_port *, struct en_port_attribute *, char *buf);
	ssize_t (*store)(struct en_port *, struct en_port_attribute *, char *buf, size_t count);
};

#define PORT_ATTR_RO(_name) \
struct en_port_attribute en_port_attr_##_name = __ATTR_RO(_name)

#define EN_PORT_ATTR(_name, _mode, _show, _store) \
struct en_port_attribute en_port_attr_##_name = __ATTR(_name, _mode, _show, _store)

void mlx4_en_destroy_netdev(struct net_device *dev)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;

	en_dbg(DRV, priv, "Destroying netdev on port:%d\n", priv->port);

	/* don't allow more IOCTLs */
	priv->gone = 1;

	/* XXX wait a bit to allow IOCTL handlers to complete */
	pause("W", hz);

	if (priv->vlan_attach != NULL)
		EVENTHANDLER_DEREGISTER(vlan_config, priv->vlan_attach);
	if (priv->vlan_detach != NULL)
		EVENTHANDLER_DEREGISTER(vlan_unconfig, priv->vlan_detach);

	mutex_lock(&mdev->state_lock);
	mlx4_en_stop_port(dev);
	mutex_unlock(&mdev->state_lock);

	/* Unregister device - this will close the port if it was up */
	if (priv->registered)
		ether_ifdetach(dev);

	if (priv->allocated)
		mlx4_free_hwq_res(mdev->dev, &priv->res, MLX4_EN_PAGE_SIZE);

	cancel_delayed_work(&priv->stats_task);
	cancel_delayed_work(&priv->service_task);
	/* flush any pending task for this netdev */
	flush_workqueue(mdev->workqueue);
        callout_drain(&priv->watchdog_timer);

	/* Detach the netdev so tasks would not attempt to access it */
	mutex_lock(&mdev->state_lock);
	mdev->pndev[priv->port] = NULL;
	mutex_unlock(&mdev->state_lock);


	mlx4_en_free_resources(priv);

	/* freeing the sysctl conf cannot be called from within mlx4_en_free_resources */
	if (priv->conf_sysctl != NULL)
		sysctl_ctx_free(&priv->conf_ctx);

	kfree(priv->tx_ring);
	kfree(priv->tx_cq);

        kfree(priv);
        if_free(dev);

}

static int mlx4_en_change_mtu(struct net_device *dev, int new_mtu)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	int err = 0;

	en_dbg(DRV, priv, "Change MTU called - current:%u new:%u\n",
	       (unsigned)dev->if_mtu, (unsigned)new_mtu);

	if ((new_mtu < MLX4_EN_MIN_MTU) || (new_mtu > priv->max_mtu)) {
		en_err(priv, "Bad MTU size:%d, max %u.\n", new_mtu,
		    priv->max_mtu);
		return -EPERM;
	}
	mutex_lock(&mdev->state_lock);
	dev->if_mtu = new_mtu;
	if (dev->if_drv_flags & IFF_DRV_RUNNING) {
		if (!mdev->device_up) {
			/* NIC is probably restarting - let watchdog task reset
			 *                          * the port */
			en_dbg(DRV, priv, "Change MTU called with card down!?\n");
		} else {
			mlx4_en_stop_port(dev);
			err = mlx4_en_start_port(dev);
			if (err) {
				en_err(priv, "Failed restarting port:%d\n",
						priv->port);
				queue_work(mdev->workqueue, &priv->watchdog_task);
			}
		}
	}
	mutex_unlock(&mdev->state_lock);
	return 0;
}

static int mlx4_en_calc_media(struct mlx4_en_priv *priv)
{
	int trans_type;
	int active;

	active = IFM_ETHER;
	if (priv->last_link_state == MLX4_DEV_EVENT_PORT_DOWN)
		return (active);
	active |= IFM_FDX;
	trans_type = priv->port_state.transceiver;
	/* XXX I don't know all of the transceiver values. */
	switch (priv->port_state.link_speed) {
	case 100:
		active |= IFM_100_T;
		break;
	case 1000:
		active |= IFM_1000_T;
		break;
	case 10000:
		if (trans_type > 0 && trans_type <= 0xC)
			active |= IFM_10G_SR;
		else if (trans_type == 0x80 || trans_type == 0)
			active |= IFM_10G_CX4;
		break;
	case 40000:
		active |= IFM_40G_CR4;
		break;
	}
	if (priv->prof->tx_pause)
		active |= IFM_ETH_TXPAUSE;
	if (priv->prof->rx_pause)
		active |= IFM_ETH_RXPAUSE;

	return (active);
}

static void mlx4_en_media_status(struct ifnet *dev, struct ifmediareq *ifmr)
{
	struct mlx4_en_priv *priv;

	priv = dev->if_softc;
	ifmr->ifm_status = IFM_AVALID;
	if (priv->last_link_state != MLX4_DEV_EVENT_PORT_DOWN)
		ifmr->ifm_status |= IFM_ACTIVE;
	ifmr->ifm_active = mlx4_en_calc_media(priv);

	return;
}

static int mlx4_en_media_change(struct ifnet *dev)
{
	struct mlx4_en_priv *priv;
        struct ifmedia *ifm;
	int rxpause;
	int txpause;
	int error;

	priv = dev->if_softc;
	ifm = &priv->media;
	rxpause = txpause = 0;
	error = 0;

	if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER)
		return (EINVAL);
        switch (IFM_SUBTYPE(ifm->ifm_media)) {
        case IFM_AUTO:
		break;
	case IFM_10G_SR:
	case IFM_10G_CX4:
	case IFM_1000_T:
	case IFM_40G_CR4:
		if ((IFM_SUBTYPE(ifm->ifm_media)
			== IFM_SUBTYPE(mlx4_en_calc_media(priv)))
			&& (ifm->ifm_media & IFM_FDX))
			break;
		/* Fallthrough */
	default:
                printf("%s: Only auto media type\n", if_name(dev));
                return (EINVAL);
	}
	/* Allow user to set/clear pause */
	if (IFM_OPTIONS(ifm->ifm_media) & IFM_ETH_RXPAUSE)
		rxpause = 1;
	if (IFM_OPTIONS(ifm->ifm_media) & IFM_ETH_TXPAUSE)
		txpause = 1;
	if (priv->prof->tx_pause != txpause || priv->prof->rx_pause != rxpause) {
		priv->prof->tx_pause = txpause;
		priv->prof->rx_pause = rxpause;
		error = -mlx4_SET_PORT_general(priv->mdev->dev, priv->port,
		     priv->rx_mb_size + ETHER_CRC_LEN, priv->prof->tx_pause,
		     priv->prof->tx_ppp, priv->prof->rx_pause,
		     priv->prof->rx_ppp);
	}
	return (error);
}

static int mlx4_en_ioctl(struct ifnet *dev, u_long command, caddr_t data)
{
	struct mlx4_en_priv *priv;
	struct mlx4_en_dev *mdev;
	struct ifreq *ifr;
	int error;
	int mask;
	struct ifrsskey *ifrk;
	const u32 *key;
	struct ifrsshash *ifrh;
	u8 rss_mask;

	error = 0;
	mask = 0;
	priv = dev->if_softc;

	/* check if detaching */
	if (priv == NULL || priv->gone != 0)
		return (ENXIO);

	mdev = priv->mdev;
	ifr = (struct ifreq *) data;

	switch (command) {
	case SIOCSIFMTU:
		error = -mlx4_en_change_mtu(dev, ifr->ifr_mtu);
		break;
	case SIOCSIFFLAGS:
		if (dev->if_flags & IFF_UP) {
			if ((dev->if_drv_flags & IFF_DRV_RUNNING) == 0) {
				mutex_lock(&mdev->state_lock);
				mlx4_en_start_port(dev);
				mutex_unlock(&mdev->state_lock);
			} else {
				mlx4_en_set_rx_mode(dev);
			}
		} else {
			mutex_lock(&mdev->state_lock);
			if (dev->if_drv_flags & IFF_DRV_RUNNING) {
				mlx4_en_stop_port(dev);
				if_link_state_change(dev, LINK_STATE_DOWN);
			}
			mutex_unlock(&mdev->state_lock);
		}
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		mlx4_en_set_rx_mode(dev);
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(dev, ifr, &priv->media, command);
		break;
	case SIOCSIFCAP:
		mutex_lock(&mdev->state_lock);
		mask = ifr->ifr_reqcap ^ dev->if_capenable;
		if (mask & IFCAP_TXCSUM) {
			dev->if_capenable ^= IFCAP_TXCSUM;
			dev->if_hwassist ^= (CSUM_TCP | CSUM_UDP | CSUM_IP);

			if (IFCAP_TSO4 & dev->if_capenable &&
			    !(IFCAP_TXCSUM & dev->if_capenable)) {
				dev->if_capenable &= ~IFCAP_TSO4;
				dev->if_hwassist &= ~CSUM_IP_TSO;
				if_printf(dev,
				    "tso4 disabled due to -txcsum.\n");
			}
		}
		if (mask & IFCAP_TXCSUM_IPV6) {
			dev->if_capenable ^= IFCAP_TXCSUM_IPV6;
			dev->if_hwassist ^= (CSUM_UDP_IPV6 | CSUM_TCP_IPV6);

			if (IFCAP_TSO6 & dev->if_capenable &&
			    !(IFCAP_TXCSUM_IPV6 & dev->if_capenable)) {
				dev->if_capenable &= ~IFCAP_TSO6;
				dev->if_hwassist &= ~CSUM_IP6_TSO;
				if_printf(dev,
				    "tso6 disabled due to -txcsum6.\n");
			}
		}
		if (mask & IFCAP_RXCSUM)
			dev->if_capenable ^= IFCAP_RXCSUM;
		if (mask & IFCAP_RXCSUM_IPV6)
			dev->if_capenable ^= IFCAP_RXCSUM_IPV6;

		if (mask & IFCAP_TSO4) {
			if (!(IFCAP_TSO4 & dev->if_capenable) &&
			    !(IFCAP_TXCSUM & dev->if_capenable)) {
				if_printf(dev, "enable txcsum first.\n");
				error = EAGAIN;
				goto out;
			}
			dev->if_capenable ^= IFCAP_TSO4;
			dev->if_hwassist ^= CSUM_IP_TSO;
		}
		if (mask & IFCAP_TSO6) {
			if (!(IFCAP_TSO6 & dev->if_capenable) &&
			    !(IFCAP_TXCSUM_IPV6 & dev->if_capenable)) {
				if_printf(dev, "enable txcsum6 first.\n");
				error = EAGAIN;
				goto out;
			}
			dev->if_capenable ^= IFCAP_TSO6;
			dev->if_hwassist ^= CSUM_IP6_TSO;
		}
		if (mask & IFCAP_LRO)
			dev->if_capenable ^= IFCAP_LRO;
		if (mask & IFCAP_VLAN_HWTAGGING)
			dev->if_capenable ^= IFCAP_VLAN_HWTAGGING;
		if (mask & IFCAP_VLAN_HWFILTER)
			dev->if_capenable ^= IFCAP_VLAN_HWFILTER;
		if (mask & IFCAP_WOL_MAGIC)
			dev->if_capenable ^= IFCAP_WOL_MAGIC;
		if (dev->if_drv_flags & IFF_DRV_RUNNING)
			mlx4_en_start_port(dev);
out:
		mutex_unlock(&mdev->state_lock);
		VLAN_CAPABILITIES(dev);
		break;
#if __FreeBSD_version >= 1100036
	case SIOCGI2C: {
		struct ifi2creq i2c;

		error = copyin(ifr_data_get_ptr(ifr), &i2c, sizeof(i2c));
		if (error)
			break;
		if (i2c.len > sizeof(i2c.data)) {
			error = EINVAL;
			break;
		}
		/*
		 * Note that we ignore i2c.addr here. The driver hardcodes
		 * the address to 0x50, while standard expects it to be 0xA0.
		 */
		error = mlx4_get_module_info(mdev->dev, priv->port,
		    i2c.offset, i2c.len, i2c.data);
		if (error < 0) {
			error = -error;
			break;
		}
		error = copyout(&i2c, ifr_data_get_ptr(ifr), sizeof(i2c));
		break;
	}
#endif
	case SIOCGIFRSSKEY:
		ifrk = (struct ifrsskey *)data;
		ifrk->ifrk_func = RSS_FUNC_TOEPLITZ;
		mutex_lock(&mdev->state_lock);
		key = mlx4_en_get_rss_key(priv, &ifrk->ifrk_keylen);
		if (ifrk->ifrk_keylen > RSS_KEYLEN)
			error = EINVAL;
		else
			memcpy(ifrk->ifrk_key, key, ifrk->ifrk_keylen);
		mutex_unlock(&mdev->state_lock);
		break;

	case SIOCGIFRSSHASH:
		mutex_lock(&mdev->state_lock);
		rss_mask = mlx4_en_get_rss_mask(priv);
		mutex_unlock(&mdev->state_lock);
		ifrh = (struct ifrsshash *)data;
		ifrh->ifrh_func = RSS_FUNC_TOEPLITZ;
		ifrh->ifrh_types = 0;
		if (rss_mask & MLX4_RSS_IPV4)
			ifrh->ifrh_types |= RSS_TYPE_IPV4;
		if (rss_mask & MLX4_RSS_TCP_IPV4)
			ifrh->ifrh_types |= RSS_TYPE_TCP_IPV4;
		if (rss_mask & MLX4_RSS_IPV6)
			ifrh->ifrh_types |= RSS_TYPE_IPV6;
		if (rss_mask & MLX4_RSS_TCP_IPV6)
			ifrh->ifrh_types |= RSS_TYPE_TCP_IPV6;
		if (rss_mask & MLX4_RSS_UDP_IPV4)
			ifrh->ifrh_types |= RSS_TYPE_UDP_IPV4;
		if (rss_mask & MLX4_RSS_UDP_IPV6)
			ifrh->ifrh_types |= RSS_TYPE_UDP_IPV6;
		break;

	default:
		error = ether_ioctl(dev, command, data);
		break;
	}

	return (error);
}


int mlx4_en_init_netdev(struct mlx4_en_dev *mdev, int port,
			struct mlx4_en_port_profile *prof)
{
	struct net_device *dev;
	struct mlx4_en_priv *priv;
	uint8_t dev_addr[ETHER_ADDR_LEN];
	int err;
	int i;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	dev = priv->dev = if_alloc(IFT_ETHER);
	if (dev == NULL) {
		en_err(priv, "Net device allocation failed\n");
		kfree(priv);
		return -ENOMEM;
	}
	dev->if_softc = priv;
	if_initname(dev, "mlxen", (device_get_unit(
	    mdev->pdev->dev.bsddev) * MLX4_MAX_PORTS) + port - 1);
	dev->if_mtu = ETHERMTU;
	dev->if_init = mlx4_en_open;
	dev->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	dev->if_ioctl = mlx4_en_ioctl;
	dev->if_transmit = mlx4_en_transmit;
	dev->if_qflush = mlx4_en_qflush;
	dev->if_snd.ifq_maxlen = prof->tx_ring_size;

	/*
	 * Initialize driver private data
	 */
	priv->counter_index = 0xff;
	spin_lock_init(&priv->stats_lock);
	INIT_WORK(&priv->rx_mode_task, mlx4_en_do_set_rx_mode);
	INIT_WORK(&priv->watchdog_task, mlx4_en_restart);
	INIT_WORK(&priv->linkstate_task, mlx4_en_linkstate);
	INIT_DELAYED_WORK(&priv->stats_task, mlx4_en_do_get_stats);
	INIT_DELAYED_WORK(&priv->service_task, mlx4_en_service_task);
	callout_init(&priv->watchdog_timer, 1);
#ifdef CONFIG_RFS_ACCEL
	INIT_LIST_HEAD(&priv->filters);
	spin_lock_init(&priv->filters_lock);
#endif

	priv->msg_enable = MLX4_EN_MSG_LEVEL;
	priv->dev = dev;
	priv->mdev = mdev;
	priv->ddev = &mdev->pdev->dev;
	priv->prof = prof;
	priv->port = port;
	priv->port_up = false;
	priv->flags = prof->flags;

	priv->num_tx_rings_p_up = mdev->profile.num_tx_rings_p_up;
	priv->tx_ring_num = prof->tx_ring_num;
	priv->tx_ring = kcalloc(MAX_TX_RINGS,
				sizeof(struct mlx4_en_tx_ring *), GFP_KERNEL);
	if (!priv->tx_ring) {
		err = -ENOMEM;
		goto out;
	}
	priv->tx_cq = kcalloc(sizeof(struct mlx4_en_cq *), MAX_TX_RINGS,
			GFP_KERNEL);
	if (!priv->tx_cq) {
		err = -ENOMEM;
		goto out;
	}

	priv->rx_ring_num = prof->rx_ring_num;
	priv->cqe_factor = (mdev->dev->caps.cqe_size == 64) ? 1 : 0;
	priv->mac_index = -1;
	priv->last_ifq_jiffies = 0;
	priv->if_counters_rx_errors = 0;
	priv->if_counters_rx_no_buffer = 0;
#ifdef CONFIG_MLX4_EN_DCB
	if (!mlx4_is_slave(priv->mdev->dev)) {
		priv->dcbx_cap = DCB_CAP_DCBX_HOST;
		priv->flags |= MLX4_EN_FLAG_DCB_ENABLED;
		if (mdev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_ETS_CFG) {
			dev->dcbnl_ops = &mlx4_en_dcbnl_ops;
		} else {
			en_info(priv, "QoS disabled - no HW support\n");
			dev->dcbnl_ops = &mlx4_en_dcbnl_pfc_ops;
		}
	}
#endif

	/* Query for default mac and max mtu */
	priv->max_mtu = mdev->dev->caps.eth_mtu_cap[priv->port];
        priv->mac = mdev->dev->caps.def_mac[priv->port];
        if (ILLEGAL_MAC(priv->mac)) {
#if BITS_PER_LONG == 64
                en_err(priv, "Port: %d, invalid mac burned: 0x%lx, quiting\n",
                                priv->port, priv->mac);
#elif BITS_PER_LONG == 32
                en_err(priv, "Port: %d, invalid mac burned: 0x%llx, quiting\n",
                                priv->port, priv->mac);
#endif
                err = -EINVAL;
                goto out;
        }

	mlx4_en_sysctl_conf(priv);

	err = mlx4_en_alloc_resources(priv);
	if (err)
		goto out;

	/* Allocate page for receive rings */
	err = mlx4_alloc_hwq_res(mdev->dev, &priv->res,
				MLX4_EN_PAGE_SIZE, MLX4_EN_PAGE_SIZE);
	if (err) {
		en_err(priv, "Failed to allocate page for rx qps\n");
		goto out;
	}
	priv->allocated = 1;

	/*
	 * Set driver features
	 */
	dev->if_capabilities |= IFCAP_HWCSUM | IFCAP_HWCSUM_IPV6;
	dev->if_capabilities |= IFCAP_VLAN_MTU | IFCAP_VLAN_HWTAGGING;
	dev->if_capabilities |= IFCAP_VLAN_HWCSUM | IFCAP_VLAN_HWFILTER;
	dev->if_capabilities |= IFCAP_LINKSTATE | IFCAP_JUMBO_MTU;
	dev->if_capabilities |= IFCAP_LRO;
	dev->if_capabilities |= IFCAP_HWSTATS;

	if (mdev->LSO_support)
		dev->if_capabilities |= IFCAP_TSO4 | IFCAP_TSO6 | IFCAP_VLAN_HWTSO;

#if __FreeBSD_version >= 1100000
	/* set TSO limits so that we don't have to drop TX packets */
	dev->if_hw_tsomax = MLX4_EN_TX_MAX_PAYLOAD_SIZE - (ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN) /* hdr */;
	dev->if_hw_tsomaxsegcount = MLX4_EN_TX_MAX_MBUF_FRAGS - 1 /* hdr */;
	dev->if_hw_tsomaxsegsize = MLX4_EN_TX_MAX_MBUF_SIZE;
#endif

	dev->if_capenable = dev->if_capabilities;

	dev->if_hwassist = 0;
	if (dev->if_capenable & (IFCAP_TSO4 | IFCAP_TSO6))
		dev->if_hwassist |= CSUM_TSO;
	if (dev->if_capenable & IFCAP_TXCSUM)
		dev->if_hwassist |= (CSUM_TCP | CSUM_UDP | CSUM_IP);
	if (dev->if_capenable & IFCAP_TXCSUM_IPV6)
		dev->if_hwassist |= (CSUM_UDP_IPV6 | CSUM_TCP_IPV6);


        /* Register for VLAN events */
	priv->vlan_attach = EVENTHANDLER_REGISTER(vlan_config,
            mlx4_en_vlan_rx_add_vid, priv, EVENTHANDLER_PRI_FIRST);
	priv->vlan_detach = EVENTHANDLER_REGISTER(vlan_unconfig,
            mlx4_en_vlan_rx_kill_vid, priv, EVENTHANDLER_PRI_FIRST);

	mdev->pndev[priv->port] = dev;

	priv->last_link_state = MLX4_DEV_EVENT_PORT_DOWN;
        mlx4_en_set_default_moderation(priv);

	/* Set default MAC */
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		dev_addr[ETHER_ADDR_LEN - 1 - i] = (u8) (priv->mac >> (8 * i));


	ether_ifattach(dev, dev_addr);
	if_link_state_change(dev, LINK_STATE_DOWN);
	ifmedia_init(&priv->media, IFM_IMASK | IFM_ETH_FMASK,
	    mlx4_en_media_change, mlx4_en_media_status);
	ifmedia_add(&priv->media, IFM_ETHER | IFM_FDX | IFM_1000_T, 0, NULL);
	ifmedia_add(&priv->media, IFM_ETHER | IFM_FDX | IFM_10G_SR, 0, NULL);
	ifmedia_add(&priv->media, IFM_ETHER | IFM_FDX | IFM_10G_CX4, 0, NULL);
	ifmedia_add(&priv->media, IFM_ETHER | IFM_FDX | IFM_40G_CR4, 0, NULL);
	ifmedia_add(&priv->media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&priv->media, IFM_ETHER | IFM_AUTO);

	NETDUMP_SET(dev, mlx4_en);

	en_warn(priv, "Using %d TX rings\n", prof->tx_ring_num);
	en_warn(priv, "Using %d RX rings\n", prof->rx_ring_num);

	priv->registered = 1;

        en_warn(priv, "Using %d TX rings\n", prof->tx_ring_num);
        en_warn(priv, "Using %d RX rings\n", prof->rx_ring_num);


	priv->rx_mb_size = dev->if_mtu + ETH_HLEN + VLAN_HLEN + ETH_FCS_LEN;
	err = mlx4_SET_PORT_general(mdev->dev, priv->port,
				    priv->rx_mb_size,
				    prof->tx_pause, prof->tx_ppp,
				    prof->rx_pause, prof->rx_ppp);
	if (err) {
		en_err(priv, "Failed setting port general configurations "
		       "for port %d, with error %d\n", priv->port, err);
		goto out;
	}

	/* Init port */
	en_warn(priv, "Initializing port\n");
	err = mlx4_INIT_PORT(mdev->dev, priv->port);
	if (err) {
		en_err(priv, "Failed Initializing port\n");
		goto out;
	}

	queue_delayed_work(mdev->workqueue, &priv->stats_task, STATS_DELAY);

        if (mdev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_TS)
                queue_delayed_work(mdev->workqueue, &priv->service_task, SERVICE_TASK_DELAY);

	return 0;

out:
	mlx4_en_destroy_netdev(dev);
	return err;
}

static int mlx4_en_set_ring_size(struct net_device *dev,
    int rx_size, int tx_size)
{
        struct mlx4_en_priv *priv = netdev_priv(dev);
        struct mlx4_en_dev *mdev = priv->mdev;
        int port_up = 0;
        int err = 0;

        rx_size = roundup_pow_of_two(rx_size);
        rx_size = max_t(u32, rx_size, MLX4_EN_MIN_RX_SIZE);
        rx_size = min_t(u32, rx_size, MLX4_EN_MAX_RX_SIZE);
        tx_size = roundup_pow_of_two(tx_size);
        tx_size = max_t(u32, tx_size, MLX4_EN_MIN_TX_SIZE);
        tx_size = min_t(u32, tx_size, MLX4_EN_MAX_TX_SIZE);

        if (rx_size == (priv->port_up ?
            priv->rx_ring[0]->actual_size : priv->rx_ring[0]->size) &&
            tx_size == priv->tx_ring[0]->size)
                return 0;
        mutex_lock(&mdev->state_lock);
        if (priv->port_up) {
                port_up = 1;
                mlx4_en_stop_port(dev);
        }
        mlx4_en_free_resources(priv);
        priv->prof->tx_ring_size = tx_size;
        priv->prof->rx_ring_size = rx_size;
        err = mlx4_en_alloc_resources(priv);
        if (err) {
                en_err(priv, "Failed reallocating port resources\n");
                goto out;
        }
        if (port_up) {
                err = mlx4_en_start_port(dev);
                if (err)
                        en_err(priv, "Failed starting port\n");
        }
out:
        mutex_unlock(&mdev->state_lock);
        return err;
}
static int mlx4_en_set_rx_ring_size(SYSCTL_HANDLER_ARGS)
{
        struct mlx4_en_priv *priv;
        int size;
        int error;

        priv = arg1;
        size = priv->prof->rx_ring_size;
        error = sysctl_handle_int(oidp, &size, 0, req);
        if (error || !req->newptr)
                return (error);
        error = -mlx4_en_set_ring_size(priv->dev, size,
            priv->prof->tx_ring_size);
        return (error);
}

static int mlx4_en_set_tx_ring_size(SYSCTL_HANDLER_ARGS)
{
        struct mlx4_en_priv *priv;
        int size;
        int error;

        priv = arg1;
        size = priv->prof->tx_ring_size;
        error = sysctl_handle_int(oidp, &size, 0, req);
        if (error || !req->newptr)
                return (error);
        error = -mlx4_en_set_ring_size(priv->dev, priv->prof->rx_ring_size,
            size);

        return (error);
}

static int mlx4_en_get_module_info(struct net_device *dev,
				   struct ethtool_modinfo *modinfo)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	int ret;
	u8 data[4];

	/* Read first 2 bytes to get Module & REV ID */
	ret = mlx4_get_module_info(mdev->dev, priv->port,
				   0/*offset*/, 2/*size*/, data);

	if (ret < 2) {
		en_err(priv, "Failed to read eeprom module first two bytes, error: 0x%x\n", -ret);
		return -EIO;
	}

	switch (data[0] /* identifier */) {
	case MLX4_MODULE_ID_QSFP:
		modinfo->type = ETH_MODULE_SFF_8436;
		modinfo->eeprom_len = ETH_MODULE_SFF_8436_LEN;
		break;
	case MLX4_MODULE_ID_QSFP_PLUS:
		if (data[1] >= 0x3) { /* revision id */
			modinfo->type = ETH_MODULE_SFF_8636;
			modinfo->eeprom_len = ETH_MODULE_SFF_8636_LEN;
		} else {
			modinfo->type = ETH_MODULE_SFF_8436;
			modinfo->eeprom_len = ETH_MODULE_SFF_8436_LEN;
		}
		break;
	case MLX4_MODULE_ID_QSFP28:
		modinfo->type = ETH_MODULE_SFF_8636;
		modinfo->eeprom_len = ETH_MODULE_SFF_8636_LEN;
		break;
	case MLX4_MODULE_ID_SFP:
		modinfo->type = ETH_MODULE_SFF_8472;
		modinfo->eeprom_len = ETH_MODULE_SFF_8472_LEN;
		break;
	default:
		en_err(priv, "mlx4_en_get_module_info :  Not recognized cable type\n");
		return -EINVAL;
	}

	return 0;
}

static int mlx4_en_get_module_eeprom(struct net_device *dev,
				     struct ethtool_eeprom *ee,
				     u8 *data)
{
	struct mlx4_en_priv *priv = netdev_priv(dev);
	struct mlx4_en_dev *mdev = priv->mdev;
	int offset = ee->offset;
	int i = 0, ret;

	if (ee->len == 0)
		return -EINVAL;

	memset(data, 0, ee->len);

	while (i < ee->len) {
		en_dbg(DRV, priv,
		       "mlx4_get_module_info i(%d) offset(%d) len(%d)\n",
		       i, offset, ee->len - i);

		ret = mlx4_get_module_info(mdev->dev, priv->port,
					   offset, ee->len - i, data + i);

		if (!ret) /* Done reading */
			return 0;

		if (ret < 0) {
			en_err(priv,
			       "mlx4_get_module_info i(%d) offset(%d) bytes_to_read(%d) - FAILED (0x%x)\n",
			       i, offset, ee->len - i, ret);
			return -1;
		}

		i += ret;
		offset += ret;
	}
	return 0;
}

static void mlx4_en_print_eeprom(u8 *data, __u32 len)
{
	int		i;
	int		j = 0;
	int		row = 0;
	const int	NUM_OF_BYTES = 16;

	printf("\nOffset\t\tValues\n");
	printf("------\t\t------\n");
	while(row < len){
		printf("0x%04x\t\t",row);
		for(i=0; i < NUM_OF_BYTES; i++){
			printf("%02x ", data[j]);
			row++;
			j++;
		}
		printf("\n");
	}
}

/* Read cable EEPROM module information by first inspecting the first
 * two bytes to get the length and then read the rest of the information.
 * The information is printed to dmesg. */
static int mlx4_en_read_eeprom(SYSCTL_HANDLER_ARGS)
{

	u8*		data;
	int		error;
	int		result = 0;
	struct		mlx4_en_priv *priv;
	struct		net_device *dev;
	struct		ethtool_modinfo modinfo;
	struct		ethtool_eeprom ee;

	error = sysctl_handle_int(oidp, &result, 0, req);
	if (error || !req->newptr)
		return (error);

	if (result == 1) {
		priv = arg1;
		dev = priv->dev;
		data = kmalloc(PAGE_SIZE, GFP_KERNEL);

		error = mlx4_en_get_module_info(dev, &modinfo);
		if (error) {
			en_err(priv,
			       "mlx4_en_get_module_info returned with error - FAILED (0x%x)\n",
			       -error);
			goto out;
		}

		ee.len = modinfo.eeprom_len;
		ee.offset = 0;

		error = mlx4_en_get_module_eeprom(dev, &ee, data);
		if (error) {
			en_err(priv,
			       "mlx4_en_get_module_eeprom returned with error - FAILED (0x%x)\n",
			       -error);
			/* Continue printing partial information in case of an error */
		}

		/* EEPROM information will be printed in dmesg */
		mlx4_en_print_eeprom(data, ee.len);
out:
		kfree(data);
	}
	/* Return zero to prevent sysctl failure. */
	return (0);
}

static int mlx4_en_set_tx_ppp(SYSCTL_HANDLER_ARGS)
{
        struct mlx4_en_priv *priv;
        int ppp;
        int error;

        priv = arg1;
        ppp = priv->prof->tx_ppp;
        error = sysctl_handle_int(oidp, &ppp, 0, req);
        if (error || !req->newptr)
                return (error);
        if (ppp > 0xff || ppp < 0)
                return (-EINVAL);
        priv->prof->tx_ppp = ppp;
        error = -mlx4_SET_PORT_general(priv->mdev->dev, priv->port,
                                       priv->rx_mb_size + ETHER_CRC_LEN,
                                       priv->prof->tx_pause,
                                       priv->prof->tx_ppp,
                                       priv->prof->rx_pause,
                                       priv->prof->rx_ppp);

        return (error);
}

static int mlx4_en_set_rx_ppp(SYSCTL_HANDLER_ARGS)
{
        struct mlx4_en_priv *priv;
        struct mlx4_en_dev *mdev;
        int ppp;
        int error;
        int port_up;

        port_up = 0;
        priv = arg1;
        mdev = priv->mdev;
        ppp = priv->prof->rx_ppp;
        error = sysctl_handle_int(oidp, &ppp, 0, req);
        if (error || !req->newptr)
                return (error);
        if (ppp > 0xff || ppp < 0)
                return (-EINVAL);
        /* See if we have to change the number of tx queues. */
        if (!ppp != !priv->prof->rx_ppp) {
                mutex_lock(&mdev->state_lock);
                if (priv->port_up) {
                        port_up = 1;
                        mlx4_en_stop_port(priv->dev);
                }
                mlx4_en_free_resources(priv);
                priv->prof->rx_ppp = ppp;
                error = -mlx4_en_alloc_resources(priv);
                if (error)
                        en_err(priv, "Failed reallocating port resources\n");
                if (error == 0 && port_up) {
                        error = -mlx4_en_start_port(priv->dev);
                        if (error)
                                en_err(priv, "Failed starting port\n");
                }
                mutex_unlock(&mdev->state_lock);
                return (error);

        }
        priv->prof->rx_ppp = ppp;
        error = -mlx4_SET_PORT_general(priv->mdev->dev, priv->port,
                                       priv->rx_mb_size + ETHER_CRC_LEN,
                                       priv->prof->tx_pause,
                                       priv->prof->tx_ppp,
                                       priv->prof->rx_pause,
                                       priv->prof->rx_ppp);

        return (error);
}

static void mlx4_en_sysctl_conf(struct mlx4_en_priv *priv)
{
        struct net_device *dev;
        struct sysctl_ctx_list *ctx;
        struct sysctl_oid *node;
        struct sysctl_oid_list *node_list;
        struct sysctl_oid *coal;
        struct sysctl_oid_list *coal_list;
	const char *pnameunit;
        dev = priv->dev;
        ctx = &priv->conf_ctx;
	pnameunit = device_get_nameunit(priv->mdev->pdev->dev.bsddev);

        sysctl_ctx_init(ctx);
        priv->conf_sysctl = SYSCTL_ADD_NODE(ctx, SYSCTL_STATIC_CHILDREN(_hw),
            OID_AUTO, dev->if_xname, CTLFLAG_RD, 0, "mlx4 10gig ethernet");
        node = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(priv->conf_sysctl), OID_AUTO,
            "conf", CTLFLAG_RD, NULL, "Configuration");
        node_list = SYSCTL_CHILDREN(node);

        SYSCTL_ADD_UINT(ctx, node_list, OID_AUTO, "msg_enable",
            CTLFLAG_RW, &priv->msg_enable, 0,
            "Driver message enable bitfield");
        SYSCTL_ADD_UINT(ctx, node_list, OID_AUTO, "rx_rings",
            CTLFLAG_RD, &priv->rx_ring_num, 0,
            "Number of receive rings");
        SYSCTL_ADD_UINT(ctx, node_list, OID_AUTO, "tx_rings",
            CTLFLAG_RD, &priv->tx_ring_num, 0,
            "Number of transmit rings");
        SYSCTL_ADD_PROC(ctx, node_list, OID_AUTO, "rx_size",
            CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE, priv, 0,
            mlx4_en_set_rx_ring_size, "I", "Receive ring size");
        SYSCTL_ADD_PROC(ctx, node_list, OID_AUTO, "tx_size",
            CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE, priv, 0,
            mlx4_en_set_tx_ring_size, "I", "Transmit ring size");
        SYSCTL_ADD_PROC(ctx, node_list, OID_AUTO, "tx_ppp",
            CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE, priv, 0,
            mlx4_en_set_tx_ppp, "I", "TX Per-priority pause");
        SYSCTL_ADD_PROC(ctx, node_list, OID_AUTO, "rx_ppp",
            CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE, priv, 0,
            mlx4_en_set_rx_ppp, "I", "RX Per-priority pause");
        SYSCTL_ADD_UINT(ctx, node_list, OID_AUTO, "port_num",
            CTLFLAG_RD, &priv->port, 0,
            "Port Number");
        SYSCTL_ADD_STRING(ctx, node_list, OID_AUTO, "device_name",
	    CTLFLAG_RD, __DECONST(void *, pnameunit), 0,
	    "PCI device name");
        /* Add coalescer configuration. */
        coal = SYSCTL_ADD_NODE(ctx, node_list, OID_AUTO,
            "coalesce", CTLFLAG_RD, NULL, "Interrupt coalesce configuration");
        coal_list = SYSCTL_CHILDREN(coal);
        SYSCTL_ADD_UINT(ctx, coal_list, OID_AUTO, "pkt_rate_low",
            CTLFLAG_RW, &priv->pkt_rate_low, 0,
            "Packets per-second for minimum delay");
        SYSCTL_ADD_UINT(ctx, coal_list, OID_AUTO, "rx_usecs_low",
            CTLFLAG_RW, &priv->rx_usecs_low, 0,
            "Minimum RX delay in micro-seconds");
        SYSCTL_ADD_UINT(ctx, coal_list, OID_AUTO, "pkt_rate_high",
            CTLFLAG_RW, &priv->pkt_rate_high, 0,
            "Packets per-second for maximum delay");
        SYSCTL_ADD_UINT(ctx, coal_list, OID_AUTO, "rx_usecs_high",
            CTLFLAG_RW, &priv->rx_usecs_high, 0,
            "Maximum RX delay in micro-seconds");
        SYSCTL_ADD_UINT(ctx, coal_list, OID_AUTO, "sample_interval",
            CTLFLAG_RW, &priv->sample_interval, 0,
            "adaptive frequency in units of HZ ticks");
        SYSCTL_ADD_UINT(ctx, coal_list, OID_AUTO, "adaptive_rx_coal",
            CTLFLAG_RW, &priv->adaptive_rx_coal, 0,
            "Enable adaptive rx coalescing");
	/* EEPROM support */
	SYSCTL_ADD_PROC(ctx, node_list, OID_AUTO, "eeprom_info",
	    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE, priv, 0,
	    mlx4_en_read_eeprom, "I", "EEPROM information");
}

static void mlx4_en_sysctl_stat(struct mlx4_en_priv *priv)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid_list *node_list;
	struct sysctl_oid *ring_node;
	struct sysctl_oid_list *ring_list;
	struct mlx4_en_tx_ring *tx_ring;
	struct mlx4_en_rx_ring *rx_ring;
	char namebuf[128];
	int i;

	ctx = &priv->stat_ctx;
	sysctl_ctx_init(ctx);
	priv->stat_sysctl = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(priv->conf_sysctl), OID_AUTO,
	    "stat", CTLFLAG_RD, NULL, "Statistics");
	node_list = SYSCTL_CHILDREN(priv->stat_sysctl);

#ifdef MLX4_EN_PERF_STAT
	SYSCTL_ADD_UINT(ctx, node_list, OID_AUTO, "tx_poll", CTLFLAG_RD,
	    &priv->pstats.tx_poll, "TX Poll calls");
	SYSCTL_ADD_QUAD(ctx, node_list, OID_AUTO, "tx_pktsz_avg", CTLFLAG_RD,
	    &priv->pstats.tx_pktsz_avg, "TX average packet size");
	SYSCTL_ADD_UINT(ctx, node_list, OID_AUTO, "inflight_avg", CTLFLAG_RD,
	    &priv->pstats.inflight_avg, "TX average packets in-flight");
	SYSCTL_ADD_UINT(ctx, node_list, OID_AUTO, "tx_coal_avg", CTLFLAG_RD,
	    &priv->pstats.tx_coal_avg, "TX average coalesced completions");
	SYSCTL_ADD_UINT(ctx, node_list, OID_AUTO, "rx_coal_avg", CTLFLAG_RD,
	    &priv->pstats.rx_coal_avg, "RX average coalesced completions");
#endif

	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "tso_packets", CTLFLAG_RD,
	    &priv->port_stats.tso_packets, 0, "TSO packets sent");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "queue_stopped", CTLFLAG_RD,
	    &priv->port_stats.queue_stopped, 0, "Queue full");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "wake_queue", CTLFLAG_RD,
	    &priv->port_stats.wake_queue, 0, "Queue resumed after full");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "tx_timeout", CTLFLAG_RD,
	    &priv->port_stats.tx_timeout, 0, "Transmit timeouts");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "tx_oversized_packets", CTLFLAG_RD,
	    &priv->port_stats.oversized_packets, 0, "TX oversized packets, m_defrag failed");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "rx_alloc_failed", CTLFLAG_RD,
	    &priv->port_stats.rx_alloc_failed, 0, "RX failed to allocate mbuf");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "rx_chksum_good", CTLFLAG_RD,
            &priv->port_stats.rx_chksum_good, 0, "RX checksum offload success");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "rx_chksum_none", CTLFLAG_RD,
	    &priv->port_stats.rx_chksum_none, 0, "RX without checksum offload");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "tx_chksum_offload",
	    CTLFLAG_RD, &priv->port_stats.tx_chksum_offload, 0,
	    "TX checksum offloads");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "defrag_attempts",
	    CTLFLAG_RD, &priv->port_stats.defrag_attempts, 0,
	    "Oversized chains defragged");

	/* Could strdup the names and add in a loop.  This is simpler. */
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "rx_bytes", CTLFLAG_RD,
	    &priv->pkstats.rx_bytes, 0, "RX Bytes");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "rx_packets", CTLFLAG_RD,
	    &priv->pkstats.rx_packets, 0, "RX packets");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "rx_multicast_packets", CTLFLAG_RD,
	    &priv->pkstats.rx_multicast_packets, 0, "RX Multicast Packets");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "rx_broadcast_packets", CTLFLAG_RD,
	    &priv->pkstats.rx_broadcast_packets, 0, "RX Broadcast Packets");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "rx_errors", CTLFLAG_RD,
	    &priv->pkstats.rx_errors, 0, "RX Errors");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "rx_dropped", CTLFLAG_RD,
	    &priv->pkstats.rx_dropped, 0, "RX Dropped");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "rx_length_errors", CTLFLAG_RD,
	    &priv->pkstats.rx_length_errors, 0, "RX Length Errors");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "rx_over_errors", CTLFLAG_RD,
	    &priv->pkstats.rx_over_errors, 0, "RX Over Errors");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "rx_crc_errors", CTLFLAG_RD,
	    &priv->pkstats.rx_crc_errors, 0, "RX CRC Errors");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "rx_jabbers", CTLFLAG_RD,
	    &priv->pkstats.rx_jabbers, 0, "RX Jabbers");

	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "rx_in_range_length_error", CTLFLAG_RD,
	    &priv->pkstats.rx_in_range_length_error, 0, "RX IN_Range Length Error");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "rx_out_range_length_error",
	    CTLFLAG_RD, &priv->pkstats.rx_out_range_length_error, 0,
	    "RX Out Range Length Error");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "rx_lt_64_bytes_packets", CTLFLAG_RD,
	    &priv->pkstats.rx_lt_64_bytes_packets, 0, "RX Lt 64 Bytes Packets");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "rx_127_bytes_packets", CTLFLAG_RD,
	    &priv->pkstats.rx_127_bytes_packets, 0, "RX 127 bytes Packets");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "rx_255_bytes_packets", CTLFLAG_RD,
	    &priv->pkstats.rx_255_bytes_packets, 0, "RX 255 bytes Packets");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "rx_511_bytes_packets", CTLFLAG_RD,
	    &priv->pkstats.rx_511_bytes_packets, 0, "RX 511 bytes Packets");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "rx_1023_bytes_packets", CTLFLAG_RD,
	    &priv->pkstats.rx_1023_bytes_packets, 0, "RX 1023 bytes Packets");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "rx_1518_bytes_packets", CTLFLAG_RD,
	    &priv->pkstats.rx_1518_bytes_packets, 0, "RX 1518 bytes Packets");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "rx_1522_bytes_packets", CTLFLAG_RD,
	    &priv->pkstats.rx_1522_bytes_packets, 0, "RX 1522 bytes Packets");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "rx_1548_bytes_packets", CTLFLAG_RD,
	    &priv->pkstats.rx_1548_bytes_packets, 0, "RX 1548 bytes Packets");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "rx_gt_1548_bytes_packets", CTLFLAG_RD,
	    &priv->pkstats.rx_gt_1548_bytes_packets, 0,
	    "RX Greater Then 1548 bytes Packets");

	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "tx_packets", CTLFLAG_RD,
	    &priv->pkstats.tx_packets, 0, "TX packets");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "tx_bytes", CTLFLAG_RD,
	    &priv->pkstats.tx_bytes, 0, "TX Bytes");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "tx_multicast_packets", CTLFLAG_RD,
	    &priv->pkstats.tx_multicast_packets, 0, "TX Multicast Packets");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "tx_broadcast_packets", CTLFLAG_RD,
	    &priv->pkstats.tx_broadcast_packets, 0, "TX Broadcast Packets");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "tx_errors", CTLFLAG_RD,
	    &priv->pkstats.tx_errors, 0, "TX Errors");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "tx_dropped", CTLFLAG_RD,
	    &priv->pkstats.tx_dropped, 0, "TX Dropped");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "tx_lt_64_bytes_packets", CTLFLAG_RD,
	    &priv->pkstats.tx_lt_64_bytes_packets, 0, "TX Less Then 64 Bytes Packets");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "tx_127_bytes_packets", CTLFLAG_RD,
	    &priv->pkstats.tx_127_bytes_packets, 0, "TX 127 Bytes Packets");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "tx_255_bytes_packets", CTLFLAG_RD,
	    &priv->pkstats.tx_255_bytes_packets, 0, "TX 255 Bytes Packets");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "tx_511_bytes_packets", CTLFLAG_RD,
	    &priv->pkstats.tx_511_bytes_packets, 0, "TX 511 Bytes Packets");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "tx_1023_bytes_packets", CTLFLAG_RD,
	    &priv->pkstats.tx_1023_bytes_packets, 0, "TX 1023 Bytes Packets");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "tx_1518_bytes_packets", CTLFLAG_RD,
	    &priv->pkstats.tx_1518_bytes_packets, 0, "TX 1518 Bytes Packets");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "tx_1522_bytes_packets", CTLFLAG_RD,
	    &priv->pkstats.tx_1522_bytes_packets, 0, "TX 1522 Bytes Packets");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "tx_1548_bytes_packets", CTLFLAG_RD,
	    &priv->pkstats.tx_1548_bytes_packets, 0, "TX 1548 Bytes Packets");
	SYSCTL_ADD_U64(ctx, node_list, OID_AUTO, "tx_gt_1548_bytes_packets", CTLFLAG_RD,
	    &priv->pkstats.tx_gt_1548_bytes_packets, 0,
	    "TX Greater Then 1548 Bytes Packets");

	for (i = 0; i < priv->tx_ring_num; i++) {
		tx_ring = priv->tx_ring[i];
		snprintf(namebuf, sizeof(namebuf), "tx_ring%d", i);
		ring_node = SYSCTL_ADD_NODE(ctx, node_list, OID_AUTO, namebuf,
		    CTLFLAG_RD, NULL, "TX Ring");
		ring_list = SYSCTL_CHILDREN(ring_node);
		SYSCTL_ADD_U64(ctx, ring_list, OID_AUTO, "packets",
		    CTLFLAG_RD, &tx_ring->packets, 0, "TX packets");
		SYSCTL_ADD_U64(ctx, ring_list, OID_AUTO, "bytes",
		    CTLFLAG_RD, &tx_ring->bytes, 0, "TX bytes");
		SYSCTL_ADD_U64(ctx, ring_list, OID_AUTO, "tso_packets",
		    CTLFLAG_RD, &tx_ring->tso_packets, 0, "TSO packets");
		SYSCTL_ADD_U64(ctx, ring_list, OID_AUTO, "defrag_attempts",
		    CTLFLAG_RD, &tx_ring->defrag_attempts, 0,
		    "Oversized chains defragged");
	}

	for (i = 0; i < priv->rx_ring_num; i++) {
		rx_ring = priv->rx_ring[i];
		snprintf(namebuf, sizeof(namebuf), "rx_ring%d", i);
		ring_node = SYSCTL_ADD_NODE(ctx, node_list, OID_AUTO, namebuf,
		    CTLFLAG_RD, NULL, "RX Ring");
		ring_list = SYSCTL_CHILDREN(ring_node);
		SYSCTL_ADD_U64(ctx, ring_list, OID_AUTO, "packets",
		    CTLFLAG_RD, &rx_ring->packets, 0, "RX packets");
		SYSCTL_ADD_U64(ctx, ring_list, OID_AUTO, "bytes",
		    CTLFLAG_RD, &rx_ring->bytes, 0, "RX bytes");
		SYSCTL_ADD_U64(ctx, ring_list, OID_AUTO, "error",
		    CTLFLAG_RD, &rx_ring->errors, 0, "RX soft errors");
	}
}

#ifdef NETDUMP
static void
mlx4_en_netdump_init(struct ifnet *dev, int *nrxr, int *ncl, int *clsize)
{
	struct mlx4_en_priv *priv;

	priv = if_getsoftc(dev);
	mutex_lock(&priv->mdev->state_lock);
	*nrxr = priv->rx_ring_num;
	*ncl = NETDUMP_MAX_IN_FLIGHT;
	*clsize = priv->rx_mb_size;
	mutex_unlock(&priv->mdev->state_lock);
}

static void
mlx4_en_netdump_event(struct ifnet *dev, enum netdump_ev event)
{
}

static int
mlx4_en_netdump_transmit(struct ifnet *dev, struct mbuf *m)
{
	struct mlx4_en_priv *priv;
	int err;

	priv = if_getsoftc(dev);
	if ((if_getdrvflags(dev) & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING || !priv->link_state)
		return (ENOENT);

	err = mlx4_en_xmit(priv, 0, &m);
	if (err != 0 && m != NULL)
		m_freem(m);
	return (err);
}

static int
mlx4_en_netdump_poll(struct ifnet *dev, int count)
{
	struct mlx4_en_priv *priv;

	priv = if_getsoftc(dev);
	if ((if_getdrvflags(dev) & IFF_DRV_RUNNING) == 0 || !priv->link_state)
		return (ENOENT);

	mlx4_poll_interrupts(priv->mdev->dev);

	return (0);
}
#endif /* NETDUMP */
