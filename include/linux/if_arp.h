/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Global definitions for the ARP (RFC 826) protocol.
 *
 * Version:	@(#)if_arp.h	1.0.1	04/16/93
 *
 * Authors:	Original taken from Berkeley UNIX 4.3, (c) UCB 1986-1988
 *		Portions taken from the KA9Q/NOS (v2.00m PA0GRI) source.
 *		Ross Biro
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Florian La Roche,
 *		Jonathan Layes <layes@loran.com>
 *		Arnaldo Carvalho de Melo <acme@conectiva.com.br> ARPHRD_HWX25
 */
#ifndef _LINUX_IF_ARP_H
#define _LINUX_IF_ARP_H

#include <linux/skbuff.h>
#include <uapi/linux/if_arp.h>

static inline struct arphdr *arp_hdr(const struct sk_buff *skb)
{
	return (struct arphdr *)skb_network_header(skb);
}

static inline unsigned int arp_hdr_len(const struct net_device *dev)
{
	switch (dev->type) {
#if IS_ENABLED(CONFIG_FIREWIRE_NET)
	case ARPHRD_IEEE1394:
		/* ARP header, device address and 2 IP addresses */
		return sizeof(struct arphdr) + dev->addr_len + sizeof(u32) * 2;
#endif
	default:
		/* ARP header, plus 2 device addresses, plus 2 IP addresses. */
		return sizeof(struct arphdr) + (dev->addr_len + sizeof(u32)) * 2;
	}
}

static inline bool dev_is_mac_header_xmit(const struct net_device *dev)
{
	switch (dev->type) {
	case ARPHRD_TUNNEL:
	case ARPHRD_TUNNEL6:
	case ARPHRD_SIT:
	case ARPHRD_IPGRE:
	case ARPHRD_IP6GRE:
	case ARPHRD_VOID:
	case ARPHRD_NONE:
	case ARPHRD_RAWIP:
	case ARPHRD_PIMREG:
	/* PPP adds its l2 header automatically in ppp_start_xmit().
	 * This makes it look like an l3 device to __bpf_redirect() and tcf_mirred_init().
	 */
	case ARPHRD_PPP:
		return false;
	default:
		return true;
	}
}

#endif	/* _LINUX_IF_ARP_H */
