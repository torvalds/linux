/*
 * Copyright (C) 2016, Semihalf
 *	Author: Tomasz Nowicki <tn@semihalf.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 */

#ifndef __ACPI_IORT_H__
#define __ACPI_IORT_H__

#include <linux/acpi.h>
#include <linux/fwnode.h>
#include <linux/irqdomain.h>

int iort_register_domain_token(int trans_id, struct fwnode_handle *fw_node);
void iort_deregister_domain_token(int trans_id);
struct fwnode_handle *iort_find_domain_token(int trans_id);
#ifdef CONFIG_ACPI_IORT
void acpi_iort_init(void);
u32 iort_msi_map_rid(struct device *dev, u32 req_id);
struct irq_domain *iort_get_device_domain(struct device *dev, u32 req_id);
#else
static inline void acpi_iort_init(void) { }
static inline u32 iort_msi_map_rid(struct device *dev, u32 req_id)
{ return req_id; }
static inline struct irq_domain *iort_get_device_domain(struct device *dev,
							u32 req_id)
{ return NULL; }
#endif

#define IORT_ACPI_DECLARE(name, table_id, fn)		\
	ACPI_DECLARE_PROBE_ENTRY(iort, name, table_id, 0, NULL, 0, fn)

#endif /* __ACPI_IORT_H__ */
