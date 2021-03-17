/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2007-2020  B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Simon Wunderlich
 */

#ifndef _NET_BATMAN_ADV_HARD_INTERFACE_H_
#define _NET_BATMAN_ADV_HARD_INTERFACE_H_

#include "main.h"

#include <linux/compiler.h>
#include <linux/kref.h>
#include <linux/netdevice.h>
#include <linux/notifier.h>
#include <linux/rcupdate.h>
#include <linux/stddef.h>
#include <linux/types.h>
#include <net/net_namespace.h>

/**
 * enum batadv_hard_if_state - State of a hard interface
 */
enum batadv_hard_if_state {
	/**
	 * @BATADV_IF_NOT_IN_USE: interface is not used as slave interface of a
	 * batman-adv soft interface
	 */
	BATADV_IF_NOT_IN_USE,

	/**
	 * @BATADV_IF_TO_BE_REMOVED: interface will be removed from soft
	 * interface
	 */
	BATADV_IF_TO_BE_REMOVED,

	/** @BATADV_IF_INACTIVE: interface is deactivated */
	BATADV_IF_INACTIVE,

	/** @BATADV_IF_ACTIVE: interface is used */
	BATADV_IF_ACTIVE,

	/** @BATADV_IF_TO_BE_ACTIVATED: interface is getting activated */
	BATADV_IF_TO_BE_ACTIVATED,

	/**
	 * @BATADV_IF_I_WANT_YOU: interface is queued up (using sysfs) for being
	 * added as slave interface of a batman-adv soft interface
	 */
	BATADV_IF_I_WANT_YOU,
};

/**
 * enum batadv_hard_if_bcast - broadcast avoidance options
 */
enum batadv_hard_if_bcast {
	/** @BATADV_HARDIF_BCAST_OK: Do broadcast on according hard interface */
	BATADV_HARDIF_BCAST_OK = 0,

	/**
	 * @BATADV_HARDIF_BCAST_NORECIPIENT: Broadcast not needed, there is no
	 *  recipient
	 */
	BATADV_HARDIF_BCAST_NORECIPIENT,

	/**
	 * @BATADV_HARDIF_BCAST_DUPFWD: There is just the neighbor we got it
	 *  from
	 */
	BATADV_HARDIF_BCAST_DUPFWD,

	/** @BATADV_HARDIF_BCAST_DUPORIG: There is just the originator */
	BATADV_HARDIF_BCAST_DUPORIG,
};

/**
 * enum batadv_hard_if_cleanup - Cleanup modi for soft_iface after slave removal
 */
enum batadv_hard_if_cleanup {
	/**
	 * @BATADV_IF_CLEANUP_KEEP: Don't automatically delete soft-interface
	 */
	BATADV_IF_CLEANUP_KEEP,

	/**
	 * @BATADV_IF_CLEANUP_AUTO: Delete soft-interface after last slave was
	 *  removed
	 */
	BATADV_IF_CLEANUP_AUTO,
};

extern struct notifier_block batadv_hard_if_notifier;

struct net_device *batadv_get_real_netdev(struct net_device *net_device);
bool batadv_is_cfg80211_hardif(struct batadv_hard_iface *hard_iface);
bool batadv_is_wifi_hardif(struct batadv_hard_iface *hard_iface);
struct batadv_hard_iface*
batadv_hardif_get_by_netdev(const struct net_device *net_dev);
int batadv_hardif_enable_interface(struct batadv_hard_iface *hard_iface,
				   struct net *net, const char *iface_name);
void batadv_hardif_disable_interface(struct batadv_hard_iface *hard_iface,
				     enum batadv_hard_if_cleanup autodel);
int batadv_hardif_min_mtu(struct net_device *soft_iface);
void batadv_update_min_mtu(struct net_device *soft_iface);
void batadv_hardif_release(struct kref *ref);
int batadv_hardif_no_broadcast(struct batadv_hard_iface *if_outgoing,
			       u8 *orig_addr, u8 *orig_neigh);

/**
 * batadv_hardif_put() - decrement the hard interface refcounter and possibly
 *  release it
 * @hard_iface: the hard interface to free
 */
static inline void batadv_hardif_put(struct batadv_hard_iface *hard_iface)
{
	kref_put(&hard_iface->refcount, batadv_hardif_release);
}

/**
 * batadv_primary_if_get_selected() - Get reference to primary interface
 * @bat_priv: the bat priv with all the soft interface information
 *
 * Return: primary interface (with increased refcnt), otherwise NULL
 */
static inline struct batadv_hard_iface *
batadv_primary_if_get_selected(struct batadv_priv *bat_priv)
{
	struct batadv_hard_iface *hard_iface;

	rcu_read_lock();
	hard_iface = rcu_dereference(bat_priv->primary_if);
	if (!hard_iface)
		goto out;

	if (!kref_get_unless_zero(&hard_iface->refcount))
		hard_iface = NULL;

out:
	rcu_read_unlock();
	return hard_iface;
}

#endif /* _NET_BATMAN_ADV_HARD_INTERFACE_H_ */
