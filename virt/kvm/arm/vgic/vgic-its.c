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

/*
 * Creates a new (reference to a) struct vgic_irq for a given LPI.
 * If this LPI is already mapped on another ITS, we increase its refcount
 * and return a pointer to the existing structure.
 * If this is a "new" LPI, we allocate and initialize a new struct vgic_irq.
 * This function returns a pointer to the _unlocked_ structure.
 */
static struct vgic_irq *vgic_add_lpi(struct kvm *kvm, u32 intid)
{
	struct vgic_dist *dist = &kvm->arch.vgic;
	struct vgic_irq *irq = vgic_get_irq(kvm, NULL, intid), *oldirq;

	/* In this case there is no put, since we keep the reference. */
	if (irq)
		return irq;

	irq = kzalloc(sizeof(struct vgic_irq), GFP_KERNEL);
	if (!irq)
		return NULL;

	INIT_LIST_HEAD(&irq->lpi_list);
	INIT_LIST_HEAD(&irq->ap_list);
	spin_lock_init(&irq->irq_lock);

	irq->config = VGIC_CONFIG_EDGE;
	kref_init(&irq->refcount);
	irq->intid = intid;

	spin_lock(&dist->lpi_list_lock);

	/*
	 * There could be a race with another vgic_add_lpi(), so we need to
	 * check that we don't add a second list entry with the same LPI.
	 */
	list_for_each_entry(oldirq, &dist->lpi_list_head, lpi_list) {
		if (oldirq->intid != intid)
			continue;

		/* Someone was faster with adding this LPI, lets use that. */
		kfree(irq);
		irq = oldirq;

		/*
		 * This increases the refcount, the caller is expected to
		 * call vgic_put_irq() on the returned pointer once it's
		 * finished with the IRQ.
		 */
		vgic_get_irq_kref(irq);

		goto out_unlock;
	}

	list_add_tail(&irq->lpi_list, &dist->lpi_list_head);
	dist->lpi_list_count++;

out_unlock:
	spin_unlock(&dist->lpi_list_lock);

	return irq;
}

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
 * Find and returns a device in the device table for an ITS.
 * Must be called with the its_lock mutex held.
 */
static struct its_device *find_its_device(struct vgic_its *its, u32 device_id)
{
	struct its_device *device;

	list_for_each_entry(device, &its->device_list, dev_list)
		if (device_id == device->device_id)
			return device;

	return NULL;
}

/*
 * Find and returns an interrupt translation table entry (ITTE) for a given
 * Device ID/Event ID pair on an ITS.
 * Must be called with the its_lock mutex held.
 */
static struct its_itte *find_itte(struct vgic_its *its, u32 device_id,
				  u32 event_id)
{
	struct its_device *device;
	struct its_itte *itte;

	device = find_its_device(its, device_id);
	if (device == NULL)
		return NULL;

	list_for_each_entry(itte, &device->itt_head, itte_list)
		if (itte->event_id == event_id)
			return itte;

	return NULL;
}

/* To be used as an iterator this macro misses the enclosing parentheses */
#define for_each_lpi_its(dev, itte, its) \
	list_for_each_entry(dev, &(its)->device_list, dev_list) \
		list_for_each_entry(itte, &(dev)->itt_head, itte_list)

/*
 * We only implement 48 bits of PA at the moment, although the ITS
 * supports more. Let's be restrictive here.
 */
#define BASER_ADDRESS(x)	((x) & GENMASK_ULL(47, 16))
#define CBASER_ADDRESS(x)	((x) & GENMASK_ULL(47, 12))
#define PENDBASER_ADDRESS(x)	((x) & GENMASK_ULL(47, 16))
#define PROPBASER_ADDRESS(x)	((x) & GENMASK_ULL(47, 12))

#define GIC_LPI_OFFSET 8192

/*
 * Finds and returns a collection in the ITS collection table.
 * Must be called with the its_lock mutex held.
 */
static struct its_collection *find_collection(struct vgic_its *its, int coll_id)
{
	struct its_collection *collection;

	list_for_each_entry(collection, &its->collection_list, coll_list) {
		if (coll_id == collection->collection_id)
			return collection;
	}

	return NULL;
}

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
 * Promotes the ITS view of affinity of an ITTE (which redistributor this LPI
 * is targeting) to the VGIC's view, which deals with target VCPUs.
 * Needs to be called whenever either the collection for a LPIs has
 * changed or the collection itself got retargeted.
 */
static void update_affinity_itte(struct kvm *kvm, struct its_itte *itte)
{
	struct kvm_vcpu *vcpu;

	if (!its_is_collection_mapped(itte->collection))
		return;

	vcpu = kvm_get_vcpu(kvm, itte->collection->target_addr);

	spin_lock(&itte->irq->irq_lock);
	itte->irq->target_vcpu = vcpu;
	spin_unlock(&itte->irq->irq_lock);
}

/*
 * Updates the target VCPU for every LPI targeting this collection.
 * Must be called with the its_lock mutex held.
 */
static void update_affinity_collection(struct kvm *kvm, struct vgic_its *its,
				       struct its_collection *coll)
{
	struct its_device *device;
	struct its_itte *itte;

	for_each_lpi_its(device, itte, its) {
		if (!itte->collection || coll != itte->collection)
			continue;

		update_affinity_itte(kvm, itte);
	}
}

static u32 max_lpis_propbaser(u64 propbaser)
{
	int nr_idbits = (propbaser & 0x1f) + 1;

	return 1U << min(nr_idbits, INTERRUPT_ID_BITS_ITS);
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

/*
 * Find the target VCPU and the LPI number for a given devid/eventid pair
 * and make this IRQ pending, possibly injecting it.
 * Must be called with the its_lock mutex held.
 */
static void vgic_its_trigger_msi(struct kvm *kvm, struct vgic_its *its,
				 u32 devid, u32 eventid)
{
	struct its_itte *itte;

	if (!its->enabled)
		return;

	itte = find_itte(its, devid, eventid);
	/* Triggering an unmapped IRQ gets silently dropped. */
	if (itte && its_is_collection_mapped(itte->collection)) {
		struct kvm_vcpu *vcpu;

		vcpu = kvm_get_vcpu(kvm, itte->collection->target_addr);
		if (vcpu && vcpu->arch.vgic_cpu.lpis_enabled) {
			spin_lock(&itte->irq->irq_lock);
			itte->irq->pending = true;
			vgic_queue_irq_unlock(kvm, itte->irq);
		}
	}
}

/*
 * Queries the KVM IO bus framework to get the ITS pointer from the given
 * doorbell address.
 * We then call vgic_its_trigger_msi() with the decoded data.
 */
int vgic_its_inject_msi(struct kvm *kvm, struct kvm_msi *msi)
{
	u64 address;
	struct kvm_io_device *kvm_io_dev;
	struct vgic_io_device *iodev;

	if (!vgic_has_its(kvm))
		return -ENODEV;

	if (!(msi->flags & KVM_MSI_VALID_DEVID))
		return -EINVAL;

	address = (u64)msi->address_hi << 32 | msi->address_lo;

	kvm_io_dev = kvm_io_bus_get_dev(kvm, KVM_MMIO_BUS, address);
	if (!kvm_io_dev)
		return -ENODEV;

	iodev = container_of(kvm_io_dev, struct vgic_io_device, dev);

	mutex_lock(&iodev->its->its_lock);
	vgic_its_trigger_msi(kvm, iodev->its, msi->devid, msi->data);
	mutex_unlock(&iodev->its->its_lock);

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

static u64 its_cmd_mask_field(u64 *its_cmd, int word, int shift, int size)
{
	return (le64_to_cpu(its_cmd[word]) >> shift) & (BIT_ULL(size) - 1);
}

#define its_cmd_get_command(cmd)	its_cmd_mask_field(cmd, 0,  0,  8)
#define its_cmd_get_deviceid(cmd)	its_cmd_mask_field(cmd, 0, 32, 32)
#define its_cmd_get_id(cmd)		its_cmd_mask_field(cmd, 1,  0, 32)
#define its_cmd_get_physical_id(cmd)	its_cmd_mask_field(cmd, 1, 32, 32)
#define its_cmd_get_collection(cmd)	its_cmd_mask_field(cmd, 2,  0, 16)
#define its_cmd_get_target_addr(cmd)	its_cmd_mask_field(cmd, 2, 16, 32)
#define its_cmd_get_validbit(cmd)	its_cmd_mask_field(cmd, 2, 63,  1)

/*
 * The DISCARD command frees an Interrupt Translation Table Entry (ITTE).
 * Must be called with the its_lock mutex held.
 */
static int vgic_its_cmd_handle_discard(struct kvm *kvm, struct vgic_its *its,
				       u64 *its_cmd)
{
	u32 device_id = its_cmd_get_deviceid(its_cmd);
	u32 event_id = its_cmd_get_id(its_cmd);
	struct its_itte *itte;


	itte = find_itte(its, device_id, event_id);
	if (itte && itte->collection) {
		/*
		 * Though the spec talks about removing the pending state, we
		 * don't bother here since we clear the ITTE anyway and the
		 * pending state is a property of the ITTE struct.
		 */
		its_free_itte(kvm, itte);
		return 0;
	}

	return E_ITS_DISCARD_UNMAPPED_INTERRUPT;
}

/*
 * The MOVI command moves an ITTE to a different collection.
 * Must be called with the its_lock mutex held.
 */
static int vgic_its_cmd_handle_movi(struct kvm *kvm, struct vgic_its *its,
				    u64 *its_cmd)
{
	u32 device_id = its_cmd_get_deviceid(its_cmd);
	u32 event_id = its_cmd_get_id(its_cmd);
	u32 coll_id = its_cmd_get_collection(its_cmd);
	struct kvm_vcpu *vcpu;
	struct its_itte *itte;
	struct its_collection *collection;

	itte = find_itte(its, device_id, event_id);
	if (!itte)
		return E_ITS_MOVI_UNMAPPED_INTERRUPT;

	if (!its_is_collection_mapped(itte->collection))
		return E_ITS_MOVI_UNMAPPED_COLLECTION;

	collection = find_collection(its, coll_id);
	if (!its_is_collection_mapped(collection))
		return E_ITS_MOVI_UNMAPPED_COLLECTION;

	itte->collection = collection;
	vcpu = kvm_get_vcpu(kvm, collection->target_addr);

	spin_lock(&itte->irq->irq_lock);
	itte->irq->target_vcpu = vcpu;
	spin_unlock(&itte->irq->irq_lock);

	return 0;
}

static void vgic_its_init_collection(struct vgic_its *its,
				     struct its_collection *collection,
				     u32 coll_id)
{
	collection->collection_id = coll_id;
	collection->target_addr = COLLECTION_NOT_MAPPED;

	list_add_tail(&collection->coll_list, &its->collection_list);
}

/*
 * The MAPTI and MAPI commands map LPIs to ITTEs.
 * Must be called with its_lock mutex held.
 */
static int vgic_its_cmd_handle_mapi(struct kvm *kvm, struct vgic_its *its,
				    u64 *its_cmd, u8 subcmd)
{
	u32 device_id = its_cmd_get_deviceid(its_cmd);
	u32 event_id = its_cmd_get_id(its_cmd);
	u32 coll_id = its_cmd_get_collection(its_cmd);
	struct its_itte *itte;
	struct its_device *device;
	struct its_collection *collection, *new_coll = NULL;
	int lpi_nr;

	device = find_its_device(its, device_id);
	if (!device)
		return E_ITS_MAPTI_UNMAPPED_DEVICE;

	collection = find_collection(its, coll_id);
	if (!collection) {
		new_coll = kzalloc(sizeof(struct its_collection), GFP_KERNEL);
		if (!new_coll)
			return -ENOMEM;
	}

	if (subcmd == GITS_CMD_MAPTI)
		lpi_nr = its_cmd_get_physical_id(its_cmd);
	else
		lpi_nr = event_id;
	if (lpi_nr < GIC_LPI_OFFSET ||
	    lpi_nr >= max_lpis_propbaser(kvm->arch.vgic.propbaser)) {
		kfree(new_coll);
		return E_ITS_MAPTI_PHYSICALID_OOR;
	}

	itte = find_itte(its, device_id, event_id);
	if (!itte) {
		itte = kzalloc(sizeof(struct its_itte), GFP_KERNEL);
		if (!itte) {
			kfree(new_coll);
			return -ENOMEM;
		}

		itte->event_id	= event_id;
		list_add_tail(&itte->itte_list, &device->itt_head);
	}

	if (!collection) {
		collection = new_coll;
		vgic_its_init_collection(its, collection, coll_id);
	}

	itte->collection = collection;
	itte->lpi = lpi_nr;
	itte->irq = vgic_add_lpi(kvm, lpi_nr);
	update_affinity_itte(kvm, itte);

	/*
	 * We "cache" the configuration table entries in out struct vgic_irq's.
	 * However we only have those structs for mapped IRQs, so we read in
	 * the respective config data from memory here upon mapping the LPI.
	 */
	update_lpi_config(kvm, itte->irq, NULL);

	return 0;
}

/* Requires the its_lock to be held. */
static void vgic_its_unmap_device(struct kvm *kvm, struct its_device *device)
{
	struct its_itte *itte, *temp;

	/*
	 * The spec says that unmapping a device with still valid
	 * ITTEs associated is UNPREDICTABLE. We remove all ITTEs,
	 * since we cannot leave the memory unreferenced.
	 */
	list_for_each_entry_safe(itte, temp, &device->itt_head, itte_list)
		its_free_itte(kvm, itte);

	list_del(&device->dev_list);
	kfree(device);
}

/*
 * Check whether a device ID can be stored into the guest device tables.
 * For a direct table this is pretty easy, but gets a bit nasty for
 * indirect tables. We check whether the resulting guest physical address
 * is actually valid (covered by a memslot and guest accessbible).
 * For this we have to read the respective first level entry.
 */
static bool vgic_its_check_device_id(struct kvm *kvm, struct vgic_its *its,
				     int device_id)
{
	u64 r = its->baser_device_table;
	int l1_tbl_size = GITS_BASER_NR_PAGES(r) * SZ_64K;
	int index;
	u64 indirect_ptr;
	gfn_t gfn;


	if (!(r & GITS_BASER_INDIRECT)) {
		phys_addr_t addr;

		if (device_id >= (l1_tbl_size / GITS_BASER_ENTRY_SIZE(r)))
			return false;

		addr = BASER_ADDRESS(r) + device_id * GITS_BASER_ENTRY_SIZE(r);
		gfn = addr >> PAGE_SHIFT;

		return kvm_is_visible_gfn(kvm, gfn);
	}

	/* calculate and check the index into the 1st level */
	index = device_id / (SZ_64K / GITS_BASER_ENTRY_SIZE(r));
	if (index >= (l1_tbl_size / sizeof(u64)))
		return false;

	/* Each 1st level entry is represented by a 64-bit value. */
	if (kvm_read_guest(kvm,
			   BASER_ADDRESS(r) + index * sizeof(indirect_ptr),
			   &indirect_ptr, sizeof(indirect_ptr)))
		return false;

	indirect_ptr = le64_to_cpu(indirect_ptr);

	/* check the valid bit of the first level entry */
	if (!(indirect_ptr & BIT_ULL(63)))
		return false;

	/*
	 * Mask the guest physical address and calculate the frame number.
	 * Any address beyond our supported 48 bits of PA will be caught
	 * by the actual check in the final step.
	 */
	gfn = (indirect_ptr & GENMASK_ULL(51, 16)) >> PAGE_SHIFT;

	return kvm_is_visible_gfn(kvm, gfn);
}

/*
 * MAPD maps or unmaps a device ID to Interrupt Translation Tables (ITTs).
 * Must be called with the its_lock mutex held.
 */
static int vgic_its_cmd_handle_mapd(struct kvm *kvm, struct vgic_its *its,
				    u64 *its_cmd)
{
	u32 device_id = its_cmd_get_deviceid(its_cmd);
	bool valid = its_cmd_get_validbit(its_cmd);
	struct its_device *device;

	if (!vgic_its_check_device_id(kvm, its, device_id))
		return E_ITS_MAPD_DEVICE_OOR;

	device = find_its_device(its, device_id);

	/*
	 * The spec says that calling MAPD on an already mapped device
	 * invalidates all cached data for this device. We implement this
	 * by removing the mapping and re-establishing it.
	 */
	if (device)
		vgic_its_unmap_device(kvm, device);

	/*
	 * The spec does not say whether unmapping a not-mapped device
	 * is an error, so we are done in any case.
	 */
	if (!valid)
		return 0;

	device = kzalloc(sizeof(struct its_device), GFP_KERNEL);
	if (!device)
		return -ENOMEM;

	device->device_id = device_id;
	INIT_LIST_HEAD(&device->itt_head);

	list_add_tail(&device->dev_list, &its->device_list);

	return 0;
}

static int vgic_its_nr_collection_ids(struct vgic_its *its)
{
	u64 r = its->baser_coll_table;

	return (GITS_BASER_NR_PAGES(r) * SZ_64K) / GITS_BASER_ENTRY_SIZE(r);
}

/*
 * The MAPC command maps collection IDs to redistributors.
 * Must be called with the its_lock mutex held.
 */
static int vgic_its_cmd_handle_mapc(struct kvm *kvm, struct vgic_its *its,
				    u64 *its_cmd)
{
	u16 coll_id;
	u32 target_addr;
	struct its_collection *collection;
	bool valid;

	valid = its_cmd_get_validbit(its_cmd);
	coll_id = its_cmd_get_collection(its_cmd);
	target_addr = its_cmd_get_target_addr(its_cmd);

	if (target_addr >= atomic_read(&kvm->online_vcpus))
		return E_ITS_MAPC_PROCNUM_OOR;

	if (coll_id >= vgic_its_nr_collection_ids(its))
		return E_ITS_MAPC_COLLECTION_OOR;

	collection = find_collection(its, coll_id);

	if (!valid) {
		struct its_device *device;
		struct its_itte *itte;
		/*
		 * Clearing the mapping for that collection ID removes the
		 * entry from the list. If there wasn't any before, we can
		 * go home early.
		 */
		if (!collection)
			return 0;

		for_each_lpi_its(device, itte, its)
			if (itte->collection &&
			    itte->collection->collection_id == coll_id)
				itte->collection = NULL;

		list_del(&collection->coll_list);
		kfree(collection);
	} else {
		if (!collection) {
			collection = kzalloc(sizeof(struct its_collection),
					     GFP_KERNEL);
			if (!collection)
				return -ENOMEM;

			vgic_its_init_collection(its, collection, coll_id);
			collection->target_addr = target_addr;
		} else {
			collection->target_addr = target_addr;
			update_affinity_collection(kvm, its, collection);
		}
	}

	return 0;
}

/*
 * The CLEAR command removes the pending state for a particular LPI.
 * Must be called with the its_lock mutex held.
 */
static int vgic_its_cmd_handle_clear(struct kvm *kvm, struct vgic_its *its,
				     u64 *its_cmd)
{
	u32 device_id = its_cmd_get_deviceid(its_cmd);
	u32 event_id = its_cmd_get_id(its_cmd);
	struct its_itte *itte;


	itte = find_itte(its, device_id, event_id);
	if (!itte)
		return E_ITS_CLEAR_UNMAPPED_INTERRUPT;

	itte->irq->pending = false;

	return 0;
}

/*
 * The INV command syncs the configuration bits from the memory table.
 * Must be called with the its_lock mutex held.
 */
static int vgic_its_cmd_handle_inv(struct kvm *kvm, struct vgic_its *its,
				   u64 *its_cmd)
{
	u32 device_id = its_cmd_get_deviceid(its_cmd);
	u32 event_id = its_cmd_get_id(its_cmd);
	struct its_itte *itte;


	itte = find_itte(its, device_id, event_id);
	if (!itte)
		return E_ITS_INV_UNMAPPED_INTERRUPT;

	return update_lpi_config(kvm, itte->irq, NULL);
}

/*
 * The INVALL command requests flushing of all IRQ data in this collection.
 * Find the VCPU mapped to that collection, then iterate over the VM's list
 * of mapped LPIs and update the configuration for each IRQ which targets
 * the specified vcpu. The configuration will be read from the in-memory
 * configuration table.
 * Must be called with the its_lock mutex held.
 */
static int vgic_its_cmd_handle_invall(struct kvm *kvm, struct vgic_its *its,
				      u64 *its_cmd)
{
	u32 coll_id = its_cmd_get_collection(its_cmd);
	struct its_collection *collection;
	struct kvm_vcpu *vcpu;
	struct vgic_irq *irq;
	u32 *intids;
	int irq_count, i;

	collection = find_collection(its, coll_id);
	if (!its_is_collection_mapped(collection))
		return E_ITS_INVALL_UNMAPPED_COLLECTION;

	vcpu = kvm_get_vcpu(kvm, collection->target_addr);

	irq_count = vgic_copy_lpi_list(kvm, &intids);
	if (irq_count < 0)
		return irq_count;

	for (i = 0; i < irq_count; i++) {
		irq = vgic_get_irq(kvm, NULL, intids[i]);
		if (!irq)
			continue;
		update_lpi_config(kvm, irq, vcpu);
		vgic_put_irq(kvm, irq);
	}

	kfree(intids);

	return 0;
}

/*
 * The MOVALL command moves the pending state of all IRQs targeting one
 * redistributor to another. We don't hold the pending state in the VCPUs,
 * but in the IRQs instead, so there is really not much to do for us here.
 * However the spec says that no IRQ must target the old redistributor
 * afterwards, so we make sure that no LPI is using the associated target_vcpu.
 * This command affects all LPIs in the system that target that redistributor.
 */
static int vgic_its_cmd_handle_movall(struct kvm *kvm, struct vgic_its *its,
				      u64 *its_cmd)
{
	struct vgic_dist *dist = &kvm->arch.vgic;
	u32 target1_addr = its_cmd_get_target_addr(its_cmd);
	u32 target2_addr = its_cmd_mask_field(its_cmd, 3, 16, 32);
	struct kvm_vcpu *vcpu1, *vcpu2;
	struct vgic_irq *irq;

	if (target1_addr >= atomic_read(&kvm->online_vcpus) ||
	    target2_addr >= atomic_read(&kvm->online_vcpus))
		return E_ITS_MOVALL_PROCNUM_OOR;

	if (target1_addr == target2_addr)
		return 0;

	vcpu1 = kvm_get_vcpu(kvm, target1_addr);
	vcpu2 = kvm_get_vcpu(kvm, target2_addr);

	spin_lock(&dist->lpi_list_lock);

	list_for_each_entry(irq, &dist->lpi_list_head, lpi_list) {
		spin_lock(&irq->irq_lock);

		if (irq->target_vcpu == vcpu1)
			irq->target_vcpu = vcpu2;

		spin_unlock(&irq->irq_lock);
	}

	spin_unlock(&dist->lpi_list_lock);

	return 0;
}

/*
 * The INT command injects the LPI associated with that DevID/EvID pair.
 * Must be called with the its_lock mutex held.
 */
static int vgic_its_cmd_handle_int(struct kvm *kvm, struct vgic_its *its,
				   u64 *its_cmd)
{
	u32 msi_data = its_cmd_get_id(its_cmd);
	u64 msi_devid = its_cmd_get_deviceid(its_cmd);

	vgic_its_trigger_msi(kvm, its, msi_devid, msi_data);

	return 0;
}

/*
 * This function is called with the its_cmd lock held, but the ITS data
 * structure lock dropped.
 */
static int vgic_its_handle_command(struct kvm *kvm, struct vgic_its *its,
				   u64 *its_cmd)
{
	u8 cmd = its_cmd_get_command(its_cmd);
	int ret = -ENODEV;

	mutex_lock(&its->its_lock);
	switch (cmd) {
	case GITS_CMD_MAPD:
		ret = vgic_its_cmd_handle_mapd(kvm, its, its_cmd);
		break;
	case GITS_CMD_MAPC:
		ret = vgic_its_cmd_handle_mapc(kvm, its, its_cmd);
		break;
	case GITS_CMD_MAPI:
		ret = vgic_its_cmd_handle_mapi(kvm, its, its_cmd, cmd);
		break;
	case GITS_CMD_MAPTI:
		ret = vgic_its_cmd_handle_mapi(kvm, its, its_cmd, cmd);
		break;
	case GITS_CMD_MOVI:
		ret = vgic_its_cmd_handle_movi(kvm, its, its_cmd);
		break;
	case GITS_CMD_DISCARD:
		ret = vgic_its_cmd_handle_discard(kvm, its, its_cmd);
		break;
	case GITS_CMD_CLEAR:
		ret = vgic_its_cmd_handle_clear(kvm, its, its_cmd);
		break;
	case GITS_CMD_MOVALL:
		ret = vgic_its_cmd_handle_movall(kvm, its, its_cmd);
		break;
	case GITS_CMD_INT:
		ret = vgic_its_cmd_handle_int(kvm, its, its_cmd);
		break;
	case GITS_CMD_INV:
		ret = vgic_its_cmd_handle_inv(kvm, its, its_cmd);
		break;
	case GITS_CMD_INVALL:
		ret = vgic_its_cmd_handle_invall(kvm, its, its_cmd);
		break;
	case GITS_CMD_SYNC:
		/* we ignore this command: we are in sync all of the time */
		ret = 0;
		break;
	}
	mutex_unlock(&its->its_lock);

	return ret;
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
