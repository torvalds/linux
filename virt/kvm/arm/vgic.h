/*
 * Copyright (C) 2012-2014 ARM Ltd.
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * Derived from virt/kvm/arm/vgic.c
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

#ifndef __KVM_VGIC_H__
#define __KVM_VGIC_H__

#include <kvm/iodev.h>

#define VGIC_ADDR_UNDEF		(-1)
#define IS_VGIC_ADDR_UNDEF(_x)  ((_x) == VGIC_ADDR_UNDEF)

#define PRODUCT_ID_KVM		0x4b	/* ASCII code K */
#define IMPLEMENTER_ARM		0x43b

#define ACCESS_READ_VALUE	(1 << 0)
#define ACCESS_READ_RAZ		(0 << 0)
#define ACCESS_READ_MASK(x)	((x) & (1 << 0))
#define ACCESS_WRITE_IGNORED	(0 << 1)
#define ACCESS_WRITE_SETBIT	(1 << 1)
#define ACCESS_WRITE_CLEARBIT	(2 << 1)
#define ACCESS_WRITE_VALUE	(3 << 1)
#define ACCESS_WRITE_MASK(x)	((x) & (3 << 1))

#define VCPU_NOT_ALLOCATED	((u8)-1)

unsigned long *vgic_bitmap_get_shared_map(struct vgic_bitmap *x);

void vgic_update_state(struct kvm *kvm);
int vgic_init_common_maps(struct kvm *kvm);

u32 *vgic_bitmap_get_reg(struct vgic_bitmap *x, int cpuid, u32 offset);
u32 *vgic_bytemap_get_reg(struct vgic_bytemap *x, int cpuid, u32 offset);

void vgic_dist_irq_set_pending(struct kvm_vcpu *vcpu, int irq);
void vgic_dist_irq_clear_pending(struct kvm_vcpu *vcpu, int irq);
void vgic_cpu_irq_clear(struct kvm_vcpu *vcpu, int irq);
void vgic_bitmap_set_irq_val(struct vgic_bitmap *x, int cpuid,
			     int irq, int val);

void vgic_get_vmcr(struct kvm_vcpu *vcpu, struct vgic_vmcr *vmcr);
void vgic_set_vmcr(struct kvm_vcpu *vcpu, struct vgic_vmcr *vmcr);

bool vgic_queue_irq(struct kvm_vcpu *vcpu, u8 sgi_source_id, int irq);
void vgic_unqueue_irqs(struct kvm_vcpu *vcpu);

struct kvm_exit_mmio {
	phys_addr_t	phys_addr;
	void		*data;
	u32		len;
	bool		is_write;
	void		*private;
};

void vgic_reg_access(struct kvm_exit_mmio *mmio, u32 *reg,
		     phys_addr_t offset, int mode);
bool handle_mmio_raz_wi(struct kvm_vcpu *vcpu, struct kvm_exit_mmio *mmio,
			phys_addr_t offset);

static inline
u32 mmio_data_read(struct kvm_exit_mmio *mmio, u32 mask)
{
	return le32_to_cpu(*((u32 *)mmio->data)) & mask;
}

static inline
void mmio_data_write(struct kvm_exit_mmio *mmio, u32 mask, u32 value)
{
	*((u32 *)mmio->data) = cpu_to_le32(value) & mask;
}

struct vgic_io_range {
	phys_addr_t base;
	unsigned long len;
	int bits_per_irq;
	bool (*handle_mmio)(struct kvm_vcpu *vcpu, struct kvm_exit_mmio *mmio,
			    phys_addr_t offset);
};

int vgic_register_kvm_io_dev(struct kvm *kvm, gpa_t base, int len,
			     const struct vgic_io_range *ranges,
			     int redist_id,
			     struct vgic_io_device *iodev);

static inline bool is_in_range(phys_addr_t addr, unsigned long len,
			       phys_addr_t baseaddr, unsigned long size)
{
	return (addr >= baseaddr) && (addr + len <= baseaddr + size);
}

const
struct vgic_io_range *vgic_find_range(const struct vgic_io_range *ranges,
				      int len, gpa_t offset);

bool vgic_handle_enable_reg(struct kvm *kvm, struct kvm_exit_mmio *mmio,
			    phys_addr_t offset, int vcpu_id, int access);

bool vgic_handle_set_pending_reg(struct kvm *kvm, struct kvm_exit_mmio *mmio,
				 phys_addr_t offset, int vcpu_id);

bool vgic_handle_clear_pending_reg(struct kvm *kvm, struct kvm_exit_mmio *mmio,
				   phys_addr_t offset, int vcpu_id);

bool vgic_handle_set_active_reg(struct kvm *kvm,
				struct kvm_exit_mmio *mmio,
				phys_addr_t offset, int vcpu_id);

bool vgic_handle_clear_active_reg(struct kvm *kvm,
				  struct kvm_exit_mmio *mmio,
				  phys_addr_t offset, int vcpu_id);

bool vgic_handle_cfg_reg(u32 *reg, struct kvm_exit_mmio *mmio,
			 phys_addr_t offset);

void vgic_kick_vcpus(struct kvm *kvm);

int vgic_has_attr_regs(const struct vgic_io_range *ranges, phys_addr_t offset);
int vgic_set_common_attr(struct kvm_device *dev, struct kvm_device_attr *attr);
int vgic_get_common_attr(struct kvm_device *dev, struct kvm_device_attr *attr);

int vgic_init(struct kvm *kvm);
void vgic_v2_init_emulation(struct kvm *kvm);
void vgic_v3_init_emulation(struct kvm *kvm);

#endif
