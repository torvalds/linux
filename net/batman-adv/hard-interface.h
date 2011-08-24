/*
 * Copyright (C) 2007-2011 B.A.T.M.A.N. contributors:
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 *
 */

#ifndef _NET_BATMAN_ADV_HARD_INTERFACE_H_
#define _NET_BATMAN_ADV_HARD_INTERFACE_H_

enum hard_if_state {
	IF_NOT_IN_USE,
	IF_TO_BE_REMOVED,
	IF_INACTIVE,
	IF_ACTIVE,
	IF_TO_BE_ACTIVATED,
	IF_I_WANT_YOU
};

extern struct notifier_block hard_if_notifier;

struct hard_iface*
hardif_get_by_netdev(const struct net_device *net_dev);
int hardif_enable_interface(struct hard_iface *hard_iface,
			    const char *iface_name);
void hardif_disable_interface(struct hard_iface *hard_iface);
void hardif_remove_interfaces(void);
int hardif_min_mtu(struct net_device *soft_iface);
void update_min_mtu(struct net_device *soft_iface);
void hardif_free_rcu(struct rcu_head *rcu);
bool is_wifi_iface(int ifindex);

static inline void hardif_free_ref(struct hard_iface *hard_iface)
{
	if (atomic_dec_and_test(&hard_iface->refcount))
		call_rcu(&hard_iface->rcu, hardif_free_rcu);
}

static inline struct hard_iface *primary_if_get_selected(
						struct bat_priv *bat_priv)
{
	struct hard_iface *hard_iface;

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
