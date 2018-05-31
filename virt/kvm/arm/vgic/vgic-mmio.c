/*
 * VGIC MMIO handling functions
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

#include <linux/bitops.h>
#include <linux/bsearch.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <kvm/iodev.h>
#include <kvm/arm_vgic.h>

#include "vgic.h"
#include "vgic-mmio.h"

unsigned long vgic_mmio_read_raz(struct kvm_vcpu *vcpu,
				 gpa_t addr, unsigned int len)
{
	return 0;
}

unsigned long vgic_mmio_read_rao(struct kvm_vcpu *vcpu,
				 gpa_t addr, unsigned int len)
{
	return -1UL;
}

void vgic_mmio_write_wi(struct kvm_vcpu *vcpu, gpa_t addr,
			unsigned int len, unsigned long val)
{
	/* Ignore */
}

/*
 * Read accesses to both GICD_ICENABLER and GICD_ISENABLER return the value
 * of the enabled bit, so there is only one function for both here.
 */
unsigned long vgic_mmio_read_enable(struct kvm_vcpu *vcpu,
				    gpa_t addr, unsigned int len)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 1);
	u32 value = 0;
	int i;

	/* Loop over all IRQs affected by this read */
	for (i = 0; i < len * 8; i++) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);

		if (irq->enabled)
			value |= (1U << i);

		vgic_put_irq(vcpu->kvm, irq);
	}

	return value;
}

void vgic_mmio_write_senable(struct kvm_vcpu *vcpu,
			     gpa_t addr, unsigned int len,
			     unsigned long val)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 1);
	int i;

	for_each_set_bit(i, &val, len * 8) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);

		spin_lock(&irq->irq_lock);
		irq->enabled = true;
		vgic_queue_irq_unlock(vcpu->kvm, irq);

		vgic_put_irq(vcpu->kvm, irq);
	}
}

void vgic_mmio_write_cenable(struct kvm_vcpu *vcpu,
			     gpa_t addr, unsigned int len,
			     unsigned long val)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 1);
	int i;

	for_each_set_bit(i, &val, len * 8) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);

		spin_lock(&irq->irq_lock);

		irq->enabled = false;

		spin_unlock(&irq->irq_lock);
		vgic_put_irq(vcpu->kvm, irq);
	}
}

unsigned long vgic_mmio_read_pending(struct kvm_vcpu *vcpu,
				     gpa_t addr, unsigned int len)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 1);
	u32 value = 0;
	int i;

	/* Loop over all IRQs affected by this read */
	for (i = 0; i < len * 8; i++) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);
		unsigned long flags;

		spin_lock_irqsave(&irq->irq_lock, flags);
		if (irq_is_pending(irq))
			value |= (1U << i);
		spin_unlock_irqrestore(&irq->irq_lock, flags);

		vgic_put_irq(vcpu->kvm, irq);
	}

	return value;
}

void vgic_mmio_write_spending(struct kvm_vcpu *vcpu,
			      gpa_t addr, unsigned int len,
			      unsigned long val)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 1);
	int i;

	for_each_set_bit(i, &val, len * 8) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);

		spin_lock(&irq->irq_lock);
		irq->pending_latch = true;

		vgic_queue_irq_unlock(vcpu->kvm, irq);
		vgic_put_irq(vcpu->kvm, irq);
	}
}

void vgic_mmio_write_cpending(struct kvm_vcpu *vcpu,
			      gpa_t addr, unsigned int len,
			      unsigned long val)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 1);
	int i;

	for_each_set_bit(i, &val, len * 8) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);

		spin_lock(&irq->irq_lock);

		irq->pending_latch = false;

		spin_unlock(&irq->irq_lock);
		vgic_put_irq(vcpu->kvm, irq);
	}
}

unsigned long vgic_mmio_read_active(struct kvm_vcpu *vcpu,
				    gpa_t addr, unsigned int len)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 1);
	u32 value = 0;
	int i;

	/* Loop over all IRQs affected by this read */
	for (i = 0; i < len * 8; i++) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);

		if (irq->active)
			value |= (1U << i);

		vgic_put_irq(vcpu->kvm, irq);
	}

	return value;
}

static void vgic_mmio_change_active(struct kvm_vcpu *vcpu, struct vgic_irq *irq,
				    bool new_active_state)
{
	struct kvm_vcpu *requester_vcpu;
	spin_lock(&irq->irq_lock);

	/*
	 * The vcpu parameter here can mean multiple things depending on how
	 * this function is called; when handling a trap from the kernel it
	 * depends on the GIC version, and these functions are also called as
	 * part of save/restore from userspace.
	 *
	 * Therefore, we have to figure out the requester in a reliable way.
	 *
	 * When accessing VGIC state from user space, the requester_vcpu is
	 * NULL, which is fine, because we guarantee that no VCPUs are running
	 * when accessing VGIC state from user space so irq->vcpu->cpu is
	 * always -1.
	 */
	requester_vcpu = kvm_arm_get_running_vcpu();

	/*
	 * If this virtual IRQ was written into a list register, we
	 * have to make sure the CPU that runs the VCPU thread has
	 * synced back the LR state to the struct vgic_irq.
	 *
	 * As long as the conditions below are true, we know the VCPU thread
	 * may be on its way back from the guest (we kicked the VCPU thread in
	 * vgic_change_active_prepare)  and still has to sync back this IRQ,
	 * so we release and re-acquire the spin_lock to let the other thread
	 * sync back the IRQ.
	 */
	while (irq->vcpu && /* IRQ may have state in an LR somewhere */
	       irq->vcpu != requester_vcpu && /* Current thread is not the VCPU thread */
	       irq->vcpu->cpu != -1) /* VCPU thread is running */
		cond_resched_lock(&irq->irq_lock);

	irq->active = new_active_state;
	if (new_active_state)
		vgic_queue_irq_unlock(vcpu->kvm, irq);
	else
		spin_unlock(&irq->irq_lock);
}

/*
 * If we are fiddling with an IRQ's active state, we have to make sure the IRQ
 * is not queued on some running VCPU's LRs, because then the change to the
 * active state can be overwritten when the VCPU's state is synced coming back
 * from the guest.
 *
 * For shared interrupts, we have to stop all the VCPUs because interrupts can
 * be migrated while we don't hold the IRQ locks and we don't want to be
 * chasing moving targets.
 *
 * For private interrupts we don't have to do anything because userspace
 * accesses to the VGIC state already require all VCPUs to be stopped, and
 * only the VCPU itself can modify its private interrupts active state, which
 * guarantees that the VCPU is not running.
 */
static void vgic_change_active_prepare(struct kvm_vcpu *vcpu, u32 intid)
{
	if (intid > VGIC_NR_PRIVATE_IRQS)
		kvm_arm_halt_guest(vcpu->kvm);
}

/* See vgic_change_active_prepare */
static void vgic_change_active_finish(struct kvm_vcpu *vcpu, u32 intid)
{
	if (intid > VGIC_NR_PRIVATE_IRQS)
		kvm_arm_resume_guest(vcpu->kvm);
}

static void __vgic_mmio_write_cactive(struct kvm_vcpu *vcpu,
				      gpa_t addr, unsigned int len,
				      unsigned long val)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 1);
	int i;

	for_each_set_bit(i, &val, len * 8) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);
		vgic_mmio_change_active(vcpu, irq, false);
		vgic_put_irq(vcpu->kvm, irq);
	}
}

void vgic_mmio_write_cactive(struct kvm_vcpu *vcpu,
			     gpa_t addr, unsigned int len,
			     unsigned long val)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 1);

	mutex_lock(&vcpu->kvm->lock);
	vgic_change_active_prepare(vcpu, intid);

	__vgic_mmio_write_cactive(vcpu, addr, len, val);

	vgic_change_active_finish(vcpu, intid);
	mutex_unlock(&vcpu->kvm->lock);
}

void vgic_mmio_uaccess_write_cactive(struct kvm_vcpu *vcpu,
				     gpa_t addr, unsigned int len,
				     unsigned long val)
{
	__vgic_mmio_write_cactive(vcpu, addr, len, val);
}

static void __vgic_mmio_write_sactive(struct kvm_vcpu *vcpu,
				      gpa_t addr, unsigned int len,
				      unsigned long val)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 1);
	int i;

	for_each_set_bit(i, &val, len * 8) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);
		vgic_mmio_change_active(vcpu, irq, true);
		vgic_put_irq(vcpu->kvm, irq);
	}
}

void vgic_mmio_write_sactive(struct kvm_vcpu *vcpu,
			     gpa_t addr, unsigned int len,
			     unsigned long val)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 1);

	mutex_lock(&vcpu->kvm->lock);
	vgic_change_active_prepare(vcpu, intid);

	__vgic_mmio_write_sactive(vcpu, addr, len, val);

	vgic_change_active_finish(vcpu, intid);
	mutex_unlock(&vcpu->kvm->lock);
}

void vgic_mmio_uaccess_write_sactive(struct kvm_vcpu *vcpu,
				     gpa_t addr, unsigned int len,
				     unsigned long val)
{
	__vgic_mmio_write_sactive(vcpu, addr, len, val);
}

unsigned long vgic_mmio_read_priority(struct kvm_vcpu *vcpu,
				      gpa_t addr, unsigned int len)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 8);
	int i;
	u64 val = 0;

	for (i = 0; i < len; i++) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);

		val |= (u64)irq->priority << (i * 8);

		vgic_put_irq(vcpu->kvm, irq);
	}

	return val;
}

/*
 * We currently don't handle changing the priority of an interrupt that
 * is already pending on a VCPU. If there is a need for this, we would
 * need to make this VCPU exit and re-evaluate the priorities, potentially
 * leading to this interrupt getting presented now to the guest (if it has
 * been masked by the priority mask before).
 */
void vgic_mmio_write_priority(struct kvm_vcpu *vcpu,
			      gpa_t addr, unsigned int len,
			      unsigned long val)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 8);
	int i;

	for (i = 0; i < len; i++) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);

		spin_lock(&irq->irq_lock);
		/* Narrow the priority range to what we actually support */
		irq->priority = (val >> (i * 8)) & GENMASK(7, 8 - VGIC_PRI_BITS);
		spin_unlock(&irq->irq_lock);

		vgic_put_irq(vcpu->kvm, irq);
	}
}

unsigned long vgic_mmio_read_config(struct kvm_vcpu *vcpu,
				    gpa_t addr, unsigned int len)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 2);
	u32 value = 0;
	int i;

	for (i = 0; i < len * 4; i++) {
		struct vgic_irq *irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);

		if (irq->config == VGIC_CONFIG_EDGE)
			value |= (2U << (i * 2));

		vgic_put_irq(vcpu->kvm, irq);
	}

	return value;
}

void vgic_mmio_write_config(struct kvm_vcpu *vcpu,
			    gpa_t addr, unsigned int len,
			    unsigned long val)
{
	u32 intid = VGIC_ADDR_TO_INTID(addr, 2);
	int i;

	for (i = 0; i < len * 4; i++) {
		struct vgic_irq *irq;

		/*
		 * The configuration cannot be changed for SGIs in general,
		 * for PPIs this is IMPLEMENTATION DEFINED. The arch timer
		 * code relies on PPIs being level triggered, so we also
		 * make them read-only here.
		 */
		if (intid + i < VGIC_NR_PRIVATE_IRQS)
			continue;

		irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);
		spin_lock(&irq->irq_lock);

		if (test_bit(i * 2 + 1, &val))
			irq->config = VGIC_CONFIG_EDGE;
		else
			irq->config = VGIC_CONFIG_LEVEL;

		spin_unlock(&irq->irq_lock);
		vgic_put_irq(vcpu->kvm, irq);
	}
}

u64 vgic_read_irq_line_level_info(struct kvm_vcpu *vcpu, u32 intid)
{
	int i;
	u64 val = 0;
	int nr_irqs = vcpu->kvm->arch.vgic.nr_spis + VGIC_NR_PRIVATE_IRQS;

	for (i = 0; i < 32; i++) {
		struct vgic_irq *irq;

		if ((intid + i) < VGIC_NR_SGIS || (intid + i) >= nr_irqs)
			continue;

		irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);
		if (irq->config == VGIC_CONFIG_LEVEL && irq->line_level)
			val |= (1U << i);

		vgic_put_irq(vcpu->kvm, irq);
	}

	return val;
}

void vgic_write_irq_line_level_info(struct kvm_vcpu *vcpu, u32 intid,
				    const u64 val)
{
	int i;
	int nr_irqs = vcpu->kvm->arch.vgic.nr_spis + VGIC_NR_PRIVATE_IRQS;

	for (i = 0; i < 32; i++) {
		struct vgic_irq *irq;
		bool new_level;

		if ((intid + i) < VGIC_NR_SGIS || (intid + i) >= nr_irqs)
			continue;

		irq = vgic_get_irq(vcpu->kvm, vcpu, intid + i);

		/*
		 * Line level is set irrespective of irq type
		 * (level or edge) to avoid dependency that VM should
		 * restore irq config before line level.
		 */
		new_level = !!(val & (1U << i));
		spin_lock(&irq->irq_lock);
		irq->line_level = new_level;
		if (new_level)
			vgic_queue_irq_unlock(vcpu->kvm, irq);
		else
			spin_unlock(&irq->irq_lock);

		vgic_put_irq(vcpu->kvm, irq);
	}
}

static int match_region(const void *key, const void *elt)
{
	const unsigned int offset = (unsigned long)key;
	const struct vgic_register_region *region = elt;

	if (offset < region->reg_offset)
		return -1;

	if (offset >= region->reg_offset + region->len)
		return 1;

	return 0;
}

const struct vgic_register_region *
vgic_find_mmio_region(const struct vgic_register_region *regions,
		      int nr_regions, unsigned int offset)
{
	return bsearch((void *)(uintptr_t)offset, regions, nr_regions,
		       sizeof(regions[0]), match_region);
}

void vgic_set_vmcr(struct kvm_vcpu *vcpu, struct vgic_vmcr *vmcr)
{
	if (kvm_vgic_global_state.type == VGIC_V2)
		vgic_v2_set_vmcr(vcpu, vmcr);
	else
		vgic_v3_set_vmcr(vcpu, vmcr);
}

void vgic_get_vmcr(struct kvm_vcpu *vcpu, struct vgic_vmcr *vmcr)
{
	if (kvm_vgic_global_state.type == VGIC_V2)
		vgic_v2_get_vmcr(vcpu, vmcr);
	else
		vgic_v3_get_vmcr(vcpu, vmcr);
}

/*
 * kvm_mmio_read_buf() returns a value in a format where it can be converted
 * to a byte array and be directly observed as the guest wanted it to appear
 * in memory if it had done the store itself, which is LE for the GIC, as the
 * guest knows the GIC is always LE.
 *
 * We convert this value to the CPUs native format to deal with it as a data
 * value.
 */
unsigned long vgic_data_mmio_bus_to_host(const void *val, unsigned int len)
{
	unsigned long data = kvm_mmio_read_buf(val, len);

	switch (len) {
	case 1:
		return data;
	case 2:
		return le16_to_cpu(data);
	case 4:
		return le32_to_cpu(data);
	default:
		return le64_to_cpu(data);
	}
}

/*
 * kvm_mmio_write_buf() expects a value in a format such that if converted to
 * a byte array it is observed as the guest would see it if it could perform
 * the load directly.  Since the GIC is LE, and the guest knows this, the
 * guest expects a value in little endian format.
 *
 * We convert the data value from the CPUs native format to LE so that the
 * value is returned in the proper format.
 */
void vgic_data_host_to_mmio_bus(void *buf, unsigned int len,
				unsigned long data)
{
	switch (len) {
	case 1:
		break;
	case 2:
		data = cpu_to_le16(data);
		break;
	case 4:
		data = cpu_to_le32(data);
		break;
	default:
		data = cpu_to_le64(data);
	}

	kvm_mmio_write_buf(buf, len, data);
}

static
struct vgic_io_device *kvm_to_vgic_iodev(const struct kvm_io_device *dev)
{
	return container_of(dev, struct vgic_io_device, dev);
}

static bool check_region(const struct kvm *kvm,
			 const struct vgic_register_region *region,
			 gpa_t addr, int len)
{
	int flags, nr_irqs = kvm->arch.vgic.nr_spis + VGIC_NR_PRIVATE_IRQS;

	switch (len) {
	case sizeof(u8):
		flags = VGIC_ACCESS_8bit;
		break;
	case sizeof(u32):
		flags = VGIC_ACCESS_32bit;
		break;
	case sizeof(u64):
		flags = VGIC_ACCESS_64bit;
		break;
	default:
		return false;
	}

	if ((region->access_flags & flags) && IS_ALIGNED(addr, len)) {
		if (!region->bits_per_irq)
			return true;

		/* Do we access a non-allocated IRQ? */
		return VGIC_ADDR_TO_INTID(addr, region->bits_per_irq) < nr_irqs;
	}

	return false;
}

const struct vgic_register_region *
vgic_get_mmio_region(struct kvm_vcpu *vcpu, struct vgic_io_device *iodev,
		     gpa_t addr, int len)
{
	const struct vgic_register_region *region;

	region = vgic_find_mmio_region(iodev->regions, iodev->nr_regions,
				       addr - iodev->base_addr);
	if (!region || !check_region(vcpu->kvm, region, addr, len))
		return NULL;

	return region;
}

static int vgic_uaccess_read(struct kvm_vcpu *vcpu, struct kvm_io_device *dev,
			     gpa_t addr, u32 *val)
{
	struct vgic_io_device *iodev = kvm_to_vgic_iodev(dev);
	const struct vgic_register_region *region;
	struct kvm_vcpu *r_vcpu;

	region = vgic_get_mmio_region(vcpu, iodev, addr, sizeof(u32));
	if (!region) {
		*val = 0;
		return 0;
	}

	r_vcpu = iodev->redist_vcpu ? iodev->redist_vcpu : vcpu;
	if (region->uaccess_read)
		*val = region->uaccess_read(r_vcpu, addr, sizeof(u32));
	else
		*val = region->read(r_vcpu, addr, sizeof(u32));

	return 0;
}

static int vgic_uaccess_write(struct kvm_vcpu *vcpu, struct kvm_io_device *dev,
			      gpa_t addr, const u32 *val)
{
	struct vgic_io_device *iodev = kvm_to_vgic_iodev(dev);
	const struct vgic_register_region *region;
	struct kvm_vcpu *r_vcpu;

	region = vgic_get_mmio_region(vcpu, iodev, addr, sizeof(u32));
	if (!region)
		return 0;

	r_vcpu = iodev->redist_vcpu ? iodev->redist_vcpu : vcpu;
	if (region->uaccess_write)
		region->uaccess_write(r_vcpu, addr, sizeof(u32), *val);
	else
		region->write(r_vcpu, addr, sizeof(u32), *val);

	return 0;
}

/*
 * Userland access to VGIC registers.
 */
int vgic_uaccess(struct kvm_vcpu *vcpu, struct vgic_io_device *dev,
		 bool is_write, int offset, u32 *val)
{
	if (is_write)
		return vgic_uaccess_write(vcpu, &dev->dev, offset, val);
	else
		return vgic_uaccess_read(vcpu, &dev->dev, offset, val);
}

static int dispatch_mmio_read(struct kvm_vcpu *vcpu, struct kvm_io_device *dev,
			      gpa_t addr, int len, void *val)
{
	struct vgic_io_device *iodev = kvm_to_vgic_iodev(dev);
	const struct vgic_register_region *region;
	unsigned long data = 0;

	region = vgic_get_mmio_region(vcpu, iodev, addr, len);
	if (!region) {
		memset(val, 0, len);
		return 0;
	}

	switch (iodev->iodev_type) {
	case IODEV_CPUIF:
		data = region->read(vcpu, addr, len);
		break;
	case IODEV_DIST:
		data = region->read(vcpu, addr, len);
		break;
	case IODEV_REDIST:
		data = region->read(iodev->redist_vcpu, addr, len);
		break;
	case IODEV_ITS:
		data = region->its_read(vcpu->kvm, iodev->its, addr, len);
		break;
	}

	vgic_data_host_to_mmio_bus(val, len, data);
	return 0;
}

static int dispatch_mmio_write(struct kvm_vcpu *vcpu, struct kvm_io_device *dev,
			       gpa_t addr, int len, const void *val)
{
	struct vgic_io_device *iodev = kvm_to_vgic_iodev(dev);
	const struct vgic_register_region *region;
	unsigned long data = vgic_data_mmio_bus_to_host(val, len);

	region = vgic_get_mmio_region(vcpu, iodev, addr, len);
	if (!region)
		return 0;

	switch (iodev->iodev_type) {
	case IODEV_CPUIF:
		region->write(vcpu, addr, len, data);
		break;
	case IODEV_DIST:
		region->write(vcpu, addr, len, data);
		break;
	case IODEV_REDIST:
		region->write(iodev->redist_vcpu, addr, len, data);
		break;
	case IODEV_ITS:
		region->its_write(vcpu->kvm, iodev->its, addr, len, data);
		break;
	}

	return 0;
}

struct kvm_io_device_ops kvm_io_gic_ops = {
	.read = dispatch_mmio_read,
	.write = dispatch_mmio_write,
};

int vgic_register_dist_iodev(struct kvm *kvm, gpa_t dist_base_address,
			     enum vgic_type type)
{
	struct vgic_io_device *io_device = &kvm->arch.vgic.dist_iodev;
	int ret = 0;
	unsigned int len;

	switch (type) {
	case VGIC_V2:
		len = vgic_v2_init_dist_iodev(io_device);
		break;
	case VGIC_V3:
		len = vgic_v3_init_dist_iodev(io_device);
		break;
	default:
		BUG_ON(1);
	}

	io_device->base_addr = dist_base_address;
	io_device->iodev_type = IODEV_DIST;
	io_device->redist_vcpu = NULL;

	mutex_lock(&kvm->slots_lock);
	ret = kvm_io_bus_register_dev(kvm, KVM_MMIO_BUS, dist_base_address,
				      len, &io_device->dev);
	mutex_unlock(&kvm->slots_lock);

	return ret;
}
