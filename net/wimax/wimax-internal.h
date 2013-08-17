/*
 * Linux WiMAX
 * Internal API for kernel space WiMAX stack
 *
 *
 * Copyright (C) 2007 Intel Corporation <linux-wimax@intel.com>
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * This header file is for declarations and definitions internal to
 * the WiMAX stack. For public APIs and documentation, see
 * include/net/wimax.h and include/linux/wimax.h.
 */

#ifndef __WIMAX_INTERNAL_H__
#define __WIMAX_INTERNAL_H__
#ifdef __KERNEL__

#include <linux/device.h>
#include <net/wimax.h>


/*
 * Decide if a (locked) device is ready for use
 *
 * Before using the device structure, it must be locked
 * (wimax_dev->mutex). As well, most operations need to call this
 * function to check if the state is the right one.
 *
 * An error value will be returned if the state is not the right
 * one. In that case, the caller should not attempt to use the device
 * and just unlock it.
 */
static inline __must_check
int wimax_dev_is_ready(struct wimax_dev *wimax_dev)
{
	if (wimax_dev->state == __WIMAX_ST_NULL)
		return -EINVAL;	/* Device is not even registered! */
	if (wimax_dev->state == WIMAX_ST_DOWN)
		return -ENOMEDIUM;
	if (wimax_dev->state == __WIMAX_ST_QUIESCING)
		return -ESHUTDOWN;
	return 0;
}


static inline
void __wimax_state_set(struct wimax_dev *wimax_dev, enum wimax_st state)
{
	wimax_dev->state = state;
}
extern void __wimax_state_change(struct wimax_dev *, enum wimax_st);

#ifdef CONFIG_DEBUG_FS
extern int wimax_debugfs_add(struct wimax_dev *);
extern void wimax_debugfs_rm(struct wimax_dev *);
#else
static inline int wimax_debugfs_add(struct wimax_dev *wimax_dev)
{
	return 0;
}
static inline void wimax_debugfs_rm(struct wimax_dev *wimax_dev) {}
#endif

extern void wimax_id_table_add(struct wimax_dev *);
extern struct wimax_dev *wimax_dev_get_by_genl_info(struct genl_info *, int);
extern void wimax_id_table_rm(struct wimax_dev *);
extern void wimax_id_table_release(void);

extern int wimax_rfkill_add(struct wimax_dev *);
extern void wimax_rfkill_rm(struct wimax_dev *);

extern struct genl_family wimax_gnl_family;
extern struct genl_multicast_group wimax_gnl_mcg;

#endif /* #ifdef __KERNEL__ */
#endif /* #ifndef __WIMAX_INTERNAL_H__ */
