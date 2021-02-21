/* SPDX-License-Identifier: GPL-1.0+ WITH Linux-syscall-note */
/*
 * Bond several ethernet interfaces into a Cisco, running 'Etherchannel'.
 *
 *
 * Portions are (c) Copyright 1995 Simon "Guru Aleph-Null" Janes
 * NCM: Network and Communications Management, Inc.
 *
 * BUT, I'm the one who modified it for ethernet, so:
 * (c) Copyright 1999, Thomas Davis, tadavis@lbl.gov
 *
 *	This software may be used and distributed according to the terms
 *	of the GNU Public License, incorporated herein by reference.
 *
 * 2003/03/18 - Amir Noam <amir.noam at intel dot com>
 *	- Added support for getting slave's speed and duplex via ethtool.
 *	  Needed for 802.3ad and other future modes.
 *
 * 2003/03/18 - Tsippy Mendelson <tsippy.mendelson at intel dot com> and
 *		Shmulik Hen <shmulik.hen at intel dot com>
 *	- Enable support of modes that need to use the unique mac address of
 *	  each slave.
 *
 * 2003/03/18 - Tsippy Mendelson <tsippy.mendelson at intel dot com> and
 *		Amir Noam <amir.noam at intel dot com>
 *	- Moved driver's private data types to bonding.h
 *
 * 2003/03/18 - Amir Noam <amir.noam at intel dot com>,
 *		Tsippy Mendelson <tsippy.mendelson at intel dot com> and
 *		Shmulik Hen <shmulik.hen at intel dot com>
 *	- Added support for IEEE 802.3ad Dynamic link aggregation mode.
 *
 * 2003/05/01 - Amir Noam <amir.noam at intel dot com>
 *	- Added ABI version control to restore compatibility between
 *	  new/old ifenslave and new/old bonding.
 *
 * 2003/12/01 - Shmulik Hen <shmulik.hen at intel dot com>
 *	- Code cleanup and style changes
 *
 * 2005/05/05 - Jason Gabler <jygabler at lbl dot gov>
 *      - added definitions for various XOR hashing policies
 */

#ifndef _LINUX_IF_BONDING_H
#define _LINUX_IF_BONDING_H

#include <linux/if.h>
#include <linux/types.h>
#include <linux/if_ether.h>

/* userland - kernel ABI version (2003/05/08) */
#define BOND_ABI_VERSION 2

/*
 * We can remove these ioctl definitions in 2.5.  People should use the
 * SIOC*** versions of them instead
 */
#define BOND_ENSLAVE_OLD		(SIOCDEVPRIVATE)
#define BOND_RELEASE_OLD		(SIOCDEVPRIVATE + 1)
#define BOND_SETHWADDR_OLD		(SIOCDEVPRIVATE + 2)
#define BOND_SLAVE_INFO_QUERY_OLD	(SIOCDEVPRIVATE + 11)
#define BOND_INFO_QUERY_OLD		(SIOCDEVPRIVATE + 12)
#define BOND_CHANGE_ACTIVE_OLD		(SIOCDEVPRIVATE + 13)

#define BOND_CHECK_MII_STATUS	(SIOCGMIIPHY)

#define BOND_MODE_ROUNDROBIN	0
#define BOND_MODE_ACTIVEBACKUP	1
#define BOND_MODE_XOR		2
#define BOND_MODE_BROADCAST	3
#define BOND_MODE_8023AD        4
#define BOND_MODE_TLB           5
#define BOND_MODE_ALB		6 /* TLB + RLB (receive load balancing) */

/* each slave's link has 4 states */
#define BOND_LINK_UP    0           /* link is up and running */
#define BOND_LINK_FAIL  1           /* link has just gone down */
#define BOND_LINK_DOWN  2           /* link has been down for too long time */
#define BOND_LINK_BACK  3           /* link is going back */

/* each slave has several states */
#define BOND_STATE_ACTIVE       0   /* link is active */
#define BOND_STATE_BACKUP       1   /* link is backup */

#define BOND_DEFAULT_MAX_BONDS  1   /* Default maximum number of devices to support */

#define BOND_DEFAULT_TX_QUEUES 16   /* Default number of tx queues per device */

#define BOND_DEFAULT_RESEND_IGMP	1 /* Default number of IGMP membership reports */

/* hashing types */
#define BOND_XMIT_POLICY_LAYER2		0 /* layer 2 (MAC only), default */
#define BOND_XMIT_POLICY_LAYER34	1 /* layer 3+4 (IP ^ (TCP || UDP)) */
#define BOND_XMIT_POLICY_LAYER23	2 /* layer 2+3 (IP ^ MAC) */
#define BOND_XMIT_POLICY_ENCAP23	3 /* encapsulated layer 2+3 */
#define BOND_XMIT_POLICY_ENCAP34	4 /* encapsulated layer 3+4 */
#define BOND_XMIT_POLICY_VLAN_SRCMAC	5 /* vlan + source MAC */

/* 802.3ad port state definitions (43.4.2.2 in the 802.3ad standard) */
#define LACP_STATE_LACP_ACTIVITY   0x1
#define LACP_STATE_LACP_TIMEOUT    0x2
#define LACP_STATE_AGGREGATION     0x4
#define LACP_STATE_SYNCHRONIZATION 0x8
#define LACP_STATE_COLLECTING      0x10
#define LACP_STATE_DISTRIBUTING    0x20
#define LACP_STATE_DEFAULTED       0x40
#define LACP_STATE_EXPIRED         0x80

typedef struct ifbond {
	__s32 bond_mode;
	__s32 num_slaves;
	__s32 miimon;
} ifbond;

typedef struct ifslave {
	__s32 slave_id; /* Used as an IN param to the BOND_SLAVE_INFO_QUERY ioctl */
	char slave_name[IFNAMSIZ];
	__s8 link;
	__s8 state;
	__u32  link_failure_count;
} ifslave;

struct ad_info {
	__u16 aggregator_id;
	__u16 ports;
	__u16 actor_key;
	__u16 partner_key;
	__u8 partner_system[ETH_ALEN];
};

/* Embedded inside LINK_XSTATS_TYPE_BOND */
enum {
	BOND_XSTATS_UNSPEC,
	BOND_XSTATS_3AD,
	__BOND_XSTATS_MAX
};
#define BOND_XSTATS_MAX (__BOND_XSTATS_MAX - 1)

/* Embedded inside BOND_XSTATS_3AD */
enum {
	BOND_3AD_STAT_LACPDU_RX,
	BOND_3AD_STAT_LACPDU_TX,
	BOND_3AD_STAT_LACPDU_UNKNOWN_RX,
	BOND_3AD_STAT_LACPDU_ILLEGAL_RX,
	BOND_3AD_STAT_MARKER_RX,
	BOND_3AD_STAT_MARKER_TX,
	BOND_3AD_STAT_MARKER_RESP_RX,
	BOND_3AD_STAT_MARKER_RESP_TX,
	BOND_3AD_STAT_MARKER_UNKNOWN_RX,
	BOND_3AD_STAT_PAD,
	__BOND_3AD_STAT_MAX
};
#define BOND_3AD_STAT_MAX (__BOND_3AD_STAT_MAX - 1)

#endif /* _LINUX_IF_BONDING_H */

/*
 * Local variables:
 *  version-control: t
 *  kept-new-versions: 5
 *  c-indent-level: 8
 *  c-basic-offset: 8
 *  tab-width: 8
 * End:
 */

