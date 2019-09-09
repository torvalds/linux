/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_IF_LAG_H
#define _LINUX_IF_LAG_H

#include <linux/netdevice.h>
#include <linux/if_team.h>
#include <net/bonding.h>

static inline bool net_lag_port_dev_txable(const struct net_device *port_dev)
{
	if (netif_is_team_port(port_dev))
		return team_port_dev_txable(port_dev);
	else
		return bond_is_active_slave_dev(port_dev);
}

#endif /* _LINUX_IF_LAG_H */
