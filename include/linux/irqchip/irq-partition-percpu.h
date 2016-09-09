/*
 * Copyright (C) 2016 ARM Limited, All Rights Reserved.
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/fwnode.h>
#include <linux/cpumask.h>
#include <linux/irqdomain.h>

struct partition_affinity {
	cpumask_t			mask;
	void				*partition_id;
};

struct partition_desc;

#ifdef CONFIG_PARTITION_PERCPU
int partition_translate_id(struct partition_desc *desc, void *partition_id);
struct partition_desc *partition_create_desc(struct fwnode_handle *fwnode,
					     struct partition_affinity *parts,
					     int nr_parts,
					     int chained_irq,
					     const struct irq_domain_ops *ops);
struct irq_domain *partition_get_domain(struct partition_desc *dsc);
#else
static inline int partition_translate_id(struct partition_desc *desc,
					 void *partition_id)
{
	return -EINVAL;
}

static inline
struct partition_desc *partition_create_desc(struct fwnode_handle *fwnode,
					     struct partition_affinity *parts,
					     int nr_parts,
					     int chained_irq,
					     const struct irq_domain_ops *ops)
{
	return NULL;
}

static inline
struct irq_domain *partition_get_domain(struct partition_desc *dsc)
{
	return NULL;
}
#endif
