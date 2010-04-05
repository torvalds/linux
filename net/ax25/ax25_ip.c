/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Copyright (C) Jonathan Naylor G4KLX (g4klx@g4klx.demon.co.uk)
 */
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/slab.h>
#include <net/ax25.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/termios.h>	/* For TIOCINQ/OUTQ */
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/netfilter.h>
#include <linux/sysctl.h>
#include <net/ip.h>
#include <net/arp.h>

/*
 *	IP over AX.25 encapsulation.
 */

/*
 *	Shove an AX.25 UI header on an IP packet and handle ARP
 */

#ifdef CONFIG_INET

int ax25_hard_header(struct sk_buff *skb, struct net_device *dev,
		     unsigned short type, const void *daddr,
		     const void *saddr, unsigned len)
{
	unsigned char *buff;

	/* they sometimes come back to us... */
	if (type == ETH_P_AX25)
		return 0;

	/* header is an AX.25 UI frame from us to them */
	buff = skb_push(skb, AX25_HEADER_LEN);
	*buff++ = 0x00;	/* KISS DATA */

	if (daddr != NULL)
		memcpy(buff, daddr, dev->addr_len);	/* Address specified */

	buff[6] &= ~AX25_CBIT;
	buff[6] &= ~AX25_EBIT;
	buff[6] |= AX25_SSSID_SPARE;
	buff    += AX25_ADDR_LEN;

	if (saddr != NULL)
		memcpy(buff, saddr, dev->addr_len);
	else
		memcpy(buff, dev->dev_addr, dev->addr_len);

	buff[6] &= ~AX25_CBIT;
	buff[6] |= AX25_EBIT;
	buff[6] |= AX25_SSSID_SPARE;
	buff    += AX25_ADDR_LEN;

	*buff++  = AX25_UI;	/* UI */

	/* Append a suitable AX.25 PID */
	switch (type) {
	case ETH_P_IP:
		*buff++ = AX25_P_IP;
		break;
	case ETH_P_ARP:
		*buff++ = AX25_P_ARP;
		break;
	default:
		printk(KERN_ERR "AX.25: ax25_hard_header - wrong protocol type 0x%2.2x\n", type);
		*buff++ = 0;
		break;
	}

	if (daddr != NULL)
		return AX25_HEADER_LEN;

	return -AX25_HEADER_LEN;	/* Unfinished header */
}

int ax25_rebuild_header(struct sk_buff *skb)
{
	struct sk_buff *ourskb;
	unsigned char *bp  = skb->data;
	ax25_route *route;
	struct net_device *dev = NULL;
	ax25_address *src, *dst;
	ax25_digi *digipeat = NULL;
	ax25_dev *ax25_dev;
	ax25_cb *ax25;
	char ip_mode = ' ';

	dst = (ax25_address *)(bp + 1);
	src = (ax25_address *)(bp + 8);

	if (arp_find(bp + 1, skb))
		return 1;

	route = ax25_get_route(dst, NULL);
	if (route) {
		digipeat = route->digipeat;
		dev = route->dev;
		ip_mode = route->ip_mode;
	}

	if (dev == NULL)
		dev = skb->dev;

	if ((ax25_dev = ax25_dev_ax25dev(dev)) == NULL) {
		goto put;
	}

	if (bp[16] == AX25_P_IP) {
		if (ip_mode == 'V' || (ip_mode == ' ' && ax25_dev->values[AX25_VALUES_IPDEFMODE])) {
			/*
			 *	We copy the buffer and release the original thereby
			 *	keeping it straight
			 *
			 *	Note: we report 1 back so the caller will
			 *	not feed the frame direct to the physical device
			 *	We don't want that to happen. (It won't be upset
			 *	as we have pulled the frame from the queue by
			 *	freeing it).
			 *
			 *	NB: TCP modifies buffers that are still
			 *	on a device queue, thus we use skb_copy()
			 *      instead of using skb_clone() unless this
			 *	gets fixed.
			 */

			ax25_address src_c;
			ax25_address dst_c;

			if ((ourskb = skb_copy(skb, GFP_ATOMIC)) == NULL) {
				kfree_skb(skb);
				goto put;
			}

			if (skb->sk != NULL)
				skb_set_owner_w(ourskb, skb->sk);

			kfree_skb(skb);
			/* dl9sau: bugfix
			 * after kfree_skb(), dst and src which were pointer
			 * to bp which is part of skb->data would not be valid
			 * anymore hope that after skb_pull(ourskb, ..) our
			 * dsc_c and src_c will not become invalid
			 */
			bp  = ourskb->data;
			dst_c = *(ax25_address *)(bp + 1);
			src_c = *(ax25_address *)(bp + 8);

			skb_pull(ourskb, AX25_HEADER_LEN - 1);	/* Keep PID */
			skb_reset_network_header(ourskb);

			ax25=ax25_send_frame(
			    ourskb,
			    ax25_dev->values[AX25_VALUES_PACLEN],
			    &src_c,
			    &dst_c, digipeat, dev);
			if (ax25) {
				ax25_cb_put(ax25);
			}
			goto put;
		}
	}

	bp[7]  &= ~AX25_CBIT;
	bp[7]  &= ~AX25_EBIT;
	bp[7]  |= AX25_SSSID_SPARE;

	bp[14] &= ~AX25_CBIT;
	bp[14] |= AX25_EBIT;
	bp[14] |= AX25_SSSID_SPARE;

	skb_pull(skb, AX25_KISS_HEADER_LEN);

	if (digipeat != NULL) {
		if ((ourskb = ax25_rt_build_path(skb, src, dst, route->digipeat)) == NULL) {
			kfree_skb(skb);
			goto put;
		}

		skb = ourskb;
	}

	ax25_queue_xmit(skb, dev);

put:
	if (route)
		ax25_put_route(route);

	return 1;
}

#else	/* INET */

int ax25_hard_header(struct sk_buff *skb, struct net_device *dev,
		     unsigned short type, const void *daddr,
		     const void *saddr, unsigned len)
{
	return -AX25_HEADER_LEN;
}

int ax25_rebuild_header(struct sk_buff *skb)
{
	return 1;
}

#endif

const struct header_ops ax25_header_ops = {
	.create = ax25_hard_header,
	.rebuild = ax25_rebuild_header,
};

EXPORT_SYMBOL(ax25_hard_header);
EXPORT_SYMBOL(ax25_rebuild_header);
EXPORT_SYMBOL(ax25_header_ops);

