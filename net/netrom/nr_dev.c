/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Copyright Jonathan Naylor G4KLX (g4klx@g4klx.demon.co.uk)
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/kernel.h>
#include <linux/sched.h>
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

#include <asm/system.h>
#include <asm/uaccess.h>
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
	struct net_device_stats *stats = netdev_priv(dev);

	if (!netif_running(dev)) {
		stats->rx_dropped++;
		return 0;
	}

	stats->rx_packets++;
	stats->rx_bytes += skb->len;

	skb->protocol = htons(ETH_P_IP);

	/* Spoof incoming device */
	skb->dev      = dev;
	skb->mac.raw  = skb->nh.raw;
	skb->nh.raw   = skb->data;
	skb->pkt_type = PACKET_HOST;

	netif_rx(skb);

	return 1;
}

#ifdef CONFIG_INET

static int nr_rebuild_header(struct sk_buff *skb)
{
	unsigned char *bp = skb->data;

	if (arp_find(bp + 7, skb))
		return 1;

	bp[6] &= ~AX25_CBIT;
	bp[6] &= ~AX25_EBIT;
	bp[6] |= AX25_SSSID_SPARE;
	bp    += AX25_ADDR_LEN;

	bp[6] &= ~AX25_CBIT;
	bp[6] |= AX25_EBIT;
	bp[6] |= AX25_SSSID_SPARE;

	return 0;
}

#else

static int nr_rebuild_header(struct sk_buff *skb)
{
	return 1;
}

#endif

static int nr_header(struct sk_buff *skb, struct net_device *dev, unsigned short type,
	void *daddr, void *saddr, unsigned len)
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

static int nr_set_mac_address(struct net_device *dev, void *addr)
{
	struct sockaddr *sa = addr;

	if (dev->flags & IFF_UP)
		ax25_listen_release((ax25_address *)dev->dev_addr, NULL);

	memcpy(dev->dev_addr, sa->sa_data, dev->addr_len);

	if (dev->flags & IFF_UP)
		ax25_listen_register((ax25_address *)dev->dev_addr, NULL);

	return 0;
}

static int nr_open(struct net_device *dev)
{
	netif_start_queue(dev);
	ax25_listen_register((ax25_address *)dev->dev_addr, NULL);
	return 0;
}

static int nr_close(struct net_device *dev)
{
	ax25_listen_release((ax25_address *)dev->dev_addr, NULL);
	netif_stop_queue(dev);
	return 0;
}

static int nr_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct nr_private *nr = netdev_priv(dev);
	struct net_device_stats *stats = &nr->stats;
	unsigned int len = skb->len;

	if (!nr_route_frame(skb, NULL)) {
		kfree_skb(skb);
		stats->tx_errors++;
		return 0;
	}

	stats->tx_packets++;
	stats->tx_bytes += len;

	return 0;
}

static struct net_device_stats *nr_get_stats(struct net_device *dev)
{
	struct nr_private *nr = netdev_priv(dev);

	return &nr->stats;
}

void nr_setup(struct net_device *dev)
{
	dev->mtu		= NR_MAX_PACKET_SIZE;
	dev->hard_start_xmit	= nr_xmit;
	dev->open		= nr_open;
	dev->stop		= nr_close;

	dev->hard_header	= nr_header;
	dev->hard_header_len	= NR_NETWORK_LEN + NR_TRANSPORT_LEN;
	dev->addr_len		= AX25_ADDR_LEN;
	dev->type		= ARPHRD_NETROM;
	dev->rebuild_header	= nr_rebuild_header;
	dev->set_mac_address    = nr_set_mac_address;

	/* New-style flags. */
	dev->flags		= IFF_NOARP;

	dev->get_stats 		= nr_get_stats;
}
