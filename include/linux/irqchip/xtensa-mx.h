/*
 * Xtensa MX interrupt distributor
 *
 * Copyright (C) 2002 - 2013 Tensilica, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef __LINUX_IRQCHIP_XTENSA_MX_H
#define __LINUX_IRQCHIP_XTENSA_MX_H

struct device_node;
int xtensa_mx_init_legacy(struct device_node *interrupt_parent);

#endif /* __LINUX_IRQCHIP_XTENSA_MX_H */
