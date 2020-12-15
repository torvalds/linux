// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) ST-Ericsson AB 2010
 * Authors:	Sjur Brendeland
 *		Daniel Martensson
 */

#define pr_fmt(fmt) KBUILD_MODNAME ":%s(): " fmt, __func__

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/sched.h>
#include <linux/sockios.h>
#include <linux/caif/if_caif.h>
#include <net/rtnetlink.h>
#include <net/caif/caif_layer.h>
#include <net/caif/cfpkt.h>
#include <net/caif/caif_dev.h>

/* GPRS PDP connection has MTU to 1500 */
#define GPRS_PDP_MTU 1500
/* 5 sec. connect timeout */
#define CONNECT_TIMEOUT (5 * HZ)
#define CAIF_NET_DEFAULT_QUEUE_LEN 500
#define UNDEF_CONNID 0xffffffff

/*This list is protected by the rtnl lock. */
static LIST_HEAD(chnl_net_list);

MODULE_LICENSE("GPL");
MODULE_ALIAS_RTNL_LINK("caif");

enum caif_states {
	CAIF_CONNECTED		= 1,
	CAIF_CONNECTING,
	CAIF_DISCONNECTED,
	CAIF_SHUTDOWN
};

struct chnl_net {
	struct cflayer chnl;
	struct caif_connect_request conn_req;
	struct list_head list_field;
	struct net_device *netdev;
	char name[256];
	wait_queue_head_t netmgmt_wq;
	/* Flow status to remember and control the transmission. */
	bool flowenabled;
	enum caif_states state;
};

static void robust_list_del(struct list_head *delete_node)
{
	struct list_head *list_node;
	struct list_head *n;
	ASSERT_RTNL();
	list_for_each_safe(list_node, n, &chnl_net_list) {
		if (list_node == delete_node) {
			list_del(list_node);
			return;
		}
	}
	WARN_ON(1);
}

static int chnl_recv_cb(struct cflayer *layr, struct cfpkt *pkt)
{
	struct sk_buff *skb;
	struct chnl_net *priv;
	int pktlen;
	const u8 *ip_version;
	u8 buf;

	priv = container_of(layr, struct chnl_net, chnl);
	if (!priv)
		return -EINVAL;

	skb = (struct sk_buff *) cfpkt_tonative(pkt);

	/* Get length of CAIF packet. */
	pktlen = skb->len;

	/* Pass some minimum information and
	 * send the packet to the net stack.
	 */
	skb->dev = priv->netdev;

	/* check the version of IP */
	ip_version = skb_header_pointer(skb, 0, 1, &buf);
	if (!ip_version) {
		kfree_skb(skb);
		return -EINVAL;
	}

	switch (*ip_version >> 4) {
	case 4:
		skb->protocol = htons(ETH_P_IP);
		break;
	case 6:
		skb->protocol = htons(ETH_P_IPV6);
		break;
	default:
		kfree_skb(skb);
		priv->netdev->stats.rx_errors++;
		return -EINVAL;
	}

	/* If we change the header in loop mode, the checksum is corrupted. */
	if (priv->conn_req.protocol == CAIFPROTO_DATAGRAM_LOOP)
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	else
		skb->ip_summed = CHECKSUM_NONE;

	if (in_interrupt())
		netif_rx(skb);
	else
		netif_rx_ni(skb);

	/* Update statistics. */
	priv->netdev->stats.rx_packets++;
	priv->netdev->stats.rx_bytes += pktlen;

	return 0;
}

static int delete_device(struct chnl_net *dev)
{
	ASSERT_RTNL();
	if (dev->netdev)
		unregister_netdevice(dev->netdev);
	return 0;
}

static void close_work(struct work_struct *work)
{
	struct chnl_net *dev = NULL;
	struct list_head *list_node;
	struct list_head *_tmp;

	rtnl_lock();
	list_for_each_safe(list_node, _tmp, &chnl_net_list) {
		dev = list_entry(list_node, struct chnl_net, list_field);
		if (dev->state == CAIF_SHUTDOWN)
			dev_close(dev->netdev);
	}
	rtnl_unlock();
}
static DECLARE_WORK(close_worker, close_work);

static void chnl_hold(struct cflayer *lyr)
{
	struct chnl_net *priv = container_of(lyr, struct chnl_net, chnl);
	dev_hold(priv->netdev);
}

static void chnl_put(struct cflayer *lyr)
{
	struct chnl_net *priv = container_of(lyr, struct chnl_net, chnl);
	dev_put(priv->netdev);
}

static void chnl_flowctrl_cb(struct cflayer *layr, enum caif_ctrlcmd flow,
			     int phyid)
{
	struct chnl_net *priv = container_of(layr, struct chnl_net, chnl);
	pr_debug("NET flowctrl func called flow: %s\n",
		flow == CAIF_CTRLCMD_FLOW_ON_IND ? "ON" :
		flow == CAIF_CTRLCMD_INIT_RSP ? "INIT" :
		flow == CAIF_CTRLCMD_FLOW_OFF_IND ? "OFF" :
		flow == CAIF_CTRLCMD_DEINIT_RSP ? "CLOSE/DEINIT" :
		flow == CAIF_CTRLCMD_INIT_FAIL_RSP ? "OPEN_FAIL" :
		flow == CAIF_CTRLCMD_REMOTE_SHUTDOWN_IND ?
		 "REMOTE_SHUTDOWN" : "UNKNOWN CTRL COMMAND");



	switch (flow) {
	case CAIF_CTRLCMD_FLOW_OFF_IND:
		priv->flowenabled = false;
		netif_stop_queue(priv->netdev);
		break;
	case CAIF_CTRLCMD_DEINIT_RSP:
		priv->state = CAIF_DISCONNECTED;
		break;
	case CAIF_CTRLCMD_INIT_FAIL_RSP:
		priv->state = CAIF_DISCONNECTED;
		wake_up_interruptible(&priv->netmgmt_wq);
		break;
	case CAIF_CTRLCMD_REMOTE_SHUTDOWN_IND:
		priv->state = CAIF_SHUTDOWN;
		netif_tx_disable(priv->netdev);
		schedule_work(&close_worker);
		break;
	case CAIF_CTRLCMD_FLOW_ON_IND:
		priv->flowenabled = true;
		netif_wake_queue(priv->netdev);
		break;
	case CAIF_CTRLCMD_INIT_RSP:
		caif_client_register_refcnt(&priv->chnl, chnl_hold, chnl_put);
		priv->state = CAIF_CONNECTED;
		priv->flowenabled = true;
		netif_wake_queue(priv->netdev);
		wake_up_interruptible(&priv->netmgmt_wq);
		break;
	default:
		break;
	}
}

static netdev_tx_t chnl_net_start_xmit(struct sk_buff *skb,
				       struct net_device *dev)
{
	struct chnl_net *priv;
	struct cfpkt *pkt = NULL;
	int len;
	int result = -1;
	/* Get our private data. */
	priv = netdev_priv(dev);

	if (skb->len > priv->netdev->mtu) {
		pr_warn("Size of skb exceeded MTU\n");
		kfree_skb(skb);
		dev->stats.tx_errors++;
		return NETDEV_TX_OK;
	}

	if (!priv->flowenabled) {
		pr_debug("dropping packets flow off\n");
		kfree_skb(skb);
		dev->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}

	if (priv->conn_req.protocol == CAIFPROTO_DATAGRAM_LOOP)
		swap(ip_hdr(skb)->saddr, ip_hdr(skb)->daddr);

	/* Store original SKB length. */
	len = skb->len;

	pkt = cfpkt_fromnative(CAIF_DIR_OUT, (void *) skb);

	/* Send the packet down the stack. */
	result = priv->chnl.dn->transmit(priv->chnl.dn, pkt);
	if (result) {
		dev->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}

	/* Update statistics. */
	dev->stats.tx_packets++;
	dev->stats.tx_bytes += len;

	return NETDEV_TX_OK;
}

static int chnl_net_open(struct net_device *dev)
{
	struct chnl_net *priv = NULL;
	int result = -1;
	int llifindex, headroom, tailroom, mtu;
	struct net_device *lldev;
	ASSERT_RTNL();
	priv = netdev_priv(dev);
	if (!priv) {
		pr_debug("chnl_net_open: no priv\n");
		return -ENODEV;
	}

	if (priv->state != CAIF_CONNECTING) {
		priv->state = CAIF_CONNECTING;
		result = caif_connect_client(dev_net(dev), &priv->conn_req,
						&priv->chnl, &llifindex,
						&headroom, &tailroom);
		if (result != 0) {
				pr_debug("err: "
					 "Unable to register and open device,"
					 " Err:%d\n",
					 result);
				goto error;
		}

		lldev = __dev_get_by_index(dev_net(dev), llifindex);

		if (lldev == NULL) {
			pr_debug("no interface?\n");
			result = -ENODEV;
			goto error;
		}

		dev->needed_tailroom = tailroom + lldev->needed_tailroom;
		dev->hard_header_len = headroom + lldev->hard_header_len +
			lldev->needed_tailroom;

		/*
		 * MTU, head-room etc is not know before we have a
		 * CAIF link layer device available. MTU calculation may
		 * override initial RTNL configuration.
		 * MTU is minimum of current mtu, link layer mtu pluss
		 * CAIF head and tail, and PDP GPRS contexts max MTU.
		 */
		mtu = min_t(int, dev->mtu, lldev->mtu - (headroom + tailroom));
		mtu = min_t(int, GPRS_PDP_MTU, mtu);
		dev_set_mtu(dev, mtu);

		if (mtu < 100) {
			pr_warn("CAIF Interface MTU too small (%d)\n", mtu);
			result = -ENODEV;
			goto error;
		}
	}

	rtnl_unlock();  /* Release RTNL lock during connect wait */

	result = wait_event_interruptible_timeout(priv->netmgmt_wq,
						priv->state != CAIF_CONNECTING,
						CONNECT_TIMEOUT);

	rtnl_lock();

	if (result == -ERESTARTSYS) {
		pr_debug("wait_event_interruptible woken by a signal\n");
		result = -ERESTARTSYS;
		goto error;
	}

	if (result == 0) {
		pr_debug("connect timeout\n");
		caif_disconnect_client(dev_net(dev), &priv->chnl);
		priv->state = CAIF_DISCONNECTED;
		pr_debug("state disconnected\n");
		result = -ETIMEDOUT;
		goto error;
	}

	if (priv->state != CAIF_CONNECTED) {
		pr_debug("connect failed\n");
		result = -ECONNREFUSED;
		goto error;
	}
	pr_debug("CAIF Netdevice connected\n");
	return 0;

error:
	caif_disconnect_client(dev_net(dev), &priv->chnl);
	priv->state = CAIF_DISCONNECTED;
	pr_debug("state disconnected\n");
	return result;

}

static int chnl_net_stop(struct net_device *dev)
{
	struct chnl_net *priv;

	ASSERT_RTNL();
	priv = netdev_priv(dev);
	priv->state = CAIF_DISCONNECTED;
	caif_disconnect_client(dev_net(dev), &priv->chnl);
	return 0;
}

static int chnl_net_init(struct net_device *dev)
{
	struct chnl_net *priv;
	ASSERT_RTNL();
	priv = netdev_priv(dev);
	strncpy(priv->name, dev->name, sizeof(priv->name));
	return 0;
}

static void chnl_net_uninit(struct net_device *dev)
{
	struct chnl_net *priv;
	ASSERT_RTNL();
	priv = netdev_priv(dev);
	robust_list_del(&priv->list_field);
}

static const struct net_device_ops netdev_ops = {
	.ndo_open = chnl_net_open,
	.ndo_stop = chnl_net_stop,
	.ndo_init = chnl_net_init,
	.ndo_uninit = chnl_net_uninit,
	.ndo_start_xmit = chnl_net_start_xmit,
};

static void chnl_net_destructor(struct net_device *dev)
{
	struct chnl_net *priv = netdev_priv(dev);
	caif_free_client(&priv->chnl);
}

static void ipcaif_net_setup(struct net_device *dev)
{
	struct chnl_net *priv;
	dev->netdev_ops = &netdev_ops;
	dev->needs_free_netdev = true;
	dev->priv_destructor = chnl_net_destructor;
	dev->flags |= IFF_NOARP;
	dev->flags |= IFF_POINTOPOINT;
	dev->mtu = GPRS_PDP_MTU;
	dev->tx_queue_len = CAIF_NET_DEFAULT_QUEUE_LEN;

	priv = netdev_priv(dev);
	priv->chnl.receive = chnl_recv_cb;
	priv->chnl.ctrlcmd = chnl_flowctrl_cb;
	priv->netdev = dev;
	priv->conn_req.protocol = CAIFPROTO_DATAGRAM;
	priv->conn_req.link_selector = CAIF_LINK_HIGH_BANDW;
	priv->conn_req.priority = CAIF_PRIO_LOW;
	/* Insert illegal value */
	priv->conn_req.sockaddr.u.dgm.connection_id = UNDEF_CONNID;
	priv->flowenabled = false;

	init_waitqueue_head(&priv->netmgmt_wq);
}


static int ipcaif_fill_info(struct sk_buff *skb, const struct net_device *dev)
{
	struct chnl_net *priv;
	u8 loop;
	priv = netdev_priv(dev);
	if (nla_put_u32(skb, IFLA_CAIF_IPV4_CONNID,
			priv->conn_req.sockaddr.u.dgm.connection_id) ||
	    nla_put_u32(skb, IFLA_CAIF_IPV6_CONNID,
			priv->conn_req.sockaddr.u.dgm.connection_id))
		goto nla_put_failure;
	loop = priv->conn_req.protocol == CAIFPROTO_DATAGRAM_LOOP;
	if (nla_put_u8(skb, IFLA_CAIF_LOOPBACK, loop))
		goto nla_put_failure;
	return 0;
nla_put_failure:
	return -EMSGSIZE;

}

static void caif_netlink_parms(struct nlattr *data[],
			       struct caif_connect_request *conn_req)
{
	if (!data) {
		pr_warn("no params data found\n");
		return;
	}
	if (data[IFLA_CAIF_IPV4_CONNID])
		conn_req->sockaddr.u.dgm.connection_id =
			nla_get_u32(data[IFLA_CAIF_IPV4_CONNID]);
	if (data[IFLA_CAIF_IPV6_CONNID])
		conn_req->sockaddr.u.dgm.connection_id =
			nla_get_u32(data[IFLA_CAIF_IPV6_CONNID]);
	if (data[IFLA_CAIF_LOOPBACK]) {
		if (nla_get_u8(data[IFLA_CAIF_LOOPBACK]))
			conn_req->protocol = CAIFPROTO_DATAGRAM_LOOP;
		else
			conn_req->protocol = CAIFPROTO_DATAGRAM;
	}
}

static int ipcaif_newlink(struct net *src_net, struct net_device *dev,
			  struct nlattr *tb[], struct nlattr *data[],
			  struct netlink_ext_ack *extack)
{
	int ret;
	struct chnl_net *caifdev;
	ASSERT_RTNL();
	caifdev = netdev_priv(dev);
	caif_netlink_parms(data, &caifdev->conn_req);

	ret = register_netdevice(dev);
	if (ret)
		pr_warn("device rtml registration failed\n");
	else
		list_add(&caifdev->list_field, &chnl_net_list);

	/* Use ifindex as connection id, and use loopback channel default. */
	if (caifdev->conn_req.sockaddr.u.dgm.connection_id == UNDEF_CONNID) {
		caifdev->conn_req.sockaddr.u.dgm.connection_id = dev->ifindex;
		caifdev->conn_req.protocol = CAIFPROTO_DATAGRAM_LOOP;
	}
	return ret;
}

static int ipcaif_changelink(struct net_device *dev, struct nlattr *tb[],
			     struct nlattr *data[],
			     struct netlink_ext_ack *extack)
{
	struct chnl_net *caifdev;
	ASSERT_RTNL();
	caifdev = netdev_priv(dev);
	caif_netlink_parms(data, &caifdev->conn_req);
	netdev_state_change(dev);
	return 0;
}

static size_t ipcaif_get_size(const struct net_device *dev)
{
	return
		/* IFLA_CAIF_IPV4_CONNID */
		nla_total_size(4) +
		/* IFLA_CAIF_IPV6_CONNID */
		nla_total_size(4) +
		/* IFLA_CAIF_LOOPBACK */
		nla_total_size(2) +
		0;
}

static const struct nla_policy ipcaif_policy[IFLA_CAIF_MAX + 1] = {
	[IFLA_CAIF_IPV4_CONNID]	      = { .type = NLA_U32 },
	[IFLA_CAIF_IPV6_CONNID]	      = { .type = NLA_U32 },
	[IFLA_CAIF_LOOPBACK]	      = { .type = NLA_U8 }
};


static struct rtnl_link_ops ipcaif_link_ops __read_mostly = {
	.kind		= "caif",
	.priv_size	= sizeof(struct chnl_net),
	.setup		= ipcaif_net_setup,
	.maxtype	= IFLA_CAIF_MAX,
	.policy		= ipcaif_policy,
	.newlink	= ipcaif_newlink,
	.changelink	= ipcaif_changelink,
	.get_size	= ipcaif_get_size,
	.fill_info	= ipcaif_fill_info,

};

static int __init chnl_init_module(void)
{
	return rtnl_link_register(&ipcaif_link_ops);
}

static void __exit chnl_exit_module(void)
{
	struct chnl_net *dev = NULL;
	struct list_head *list_node;
	struct list_head *_tmp;
	rtnl_link_unregister(&ipcaif_link_ops);
	rtnl_lock();
	list_for_each_safe(list_node, _tmp, &chnl_net_list) {
		dev = list_entry(list_node, struct chnl_net, list_field);
		list_del(list_node);
		delete_device(dev);
	}
	rtnl_unlock();
}

module_init(chnl_init_module);
module_exit(chnl_exit_module);
