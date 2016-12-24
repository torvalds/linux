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
#include <asm/kvm_mmu.h>
#include "vgic.h"

/*
 * Initialization rules: there are multiple stages to the vgic
 * initialization, both for the distributor and the CPU interfaces.
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
 * - kvm_vgic_cpu_early_init(): initialization of static data that
 *   doesn't depend on any sizing information or emulation type. No
 *   allocation is allowed there.
 */

/* EARLY INIT */

/*
 * Those 2 functions should not be needed anymore but they
 * still are called from arm.c
 */
void kvm_vgic_early_init(struct kvm *kvm)
{
}

void kvm_vgic_vcpu_early_init(struct kvm_vcpu *vcpu)
{
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

	/*
	 * kvm_vgic_global_state.vctrl_base is set on vgic probe (kvm_arch_init)
	 * it is stored in distributor struct for asm save/restore purpose
	 */
	kvm->arch.vgic.vctrl_base = kvm_vgic_global_state.vctrl_base;

	kvm->arch.vgic.vgic_dist_base = VGIC_ADDR_UNDEF;
	kvm->arch.vgic.vgic_cpu_base = VGIC_ADDR_UNDEF;
	kvm->arch.vgic.vgic_redist_base = VGIC_ADDR_UNDEF;

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

	INIT_LIST_HEAD(&dist->lpi_list_head);
	spin_lock_init(&dist->lpi_list_lock);

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
		if (dist->vgic_model == KVM_DEV_TYPE_ARM_VGIC_V2)
			irq->targets = 0;
		else
			irq->mpidr = 0;
	}
	return 0;
}

/**
 * kvm_vgic_vcpu_init: initialize the vcpu data structures and
 * enable the VCPU interface
 * @vcpu: the VCPU which's VGIC should be initialized
 */
static void kvm_vgic_vcpu_init(struct kvm_vcpu *vcpu)
{
	struct vgic_cpu *vgic_cpu = &vcpu->arch.vgic_cpu;
	int i;

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
		irq->targets = 1U << vcpu->vcpu_id;
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
	int ret = 0, i;

	if (vgic_initialized(kvm))
		return 0;

	/* freeze the number of spis */
	if (!dist->nr_spis)
		dist->nr_spis = VGIC_NR_IRQS_LEGACY - VGIC_NR_PRIVATE_IRQS;

	ret = kvm_vgic_dist_init(kvm, dist->nr_spis);
	if (ret)
		goto out;

	if (vgic_has_its(kvm))
		dist->msis_require_devid = true;

	kvm_for_each_vcpu(i, vcpu, kvm)
		kvm_vgic_vcpu_init(vcpu);

	ret = kvm_vgic_setup_default_irq_routing(kvm);
	if (ret)
		goto out;

	dist->initialized = true;
out:
	return ret;
}

static void kvm_vgic_dist_destroy(struct kvm *kvm)
{
	struct vgic_dist *dist = &kvm->arch.vgic;

	mutex_lock(&kvm->lock);

	dist->ready = false;
	dist->initialized = false;

	kfree(dist->spis);
	dist->nr_spis = 0;

	mutex_unlock(&kvm->lock);
}

void kvm_vgic_vcpu_destroy(struct kvm_vcpu *vcpu)
{
	struct vgic_cpu *vgic_cpu = &vcpu->arch.vgic_cpu;

	INIT_LIST_HEAD(&vgic_cpu->ap_list_head);
}

void kvm_vgic_destroy(struct kvm *kvm)
{
	struct kvm_vcpu *vcpu;
	int i;

	kvm_vgic_dist_destroy(kvm);

	kvm_for_each_vcpu(i, vcpu, kvm)
		kvm_vgic_vcpu_destroy(vcpu);
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
	 * interrupts on the exit path (see vgic_process_maintenance).
	 */
	return IRQ_HANDLED;
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
				"AP_KVM_ARM_VGIC_INIT_STARTING",
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
