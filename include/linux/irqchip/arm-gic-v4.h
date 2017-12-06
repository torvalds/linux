/*
 * Copyright (C) 2016,2017 ARM Limited, All Rights Reserved.
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

#ifndef __LINUX_IRQCHIP_ARM_GIC_V4_H
#define __LINUX_IRQCHIP_ARM_GIC_V4_H

struct its_vpe;

/* Embedded in kvm.arch */
struct its_vm {
	struct fwnode_handle	*fwnode;
	struct irq_domain	*domain;
	struct page		*vprop_page;
	struct its_vpe		**vpes;
	int			nr_vpes;
	irq_hw_number_t		db_lpi_base;
	unsigned long		*db_bitmap;
	int			nr_db_lpis;
};

/* Embedded in kvm_vcpu.arch */
struct its_vpe {
	struct page 		*vpt_page;
	struct its_vm		*its_vm;
	/* Doorbell interrupt */
	int			irq;
	irq_hw_number_t		vpe_db_lpi;
	/* VPE proxy mapping */
	int			vpe_proxy_event;
	/*
	 * This collection ID is used to indirect the target
	 * redistributor for this VPE. The ID itself isn't involved in
	 * programming of the ITS.
	 */
	u16			col_idx;
	/* Unique (system-wide) VPE identifier */
	u16			vpe_id;
	/* Implementation Defined Area Invalid */
	bool			idai;
	/* Pending VLPIs on schedule out? */
	bool			pending_last;
};

/*
 * struct its_vlpi_map: structure describing the mapping of a
 * VLPI. Only to be interpreted in the context of a physical interrupt
 * it complements.  To be used as the vcpu_info passed to
 * irq_set_vcpu_affinity().
 *
 * @vm:		Pointer to the GICv4 notion of a VM
 * @vpe:	Pointer to the GICv4 notion of a virtual CPU (VPE)
 * @vintid:	Virtual LPI number
 * @db_enabled:	Is the VPE doorbell to be generated?
 */
struct its_vlpi_map {
	struct its_vm		*vm;
	struct its_vpe		*vpe;
	u32			vintid;
	bool			db_enabled;
};

enum its_vcpu_info_cmd_type {
	MAP_VLPI,
	GET_VLPI,
	PROP_UPDATE_VLPI,
	PROP_UPDATE_AND_INV_VLPI,
	SCHEDULE_VPE,
	DESCHEDULE_VPE,
	INVALL_VPE,
};

struct its_cmd_info {
	enum its_vcpu_info_cmd_type	cmd_type;
	union {
		struct its_vlpi_map	*map;
		u8			config;
	};
};

int its_alloc_vcpu_irqs(struct its_vm *vm);
void its_free_vcpu_irqs(struct its_vm *vm);
int its_schedule_vpe(struct its_vpe *vpe, bool on);
int its_invall_vpe(struct its_vpe *vpe);
int its_map_vlpi(int irq, struct its_vlpi_map *map);
int its_get_vlpi(int irq, struct its_vlpi_map *map);
int its_unmap_vlpi(int irq);
int its_prop_update_vlpi(int irq, u8 config, bool inv);

int its_init_v4(struct irq_domain *domain, const struct irq_domain_ops *ops);

#endif
