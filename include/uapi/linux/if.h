/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Global definitions for the INET interface module.
 *
 * Version:	@(#)if.h	1.0.2	04/18/93
 *
 * Authors:	Original taken from Berkeley UNIX 4.3, (c) UCB 1982-1988
 *		Ross Biro
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _LINUX_IF_H
#define _LINUX_IF_H

#include <linux/types.h>		/* for "__kernel_caddr_t" et al	*/
#include <linux/socket.h>		/* for "struct sockaddr" et al	*/
#include <linux/compiler.h>		/* for "__user" et al           */

#define	IFNAMSIZ	16
#define	IFALIASZ	256
#include <linux/hdlc/ioctl.h>

/* Standard interface flags (netdevice->flags). */
#define	IFF_UP		0x1		/* interface is up		*/
#define	IFF_BROADCAST	0x2		/* broadcast address valid	*/
#define	IFF_DEBUG	0x4		/* turn on debugging		*/
#define	IFF_LOOPBACK	0x8		/* is a loopback net		*/
#define	IFF_POINTOPOINT	0x10		/* interface is has p-p link	*/
#define	IFF_NOTRAILERS	0x20		/* avoid use of trailers	*/
#define	IFF_RUNNING	0x40		/* interface RFC2863 OPER_UP	*/
#define	IFF_NOARP	0x80		/* no ARP protocol		*/
#define	IFF_PROMISC	0x100		/* receive all packets		*/
#define	IFF_ALLMULTI	0x200		/* receive all multicast packets*/

#define IFF_MASTER	0x400		/* master of a load balancer 	*/
#define IFF_SLAVE	0x800		/* slave of a load balancer	*/

#define IFF_MULTICAST	0x1000		/* Supports multicast		*/

#define IFF_PORTSEL	0x2000          /* can set media type		*/
#define IFF_AUTOMEDIA	0x4000		/* auto media select active	*/
#define IFF_DYNAMIC	0x8000		/* dialup device with changing addresses*/

#define IFF_LOWER_UP	0x10000		/* driver signals L1 up		*/
#define IFF_DORMANT	0x20000		/* driver signals dormant	*/

#define IFF_ECHO	0x40000		/* echo sent packets		*/

#define IFF_VOLATILE	(IFF_LOOPBACK|IFF_POINTOPOINT|IFF_BROADCAST|IFF_ECHO|\
		IFF_MASTER|IFF_SLAVE|IFF_RUNNING|IFF_LOWER_UP|IFF_DORMANT)

/* Private (from user) interface flags (netdevice->priv_flags). */
#define IFF_802_1Q_VLAN 0x1             /* 802.1Q VLAN device.          */
#define IFF_EBRIDGE	0x2		/* Ethernet bridging device.	*/
#define IFF_SLAVE_INACTIVE	0x4	/* bonding slave not the curr. active */
#define IFF_MASTER_8023AD	0x8	/* bonding master, 802.3ad. 	*/
#define IFF_MASTER_ALB	0x10		/* bonding master, balance-alb.	*/
#define IFF_BONDING	0x20		/* bonding master or slave	*/
#define IFF_SLAVE_NEEDARP 0x40		/* need ARPs for validation	*/
#define IFF_ISATAP	0x80		/* ISATAP interface (RFC4214)	*/
#define IFF_MASTER_ARPMON 0x100		/* bonding master, ARP mon in use */
#define IFF_WAN_HDLC	0x200		/* WAN HDLC device		*/
#define IFF_XMIT_DST_RELEASE 0x400	/* dev_hard_start_xmit() is allowed to
					 * release skb->dst
					 */
#define IFF_DONT_BRIDGE 0x800		/* disallow bridging this ether dev */
#define IFF_DISABLE_NETPOLL	0x1000	/* disable netpoll at run-time */
#define IFF_MACVLAN_PORT	0x2000	/* device used as macvlan port */
#define IFF_BRIDGE_PORT	0x4000		/* device used as bridge port */
#define IFF_OVS_DATAPATH	0x8000	/* device used as Open vSwitch
					 * datapath port */
#define IFF_TX_SKB_SHARING	0x10000	/* The interface supports sharing
					 * skbs on transmit */
#define IFF_UNICAST_FLT	0x20000		/* Supports unicast filtering	*/
#define IFF_TEAM_PORT	0x40000		/* device used as team port */
#define IFF_SUPP_NOFCS	0x80000		/* device supports sending custom FCS */
#define IFF_LIVE_ADDR_CHANGE 0x100000	/* device supports hardware address
					 * change when it's running */
#define IFF_MACVLAN 0x200000		/* Macvlan device */


#define IF_GET_IFACE	0x0001		/* for querying only */
#define IF_GET_PROTO	0x0002

/* For definitions see hdlc.h */
#define IF_IFACE_V35	0x1000		/* V.35 serial interface	*/
#define IF_IFACE_V24	0x1001		/* V.24 serial interface	*/
#define IF_IFACE_X21	0x1002		/* X.21 serial interface	*/
#define IF_IFACE_T1	0x1003		/* T1 telco serial interface	*/
#define IF_IFACE_E1	0x1004		/* E1 telco serial interface	*/
#define IF_IFACE_SYNC_SERIAL 0x1005	/* can't be set by software	*/
#define IF_IFACE_X21D   0x1006          /* X.21 Dual Clocking (FarSite) */

/* For definitions see hdlc.h */
#define IF_PROTO_HDLC	0x2000		/* raw HDLC protocol		*/
#define IF_PROTO_PPP	0x2001		/* PPP protocol			*/
#define IF_PROTO_CISCO	0x2002		/* Cisco HDLC protocol		*/
#define IF_PROTO_FR	0x2003		/* Frame Relay protocol		*/
#define IF_PROTO_FR_ADD_PVC 0x2004	/*    Create FR PVC		*/
#define IF_PROTO_FR_DEL_PVC 0x2005	/*    Delete FR PVC		*/
#define IF_PROTO_X25	0x2006		/* X.25				*/
#define IF_PROTO_HDLC_ETH 0x2007	/* raw HDLC, Ethernet emulation	*/
#define IF_PROTO_FR_ADD_ETH_PVC 0x2008	/*  Create FR Ethernet-bridged PVC */
#define IF_PROTO_FR_DEL_ETH_PVC 0x2009	/*  Delete FR Ethernet-bridged PVC */
#define IF_PROTO_FR_PVC	0x200A		/* for reading PVC status	*/
#define IF_PROTO_FR_ETH_PVC 0x200B
#define IF_PROTO_RAW    0x200C          /* RAW Socket                   */

/* RFC 2863 operational status */
enum {
	IF_OPER_UNKNOWN,
	IF_OPER_NOTPRESENT,
	IF_OPER_DOWN,
	IF_OPER_LOWERLAYERDOWN,
	IF_OPER_TESTING,
	IF_OPER_DORMANT,
	IF_OPER_UP,
};

/* link modes */
enum {
	IF_LINK_MODE_DEFAULT,
	IF_LINK_MODE_DORMANT,	/* limit upward transition to dormant */
};

/*
 *	Device mapping structure. I'd just gone off and designed a 
 *	beautiful scheme using only loadable modules with arguments
 *	for driver options and along come the PCMCIA people 8)
 *
 *	Ah well. The get() side of this is good for WDSETUP, and it'll
 *	be handy for debugging things. The set side is fine for now and
 *	being very small might be worth keeping for clean configuration.
 */

struct ifmap {
	unsigned long mem_start;
	unsigned long mem_end;
	unsigned short base_addr; 
	unsigned char irq;
	unsigned char dma;
	unsigned char port;
	/* 3 bytes spare */
};

struct if_settings {
	unsigned int type;	/* Type of physical device or protocol */
	unsigned int size;	/* Size of the data allocated by the caller */
	union {
		/* {atm/eth/dsl}_settings anyone ? */
		raw_hdlc_proto		__user *raw_hdlc;
		cisco_proto		__user *cisco;
		fr_proto		__user *fr;
		fr_proto_pvc		__user *fr_pvc;
		fr_proto_pvc_info	__user *fr_pvc_info;

		/* interface settings */
		sync_serial_settings	__user *sync;
		te1_settings		__user *te1;
	} ifs_ifsu;
};

/*
 * Interface request structure used for socket
 * ioctl's.  All interface ioctl's must have parameter
 * definitions which begin with ifr_name.  The
 * remainder may be interface specific.
 */

struct ifreq {
#define IFHWADDRLEN	6
	union
	{
		char	ifrn_name[IFNAMSIZ];		/* if name, e.g. "en0" */
	} ifr_ifrn;
	
	union {
		struct	sockaddr ifru_addr;
		struct	sockaddr ifru_dstaddr;
		struct	sockaddr ifru_broadaddr;
		struct	sockaddr ifru_netmask;
		struct  sockaddr ifru_hwaddr;
		short	ifru_flags;
		int	ifru_ivalue;
		int	ifru_mtu;
		struct  ifmap ifru_map;
		char	ifru_slave[IFNAMSIZ];	/* Just fits the size */
		char	ifru_newname[IFNAMSIZ];
		void __user *	ifru_data;
		struct	if_settings ifru_settings;
	} ifr_ifru;
};

#define ifr_name	ifr_ifrn.ifrn_name	/* interface name 	*/
#define ifr_hwaddr	ifr_ifru.ifru_hwaddr	/* MAC address 		*/
#define	ifr_addr	ifr_ifru.ifru_addr	/* address		*/
#define	ifr_dstaddr	ifr_ifru.ifru_dstaddr	/* other end of p-p lnk	*/
#define	ifr_broadaddr	ifr_ifru.ifru_broadaddr	/* broadcast address	*/
#define	ifr_netmask	ifr_ifru.ifru_netmask	/* interface net mask	*/
#define	ifr_flags	ifr_ifru.ifru_flags	/* flags		*/
#define	ifr_metric	ifr_ifru.ifru_ivalue	/* metric		*/
#define	ifr_mtu		ifr_ifru.ifru_mtu	/* mtu			*/
#define ifr_map		ifr_ifru.ifru_map	/* device map		*/
#define ifr_slave	ifr_ifru.ifru_slave	/* slave device		*/
#define	ifr_data	ifr_ifru.ifru_data	/* for use by interface	*/
#define ifr_ifindex	ifr_ifru.ifru_ivalue	/* interface index	*/
#define ifr_bandwidth	ifr_ifru.ifru_ivalue    /* link bandwidth	*/
#define ifr_qlen	ifr_ifru.ifru_ivalue	/* Queue length 	*/
#define ifr_newname	ifr_ifru.ifru_newname	/* New name		*/
#define ifr_settings	ifr_ifru.ifru_settings	/* Device/proto settings*/

/*
 * Structure used in SIOCGIFCONF request.
 * Used to retrieve interface configuration
 * for machine (useful for programs which
 * must know all networks accessible).
 */

struct ifconf  {
	int	ifc_len;			/* size of buffer	*/
	union {
		char __user *ifcu_buf;
		struct ifreq __user *ifcu_req;
	} ifc_ifcu;
};
#define	ifc_buf	ifc_ifcu.ifcu_buf		/* buffer address	*/
#define	ifc_req	ifc_ifcu.ifcu_req		/* array of structures	*/

#endif /* _LINUX_IF_H */
