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
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _LINUX_IF_ARP_H
#define _LINUX_IF_ARP_H

#include <linux/netdevice.h>

/* ARP protocol HARDWARE identifiers. */
#define ARPHRD_NETROM	0		/* from KA9Q: NET/ROM pseudo	*/
#define ARPHRD_ETHER 	1		/* Ethernet 10Mbps		*/
#define	ARPHRD_EETHER	2		/* Experimental Ethernet	*/
#define	ARPHRD_AX25	3		/* AX.25 Level 2		*/
#define	ARPHRD_PRONET	4		/* PROnet token ring		*/
#define	ARPHRD_CHAOS	5		/* Chaosnet			*/
#define	ARPHRD_IEEE802	6		/* IEEE 802.2 Ethernet/TR/TB	*/
#define	ARPHRD_ARCNET	7		/* ARCnet			*/
#define	ARPHRD_APPLETLK	8		/* APPLEtalk			*/
#define ARPHRD_DLCI	15		/* Frame Relay DLCI		*/
#define ARPHRD_ATM	19		/* ATM 				*/
#define ARPHRD_METRICOM	23		/* Metricom STRIP (new IANA id)	*/
#define	ARPHRD_IEEE1394	24		/* IEEE 1394 IPv4 - RFC 2734	*/
#define ARPHRD_EUI64	27		/* EUI-64                       */
#define ARPHRD_INFINIBAND 32		/* InfiniBand			*/

/* Dummy types for non ARP hardware */
#define ARPHRD_SLIP	256
#define ARPHRD_CSLIP	257
#define ARPHRD_SLIP6	258
#define ARPHRD_CSLIP6	259
#define ARPHRD_RSRVD	260		/* Notional KISS type 		*/
#define ARPHRD_ADAPT	264
#define ARPHRD_ROSE	270
#define ARPHRD_X25	271		/* CCITT X.25			*/
#define ARPHRD_HWX25	272		/* Boards with X.25 in firmware	*/
#define ARPHRD_CAN	280		/* Controller Area Network      */
#define ARPHRD_PPP	512
#define ARPHRD_CISCO	513		/* Cisco HDLC	 		*/
#define ARPHRD_HDLC	ARPHRD_CISCO
#define ARPHRD_LAPB	516		/* LAPB				*/
#define ARPHRD_DDCMP    517		/* Digital's DDCMP protocol     */
#define ARPHRD_RAWHDLC	518		/* Raw HDLC			*/

#define ARPHRD_TUNNEL	768		/* IPIP tunnel			*/
#define ARPHRD_TUNNEL6	769		/* IP6IP6 tunnel       		*/
#define ARPHRD_FRAD	770             /* Frame Relay Access Device    */
#define ARPHRD_SKIP	771		/* SKIP vif			*/
#define ARPHRD_LOOPBACK	772		/* Loopback device		*/
#define ARPHRD_LOCALTLK 773		/* Localtalk device		*/
#define ARPHRD_FDDI	774		/* Fiber Distributed Data Interface */
#define ARPHRD_BIF      775             /* AP1000 BIF                   */
#define ARPHRD_SIT	776		/* sit0 device - IPv6-in-IPv4	*/
#define ARPHRD_IPDDP	777		/* IP over DDP tunneller	*/
#define ARPHRD_IPGRE	778		/* GRE over IP			*/
#define ARPHRD_PIMREG	779		/* PIMSM register interface	*/
#define ARPHRD_HIPPI	780		/* High Performance Parallel Interface */
#define ARPHRD_ASH	781		/* Nexus 64Mbps Ash		*/
#define ARPHRD_ECONET	782		/* Acorn Econet			*/
#define ARPHRD_IRDA 	783		/* Linux-IrDA			*/
/* ARP works differently on different FC media .. so  */
#define ARPHRD_FCPP	784		/* Point to point fibrechannel	*/
#define ARPHRD_FCAL	785		/* Fibrechannel arbitrated loop */
#define ARPHRD_FCPL	786		/* Fibrechannel public loop	*/
#define ARPHRD_FCFABRIC	787		/* Fibrechannel fabric		*/
	/* 787->799 reserved for fibrechannel media types */
#define ARPHRD_IEEE802_TR 800		/* Magic type ident for TR	*/
#define ARPHRD_IEEE80211 801		/* IEEE 802.11			*/
#define ARPHRD_IEEE80211_PRISM 802	/* IEEE 802.11 + Prism2 header  */
#define ARPHRD_IEEE80211_RADIOTAP 803	/* IEEE 802.11 + radiotap header */
#define ARPHRD_IEEE802154	  804
#define ARPHRD_IEEE802154_MONITOR 805	/* IEEE 802.15.4 network monitor */

#define ARPHRD_PHONET	820		/* PhoNet media type		*/
#define ARPHRD_PHONET_PIPE 821		/* PhoNet pipe header		*/
#define ARPHRD_CAIF	822		/* CAIF media type		*/
#define ARPHRD_IP6GRE	823		/* GRE over IPv6		*/

#define ARPHRD_VOID	  0xFFFF	/* Void type, nothing is known */
#define ARPHRD_NONE	  0xFFFE	/* zero header length */

/* ARP protocol opcodes. */
#define	ARPOP_REQUEST	1		/* ARP request			*/
#define	ARPOP_REPLY	2		/* ARP reply			*/
#define	ARPOP_RREQUEST	3		/* RARP request			*/
#define	ARPOP_RREPLY	4		/* RARP reply			*/
#define	ARPOP_InREQUEST	8		/* InARP request		*/
#define	ARPOP_InREPLY	9		/* InARP reply			*/
#define	ARPOP_NAK	10		/* (ATM)ARP NAK			*/


/* ARP ioctl request. */
struct arpreq {
  struct sockaddr	arp_pa;		/* protocol address		*/
  struct sockaddr	arp_ha;		/* hardware address		*/
  int			arp_flags;	/* flags			*/
  struct sockaddr       arp_netmask;    /* netmask (only for proxy arps) */
  char			arp_dev[16];
};

struct arpreq_old {
  struct sockaddr	arp_pa;		/* protocol address		*/
  struct sockaddr	arp_ha;		/* hardware address		*/
  int			arp_flags;	/* flags			*/
  struct sockaddr       arp_netmask;    /* netmask (only for proxy arps) */
};

/* ARP Flag values. */
#define ATF_COM		0x02		/* completed entry (ha valid)	*/
#define	ATF_PERM	0x04		/* permanent entry		*/
#define	ATF_PUBL	0x08		/* publish entry		*/
#define	ATF_USETRAILERS	0x10		/* has requested trailers	*/
#define ATF_NETMASK     0x20            /* want to use a netmask (only
					   for proxy entries) */
#define ATF_DONTPUB	0x40		/* don't answer this addresses	*/

/*
 *	This structure defines an ethernet arp header.
 */

struct arphdr {
	__be16		ar_hrd;		/* format of hardware address	*/
	__be16		ar_pro;		/* format of protocol address	*/
	unsigned char	ar_hln;		/* length of hardware address	*/
	unsigned char	ar_pln;		/* length of protocol address	*/
	__be16		ar_op;		/* ARP opcode (command)		*/

#if 0
	 /*
	  *	 Ethernet looks like this : This bit is variable sized however...
	  */
	unsigned char		ar_sha[ETH_ALEN];	/* sender hardware address	*/
	unsigned char		ar_sip[4];		/* sender IP address		*/
	unsigned char		ar_tha[ETH_ALEN];	/* target hardware address	*/
	unsigned char		ar_tip[4];		/* target IP address		*/
#endif

};

#ifdef __KERNEL__
#include <linux/skbuff.h>

static inline struct arphdr *arp_hdr(const struct sk_buff *skb)
{
	return (struct arphdr *)skb_network_header(skb);
}

static inline int arp_hdr_len(struct net_device *dev)
{
	/* ARP header, plus 2 device addresses, plus 2 IP addresses. */
	return sizeof(struct arphdr) + (dev->addr_len + sizeof(u32)) * 2;
}
#endif

#endif	/* _LINUX_IF_ARP_H */
