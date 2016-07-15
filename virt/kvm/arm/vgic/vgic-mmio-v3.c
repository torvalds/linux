/*
 * VGICv3 MMIO handling functions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/irqchip/arm-gic-v3.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <kvm/iodev.h>
#include <kvm/arm_vgic.h>

#include <asm/kvm_emulate.h>

#include "vgic.h"
#include "vgic-mmio.h"

/* extract @num bytes at @offset bytes offset in data */
unsigned long extract_bytes(unsigned long data, unsigned int offset,
			    unsigned int num)
{
	return (data >> (offset * 8)) & GENMASK_ULL(num * 8 - 1, 0);
}

/* allows updates of any half of a 64-bit register (or the whole thing) */
u64 update_64bit_reg(u64 reg, unsigned int offset, unsigned int len,
		     unsigned long val)
{
	int lower = (offset & 4) * 8;
	int upper = lower + 8 * len - 1;

	reg &= ~GENMASK_ULL(upper, lower);
	val &= GENMASK_ULL(len * 8 - 1, 0);

	return reg | ((u64)val << lower);
}

bool vgic_has_its(struct kvm *kvm)
{
	struct vgic_dist *dist = &kvm->arch.vgic;

	if (dist->vgic_model != KVM_DEV_TYPE_ARM_VGIC_V3)
		return false;

	return dist->has_its;
}

static unsigned long vgic_mmio_read_v3_misc(struct kvm_vcpu *vcpu,
					    gpa_t addr, unsigned int len)
{
	u32 value = 0;

	switch (addr & 0x0c) {
	case GICD_CTLR:
		if (vcpu->kvm->arch.vgic.enabled)
			value |= GICD_CTLR_ENABLE_SS_G1;
		value |= GICD_CTLR_ARE_NS | GICD_CTLR_DS;
		break;
	case GICD_TYPER:
		value = vcpu->kvm->arch.vgic.nr_spis + VGIC_NR_PRIVATE_IRQS;
		value = (value >> 5) - 1;
		value |= (INTERRUPT_ID_BITS_SPIS - 1) << 19;
		break;
	case GICD_IIDR:
		value = (PRODUCT_ID_KVM << 24) | (IMPLEMENTER_ARM << 0);
		break;
	default:
		return 0;
	}

	return value;
}

static void vgic_mmio_write_v3_misc(struct kvm_vcpu *vcpu,
				    gpa_t addr, unsigned int len,
				    unsigned long val)
{
	struct vgic_dist *dist = &vcpu->kvm->arch.vgic;
	bool was_enabled = dist->enabled;

	switch (addr & 0x0c) {
	case GICD_CTLR:
		dist->enabled = val & GICD_CTLR_ENABLE_SS_G1;

		if (!was_enabled && dist->enabled)
			vgic_kick_vcpus(vcpu->kvm);
		break;
	case GICD_TYPER:
	case GICD_IIDR:
		return;
	}
}

static unsigned long vgic_mmio_read_irouter(struct kvm_vcpu *vcpu,
					    gpa_t addr, unsigned int len)
{
	int intid = VGIC_ADDR_TO_INTID(addr, 64);
	struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, NULL, intid);
	unsigned long ret = 0;

	if (!irq)
		return 0;

	/* The upper word is RAZ for us. */
	if (!(addr & 4))
		ret = extract_bytes(READ_ONCE(irq->mpidr), addr & 7, len);

	vgic_put_irq(vcpu->kvm, irq);
	return ret;
}

static void vgic_mmio_write_irouter(struct kvm_vcpu *vcpu,
				    gpa_t addr, unsigned int len,
				    unsigned long val)
{
	int intid = VGIC_ADDR_TO_INTID(addr, 64);
	struct vgic_irq *irq;

	/* The upper word is WI for us since we don't implement Aff3. */
	if (addr & 4)
		return;

	irq = vgic_get_irq(vcpu->kvm, NULL, intid);

	if (!irq)
		return;

	spin_lock(&irq->irq_lock);

	/* We only care about and preserve Aff0, Aff1 and Aff2. */
	irq->mpidr = val & GENMASK(23, 0);
	irq->target_vcpu = kvm_mpidr_to_vcpu(vcpu->kvm, irq->mpidr);

	spin_unlock(&irq->irq_lock);
	vgic_put_irq(vcpu->kvm, irq);
}

static unsigned long vgic_mmio_read_v3r_ctlr(struct kvm_vcpu *vcpu,
					     gpa_t addr, unsigned int len)
{
	struct vgic_cpu *vgic_cpu = &vcpu->arch.vgic_cpu;

	return vgic_cpu->lpis_enabled ? GICR_CTLR_ENABLE_LPIS : 0;
}


static void vgic_mmio_write_v3r_ctlr(struct kvm_vcpu *vcpu,
				     gpa_t addr, unsigned int len,
				     unsigned long val)
{
	struct vgic_cpu *vgic_cpu = &vcpu->arch.vgic_cpu;
	bool was_enabled = vgic_cpu->lpis_enabled;

	if (!vgic_has_its(vcpu->kvm))
		return;

	vgic_cpu->lpis_enabled = val & GICR_CTLR_ENABLE_LPIS;

	if (!was_enabled && vgic_cpu->lpis_enabled) {
		/* Eventually do something */
	}
}

static unsigned long vgic_mmio_read_v3r_typer(struct kvm_vcpu *vcpu,
					      gpa_t addr, unsigned int len)
{
	unsigned long mpidr = kvm_vcpu_get_mpidr_aff(vcpu);
	int target_vcpu_id = vcpu->vcpu_id;
	u64 value;

	value = (mpidr & GENMASK(23, 0)) << 32;
	value |= ((target_vcpu_id & 0xffff) << 8);
	if (target_vcpu_id == atomic_read(&vcpu->kvm->online_vcpus) - 1)
		value |= GICR_TYPER_LAST;

	return extract_bytes(value, addr & 7, len);
}

static unsigned long vgic_mmio_read_v3r_iidr(struct kvm_vcpu *vcpu,
					     gpa_t addr, unsigned int len)
{
	return (PRODUCT_ID_KVM << 24) | (IMPLEMENTER_ARM << 0);
}

static unsigned long vgic_mmio_read_v3_idregs(struct kvm_vcpu *vcpu,
					      gpa_t addr, unsigned int len)
{
	switch (addr & 0xffff) {
	case GICD_PIDR2:
		/* report a GICv3 compliant implementation */
		return 0x3b;
	}

	return 0;
}

/* We want to avoid outer shareable. */
u64 vgic_sanitise_shareability(u64 field)
{
	switch (field) {
	case GIC_BASER_OuterShareable:
		return GIC_BASER_InnerShareable;
	default:
		return field;
	}
}

/* Avoid any inner non-cacheable mapping. */
u64 vgic_sanitise_inner_cacheability(u64 field)
{
	switch (field) {
	case GIC_BASER_CACHE_nCnB:
	case GIC_BASER_CACHE_nC:
		return GIC_BASER_CACHE_RaWb;
	default:
		return field;
	}
}

/* Non-cacheable or same-as-inner are OK. */
u64 vgic_sanitise_outer_cacheability(u64 field)
{
	switch (field) {
	case GIC_BASER_CACHE_SameAsInner:
	case GIC_BASER_CACHE_nC:
		return field;
	default:
		return GIC_BASER_CACHE_nC;
	}
}

u64 vgic_sanitise_field(u64 reg, u64 field_mask, int field_shift,
			u64 (*sanitise_fn)(u64))
{
	u64 field = (reg & field_mask) >> field_shift;

	field = sanitise_fn(field) << field_shift;
	return (reg & ~field_mask) | field;
}

#define PROPBASER_RES0_MASK						\
	(GENMASK_ULL(63, 59) | GENMASK_ULL(55, 52) | GENMASK_ULL(6, 5))
#define PENDBASER_RES0_MASK						\
	(BIT_ULL(63) | GENMASK_ULL(61, 59) | GENMASK_ULL(55, 52) |	\
	 GENMASK_ULL(15, 12) | GENMASK_ULL(6, 0))

static u64 vgic_sanitise_pendbaser(u64 reg)
{
	reg = vgic_sanitise_field(reg, GICR_PENDBASER_SHAREABILITY_MASK,
				  GICR_PENDBASER_SHAREABILITY_SHIFT,
				  vgic_sanitise_shareability);
	reg = vgic_sanitise_field(reg, GICR_PENDBASER_INNER_CACHEABILITY_MASK,
				  GICR_PENDBASER_INNER_CACHEABILITY_SHIFT,
				  vgic_sanitise_inner_cacheability);
	reg = vgic_sanitise_field(reg, GICR_PENDBASER_OUTER_CACHEABILITY_MASK,
				  GICR_PENDBASER_OUTER_CACHEABILITY_SHIFT,
				  vgic_sanitise_outer_cacheability);

	reg &= ~PENDBASER_RES0_MASK;
	reg &= ~GENMASK_ULL(51, 48);

	return reg;
}

static u64 vgic_sanitise_propbaser(u64 reg)
{
	reg = vgic_sanitise_field(reg, GICR_PROPBASER_SHAREABILITY_MASK,
				  GICR_PROPBASER_SHAREABILITY_SHIFT,
				  vgic_sanitise_shareability);
	reg = vgic_sanitise_field(reg, GICR_PROPBASER_INNER_CACHEABILITY_MASK,
				  GICR_PROPBASER_INNER_CACHEABILITY_SHIFT,
				  vgic_sanitise_inner_cacheability);
	reg = vgic_sanitise_field(reg, GICR_PROPBASER_OUTER_CACHEABILITY_MASK,
				  GICR_PROPBASER_OUTER_CACHEABILITY_SHIFT,
				  vgic_sanitise_outer_cacheability);

	reg &= ~PROPBASER_RES0_MASK;
	reg &= ~GENMASK_ULL(51, 48);
	return reg;
}

static unsigned long vgic_mmio_read_propbase(struct kvm_vcpu *vcpu,
					     gpa_t addr, unsigned int len)
{
	struct vgic_dist *dist = &vcpu->kvm->arch.vgic;

	return extract_bytes(dist->propbaser, addr & 7, len);
}

static void vgic_mmio_write_propbase(struct kvm_vcpu *vcpu,
				     gpa_t addr, unsigned int len,
				     unsigned long val)
{
	struct vgic_dist *dist = &vcpu->kvm->arch.vgic;
	struct vgic_cpu *vgic_cpu = &vcpu->arch.vgic_cpu;
	u64 propbaser = dist->propbaser;

	/* Storing a value with LPIs already enabled is undefined */
	if (vgic_cpu->lpis_enabled)
		return;

	propbaser = update_64bit_reg(propbaser, addr & 4, len, val);
	propbaser = vgic_sanitise_propbaser(propbaser);

	dist->propbaser = propbaser;
}

static unsigned long vgic_mmio_read_pendbase(struct kvm_vcpu *vcpu,
					     gpa_t addr, unsigned int len)
{
	struct vgic_cpu *vgic_cpu = &vcpu->arch.vgic_cpu;

	return extract_bytes(vgic_cpu->pendbaser, addr & 7, len);
}

static void vgic_mmio_write_pendbase(struct kvm_vcpu *vcpu,
				     gpa_t addr, unsigned int len,
				     unsigned long val)
{
	struct vgic_cpu *vgic_cpu = &vcpu->arch.vgic_cpu;
	u64 pendbaser = vgic_cpu->pendbaser;

	/* Storing a value with LPIs already enabled is undefined */
	if (vgic_cpu->lpis_enabled)
		return;

	pendbaser = update_64bit_reg(pendbaser, addr & 4, len, val);
	pendbaser = vgic_sanitise_pendbaser(pendbaser);

	vgic_cpu->pendbaser = pendbaser;
}

/*
 * The GICv3 per-IRQ registers are split to control PPIs and SGIs in the
 * redistributors, while SPIs are covered by registers in the distributor
 * block. Trying to set private IRQs in this block gets ignored.
 * We take some special care here to fix the calculation of the register
 * offset.
 */
#define REGISTER_DESC_WITH_BITS_PER_IRQ_SHARED(off, rd, wr, bpi, acc)	\
	{								\
		.reg_offset = off,					\
		.bits_per_irq = bpi,					\
		.len = (bpi * VGIC_NR_PRIVATE_IRQS) / 8,		\
		.access_flags = acc,					\
		.read = vgic_mmio_read_raz,				\
		.write = vgic_mmio_write_wi,				\
	}, {								\
		.reg_offset = off + (bpi * VGIC_NR_PRIVATE_IRQS) / 8,	\
		.bits_per_irq = bpi,					\
		.len = (bpi * (1024 - VGIC_NR_PRIVATE_IRQS)) / 8,	\
		.access_flags = acc,					\
		.read = rd,						\
		.write = wr,						\
	}

static const struct vgic_register_region vgic_v3_dist_registers[] = {
	REGISTER_DESC_WITH_LENGTH(GICD_CTLR,
		vgic_mmio_read_v3_misc, vgic_mmio_write_v3_misc, 16,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ_SHARED(GICD_IGROUPR,
		vgic_mmio_read_rao, vgic_mmio_write_wi, 1,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ_SHARED(GICD_ISENABLER,
		vgic_mmio_read_enable, vgic_mmio_write_senable, 1,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ_SHARED(GICD_ICENABLER,
		vgic_mmio_read_enable, vgic_mmio_write_cenable, 1,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ_SHARED(GICD_ISPENDR,
		vgic_mmio_read_pending, vgic_mmio_write_spending, 1,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ_SHARED(GICD_ICPENDR,
		vgic_mmio_read_pending, vgic_mmio_write_cpending, 1,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ_SHARED(GICD_ISACTIVER,
		vgic_mmio_read_active, vgic_mmio_write_sactive, 1,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ_SHARED(GICD_ICACTIVER,
		vgic_mmio_read_active, vgic_mmio_write_cactive, 1,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ_SHARED(GICD_IPRIORITYR,
		vgic_mmio_read_priority, vgic_mmio_write_priority, 8,
		VGIC_ACCESS_32bit | VGIC_ACCESS_8bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ_SHARED(GICD_ITARGETSR,
		vgic_mmio_read_raz, vgic_mmio_write_wi, 8,
		VGIC_ACCESS_32bit | VGIC_ACCESS_8bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ_SHARED(GICD_ICFGR,
		vgic_mmio_read_config, vgic_mmio_write_config, 2,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ_SHARED(GICD_IGRPMODR,
		vgic_mmio_read_raz, vgic_mmio_write_wi, 1,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ_SHARED(GICD_IROUTER,
		vgic_mmio_read_irouter, vgic_mmio_write_irouter, 64,
		VGIC_ACCESS_64bit | VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_LENGTH(GICD_IDREGS,
		vgic_mmio_read_v3_idregs, vgic_mmio_write_wi, 48,
		VGIC_ACCESS_32bit),
};

static const struct vgic_register_region vgic_v3_rdbase_registers[] = {
	REGISTER_DESC_WITH_LENGTH(GICR_CTLR,
		vgic_mmio_read_v3r_ctlr, vgic_mmio_write_v3r_ctlr, 4,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_LENGTH(GICR_IIDR,
		vgic_mmio_read_v3r_iidr, vgic_mmio_write_wi, 4,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_LENGTH(GICR_TYPER,
		vgic_mmio_read_v3r_typer, vgic_mmio_write_wi, 8,
		VGIC_ACCESS_64bit | VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_LENGTH(GICR_PROPBASER,
		vgic_mmio_read_propbase, vgic_mmio_write_propbase, 8,
		VGIC_ACCESS_64bit | VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_LENGTH(GICR_PENDBASER,
		vgic_mmio_read_pendbase, vgic_mmio_write_pendbase, 8,
		VGIC_ACCESS_64bit | VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_LENGTH(GICR_IDREGS,
		vgic_mmio_read_v3_idregs, vgic_mmio_write_wi, 48,
		VGIC_ACCESS_32bit),
};

static const struct vgic_register_region vgic_v3_sgibase_registers[] = {
	REGISTER_DESC_WITH_LENGTH(GICR_IGROUPR0,
		vgic_mmio_read_rao, vgic_mmio_write_wi, 4,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_LENGTH(GICR_ISENABLER0,
		vgic_mmio_read_enable, vgic_mmio_write_senable, 4,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_LENGTH(GICR_ICENABLER0,
		vgic_mmio_read_enable, vgic_mmio_write_cenable, 4,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_LENGTH(GICR_ISPENDR0,
		vgic_mmio_read_pending, vgic_mmio_write_spending, 4,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_LENGTH(GICR_ICPENDR0,
		vgic_mmio_read_pending, vgic_mmio_write_cpending, 4,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_LENGTH(GICR_ISACTIVER0,
		vgic_mmio_read_active, vgic_mmio_write_sactive, 4,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_LENGTH(GICR_ICACTIVER0,
		vgic_mmio_read_active, vgic_mmio_write_cactive, 4,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_LENGTH(GICR_IPRIORITYR0,
		vgic_mmio_read_priority, vgic_mmio_write_priority, 32,
		VGIC_ACCESS_32bit | VGIC_ACCESS_8bit),
	REGISTER_DESC_WITH_LENGTH(GICR_ICFGR0,
		vgic_mmio_read_config, vgic_mmio_write_config, 8,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_LENGTH(GICR_IGRPMODR0,
		vgic_mmio_read_raz, vgic_mmio_write_wi, 4,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_LENGTH(GICR_NSACR,
		vgic_mmio_read_raz, vgic_mmio_write_wi, 4,
		VGIC_ACCESS_32bit),
};

unsigned int vgic_v3_init_dist_iodev(struct vgic_io_device *dev)
{
	dev->regions = vgic_v3_dist_registers;
	dev->nr_regions = ARRAY_SIZE(vgic_v3_dist_registers);

	kvm_iodevice_init(&dev->dev, &kvm_io_gic_ops);

	return SZ_64K;
}

int vgic_register_redist_iodevs(struct kvm *kvm, gpa_t redist_base_address)
{
	struct kvm_vcpu *vcpu;
	int c, ret = 0;

	kvm_for_each_vcpu(c, vcpu, kvm) {
		gpa_t rd_base = redist_base_address + c * SZ_64K * 2;
		gpa_t sgi_base = rd_base + SZ_64K;
		struct vgic_io_device *rd_dev = &vcpu->arch.vgic_cpu.rd_iodev;
		struct vgic_io_device *sgi_dev = &vcpu->arch.vgic_cpu.sgi_iodev;

		kvm_iodevice_init(&rd_dev->dev, &kvm_io_gic_ops);
		rd_dev->base_addr = rd_base;
		rd_dev->iodev_type = IODEV_REDIST;
		rd_dev->regions = vgic_v3_rdbase_registers;
		rd_dev->nr_regions = ARRAY_SIZE(vgic_v3_rdbase_registers);
		rd_dev->redist_vcpu = vcpu;

		mutex_lock(&kvm->slots_lock);
		ret = kvm_io_bus_register_dev(kvm, KVM_MMIO_BUS, rd_base,
					      SZ_64K, &rd_dev->dev);
		mutex_unlock(&kvm->slots_lock);

		if (ret)
			break;

		kvm_iodevice_init(&sgi_dev->dev, &kvm_io_gic_ops);
		sgi_dev->base_addr = sgi_base;
		sgi_dev->iodev_type = IODEV_REDIST;
		sgi_dev->regions = vgic_v3_sgibase_registers;
		sgi_dev->nr_regions = ARRAY_SIZE(vgic_v3_sgibase_registers);
		sgi_dev->redist_vcpu = vcpu;

		mutex_lock(&kvm->slots_lock);
		ret = kvm_io_bus_register_dev(kvm, KVM_MMIO_BUS, sgi_base,
					      SZ_64K, &sgi_dev->dev);
		mutex_unlock(&kvm->slots_lock);
		if (ret) {
			kvm_io_bus_unregister_dev(kvm, KVM_MMIO_BUS,
						  &rd_dev->dev);
			break;
		}
	}

	if (ret) {
		/* The current c failed, so we start with the previous one. */
		for (c--; c >= 0; c--) {
			struct vgic_cpu *vgic_cpu;

			vcpu = kvm_get_vcpu(kvm, c);
			vgic_cpu = &vcpu->arch.vgic_cpu;
			kvm_io_bus_unregister_dev(kvm, KVM_MMIO_BUS,
						  &vgic_cpu->rd_iodev.dev);
			kvm_io_bus_unregister_dev(kvm, KVM_MMIO_BUS,
						  &vgic_cpu->sgi_iodev.dev);
		}
	}

	return ret;
}

/*
 * Compare a given affinity (level 1-3 and a level 0 mask, from the SGI
 * generation register ICC_SGI1R_EL1) with a given VCPU.
 * If the VCPU's MPIDR matches, return the level0 affinity, otherwise
 * return -1.
 */
static int match_mpidr(u64 sgi_aff, u16 sgi_cpu_mask, struct kvm_vcpu *vcpu)
{
	unsigned long affinity;
	int level0;

	/*
	 * Split the current VCPU's MPIDR into affinity level 0 and the
	 * rest as this is what we have to compare against.
	 */
	affinity = kvm_vcpu_get_mpidr_aff(vcpu);
	level0 = MPIDR_AFFINITY_LEVEL(affinity, 0);
	affinity &= ~MPIDR_LEVEL_MASK;

	/* bail out if the upper three levels don't match */
	if (sgi_aff != affinity)
		return -1;

	/* Is this VCPU's bit set in the mask ? */
	if (!(sgi_cpu_mask & BIT(level0)))
		return -1;

	return level0;
}

/*
 * The ICC_SGI* registers encode the affinity differently from the MPIDR,
 * so provide a wrapper to use the existing defines to isolate a certain
 * affinity level.
 */
#define SGI_AFFINITY_LEVEL(reg, level) \
	((((reg) & ICC_SGI1R_AFFINITY_## level ##_MASK) \
	>> ICC_SGI1R_AFFINITY_## level ##_SHIFT) << MPIDR_LEVEL_SHIFT(level))

/**
 * vgic_v3_dispatch_sgi - handle SGI requests from VCPUs
 * @vcpu: The VCPU requesting a SGI
 * @reg: The value written into the ICC_SGI1R_EL1 register by that VCPU
 *
 * With GICv3 (and ARE=1) CPUs trigger SGIs by writing to a system register.
 * This will trap in sys_regs.c and call this function.
 * This ICC_SGI1R_EL1 register contains the upper three affinity levels of the
 * target processors as well as a bitmask of 16 Aff0 CPUs.
 * If the interrupt routing mode bit is not set, we iterate over all VCPUs to
 * check for matching ones. If this bit is set, we signal all, but not the
 * calling VCPU.
 */
void vgic_v3_dispatch_sgi(struct kvm_vcpu *vcpu, u64 reg)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvm_vcpu *c_vcpu;
	u16 target_cpus;
	u64 mpidr;
	int sgi, c;
	int vcpu_id = vcpu->vcpu_id;
	bool broadcast;

	sgi = (reg & ICC_SGI1R_SGI_ID_MASK) >> ICC_SGI1R_SGI_ID_SHIFT;
	broadcast = reg & BIT(ICC_SGI1R_IRQ_ROUTING_MODE_BIT);
	target_cpus = (reg & ICC_SGI1R_TARGET_LIST_MASK) >> ICC_SGI1R_TARGET_LIST_SHIFT;
	mpidr = SGI_AFFINITY_LEVEL(reg, 3);
	mpidr |= SGI_AFFINITY_LEVEL(reg, 2);
	mpidr |= SGI_AFFINITY_LEVEL(reg, 1);

	/*
	 * We iterate over all VCPUs to find the MPIDRs matching the request.
	 * If we have handled one CPU, we clear its bit to detect early
	 * if we are already finished. This avoids iterating through all
	 * VCPUs when most of the times we just signal a single VCPU.
	 */
	kvm_for_each_vcpu(c, c_vcpu, kvm) {
		struct vgic_irq *irq;

		/* Exit early if we have dealt with all requested CPUs */
		if (!broadcast && target_cpus == 0)
			break;

		/* Don't signal the calling VCPU */
		if (broadcast && c == vcpu_id)
			continue;

		if (!broadcast) {
			int level0;

			level0 = match_mpidr(mpidr, target_cpus, c_vcpu);
			if (level0 == -1)
				continue;

			/* remove this matching VCPU from the mask */
			target_cpus &= ~BIT(level0);
		}

		irq = vgic_get_irq(vcpu->kvm, c_vcpu, sgi);

		spin_lock(&irq->irq_lock);
		irq->pending = true;

		vgic_queue_irq_unlock(vcpu->kvm, irq);
		vgic_put_irq(vcpu->kvm, irq);
	}
}
