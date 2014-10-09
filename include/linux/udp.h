/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the UDP protocol.
 *
 * Version:	@(#)udp.h	1.0.2	04/28/93
 *
 * Author:	Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _LINUX_UDP_H
#define _LINUX_UDP_H

#include <net/inet_sock.h>
#include <linux/skbuff.h>
#include <net/netns/hash.h>
#include <uapi/linux/udp.h>

static inline struct udphdr *udp_hdr(const struct sk_buff *skb)
{
	return (struct udphdr *)skb_transport_header(skb);
}

static inline struct udphdr *inner_udp_hdr(const struct sk_buff *skb)
{
	return (struct udphdr *)skb_inner_transport_header(skb);
}

#define UDP_HTABLE_SIZE_MIN		(CONFIG_BASE_SMALL ? 128 : 256)

static inline int udp_hashfn(struct net *net, unsigned num, unsigned mask)
{
	return (num + net_hash_mix(net)) & mask;
}

struct udp_sock {
	/* inet_sock has to be the first member */
	struct inet_sock inet;
#define udp_port_hash		inet.sk.__sk_common.skc_u16hashes[0]
#define udp_portaddr_hash	inet.sk.__sk_common.skc_u16hashes[1]
#define udp_portaddr_node	inet.sk.__sk_common.skc_portaddr_node
	int		 pending;	/* Any pending frames ? */
	unsigned int	 corkflag;	/* Cork is required */
	__u8		 encap_type;	/* Is this an Encapsulation socket? */
	unsigned char	 no_check6_tx:1,/* Send zero UDP6 checksums on TX? */
			 no_check6_rx:1,/* Allow zero UDP6 checksums on RX? */
			 convert_csum:1;/* On receive, convert checksum
					 * unnecessary to checksum complete
					 * if possible.
					 */
	/*
	 * Following member retains the information to create a UDP header
	 * when the socket is uncorked.
	 */
	__u16		 len;		/* total length of pending frames */
	/*
	 * Fields specific to UDP-Lite.
	 */
	__u16		 pcslen;
	__u16		 pcrlen;
/* indicator bits used by pcflag: */
#define UDPLITE_BIT      0x1  		/* set by udplite proto init function */
#define UDPLITE_SEND_CC  0x2  		/* set via udplite setsockopt         */
#define UDPLITE_RECV_CC  0x4		/* set via udplite setsocktopt        */
	__u8		 pcflag;        /* marks socket as UDP-Lite if > 0    */
	__u8		 unused[3];
	/*
	 * For encapsulation sockets.
	 */
	int (*encap_rcv)(struct sock *sk, struct sk_buff *skb);
	void (*encap_destroy)(struct sock *sk);
};

static inline struct udp_sock *udp_sk(const struct sock *sk)
{
	return (struct udp_sock *)sk;
}

static inline void udp_set_no_check6_tx(struct sock *sk, bool val)
{
	udp_sk(sk)->no_check6_tx = val;
}

static inline void udp_set_no_check6_rx(struct sock *sk, bool val)
{
	udp_sk(sk)->no_check6_rx = val;
}

static inline bool udp_get_no_check6_tx(struct sock *sk)
{
	return udp_sk(sk)->no_check6_tx;
}

static inline bool udp_get_no_check6_rx(struct sock *sk)
{
	return udp_sk(sk)->no_check6_rx;
}

static inline void udp_set_convert_csum(struct sock *sk, bool val)
{
	udp_sk(sk)->convert_csum = val;
}

static inline bool udp_get_convert_csum(struct sock *sk)
{
	return udp_sk(sk)->convert_csum;
}

#define udp_portaddr_for_each_entry(__sk, node, list) \
	hlist_nulls_for_each_entry(__sk, node, list, __sk_common.skc_portaddr_node)

#define udp_portaddr_for_each_entry_rcu(__sk, node, list) \
	hlist_nulls_for_each_entry_rcu(__sk, node, list, __sk_common.skc_portaddr_node)

#define IS_UDPLITE(__sk) (udp_sk(__sk)->pcflag)

#endif	/* _LINUX_UDP_H */
