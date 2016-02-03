/* Copyright (C) 2007-2015 B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Simon Wunderlich
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _NET_BATMAN_ADV_HARD_INTERFACE_H_
#define _NET_BATMAN_ADV_HARD_INTERFACE_H_

#include "main.h"

#include <linux/atomic.h>
#include <linux/compiler.h>
#include <linux/notifier.h>
#include <linux/rcupdate.h>
#include <linux/stddef.h>
#include <linux/types.h>

struct net_device;

enum batadv_hard_if_state {
	BATADV_IF_NOT_IN_USE,
	BATADV_IF_TO_BE_REMOVED,
	BATADV_IF_INACTIVE,
	BATADV_IF_ACTIVE,
	BATADV_IF_TO_BE_ACTIVATED,
	BATADV_IF_I_WANT_YOU,
};

/**
 * enum batadv_hard_if_cleanup - Cleanup modi for soft_iface after slave removal
 * @BATADV_IF_CLEANUP_KEEP: Don't automatically delete soft-interface
 * @BATADV_IF_CLEANUP_AUTO: Delete soft-interface after last slave was removed
 */
enum batadv_hard_if_cleanup {
	BATADV_IF_CLEANUP_KEEP,
	BATADV_IF_CLEANUP_AUTO,
};

extern struct notifier_block batadv_hard_if_notifier;

bool batadv_is_wifi_netdev(struct net_device *net_device);
bool batadv_is_wifi_iface(int ifindex);
struct batadv_hard_iface*
batadv_hardif_get_by_netdev(const struct net_device *net_dev);
int batadv_hardif_enable_interface(struct batadv_hard_iface *hard_iface,
				   const char *iface_name);
void batadv_hardif_disable_interface(struct batadv_hard_iface *hard_iface,
				     enum batadv_hard_if_cleanup autodel);
void batadv_hardif_remove_interfaces(void);
int batadv_hardif_min_mtu(struct net_device *soft_iface);
void batadv_update_min_mtu(struct net_device *soft_iface);
void batadv_hardif_free_rcu(struct rcu_head *rcu);

/**
 * batadv_hardif_free_ref - decrement the hard interface refcounter and
 *  possibly free it
 * @hard_iface: the hard interface to free
 */
static inline void
batadv_hardif_free_ref(struct batadv_hard_iface *hard_iface)
{
	if (atomic_dec_and_test(&hard_iface->refcount))
		call_rcu(&hard_iface->rcu, batadv_hardif_free_rcu);
}

static inline struct batadv_hard_iface *
batadv_primary_if_get_selected(struct batadv_priv *bat_priv)
{
	struct batadv_hard_iface *hard_iface;

	rcu_read_lock();
	hard_iface = rcu_dereference(bat_priv->primary_if);
	if (!hard_iface)
		goto out;

	if (!atomic_inc_not_zero(&hard_iface->refcount))
		hard_iface = NULL;

out:
	rcu_read_unlock();
	return hard_iface;
}

#endif /* _NET_BATMAN_ADV_HARD_INTERFACE_H_ */
