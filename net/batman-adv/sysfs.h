/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2010-2018  B.A.T.M.A.N. contributors:
 *
 * Marek Lindner
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

#ifndef _NET_BATMAN_ADV_SYSFS_H_
#define _NET_BATMAN_ADV_SYSFS_H_

#include "main.h"

#include <linux/sysfs.h>
#include <linux/types.h>

struct kobject;
struct net_device;

#define BATADV_SYSFS_IF_MESH_SUBDIR "mesh"
#define BATADV_SYSFS_IF_BAT_SUBDIR "batman_adv"
/**
 * BATADV_SYSFS_VLAN_SUBDIR_PREFIX - prefix of the subfolder that will be
 *  created in the sysfs hierarchy for each VLAN interface. The subfolder will
 *  be named "BATADV_SYSFS_VLAN_SUBDIR_PREFIX%vid".
 */
#define BATADV_SYSFS_VLAN_SUBDIR_PREFIX "vlan"

/**
 * struct batadv_attribute - sysfs export helper for batman-adv attributes
 */
struct batadv_attribute {
	/** @attr: sysfs attribute file */
	struct attribute attr;

	/**
	 * @show: function to export the current attribute's content to sysfs
	 */
	ssize_t (*show)(struct kobject *kobj, struct attribute *attr,
			char *buf);

	/**
	 * @store: function to load new value from character buffer and save it
	 * in batman-adv attribute
	 */
	ssize_t (*store)(struct kobject *kobj, struct attribute *attr,
			 char *buf, size_t count);
};

int batadv_sysfs_add_meshif(struct net_device *dev);
void batadv_sysfs_del_meshif(struct net_device *dev);
int batadv_sysfs_add_hardif(struct kobject **hardif_obj,
			    struct net_device *dev);
void batadv_sysfs_del_hardif(struct kobject **hardif_obj);
int batadv_sysfs_add_vlan(struct net_device *dev,
			  struct batadv_softif_vlan *vlan);
void batadv_sysfs_del_vlan(struct batadv_priv *bat_priv,
			   struct batadv_softif_vlan *vlan);
int batadv_throw_uevent(struct batadv_priv *bat_priv, enum batadv_uev_type type,
			enum batadv_uev_action action, const char *data);

#endif /* _NET_BATMAN_ADV_SYSFS_H_ */
