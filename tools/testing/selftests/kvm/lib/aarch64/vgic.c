// SPDX-License-Identifier: GPL-2.0
/*
 * ARM Generic Interrupt Controller (GIC) v3 host support
 */

#include <linux/kvm.h>
#include <linux/sizes.h>
#include <asm/kvm.h>

#include "kvm_util.h"
#include "../kvm_util_internal.h"
#include "vgic.h"

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
int vgic_v3_setup(struct kvm_vm *vm, unsigned int nr_vcpus,
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
