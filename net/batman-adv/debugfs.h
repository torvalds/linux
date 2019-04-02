/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2010-2019  B.A.T.M.A.N. contributors:
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

#ifndef _NET_BATMAN_ADV_DEFS_H_
#define _NET_BATMAN_ADV_DEFS_H_

#include "main.h"

struct file;
struct net_device;

#define BATADV_DEFS_SUBDIR "batman_adv"

#if IS_ENABLED(CONFIG_BATMAN_ADV_DEFS)

void batadv_defs_deprecated(struct file *file, const char *alt);
void batadv_defs_init(void);
void batadv_defs_destroy(void);
int batadv_defs_add_meshif(struct net_device *dev);
void batadv_defs_rename_meshif(struct net_device *dev);
void batadv_defs_del_meshif(struct net_device *dev);
int batadv_defs_add_hardif(struct batadv_hard_iface *hard_iface);
void batadv_defs_rename_hardif(struct batadv_hard_iface *hard_iface);
void batadv_defs_del_hardif(struct batadv_hard_iface *hard_iface);

#else

static inline void batadv_defs_deprecated(struct file *file, const char *alt)
{
}

static inline void batadv_defs_init(void)
{
}

static inline void batadv_defs_destroy(void)
{
}

static inline int batadv_defs_add_meshif(struct net_device *dev)
{
	return 0;
}

static inline void batadv_defs_rename_meshif(struct net_device *dev)
{
}

static inline void batadv_defs_del_meshif(struct net_device *dev)
{
}

static inline
int batadv_defs_add_hardif(struct batadv_hard_iface *hard_iface)
{
	return 0;
}

static inline
void batadv_defs_rename_hardif(struct batadv_hard_iface *hard_iface)
{
}

static inline
void batadv_defs_del_hardif(struct batadv_hard_iface *hard_iface)
{
}

#endif

#endif /* _NET_BATMAN_ADV_DEFS_H_ */
