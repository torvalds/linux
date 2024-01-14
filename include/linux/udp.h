/* SPDX-License-Identifier: GPL-2.0-or-later */
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

#define UDP_HTABLE_SIZE_MIN		(CONFIG_BASE_SMALL ? 128 : 256)

static inline u32 udp_hashfn(const struct net *net, u32 num, u32 mask)
{
	return (num + net_hash_mix(net)) & mask;
}

enum {
	UDP_FLAGS_CORK,		/* Cork is required */
	UDP_FLAGS_NO_CHECK6_TX, /* Send zero UDP6 checksums on TX? */
	UDP_FLAGS_NO_CHECK6_RX, /* Allow zero UDP6 checksums on RX? */
	UDP_FLAGS_GRO_ENABLED,	/* Request GRO aggregation */
	UDP_FLAGS_ACCEPT_FRAGLIST,
	UDP_FLAGS_ACCEPT_L4,
	UDP_FLAGS_ENCAP_ENABLED, /* This socket enabled encap */
};

struct udp_sock {
	/* inet_sock has to be the first member */
	struct inet_sock inet;
#define udp_port_hash		inet.sk.__sk_common.skc_u16hashes[0]
#define udp_portaddr_hash	inet.sk.__sk_common.skc_u16hashes[1]
#define udp_portaddr_node	inet.sk.__sk_common.skc_portaddr_node

	unsigned long	 udp_flags;

	int		 pending;	/* Any pending frames ? */
	__u8		 encap_type;	/* Is this an Encapsulation socket? */

/* indicator bits used by pcflag: */
#define UDPLITE_BIT      0x1  		/* set by udplite proto init function */
#define UDPLITE_SEND_CC  0x2  		/* set via udplite setsockopt         */
#define UDPLITE_RECV_CC  0x4		/* set via udplite setsocktopt        */
	__u8		 pcflag;        /* marks socket as UDP-Lite if > 0    */
	/*
	 * Following member retains the information to create a UDP header
	 * when the socket is uncorked.
	 */
	__u16		 len;		/* total length of pending frames */
	__u16		 gso_size;
	/*
	 * Fields specific to UDP-Lite.
	 */
	__u16		 pcslen;
	__u16		 pcrlen;
	/*
	 * For encapsulation sockets.
	 */
	int (*encap_rcv)(struct sock *sk, struct sk_buff *skb);
	void (*encap_err_rcv)(struct sock *sk, struct sk_buff *skb, unsigned int udp_offset);
	int (*encap_err_lookup)(struct sock *sk, struct sk_buff *skb);
	void (*encap_destroy)(struct sock *sk);

	/* GRO functions for UDP socket */
	struct sk_buff *	(*gro_receive)(struct sock *sk,
					       struct list_head *head,
					       struct sk_buff *skb);
	int			(*gro_complete)(struct sock *sk,
						struct sk_buff *skb,
						int nhoff);

	/* udp_recvmsg try to use this before splicing sk_receive_queue */
	struct sk_buff_head	reader_queue ____cacheline_aligned_in_smp;

	/* This field is dirtied by udp_recvmsg() */
	int		forward_deficit;
};

#define udp_test_bit(nr, sk)			\
	test_bit(UDP_FLAGS_##nr, &udp_sk(sk)->udp_flags)
#define udp_set_bit(nr, sk)			\
	set_bit(UDP_FLAGS_##nr, &udp_sk(sk)->udp_flags)
#define udp_test_and_set_bit(nr, sk)		\
	test_and_set_bit(UDP_FLAGS_##nr, &udp_sk(sk)->udp_flags)
#define udp_clear_bit(nr, sk)			\
	clear_bit(UDP_FLAGS_##nr, &udp_sk(sk)->udp_flags)
#define udp_assign_bit(nr, sk, val)		\
	assign_bit(UDP_FLAGS_##nr, &udp_sk(sk)->udp_flags, val)

#define UDP_MAX_SEGMENTS	(1 << 6UL)

static inline struct udp_sock *udp_sk(const struct sock *sk)
{
	return (struct udp_sock *)sk;
}

static inline void udp_set_no_check6_tx(struct sock *sk, bool val)
{
	udp_assign_bit(NO_CHECK6_TX, sk, val);
}

static inline void udp_set_no_check6_rx(struct sock *sk, bool val)
{
	udp_assign_bit(NO_CHECK6_RX, sk, val);
}

static inline bool udp_get_no_check6_tx(const struct sock *sk)
{
	return udp_test_bit(NO_CHECK6_TX, sk);
}

static inline bool udp_get_no_check6_rx(const struct sock *sk)
{
	return udp_test_bit(NO_CHECK6_RX, sk);
}

static inline void udp_cmsg_recv(struct msghdr *msg, struct sock *sk,
				 struct sk_buff *skb)
{
	int gso_size;

	if (skb_shinfo(skb)->gso_type & SKB_GSO_UDP_L4) {
		gso_size = skb_shinfo(skb)->gso_size;
		put_cmsg(msg, SOL_UDP, UDP_GRO, sizeof(gso_size), &gso_size);
	}
}

static inline bool udp_unexpected_gso(struct sock *sk, struct sk_buff *skb)
{
	if (!skb_is_gso(skb))
		return false;

	if (skb_shinfo(skb)->gso_type & SKB_GSO_UDP_L4 &&
	    !udp_test_bit(ACCEPT_L4, sk))
		return true;

	if (skb_shinfo(skb)->gso_type & SKB_GSO_FRAGLIST &&
	    !udp_test_bit(ACCEPT_FRAGLIST, sk))
		return true;

	return false;
}

static inline void udp_allow_gso(struct sock *sk)
{
	udp_set_bit(ACCEPT_L4, sk);
	udp_set_bit(ACCEPT_FRAGLIST, sk);
}

#define udp_portaddr_for_each_entry(__sk, list) \
	hlist_for_each_entry(__sk, list, __sk_common.skc_portaddr_node)

#define udp_portaddr_for_each_entry_rcu(__sk, list) \
	hlist_for_each_entry_rcu(__sk, list, __sk_common.skc_portaddr_node)

#define IS_UDPLITE(__sk) (__sk->sk_protocol == IPPROTO_UDPLITE)

#endif	/* _LINUX_UDP_H */
