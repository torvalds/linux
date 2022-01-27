// SPDX-License-Identifier: GPL-2.0
/*
 * ARM Generic Interrupt Controller (GIC) v3 host support
 */

#include <linux/kvm.h>
#include <linux/sizes.h>
#include <asm/kvm_para.h>
#include <asm/kvm.h>

#include "kvm_util.h"
#include "../kvm_util_internal.h"
#include "vgic.h"
#include "gic.h"
#include "gic_v3.h"

/*
 * vGIC-v3 default host setup
 *
 * Input args:
 *	vm - KVM VM
 *	nr_vcpus - Number of vCPUs supported by this VM
 *	gicd_base_gpa - Guest Physical Address of the Distributor region
 *	gicr_base_gpa - Guest Physical Address of the Redistributor region
 *
 * Output args: None
 *
 * Return: GIC file-descriptor or negative error code upon failure
 *
 * The function creates a vGIC-v3 device and maps the distributor and
 * redistributor regions of the guest. Since it depends on the number of
 * vCPUs for the VM, it must be called after all the vCPUs have been created.
 */
int vgic_v3_setup(struct kvm_vm *vm, unsigned int nr_vcpus, uint32_t nr_irqs,
		uint64_t gicd_base_gpa, uint64_t gicr_base_gpa)
{
	int gic_fd;
	uint64_t redist_attr;
	struct list_head *iter;
	unsigned int nr_gic_pages, nr_vcpus_created = 0;

	TEST_ASSERT(nr_vcpus, "Number of vCPUs cannot be empty\n");

	/*
	 * Make sure that the caller is infact calling this
	 * function after all the vCPUs are added.
	 */
	list_for_each(iter, &vm->vcpus)
		nr_vcpus_created++;
	TEST_ASSERT(nr_vcpus == nr_vcpus_created,
			"Number of vCPUs requested (%u) doesn't match with the ones created for the VM (%u)\n",
			nr_vcpus, nr_vcpus_created);

	/* Distributor setup */
	gic_fd = kvm_create_device(vm, KVM_DEV_TYPE_ARM_VGIC_V3, false);

	kvm_device_access(gic_fd, KVM_DEV_ARM_VGIC_GRP_NR_IRQS,
			0, &nr_irqs, true);

	kvm_device_access(gic_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
			KVM_DEV_ARM_VGIC_CTRL_INIT, NULL, true);

	kvm_device_access(gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
			KVM_VGIC_V3_ADDR_TYPE_DIST, &gicd_base_gpa, true);
	nr_gic_pages = vm_calc_num_guest_pages(vm->mode, KVM_VGIC_V3_DIST_SIZE);
	virt_map(vm, gicd_base_gpa, gicd_base_gpa,  nr_gic_pages);

	/* Redistributor setup */
	redist_attr = REDIST_REGION_ATTR_ADDR(nr_vcpus, gicr_base_gpa, 0, 0);
	kvm_device_access(gic_fd, KVM_DEV_ARM_VGIC_GRP_ADDR,
			KVM_VGIC_V3_ADDR_TYPE_REDIST_REGION, &redist_attr, true);
	nr_gic_pages = vm_calc_num_guest_pages(vm->mode,
						KVM_VGIC_V3_REDIST_SIZE * nr_vcpus);
	virt_map(vm, gicr_base_gpa, gicr_base_gpa,  nr_gic_pages);

	kvm_device_access(gic_fd, KVM_DEV_ARM_VGIC_GRP_CTRL,
				KVM_DEV_ARM_VGIC_CTRL_INIT, NULL, true);

	return gic_fd;
}

/* should only work for level sensitive interrupts */
int _kvm_irq_set_level_info(int gic_fd, uint32_t intid, int level)
{
	uint64_t attr = 32 * (intid / 32);
	uint64_t index = intid % 32;
	uint64_t val;
	int ret;

	ret = _kvm_device_access(gic_fd, KVM_DEV_ARM_VGIC_GRP_LEVEL_INFO,
				 attr, &val, false);
	if (ret != 0)
		return ret;

	val |= 1U << index;
	ret = _kvm_device_access(gic_fd, KVM_DEV_ARM_VGIC_GRP_LEVEL_INFO,
				 attr, &val, true);
	return ret;
}

void kvm_irq_set_level_info(int gic_fd, uint32_t intid, int level)
{
	int ret = _kvm_irq_set_level_info(gic_fd, intid, level);

	TEST_ASSERT(ret == 0, "KVM_DEV_ARM_VGIC_GRP_LEVEL_INFO failed, "
			"rc: %i errno: %i", ret, errno);
}

int _kvm_arm_irq_line(struct kvm_vm *vm, uint32_t intid, int level)
{
	uint32_t irq = intid & KVM_ARM_IRQ_NUM_MASK;

	TEST_ASSERT(!INTID_IS_SGI(intid), "KVM_IRQ_LINE's interface itself "
		"doesn't allow injecting SGIs. There's no mask for it.");

	if (INTID_IS_PPI(intid))
		irq |= KVM_ARM_IRQ_TYPE_PPI << KVM_ARM_IRQ_TYPE_SHIFT;
	else
		irq |= KVM_ARM_IRQ_TYPE_SPI << KVM_ARM_IRQ_TYPE_SHIFT;

	return _kvm_irq_line(vm, irq, level);
}

void kvm_arm_irq_line(struct kvm_vm *vm, uint32_t intid, int level)
{
	int ret = _kvm_arm_irq_line(vm, intid, level);

	TEST_ASSERT(ret == 0, "KVM_IRQ_LINE failed, rc: %i errno: %i",
			ret, errno);
}

static void vgic_poke_irq(int gic_fd, uint32_t intid,
		uint32_t vcpu, uint64_t reg_off)
{
	uint64_t reg = intid / 32;
	uint64_t index = intid % 32;
	uint64_t attr = reg_off + reg * 4;
	uint64_t val;
	bool intid_is_private = INTID_IS_SGI(intid) || INTID_IS_PPI(intid);

	/* Check that the addr part of the attr is within 32 bits. */
	assert(attr <= KVM_DEV_ARM_VGIC_OFFSET_MASK);

	uint32_t group = intid_is_private ? KVM_DEV_ARM_VGIC_GRP_REDIST_REGS
					  : KVM_DEV_ARM_VGIC_GRP_DIST_REGS;

	if (intid_is_private) {
		/* TODO: only vcpu 0 implemented for now. */
		assert(vcpu == 0);
		attr += SZ_64K;
	}

	/*
	 * All calls will succeed, even with invalid intid's, as long as the
	 * addr part of the attr is within 32 bits (checked above). An invalid
	 * intid will just make the read/writes point to above the intended
	 * register space (i.e., ICPENDR after ISPENDR).
	 */
	kvm_device_access(gic_fd, group, attr, &val, false);
	val |= 1ULL << index;
	kvm_device_access(gic_fd, group, attr, &val, true);
}

void kvm_irq_write_ispendr(int gic_fd, uint32_t intid, uint32_t vcpu)
{
	vgic_poke_irq(gic_fd, intid, vcpu, GICD_ISPENDR);
}

void kvm_irq_write_isactiver(int gic_fd, uint32_t intid, uint32_t vcpu)
{
	vgic_poke_irq(gic_fd, intid, vcpu, GICD_ISACTIVER);
}
