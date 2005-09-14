/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the IP protocol.
 *
 * Version:	@(#)ip.h	1.0.2	04/28/93
 *
 * Authors:	Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _LINUX_IP_H
#define _LINUX_IP_H
#include <asm/byteorder.h>

#define IPTOS_TOS_MASK		0x1E
#define IPTOS_TOS(tos)		((tos)&IPTOS_TOS_MASK)
#define	IPTOS_LOWDELAY		0x10
#define	IPTOS_THROUGHPUT	0x08
#define	IPTOS_RELIABILITY	0x04
#define	IPTOS_MINCOST		0x02

#define IPTOS_PREC_MASK		0xE0
#define IPTOS_PREC(tos)		((tos)&IPTOS_PREC_MASK)
#define IPTOS_PREC_NETCONTROL           0xe0
#define IPTOS_PREC_INTERNETCONTROL      0xc0
#define IPTOS_PREC_CRITIC_ECP           0xa0
#define IPTOS_PREC_FLASHOVERRIDE        0x80
#define IPTOS_PREC_FLASH                0x60
#define IPTOS_PREC_IMMEDIATE            0x40
#define IPTOS_PREC_PRIORITY             0x20
#define IPTOS_PREC_ROUTINE              0x00


/* IP options */
#define IPOPT_COPY		0x80
#define IPOPT_CLASS_MASK	0x60
#define IPOPT_NUMBER_MASK	0x1f

#define	IPOPT_COPIED(o)		((o)&IPOPT_COPY)
#define	IPOPT_CLASS(o)		((o)&IPOPT_CLASS_MASK)
#define	IPOPT_NUMBER(o)		((o)&IPOPT_NUMBER_MASK)

#define	IPOPT_CONTROL		0x00
#define	IPOPT_RESERVED1		0x20
#define	IPOPT_MEASUREMENT	0x40
#define	IPOPT_RESERVED2		0x60

#define IPOPT_END	(0 |IPOPT_CONTROL)
#define IPOPT_NOOP	(1 |IPOPT_CONTROL)
#define IPOPT_SEC	(2 |IPOPT_CONTROL|IPOPT_COPY)
#define IPOPT_LSRR	(3 |IPOPT_CONTROL|IPOPT_COPY)
#define IPOPT_TIMESTAMP	(4 |IPOPT_MEASUREMENT)
#define IPOPT_RR	(7 |IPOPT_CONTROL)
#define IPOPT_SID	(8 |IPOPT_CONTROL|IPOPT_COPY)
#define IPOPT_SSRR	(9 |IPOPT_CONTROL|IPOPT_COPY)
#define IPOPT_RA	(20|IPOPT_CONTROL|IPOPT_COPY)

#define IPVERSION	4
#define MAXTTL		255
#define IPDEFTTL	64

#define IPOPT_OPTVAL 0
#define IPOPT_OLEN   1
#define IPOPT_OFFSET 2
#define IPOPT_MINOFF 4
#define MAX_IPOPTLEN 40
#define IPOPT_NOP IPOPT_NOOP
#define IPOPT_EOL IPOPT_END
#define IPOPT_TS  IPOPT_TIMESTAMP

#define	IPOPT_TS_TSONLY		0		/* timestamps only */
#define	IPOPT_TS_TSANDADDR	1		/* timestamps and addresses */
#define	IPOPT_TS_PRESPEC	3		/* specified modules only */

#ifdef __KERNEL__
#include <linux/config.h>
#include <linux/types.h>
#include <net/request_sock.h>
#include <net/sock.h>
#include <linux/igmp.h>
#include <net/flow.h>

struct ip_options {
  __u32		faddr;				/* Saved first hop address */
  unsigned char	optlen;
  unsigned char srr;
  unsigned char rr;
  unsigned char ts;
  unsigned char is_setbyuser:1,			/* Set by setsockopt?			*/
                is_data:1,			/* Options in __data, rather than skb	*/
                is_strictroute:1,		/* Strict source route			*/
                srr_is_hit:1,			/* Packet destination addr was our one	*/
                is_changed:1,			/* IP checksum more not valid		*/	
                rr_needaddr:1,			/* Need to record addr of outgoing dev	*/
                ts_needtime:1,			/* Need to record timestamp		*/
                ts_needaddr:1;			/* Need to record addr of outgoing dev  */
  unsigned char router_alert;
  unsigned char __pad1;
  unsigned char __pad2;
  unsigned char __data[0];
};

#define optlength(opt) (sizeof(struct ip_options) + opt->optlen)

struct inet_request_sock {
	struct request_sock	req;
	u32			loc_addr;
	u32			rmt_addr;
	u16			rmt_port;
	u16			snd_wscale : 4, 
				rcv_wscale : 4, 
				tstamp_ok  : 1,
				sack_ok	   : 1,
				wscale_ok  : 1,
				ecn_ok	   : 1,
				acked	   : 1;
	struct ip_options	*opt;
};

static inline struct inet_request_sock *inet_rsk(const struct request_sock *sk)
{
	return (struct inet_request_sock *)sk;
}

struct ipv6_pinfo;

struct inet_sock {
	/* sk and pinet6 has to be the first two members of inet_sock */
	struct sock		sk;
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	struct ipv6_pinfo	*pinet6;
#endif
	/* Socket demultiplex comparisons on incoming packets. */
	__u32			daddr;		/* Foreign IPv4 addr */
	__u32			rcv_saddr;	/* Bound local IPv4 addr */
	__u16			dport;		/* Destination port */
	__u16			num;		/* Local port */
	__u32			saddr;		/* Sending source */
	__s16			uc_ttl;		/* Unicast TTL */
	__u16			cmsg_flags;
	struct ip_options	*opt;
	__u16			sport;		/* Source port */
	__u16			id;		/* ID counter for DF pkts */
	__u8			tos;		/* TOS */
	__u8			mc_ttl;		/* Multicasting TTL */
	__u8			pmtudisc;
	unsigned		recverr : 1,
				freebind : 1,
				hdrincl : 1,
				mc_loop : 1;
	int			mc_index;	/* Multicast device index */
	__u32			mc_addr;
	struct ip_mc_socklist	*mc_list;	/* Group array */
	/*
	 * Following members are used to retain the infomation to build
	 * an ip header on each ip fragmentation while the socket is corked.
	 */
	struct {
		unsigned int		flags;
		unsigned int		fragsize;
		struct ip_options	*opt;
		struct rtable		*rt;
		int			length; /* Total length of all frames */
		u32			addr;
		struct flowi		fl;
	} cork;
};

#define IPCORK_OPT	1	/* ip-options has been held in ipcork.opt */
#define IPCORK_ALLFRAG	2	/* always fragment (for ipv6 for now) */

static inline struct inet_sock *inet_sk(const struct sock *sk)
{
	return (struct inet_sock *)sk;
}

static inline void __inet_sk_copy_descendant(struct sock *sk_to,
					     const struct sock *sk_from,
					     const int ancestor_size)
{
	memcpy(inet_sk(sk_to) + 1, inet_sk(sk_from) + 1,
	       sk_from->sk_prot->obj_size - ancestor_size);
}
#if !(defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE))
static inline void inet_sk_copy_descendant(struct sock *sk_to,
					   const struct sock *sk_from)
{
	__inet_sk_copy_descendant(sk_to, sk_from, sizeof(struct inet_sock));
}
#endif
#endif

extern int inet_sk_rebuild_header(struct sock *sk);

struct iphdr {
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8	ihl:4,
		version:4;
#elif defined (__BIG_ENDIAN_BITFIELD)
	__u8	version:4,
  		ihl:4;
#else
#error	"Please fix <asm/byteorder.h>"
#endif
	__u8	tos;
	__u16	tot_len;
	__u16	id;
	__u16	frag_off;
	__u8	ttl;
	__u8	protocol;
	__u16	check;
	__u32	saddr;
	__u32	daddr;
	/*The options start here. */
};

struct ip_auth_hdr {
	__u8  nexthdr;
	__u8  hdrlen;		/* This one is measured in 32 bit units! */
	__u16 reserved;
	__u32 spi;
	__u32 seq_no;		/* Sequence number */
	__u8  auth_data[0];	/* Variable len but >=4. Mind the 64 bit alignment! */
};

struct ip_esp_hdr {
	__u32 spi;
	__u32 seq_no;		/* Sequence number */
	__u8  enc_data[0];	/* Variable len but >=8. Mind the 64 bit alignment! */
};

struct ip_comp_hdr {
	__u8 nexthdr;
	__u8 flags;
	__u16 cpi;
};

#endif	/* _LINUX_IP_H */
