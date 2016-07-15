/*
 * GICv3 ITS emulation
 *
 * Copyright (C) 2015,2016 ARM Ltd.
 * Author: Andre Przywara <andre.przywara@arm.com>
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

#include <linux/cpu.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/uaccess.h>

#include <linux/irqchip/arm-gic-v3.h>

#include <asm/kvm_emulate.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_mmu.h>

#include "vgic.h"
#include "vgic-mmio.h"

struct its_device {
	struct list_head dev_list;

	/* the head for the list of ITTEs */
	struct list_head itt_head;
	u32 device_id;
};

#define COLLECTION_NOT_MAPPED ((u32)~0)

struct its_collection {
	struct list_head coll_list;

	u32 collection_id;
	u32 target_addr;
};

#define its_is_collection_mapped(coll) ((coll) && \
				((coll)->target_addr != COLLECTION_NOT_MAPPED))

struct its_itte {
	struct list_head itte_list;

	struct vgic_irq *irq;
	struct its_collection *collection;
	u32 lpi;
	u32 event_id;
};

/*
 * We only implement 48 bits of PA at the moment, although the ITS
 * supports more. Let's be restrictive here.
 */
#define CBASER_ADDRESS(x)	((x) & GENMASK_ULL(47, 12))
#define PENDBASER_ADDRESS(x)	((x) & GENMASK_ULL(47, 16))
#define PROPBASER_ADDRESS(x)	((x) & GENMASK_ULL(47, 12))

#define GIC_LPI_OFFSET 8192

#define LPI_PROP_ENABLE_BIT(p)	((p) & LPI_PROP_ENABLED)
#define LPI_PROP_PRIORITY(p)	((p) & 0xfc)

/*
 * Reads the configuration data for a given LPI from guest memory and
 * updates the fields in struct vgic_irq.
 * If filter_vcpu is not NULL, applies only if the IRQ is targeting this
 * VCPU. Unconditionally applies if filter_vcpu is NULL.
 */
static int update_lpi_config(struct kvm *kvm, struct vgic_irq *irq,
			     struct kvm_vcpu *filter_vcpu)
{
	u64 propbase = PROPBASER_ADDRESS(kvm->arch.vgic.propbaser);
	u8 prop;
	int ret;

	ret = kvm_read_guest(kvm, propbase + irq->intid - GIC_LPI_OFFSET,
			     &prop, 1);

	if (ret)
		return ret;

	spin_lock(&irq->irq_lock);

	if (!filter_vcpu || filter_vcpu == irq->target_vcpu) {
		irq->priority = LPI_PROP_PRIORITY(prop);
		irq->enabled = LPI_PROP_ENABLE_BIT(prop);

		vgic_queue_irq_unlock(kvm, irq);
	} else {
		spin_unlock(&irq->irq_lock);
	}

	return 0;
}

/*
 * Create a snapshot of the current LPI list, so that we can enumerate all
 * LPIs without holding any lock.
 * Returns the array length and puts the kmalloc'ed array into intid_ptr.
 */
static int vgic_copy_lpi_list(struct kvm *kvm, u32 **intid_ptr)
{
	struct vgic_dist *dist = &kvm->arch.vgic;
	struct vgic_irq *irq;
	u32 *intids;
	int irq_count = dist->lpi_list_count, i = 0;

	/*
	 * We use the current value of the list length, which may change
	 * after the kmalloc. We don't care, because the guest shouldn't
	 * change anything while the command handling is still running,
	 * and in the worst case we would miss a new IRQ, which one wouldn't
	 * expect to be covered by this command anyway.
	 */
	intids = kmalloc_array(irq_count, sizeof(intids[0]), GFP_KERNEL);
	if (!intids)
		return -ENOMEM;

	spin_lock(&dist->lpi_list_lock);
	list_for_each_entry(irq, &dist->lpi_list_head, lpi_list) {
		/* We don't need to "get" the IRQ, as we hold the list lock. */
		intids[i] = irq->intid;
		if (++i == irq_count)
			break;
	}
	spin_unlock(&dist->lpi_list_lock);

	*intid_ptr = intids;
	return irq_count;
}

/*
 * Scan the whole LPI pending table and sync the pending bit in there
 * with our own data structures. This relies on the LPI being
 * mapped before.
 */
static int its_sync_lpi_pending_table(struct kvm_vcpu *vcpu)
{
	gpa_t pendbase = PENDBASER_ADDRESS(vcpu->arch.vgic_cpu.pendbaser);
	struct vgic_irq *irq;
	int last_byte_offset = -1;
	int ret = 0;
	u32 *intids;
	int nr_irqs, i;

	nr_irqs = vgic_copy_lpi_list(vcpu->kvm, &intids);
	if (nr_irqs < 0)
		return nr_irqs;

	for (i = 0; i < nr_irqs; i++) {
		int byte_offset, bit_nr;
		u8 pendmask;

		byte_offset = intids[i] / BITS_PER_BYTE;
		bit_nr = intids[i] % BITS_PER_BYTE;

		/*
		 * For contiguously allocated LPIs chances are we just read
		 * this very same byte in the last iteration. Reuse that.
		 */
		if (byte_offset != last_byte_offset) {
			ret = kvm_read_guest(vcpu->kvm, pendbase + byte_offset,
					     &pendmask, 1);
			if (ret) {
				kfree(intids);
				return ret;
			}
			last_byte_offset = byte_offset;
		}

		irq = vgic_get_irq(vcpu->kvm, NULL, intids[i]);
		spin_lock(&irq->irq_lock);
		irq->pending = pendmask & (1U << bit_nr);
		vgic_queue_irq_unlock(vcpu->kvm, irq);
		vgic_put_irq(vcpu->kvm, irq);
	}

	kfree(intids);

	return ret;
}

static unsigned long vgic_mmio_read_its_ctlr(struct kvm *vcpu,
					     struct vgic_its *its,
					     gpa_t addr, unsigned int len)
{
	u32 reg = 0;

	mutex_lock(&its->cmd_lock);
	if (its->creadr == its->cwriter)
		reg |= GITS_CTLR_QUIESCENT;
	if (its->enabled)
		reg |= GITS_CTLR_ENABLE;
	mutex_unlock(&its->cmd_lock);

	return reg;
}

static void vgic_mmio_write_its_ctlr(struct kvm *kvm, struct vgic_its *its,
				     gpa_t addr, unsigned int len,
				     unsigned long val)
{
	its->enabled = !!(val & GITS_CTLR_ENABLE);
}

static unsigned long vgic_mmio_read_its_typer(struct kvm *kvm,
					      struct vgic_its *its,
					      gpa_t addr, unsigned int len)
{
	u64 reg = GITS_TYPER_PLPIS;

	/*
	 * We use linear CPU numbers for redistributor addressing,
	 * so GITS_TYPER.PTA is 0.
	 * Also we force all PROPBASER registers to be the same, so
	 * CommonLPIAff is 0 as well.
	 * To avoid memory waste in the guest, we keep the number of IDBits and
	 * DevBits low - as least for the time being.
	 */
	reg |= 0x0f << GITS_TYPER_DEVBITS_SHIFT;
	reg |= 0x0f << GITS_TYPER_IDBITS_SHIFT;

	return extract_bytes(reg, addr & 7, len);
}

static unsigned long vgic_mmio_read_its_iidr(struct kvm *kvm,
					     struct vgic_its *its,
					     gpa_t addr, unsigned int len)
{
	return (PRODUCT_ID_KVM << 24) | (IMPLEMENTER_ARM << 0);
}

static unsigned long vgic_mmio_read_its_idregs(struct kvm *kvm,
					       struct vgic_its *its,
					       gpa_t addr, unsigned int len)
{
	switch (addr & 0xffff) {
	case GITS_PIDR0:
		return 0x92;	/* part number, bits[7:0] */
	case GITS_PIDR1:
		return 0xb4;	/* part number, bits[11:8] */
	case GITS_PIDR2:
		return GIC_PIDR2_ARCH_GICv3 | 0x0b;
	case GITS_PIDR4:
		return 0x40;	/* This is a 64K software visible page */
	/* The following are the ID registers for (any) GIC. */
	case GITS_CIDR0:
		return 0x0d;
	case GITS_CIDR1:
		return 0xf0;
	case GITS_CIDR2:
		return 0x05;
	case GITS_CIDR3:
		return 0xb1;
	}

	return 0;
}

/* Requires the its_lock to be held. */
static void its_free_itte(struct kvm *kvm, struct its_itte *itte)
{
	list_del(&itte->itte_list);

	/* This put matches the get in vgic_add_lpi. */
	vgic_put_irq(kvm, itte->irq);

	kfree(itte);
}

static int vgic_its_handle_command(struct kvm *kvm, struct vgic_its *its,
				   u64 *its_cmd)
{
	return -ENODEV;
}

static u64 vgic_sanitise_its_baser(u64 reg)
{
	reg = vgic_sanitise_field(reg, GITS_BASER_SHAREABILITY_MASK,
				  GITS_BASER_SHAREABILITY_SHIFT,
				  vgic_sanitise_shareability);
	reg = vgic_sanitise_field(reg, GITS_BASER_INNER_CACHEABILITY_MASK,
				  GITS_BASER_INNER_CACHEABILITY_SHIFT,
				  vgic_sanitise_inner_cacheability);
	reg = vgic_sanitise_field(reg, GITS_BASER_OUTER_CACHEABILITY_MASK,
				  GITS_BASER_OUTER_CACHEABILITY_SHIFT,
				  vgic_sanitise_outer_cacheability);

	/* Bits 15:12 contain bits 51:48 of the PA, which we don't support. */
	reg &= ~GENMASK_ULL(15, 12);

	/* We support only one (ITS) page size: 64K */
	reg = (reg & ~GITS_BASER_PAGE_SIZE_MASK) | GITS_BASER_PAGE_SIZE_64K;

	return reg;
}

static u64 vgic_sanitise_its_cbaser(u64 reg)
{
	reg = vgic_sanitise_field(reg, GITS_CBASER_SHAREABILITY_MASK,
				  GITS_CBASER_SHAREABILITY_SHIFT,
				  vgic_sanitise_shareability);
	reg = vgic_sanitise_field(reg, GITS_CBASER_INNER_CACHEABILITY_MASK,
				  GITS_CBASER_INNER_CACHEABILITY_SHIFT,
				  vgic_sanitise_inner_cacheability);
	reg = vgic_sanitise_field(reg, GITS_CBASER_OUTER_CACHEABILITY_MASK,
				  GITS_CBASER_OUTER_CACHEABILITY_SHIFT,
				  vgic_sanitise_outer_cacheability);

	/*
	 * Sanitise the physical address to be 64k aligned.
	 * Also limit the physical addresses to 48 bits.
	 */
	reg &= ~(GENMASK_ULL(51, 48) | GENMASK_ULL(15, 12));

	return reg;
}

static unsigned long vgic_mmio_read_its_cbaser(struct kvm *kvm,
					       struct vgic_its *its,
					       gpa_t addr, unsigned int len)
{
	return extract_bytes(its->cbaser, addr & 7, len);
}

static void vgic_mmio_write_its_cbaser(struct kvm *kvm, struct vgic_its *its,
				       gpa_t addr, unsigned int len,
				       unsigned long val)
{
	/* When GITS_CTLR.Enable is 1, this register is RO. */
	if (its->enabled)
		return;

	mutex_lock(&its->cmd_lock);
	its->cbaser = update_64bit_reg(its->cbaser, addr & 7, len, val);
	its->cbaser = vgic_sanitise_its_cbaser(its->cbaser);
	its->creadr = 0;
	/*
	 * CWRITER is architecturally UNKNOWN on reset, but we need to reset
	 * it to CREADR to make sure we start with an empty command buffer.
	 */
	its->cwriter = its->creadr;
	mutex_unlock(&its->cmd_lock);
}

#define ITS_CMD_BUFFER_SIZE(baser)	((((baser) & 0xff) + 1) << 12)
#define ITS_CMD_SIZE			32
#define ITS_CMD_OFFSET(reg)		((reg) & GENMASK(19, 5))

/*
 * By writing to CWRITER the guest announces new commands to be processed.
 * To avoid any races in the first place, we take the its_cmd lock, which
 * protects our ring buffer variables, so that there is only one user
 * per ITS handling commands at a given time.
 */
static void vgic_mmio_write_its_cwriter(struct kvm *kvm, struct vgic_its *its,
					gpa_t addr, unsigned int len,
					unsigned long val)
{
	gpa_t cbaser;
	u64 cmd_buf[4];
	u32 reg;

	if (!its)
		return;

	mutex_lock(&its->cmd_lock);

	reg = update_64bit_reg(its->cwriter, addr & 7, len, val);
	reg = ITS_CMD_OFFSET(reg);
	if (reg >= ITS_CMD_BUFFER_SIZE(its->cbaser)) {
		mutex_unlock(&its->cmd_lock);
		return;
	}

	its->cwriter = reg;
	cbaser = CBASER_ADDRESS(its->cbaser);

	while (its->cwriter != its->creadr) {
		int ret = kvm_read_guest(kvm, cbaser + its->creadr,
					 cmd_buf, ITS_CMD_SIZE);
		/*
		 * If kvm_read_guest() fails, this could be due to the guest
		 * programming a bogus value in CBASER or something else going
		 * wrong from which we cannot easily recover.
		 * According to section 6.3.2 in the GICv3 spec we can just
		 * ignore that command then.
		 */
		if (!ret)
			vgic_its_handle_command(kvm, its, cmd_buf);

		its->creadr += ITS_CMD_SIZE;
		if (its->creadr == ITS_CMD_BUFFER_SIZE(its->cbaser))
			its->creadr = 0;
	}

	mutex_unlock(&its->cmd_lock);
}

static unsigned long vgic_mmio_read_its_cwriter(struct kvm *kvm,
						struct vgic_its *its,
						gpa_t addr, unsigned int len)
{
	return extract_bytes(its->cwriter, addr & 0x7, len);
}

static unsigned long vgic_mmio_read_its_creadr(struct kvm *kvm,
					       struct vgic_its *its,
					       gpa_t addr, unsigned int len)
{
	return extract_bytes(its->creadr, addr & 0x7, len);
}

#define BASER_INDEX(addr) (((addr) / sizeof(u64)) & 0x7)
static unsigned long vgic_mmio_read_its_baser(struct kvm *kvm,
					      struct vgic_its *its,
					      gpa_t addr, unsigned int len)
{
	u64 reg;

	switch (BASER_INDEX(addr)) {
	case 0:
		reg = its->baser_device_table;
		break;
	case 1:
		reg = its->baser_coll_table;
		break;
	default:
		reg = 0;
		break;
	}

	return extract_bytes(reg, addr & 7, len);
}

#define GITS_BASER_RO_MASK	(GENMASK_ULL(52, 48) | GENMASK_ULL(58, 56))
static void vgic_mmio_write_its_baser(struct kvm *kvm,
				      struct vgic_its *its,
				      gpa_t addr, unsigned int len,
				      unsigned long val)
{
	u64 entry_size, device_type;
	u64 reg, *regptr, clearbits = 0;

	/* When GITS_CTLR.Enable is 1, we ignore write accesses. */
	if (its->enabled)
		return;

	switch (BASER_INDEX(addr)) {
	case 0:
		regptr = &its->baser_device_table;
		entry_size = 8;
		device_type = GITS_BASER_TYPE_DEVICE;
		break;
	case 1:
		regptr = &its->baser_coll_table;
		entry_size = 8;
		device_type = GITS_BASER_TYPE_COLLECTION;
		clearbits = GITS_BASER_INDIRECT;
		break;
	default:
		return;
	}

	reg = update_64bit_reg(*regptr, addr & 7, len, val);
	reg &= ~GITS_BASER_RO_MASK;
	reg &= ~clearbits;

	reg |= (entry_size - 1) << GITS_BASER_ENTRY_SIZE_SHIFT;
	reg |= device_type << GITS_BASER_TYPE_SHIFT;
	reg = vgic_sanitise_its_baser(reg);

	*regptr = reg;
}

#define REGISTER_ITS_DESC(off, rd, wr, length, acc)		\
{								\
	.reg_offset = off,					\
	.len = length,						\
	.access_flags = acc,					\
	.its_read = rd,						\
	.its_write = wr,					\
}

static void its_mmio_write_wi(struct kvm *kvm, struct vgic_its *its,
			      gpa_t addr, unsigned int len, unsigned long val)
{
	/* Ignore */
}

static struct vgic_register_region its_registers[] = {
	REGISTER_ITS_DESC(GITS_CTLR,
		vgic_mmio_read_its_ctlr, vgic_mmio_write_its_ctlr, 4,
		VGIC_ACCESS_32bit),
	REGISTER_ITS_DESC(GITS_IIDR,
		vgic_mmio_read_its_iidr, its_mmio_write_wi, 4,
		VGIC_ACCESS_32bit),
	REGISTER_ITS_DESC(GITS_TYPER,
		vgic_mmio_read_its_typer, its_mmio_write_wi, 8,
		VGIC_ACCESS_64bit | VGIC_ACCESS_32bit),
	REGISTER_ITS_DESC(GITS_CBASER,
		vgic_mmio_read_its_cbaser, vgic_mmio_write_its_cbaser, 8,
		VGIC_ACCESS_64bit | VGIC_ACCESS_32bit),
	REGISTER_ITS_DESC(GITS_CWRITER,
		vgic_mmio_read_its_cwriter, vgic_mmio_write_its_cwriter, 8,
		VGIC_ACCESS_64bit | VGIC_ACCESS_32bit),
	REGISTER_ITS_DESC(GITS_CREADR,
		vgic_mmio_read_its_creadr, its_mmio_write_wi, 8,
		VGIC_ACCESS_64bit | VGIC_ACCESS_32bit),
	REGISTER_ITS_DESC(GITS_BASER,
		vgic_mmio_read_its_baser, vgic_mmio_write_its_baser, 0x40,
		VGIC_ACCESS_64bit | VGIC_ACCESS_32bit),
	REGISTER_ITS_DESC(GITS_IDREGS_BASE,
		vgic_mmio_read_its_idregs, its_mmio_write_wi, 0x30,
		VGIC_ACCESS_32bit),
};

/* This is called on setting the LPI enable bit in the redistributor. */
void vgic_enable_lpis(struct kvm_vcpu *vcpu)
{
	if (!(vcpu->arch.vgic_cpu.pendbaser & GICR_PENDBASER_PTZ))
		its_sync_lpi_pending_table(vcpu);
}

static int vgic_its_init_its(struct kvm *kvm, struct vgic_its *its)
{
	struct vgic_io_device *iodev = &its->iodev;
	int ret;

	if (its->initialized)
		return 0;

	if (IS_VGIC_ADDR_UNDEF(its->vgic_its_base))
		return -ENXIO;

	iodev->regions = its_registers;
	iodev->nr_regions = ARRAY_SIZE(its_registers);
	kvm_iodevice_init(&iodev->dev, &kvm_io_gic_ops);

	iodev->base_addr = its->vgic_its_base;
	iodev->iodev_type = IODEV_ITS;
	iodev->its = its;
	mutex_lock(&kvm->slots_lock);
	ret = kvm_io_bus_register_dev(kvm, KVM_MMIO_BUS, iodev->base_addr,
				      KVM_VGIC_V3_ITS_SIZE, &iodev->dev);
	mutex_unlock(&kvm->slots_lock);

	if (!ret)
		its->initialized = true;

	return ret;
}

#define INITIAL_BASER_VALUE						  \
	(GIC_BASER_CACHEABILITY(GITS_BASER, INNER, RaWb)		| \
	 GIC_BASER_CACHEABILITY(GITS_BASER, OUTER, SameAsInner)		| \
	 GIC_BASER_SHAREABILITY(GITS_BASER, InnerShareable)		| \
	 ((8ULL - 1) << GITS_BASER_ENTRY_SIZE_SHIFT)			| \
	 GITS_BASER_PAGE_SIZE_64K)

#define INITIAL_PROPBASER_VALUE						  \
	(GIC_BASER_CACHEABILITY(GICR_PROPBASER, INNER, RaWb)		| \
	 GIC_BASER_CACHEABILITY(GICR_PROPBASER, OUTER, SameAsInner)	| \
	 GIC_BASER_SHAREABILITY(GICR_PROPBASER, InnerShareable))

static int vgic_its_create(struct kvm_device *dev, u32 type)
{
	struct vgic_its *its;

	if (type != KVM_DEV_TYPE_ARM_VGIC_ITS)
		return -ENODEV;

	its = kzalloc(sizeof(struct vgic_its), GFP_KERNEL);
	if (!its)
		return -ENOMEM;

	mutex_init(&its->its_lock);
	mutex_init(&its->cmd_lock);

	its->vgic_its_base = VGIC_ADDR_UNDEF;

	INIT_LIST_HEAD(&its->device_list);
	INIT_LIST_HEAD(&its->collection_list);

	dev->kvm->arch.vgic.has_its = true;
	its->initialized = false;
	its->enabled = false;

	its->baser_device_table = INITIAL_BASER_VALUE			|
		((u64)GITS_BASER_TYPE_DEVICE << GITS_BASER_TYPE_SHIFT);
	its->baser_coll_table = INITIAL_BASER_VALUE |
		((u64)GITS_BASER_TYPE_COLLECTION << GITS_BASER_TYPE_SHIFT);
	dev->kvm->arch.vgic.propbaser = INITIAL_PROPBASER_VALUE;

	dev->private = its;

	return 0;
}

static void vgic_its_destroy(struct kvm_device *kvm_dev)
{
	struct kvm *kvm = kvm_dev->kvm;
	struct vgic_its *its = kvm_dev->private;
	struct its_device *dev;
	struct its_itte *itte;
	struct list_head *dev_cur, *dev_temp;
	struct list_head *cur, *temp;

	/*
	 * We may end up here without the lists ever having been initialized.
	 * Check this and bail out early to avoid dereferencing a NULL pointer.
	 */
	if (!its->device_list.next)
		return;

	mutex_lock(&its->its_lock);
	list_for_each_safe(dev_cur, dev_temp, &its->device_list) {
		dev = container_of(dev_cur, struct its_device, dev_list);
		list_for_each_safe(cur, temp, &dev->itt_head) {
			itte = (container_of(cur, struct its_itte, itte_list));
			its_free_itte(kvm, itte);
		}
		list_del(dev_cur);
		kfree(dev);
	}

	list_for_each_safe(cur, temp, &its->collection_list) {
		list_del(cur);
		kfree(container_of(cur, struct its_collection, coll_list));
	}
	mutex_unlock(&its->its_lock);

	kfree(its);
}

static int vgic_its_has_attr(struct kvm_device *dev,
			     struct kvm_device_attr *attr)
{
	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_ADDR:
		switch (attr->attr) {
		case KVM_VGIC_ITS_ADDR_TYPE:
			return 0;
		}
		break;
	case KVM_DEV_ARM_VGIC_GRP_CTRL:
		switch (attr->attr) {
		case KVM_DEV_ARM_VGIC_CTRL_INIT:
			return 0;
		}
		break;
	}
	return -ENXIO;
}

static int vgic_its_set_attr(struct kvm_device *dev,
			     struct kvm_device_attr *attr)
{
	struct vgic_its *its = dev->private;
	int ret;

	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_ADDR: {
		u64 __user *uaddr = (u64 __user *)(long)attr->addr;
		unsigned long type = (unsigned long)attr->attr;
		u64 addr;

		if (type != KVM_VGIC_ITS_ADDR_TYPE)
			return -ENODEV;

		if (its->initialized)
			return -EBUSY;

		if (copy_from_user(&addr, uaddr, sizeof(addr)))
			return -EFAULT;

		ret = vgic_check_ioaddr(dev->kvm, &its->vgic_its_base,
					addr, SZ_64K);
		if (ret)
			return ret;

		its->vgic_its_base = addr;

		return 0;
	}
	case KVM_DEV_ARM_VGIC_GRP_CTRL:
		switch (attr->attr) {
		case KVM_DEV_ARM_VGIC_CTRL_INIT:
			return vgic_its_init_its(dev->kvm, its);
		}
		break;
	}
	return -ENXIO;
}

static int vgic_its_get_attr(struct kvm_device *dev,
			     struct kvm_device_attr *attr)
{
	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_ADDR: {
		struct vgic_its *its = dev->private;
		u64 addr = its->vgic_its_base;
		u64 __user *uaddr = (u64 __user *)(long)attr->addr;
		unsigned long type = (unsigned long)attr->attr;

		if (type != KVM_VGIC_ITS_ADDR_TYPE)
			return -ENODEV;

		if (copy_to_user(uaddr, &addr, sizeof(addr)))
			return -EFAULT;
		break;
	default:
		return -ENXIO;
	}
	}

	return 0;
}

static struct kvm_device_ops kvm_arm_vgic_its_ops = {
	.name = "kvm-arm-vgic-its",
	.create = vgic_its_create,
	.destroy = vgic_its_destroy,
	.set_attr = vgic_its_set_attr,
	.get_attr = vgic_its_get_attr,
	.has_attr = vgic_its_has_attr,
};

int kvm_vgic_register_its_device(void)
{
	return kvm_register_device_ops(&kvm_arm_vgic_its_ops,
				       KVM_DEV_TYPE_ARM_VGIC_ITS);
}
