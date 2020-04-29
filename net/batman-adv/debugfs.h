/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2010-2020  B.A.T.M.A.N. contributors:
 *
 * Marek Lindner
 */

#ifndef _NET_BATMAN_ADV_DEBUGFS_H_
#define _NET_BATMAN_ADV_DEBUGFS_H_

#include "main.h"

#include <linux/fs.h>
#include <linux/netdevice.h>

#define BATADV_DEBUGFS_SUBDIR "batman_adv"

#if IS_ENABLED(CONFIG_BATMAN_ADV_DEBUGFS)

void batadv_debugfs_deprecated(struct file *file, const char *alt);
void batadv_debugfs_init(void);
void batadv_debugfs_destroy(void);
int batadv_debugfs_add_meshif(struct net_device *dev);
void batadv_debugfs_rename_meshif(struct net_device *dev);
void batadv_debugfs_del_meshif(struct net_device *dev);
void batadv_debugfs_add_hardif(struct batadv_hard_iface *hard_iface);
void batadv_debugfs_rename_hardif(struct batadv_hard_iface *hard_iface);
void batadv_debugfs_del_hardif(struct batadv_hard_iface *hard_iface);

#else

static inline void batadv_debugfs_deprecated(struct file *file, const char *alt)
{
}

static inline void batadv_debugfs_init(void)
{
}

static inline void batadv_debugfs_destroy(void)
{
}

static inline int batadv_debugfs_add_meshif(struct net_device *dev)
{
	return 0;
}

static inline void batadv_debugfs_rename_meshif(struct net_device *dev)
{
}

static inline void batadv_debugfs_del_meshif(struct net_device *dev)
{
}

static inline
void batadv_debugfs_add_hardif(struct batadv_hard_iface *hard_iface)
{
}

static inline
void batadv_debugfs_rename_hardif(struct batadv_hard_iface *hard_iface)
{
}

static inline
void batadv_debugfs_del_hardif(struct batadv_hard_iface *hard_iface)
{
}

#endif

#endif /* _NET_BATMAN_ADV_DEBUGFS_H_ */
