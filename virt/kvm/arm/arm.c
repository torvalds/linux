// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Author: Christoffer Dall <c.dall@virtualopensystems.com>
 */

#include <linux/bug.h>
#include <linux/cpu_pm.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/kvm_host.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/mman.h>
#include <linux/sched.h>
#include <linux/kvm.h>
#include <linux/kvm_irqfd.h>
#include <linux/irqbypass.h>
#include <linux/sched/stat.h>
#include <trace/events/kvm.h>
#include <kvm/arm_pmu.h>
#include <kvm/arm_psci.h>

#define CREATE_TRACE_POINTS
#include "trace.h"

#include <linux/uaccess.h>
#include <asm/ptrace.h>
#include <asm/mman.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>
#include <asm/cpufeature.h>
#include <asm/virt.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_mmu.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_coproc.h>
#include <asm/sections.h>

#ifdef REQUIRES_VIRT
__asm__(".arch_extension	virt");
#endif

DEFINE_PER_CPU(kvm_host_data_t, kvm_host_data);
static DEFINE_PER_CPU(unsigned long, kvm_arm_hyp_stack_page);

/* Per-CPU variable containing the currently running vcpu. */
static DEFINE_PER_CPU(struct kvm_vcpu *, kvm_arm_running_vcpu);

/* The VMID used in the VTTBR */
static atomic64_t kvm_vmid_gen = ATOMIC64_INIT(1);
static u32 kvm_next_vmid;
static DEFINE_SPINLOCK(kvm_vmid_lock);

static bool vgic_present;

static DEFINE_PER_CPU(unsigned char, kvm_arm_hardware_enabled);

static void kvm_arm_set_running_vcpu(struct kvm_vcpu *vcpu)
{
	__this_cpu_write(kvm_arm_running_vcpu, vcpu);
}

DEFINE_STATIC_KEY_FALSE(userspace_irqchip_in_use);

/**
 * kvm_arm_get_running_vcpu - get the vcpu running on the current CPU.
 * Must be called from non-preemptible context
 */
struct kvm_vcpu *kvm_arm_get_running_vcpu(void)
{
	return __this_cpu_read(kvm_arm_running_vcpu);
}

/**
 * kvm_arm_get_running_vcpus - get the per-CPU array of currently running vcpus.
 */
struct kvm_vcpu * __percpu *kvm_get_running_vcpus(void)
{
	return &kvm_arm_running_vcpu;
}

int kvm_arch_vcpu_should_kick(struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_exiting_guest_mode(vcpu) == IN_GUEST_MODE;
}

int kvm_arch_hardware_setup(void)
{
	return 0;
}

void kvm_arch_check_processor_compat(void *rtn)
{
	*(int *)rtn = 0;
}


/**
 * kvm_arch_init_vm - initializes a VM data structure
 * @kvm:	pointer to the KVM struct
 */
int kvm_arch_init_vm(struct kvm *kvm, unsigned long type)
{
	int ret, cpu;

	ret = kvm_arm_setup_stage2(kvm, type);
	if (ret)
		return ret;

	kvm->arch.last_vcpu_ran = alloc_percpu(typeof(*kvm->arch.last_vcpu_ran));
	if (!kvm->arch.last_vcpu_ran)
		return -ENOMEM;

	for_each_possible_cpu(cpu)
		*per_cpu_ptr(kvm->arch.last_vcpu_ran, cpu) = -1;

	ret = kvm_alloc_stage2_pgd(kvm);
	if (ret)
		goto out_fail_alloc;

	ret = create_hyp_mappings(kvm, kvm + 1, PAGE_HYP);
	if (ret)
		goto out_free_stage2_pgd;

	kvm_vgic_early_init(kvm);

	/* Mark the initial VMID generation invalid */
	kvm->arch.vmid.vmid_gen = 0;

	/* The maximum number of VCPUs is limited by the host's GIC model */
	kvm->arch.max_vcpus = vgic_present ?
				kvm_vgic_get_max_vcpus() : KVM_MAX_VCPUS;

	return ret;
out_free_stage2_pgd:
	kvm_free_stage2_pgd(kvm);
out_fail_alloc:
	free_percpu(kvm->arch.last_vcpu_ran);
	kvm->arch.last_vcpu_ran = NULL;
	return ret;
}

bool kvm_arch_has_vcpu_debugfs(void)
{
	return false;
}

int kvm_arch_create_vcpu_debugfs(struct kvm_vcpu *vcpu)
{
	return 0;
}

vm_fault_t kvm_arch_vcpu_fault(struct kvm_vcpu *vcpu, struct vm_fault *vmf)
{
	return VM_FAULT_SIGBUS;
}


/**
 * kvm_arch_destroy_vm - destroy the VM data structure
 * @kvm:	pointer to the KVM struct
 */
void kvm_arch_destroy_vm(struct kvm *kvm)
{
	int i;

	kvm_vgic_destroy(kvm);

	free_percpu(kvm->arch.last_vcpu_ran);
	kvm->arch.last_vcpu_ran = NULL;

	for (i = 0; i < KVM_MAX_VCPUS; ++i) {
		if (kvm->vcpus[i]) {
			kvm_arch_vcpu_free(kvm->vcpus[i]);
			kvm->vcpus[i] = NULL;
		}
	}
	atomic_set(&kvm->online_vcpus, 0);
}

int kvm_vm_ioctl_check_extension(struct kvm *kvm, long ext)
{
	int r;
	switch (ext) {
	case KVM_CAP_IRQCHIP:
		r = vgic_present;
		break;
	case KVM_CAP_IOEVENTFD:
	case KVM_CAP_DEVICE_CTRL:
	case KVM_CAP_USER_MEMORY:
	case KVM_CAP_SYNC_MMU:
	case KVM_CAP_DESTROY_MEMORY_REGION_WORKS:
	case KVM_CAP_ONE_REG:
	case KVM_CAP_ARM_PSCI:
	case KVM_CAP_ARM_PSCI_0_2:
	case KVM_CAP_READONLY_MEM:
	case KVM_CAP_MP_STATE:
	case KVM_CAP_IMMEDIATE_EXIT:
	case KVM_CAP_VCPU_EVENTS:
		r = 1;
		break;
	case KVM_CAP_ARM_SET_DEVICE_ADDR:
		r = 1;
		break;
	case KVM_CAP_NR_VCPUS:
		r = num_online_cpus();
		break;
	case KVM_CAP_MAX_VCPUS:
		r = KVM_MAX_VCPUS;
		break;
	case KVM_CAP_MAX_VCPU_ID:
		r = KVM_MAX_VCPU_ID;
		break;
	case KVM_CAP_MSI_DEVID:
		if (!kvm)
			r = -EINVAL;
		else
			r = kvm->arch.vgic.msis_require_devid;
		break;
	case KVM_CAP_ARM_USER_IRQ:
		/*
		 * 1: EL1_VTIMER, EL1_PTIMER, and PMU.
		 * (bump this number if adding more devices)
		 */
		r = 1;
		break;
	default:
		r = kvm_arch_vm_ioctl_check_extension(kvm, ext);
		break;
	}
	return r;
}

long kvm_arch_dev_ioctl(struct file *filp,
			unsigned int ioctl, unsigned long arg)
{
	return -EINVAL;
}

struct kvm *kvm_arch_alloc_vm(void)
{
	if (!has_vhe())
		return kzalloc(sizeof(struct kvm), GFP_KERNEL);

	return vzalloc(sizeof(struct kvm));
}

void kvm_arch_free_vm(struct kvm *kvm)
{
	if (!has_vhe())
		kfree(kvm);
	else
		vfree(kvm);
}

struct kvm_vcpu *kvm_arch_vcpu_create(struct kvm *kvm, unsigned int id)
{
	int err;
	struct kvm_vcpu *vcpu;

	if (irqchip_in_kernel(kvm) && vgic_initialized(kvm)) {
		err = -EBUSY;
		goto out;
	}

	if (id >= kvm->arch.max_vcpus) {
		err = -EINVAL;
		goto out;
	}

	vcpu = kmem_cache_zalloc(kvm_vcpu_cache, GFP_KERNEL);
	if (!vcpu) {
		err = -ENOMEM;
		goto out;
	}

	err = kvm_vcpu_init(vcpu, kvm, id);
	if (err)
		goto free_vcpu;

	err = create_hyp_mappings(vcpu, vcpu + 1, PAGE_HYP);
	if (err)
		goto vcpu_uninit;

	return vcpu;
vcpu_uninit:
	kvm_vcpu_uninit(vcpu);
free_vcpu:
	kmem_cache_free(kvm_vcpu_cache, vcpu);
out:
	return ERR_PTR(err);
}

void kvm_arch_vcpu_postcreate(struct kvm_vcpu *vcpu)
{
}

void kvm_arch_vcpu_free(struct kvm_vcpu *vcpu)
{
	if (vcpu->arch.has_run_once && unlikely(!irqchip_in_kernel(vcpu->kvm)))
		static_branch_dec(&userspace_irqchip_in_use);

	kvm_mmu_free_memory_caches(vcpu);
	kvm_timer_vcpu_terminate(vcpu);
	kvm_pmu_vcpu_destroy(vcpu);
	kvm_vcpu_uninit(vcpu);
	kmem_cache_free(kvm_vcpu_cache, vcpu);
}

void kvm_arch_vcpu_destroy(struct kvm_vcpu *vcpu)
{
	kvm_arch_vcpu_free(vcpu);
}

int kvm_cpu_has_pending_timer(struct kvm_vcpu *vcpu)
{
	return kvm_timer_is_pending(vcpu);
}

void kvm_arch_vcpu_blocking(struct kvm_vcpu *vcpu)
{
	/*
	 * If we're about to block (most likely because we've just hit a
	 * WFI), we need to sync back the state of the GIC CPU interface
	 * so that we have the lastest PMR and group enables. This ensures
	 * that kvm_arch_vcpu_runnable has up-to-date data to decide
	 * whether we have pending interrupts.
	 */
	preempt_disable();
	kvm_vgic_vmcr_sync(vcpu);
	preempt_enable();

	kvm_vgic_v4_enable_doorbell(vcpu);
}

void kvm_arch_vcpu_unblocking(struct kvm_vcpu *vcpu)
{
	kvm_vgic_v4_disable_doorbell(vcpu);
}

int kvm_arch_vcpu_init(struct kvm_vcpu *vcpu)
{
	/* Force users to call KVM_ARM_VCPU_INIT */
	vcpu->arch.target = -1;
	bitmap_zero(vcpu->arch.features, KVM_VCPU_MAX_FEATURES);

	/* Set up the timer */
	kvm_timer_vcpu_init(vcpu);

	kvm_arm_reset_debug_ptr(vcpu);

	return kvm_vgic_vcpu_init(vcpu);
}

void kvm_arch_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	int *last_ran;
	kvm_host_data_t *cpu_data;

	last_ran = this_cpu_ptr(vcpu->kvm->arch.last_vcpu_ran);
	cpu_data = this_cpu_ptr(&kvm_host_data);

	/*
	 * We might get preempted before the vCPU actually runs, but
	 * over-invalidation doesn't affect correctness.
	 */
	if (*last_ran != vcpu->vcpu_id) {
		kvm_call_hyp(__kvm_tlb_flush_local_vmid, vcpu);
		*last_ran = vcpu->vcpu_id;
	}

	vcpu->cpu = cpu;
	vcpu->arch.host_cpu_context = &cpu_data->host_ctxt;

	kvm_arm_set_running_vcpu(vcpu);
	kvm_vgic_load(vcpu);
	kvm_timer_vcpu_load(vcpu);
	kvm_vcpu_load_sysregs(vcpu);
	kvm_arch_vcpu_load_fp(vcpu);
	kvm_vcpu_pmu_restore_guest(vcpu);

	if (single_task_running())
		vcpu_clear_wfe_traps(vcpu);
	else
		vcpu_set_wfe_traps(vcpu);

	vcpu_ptrauth_setup_lazy(vcpu);
}

void kvm_arch_vcpu_put(struct kvm_vcpu *vcpu)
{
	kvm_arch_vcpu_put_fp(vcpu);
	kvm_vcpu_put_sysregs(vcpu);
	kvm_timer_vcpu_put(vcpu);
	kvm_vgic_put(vcpu);
	kvm_vcpu_pmu_restore_host(vcpu);

	vcpu->cpu = -1;

	kvm_arm_set_running_vcpu(NULL);
}

static void vcpu_power_off(struct kvm_vcpu *vcpu)
{
	vcpu->arch.power_off = true;
	kvm_make_request(KVM_REQ_SLEEP, vcpu);
	kvm_vcpu_kick(vcpu);
}

int kvm_arch_vcpu_ioctl_get_mpstate(struct kvm_vcpu *vcpu,
				    struct kvm_mp_state *mp_state)
{
	if (vcpu->arch.power_off)
		mp_state->mp_state = KVM_MP_STATE_STOPPED;
	else
		mp_state->mp_state = KVM_MP_STATE_RUNNABLE;

	return 0;
}

int kvm_arch_vcpu_ioctl_set_mpstate(struct kvm_vcpu *vcpu,
				    struct kvm_mp_state *mp_state)
{
	int ret = 0;

	switch (mp_state->mp_state) {
	case KVM_MP_STATE_RUNNABLE:
		vcpu->arch.power_off = false;
		break;
	case KVM_MP_STATE_STOPPED:
		vcpu_power_off(vcpu);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

/**
 * kvm_arch_vcpu_runnable - determine if the vcpu can be scheduled
 * @v:		The VCPU pointer
 *
 * If the guest CPU is not waiting for interrupts or an interrupt line is
 * asserted, the CPU is by definition runnable.
 */
int kvm_arch_vcpu_runnable(struct kvm_vcpu *v)
{
	bool irq_lines = *vcpu_hcr(v) & (HCR_VI | HCR_VF);
	return ((irq_lines || kvm_vgic_vcpu_pending_irq(v))
		&& !v->arch.power_off && !v->arch.pause);
}

bool kvm_arch_vcpu_in_kernel(struct kvm_vcpu *vcpu)
{
	return vcpu_mode_priv(vcpu);
}

/* Just ensure a guest exit from a particular CPU */
static void exit_vm_noop(void *info)
{
}

void force_vm_exit(const cpumask_t *mask)
{
	preempt_disable();
	smp_call_function_many(mask, exit_vm_noop, NULL, true);
	preempt_enable();
}

/**
 * need_new_vmid_gen - check that the VMID is still valid
 * @vmid: The VMID to check
 *
 * return true if there is a new generation of VMIDs being used
 *
 * The hardware supports a limited set of values with the value zero reserved
 * for the host, so we check if an assigned value belongs to a previous
 * generation, which which requires us to assign a new value. If we're the
 * first to use a VMID for the new generation, we must flush necessary caches
 * and TLBs on all CPUs.
 */
static bool need_new_vmid_gen(struct kvm_vmid *vmid)
{
	u64 current_vmid_gen = atomic64_read(&kvm_vmid_gen);
	smp_rmb(); /* Orders read of kvm_vmid_gen and kvm->arch.vmid */
	return unlikely(READ_ONCE(vmid->vmid_gen) != current_vmid_gen);
}

/**
 * update_vmid - Update the vmid with a valid VMID for the current generation
 * @kvm: The guest that struct vmid belongs to
 * @vmid: The stage-2 VMID information struct
 */
static void update_vmid(struct kvm_vmid *vmid)
{
	if (!need_new_vmid_gen(vmid))
		return;

	spin_lock(&kvm_vmid_lock);

	/*
	 * We need to re-check the vmid_gen here to ensure that if another vcpu
	 * already allocated a valid vmid for this vm, then this vcpu should
	 * use the same vmid.
	 */
	if (!need_new_vmid_gen(vmid)) {
		spin_unlock(&kvm_vmid_lock);
		return;
	}

	/* First user of a new VMID generation? */
	if (unlikely(kvm_next_vmid == 0)) {
		atomic64_inc(&kvm_vmid_gen);
		kvm_next_vmid = 1;

		/*
		 * On SMP we know no other CPUs can use this CPU's or each
		 * other's VMID after force_vm_exit returns since the
		 * kvm_vmid_lock blocks them from reentry to the guest.
		 */
		force_vm_exit(cpu_all_mask);
		/*
		 * Now broadcast TLB + ICACHE invalidation over the inner
		 * shareable domain to make sure all data structures are
		 * clean.
		 */
		kvm_call_hyp(__kvm_flush_vm_context);
	}

	vmid->vmid = kvm_next_vmid;
	kvm_next_vmid++;
	kvm_next_vmid &= (1 << kvm_get_vmid_bits()) - 1;

	smp_wmb();
	WRITE_ONCE(vmid->vmid_gen, atomic64_read(&kvm_vmid_gen));

	spin_unlock(&kvm_vmid_lock);
}

static int kvm_vcpu_first_run_init(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;
	int ret = 0;

	if (likely(vcpu->arch.has_run_once))
		return 0;

	if (!kvm_arm_vcpu_is_finalized(vcpu))
		return -EPERM;

	vcpu->arch.has_run_once = true;

	if (likely(irqchip_in_kernel(kvm))) {
		/*
		 * Map the VGIC hardware resources before running a vcpu the
		 * first time on this VM.
		 */
		if (unlikely(!vgic_ready(kvm))) {
			ret = kvm_vgic_map_resources(kvm);
			if (ret)
				return ret;
		}
	} else {
		/*
		 * Tell the rest of the code that there are userspace irqchip
		 * VMs in the wild.
		 */
		static_branch_inc(&userspace_irqchip_in_use);
	}

	ret = kvm_timer_enable(vcpu);
	if (ret)
		return ret;

	ret = kvm_arm_pmu_v3_enable(vcpu);

	return ret;
}

bool kvm_arch_intc_initialized(struct kvm *kvm)
{
	return vgic_initialized(kvm);
}

void kvm_arm_halt_guest(struct kvm *kvm)
{
	int i;
	struct kvm_vcpu *vcpu;

	kvm_for_each_vcpu(i, vcpu, kvm)
		vcpu->arch.pause = true;
	kvm_make_all_cpus_request(kvm, KVM_REQ_SLEEP);
}

void kvm_arm_resume_guest(struct kvm *kvm)
{
	int i;
	struct kvm_vcpu *vcpu;

	kvm_for_each_vcpu(i, vcpu, kvm) {
		vcpu->arch.pause = false;
		swake_up_one(kvm_arch_vcpu_wq(vcpu));
	}
}

static void vcpu_req_sleep(struct kvm_vcpu *vcpu)
{
	struct swait_queue_head *wq = kvm_arch_vcpu_wq(vcpu);

	swait_event_interruptible_exclusive(*wq, ((!vcpu->arch.power_off) &&
				       (!vcpu->arch.pause)));

	if (vcpu->arch.power_off || vcpu->arch.pause) {
		/* Awaken to handle a signal, request we sleep again later. */
		kvm_make_request(KVM_REQ_SLEEP, vcpu);
	}

	/*
	 * Make sure we will observe a potential reset request if we've
	 * observed a change to the power state. Pairs with the smp_wmb() in
	 * kvm_psci_vcpu_on().
	 */
	smp_rmb();
}

static int kvm_vcpu_initialized(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.target >= 0;
}

static void check_vcpu_requests(struct kvm_vcpu *vcpu)
{
	if (kvm_request_pending(vcpu)) {
		if (kvm_check_request(KVM_REQ_SLEEP, vcpu))
			vcpu_req_sleep(vcpu);

		if (kvm_check_request(KVM_REQ_VCPU_RESET, vcpu))
			kvm_reset_vcpu(vcpu);

		/*
		 * Clear IRQ_PENDING requests that were made to guarantee
		 * that a VCPU sees new virtual interrupts.
		 */
		kvm_check_request(KVM_REQ_IRQ_PENDING, vcpu);
	}
}

/**
 * kvm_arch_vcpu_ioctl_run - the main VCPU run function to execute guest code
 * @vcpu:	The VCPU pointer
 * @run:	The kvm_run structure pointer used for userspace state exchange
 *
 * This function is called through the VCPU_RUN ioctl called from user space. It
 * will execute VM code in a loop until the time slice for the process is used
 * or some emulation is needed from user space in which case the function will
 * return with return value 0 and with the kvm_run structure filled in with the
 * required data for the requested emulation.
 */
int kvm_arch_vcpu_ioctl_run(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	int ret;

	if (unlikely(!kvm_vcpu_initialized(vcpu)))
		return -ENOEXEC;

	ret = kvm_vcpu_first_run_init(vcpu);
	if (ret)
		return ret;

	if (run->exit_reason == KVM_EXIT_MMIO) {
		ret = kvm_handle_mmio_return(vcpu, vcpu->run);
		if (ret)
			return ret;
	}

	if (run->immediate_exit)
		return -EINTR;

	vcpu_load(vcpu);

	kvm_sigset_activate(vcpu);

	ret = 1;
	run->exit_reason = KVM_EXIT_UNKNOWN;
	while (ret > 0) {
		/*
		 * Check conditions before entering the guest
		 */
		cond_resched();

		update_vmid(&vcpu->kvm->arch.vmid);

		check_vcpu_requests(vcpu);

		/*
		 * Preparing the interrupts to be injected also
		 * involves poking the GIC, which must be done in a
		 * non-preemptible context.
		 */
		preempt_disable();

		kvm_pmu_flush_hwstate(vcpu);

		local_irq_disable();

		kvm_vgic_flush_hwstate(vcpu);

		/*
		 * Exit if we have a signal pending so that we can deliver the
		 * signal to user space.
		 */
		if (signal_pending(current)) {
			ret = -EINTR;
			run->exit_reason = KVM_EXIT_INTR;
		}

		/*
		 * If we're using a userspace irqchip, then check if we need
		 * to tell a userspace irqchip about timer or PMU level
		 * changes and if so, exit to userspace (the actual level
		 * state gets updated in kvm_timer_update_run and
		 * kvm_pmu_update_run below).
		 */
		if (static_branch_unlikely(&userspace_irqchip_in_use)) {
			if (kvm_timer_should_notify_user(vcpu) ||
			    kvm_pmu_should_notify_user(vcpu)) {
				ret = -EINTR;
				run->exit_reason = KVM_EXIT_INTR;
			}
		}

		/*
		 * Ensure we set mode to IN_GUEST_MODE after we disable
		 * interrupts and before the final VCPU requests check.
		 * See the comment in kvm_vcpu_exiting_guest_mode() and
		 * Documentation/virtual/kvm/vcpu-requests.rst
		 */
		smp_store_mb(vcpu->mode, IN_GUEST_MODE);

		if (ret <= 0 || need_new_vmid_gen(&vcpu->kvm->arch.vmid) ||
		    kvm_request_pending(vcpu)) {
			vcpu->mode = OUTSIDE_GUEST_MODE;
			isb(); /* Ensure work in x_flush_hwstate is committed */
			kvm_pmu_sync_hwstate(vcpu);
			if (static_branch_unlikely(&userspace_irqchip_in_use))
				kvm_timer_sync_hwstate(vcpu);
			kvm_vgic_sync_hwstate(vcpu);
			local_irq_enable();
			preempt_enable();
			continue;
		}

		kvm_arm_setup_debug(vcpu);

		/**************************************************************
		 * Enter the guest
		 */
		trace_kvm_entry(*vcpu_pc(vcpu));
		guest_enter_irqoff();

		if (has_vhe()) {
			kvm_arm_vhe_guest_enter();
			ret = kvm_vcpu_run_vhe(vcpu);
			kvm_arm_vhe_guest_exit();
		} else {
			ret = kvm_call_hyp_ret(__kvm_vcpu_run_nvhe, vcpu);
		}

		vcpu->mode = OUTSIDE_GUEST_MODE;
		vcpu->stat.exits++;
		/*
		 * Back from guest
		 *************************************************************/

		kvm_arm_clear_debug(vcpu);

		/*
		 * We must sync the PMU state before the vgic state so
		 * that the vgic can properly sample the updated state of the
		 * interrupt line.
		 */
		kvm_pmu_sync_hwstate(vcpu);

		/*
		 * Sync the vgic state before syncing the timer state because
		 * the timer code needs to know if the virtual timer
		 * interrupts are active.
		 */
		kvm_vgic_sync_hwstate(vcpu);

		/*
		 * Sync the timer hardware state before enabling interrupts as
		 * we don't want vtimer interrupts to race with syncing the
		 * timer virtual interrupt state.
		 */
		if (static_branch_unlikely(&userspace_irqchip_in_use))
			kvm_timer_sync_hwstate(vcpu);

		kvm_arch_vcpu_ctxsync_fp(vcpu);

		/*
		 * We may have taken a host interrupt in HYP mode (ie
		 * while executing the guest). This interrupt is still
		 * pending, as we haven't serviced it yet!
		 *
		 * We're now back in SVC mode, with interrupts
		 * disabled.  Enabling the interrupts now will have
		 * the effect of taking the interrupt again, in SVC
		 * mode this time.
		 */
		local_irq_enable();

		/*
		 * We do local_irq_enable() before calling guest_exit() so
		 * that if a timer interrupt hits while running the guest we
		 * account that tick as being spent in the guest.  We enable
		 * preemption after calling guest_exit() so that if we get
		 * preempted we make sure ticks after that is not counted as
		 * guest time.
		 */
		guest_exit();
		trace_kvm_exit(ret, kvm_vcpu_trap_get_class(vcpu), *vcpu_pc(vcpu));

		/* Exit types that need handling before we can be preempted */
		handle_exit_early(vcpu, run, ret);

		preempt_enable();

		ret = handle_exit(vcpu, run, ret);
	}

	/* Tell userspace about in-kernel device output levels */
	if (unlikely(!irqchip_in_kernel(vcpu->kvm))) {
		kvm_timer_update_run(vcpu);
		kvm_pmu_update_run(vcpu);
	}

	kvm_sigset_deactivate(vcpu);

	vcpu_put(vcpu);
	return ret;
}

static int vcpu_interrupt_line(struct kvm_vcpu *vcpu, int number, bool level)
{
	int bit_index;
	bool set;
	unsigned long *hcr;

	if (number == KVM_ARM_IRQ_CPU_IRQ)
		bit_index = __ffs(HCR_VI);
	else /* KVM_ARM_IRQ_CPU_FIQ */
		bit_index = __ffs(HCR_VF);

	hcr = vcpu_hcr(vcpu);
	if (level)
		set = test_and_set_bit(bit_index, hcr);
	else
		set = test_and_clear_bit(bit_index, hcr);

	/*
	 * If we didn't change anything, no need to wake up or kick other CPUs
	 */
	if (set == level)
		return 0;

	/*
	 * The vcpu irq_lines field was updated, wake up sleeping VCPUs and
	 * trigger a world-switch round on the running physical CPU to set the
	 * virtual IRQ/FIQ fields in the HCR appropriately.
	 */
	kvm_make_request(KVM_REQ_IRQ_PENDING, vcpu);
	kvm_vcpu_kick(vcpu);

	return 0;
}

int kvm_vm_ioctl_irq_line(struct kvm *kvm, struct kvm_irq_level *irq_level,
			  bool line_status)
{
	u32 irq = irq_level->irq;
	unsigned int irq_type, vcpu_idx, irq_num;
	int nrcpus = atomic_read(&kvm->online_vcpus);
	struct kvm_vcpu *vcpu = NULL;
	bool level = irq_level->level;

	irq_type = (irq >> KVM_ARM_IRQ_TYPE_SHIFT) & KVM_ARM_IRQ_TYPE_MASK;
	vcpu_idx = (irq >> KVM_ARM_IRQ_VCPU_SHIFT) & KVM_ARM_IRQ_VCPU_MASK;
	irq_num = (irq >> KVM_ARM_IRQ_NUM_SHIFT) & KVM_ARM_IRQ_NUM_MASK;

	trace_kvm_irq_line(irq_type, vcpu_idx, irq_num, irq_level->level);

	switch (irq_type) {
	case KVM_ARM_IRQ_TYPE_CPU:
		if (irqchip_in_kernel(kvm))
			return -ENXIO;

		if (vcpu_idx >= nrcpus)
			return -EINVAL;

		vcpu = kvm_get_vcpu(kvm, vcpu_idx);
		if (!vcpu)
			return -EINVAL;

		if (irq_num > KVM_ARM_IRQ_CPU_FIQ)
			return -EINVAL;

		return vcpu_interrupt_line(vcpu, irq_num, level);
	case KVM_ARM_IRQ_TYPE_PPI:
		if (!irqchip_in_kernel(kvm))
			return -ENXIO;

		if (vcpu_idx >= nrcpus)
			return -EINVAL;

		vcpu = kvm_get_vcpu(kvm, vcpu_idx);
		if (!vcpu)
			return -EINVAL;

		if (irq_num < VGIC_NR_SGIS || irq_num >= VGIC_NR_PRIVATE_IRQS)
			return -EINVAL;

		return kvm_vgic_inject_irq(kvm, vcpu->vcpu_id, irq_num, level, NULL);
	case KVM_ARM_IRQ_TYPE_SPI:
		if (!irqchip_in_kernel(kvm))
			return -ENXIO;

		if (irq_num < VGIC_NR_PRIVATE_IRQS)
			return -EINVAL;

		return kvm_vgic_inject_irq(kvm, 0, irq_num, level, NULL);
	}

	return -EINVAL;
}

static int kvm_vcpu_set_target(struct kvm_vcpu *vcpu,
			       const struct kvm_vcpu_init *init)
{
	unsigned int i, ret;
	int phys_target = kvm_target_cpu();

	if (init->target != phys_target)
		return -EINVAL;

	/*
	 * Secondary and subsequent calls to KVM_ARM_VCPU_INIT must
	 * use the same target.
	 */
	if (vcpu->arch.target != -1 && vcpu->arch.target != init->target)
		return -EINVAL;

	/* -ENOENT for unknown features, -EINVAL for invalid combinations. */
	for (i = 0; i < sizeof(init->features) * 8; i++) {
		bool set = (init->features[i / 32] & (1 << (i % 32)));

		if (set && i >= KVM_VCPU_MAX_FEATURES)
			return -ENOENT;

		/*
		 * Secondary and subsequent calls to KVM_ARM_VCPU_INIT must
		 * use the same feature set.
		 */
		if (vcpu->arch.target != -1 && i < KVM_VCPU_MAX_FEATURES &&
		    test_bit(i, vcpu->arch.features) != set)
			return -EINVAL;

		if (set)
			set_bit(i, vcpu->arch.features);
	}

	vcpu->arch.target = phys_target;

	/* Now we know what it is, we can reset it. */
	ret = kvm_reset_vcpu(vcpu);
	if (ret) {
		vcpu->arch.target = -1;
		bitmap_zero(vcpu->arch.features, KVM_VCPU_MAX_FEATURES);
	}

	return ret;
}

static int kvm_arch_vcpu_ioctl_vcpu_init(struct kvm_vcpu *vcpu,
					 struct kvm_vcpu_init *init)
{
	int ret;

	ret = kvm_vcpu_set_target(vcpu, init);
	if (ret)
		return ret;

	/*
	 * Ensure a rebooted VM will fault in RAM pages and detect if the
	 * guest MMU is turned off and flush the caches as needed.
	 */
	if (vcpu->arch.has_run_once)
		stage2_unmap_vm(vcpu->kvm);

	vcpu_reset_hcr(vcpu);

	/*
	 * Handle the "start in power-off" case.
	 */
	if (test_bit(KVM_ARM_VCPU_POWER_OFF, vcpu->arch.features))
		vcpu_power_off(vcpu);
	else
		vcpu->arch.power_off = false;

	return 0;
}

static int kvm_arm_vcpu_set_attr(struct kvm_vcpu *vcpu,
				 struct kvm_device_attr *attr)
{
	int ret = -ENXIO;

	switch (attr->group) {
	default:
		ret = kvm_arm_vcpu_arch_set_attr(vcpu, attr);
		break;
	}

	return ret;
}

static int kvm_arm_vcpu_get_attr(struct kvm_vcpu *vcpu,
				 struct kvm_device_attr *attr)
{
	int ret = -ENXIO;

	switch (attr->group) {
	default:
		ret = kvm_arm_vcpu_arch_get_attr(vcpu, attr);
		break;
	}

	return ret;
}

static int kvm_arm_vcpu_has_attr(struct kvm_vcpu *vcpu,
				 struct kvm_device_attr *attr)
{
	int ret = -ENXIO;

	switch (attr->group) {
	default:
		ret = kvm_arm_vcpu_arch_has_attr(vcpu, attr);
		break;
	}

	return ret;
}

static int kvm_arm_vcpu_get_events(struct kvm_vcpu *vcpu,
				   struct kvm_vcpu_events *events)
{
	memset(events, 0, sizeof(*events));

	return __kvm_arm_vcpu_get_events(vcpu, events);
}

static int kvm_arm_vcpu_set_events(struct kvm_vcpu *vcpu,
				   struct kvm_vcpu_events *events)
{
	int i;

	/* check whether the reserved field is zero */
	for (i = 0; i < ARRAY_SIZE(events->reserved); i++)
		if (events->reserved[i])
			return -EINVAL;

	/* check whether the pad field is zero */
	for (i = 0; i < ARRAY_SIZE(events->exception.pad); i++)
		if (events->exception.pad[i])
			return -EINVAL;

	return __kvm_arm_vcpu_set_events(vcpu, events);
}

long kvm_arch_vcpu_ioctl(struct file *filp,
			 unsigned int ioctl, unsigned long arg)
{
	struct kvm_vcpu *vcpu = filp->private_data;
	void __user *argp = (void __user *)arg;
	struct kvm_device_attr attr;
	long r;

	switch (ioctl) {
	case KVM_ARM_VCPU_INIT: {
		struct kvm_vcpu_init init;

		r = -EFAULT;
		if (copy_from_user(&init, argp, sizeof(init)))
			break;

		r = kvm_arch_vcpu_ioctl_vcpu_init(vcpu, &init);
		break;
	}
	case KVM_SET_ONE_REG:
	case KVM_GET_ONE_REG: {
		struct kvm_one_reg reg;

		r = -ENOEXEC;
		if (unlikely(!kvm_vcpu_initialized(vcpu)))
			break;

		r = -EFAULT;
		if (copy_from_user(&reg, argp, sizeof(reg)))
			break;

		if (ioctl == KVM_SET_ONE_REG)
			r = kvm_arm_set_reg(vcpu, &reg);
		else
			r = kvm_arm_get_reg(vcpu, &reg);
		break;
	}
	case KVM_GET_REG_LIST: {
		struct kvm_reg_list __user *user_list = argp;
		struct kvm_reg_list reg_list;
		unsigned n;

		r = -ENOEXEC;
		if (unlikely(!kvm_vcpu_initialized(vcpu)))
			break;

		r = -EPERM;
		if (!kvm_arm_vcpu_is_finalized(vcpu))
			break;

		r = -EFAULT;
		if (copy_from_user(&reg_list, user_list, sizeof(reg_list)))
			break;
		n = reg_list.n;
		reg_list.n = kvm_arm_num_regs(vcpu);
		if (copy_to_user(user_list, &reg_list, sizeof(reg_list)))
			break;
		r = -E2BIG;
		if (n < reg_list.n)
			break;
		r = kvm_arm_copy_reg_indices(vcpu, user_list->reg);
		break;
	}
	case KVM_SET_DEVICE_ATTR: {
		r = -EFAULT;
		if (copy_from_user(&attr, argp, sizeof(attr)))
			break;
		r = kvm_arm_vcpu_set_attr(vcpu, &attr);
		break;
	}
	case KVM_GET_DEVICE_ATTR: {
		r = -EFAULT;
		if (copy_from_user(&attr, argp, sizeof(attr)))
			break;
		r = kvm_arm_vcpu_get_attr(vcpu, &attr);
		break;
	}
	case KVM_HAS_DEVICE_ATTR: {
		r = -EFAULT;
		if (copy_from_user(&attr, argp, sizeof(attr)))
			break;
		r = kvm_arm_vcpu_has_attr(vcpu, &attr);
		break;
	}
	case KVM_GET_VCPU_EVENTS: {
		struct kvm_vcpu_events events;

		if (kvm_arm_vcpu_get_events(vcpu, &events))
			return -EINVAL;

		if (copy_to_user(argp, &events, sizeof(events)))
			return -EFAULT;

		return 0;
	}
	case KVM_SET_VCPU_EVENTS: {
		struct kvm_vcpu_events events;

		if (copy_from_user(&events, argp, sizeof(events)))
			return -EFAULT;

		return kvm_arm_vcpu_set_events(vcpu, &events);
	}
	case KVM_ARM_VCPU_FINALIZE: {
		int what;

		if (!kvm_vcpu_initialized(vcpu))
			return -ENOEXEC;

		if (get_user(what, (const int __user *)argp))
			return -EFAULT;

		return kvm_arm_vcpu_finalize(vcpu, what);
	}
	default:
		r = -EINVAL;
	}

	return r;
}

/**
 * kvm_vm_ioctl_get_dirty_log - get and clear the log of dirty pages in a slot
 * @kvm: kvm instance
 * @log: slot id and address to which we copy the log
 *
 * Steps 1-4 below provide general overview of dirty page logging. See
 * kvm_get_dirty_log_protect() function description for additional details.
 *
 * We call kvm_get_dirty_log_protect() to handle steps 1-3, upon return we
 * always flush the TLB (step 4) even if previous step failed  and the dirty
 * bitmap may be corrupt. Regardless of previous outcome the KVM logging API
 * does not preclude user space subsequent dirty log read. Flushing TLB ensures
 * writes will be marked dirty for next log read.
 *
 *   1. Take a snapshot of the bit and clear it if needed.
 *   2. Write protect the corresponding page.
 *   3. Copy the snapshot to the userspace.
 *   4. Flush TLB's if needed.
 */
int kvm_vm_ioctl_get_dirty_log(struct kvm *kvm, struct kvm_dirty_log *log)
{
	bool flush = false;
	int r;

	mutex_lock(&kvm->slots_lock);

	r = kvm_get_dirty_log_protect(kvm, log, &flush);

	if (flush)
		kvm_flush_remote_tlbs(kvm);

	mutex_unlock(&kvm->slots_lock);
	return r;
}

int kvm_vm_ioctl_clear_dirty_log(struct kvm *kvm, struct kvm_clear_dirty_log *log)
{
	bool flush = false;
	int r;

	mutex_lock(&kvm->slots_lock);

	r = kvm_clear_dirty_log_protect(kvm, log, &flush);

	if (flush)
		kvm_flush_remote_tlbs(kvm);

	mutex_unlock(&kvm->slots_lock);
	return r;
}

static int kvm_vm_ioctl_set_device_addr(struct kvm *kvm,
					struct kvm_arm_device_addr *dev_addr)
{
	unsigned long dev_id, type;

	dev_id = (dev_addr->id & KVM_ARM_DEVICE_ID_MASK) >>
		KVM_ARM_DEVICE_ID_SHIFT;
	type = (dev_addr->id & KVM_ARM_DEVICE_TYPE_MASK) >>
		KVM_ARM_DEVICE_TYPE_SHIFT;

	switch (dev_id) {
	case KVM_ARM_DEVICE_VGIC_V2:
		if (!vgic_present)
			return -ENXIO;
		return kvm_vgic_addr(kvm, type, &dev_addr->addr, true);
	default:
		return -ENODEV;
	}
}

long kvm_arch_vm_ioctl(struct file *filp,
		       unsigned int ioctl, unsigned long arg)
{
	struct kvm *kvm = filp->private_data;
	void __user *argp = (void __user *)arg;

	switch (ioctl) {
	case KVM_CREATE_IRQCHIP: {
		int ret;
		if (!vgic_present)
			return -ENXIO;
		mutex_lock(&kvm->lock);
		ret = kvm_vgic_create(kvm, KVM_DEV_TYPE_ARM_VGIC_V2);
		mutex_unlock(&kvm->lock);
		return ret;
	}
	case KVM_ARM_SET_DEVICE_ADDR: {
		struct kvm_arm_device_addr dev_addr;

		if (copy_from_user(&dev_addr, argp, sizeof(dev_addr)))
			return -EFAULT;
		return kvm_vm_ioctl_set_device_addr(kvm, &dev_addr);
	}
	case KVM_ARM_PREFERRED_TARGET: {
		int err;
		struct kvm_vcpu_init init;

		err = kvm_vcpu_preferred_target(&init);
		if (err)
			return err;

		if (copy_to_user(argp, &init, sizeof(init)))
			return -EFAULT;

		return 0;
	}
	default:
		return -EINVAL;
	}
}

static void cpu_init_hyp_mode(void *dummy)
{
	phys_addr_t pgd_ptr;
	unsigned long hyp_stack_ptr;
	unsigned long stack_page;
	unsigned long vector_ptr;

	/* Switch from the HYP stub to our own HYP init vector */
	__hyp_set_vectors(kvm_get_idmap_vector());

	pgd_ptr = kvm_mmu_get_httbr();
	stack_page = __this_cpu_read(kvm_arm_hyp_stack_page);
	hyp_stack_ptr = stack_page + PAGE_SIZE;
	vector_ptr = (unsigned long)kvm_get_hyp_vector();

	__cpu_init_hyp_mode(pgd_ptr, hyp_stack_ptr, vector_ptr);
	__cpu_init_stage2();
}

static void cpu_hyp_reset(void)
{
	if (!is_kernel_in_hyp_mode())
		__hyp_reset_vectors();
}

static void cpu_hyp_reinit(void)
{
	cpu_hyp_reset();

	if (is_kernel_in_hyp_mode())
		kvm_timer_init_vhe();
	else
		cpu_init_hyp_mode(NULL);

	kvm_arm_init_debug();

	if (vgic_present)
		kvm_vgic_init_cpu_hardware();
}

static void _kvm_arch_hardware_enable(void *discard)
{
	if (!__this_cpu_read(kvm_arm_hardware_enabled)) {
		cpu_hyp_reinit();
		__this_cpu_write(kvm_arm_hardware_enabled, 1);
	}
}

int kvm_arch_hardware_enable(void)
{
	_kvm_arch_hardware_enable(NULL);
	return 0;
}

static void _kvm_arch_hardware_disable(void *discard)
{
	if (__this_cpu_read(kvm_arm_hardware_enabled)) {
		cpu_hyp_reset();
		__this_cpu_write(kvm_arm_hardware_enabled, 0);
	}
}

void kvm_arch_hardware_disable(void)
{
	_kvm_arch_hardware_disable(NULL);
}

#ifdef CONFIG_CPU_PM
static int hyp_init_cpu_pm_notifier(struct notifier_block *self,
				    unsigned long cmd,
				    void *v)
{
	/*
	 * kvm_arm_hardware_enabled is left with its old value over
	 * PM_ENTER->PM_EXIT. It is used to indicate PM_EXIT should
	 * re-enable hyp.
	 */
	switch (cmd) {
	case CPU_PM_ENTER:
		if (__this_cpu_read(kvm_arm_hardware_enabled))
			/*
			 * don't update kvm_arm_hardware_enabled here
			 * so that the hardware will be re-enabled
			 * when we resume. See below.
			 */
			cpu_hyp_reset();

		return NOTIFY_OK;
	case CPU_PM_ENTER_FAILED:
	case CPU_PM_EXIT:
		if (__this_cpu_read(kvm_arm_hardware_enabled))
			/* The hardware was enabled before suspend. */
			cpu_hyp_reinit();

		return NOTIFY_OK;

	default:
		return NOTIFY_DONE;
	}
}

static struct notifier_block hyp_init_cpu_pm_nb = {
	.notifier_call = hyp_init_cpu_pm_notifier,
};

static void __init hyp_cpu_pm_init(void)
{
	cpu_pm_register_notifier(&hyp_init_cpu_pm_nb);
}
static void __init hyp_cpu_pm_exit(void)
{
	cpu_pm_unregister_notifier(&hyp_init_cpu_pm_nb);
}
#else
static inline void hyp_cpu_pm_init(void)
{
}
static inline void hyp_cpu_pm_exit(void)
{
}
#endif

static int init_common_resources(void)
{
	kvm_set_ipa_limit();

	return 0;
}

static int init_subsystems(void)
{
	int err = 0;

	/*
	 * Enable hardware so that subsystem initialisation can access EL2.
	 */
	on_each_cpu(_kvm_arch_hardware_enable, NULL, 1);

	/*
	 * Register CPU lower-power notifier
	 */
	hyp_cpu_pm_init();

	/*
	 * Init HYP view of VGIC
	 */
	err = kvm_vgic_hyp_init();
	switch (err) {
	case 0:
		vgic_present = true;
		break;
	case -ENODEV:
	case -ENXIO:
		vgic_present = false;
		err = 0;
		break;
	default:
		goto out;
	}

	/*
	 * Init HYP architected timer support
	 */
	err = kvm_timer_hyp_init(vgic_present);
	if (err)
		goto out;

	kvm_perf_init();
	kvm_coproc_table_init();

out:
	on_each_cpu(_kvm_arch_hardware_disable, NULL, 1);

	return err;
}

static void teardown_hyp_mode(void)
{
	int cpu;

	free_hyp_pgds();
	for_each_possible_cpu(cpu)
		free_page(per_cpu(kvm_arm_hyp_stack_page, cpu));
	hyp_cpu_pm_exit();
}

/**
 * Inits Hyp-mode on all online CPUs
 */
static int init_hyp_mode(void)
{
	int cpu;
	int err = 0;

	/*
	 * Allocate Hyp PGD and setup Hyp identity mapping
	 */
	err = kvm_mmu_init();
	if (err)
		goto out_err;

	/*
	 * Allocate stack pages for Hypervisor-mode
	 */
	for_each_possible_cpu(cpu) {
		unsigned long stack_page;

		stack_page = __get_free_page(GFP_KERNEL);
		if (!stack_page) {
			err = -ENOMEM;
			goto out_err;
		}

		per_cpu(kvm_arm_hyp_stack_page, cpu) = stack_page;
	}

	/*
	 * Map the Hyp-code called directly from the host
	 */
	err = create_hyp_mappings(kvm_ksym_ref(__hyp_text_start),
				  kvm_ksym_ref(__hyp_text_end), PAGE_HYP_EXEC);
	if (err) {
		kvm_err("Cannot map world-switch code\n");
		goto out_err;
	}

	err = create_hyp_mappings(kvm_ksym_ref(__start_rodata),
				  kvm_ksym_ref(__end_rodata), PAGE_HYP_RO);
	if (err) {
		kvm_err("Cannot map rodata section\n");
		goto out_err;
	}

	err = create_hyp_mappings(kvm_ksym_ref(__bss_start),
				  kvm_ksym_ref(__bss_stop), PAGE_HYP_RO);
	if (err) {
		kvm_err("Cannot map bss section\n");
		goto out_err;
	}

	err = kvm_map_vectors();
	if (err) {
		kvm_err("Cannot map vectors\n");
		goto out_err;
	}

	/*
	 * Map the Hyp stack pages
	 */
	for_each_possible_cpu(cpu) {
		char *stack_page = (char *)per_cpu(kvm_arm_hyp_stack_page, cpu);
		err = create_hyp_mappings(stack_page, stack_page + PAGE_SIZE,
					  PAGE_HYP);

		if (err) {
			kvm_err("Cannot map hyp stack\n");
			goto out_err;
		}
	}

	for_each_possible_cpu(cpu) {
		kvm_host_data_t *cpu_data;

		cpu_data = per_cpu_ptr(&kvm_host_data, cpu);
		kvm_init_host_cpu_context(&cpu_data->host_ctxt, cpu);
		err = create_hyp_mappings(cpu_data, cpu_data + 1, PAGE_HYP);

		if (err) {
			kvm_err("Cannot map host CPU state: %d\n", err);
			goto out_err;
		}
	}

	err = hyp_map_aux_data();
	if (err)
		kvm_err("Cannot map host auxiliary data: %d\n", err);

	return 0;

out_err:
	teardown_hyp_mode();
	kvm_err("error initializing Hyp mode: %d\n", err);
	return err;
}

static void check_kvm_target_cpu(void *ret)
{
	*(int *)ret = kvm_target_cpu();
}

struct kvm_vcpu *kvm_mpidr_to_vcpu(struct kvm *kvm, unsigned long mpidr)
{
	struct kvm_vcpu *vcpu;
	int i;

	mpidr &= MPIDR_HWID_BITMASK;
	kvm_for_each_vcpu(i, vcpu, kvm) {
		if (mpidr == kvm_vcpu_get_mpidr_aff(vcpu))
			return vcpu;
	}
	return NULL;
}

bool kvm_arch_has_irq_bypass(void)
{
	return true;
}

int kvm_arch_irq_bypass_add_producer(struct irq_bypass_consumer *cons,
				      struct irq_bypass_producer *prod)
{
	struct kvm_kernel_irqfd *irqfd =
		container_of(cons, struct kvm_kernel_irqfd, consumer);

	return kvm_vgic_v4_set_forwarding(irqfd->kvm, prod->irq,
					  &irqfd->irq_entry);
}
void kvm_arch_irq_bypass_del_producer(struct irq_bypass_consumer *cons,
				      struct irq_bypass_producer *prod)
{
	struct kvm_kernel_irqfd *irqfd =
		container_of(cons, struct kvm_kernel_irqfd, consumer);

	kvm_vgic_v4_unset_forwarding(irqfd->kvm, prod->irq,
				     &irqfd->irq_entry);
}

void kvm_arch_irq_bypass_stop(struct irq_bypass_consumer *cons)
{
	struct kvm_kernel_irqfd *irqfd =
		container_of(cons, struct kvm_kernel_irqfd, consumer);

	kvm_arm_halt_guest(irqfd->kvm);
}

void kvm_arch_irq_bypass_start(struct irq_bypass_consumer *cons)
{
	struct kvm_kernel_irqfd *irqfd =
		container_of(cons, struct kvm_kernel_irqfd, consumer);

	kvm_arm_resume_guest(irqfd->kvm);
}

/**
 * Initialize Hyp-mode and memory mappings on all CPUs.
 */
int kvm_arch_init(void *opaque)
{
	int err;
	int ret, cpu;
	bool in_hyp_mode;

	if (!is_hyp_mode_available()) {
		kvm_info("HYP mode not available\n");
		return -ENODEV;
	}

	in_hyp_mode = is_kernel_in_hyp_mode();

	if (!in_hyp_mode && kvm_arch_requires_vhe()) {
		kvm_pr_unimpl("CPU unsupported in non-VHE mode, not initializing\n");
		return -ENODEV;
	}

	for_each_online_cpu(cpu) {
		smp_call_function_single(cpu, check_kvm_target_cpu, &ret, 1);
		if (ret < 0) {
			kvm_err("Error, CPU %d not supported!\n", cpu);
			return -ENODEV;
		}
	}

	err = init_common_resources();
	if (err)
		return err;

	err = kvm_arm_init_sve();
	if (err)
		return err;

	if (!in_hyp_mode) {
		err = init_hyp_mode();
		if (err)
			goto out_err;
	}

	err = init_subsystems();
	if (err)
		goto out_hyp;

	if (in_hyp_mode)
		kvm_info("VHE mode initialized successfully\n");
	else
		kvm_info("Hyp mode initialized successfully\n");

	return 0;

out_hyp:
	if (!in_hyp_mode)
		teardown_hyp_mode();
out_err:
	return err;
}

/* NOP: Compiling as a module not supported */
void kvm_arch_exit(void)
{
	kvm_perf_teardown();
}

static int arm_init(void)
{
	int rc = kvm_init(NULL, sizeof(struct kvm_vcpu), 0, THIS_MODULE);
	return rc;
}

module_init(arm_init);
