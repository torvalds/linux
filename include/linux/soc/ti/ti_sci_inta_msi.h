/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Texas Instruments' K3 TI SCI INTA MSI helper
 *
 * Copyright (C) 2018-2019 Texas Instruments Incorporated - https://www.ti.com/
 *	Lokesh Vutla <lokeshvutla@ti.com>
 */

#ifndef __INCLUDE_LINUX_TI_SCI_INTA_MSI_H
#define __INCLUDE_LINUX_TI_SCI_INTA_MSI_H

#include <linux/msi.h>
#include <linux/soc/ti/ti_sci_protocol.h>

struct irq_domain
*ti_sci_inta_msi_create_irq_domain(struct fwnode_handle *fwnode,
				   struct msi_domain_info *info,
				   struct irq_domain *parent);
int ti_sci_inta_msi_domain_alloc_irqs(struct device *dev,
				      struct ti_sci_resource *res);
#endif /* __INCLUDE_LINUX_IRQCHIP_TI_SCI_INTA_H */
