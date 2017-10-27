/*
 * Copyright (C) 2017 ARM Ltd.
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

#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/kvm_host.h>
#include <linux/irqchip/arm-gic-v3.h>

#include "vgic.h"

/**
 * vgic_v4_init - Initialize the GICv4 data structures
 * @kvm:	Pointer to the VM being initialized
 *
 * We may be called each time a vITS is created, or when the
 * vgic is initialized. This relies on kvm->lock to be
 * held. In both cases, the number of vcpus should now be
 * fixed.
 */
int vgic_v4_init(struct kvm *kvm)
{
	struct vgic_dist *dist = &kvm->arch.vgic;
	struct kvm_vcpu *vcpu;
	int i, nr_vcpus, ret;

	if (dist->its_vm.vpes)
		return 0;

	nr_vcpus = atomic_read(&kvm->online_vcpus);

	dist->its_vm.vpes = kzalloc(sizeof(*dist->its_vm.vpes) * nr_vcpus,
				    GFP_KERNEL);
	if (!dist->its_vm.vpes)
		return -ENOMEM;

	dist->its_vm.nr_vpes = nr_vcpus;

	kvm_for_each_vcpu(i, vcpu, kvm)
		dist->its_vm.vpes[i] = &vcpu->arch.vgic_cpu.vgic_v3.its_vpe;

	ret = its_alloc_vcpu_irqs(&dist->its_vm);
	if (ret < 0) {
		kvm_err("VPE IRQ allocation failure\n");
		kfree(dist->its_vm.vpes);
		dist->its_vm.nr_vpes = 0;
		dist->its_vm.vpes = NULL;
		return ret;
	}

	return ret;
}

/**
 * vgic_v4_teardown - Free the GICv4 data structures
 * @kvm:	Pointer to the VM being destroyed
 *
 * Relies on kvm->lock to be held.
 */
void vgic_v4_teardown(struct kvm *kvm)
{
	struct its_vm *its_vm = &kvm->arch.vgic.its_vm;

	if (!its_vm->vpes)
		return;

	its_free_vcpu_irqs(its_vm);
	kfree(its_vm->vpes);
	its_vm->nr_vpes = 0;
	its_vm->vpes = NULL;
}

static struct vgic_its *vgic_get_its(struct kvm *kvm,
				     struct kvm_kernel_irq_routing_entry *irq_entry)
{
	struct kvm_msi msi  = (struct kvm_msi) {
		.address_lo	= irq_entry->msi.address_lo,
		.address_hi	= irq_entry->msi.address_hi,
		.data		= irq_entry->msi.data,
		.flags		= irq_entry->msi.flags,
		.devid		= irq_entry->msi.devid,
	};

	return vgic_msi_to_its(kvm, &msi);
}

int kvm_vgic_v4_set_forwarding(struct kvm *kvm, int virq,
			       struct kvm_kernel_irq_routing_entry *irq_entry)
{
	struct vgic_its *its;
	struct vgic_irq *irq;
	struct its_vlpi_map map;
	int ret;

	if (!vgic_supports_direct_msis(kvm))
		return 0;

	/*
	 * Get the ITS, and escape early on error (not a valid
	 * doorbell for any of our vITSs).
	 */
	its = vgic_get_its(kvm, irq_entry);
	if (IS_ERR(its))
		return 0;

	mutex_lock(&its->its_lock);

	/* Perform then actual DevID/EventID -> LPI translation. */
	ret = vgic_its_resolve_lpi(kvm, its, irq_entry->msi.devid,
				   irq_entry->msi.data, &irq);
	if (ret)
		goto out;

	/*
	 * Emit the mapping request. If it fails, the ITS probably
	 * isn't v4 compatible, so let's silently bail out. Holding
	 * the ITS lock should ensure that nothing can modify the
	 * target vcpu.
	 */
	map = (struct its_vlpi_map) {
		.vm		= &kvm->arch.vgic.its_vm,
		.vpe		= &irq->target_vcpu->arch.vgic_cpu.vgic_v3.its_vpe,
		.vintid		= irq->intid,
		.properties	= ((irq->priority & 0xfc) |
				   (irq->enabled ? LPI_PROP_ENABLED : 0) |
				   LPI_PROP_GROUP1),
		.db_enabled	= true,
	};

	ret = its_map_vlpi(virq, &map);
	if (ret)
		goto out;

	irq->hw		= true;
	irq->host_irq	= virq;

out:
	mutex_unlock(&its->its_lock);
	return ret;
}

int kvm_vgic_v4_unset_forwarding(struct kvm *kvm, int virq,
				 struct kvm_kernel_irq_routing_entry *irq_entry)
{
	struct vgic_its *its;
	struct vgic_irq *irq;
	int ret;

	if (!vgic_supports_direct_msis(kvm))
		return 0;

	/*
	 * Get the ITS, and escape early on error (not a valid
	 * doorbell for any of our vITSs).
	 */
	its = vgic_get_its(kvm, irq_entry);
	if (IS_ERR(its))
		return 0;

	mutex_lock(&its->its_lock);

	ret = vgic_its_resolve_lpi(kvm, its, irq_entry->msi.devid,
				   irq_entry->msi.data, &irq);
	if (ret)
		goto out;

	WARN_ON(!(irq->hw && irq->host_irq == virq));
	irq->hw = false;
	ret = its_unmap_vlpi(virq);

out:
	mutex_unlock(&its->its_lock);
	return ret;
}
