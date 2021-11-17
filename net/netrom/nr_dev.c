// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 * Copyright Jonathan Naylor G4KLX (g4klx@g4klx.demon.co.uk)
 */
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/sysctl.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/in.h>
#include <linux/if_ether.h>	/* For the statistics structure. */
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <asm/io.h>

#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>

#include <net/ip.h>
#include <net/arp.h>

#include <net/ax25.h>
#include <net/netrom.h>

/*
 *	Only allow IP over NET/ROM frames through if the netrom device is up.
 */

int nr_rx_ip(struct sk_buff *skb, struct net_device *dev)
{
	struct net_device_stats *stats = &dev->stats;

	if (!netif_running(dev)) {
		stats->rx_dropped++;
		return 0;
	}

	stats->rx_packets++;
	stats->rx_bytes += skb->len;

	skb->protocol = htons(ETH_P_IP);

	/* Spoof incoming device */
	skb->dev      = dev;
	skb->mac_header = skb->network_header;
	skb_reset_network_header(skb);
	skb->pkt_type = PACKET_HOST;

	netif_rx(skb);

	return 1;
}

static int nr_header(struct sk_buff *skb, struct net_device *dev,
		     unsigned short type,
		     const void *daddr, const void *saddr, unsigned int len)
{
	unsigned char *buff = skb_push(skb, NR_NETWORK_LEN + NR_TRANSPORT_LEN);

	memcpy(buff, (saddr != NULL) ? saddr : dev->dev_addr, dev->addr_len);
	buff[6] &= ~AX25_CBIT;
	buff[6] &= ~AX25_EBIT;
	buff[6] |= AX25_SSSID_SPARE;
	buff    += AX25_ADDR_LEN;

	if (daddr != NULL)
		memcpy(buff, daddr, dev->addr_len);
	buff[6] &= ~AX25_CBIT;
	buff[6] |= AX25_EBIT;
	buff[6] |= AX25_SSSID_SPARE;
	buff    += AX25_ADDR_LEN;

	*buff++ = sysctl_netrom_network_ttl_initialiser;

	*buff++ = NR_PROTO_IP;
	*buff++ = NR_PROTO_IP;
	*buff++ = 0;
	*buff++ = 0;
	*buff++ = NR_PROTOEXT;

	if (daddr != NULL)
		return 37;

	return -37;
}

static int __must_check nr_set_mac_address(struct net_device *dev, void *addr)
{
	struct sockaddr *sa = addr;
	int err;

	if (!memcmp(dev->dev_addr, sa->sa_data, dev->addr_len))
		return 0;

	if (dev->flags & IFF_UP) {
		err = ax25_listen_register((ax25_address *)sa->sa_data, NULL);
		if (err)
			return err;

		ax25_listen_release((ax25_address *)dev->dev_addr, NULL);
	}

	memcpy(dev->dev_addr, sa->sa_data, dev->addr_len);

	return 0;
}

static int nr_open(struct net_device *dev)
{
	int err;

	err = ax25_listen_register((ax25_address *)dev->dev_addr, NULL);
	if (err)
		return err;

	netif_start_queue(dev);

	return 0;
}

static int nr_close(struct net_device *dev)
{
	ax25_listen_release((ax25_address *)dev->dev_addr, NULL);
	netif_stop_queue(dev);
	return 0;
}

static netdev_tx_t nr_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct net_device_stats *stats = &dev->stats;
	unsigned int len = skb->len;

	if (!nr_route_frame(skb, NULL)) {
		kfree_skb(skb);
		stats->tx_errors++;
		return NETDEV_TX_OK;
	}

	stats->tx_packets++;
	stats->tx_bytes += len;

	return NETDEV_TX_OK;
}

static const struct header_ops nr_header_ops = {
	.create	= nr_header,
};

static const struct net_device_ops nr_netdev_ops = {
	.ndo_open		= nr_open,
	.ndo_stop		= nr_close,
	.ndo_start_xmit		= nr_xmit,
	.ndo_set_mac_address    = nr_set_mac_address,
};

void nr_setup(struct net_device *dev)
{
	dev->mtu		= NR_MAX_PACKET_SIZE;
	dev->netdev_ops		= &nr_netdev_ops;
	dev->header_ops		= &nr_header_ops;
	dev->hard_header_len	= NR_NETWORK_LEN + NR_TRANSPORT_LEN;
	dev->addr_len		= AX25_ADDR_LEN;
	dev->type		= ARPHRD_NETROM;

	/* New-style flags. */
	dev->flags		= IFF_NOARP;
}
