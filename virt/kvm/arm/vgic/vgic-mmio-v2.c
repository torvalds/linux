// SPDX-License-Identifier: GPL-2.0-only
/*
 * VGICv2 MMIO handling functions
 */

#include <linux/irqchip/arm-gic.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/nospec.h>

#include <kvm/iodev.h>
#include <kvm/arm_vgic.h>

#include "vgic.h"
#include "vgic-mmio.h"

/*
 * The Revision field in the IIDR have the following meanings:
 *
 * Revision 1: Report GICv2 interrupts as group 0 instead of group 1
 * Revision 2: Interrupt groups are guest-configurable and signaled using
 * 	       their configured groups.
 */

static unsigned long vgic_mmio_read_v2_misc(struct kvm_vcpu *vcpu,
					    gpa_t addr, unsigned int len)
{
	struct vgic_dist *vgic = &vcpu->kvm->arch.vgic;
	u32 value;

	switch (addr & 0x0c) {
	case GIC_DIST_CTRL:
		value = vgic->enabled ? GICD_ENABLE : 0;
		break;
	case GIC_DIST_CTR:
		value = vgic->nr_spis + VGIC_NR_PRIVATE_IRQS;
		value = (value >> 5) - 1;
		value |= (atomic_read(&vcpu->kvm->online_vcpus) - 1) << 5;
		break;
	case GIC_DIST_IIDR:
		value = (PRODUCT_ID_KVM << GICD_IIDR_PRODUCT_ID_SHIFT) |
			(vgic->implementation_rev << GICD_IIDR_REVISION_SHIFT) |
			(IMPLEMENTER_ARM << GICD_IIDR_IMPLEMENTER_SHIFT);
		break;
	default:
		return 0;
	}

	return value;
}

static void vgic_mmio_write_v2_misc(struct kvm_vcpu *vcpu,
				    gpa_t addr, unsigned int len,
				    unsigned long val)
{
	struct vgic_dist *dist = &vcpu->kvm->arch.vgic;
	bool was_enabled = dist->enabled;

	switch (addr & 0x0c) {
	case GIC_DIST_CTRL:
		dist->enabled = val & GICD_ENABLE;
		if (!was_enabled && dist->enabled)
			vgic_kick_vcpus(vcpu->kvm);
		break;
	case GIC_DIST_CTR:
	case GIC_DIST_IIDR:
		/* Nothing to do */
		return;
	}
}

static int vgic_mmio_uaccess_write_v2_misc(struct kvm_vcpu *vcpu,
					   gpa_t addr, unsigned int len,
					   unsigned long val)
{
	switch (addr & 0x0c) {
	case GIC_DIST_IIDR:
		if (val != vgic_mmio_read_v2_misc(vcpu, addr, len))
			return -EINVAL;

		/*
		 * If we observe a write to GICD_IIDR we know that userspace
		 * has been updated and has had a chance to cope with older
		 * kernels (VGICv2 IIDR.Revision == 0) incorrectly reporting
		 * interrupts as group 1, and therefore we now allow groups to
		 * be user writable.  Doing this by default would break
		 * migration from old kernels to new kernels with legacy
		 * userspace.
		 */
		vcpu->kvm->arch.vgic.v2_groups_user_writable = true;
		return 0;
	}

	vgic_mmio_write_v2_misc(vcpu, addr, len, val);
	return 0;
}

static int vgic_mmio_uaccess_write_v2_group(struct kvm_vcpu *vcpu,
					    gpa_t addr, unsigned int len,
					    unsigned long val)
{
	if (vcpu->kvm->arch.vgic.v2_groups_user_writable)
		vgic_mmio_write_group(vcpu, addr, len, val);

	return 0;
}

static void vgic_mmio_write_sgir(struct kvm_vcpu *source_vcpu,
				 gpa_t addr, unsigned int len,
				 unsigned long val)
{
	int nr_vcpus = atomic_read(&source_vcpu->kvm->online_vcpus);
	int intid = val & 0xf;
	int targets = (val >> 16) & 0xff;
	int mode = (val >> 24) & 0x03;
	int c;
	struct kvm_vcpu *vcpu;
	unsigned long flags;

	switch (mode) {
	case 0x0:		/* as specified by targets */
		break;
	case 0x1:
		targets = (1U << nr_vcpus) - 1;			/* all, ... */
		targets &= ~(1U << source_vcpu->vcpu_id);	/* but self */
		break;
	case 0x2:		/* this very vCPU only */
		targets = (1U << source_vcpu->vcpu_id);
		break;
	case 0x3:		/* reserved */
		return;
	}

	kvm_for_each_vcpu(c, vcpu, source_vcpu->kvm) {
		struct vgic_irq *irq;

		if (!(targets & (1U << c)))
			continue;

		irq = vgic_get_irq(source_vcpu->kvm, vcpu, intid);

		raw_spin_lock_irqsave(&irq->irq_lock, flags);
		irq->pending_latch = true;
		irq->source |= 1U << source_vcpu->vcpu_id;

		vgic_queue_irq_unlock(source_vcpu->kvm, irq, flags);
		vgic_put_irq(source_vcpu->kvm, irq);
	}
}

static unsigned long vgic_mmio_read_target(struct kvm_vcpu *vcpu,
					   gpa_t addr, unsigned int len)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 8);
	int i;
	u64 val = 0;

	for (i = 0; i < len; i++) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);

		val |= (u64)irq->targets << (i * 8);

		vgic_put_irq(vcpu->kvm, irq);
	}

	return val;
}

static void vgic_mmio_write_target(struct kvm_vcpu *vcpu,
				   gpa_t addr, unsigned int len,
				   unsigned long val)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 8);
	u8 cpu_mask = GENMASK(atomic_read(&vcpu->kvm->online_vcpus) - 1, 0);
	int i;
	unsigned long flags;

	/* GICD_ITARGETSR[0-7] are read-only */
	if (intid < VGIC_NR_PRIVATE_IRQS)
		return;

	for (i = 0; i < len; i++) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, NULL, intid + i);
		int target;

		raw_spin_lock_irqsave(&irq->irq_lock, flags);

		irq->targets = (val >> (i * 8)) & cpu_mask;
		target = irq->targets ? __ffs(irq->targets) : 0;
		irq->target_vcpu = kvm_get_vcpu(vcpu->kvm, target);

		raw_spin_unlock_irqrestore(&irq->irq_lock, flags);
		vgic_put_irq(vcpu->kvm, irq);
	}
}

static unsigned long vgic_mmio_read_sgipend(struct kvm_vcpu *vcpu,
					    gpa_t addr, unsigned int len)
{
	u32 intid = addr & 0x0f;
	int i;
	u64 val = 0;

	for (i = 0; i < len; i++) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);

		val |= (u64)irq->source << (i * 8);

		vgic_put_irq(vcpu->kvm, irq);
	}
	return val;
}

static void vgic_mmio_write_sgipendc(struct kvm_vcpu *vcpu,
				     gpa_t addr, unsigned int len,
				     unsigned long val)
{
	u32 intid = addr & 0x0f;
	int i;
	unsigned long flags;

	for (i = 0; i < len; i++) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);

		raw_spin_lock_irqsave(&irq->irq_lock, flags);

		irq->source &= ~((val >> (i * 8)) & 0xff);
		if (!irq->source)
			irq->pending_latch = false;

		raw_spin_unlock_irqrestore(&irq->irq_lock, flags);
		vgic_put_irq(vcpu->kvm, irq);
	}
}

static void vgic_mmio_write_sgipends(struct kvm_vcpu *vcpu,
				     gpa_t addr, unsigned int len,
				     unsigned long val)
{
	u32 intid = addr & 0x0f;
	int i;
	unsigned long flags;

	for (i = 0; i < len; i++) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);

		raw_spin_lock_irqsave(&irq->irq_lock, flags);

		irq->source |= (val >> (i * 8)) & 0xff;

		if (irq->source) {
			irq->pending_latch = true;
			vgic_queue_irq_unlock(vcpu->kvm, irq, flags);
		} else {
			raw_spin_unlock_irqrestore(&irq->irq_lock, flags);
		}
		vgic_put_irq(vcpu->kvm, irq);
	}
}

#define GICC_ARCH_VERSION_V2	0x2

/* These are for userland accesses only, there is no guest-facing emulation. */
static unsigned long vgic_mmio_read_vcpuif(struct kvm_vcpu *vcpu,
					   gpa_t addr, unsigned int len)
{
	struct vgic_vmcr vmcr;
	u32 val;

	vgic_get_vmcr(vcpu, &vmcr);

	switch (addr & 0xff) {
	case GIC_CPU_CTRL:
		val = vmcr.grpen0 << GIC_CPU_CTRL_EnableGrp0_SHIFT;
		val |= vmcr.grpen1 << GIC_CPU_CTRL_EnableGrp1_SHIFT;
		val |= vmcr.ackctl << GIC_CPU_CTRL_AckCtl_SHIFT;
		val |= vmcr.fiqen << GIC_CPU_CTRL_FIQEn_SHIFT;
		val |= vmcr.cbpr << GIC_CPU_CTRL_CBPR_SHIFT;
		val |= vmcr.eoim << GIC_CPU_CTRL_EOImodeNS_SHIFT;

		break;
	case GIC_CPU_PRIMASK:
		/*
		 * Our KVM_DEV_TYPE_ARM_VGIC_V2 device ABI exports the
		 * the PMR field as GICH_VMCR.VMPriMask rather than
		 * GICC_PMR.Priority, so we expose the upper five bits of
		 * priority mask to userspace using the lower bits in the
		 * unsigned long.
		 */
		val = (vmcr.pmr & GICV_PMR_PRIORITY_MASK) >>
			GICV_PMR_PRIORITY_SHIFT;
		break;
	case GIC_CPU_BINPOINT:
		val = vmcr.bpr;
		break;
	case GIC_CPU_ALIAS_BINPOINT:
		val = vmcr.abpr;
		break;
	case GIC_CPU_IDENT:
		val = ((PRODUCT_ID_KVM << 20) |
		       (GICC_ARCH_VERSION_V2 << 16) |
		       IMPLEMENTER_ARM);
		break;
	default:
		return 0;
	}

	return val;
}

static void vgic_mmio_write_vcpuif(struct kvm_vcpu *vcpu,
				   gpa_t addr, unsigned int len,
				   unsigned long val)
{
	struct vgic_vmcr vmcr;

	vgic_get_vmcr(vcpu, &vmcr);

	switch (addr & 0xff) {
	case GIC_CPU_CTRL:
		vmcr.grpen0 = !!(val & GIC_CPU_CTRL_EnableGrp0);
		vmcr.grpen1 = !!(val & GIC_CPU_CTRL_EnableGrp1);
		vmcr.ackctl = !!(val & GIC_CPU_CTRL_AckCtl);
		vmcr.fiqen = !!(val & GIC_CPU_CTRL_FIQEn);
		vmcr.cbpr = !!(val & GIC_CPU_CTRL_CBPR);
		vmcr.eoim = !!(val & GIC_CPU_CTRL_EOImodeNS);

		break;
	case GIC_CPU_PRIMASK:
		/*
		 * Our KVM_DEV_TYPE_ARM_VGIC_V2 device ABI exports the
		 * the PMR field as GICH_VMCR.VMPriMask rather than
		 * GICC_PMR.Priority, so we expose the upper five bits of
		 * priority mask to userspace using the lower bits in the
		 * unsigned long.
		 */
		vmcr.pmr = (val << GICV_PMR_PRIORITY_SHIFT) &
			GICV_PMR_PRIORITY_MASK;
		break;
	case GIC_CPU_BINPOINT:
		vmcr.bpr = val;
		break;
	case GIC_CPU_ALIAS_BINPOINT:
		vmcr.abpr = val;
		break;
	}

	vgic_set_vmcr(vcpu, &vmcr);
}

static unsigned long vgic_mmio_read_apr(struct kvm_vcpu *vcpu,
					gpa_t addr, unsigned int len)
{
	int n; /* which APRn is this */

	n = (addr >> 2) & 0x3;

	if (kvm_vgic_global_state.type == VGIC_V2) {
		/* GICv2 hardware systems support max. 32 groups */
		if (n != 0)
			return 0;
		return vcpu->arch.vgic_cpu.vgic_v2.vgic_apr;
	} else {
		struct vgic_v3_cpu_if *vgicv3 = &vcpu->arch.vgic_cpu.vgic_v3;

		if (n > vgic_v3_max_apr_idx(vcpu))
			return 0;

		n = array_index_nospec(n, 4);

		/* GICv3 only uses ICH_AP1Rn for memory mapped (GICv2) guests */
		return vgicv3->vgic_ap1r[n];
	}
}

static void vgic_mmio_write_apr(struct kvm_vcpu *vcpu,
				gpa_t addr, unsigned int len,
				unsigned long val)
{
	int n; /* which APRn is this */

	n = (addr >> 2) & 0x3;

	if (kvm_vgic_global_state.type == VGIC_V2) {
		/* GICv2 hardware systems support max. 32 groups */
		if (n != 0)
			return;
		vcpu->arch.vgic_cpu.vgic_v2.vgic_apr = val;
	} else {
		struct vgic_v3_cpu_if *vgicv3 = &vcpu->arch.vgic_cpu.vgic_v3;

		if (n > vgic_v3_max_apr_idx(vcpu))
			return;

		n = array_index_nospec(n, 4);

		/* GICv3 only uses ICH_AP1Rn for memory mapped (GICv2) guests */
		vgicv3->vgic_ap1r[n] = val;
	}
}

static const struct vgic_register_region vgic_v2_dist_registers[] = {
	REGISTER_DESC_WITH_LENGTH_UACCESS(GIC_DIST_CTRL,
		vgic_mmio_read_v2_misc, vgic_mmio_write_v2_misc,
		NULL, vgic_mmio_uaccess_write_v2_misc,
		12, VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ(GIC_DIST_IGROUP,
		vgic_mmio_read_group, vgic_mmio_write_group,
		NULL, vgic_mmio_uaccess_write_v2_group, 1,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ(GIC_DIST_ENABLE_SET,
		vgic_mmio_read_enable, vgic_mmio_write_senable, NULL, NULL, 1,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ(GIC_DIST_ENABLE_CLEAR,
		vgic_mmio_read_enable, vgic_mmio_write_cenable, NULL, NULL, 1,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ(GIC_DIST_PENDING_SET,
		vgic_mmio_read_pending, vgic_mmio_write_spending, NULL, NULL, 1,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ(GIC_DIST_PENDING_CLEAR,
		vgic_mmio_read_pending, vgic_mmio_write_cpending, NULL, NULL, 1,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ(GIC_DIST_ACTIVE_SET,
		vgic_mmio_read_active, vgic_mmio_write_sactive,
		NULL, vgic_mmio_uaccess_write_sactive, 1,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ(GIC_DIST_ACTIVE_CLEAR,
		vgic_mmio_read_active, vgic_mmio_write_cactive,
		NULL, vgic_mmio_uaccess_write_cactive, 1,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ(GIC_DIST_PRI,
		vgic_mmio_read_priority, vgic_mmio_write_priority, NULL, NULL,
		8, VGIC_ACCESS_32bit | VGIC_ACCESS_8bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ(GIC_DIST_TARGET,
		vgic_mmio_read_target, vgic_mmio_write_target, NULL, NULL, 8,
		VGIC_ACCESS_32bit | VGIC_ACCESS_8bit),
	REGISTER_DESC_WITH_BITS_PER_IRQ(GIC_DIST_CONFIG,
		vgic_mmio_read_config, vgic_mmio_write_config, NULL, NULL, 2,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_LENGTH(GIC_DIST_SOFTINT,
		vgic_mmio_read_raz, vgic_mmio_write_sgir, 4,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_LENGTH(GIC_DIST_SGI_PENDING_CLEAR,
		vgic_mmio_read_sgipend, vgic_mmio_write_sgipendc, 16,
		VGIC_ACCESS_32bit | VGIC_ACCESS_8bit),
	REGISTER_DESC_WITH_LENGTH(GIC_DIST_SGI_PENDING_SET,
		vgic_mmio_read_sgipend, vgic_mmio_write_sgipends, 16,
		VGIC_ACCESS_32bit | VGIC_ACCESS_8bit),
};

static const struct vgic_register_region vgic_v2_cpu_registers[] = {
	REGISTER_DESC_WITH_LENGTH(GIC_CPU_CTRL,
		vgic_mmio_read_vcpuif, vgic_mmio_write_vcpuif, 4,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_LENGTH(GIC_CPU_PRIMASK,
		vgic_mmio_read_vcpuif, vgic_mmio_write_vcpuif, 4,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_LENGTH(GIC_CPU_BINPOINT,
		vgic_mmio_read_vcpuif, vgic_mmio_write_vcpuif, 4,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_LENGTH(GIC_CPU_ALIAS_BINPOINT,
		vgic_mmio_read_vcpuif, vgic_mmio_write_vcpuif, 4,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_LENGTH(GIC_CPU_ACTIVEPRIO,
		vgic_mmio_read_apr, vgic_mmio_write_apr, 16,
		VGIC_ACCESS_32bit),
	REGISTER_DESC_WITH_LENGTH(GIC_CPU_IDENT,
		vgic_mmio_read_vcpuif, vgic_mmio_write_vcpuif, 4,
		VGIC_ACCESS_32bit),
};

unsigned int vgic_v2_init_dist_iodev(struct vgic_io_device *dev)
{
	dev->regions = vgic_v2_dist_registers;
	dev->nr_regions = ARRAY_SIZE(vgic_v2_dist_registers);

	kvm_iodevice_init(&dev->dev, &kvm_io_gic_ops);

	return SZ_4K;
}

int vgic_v2_has_attr_regs(struct kvm_device *dev, struct kvm_device_attr *attr)
{
	const struct vgic_register_region *region;
	struct vgic_io_device iodev;
	struct vgic_reg_attr reg_attr;
	struct kvm_vcpu *vcpu;
	gpa_t addr;
	int ret;

	ret = vgic_v2_parse_attr(dev, attr, &reg_attr);
	if (ret)
		return ret;

	vcpu = reg_attr.vcpu;
	addr = reg_attr.addr;

	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_DIST_REGS:
		iodev.regions = vgic_v2_dist_registers;
		iodev.nr_regions = ARRAY_SIZE(vgic_v2_dist_registers);
		iodev.base_addr = 0;
		break;
	case KVM_DEV_ARM_VGIC_GRP_CPU_REGS:
		iodev.regions = vgic_v2_cpu_registers;
		iodev.nr_regions = ARRAY_SIZE(vgic_v2_cpu_registers);
		iodev.base_addr = 0;
		break;
	default:
		return -ENXIO;
	}

	/* We only support aligned 32-bit accesses. */
	if (addr & 3)
		return -ENXIO;

	region = vgic_get_mmio_region(vcpu, &iodev, addr, sizeof(u32));
	if (!region)
		return -ENXIO;

	return 0;
}

int vgic_v2_cpuif_uaccess(struct kvm_vcpu *vcpu, bool is_write,
			  int offset, u32 *val)
{
	struct vgic_io_device dev = {
		.regions = vgic_v2_cpu_registers,
		.nr_regions = ARRAY_SIZE(vgic_v2_cpu_registers),
		.iodev_type = IODEV_CPUIF,
	};

	return vgic_uaccess(vcpu, &dev, is_write, offset, val);
}

int vgic_v2_dist_uaccess(struct kvm_vcpu *vcpu, bool is_write,
			 int offset, u32 *val)
{
	struct vgic_io_device dev = {
		.regions = vgic_v2_dist_registers,
		.nr_regions = ARRAY_SIZE(vgic_v2_dist_registers),
		.iodev_type = IODEV_DIST,
	};

	return vgic_uaccess(vcpu, &dev, is_write, offset, val);
}
