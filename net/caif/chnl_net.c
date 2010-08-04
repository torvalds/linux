/*
 * Copyright (C) ST-Ericsson AB 2010
 * Authors:	Sjur Brendeland/sjur.brandeland@stericsson.com
 *		Daniel Martensson / Daniel.Martensson@stericsson.com
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/version.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/if_ether.h>
#include <linux/moduleparam.h>
#include <linux/ip.h>
#include <linux/sched.h>
#include <linux/sockios.h>
#include <linux/caif/if_caif.h>
#include <net/rtnetlink.h>
#include <net/caif/caif_layer.h>
#include <net/caif/cfcnfg.h>
#include <net/caif/cfpkt.h>
#include <net/caif/caif_dev.h>

/* GPRS PDP connection has MTU to 1500 */
#define SIZE_MTU 1500
/* 5 sec. connect timeout */
#define CONNECT_TIMEOUT (5 * HZ)
#define CAIF_NET_DEFAULT_QUEUE_LEN 500

#undef pr_debug
#define pr_debug pr_warning

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
	struct net_device_stats stats;
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
	struct chnl_net *priv  = container_of(layr, struct chnl_net, chnl);
	int pktlen;
	int err = 0;

	priv = container_of(layr, struct chnl_net, chnl);

	if (!priv)
		return -EINVAL;

	/* Get length of CAIF packet. */
	pktlen = cfpkt_getlen(pkt);

	skb = (struct sk_buff *) cfpkt_tonative(pkt);
	/* Pass some minimum information and
	 * send the packet to the net stack.
	 */
	skb->dev = priv->netdev;
	skb->protocol = htons(ETH_P_IP);

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

	return err;
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
	/* May be called with or without RTNL lock held */
	int islocked = rtnl_is_locked();
	if (!islocked)
		rtnl_lock();
	list_for_each_safe(list_node, _tmp, &chnl_net_list) {
		dev = list_entry(list_node, struct chnl_net, list_field);
		if (dev->state == CAIF_SHUTDOWN)
			dev_close(dev->netdev);
	}
	if (!islocked)
		rtnl_unlock();
}
static DECLARE_WORK(close_worker, close_work);

static void chnl_flowctrl_cb(struct cflayer *layr, enum caif_ctrlcmd flow,
				int phyid)
{
	struct chnl_net *priv = container_of(layr, struct chnl_net, chnl);
	pr_debug("CAIF: %s(): NET flowctrl func called flow: %s\n",
		__func__,
		flow == CAIF_CTRLCMD_FLOW_ON_IND ? "ON" :
		flow == CAIF_CTRLCMD_INIT_RSP ? "INIT" :
		flow == CAIF_CTRLCMD_FLOW_OFF_IND ? "OFF" :
		flow == CAIF_CTRLCMD_DEINIT_RSP ? "CLOSE/DEINIT" :
		flow == CAIF_CTRLCMD_INIT_FAIL_RSP ? "OPEN_FAIL" :
		flow == CAIF_CTRLCMD_REMOTE_SHUTDOWN_IND ?
		 "REMOTE_SHUTDOWN" : "UKNOWN CTRL COMMAND");



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
		priv->state = CAIF_CONNECTED;
		priv->flowenabled = true;
		netif_wake_queue(priv->netdev);
		wake_up_interruptible(&priv->netmgmt_wq);
		break;
	default:
		break;
	}
}

static int chnl_net_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct chnl_net *priv;
	struct cfpkt *pkt = NULL;
	int len;
	int result = -1;
	/* Get our private data. */
	priv = netdev_priv(dev);

	if (skb->len > priv->netdev->mtu) {
		pr_warning("CAIF: %s(): Size of skb exceeded MTU\n", __func__);
		return -ENOSPC;
	}

	if (!priv->flowenabled) {
		pr_debug("CAIF: %s(): dropping packets flow off\n", __func__);
		return NETDEV_TX_BUSY;
	}

	if (priv->conn_req.protocol == CAIFPROTO_DATAGRAM_LOOP)
		swap(ip_hdr(skb)->saddr, ip_hdr(skb)->daddr);

	/* Store original SKB length. */
	len = skb->len;

	pkt = cfpkt_fromnative(CAIF_DIR_OUT, (void *) skb);

	/* Send the packet down the stack. */
	result = priv->chnl.dn->transmit(priv->chnl.dn, pkt);
	if (result) {
		if (result == -EAGAIN)
			result = NETDEV_TX_BUSY;
		return result;
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
	ASSERT_RTNL();
	priv = netdev_priv(dev);
	if (!priv) {
		pr_debug("CAIF: %s(): chnl_net_open: no priv\n", __func__);
		return -ENODEV;
	}

	if (priv->state != CAIF_CONNECTING) {
		priv->state = CAIF_CONNECTING;
		result = caif_connect_client(&priv->conn_req, &priv->chnl);
		if (result != 0) {
				priv->state = CAIF_DISCONNECTED;
				pr_debug("CAIF: %s(): err: "
					"Unable to register and open device,"
					" Err:%d\n",
					__func__,
					result);
				return result;
		}
	}

	result = wait_event_interruptible_timeout(priv->netmgmt_wq,
						priv->state != CAIF_CONNECTING,
						CONNECT_TIMEOUT);

	if (result == -ERESTARTSYS) {
		pr_debug("CAIF: %s(): wait_event_interruptible"
			 " woken by a signal\n", __func__);
		return -ERESTARTSYS;
	}
	if (result == 0) {
		pr_debug("CAIF: %s(): connect timeout\n", __func__);
		caif_disconnect_client(&priv->chnl);
		priv->state = CAIF_DISCONNECTED;
		pr_debug("CAIF: %s(): state disconnected\n", __func__);
		return -ETIMEDOUT;
	}

	if (priv->state != CAIF_CONNECTED) {
		pr_debug("CAIF: %s(): connect failed\n", __func__);
		return -ECONNREFUSED;
	}
	pr_debug("CAIF: %s(): CAIF Netdevice connected\n", __func__);
	return 0;
}

static int chnl_net_stop(struct net_device *dev)
{
	struct chnl_net *priv;

	ASSERT_RTNL();
	priv = netdev_priv(dev);
	priv->state = CAIF_DISCONNECTED;
	caif_disconnect_client(&priv->chnl);
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

static void ipcaif_net_setup(struct net_device *dev)
{
	struct chnl_net *priv;
	dev->netdev_ops = &netdev_ops;
	dev->destructor = free_netdev;
	dev->flags |= IFF_NOARP;
	dev->flags |= IFF_POINTOPOINT;
	dev->needed_headroom = CAIF_NEEDED_HEADROOM;
	dev->needed_tailroom = CAIF_NEEDED_TAILROOM;
	dev->mtu = SIZE_MTU;
	dev->tx_queue_len = CAIF_NET_DEFAULT_QUEUE_LEN;

	priv = netdev_priv(dev);
	priv->chnl.receive = chnl_recv_cb;
	priv->chnl.ctrlcmd = chnl_flowctrl_cb;
	priv->netdev = dev;
	priv->conn_req.protocol = CAIFPROTO_DATAGRAM;
	priv->conn_req.link_selector = CAIF_LINK_HIGH_BANDW;
	priv->conn_req.priority = CAIF_PRIO_LOW;
	/* Insert illegal value */
	priv->conn_req.sockaddr.u.dgm.connection_id = -1;
	priv->flowenabled = false;

	ASSERT_RTNL();
	init_waitqueue_head(&priv->netmgmt_wq);
	list_add(&priv->list_field, &chnl_net_list);
}


static int ipcaif_fill_info(struct sk_buff *skb, const struct net_device *dev)
{
	struct chnl_net *priv;
	u8 loop;
	priv = netdev_priv(dev);
	NLA_PUT_U32(skb, IFLA_CAIF_IPV4_CONNID,
		    priv->conn_req.sockaddr.u.dgm.connection_id);
	NLA_PUT_U32(skb, IFLA_CAIF_IPV6_CONNID,
		    priv->conn_req.sockaddr.u.dgm.connection_id);
	loop = priv->conn_req.protocol == CAIFPROTO_DATAGRAM_LOOP;
	NLA_PUT_U8(skb, IFLA_CAIF_LOOPBACK, loop);


	return 0;
nla_put_failure:
	return -EMSGSIZE;

}

static void caif_netlink_parms(struct nlattr *data[],
				struct caif_connect_request *conn_req)
{
	if (!data) {
		pr_warning("CAIF: %s: no params data found\n", __func__);
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
			  struct nlattr *tb[], struct nlattr *data[])
{
	int ret;
	struct chnl_net *caifdev;
	ASSERT_RTNL();
	caifdev = netdev_priv(dev);
	caif_netlink_parms(data, &caifdev->conn_req);
	dev_net_set(caifdev->netdev, src_net);

	ret = register_netdevice(dev);
	if (ret)
		pr_warning("CAIF: %s(): device rtml registration failed\n",
			   __func__);
	return ret;
}

static int ipcaif_changelink(struct net_device *dev, struct nlattr *tb[],
				struct nlattr *data[])
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
