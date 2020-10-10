/*
 * Copyright (C) 2015, 2016 ARM Ltd.
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

#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/cpu.h>
#include <linux/kvm_host.h>
#include <kvm/arm_vgic.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_mmu.h>
#include "vgic.h"

/*
 * Initialization rules: there are multiple stages to the vgic
 * initialization, both for the distributor and the CPU interfaces.  The basic
 * idea is that even though the VGIC is not functional or not requested from
 * user space, the critical path of the run loop can still call VGIC functions
 * that just won't do anything, without them having to check additional
 * initialization flags to ensure they don't look at uninitialized data
 * structures.
 *
 * Distributor:
 *
 * - kvm_vgic_early_init(): initialization of static data that doesn't
 *   depend on any sizing information or emulation type. No allocation
 *   is allowed there.
 *
 * - vgic_init(): allocation and initialization of the generic data
 *   structures that depend on sizing information (number of CPUs,
 *   number of interrupts). Also initializes the vcpu specific data
 *   structures. Can be executed lazily for GICv2.
 *
 * CPU Interface:
 *
 * - kvm_vgic_vcpu_init(): initialization of static data that
 *   doesn't depend on any sizing information or emulation type. No
 *   allocation is allowed there.
 */

/* EARLY INIT */

/**
 * kvm_vgic_early_init() - Initialize static VGIC VCPU data structures
 * @kvm: The VM whose VGIC districutor should be initialized
 *
 * Only do initialization of static structures that don't require any
 * allocation or sizing information from userspace.  vgic_init() called
 * kvm_vgic_dist_init() which takes care of the rest.
 */
void kvm_vgic_early_init(struct kvm *kvm)
{
	struct vgic_dist *dist = &kvm->arch.vgic;

	INIT_LIST_HEAD(&dist->lpi_list_head);
	raw_spin_lock_init(&dist->lpi_list_lock);
}

/* CREATION */

/**
 * kvm_vgic_create: triggered by the instantiation of the VGIC device by
 * user space, either through the legacy KVM_CREATE_IRQCHIP ioctl (v2 only)
 * or through the generic KVM_CREATE_DEVICE API ioctl.
 * irqchip_in_kernel() tells you if this function succeeded or not.
 * @kvm: kvm struct pointer
 * @type: KVM_DEV_TYPE_ARM_VGIC_V[23]
 */
int kvm_vgic_create(struct kvm *kvm, u32 type)
{
	int i, vcpu_lock_idx = -1, ret;
	struct kvm_vcpu *vcpu;

	if (irqchip_in_kernel(kvm))
		return -EEXIST;

	/*
	 * This function is also called by the KVM_CREATE_IRQCHIP handler,
	 * which had no chance yet to check the availability of the GICv2
	 * emulation. So check this here again. KVM_CREATE_DEVICE does
	 * the proper checks already.
	 */
	if (type == KVM_DEV_TYPE_ARM_VGIC_V2 &&
		!kvm_vgic_global_state.can_emulate_gicv2)
		return -ENODEV;

	/*
	 * Any time a vcpu is run, vcpu_load is called which tries to grab the
	 * vcpu->mutex.  By grabbing the vcpu->mutex of all VCPUs we ensure
	 * that no other VCPUs are run while we create the vgic.
	 */
	ret = -EBUSY;
	kvm_for_each_vcpu(i, vcpu, kvm) {
		if (!mutex_trylock(&vcpu->mutex))
			goto out_unlock;
		vcpu_lock_idx = i;
	}

	kvm_for_each_vcpu(i, vcpu, kvm) {
		if (vcpu->arch.has_run_once)
			goto out_unlock;
	}
	ret = 0;

	if (type == KVM_DEV_TYPE_ARM_VGIC_V2)
		kvm->arch.max_vcpus = VGIC_V2_MAX_CPUS;
	else
		kvm->arch.max_vcpus = VGIC_V3_MAX_CPUS;

	if (atomic_read(&kvm->online_vcpus) > kvm->arch.max_vcpus) {
		ret = -E2BIG;
		goto out_unlock;
	}

	kvm->arch.vgic.in_kernel = true;
	kvm->arch.vgic.vgic_model = type;

	kvm->arch.vgic.vgic_dist_base = VGIC_ADDR_UNDEF;

	if (type == KVM_DEV_TYPE_ARM_VGIC_V2)
		kvm->arch.vgic.vgic_cpu_base = VGIC_ADDR_UNDEF;
	else
		INIT_LIST_HEAD(&kvm->arch.vgic.rd_regions);

out_unlock:
	for (; vcpu_lock_idx >= 0; vcpu_lock_idx--) {
		vcpu = kvm_get_vcpu(kvm, vcpu_lock_idx);
		mutex_unlock(&vcpu->mutex);
	}
	return ret;
}

/* INIT/DESTROY */

/**
 * kvm_vgic_dist_init: initialize the dist data structures
 * @kvm: kvm struct pointer
 * @nr_spis: number of spis, frozen by caller
 */
static int kvm_vgic_dist_init(struct kvm *kvm, unsigned int nr_spis)
{
	struct vgic_dist *dist = &kvm->arch.vgic;
	struct kvm_vcpu *vcpu0 = kvm_get_vcpu(kvm, 0);
	int i;

	dist->spis = kcalloc(nr_spis, sizeof(struct vgic_irq), GFP_KERNEL);
	if (!dist->spis)
		return  -ENOMEM;

	/*
	 * In the following code we do not take the irq struct lock since
	 * no other action on irq structs can happen while the VGIC is
	 * not initialized yet:
	 * If someone wants to inject an interrupt or does a MMIO access, we
	 * require prior initialization in case of a virtual GICv3 or trigger
	 * initialization when using a virtual GICv2.
	 */
	for (i = 0; i < nr_spis; i++) {
		struct vgic_irq *irq = &dist->spis[i];

		irq->intid = i + VGIC_NR_PRIVATE_IRQS;
		INIT_LIST_HEAD(&irq->ap_list);
		spin_lock_init(&irq->irq_lock);
		irq->vcpu = NULL;
		irq->target_vcpu = vcpu0;
		kref_init(&irq->refcount);
		switch (dist->vgic_model) {
		case KVM_DEV_TYPE_ARM_VGIC_V2:
			irq->targets = 0;
			irq->group = 0;
			break;
		case KVM_DEV_TYPE_ARM_VGIC_V3:
			irq->mpidr = 0;
			irq->group = 1;
			break;
		default:
			kfree(dist->spis);
			dist->spis = NULL;
			return -EINVAL;
		}
	}
	return 0;
}

/**
 * kvm_vgic_vcpu_init() - Initialize static VGIC VCPU data
 * structures and register VCPU-specific KVM iodevs
 *
 * @vcpu: pointer to the VCPU being created and initialized
 *
 * Only do initialization, but do not actually enable the
 * VGIC CPU interface
 */
int kvm_vgic_vcpu_init(struct kvm_vcpu *vcpu)
{
	struct vgic_cpu *vgic_cpu = &vcpu->arch.vgic_cpu;
	struct vgic_dist *dist = &vcpu->kvm->arch.vgic;
	int ret = 0;
	int i;

	vgic_cpu->rd_iodev.base_addr = VGIC_ADDR_UNDEF;
	vgic_cpu->sgi_iodev.base_addr = VGIC_ADDR_UNDEF;

	INIT_LIST_HEAD(&vgic_cpu->ap_list_head);
	spin_lock_init(&vgic_cpu->ap_list_lock);

	/*
	 * Enable and configure all SGIs to be edge-triggered and
	 * configure all PPIs as level-triggered.
	 */
	for (i = 0; i < VGIC_NR_PRIVATE_IRQS; i++) {
		struct vgic_irq *irq = &vgic_cpu->private_irqs[i];

		INIT_LIST_HEAD(&irq->ap_list);
		spin_lock_init(&irq->irq_lock);
		irq->intid = i;
		irq->vcpu = NULL;
		irq->target_vcpu = vcpu;
		kref_init(&irq->refcount);
		if (vgic_irq_is_sgi(i)) {
			/* SGIs */
			irq->enabled = 1;
			irq->config = VGIC_CONFIG_EDGE;
		} else {
			/* PPIs */
			irq->config = VGIC_CONFIG_LEVEL;
		}
	}

	if (!irqchip_in_kernel(vcpu->kvm))
		return 0;

	/*
	 * If we are creating a VCPU with a GICv3 we must also register the
	 * KVM io device for the redistributor that belongs to this VCPU.
	 */
	if (dist->vgic_model == KVM_DEV_TYPE_ARM_VGIC_V3) {
		mutex_lock(&vcpu->kvm->lock);
		ret = vgic_register_redist_iodev(vcpu);
		mutex_unlock(&vcpu->kvm->lock);
	}
	return ret;
}

static void kvm_vgic_vcpu_enable(struct kvm_vcpu *vcpu)
{
	if (kvm_vgic_global_state.type == VGIC_V2)
		vgic_v2_enable(vcpu);
	else
		vgic_v3_enable(vcpu);
}

/*
 * vgic_init: allocates and initializes dist and vcpu data structures
 * depending on two dimensioning parameters:
 * - the number of spis
 * - the number of vcpus
 * The function is generally called when nr_spis has been explicitly set
 * by the guest through the KVM DEVICE API. If not nr_spis is set to 256.
 * vgic_initialized() returns true when this function has succeeded.
 * Must be called with kvm->lock held!
 */
int vgic_init(struct kvm *kvm)
{
	struct vgic_dist *dist = &kvm->arch.vgic;
	struct kvm_vcpu *vcpu;
	int ret = 0, i, idx;

	if (vgic_initialized(kvm))
		return 0;

	/* Are we also in the middle of creating a VCPU? */
	if (kvm->created_vcpus != atomic_read(&kvm->online_vcpus))
		return -EBUSY;

	/* freeze the number of spis */
	if (!dist->nr_spis)
		dist->nr_spis = VGIC_NR_IRQS_LEGACY - VGIC_NR_PRIVATE_IRQS;

	ret = kvm_vgic_dist_init(kvm, dist->nr_spis);
	if (ret)
		goto out;

	/* Initialize groups on CPUs created before the VGIC type was known */
	kvm_for_each_vcpu(idx, vcpu, kvm) {
		struct vgic_cpu *vgic_cpu = &vcpu->arch.vgic_cpu;

		for (i = 0; i < VGIC_NR_PRIVATE_IRQS; i++) {
			struct vgic_irq *irq = &vgic_cpu->private_irqs[i];
			switch (dist->vgic_model) {
			case KVM_DEV_TYPE_ARM_VGIC_V3:
				irq->group = 1;
				irq->mpidr = kvm_vcpu_get_mpidr_aff(vcpu);
				break;
			case KVM_DEV_TYPE_ARM_VGIC_V2:
				irq->group = 0;
				irq->targets = 1U << idx;
				break;
			default:
				ret = -EINVAL;
				goto out;
			}
		}
	}

	if (vgic_has_its(kvm)) {
		ret = vgic_v4_init(kvm);
		if (ret)
			goto out;
	}

	kvm_for_each_vcpu(i, vcpu, kvm)
		kvm_vgic_vcpu_enable(vcpu);

	ret = kvm_vgic_setup_default_irq_routing(kvm);
	if (ret)
		goto out;

	vgic_debug_init(kvm);

	dist->implementation_rev = 2;
	dist->initialized = true;

out:
	return ret;
}

static void kvm_vgic_dist_destroy(struct kvm *kvm)
{
	struct vgic_dist *dist = &kvm->arch.vgic;
	struct vgic_redist_region *rdreg, *next;

	dist->ready = false;
	dist->initialized = false;

	kfree(dist->spis);
	dist->spis = NULL;
	dist->nr_spis = 0;

	if (kvm->arch.vgic.vgic_model == KVM_DEV_TYPE_ARM_VGIC_V3) {
		list_for_each_entry_safe(rdreg, next, &dist->rd_regions, list) {
			list_del(&rdreg->list);
			kfree(rdreg);
		}
		INIT_LIST_HEAD(&dist->rd_regions);
	}

	if (vgic_supports_direct_msis(kvm))
		vgic_v4_teardown(kvm);
}

void kvm_vgic_vcpu_destroy(struct kvm_vcpu *vcpu)
{
	struct vgic_cpu *vgic_cpu = &vcpu->arch.vgic_cpu;

	INIT_LIST_HEAD(&vgic_cpu->ap_list_head);
}

/* To be called with kvm->lock held */
static void __kvm_vgic_destroy(struct kvm *kvm)
{
	struct kvm_vcpu *vcpu;
	int i;

	vgic_debug_destroy(kvm);

	kvm_vgic_dist_destroy(kvm);

	kvm_for_each_vcpu(i, vcpu, kvm)
		kvm_vgic_vcpu_destroy(vcpu);
}

void kvm_vgic_destroy(struct kvm *kvm)
{
	mutex_lock(&kvm->lock);
	__kvm_vgic_destroy(kvm);
	mutex_unlock(&kvm->lock);
}

/**
 * vgic_lazy_init: Lazy init is only allowed if the GIC exposed to the guest
 * is a GICv2. A GICv3 must be explicitly initialized by the guest using the
 * KVM_DEV_ARM_VGIC_GRP_CTRL KVM_DEVICE group.
 * @kvm: kvm struct pointer
 */
int vgic_lazy_init(struct kvm *kvm)
{
	int ret = 0;

	if (unlikely(!vgic_initialized(kvm))) {
		/*
		 * We only provide the automatic initialization of the VGIC
		 * for the legacy case of a GICv2. Any other type must
		 * be explicitly initialized once setup with the respective
		 * KVM device call.
		 */
		if (kvm->arch.vgic.vgic_model != KVM_DEV_TYPE_ARM_VGIC_V2)
			return -EBUSY;

		mutex_lock(&kvm->lock);
		ret = vgic_init(kvm);
		mutex_unlock(&kvm->lock);
	}

	return ret;
}

/* RESOURCE MAPPING */

/**
 * Map the MMIO regions depending on the VGIC model exposed to the guest
 * called on the first VCPU run.
 * Also map the virtual CPU interface into the VM.
 * v2/v3 derivatives call vgic_init if not already done.
 * vgic_ready() returns true if this function has succeeded.
 * @kvm: kvm struct pointer
 */
int kvm_vgic_map_resources(struct kvm *kvm)
{
	struct vgic_dist *dist = &kvm->arch.vgic;
	int ret = 0;

	mutex_lock(&kvm->lock);
	if (!irqchip_in_kernel(kvm))
		goto out;

	if (dist->vgic_model == KVM_DEV_TYPE_ARM_VGIC_V2)
		ret = vgic_v2_map_resources(kvm);
	else
		ret = vgic_v3_map_resources(kvm);

	if (ret)
		__kvm_vgic_destroy(kvm);

out:
	mutex_unlock(&kvm->lock);
	return ret;
}

/* GENERIC PROBE */

static int vgic_init_cpu_starting(unsigned int cpu)
{
	enable_percpu_irq(kvm_vgic_global_state.maint_irq, 0);
	return 0;
}


static int vgic_init_cpu_dying(unsigned int cpu)
{
	disable_percpu_irq(kvm_vgic_global_state.maint_irq);
	return 0;
}

static irqreturn_t vgic_maintenance_handler(int irq, void *data)
{
	/*
	 * We cannot rely on the vgic maintenance interrupt to be
	 * delivered synchronously. This means we can only use it to
	 * exit the VM, and we perform the handling of EOIed
	 * interrupts on the exit path (see vgic_fold_lr_state).
	 */
	return IRQ_HANDLED;
}

/**
 * kvm_vgic_init_cpu_hardware - initialize the GIC VE hardware
 *
 * For a specific CPU, initialize the GIC VE hardware.
 */
void kvm_vgic_init_cpu_hardware(void)
{
	BUG_ON(preemptible());

	/*
	 * We want to make sure the list registers start out clear so that we
	 * only have the program the used registers.
	 */
	if (kvm_vgic_global_state.type == VGIC_V2)
		vgic_v2_init_lrs();
	else
		kvm_call_hyp(__vgic_v3_init_lrs);
}

/**
 * kvm_vgic_hyp_init: populates the kvm_vgic_global_state variable
 * according to the host GIC model. Accordingly calls either
 * vgic_v2/v3_probe which registers the KVM_DEVICE that can be
 * instantiated by a guest later on .
 */
int kvm_vgic_hyp_init(void)
{
	const struct gic_kvm_info *gic_kvm_info;
	int ret;

	gic_kvm_info = gic_get_kvm_info();
	if (!gic_kvm_info)
		return -ENODEV;

	if (!gic_kvm_info->maint_irq) {
		kvm_err("No vgic maintenance irq\n");
		return -ENXIO;
	}

	switch (gic_kvm_info->type) {
	case GIC_V2:
		ret = vgic_v2_probe(gic_kvm_info);
		break;
	case GIC_V3:
		ret = vgic_v3_probe(gic_kvm_info);
		if (!ret) {
			static_branch_enable(&kvm_vgic_global_state.gicv3_cpuif);
			kvm_info("GIC system register CPU interface enabled\n");
		}
		break;
	default:
		ret = -ENODEV;
	};

	if (ret)
		return ret;

	kvm_vgic_global_state.maint_irq = gic_kvm_info->maint_irq;
	ret = request_percpu_irq(kvm_vgic_global_state.maint_irq,
				 vgic_maintenance_handler,
				 "vgic", kvm_get_running_vcpus());
	if (ret) {
		kvm_err("Cannot register interrupt %d\n",
			kvm_vgic_global_state.maint_irq);
		return ret;
	}

	ret = cpuhp_setup_state(CPUHP_AP_KVM_ARM_VGIC_INIT_STARTING,
				"kvm/arm/vgic:starting",
				vgic_init_cpu_starting, vgic_init_cpu_dying);
	if (ret) {
		kvm_err("Cannot register vgic CPU notifier\n");
		goto out_free_irq;
	}

	kvm_info("vgic interrupt IRQ%d\n", kvm_vgic_global_state.maint_irq);
	return 0;

out_free_irq:
	free_percpu_irq(kvm_vgic_global_state.maint_irq,
			kvm_get_running_vcpus());
	return ret;
}
