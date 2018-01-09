/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __LINUX_DECNET_NETFILTER_H
#define __LINUX_DECNET_NETFILTER_H

/* DECnet-specific defines for netfilter. 
 * This file (C) Steve Whitehouse 1999 derived from the
 * ipv4 netfilter header file which is
 * (C)1998 Rusty Russell -- This code is GPL.
 */

#include <linux/netfilter.h>

/* only for userspace compatibility */
#ifndef __KERNEL__

#include <limits.h> /* for INT_MIN, INT_MAX */

/* IP Cache bits. */
/* Src IP address. */
#define NFC_DN_SRC		0x0001
/* Dest IP address. */
#define NFC_DN_DST		0x0002
/* Input device. */
#define NFC_DN_IF_IN		0x0004
/* Output device. */
#define NFC_DN_IF_OUT		0x0008

/* kernel define is in netfilter_defs.h */
#define NF_DN_NUMHOOKS		7
#endif /* ! __KERNEL__ */

/* DECnet Hooks */
/* After promisc drops, checksum checks. */
#define NF_DN_PRE_ROUTING	0
/* If the packet is destined for this box. */
#define NF_DN_LOCAL_IN		1
/* If the packet is destined for another interface. */
#define NF_DN_FORWARD		2
/* Packets coming from a local process. */
#define NF_DN_LOCAL_OUT		3
/* Packets about to hit the wire. */
#define NF_DN_POST_ROUTING	4
/* Input Hello Packets */
#define NF_DN_HELLO		5
/* Input Routing Packets */
#define NF_DN_ROUTE		6

enum nf_dn_hook_priorities {
	NF_DN_PRI_FIRST = INT_MIN,
	NF_DN_PRI_CONNTRACK = -200,
	NF_DN_PRI_MANGLE = -150,
	NF_DN_PRI_NAT_DST = -100,
	NF_DN_PRI_FILTER = 0,
	NF_DN_PRI_NAT_SRC = 100,
	NF_DN_PRI_DNRTMSG = 200,
	NF_DN_PRI_LAST = INT_MAX,
};

struct nf_dn_rtmsg {
	int nfdn_ifindex;
};

#define NFDN_RTMSG(r) ((unsigned char *)(r) + NLMSG_ALIGN(sizeof(struct nf_dn_rtmsg)))

#ifndef __KERNEL__
/* backwards compatibility for userspace */
#define DNRMG_L1_GROUP 0x01
#define DNRMG_L2_GROUP 0x02
#endif

enum {
	DNRNG_NLGRP_NONE,
#define DNRNG_NLGRP_NONE	DNRNG_NLGRP_NONE
	DNRNG_NLGRP_L1,
#define DNRNG_NLGRP_L1		DNRNG_NLGRP_L1
	DNRNG_NLGRP_L2,
#define DNRNG_NLGRP_L2		DNRNG_NLGRP_L2
	__DNRNG_NLGRP_MAX
};
#define DNRNG_NLGRP_MAX	(__DNRNG_NLGRP_MAX - 1)

#endif /*__LINUX_DECNET_NETFILTER_H*/
