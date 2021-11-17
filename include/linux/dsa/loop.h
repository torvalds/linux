/* SPDX-License-Identifier: GPL-2.0 */
#ifndef DSA_LOOP_H
#define DSA_LOOP_H

#include <linux/types.h>
#include <linux/ethtool.h>
#include <net/dsa.h>

struct dsa_loop_vlan {
	u16 members;
	u16 untagged;
};

struct dsa_loop_mib_entry {
	char name[ETH_GSTRING_LEN];
	unsigned long val;
};

enum dsa_loop_mib_counters {
	DSA_LOOP_PHY_READ_OK,
	DSA_LOOP_PHY_READ_ERR,
	DSA_LOOP_PHY_WRITE_OK,
	DSA_LOOP_PHY_WRITE_ERR,
	__DSA_LOOP_CNT_MAX,
};

struct dsa_loop_port {
	struct dsa_loop_mib_entry mib[__DSA_LOOP_CNT_MAX];
	u16 pvid;
	int mtu;
};

struct dsa_loop_priv {
	struct mii_bus	*bus;
	unsigned int	port_base;
	struct dsa_loop_vlan vlans[VLAN_N_VID];
	struct net_device *netdev;
	struct dsa_loop_port ports[DSA_MAX_PORTS];
};

#endif /* DSA_LOOP_H */
