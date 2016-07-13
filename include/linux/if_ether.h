/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Global definitions for the Ethernet IEEE 802.3 interface.
 *
 * Version:	@(#)if_ether.h	1.0.1a	02/08/94
 *
 * Author:	Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Donald Becker, <becker@super.org>
 *		Alan Cox, <alan@lxorguk.ukuu.org.uk>
 *		Steve Whitehouse, <gw7rrm@eeshack3.swan.ac.uk>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _LINUX_IF_ETHER_H
#define _LINUX_IF_ETHER_H

#include <linux/skbuff.h>
#include <uapi/linux/if_ether.h>

static inline struct ethhdr *eth_hdr(const struct sk_buff *skb)
{
	return (struct ethhdr *)skb_mac_header(skb);
}

static inline struct ethhdr *inner_eth_hdr(const struct sk_buff *skb)
{
	return (struct ethhdr *)skb_inner_mac_header(skb);
}

int eth_header_parse(const struct sk_buff *skb, unsigned char *haddr);

extern ssize_t sysfs_format_mac(char *buf, const unsigned char *addr, int len);

#endif	/* _LINUX_IF_ETHER_H */
