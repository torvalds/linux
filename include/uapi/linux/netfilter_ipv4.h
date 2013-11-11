/* IPv4-specific defines for netfilter. 
 * (C)1998 Rusty Russell -- This code is GPL.
 */
#ifndef _UAPI__LINUX_IP_NETFILTER_H
#define _UAPI__LINUX_IP_NETFILTER_H


#include <linux/netfilter.h>

/* only for userspace compatibility */
#ifndef __KERNEL__

#include <limits.h> /* for INT_MIN, INT_MAX */

/* IP Cache bits. */
/* Src IP address. */
#define NFC_IP_SRC		0x0001
/* Dest IP address. */
#define NFC_IP_DST		0x0002
/* Input device. */
#define NFC_IP_IF_IN		0x0004
/* Output device. */
#define NFC_IP_IF_OUT		0x0008
/* TOS. */
#define NFC_IP_TOS		0x0010
/* Protocol. */
#define NFC_IP_PROTO		0x0020
/* IP options. */
#define NFC_IP_OPTIONS		0x0040
/* Frag & flags. */
#define NFC_IP_FRAG		0x0080

/* Per-protocol information: only matters if proto match. */
/* TCP flags. */
#define NFC_IP_TCPFLAGS		0x0100
/* Source port. */
#define NFC_IP_SRC_PT		0x0200
/* Dest port. */
#define NFC_IP_DST_PT		0x0400
/* Something else about the proto */
#define NFC_IP_PROTO_UNKNOWN	0x2000

/* IP Hooks */
/* After promisc drops, checksum checks. */
#define NF_IP_PRE_ROUTING	0
/* If the packet is destined for this box. */
#define NF_IP_LOCAL_IN		1
/* If the packet is destined for another interface. */
#define NF_IP_FORWARD		2
/* Packets coming from a local process. */
#define NF_IP_LOCAL_OUT		3
/* Packets about to hit the wire. */
#define NF_IP_POST_ROUTING	4
#define NF_IP_NUMHOOKS		5
#endif /* ! __KERNEL__ */

enum nf_ip_hook_priorities {
	NF_IP_PRI_FIRST = INT_MIN,
	NF_IP_PRI_CONNTRACK_DEFRAG = -400,
	NF_IP_PRI_RAW = -300,
	NF_IP_PRI_SELINUX_FIRST = -225,
	NF_IP_PRI_CONNTRACK = -200,
	NF_IP_PRI_MANGLE = -150,
	NF_IP_PRI_NAT_DST = -100,
	NF_IP_PRI_FILTER = 0,
	NF_IP_PRI_SECURITY = 50,
	NF_IP_PRI_NAT_SRC = 100,
	NF_IP_PRI_SELINUX_LAST = 225,
	NF_IP_PRI_CONNTRACK_HELPER = 300,
	NF_IP_PRI_CONNTRACK_CONFIRM = INT_MAX,
	NF_IP_PRI_LAST = INT_MAX,
};

/* Arguments for setsockopt SOL_IP: */
/* 2.0 firewalling went from 64 through 71 (and +256, +512, etc). */
/* 2.2 firewalling (+ masq) went from 64 through 76 */
/* 2.4 firewalling went 64 through 67. */
#define SO_ORIGINAL_DST 80


#endif /* _UAPI__LINUX_IP_NETFILTER_H */
