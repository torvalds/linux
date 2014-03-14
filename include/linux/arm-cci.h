/*
 * CCI cache coherent interconnect support
 *
 * Copyright (C) 2013 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __LINUX_ARM_CCI_H
#define __LINUX_ARM_CCI_H

#include <linux/errno.h>
#include <linux/types.h>

struct device_node;

#ifdef CONFIG_ARM_CCI
extern bool cci_probed(void);
extern int cci_ace_get_port(struct device_node *dn);
extern int cci_disable_port_by_cpu(u64 mpidr);
extern int __cci_control_port_by_device(struct device_node *dn, bool enable);
extern int __cci_control_port_by_index(u32 port, bool enable);
#else
static inline bool cci_probed(void) { return false; }
static inline int cci_ace_get_port(struct device_node *dn)
{
	return -ENODEV;
}
static inline int cci_disable_port_by_cpu(u64 mpidr) { return -ENODEV; }
static inline int __cci_control_port_by_device(struct device_node *dn,
					       bool enable)
{
	return -ENODEV;
}
static inline int __cci_control_port_by_index(u32 port, bool enable)
{
	return -ENODEV;
}
#endif
#define cci_disable_port_by_device(dev) \
	__cci_control_port_by_device(dev, false)
#define cci_enable_port_by_device(dev) \
	__cci_control_port_by_device(dev, true)
#define cci_disable_port_by_index(dev) \
	__cci_control_port_by_index(dev, false)
#define cci_enable_port_by_index(dev) \
	__cci_control_port_by_index(dev, true)

#endif
