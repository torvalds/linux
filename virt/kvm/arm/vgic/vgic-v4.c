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
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kvm_host.h>
#include <linux/irqchip/arm-gic-v3.h>

#include "vgic.h"

static irqreturn_t vgic_v4_doorbell_handler(int irq, void *info)
{
	struct kvm_vcpu *vcpu = info;

	vcpu->arch.vgic_cpu.vgic_v3.its_vpe.pending_last = true;
	kvm_make_request(KVM_REQ_IRQ_PENDING, vcpu);
	kvm_vcpu_kick(vcpu);

	return IRQ_HANDLED;
}

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

	kvm_for_each_vcpu(i, vcpu, kvm) {
		int irq = dist->its_vm.vpes[i]->irq;

		/*
		 * Don't automatically enable the doorbell, as we're
		 * flipping it back and forth when the vcpu gets
		 * blocked. Also disable the lazy disabling, as the
		 * doorbell could kick us out of the guest too
		 * early...
		 */
		irq_set_status_flags(irq, IRQ_NOAUTOEN | IRQ_DISABLE_UNLAZY);
		ret = request_irq(irq, vgic_v4_doorbell_handler,
				  0, "vcpu", vcpu);
		if (ret) {
			kvm_err("failed to allocate vcpu IRQ%d\n", irq);
			/*
			 * Trick: adjust the number of vpes so we know
			 * how many to nuke on teardown...
			 */
			dist->its_vm.nr_vpes = i;
			break;
		}
	}

	if (ret)
		vgic_v4_teardown(kvm);

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
	int i;

	if (!its_vm->vpes)
		return;

	for (i = 0; i < its_vm->nr_vpes; i++) {
		struct kvm_vcpu *vcpu = kvm_get_vcpu(kvm, i);
		int irq = its_vm->vpes[i]->irq;

		irq_clear_status_flags(irq, IRQ_NOAUTOEN | IRQ_DISABLE_UNLAZY);
		free_irq(irq, vcpu);
	}

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

void kvm_vgic_v4_enable_doorbell(struct kvm_vcpu *vcpu)
{
	if (vgic_supports_direct_msis(vcpu->kvm)) {
		int irq = vcpu->arch.vgic_cpu.vgic_v3.its_vpe.irq;
		if (irq)
			enable_irq(irq);
	}
}

void kvm_vgic_v4_disable_doorbell(struct kvm_vcpu *vcpu)
{
	if (vgic_supports_direct_msis(vcpu->kvm)) {
		int irq = vcpu->arch.vgic_cpu.vgic_v3.its_vpe.irq;
		if (irq)
			disable_irq(irq);
	}
}
