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
#include <linux/uaccess.h>

#include <clocksource/arm_arch_timer.h>
#include <asm/arch_timer.h>
#include <asm/kvm_hyp.h>

#include <kvm/arm_vgic.h>
#include <kvm/arm_arch_timer.h>

#include "trace.h"

static struct timecounter *timecounter;
static unsigned int host_vtimer_irq;
static u32 host_vtimer_irq_flags;

static DEFINE_STATIC_KEY_FALSE(has_gic_active_state);

static const struct kvm_irq_level default_ptimer_irq = {
	.irq	= 30,
	.level	= 1,
};

static const struct kvm_irq_level default_vtimer_irq = {
	.irq	= 27,
	.level	= 1,
};

static bool kvm_timer_irq_can_fire(struct arch_timer_context *timer_ctx);
static void kvm_timer_update_irq(struct kvm_vcpu *vcpu, bool new_level,
				 struct arch_timer_context *timer_ctx);
static bool kvm_timer_should_fire(struct arch_timer_context *timer_ctx);

u64 kvm_phys_timer_read(void)
{
	return timecounter->cc->read(timecounter->cc);
}

static inline bool userspace_irqchip(struct kvm *kvm)
{
	return static_branch_unlikely(&userspace_irqchip_in_use) &&
		unlikely(!irqchip_in_kernel(kvm));
}

static void soft_timer_start(struct hrtimer *hrt, u64 ns)
{
	hrtimer_start(hrt, ktime_add_ns(ktime_get(), ns),
		      HRTIMER_MODE_ABS);
}

static void soft_timer_cancel(struct hrtimer *hrt, struct work_struct *work)
{
	hrtimer_cancel(hrt);
	if (work)
		cancel_work_sync(work);
}

static irqreturn_t kvm_arch_timer_handler(int irq, void *dev_id)
{
	struct kvm_vcpu *vcpu = *(struct kvm_vcpu **)dev_id;
	struct arch_timer_context *vtimer;

	/*
	 * We may see a timer interrupt after vcpu_put() has been called which
	 * sets the CPU's vcpu pointer to NULL, because even though the timer
	 * has been disabled in vtimer_save_state(), the hardware interrupt
	 * signal may not have been retired from the interrupt controller yet.
	 */
	if (!vcpu)
		return IRQ_HANDLED;

	vtimer = vcpu_vtimer(vcpu);
	if (kvm_timer_should_fire(vtimer))
		kvm_timer_update_irq(vcpu, true, vtimer);

	if (userspace_irqchip(vcpu->kvm) &&
	    !static_branch_unlikely(&has_gic_active_state))
		disable_percpu_irq(host_vtimer_irq);

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

static enum hrtimer_restart kvm_bg_timer_expire(struct hrtimer *hrt)
{
	struct arch_timer_cpu *timer;
	struct kvm_vcpu *vcpu;
	u64 ns;

	timer = container_of(hrt, struct arch_timer_cpu, bg_timer);
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

static enum hrtimer_restart kvm_phys_timer_expire(struct hrtimer *hrt)
{
	struct arch_timer_context *ptimer;
	struct arch_timer_cpu *timer;
	struct kvm_vcpu *vcpu;
	u64 ns;

	timer = container_of(hrt, struct arch_timer_cpu, phys_timer);
	vcpu = container_of(timer, struct kvm_vcpu, arch.timer_cpu);
	ptimer = vcpu_ptimer(vcpu);

	/*
	 * Check that the timer has really expired from the guest's
	 * PoV (NTP on the host may have forced it to expire
	 * early). If not ready, schedule for a later time.
	 */
	ns = kvm_timer_compute_delta(ptimer);
	if (unlikely(ns)) {
		hrtimer_forward_now(hrt, ns_to_ktime(ns));
		return HRTIMER_RESTART;
	}

	kvm_timer_update_irq(vcpu, true, ptimer);
	return HRTIMER_NORESTART;
}

static bool kvm_timer_should_fire(struct arch_timer_context *timer_ctx)
{
	u64 cval, now;

	if (timer_ctx->loaded) {
		u32 cnt_ctl;

		/* Only the virtual timer can be loaded so far */
		cnt_ctl = read_sysreg_el0(cntv_ctl);
		return  (cnt_ctl & ARCH_TIMER_CTRL_ENABLE) &&
		        (cnt_ctl & ARCH_TIMER_CTRL_IT_STAT) &&
		       !(cnt_ctl & ARCH_TIMER_CTRL_IT_MASK);
	}

	if (!kvm_timer_irq_can_fire(timer_ctx))
		return false;

	cval = timer_ctx->cnt_cval;
	now = kvm_phys_timer_read() - timer_ctx->cntvoff;

	return cval <= now;
}

bool kvm_timer_is_pending(struct kvm_vcpu *vcpu)
{
	struct arch_timer_context *vtimer = vcpu_vtimer(vcpu);
	struct arch_timer_context *ptimer = vcpu_ptimer(vcpu);

	if (kvm_timer_should_fire(vtimer))
		return true;

	return kvm_timer_should_fire(ptimer);
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
	if (kvm_timer_should_fire(vtimer))
		regs->device_irq_level |= KVM_ARM_DEV_EL1_VTIMER;
	if (kvm_timer_should_fire(ptimer))
		regs->device_irq_level |= KVM_ARM_DEV_EL1_PTIMER;
}

static void kvm_timer_update_irq(struct kvm_vcpu *vcpu, bool new_level,
				 struct arch_timer_context *timer_ctx)
{
	int ret;

	timer_ctx->irq.level = new_level;
	trace_kvm_timer_update_irq(vcpu->vcpu_id, timer_ctx->irq.irq,
				   timer_ctx->irq.level);

	if (!userspace_irqchip(vcpu->kvm)) {
		ret = kvm_vgic_inject_irq(vcpu->kvm, vcpu->vcpu_id,
					  timer_ctx->irq.irq,
					  timer_ctx->irq.level,
					  timer_ctx);
		WARN_ON(ret);
	}
}

/* Schedule the background timer for the emulated timer. */
static void phys_timer_emulate(struct kvm_vcpu *vcpu)
{
	struct arch_timer_cpu *timer = &vcpu->arch.timer_cpu;
	struct arch_timer_context *ptimer = vcpu_ptimer(vcpu);

	/*
	 * If the timer can fire now we have just raised the IRQ line and we
	 * don't need to have a soft timer scheduled for the future.  If the
	 * timer cannot fire at all, then we also don't need a soft timer.
	 */
	if (kvm_timer_should_fire(ptimer) || !kvm_timer_irq_can_fire(ptimer)) {
		soft_timer_cancel(&timer->phys_timer, NULL);
		return;
	}

	soft_timer_start(&timer->phys_timer, kvm_timer_compute_delta(ptimer));
}

/*
 * Check if there was a change in the timer state, so that we should either
 * raise or lower the line level to the GIC or schedule a background timer to
 * emulate the physical timer.
 */
static void kvm_timer_update_state(struct kvm_vcpu *vcpu)
{
	struct arch_timer_cpu *timer = &vcpu->arch.timer_cpu;
	struct arch_timer_context *vtimer = vcpu_vtimer(vcpu);
	struct arch_timer_context *ptimer = vcpu_ptimer(vcpu);
	bool level;

	if (unlikely(!timer->enabled))
		return;

	/*
	 * The vtimer virtual interrupt is a 'mapped' interrupt, meaning part
	 * of its lifecycle is offloaded to the hardware, and we therefore may
	 * not have lowered the irq.level value before having to signal a new
	 * interrupt, but have to signal an interrupt every time the level is
	 * asserted.
	 */
	level = kvm_timer_should_fire(vtimer);
	kvm_timer_update_irq(vcpu, level, vtimer);

	if (kvm_timer_should_fire(ptimer) != ptimer->irq.level)
		kvm_timer_update_irq(vcpu, !ptimer->irq.level, ptimer);

	phys_timer_emulate(vcpu);
}

static void vtimer_save_state(struct kvm_vcpu *vcpu)
{
	struct arch_timer_cpu *timer = &vcpu->arch.timer_cpu;
	struct arch_timer_context *vtimer = vcpu_vtimer(vcpu);
	unsigned long flags;

	local_irq_save(flags);

	if (!vtimer->loaded)
		goto out;

	if (timer->enabled) {
		vtimer->cnt_ctl = read_sysreg_el0(cntv_ctl);
		vtimer->cnt_cval = read_sysreg_el0(cntv_cval);
	}

	/* Disable the virtual timer */
	write_sysreg_el0(0, cntv_ctl);
	isb();

	vtimer->loaded = false;
out:
	local_irq_restore(flags);
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

	vtimer_save_state(vcpu);

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
	soft_timer_start(&timer->bg_timer, kvm_timer_earliest_exp(vcpu));
}

static void vtimer_restore_state(struct kvm_vcpu *vcpu)
{
	struct arch_timer_cpu *timer = &vcpu->arch.timer_cpu;
	struct arch_timer_context *vtimer = vcpu_vtimer(vcpu);
	unsigned long flags;

	local_irq_save(flags);

	if (vtimer->loaded)
		goto out;

	if (timer->enabled) {
		write_sysreg_el0(vtimer->cnt_cval, cntv_cval);
		isb();
		write_sysreg_el0(vtimer->cnt_ctl, cntv_ctl);
	}

	vtimer->loaded = true;
out:
	local_irq_restore(flags);
}

void kvm_timer_unschedule(struct kvm_vcpu *vcpu)
{
	struct arch_timer_cpu *timer = &vcpu->arch.timer_cpu;

	vtimer_restore_state(vcpu);

	soft_timer_cancel(&timer->bg_timer, &timer->expired);
}

static void set_cntvoff(u64 cntvoff)
{
	u32 low = lower_32_bits(cntvoff);
	u32 high = upper_32_bits(cntvoff);

	/*
	 * Since kvm_call_hyp doesn't fully support the ARM PCS especially on
	 * 32-bit systems, but rather passes register by register shifted one
	 * place (we put the function address in r0/x0), we cannot simply pass
	 * a 64-bit value as an argument, but have to split the value in two
	 * 32-bit halves.
	 */
	kvm_call_hyp(__kvm_timer_set_cntvoff, low, high);
}

static inline void set_vtimer_irq_phys_active(struct kvm_vcpu *vcpu, bool active)
{
	int r;
	r = irq_set_irqchip_state(host_vtimer_irq, IRQCHIP_STATE_ACTIVE, active);
	WARN_ON(r);
}

static void kvm_timer_vcpu_load_gic(struct kvm_vcpu *vcpu)
{
	struct arch_timer_context *vtimer = vcpu_vtimer(vcpu);
	bool phys_active;

	if (irqchip_in_kernel(vcpu->kvm))
		phys_active = kvm_vgic_map_is_active(vcpu, vtimer->irq.irq);
	else
		phys_active = vtimer->irq.level;
	set_vtimer_irq_phys_active(vcpu, phys_active);
}

static void kvm_timer_vcpu_load_nogic(struct kvm_vcpu *vcpu)
{
	struct arch_timer_context *vtimer = vcpu_vtimer(vcpu);

	/*
	 * When using a userspace irqchip with the architected timers and a
	 * host interrupt controller that doesn't support an active state, we
	 * must still prevent continuously exiting from the guest, and
	 * therefore mask the physical interrupt by disabling it on the host
	 * interrupt controller when the virtual level is high, such that the
	 * guest can make forward progress.  Once we detect the output level
	 * being de-asserted, we unmask the interrupt again so that we exit
	 * from the guest when the timer fires.
	 */
	if (vtimer->irq.level)
		disable_percpu_irq(host_vtimer_irq);
	else
		enable_percpu_irq(host_vtimer_irq, host_vtimer_irq_flags);
}

void kvm_timer_vcpu_load(struct kvm_vcpu *vcpu)
{
	struct arch_timer_cpu *timer = &vcpu->arch.timer_cpu;
	struct arch_timer_context *vtimer = vcpu_vtimer(vcpu);

	if (unlikely(!timer->enabled))
		return;

	if (static_branch_likely(&has_gic_active_state))
		kvm_timer_vcpu_load_gic(vcpu);
	else
		kvm_timer_vcpu_load_nogic(vcpu);

	set_cntvoff(vtimer->cntvoff);

	vtimer_restore_state(vcpu);

	/* Set the background timer for the physical timer emulation. */
	phys_timer_emulate(vcpu);
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

	return kvm_timer_should_fire(vtimer) != vlevel ||
	       kvm_timer_should_fire(ptimer) != plevel;
}

void kvm_timer_vcpu_put(struct kvm_vcpu *vcpu)
{
	struct arch_timer_cpu *timer = &vcpu->arch.timer_cpu;

	if (unlikely(!timer->enabled))
		return;

	vtimer_save_state(vcpu);

	/*
	 * Cancel the physical timer emulation, because the only case where we
	 * need it after a vcpu_put is in the context of a sleeping VCPU, and
	 * in that case we already factor in the deadline for the physical
	 * timer when scheduling the bg_timer.
	 *
	 * In any case, we re-schedule the hrtimer for the physical timer when
	 * coming back to the VCPU thread in kvm_timer_vcpu_load().
	 */
	soft_timer_cancel(&timer->phys_timer, NULL);

	/*
	 * The kernel may decide to run userspace after calling vcpu_put, so
	 * we reset cntvoff to 0 to ensure a consistent read between user
	 * accesses to the virtual counter and kernel access to the physical
	 * counter of non-VHE case. For VHE, the virtual counter uses a fixed
	 * virtual offset of zero, so no need to zero CNTVOFF_EL2 register.
	 */
	if (!has_vhe())
		set_cntvoff(0);
}

/*
 * With a userspace irqchip we have to check if the guest de-asserted the
 * timer and if so, unmask the timer irq signal on the host interrupt
 * controller to ensure that we see future timer signals.
 */
static void unmask_vtimer_irq_user(struct kvm_vcpu *vcpu)
{
	struct arch_timer_context *vtimer = vcpu_vtimer(vcpu);

	if (!kvm_timer_should_fire(vtimer)) {
		kvm_timer_update_irq(vcpu, false, vtimer);
		if (static_branch_likely(&has_gic_active_state))
			set_vtimer_irq_phys_active(vcpu, false);
		else
			enable_percpu_irq(host_vtimer_irq, host_vtimer_irq_flags);
	}
}

void kvm_timer_sync_hwstate(struct kvm_vcpu *vcpu)
{
	struct arch_timer_cpu *timer = &vcpu->arch.timer_cpu;

	if (unlikely(!timer->enabled))
		return;

	if (unlikely(!irqchip_in_kernel(vcpu->kvm)))
		unmask_vtimer_irq_user(vcpu);
}

int kvm_timer_vcpu_reset(struct kvm_vcpu *vcpu)
{
	struct arch_timer_cpu *timer = &vcpu->arch.timer_cpu;
	struct arch_timer_context *vtimer = vcpu_vtimer(vcpu);
	struct arch_timer_context *ptimer = vcpu_ptimer(vcpu);

	/*
	 * The bits in CNTV_CTL are architecturally reset to UNKNOWN for ARMv8
	 * and to 0 for ARMv7.  We provide an implementation that always
	 * resets the timer to be disabled and unmasked and is compliant with
	 * the ARMv7 architecture.
	 */
	vtimer->cnt_ctl = 0;
	ptimer->cnt_ctl = 0;
	kvm_timer_update_state(vcpu);

	if (timer->enabled && irqchip_in_kernel(vcpu->kvm))
		kvm_vgic_reset_mapped_irq(vcpu, vtimer->irq.irq);

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
	struct arch_timer_context *vtimer = vcpu_vtimer(vcpu);
	struct arch_timer_context *ptimer = vcpu_ptimer(vcpu);

	/* Synchronize cntvoff across all vtimers of a VM. */
	update_vtimer_cntvoff(vcpu, kvm_phys_timer_read());
	vcpu_ptimer(vcpu)->cntvoff = 0;

	INIT_WORK(&timer->expired, kvm_timer_inject_irq_work);
	hrtimer_init(&timer->bg_timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	timer->bg_timer.function = kvm_bg_timer_expire;

	hrtimer_init(&timer->phys_timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	timer->phys_timer.function = kvm_phys_timer_expire;

	vtimer->irq.irq = default_vtimer_irq.irq;
	ptimer->irq.irq = default_ptimer_irq.irq;
}

static void kvm_timer_init_interrupt(void *info)
{
	enable_percpu_irq(host_vtimer_irq, host_vtimer_irq_flags);
}

int kvm_arm_timer_set_reg(struct kvm_vcpu *vcpu, u64 regid, u64 value)
{
	struct arch_timer_context *vtimer = vcpu_vtimer(vcpu);
	struct arch_timer_context *ptimer = vcpu_ptimer(vcpu);

	switch (regid) {
	case KVM_REG_ARM_TIMER_CTL:
		vtimer->cnt_ctl = value & ~ARCH_TIMER_CTRL_IT_STAT;
		break;
	case KVM_REG_ARM_TIMER_CNT:
		update_vtimer_cntvoff(vcpu, kvm_phys_timer_read() - value);
		break;
	case KVM_REG_ARM_TIMER_CVAL:
		vtimer->cnt_cval = value;
		break;
	case KVM_REG_ARM_PTIMER_CTL:
		ptimer->cnt_ctl = value & ~ARCH_TIMER_CTRL_IT_STAT;
		break;
	case KVM_REG_ARM_PTIMER_CVAL:
		ptimer->cnt_cval = value;
		break;

	default:
		return -1;
	}

	kvm_timer_update_state(vcpu);
	return 0;
}

static u64 read_timer_ctl(struct arch_timer_context *timer)
{
	/*
	 * Set ISTATUS bit if it's expired.
	 * Note that according to ARMv8 ARM Issue A.k, ISTATUS bit is
	 * UNKNOWN when ENABLE bit is 0, so we chose to set ISTATUS bit
	 * regardless of ENABLE bit for our implementation convenience.
	 */
	if (!kvm_timer_compute_delta(timer))
		return timer->cnt_ctl | ARCH_TIMER_CTRL_IT_STAT;
	else
		return timer->cnt_ctl;
}

u64 kvm_arm_timer_get_reg(struct kvm_vcpu *vcpu, u64 regid)
{
	struct arch_timer_context *ptimer = vcpu_ptimer(vcpu);
	struct arch_timer_context *vtimer = vcpu_vtimer(vcpu);

	switch (regid) {
	case KVM_REG_ARM_TIMER_CTL:
		return read_timer_ctl(vtimer);
	case KVM_REG_ARM_TIMER_CNT:
		return kvm_phys_timer_read() - vtimer->cntvoff;
	case KVM_REG_ARM_TIMER_CVAL:
		return vtimer->cnt_cval;
	case KVM_REG_ARM_PTIMER_CTL:
		return read_timer_ctl(ptimer);
	case KVM_REG_ARM_PTIMER_CVAL:
		return ptimer->cnt_cval;
	case KVM_REG_ARM_PTIMER_CNT:
		return kvm_phys_timer_read();
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

int kvm_timer_hyp_init(bool has_gic)
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

	if (has_gic) {
		err = irq_set_vcpu_affinity(host_vtimer_irq,
					    kvm_get_running_vcpus());
		if (err) {
			kvm_err("kvm_arch_timer: error setting vcpu affinity\n");
			goto out_free_irq;
		}

		static_branch_enable(&has_gic_active_state);
	}

	kvm_debug("virtual timer IRQ%d\n", host_vtimer_irq);

	cpuhp_setup_state(CPUHP_AP_KVM_ARM_TIMER_STARTING,
			  "kvm/arm/timer:starting", kvm_timer_starting_cpu,
			  kvm_timer_dying_cpu);
	return 0;
out_free_irq:
	free_percpu_irq(host_vtimer_irq, kvm_get_running_vcpus());
	return err;
}

void kvm_timer_vcpu_terminate(struct kvm_vcpu *vcpu)
{
	struct arch_timer_cpu *timer = &vcpu->arch.timer_cpu;
	struct arch_timer_context *vtimer = vcpu_vtimer(vcpu);

	soft_timer_cancel(&timer->bg_timer, &timer->expired);
	soft_timer_cancel(&timer->phys_timer, NULL);
	kvm_vgic_unmap_phys_irq(vcpu, vtimer->irq.irq);
}

static bool timer_irqs_are_valid(struct kvm_vcpu *vcpu)
{
	int vtimer_irq, ptimer_irq;
	int i, ret;

	vtimer_irq = vcpu_vtimer(vcpu)->irq.irq;
	ret = kvm_vgic_set_owner(vcpu, vtimer_irq, vcpu_vtimer(vcpu));
	if (ret)
		return false;

	ptimer_irq = vcpu_ptimer(vcpu)->irq.irq;
	ret = kvm_vgic_set_owner(vcpu, ptimer_irq, vcpu_ptimer(vcpu));
	if (ret)
		return false;

	kvm_for_each_vcpu(i, vcpu, vcpu->kvm) {
		if (vcpu_vtimer(vcpu)->irq.irq != vtimer_irq ||
		    vcpu_ptimer(vcpu)->irq.irq != ptimer_irq)
			return false;
	}

	return true;
}

bool kvm_arch_timer_get_input_level(int vintid)
{
	struct kvm_vcpu *vcpu = kvm_arm_get_running_vcpu();
	struct arch_timer_context *timer;

	if (vintid == vcpu_vtimer(vcpu)->irq.irq)
		timer = vcpu_vtimer(vcpu);
	else
		BUG(); /* We only map the vtimer so far */

	return kvm_timer_should_fire(timer);
}

int kvm_timer_enable(struct kvm_vcpu *vcpu)
{
	struct arch_timer_cpu *timer = &vcpu->arch.timer_cpu;
	struct arch_timer_context *vtimer = vcpu_vtimer(vcpu);
	int ret;

	if (timer->enabled)
		return 0;

	/* Without a VGIC we do not map virtual IRQs to physical IRQs */
	if (!irqchip_in_kernel(vcpu->kvm))
		goto no_vgic;

	if (!vgic_initialized(vcpu->kvm))
		return -ENODEV;

	if (!timer_irqs_are_valid(vcpu)) {
		kvm_debug("incorrectly configured timer irqs\n");
		return -EINVAL;
	}

	ret = kvm_vgic_map_phys_irq(vcpu, host_vtimer_irq, vtimer->irq.irq,
				    kvm_arch_timer_get_input_level);
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

static void set_timer_irqs(struct kvm *kvm, int vtimer_irq, int ptimer_irq)
{
	struct kvm_vcpu *vcpu;
	int i;

	kvm_for_each_vcpu(i, vcpu, kvm) {
		vcpu_vtimer(vcpu)->irq.irq = vtimer_irq;
		vcpu_ptimer(vcpu)->irq.irq = ptimer_irq;
	}
}

int kvm_arm_timer_set_attr(struct kvm_vcpu *vcpu, struct kvm_device_attr *attr)
{
	int __user *uaddr = (int __user *)(long)attr->addr;
	struct arch_timer_context *vtimer = vcpu_vtimer(vcpu);
	struct arch_timer_context *ptimer = vcpu_ptimer(vcpu);
	int irq;

	if (!irqchip_in_kernel(vcpu->kvm))
		return -EINVAL;

	if (get_user(irq, uaddr))
		return -EFAULT;

	if (!(irq_is_ppi(irq)))
		return -EINVAL;

	if (vcpu->arch.timer_cpu.enabled)
		return -EBUSY;

	switch (attr->attr) {
	case KVM_ARM_VCPU_TIMER_IRQ_VTIMER:
		set_timer_irqs(vcpu->kvm, irq, ptimer->irq.irq);
		break;
	case KVM_ARM_VCPU_TIMER_IRQ_PTIMER:
		set_timer_irqs(vcpu->kvm, vtimer->irq.irq, irq);
		break;
	default:
		return -ENXIO;
	}

	return 0;
}

int kvm_arm_timer_get_attr(struct kvm_vcpu *vcpu, struct kvm_device_attr *attr)
{
	int __user *uaddr = (int __user *)(long)attr->addr;
	struct arch_timer_context *timer;
	int irq;

	switch (attr->attr) {
	case KVM_ARM_VCPU_TIMER_IRQ_VTIMER:
		timer = vcpu_vtimer(vcpu);
		break;
	case KVM_ARM_VCPU_TIMER_IRQ_PTIMER:
		timer = vcpu_ptimer(vcpu);
		break;
	default:
		return -ENXIO;
	}

	irq = timer->irq.irq;
	return put_user(irq, uaddr);
}

int kvm_arm_timer_has_attr(struct kvm_vcpu *vcpu, struct kvm_device_attr *attr)
{
	switch (attr->attr) {
	case KVM_ARM_VCPU_TIMER_IRQ_VTIMER:
	case KVM_ARM_VCPU_TIMER_IRQ_PTIMER:
		return 0;
	}

	return -ENXIO;
}
