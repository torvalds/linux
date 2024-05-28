// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		FDDI-type device handling.
 *
 * Version:	@(#)fddi.c	1.0.0	08/12/96
 *
 * Authors:	Lawrence V. Stefani, <stefani@lkg.dec.com>
 *
 *		fddi.c is based on previous eth.c and tr.c work by
 *			Ross Biro
 *			Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *			Mark Evans, <evansmp@uhura.aston.ac.uk>
 *			Florian La Roche, <rzsfl@rz.uni-sb.de>
 *			Alan Cox, <gw4pts@gw4pts.ampr.org>
 *
 *	Changes
 *		Alan Cox		:	New arp/rebuild header
 *		Maciej W. Rozycki	:	IPv6 support
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/fddidevice.h>
#include <linux/if_ether.h>
#include <linux/skbuff.h>
#include <linux/errno.h>
#include <net/arp.h>
#include <net/sock.h>

/*
 * Create the FDDI MAC header for an arbitrary protocol layer
 *
 * saddr=NULL	means use device source address
 * daddr=NULL	means leave destination address (eg unresolved arp)
 */

static int fddi_header(struct sk_buff *skb, struct net_device *dev,
		       unsigned short type,
		       const void *daddr, const void *saddr, unsigned int len)
{
	int hl = FDDI_K_SNAP_HLEN;
	struct fddihdr *fddi;

	if(type != ETH_P_IP && type != ETH_P_IPV6 && type != ETH_P_ARP)
		hl=FDDI_K_8022_HLEN-3;
	fddi = skb_push(skb, hl);
	fddi->fc			 = FDDI_FC_K_ASYNC_LLC_DEF;
	if(type == ETH_P_IP || type == ETH_P_IPV6 || type == ETH_P_ARP)
	{
		fddi->hdr.llc_snap.dsap		 = FDDI_EXTENDED_SAP;
		fddi->hdr.llc_snap.ssap		 = FDDI_EXTENDED_SAP;
		fddi->hdr.llc_snap.ctrl		 = FDDI_UI_CMD;
		fddi->hdr.llc_snap.oui[0]	 = 0x00;
		fddi->hdr.llc_snap.oui[1]	 = 0x00;
		fddi->hdr.llc_snap.oui[2]	 = 0x00;
		fddi->hdr.llc_snap.ethertype	 = htons(type);
	}

	/* Set the source and destination hardware addresses */

	if (saddr != NULL)
		memcpy(fddi->saddr, saddr, dev->addr_len);
	else
		memcpy(fddi->saddr, dev->dev_addr, dev->addr_len);

	if (daddr != NULL)
	{
		memcpy(fddi->daddr, daddr, dev->addr_len);
		return hl;
	}

	return -hl;
}

/*
 * Determine the packet's protocol ID and fill in skb fields.
 * This routine is called before an incoming packet is passed
 * up.  It's used to fill in specific skb fields and to set
 * the proper pointer to the start of packet data (skb->data).
 */

__be16 fddi_type_trans(struct sk_buff *skb, struct net_device *dev)
{
	struct fddihdr *fddi = (struct fddihdr *)skb->data;
	__be16 type;

	/*
	 * Set mac.raw field to point to FC byte, set data field to point
	 * to start of packet data.  Assume 802.2 SNAP frames for now.
	 */

	skb->dev = dev;
	skb_reset_mac_header(skb);	/* point to frame control (FC) */

	if(fddi->hdr.llc_8022_1.dsap==0xe0)
	{
		skb_pull(skb, FDDI_K_8022_HLEN-3);
		type = htons(ETH_P_802_2);
	}
	else
	{
		skb_pull(skb, FDDI_K_SNAP_HLEN);		/* adjust for 21 byte header */
		type=fddi->hdr.llc_snap.ethertype;
	}

	/* Set packet type based on destination address and flag settings */

	if (*fddi->daddr & 0x01)
	{
		if (memcmp(fddi->daddr, dev->broadcast, FDDI_K_ALEN) == 0)
			skb->pkt_type = PACKET_BROADCAST;
		else
			skb->pkt_type = PACKET_MULTICAST;
	}

	else if (dev->flags & IFF_PROMISC)
	{
		if (memcmp(fddi->daddr, dev->dev_addr, FDDI_K_ALEN))
			skb->pkt_type = PACKET_OTHERHOST;
	}

	/* Assume 802.2 SNAP frames, for now */

	return type;
}

EXPORT_SYMBOL(fddi_type_trans);

static const struct header_ops fddi_header_ops = {
	.create		= fddi_header,
};


static void fddi_setup(struct net_device *dev)
{
	dev->header_ops		= &fddi_header_ops;
	dev->type		= ARPHRD_FDDI;
	dev->hard_header_len	= FDDI_K_SNAP_HLEN+3;	/* Assume 802.2 SNAP hdr len + 3 pad bytes */
	dev->mtu		= FDDI_K_SNAP_DLEN;	/* Assume max payload of 802.2 SNAP frame */
	dev->min_mtu		= FDDI_K_SNAP_HLEN;
	dev->max_mtu		= FDDI_K_SNAP_DLEN;
	dev->addr_len		= FDDI_K_ALEN;
	dev->tx_queue_len	= 100;			/* Long queues on FDDI */
	dev->flags		= IFF_BROADCAST | IFF_MULTICAST;

	memset(dev->broadcast, 0xFF, FDDI_K_ALEN);
}

/**
 * alloc_fddidev - Register FDDI device
 * @sizeof_priv: Size of additional driver-private structure to be allocated
 *	for this FDDI device
 *
 * Fill in the fields of the device structure with FDDI-generic values.
 *
 * Constructs a new net device, complete with a private data area of
 * size @sizeof_priv.  A 32-byte (not bit) alignment is enforced for
 * this private data area.
 */
struct net_device *alloc_fddidev(int sizeof_priv)
{
	return alloc_netdev(sizeof_priv, "fddi%d", NET_NAME_UNKNOWN,
			    fddi_setup);
}
EXPORT_SYMBOL(alloc_fddidev);

MODULE_DESCRIPTION("Core routines for FDDI network devices");
MODULE_LICENSE("GPL");
