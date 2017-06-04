/*
 * Copyright (C) 2012 ARM Ltd.
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/cpu.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <clocksource/arm_arch_timer.h>
#include <asm/arch_timer.h>
#include <asm/kvm_hyp.h>

#include <kvm/arm_vgic.h>
#include <kvm/arm_arch_timer.h>

#include "trace.h"

static struct timecounter *timecounter;
static unsigned int host_vtimer_irq;
static u32 host_vtimer_irq_flags;

void kvm_timer_vcpu_put(struct kvm_vcpu *vcpu)
{
	vcpu_vtimer(vcpu)->active_cleared_last = false;
}

u64 kvm_phys_timer_read(void)
{
	return timecounter->cc->read(timecounter->cc);
}

static bool timer_is_armed(struct arch_timer_cpu *timer)
{
	return timer->armed;
}

/* timer_arm: as in "arm the timer", not as in ARM the company */
static void timer_arm(struct arch_timer_cpu *timer, u64 ns)
{
	timer->armed = true;
	hrtimer_start(&timer->timer, ktime_add_ns(ktime_get(), ns),
		      HRTIMER_MODE_ABS);
}

static void timer_disarm(struct arch_timer_cpu *timer)
{
	if (timer_is_armed(timer)) {
		hrtimer_cancel(&timer->timer);
		cancel_work_sync(&timer->expired);
		timer->armed = false;
	}
}

static irqreturn_t kvm_arch_timer_handler(int irq, void *dev_id)
{
	struct kvm_vcpu *vcpu = *(struct kvm_vcpu **)dev_id;

	/*
	 * We disable the timer in the world switch and let it be
	 * handled by kvm_timer_sync_hwstate(). Getting a timer
	 * interrupt at this point is a sure sign of some major
	 * breakage.
	 */
	pr_warn("Unexpected interrupt %d on vcpu %p\n", irq, vcpu);
	return IRQ_HANDLED;
}

/*
 * Work function for handling the backup timer that we schedule when a vcpu is
 * no longer running, but had a timer programmed to fire in the future.
 */
static void kvm_timer_inject_irq_work(struct work_struct *work)
{
	struct kvm_vcpu *vcpu;

	vcpu = container_of(work, struct kvm_vcpu, arch.timer_cpu.expired);

	/*
	 * If the vcpu is blocked we want to wake it up so that it will see
	 * the timer has expired when entering the guest.
	 */
	kvm_vcpu_wake_up(vcpu);
}

static u64 kvm_timer_compute_delta(struct arch_timer_context *timer_ctx)
{
	u64 cval, now;

	cval = timer_ctx->cnt_cval;
	now = kvm_phys_timer_read() - timer_ctx->cntvoff;

	if (now < cval) {
		u64 ns;

		ns = cyclecounter_cyc2ns(timecounter->cc,
					 cval - now,
					 timecounter->mask,
					 &timecounter->frac);
		return ns;
	}

	return 0;
}

static bool kvm_timer_irq_can_fire(struct arch_timer_context *timer_ctx)
{
	return !(timer_ctx->cnt_ctl & ARCH_TIMER_CTRL_IT_MASK) &&
		(timer_ctx->cnt_ctl & ARCH_TIMER_CTRL_ENABLE);
}

/*
 * Returns the earliest expiration time in ns among guest timers.
 * Note that it will return 0 if none of timers can fire.
 */
static u64 kvm_timer_earliest_exp(struct kvm_vcpu *vcpu)
{
	u64 min_virt = ULLONG_MAX, min_phys = ULLONG_MAX;
	struct arch_timer_context *vtimer = vcpu_vtimer(vcpu);
	struct arch_timer_context *ptimer = vcpu_ptimer(vcpu);

	if (kvm_timer_irq_can_fire(vtimer))
		min_virt = kvm_timer_compute_delta(vtimer);

	if (kvm_timer_irq_can_fire(ptimer))
		min_phys = kvm_timer_compute_delta(ptimer);

	/* If none of timers can fire, then return 0 */
	if ((min_virt == ULLONG_MAX) && (min_phys == ULLONG_MAX))
		return 0;

	return min(min_virt, min_phys);
}

static enum hrtimer_restart kvm_timer_expire(struct hrtimer *hrt)
{
	struct arch_timer_cpu *timer;
	struct kvm_vcpu *vcpu;
	u64 ns;

	timer = container_of(hrt, struct arch_timer_cpu, timer);
	vcpu = container_of(timer, struct kvm_vcpu, arch.timer_cpu);

	/*
	 * Check that the timer has really expired from the guest's
	 * PoV (NTP on the host may have forced it to expire
	 * early). If we should have slept longer, restart it.
	 */
	ns = kvm_timer_earliest_exp(vcpu);
	if (unlikely(ns)) {
		hrtimer_forward_now(hrt, ns_to_ktime(ns));
		return HRTIMER_RESTART;
	}

	schedule_work(&timer->expired);
	return HRTIMER_NORESTART;
}

bool kvm_timer_should_fire(struct arch_timer_context *timer_ctx)
{
	u64 cval, now;

	if (!kvm_timer_irq_can_fire(timer_ctx))
		return false;

	cval = timer_ctx->cnt_cval;
	now = kvm_phys_timer_read() - timer_ctx->cntvoff;

	return cval <= now;
}

/*
 * Reflect the timer output level into the kvm_run structure
 */
void kvm_timer_update_run(struct kvm_vcpu *vcpu)
{
	struct arch_timer_context *vtimer = vcpu_vtimer(vcpu);
	struct arch_timer_context *ptimer = vcpu_ptimer(vcpu);
	struct kvm_sync_regs *regs = &vcpu->run->s.regs;

	/* Populate the device bitmap with the timer states */
	regs->device_irq_level &= ~(KVM_ARM_DEV_EL1_VTIMER |
				    KVM_ARM_DEV_EL1_PTIMER);
	if (vtimer->irq.level)
		regs->device_irq_level |= KVM_ARM_DEV_EL1_VTIMER;
	if (ptimer->irq.level)
		regs->device_irq_level |= KVM_ARM_DEV_EL1_PTIMER;
}

static void kvm_timer_update_irq(struct kvm_vcpu *vcpu, bool new_level,
				 struct arch_timer_context *timer_ctx)
{
	int ret;

	timer_ctx->active_cleared_last = false;
	timer_ctx->irq.level = new_level;
	trace_kvm_timer_update_irq(vcpu->vcpu_id, timer_ctx->irq.irq,
				   timer_ctx->irq.level);

	if (likely(irqchip_in_kernel(vcpu->kvm))) {
		ret = kvm_vgic_inject_irq(vcpu->kvm, vcpu->vcpu_id,
					  timer_ctx->irq.irq,
					  timer_ctx->irq.level);
		WARN_ON(ret);
	}
}

/*
 * Check if there was a change in the timer state (should we raise or lower
 * the line level to the GIC).
 */
static void kvm_timer_update_state(struct kvm_vcpu *vcpu)
{
	struct arch_timer_cpu *timer = &vcpu->arch.timer_cpu;
	struct arch_timer_context *vtimer = vcpu_vtimer(vcpu);
	struct arch_timer_context *ptimer = vcpu_ptimer(vcpu);

	/*
	 * If userspace modified the timer registers via SET_ONE_REG before
	 * the vgic was initialized, we mustn't set the vtimer->irq.level value
	 * because the guest would never see the interrupt.  Instead wait
	 * until we call this function from kvm_timer_flush_hwstate.
	 */
	if (unlikely(!timer->enabled))
		return;

	if (kvm_timer_should_fire(vtimer) != vtimer->irq.level)
		kvm_timer_update_irq(vcpu, !vtimer->irq.level, vtimer);

	if (kvm_timer_should_fire(ptimer) != ptimer->irq.level)
		kvm_timer_update_irq(vcpu, !ptimer->irq.level, ptimer);
}

/* Schedule the background timer for the emulated timer. */
static void kvm_timer_emulate(struct kvm_vcpu *vcpu,
			      struct arch_timer_context *timer_ctx)
{
	struct arch_timer_cpu *timer = &vcpu->arch.timer_cpu;

	if (kvm_timer_should_fire(timer_ctx))
		return;

	if (!kvm_timer_irq_can_fire(timer_ctx))
		return;

	/*  The timer has not yet expired, schedule a background timer */
	timer_arm(timer, kvm_timer_compute_delta(timer_ctx));
}

/*
 * Schedule the background timer before calling kvm_vcpu_block, so that this
 * thread is removed from its waitqueue and made runnable when there's a timer
 * interrupt to handle.
 */
void kvm_timer_schedule(struct kvm_vcpu *vcpu)
{
	struct arch_timer_cpu *timer = &vcpu->arch.timer_cpu;
	struct arch_timer_context *vtimer = vcpu_vtimer(vcpu);
	struct arch_timer_context *ptimer = vcpu_ptimer(vcpu);

	BUG_ON(timer_is_armed(timer));

	/*
	 * No need to schedule a background timer if any guest timer has
	 * already expired, because kvm_vcpu_block will return before putting
	 * the thread to sleep.
	 */
	if (kvm_timer_should_fire(vtimer) || kvm_timer_should_fire(ptimer))
		return;

	/*
	 * If both timers are not capable of raising interrupts (disabled or
	 * masked), then there's no more work for us to do.
	 */
	if (!kvm_timer_irq_can_fire(vtimer) && !kvm_timer_irq_can_fire(ptimer))
		return;

	/*
	 * The guest timers have not yet expired, schedule a background timer.
	 * Set the earliest expiration time among the guest timers.
	 */
	timer_arm(timer, kvm_timer_earliest_exp(vcpu));
}

void kvm_timer_unschedule(struct kvm_vcpu *vcpu)
{
	struct arch_timer_cpu *timer = &vcpu->arch.timer_cpu;
	timer_disarm(timer);
}

static void kvm_timer_flush_hwstate_vgic(struct kvm_vcpu *vcpu)
{
	struct arch_timer_context *vtimer = vcpu_vtimer(vcpu);
	bool phys_active;
	int ret;

	/*
	* If we enter the guest with the virtual input level to the VGIC
	* asserted, then we have already told the VGIC what we need to, and
	* we don't need to exit from the guest until the guest deactivates
	* the already injected interrupt, so therefore we should set the
	* hardware active state to prevent unnecessary exits from the guest.
	*
	* Also, if we enter the guest with the virtual timer interrupt active,
	* then it must be active on the physical distributor, because we set
	* the HW bit and the guest must be able to deactivate the virtual and
	* physical interrupt at the same time.
	*
	* Conversely, if the virtual input level is deasserted and the virtual
	* interrupt is not active, then always clear the hardware active state
	* to ensure that hardware interrupts from the timer triggers a guest
	* exit.
	*/
	phys_active = vtimer->irq.level ||
			kvm_vgic_map_is_active(vcpu, vtimer->irq.irq);

	/*
	 * We want to avoid hitting the (re)distributor as much as
	 * possible, as this is a potentially expensive MMIO access
	 * (not to mention locks in the irq layer), and a solution for
	 * this is to cache the "active" state in memory.
	 *
	 * Things to consider: we cannot cache an "active set" state,
	 * because the HW can change this behind our back (it becomes
	 * "clear" in the HW). We must then restrict the caching to
	 * the "clear" state.
	 *
	 * The cache is invalidated on:
	 * - vcpu put, indicating that the HW cannot be trusted to be
	 *   in a sane state on the next vcpu load,
	 * - any change in the interrupt state
	 *
	 * Usage conditions:
	 * - cached value is "active clear"
	 * - value to be programmed is "active clear"
	 */
	if (vtimer->active_cleared_last && !phys_active)
		return;

	ret = irq_set_irqchip_state(host_vtimer_irq,
				    IRQCHIP_STATE_ACTIVE,
				    phys_active);
	WARN_ON(ret);

	vtimer->active_cleared_last = !phys_active;
}

bool kvm_timer_should_notify_user(struct kvm_vcpu *vcpu)
{
	struct arch_timer_context *vtimer = vcpu_vtimer(vcpu);
	struct arch_timer_context *ptimer = vcpu_ptimer(vcpu);
	struct kvm_sync_regs *sregs = &vcpu->run->s.regs;
	bool vlevel, plevel;

	if (likely(irqchip_in_kernel(vcpu->kvm)))
		return false;

	vlevel = sregs->device_irq_level & KVM_ARM_DEV_EL1_VTIMER;
	plevel = sregs->device_irq_level & KVM_ARM_DEV_EL1_PTIMER;

	return vtimer->irq.level != vlevel ||
	       ptimer->irq.level != plevel;
}

static void kvm_timer_flush_hwstate_user(struct kvm_vcpu *vcpu)
{
	struct arch_timer_context *vtimer = vcpu_vtimer(vcpu);

	/*
	 * To prevent continuously exiting from the guest, we mask the
	 * physical interrupt such that the guest can make forward progress.
	 * Once we detect the output level being deasserted, we unmask the
	 * interrupt again so that we exit from the guest when the timer
	 * fires.
	*/
	if (vtimer->irq.level)
		disable_percpu_irq(host_vtimer_irq);
	else
		enable_percpu_irq(host_vtimer_irq, 0);
}

/**
 * kvm_timer_flush_hwstate - prepare timers before running the vcpu
 * @vcpu: The vcpu pointer
 *
 * Check if the virtual timer has expired while we were running in the host,
 * and inject an interrupt if that was the case, making sure the timer is
 * masked or disabled on the host so that we keep executing.  Also schedule a
 * software timer for the physical timer if it is enabled.
 */
void kvm_timer_flush_hwstate(struct kvm_vcpu *vcpu)
{
	struct arch_timer_cpu *timer = &vcpu->arch.timer_cpu;

	if (unlikely(!timer->enabled))
		return;

	kvm_timer_update_state(vcpu);

	/* Set the background timer for the physical timer emulation. */
	kvm_timer_emulate(vcpu, vcpu_ptimer(vcpu));

	if (unlikely(!irqchip_in_kernel(vcpu->kvm)))
		kvm_timer_flush_hwstate_user(vcpu);
	else
		kvm_timer_flush_hwstate_vgic(vcpu);
}

/**
 * kvm_timer_sync_hwstate - sync timer state from cpu
 * @vcpu: The vcpu pointer
 *
 * Check if any of the timers have expired while we were running in the guest,
 * and inject an interrupt if that was the case.
 */
void kvm_timer_sync_hwstate(struct kvm_vcpu *vcpu)
{
	struct arch_timer_cpu *timer = &vcpu->arch.timer_cpu;

	/*
	 * This is to cancel the background timer for the physical timer
	 * emulation if it is set.
	 */
	timer_disarm(timer);

	/*
	 * The guest could have modified the timer registers or the timer
	 * could have expired, update the timer state.
	 */
	kvm_timer_update_state(vcpu);
}

int kvm_timer_vcpu_reset(struct kvm_vcpu *vcpu,
			 const struct kvm_irq_level *virt_irq,
			 const struct kvm_irq_level *phys_irq)
{
	struct arch_timer_context *vtimer = vcpu_vtimer(vcpu);
	struct arch_timer_context *ptimer = vcpu_ptimer(vcpu);

	/*
	 * The vcpu timer irq number cannot be determined in
	 * kvm_timer_vcpu_init() because it is called much before
	 * kvm_vcpu_set_target(). To handle this, we determine
	 * vcpu timer irq number when the vcpu is reset.
	 */
	vtimer->irq.irq = virt_irq->irq;
	ptimer->irq.irq = phys_irq->irq;

	/*
	 * The bits in CNTV_CTL are architecturally reset to UNKNOWN for ARMv8
	 * and to 0 for ARMv7.  We provide an implementation that always
	 * resets the timer to be disabled and unmasked and is compliant with
	 * the ARMv7 architecture.
	 */
	vtimer->cnt_ctl = 0;
	ptimer->cnt_ctl = 0;
	kvm_timer_update_state(vcpu);

	return 0;
}

/* Make the updates of cntvoff for all vtimer contexts atomic */
static void update_vtimer_cntvoff(struct kvm_vcpu *vcpu, u64 cntvoff)
{
	int i;
	struct kvm *kvm = vcpu->kvm;
	struct kvm_vcpu *tmp;

	mutex_lock(&kvm->lock);
	kvm_for_each_vcpu(i, tmp, kvm)
		vcpu_vtimer(tmp)->cntvoff = cntvoff;

	/*
	 * When called from the vcpu create path, the CPU being created is not
	 * included in the loop above, so we just set it here as well.
	 */
	vcpu_vtimer(vcpu)->cntvoff = cntvoff;
	mutex_unlock(&kvm->lock);
}

void kvm_timer_vcpu_init(struct kvm_vcpu *vcpu)
{
	struct arch_timer_cpu *timer = &vcpu->arch.timer_cpu;

	/* Synchronize cntvoff across all vtimers of a VM. */
	update_vtimer_cntvoff(vcpu, kvm_phys_timer_read());
	vcpu_ptimer(vcpu)->cntvoff = 0;

	INIT_WORK(&timer->expired, kvm_timer_inject_irq_work);
	hrtimer_init(&timer->timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	timer->timer.function = kvm_timer_expire;
}

static void kvm_timer_init_interrupt(void *info)
{
	enable_percpu_irq(host_vtimer_irq, host_vtimer_irq_flags);
}

int kvm_arm_timer_set_reg(struct kvm_vcpu *vcpu, u64 regid, u64 value)
{
	struct arch_timer_context *vtimer = vcpu_vtimer(vcpu);

	switch (regid) {
	case KVM_REG_ARM_TIMER_CTL:
		vtimer->cnt_ctl = value;
		break;
	case KVM_REG_ARM_TIMER_CNT:
		update_vtimer_cntvoff(vcpu, kvm_phys_timer_read() - value);
		break;
	case KVM_REG_ARM_TIMER_CVAL:
		vtimer->cnt_cval = value;
		break;
	default:
		return -1;
	}

	kvm_timer_update_state(vcpu);
	return 0;
}

u64 kvm_arm_timer_get_reg(struct kvm_vcpu *vcpu, u64 regid)
{
	struct arch_timer_context *vtimer = vcpu_vtimer(vcpu);

	switch (regid) {
	case KVM_REG_ARM_TIMER_CTL:
		return vtimer->cnt_ctl;
	case KVM_REG_ARM_TIMER_CNT:
		return kvm_phys_timer_read() - vtimer->cntvoff;
	case KVM_REG_ARM_TIMER_CVAL:
		return vtimer->cnt_cval;
	}
	return (u64)-1;
}

static int kvm_timer_starting_cpu(unsigned int cpu)
{
	kvm_timer_init_interrupt(NULL);
	return 0;
}

static int kvm_timer_dying_cpu(unsigned int cpu)
{
	disable_percpu_irq(host_vtimer_irq);
	return 0;
}

int kvm_timer_hyp_init(void)
{
	struct arch_timer_kvm_info *info;
	int err;

	info = arch_timer_get_kvm_info();
	timecounter = &info->timecounter;

	if (!timecounter->cc) {
		kvm_err("kvm_arch_timer: uninitialized timecounter\n");
		return -ENODEV;
	}

	if (info->virtual_irq <= 0) {
		kvm_err("kvm_arch_timer: invalid virtual timer IRQ: %d\n",
			info->virtual_irq);
		return -ENODEV;
	}
	host_vtimer_irq = info->virtual_irq;

	host_vtimer_irq_flags = irq_get_trigger_type(host_vtimer_irq);
	if (host_vtimer_irq_flags != IRQF_TRIGGER_HIGH &&
	    host_vtimer_irq_flags != IRQF_TRIGGER_LOW) {
		kvm_err("Invalid trigger for IRQ%d, assuming level low\n",
			host_vtimer_irq);
		host_vtimer_irq_flags = IRQF_TRIGGER_LOW;
	}

	err = request_percpu_irq(host_vtimer_irq, kvm_arch_timer_handler,
				 "kvm guest timer", kvm_get_running_vcpus());
	if (err) {
		kvm_err("kvm_arch_timer: can't request interrupt %d (%d)\n",
			host_vtimer_irq, err);
		return err;
	}

	kvm_info("virtual timer IRQ%d\n", host_vtimer_irq);

	cpuhp_setup_state(CPUHP_AP_KVM_ARM_TIMER_STARTING,
			  "kvm/arm/timer:starting", kvm_timer_starting_cpu,
			  kvm_timer_dying_cpu);
	return err;
}

void kvm_timer_vcpu_terminate(struct kvm_vcpu *vcpu)
{
	struct arch_timer_cpu *timer = &vcpu->arch.timer_cpu;
	struct arch_timer_context *vtimer = vcpu_vtimer(vcpu);

	timer_disarm(timer);
	kvm_vgic_unmap_phys_irq(vcpu, vtimer->irq.irq);
}

int kvm_timer_enable(struct kvm_vcpu *vcpu)
{
	struct arch_timer_cpu *timer = &vcpu->arch.timer_cpu;
	struct arch_timer_context *vtimer = vcpu_vtimer(vcpu);
	struct irq_desc *desc;
	struct irq_data *data;
	int phys_irq;
	int ret;

	if (timer->enabled)
		return 0;

	/* Without a VGIC we do not map virtual IRQs to physical IRQs */
	if (!irqchip_in_kernel(vcpu->kvm))
		goto no_vgic;

	if (!vgic_initialized(vcpu->kvm))
		return -ENODEV;

	/*
	 * Find the physical IRQ number corresponding to the host_vtimer_irq
	 */
	desc = irq_to_desc(host_vtimer_irq);
	if (!desc) {
		kvm_err("%s: no interrupt descriptor\n", __func__);
		return -EINVAL;
	}

	data = irq_desc_get_irq_data(desc);
	while (data->parent_data)
		data = data->parent_data;

	phys_irq = data->hwirq;

	/*
	 * Tell the VGIC that the virtual interrupt is tied to a
	 * physical interrupt. We do that once per VCPU.
	 */
	ret = kvm_vgic_map_phys_irq(vcpu, vtimer->irq.irq, phys_irq);
	if (ret)
		return ret;

no_vgic:
	timer->enabled = 1;
	return 0;
}

/*
 * On VHE system, we only need to configure trap on physical timer and counter
 * accesses in EL0 and EL1 once, not for every world switch.
 * The host kernel runs at EL2 with HCR_EL2.TGE == 1,
 * and this makes those bits have no effect for the host kernel execution.
 */
void kvm_timer_init_vhe(void)
{
	/* When HCR_EL2.E2H ==1, EL1PCEN and EL1PCTEN are shifted by 10 */
	u32 cnthctl_shift = 10;
	u64 val;

	/*
	 * Disallow physical timer access for the guest.
	 * Physical counter access is allowed.
	 */
	val = read_sysreg(cnthctl_el2);
	val &= ~(CNTHCTL_EL1PCEN << cnthctl_shift);
	val |= (CNTHCTL_EL1PCTEN << cnthctl_shift);
	write_sysreg(val, cnthctl_el2);
}
