// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 ARM Ltd.
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#include <linux/cpu.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/uaccess.h>

#include <clocksource/arm_arch_timer.h>
#include <asm/arch_timer.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_hyp.h>

#include <kvm/arm_vgic.h>
#include <kvm/arm_arch_timer.h>

#include "trace.h"

static struct timecounter *timecounter;
static unsigned int host_vtimer_irq;
static unsigned int host_ptimer_irq;
static u32 host_vtimer_irq_flags;
static u32 host_ptimer_irq_flags;

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
static void kvm_arm_timer_write(struct kvm_vcpu *vcpu,
				struct arch_timer_context *timer,
				enum kvm_arch_timer_regs treg,
				u64 val);
static u64 kvm_arm_timer_read(struct kvm_vcpu *vcpu,
			      struct arch_timer_context *timer,
			      enum kvm_arch_timer_regs treg);

u64 kvm_phys_timer_read(void)
{
	return timecounter->cc->read(timecounter->cc);
}

static void get_timer_map(struct kvm_vcpu *vcpu, struct timer_map *map)
{
	if (has_vhe()) {
		map->direct_vtimer = vcpu_vtimer(vcpu);
		map->direct_ptimer = vcpu_ptimer(vcpu);
		map->emul_ptimer = NULL;
	} else {
		map->direct_vtimer = vcpu_vtimer(vcpu);
		map->direct_ptimer = NULL;
		map->emul_ptimer = vcpu_ptimer(vcpu);
	}

	trace_kvm_get_timer_map(vcpu->vcpu_id, map);
}

static inline bool userspace_irqchip(struct kvm *kvm)
{
	return static_branch_unlikely(&userspace_irqchip_in_use) &&
		unlikely(!irqchip_in_kernel(kvm));
}

static void soft_timer_start(struct hrtimer *hrt, u64 ns)
{
	hrtimer_start(hrt, ktime_add_ns(ktime_get(), ns),
		      HRTIMER_MODE_ABS_HARD);
}

static void soft_timer_cancel(struct hrtimer *hrt)
{
	hrtimer_cancel(hrt);
}

static irqreturn_t kvm_arch_timer_handler(int irq, void *dev_id)
{
	struct kvm_vcpu *vcpu = *(struct kvm_vcpu **)dev_id;
	struct arch_timer_context *ctx;
	struct timer_map map;

	/*
	 * We may see a timer interrupt after vcpu_put() has been called which
	 * sets the CPU's vcpu pointer to NULL, because even though the timer
	 * has been disabled in timer_save_state(), the hardware interrupt
	 * signal may not have been retired from the interrupt controller yet.
	 */
	if (!vcpu)
		return IRQ_HANDLED;

	get_timer_map(vcpu, &map);

	if (irq == host_vtimer_irq)
		ctx = map.direct_vtimer;
	else
		ctx = map.direct_ptimer;

	if (kvm_timer_should_fire(ctx))
		kvm_timer_update_irq(vcpu, true, ctx);

	if (userspace_irqchip(vcpu->kvm) &&
	    !static_branch_unlikely(&has_gic_active_state))
		disable_percpu_irq(host_vtimer_irq);

	return IRQ_HANDLED;
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
	WARN_ON(timer_ctx && timer_ctx->loaded);
	return timer_ctx &&
	       !(timer_ctx->cnt_ctl & ARCH_TIMER_CTRL_IT_MASK) &&
		(timer_ctx->cnt_ctl & ARCH_TIMER_CTRL_ENABLE);
}

/*
 * Returns the earliest expiration time in ns among guest timers.
 * Note that it will return 0 if none of timers can fire.
 */
static u64 kvm_timer_earliest_exp(struct kvm_vcpu *vcpu)
{
	u64 min_delta = ULLONG_MAX;
	int i;

	for (i = 0; i < NR_KVM_TIMERS; i++) {
		struct arch_timer_context *ctx = &vcpu->arch.timer_cpu.timers[i];

		WARN(ctx->loaded, "timer %d loaded\n", i);
		if (kvm_timer_irq_can_fire(ctx))
			min_delta = min(min_delta, kvm_timer_compute_delta(ctx));
	}

	/* If none of timers can fire, then return 0 */
	if (min_delta == ULLONG_MAX)
		return 0;

	return min_delta;
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

	kvm_vcpu_wake_up(vcpu);
	return HRTIMER_NORESTART;
}

static enum hrtimer_restart kvm_hrtimer_expire(struct hrtimer *hrt)
{
	struct arch_timer_context *ctx;
	struct kvm_vcpu *vcpu;
	u64 ns;

	ctx = container_of(hrt, struct arch_timer_context, hrtimer);
	vcpu = ctx->vcpu;

	trace_kvm_timer_hrtimer_expire(ctx);

	/*
	 * Check that the timer has really expired from the guest's
	 * PoV (NTP on the host may have forced it to expire
	 * early). If not ready, schedule for a later time.
	 */
	ns = kvm_timer_compute_delta(ctx);
	if (unlikely(ns)) {
		hrtimer_forward_now(hrt, ns_to_ktime(ns));
		return HRTIMER_RESTART;
	}

	kvm_timer_update_irq(vcpu, true, ctx);
	return HRTIMER_NORESTART;
}

static bool kvm_timer_should_fire(struct arch_timer_context *timer_ctx)
{
	enum kvm_arch_timers index;
	u64 cval, now;

	if (!timer_ctx)
		return false;

	index = arch_timer_ctx_index(timer_ctx);

	if (timer_ctx->loaded) {
		u32 cnt_ctl = 0;

		switch (index) {
		case TIMER_VTIMER:
			cnt_ctl = read_sysreg_el0(SYS_CNTV_CTL);
			break;
		case TIMER_PTIMER:
			cnt_ctl = read_sysreg_el0(SYS_CNTP_CTL);
			break;
		case NR_KVM_TIMERS:
			/* GCC is braindead */
			cnt_ctl = 0;
			break;
		}

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
	struct timer_map map;

	get_timer_map(vcpu, &map);

	return kvm_timer_should_fire(map.direct_vtimer) ||
	       kvm_timer_should_fire(map.direct_ptimer) ||
	       kvm_timer_should_fire(map.emul_ptimer);
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

/* Only called for a fully emulated timer */
static void timer_emulate(struct arch_timer_context *ctx)
{
	bool should_fire = kvm_timer_should_fire(ctx);

	trace_kvm_timer_emulate(ctx, should_fire);

	if (should_fire != ctx->irq.level) {
		kvm_timer_update_irq(ctx->vcpu, should_fire, ctx);
		return;
	}

	/*
	 * If the timer can fire now, we don't need to have a soft timer
	 * scheduled for the future.  If the timer cannot fire at all,
	 * then we also don't need a soft timer.
	 */
	if (!kvm_timer_irq_can_fire(ctx)) {
		soft_timer_cancel(&ctx->hrtimer);
		return;
	}

	soft_timer_start(&ctx->hrtimer, kvm_timer_compute_delta(ctx));
}

static void timer_save_state(struct arch_timer_context *ctx)
{
	struct arch_timer_cpu *timer = vcpu_timer(ctx->vcpu);
	enum kvm_arch_timers index = arch_timer_ctx_index(ctx);
	unsigned long flags;

	if (!timer->enabled)
		return;

	local_irq_save(flags);

	if (!ctx->loaded)
		goto out;

	switch (index) {
	case TIMER_VTIMER:
		ctx->cnt_ctl = read_sysreg_el0(SYS_CNTV_CTL);
		ctx->cnt_cval = read_sysreg_el0(SYS_CNTV_CVAL);

		/* Disable the timer */
		write_sysreg_el0(0, SYS_CNTV_CTL);
		isb();

		break;
	case TIMER_PTIMER:
		ctx->cnt_ctl = read_sysreg_el0(SYS_CNTP_CTL);
		ctx->cnt_cval = read_sysreg_el0(SYS_CNTP_CVAL);

		/* Disable the timer */
		write_sysreg_el0(0, SYS_CNTP_CTL);
		isb();

		break;
	case NR_KVM_TIMERS:
		BUG();
	}

	trace_kvm_timer_save_state(ctx);

	ctx->loaded = false;
out:
	local_irq_restore(flags);
}

/*
 * Schedule the background timer before calling kvm_vcpu_block, so that this
 * thread is removed from its waitqueue and made runnable when there's a timer
 * interrupt to handle.
 */
static void kvm_timer_blocking(struct kvm_vcpu *vcpu)
{
	struct arch_timer_cpu *timer = vcpu_timer(vcpu);
	struct timer_map map;

	get_timer_map(vcpu, &map);

	/*
	 * If no timers are capable of raising interrupts (disabled or
	 * masked), then there's no more work for us to do.
	 */
	if (!kvm_timer_irq_can_fire(map.direct_vtimer) &&
	    !kvm_timer_irq_can_fire(map.direct_ptimer) &&
	    !kvm_timer_irq_can_fire(map.emul_ptimer))
		return;

	/*
	 * At least one guest time will expire. Schedule a background timer.
	 * Set the earliest expiration time among the guest timers.
	 */
	soft_timer_start(&timer->bg_timer, kvm_timer_earliest_exp(vcpu));
}

static void kvm_timer_unblocking(struct kvm_vcpu *vcpu)
{
	struct arch_timer_cpu *timer = vcpu_timer(vcpu);

	soft_timer_cancel(&timer->bg_timer);
}

static void timer_restore_state(struct arch_timer_context *ctx)
{
	struct arch_timer_cpu *timer = vcpu_timer(ctx->vcpu);
	enum kvm_arch_timers index = arch_timer_ctx_index(ctx);
	unsigned long flags;

	if (!timer->enabled)
		return;

	local_irq_save(flags);

	if (ctx->loaded)
		goto out;

	switch (index) {
	case TIMER_VTIMER:
		write_sysreg_el0(ctx->cnt_cval, SYS_CNTV_CVAL);
		isb();
		write_sysreg_el0(ctx->cnt_ctl, SYS_CNTV_CTL);
		break;
	case TIMER_PTIMER:
		write_sysreg_el0(ctx->cnt_cval, SYS_CNTP_CVAL);
		isb();
		write_sysreg_el0(ctx->cnt_ctl, SYS_CNTP_CTL);
		break;
	case NR_KVM_TIMERS:
		BUG();
	}

	trace_kvm_timer_restore_state(ctx);

	ctx->loaded = true;
out:
	local_irq_restore(flags);
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

static inline void set_timer_irq_phys_active(struct arch_timer_context *ctx, bool active)
{
	int r;
	r = irq_set_irqchip_state(ctx->host_timer_irq, IRQCHIP_STATE_ACTIVE, active);
	WARN_ON(r);
}

static void kvm_timer_vcpu_load_gic(struct arch_timer_context *ctx)
{
	struct kvm_vcpu *vcpu = ctx->vcpu;
	bool phys_active = false;

	/*
	 * Update the timer output so that it is likely to match the
	 * state we're about to restore. If the timer expires between
	 * this point and the register restoration, we'll take the
	 * interrupt anyway.
	 */
	kvm_timer_update_irq(ctx->vcpu, kvm_timer_should_fire(ctx), ctx);

	if (irqchip_in_kernel(vcpu->kvm))
		phys_active = kvm_vgic_map_is_active(vcpu, ctx->irq.irq);

	phys_active |= ctx->irq.level;

	set_timer_irq_phys_active(ctx, phys_active);
}

static void kvm_timer_vcpu_load_nogic(struct kvm_vcpu *vcpu)
{
	struct arch_timer_context *vtimer = vcpu_vtimer(vcpu);

	/*
	 * Update the timer output so that it is likely to match the
	 * state we're about to restore. If the timer expires between
	 * this point and the register restoration, we'll take the
	 * interrupt anyway.
	 */
	kvm_timer_update_irq(vcpu, kvm_timer_should_fire(vtimer), vtimer);

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
	struct arch_timer_cpu *timer = vcpu_timer(vcpu);
	struct timer_map map;

	if (unlikely(!timer->enabled))
		return;

	get_timer_map(vcpu, &map);

	if (static_branch_likely(&has_gic_active_state)) {
		kvm_timer_vcpu_load_gic(map.direct_vtimer);
		if (map.direct_ptimer)
			kvm_timer_vcpu_load_gic(map.direct_ptimer);
	} else {
		kvm_timer_vcpu_load_nogic(vcpu);
	}

	set_cntvoff(map.direct_vtimer->cntvoff);

	kvm_timer_unblocking(vcpu);

	timer_restore_state(map.direct_vtimer);
	if (map.direct_ptimer)
		timer_restore_state(map.direct_ptimer);

	if (map.emul_ptimer)
		timer_emulate(map.emul_ptimer);
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
	struct arch_timer_cpu *timer = vcpu_timer(vcpu);
	struct timer_map map;

	if (unlikely(!timer->enabled))
		return;

	get_timer_map(vcpu, &map);

	timer_save_state(map.direct_vtimer);
	if (map.direct_ptimer)
		timer_save_state(map.direct_ptimer);

	/*
	 * Cancel soft timer emulation, because the only case where we
	 * need it after a vcpu_put is in the context of a sleeping VCPU, and
	 * in that case we already factor in the deadline for the physical
	 * timer when scheduling the bg_timer.
	 *
	 * In any case, we re-schedule the hrtimer for the physical timer when
	 * coming back to the VCPU thread in kvm_timer_vcpu_load().
	 */
	if (map.emul_ptimer)
		soft_timer_cancel(&map.emul_ptimer->hrtimer);

	if (swait_active(kvm_arch_vcpu_wq(vcpu)))
		kvm_timer_blocking(vcpu);

	/*
	 * The kernel may decide to run userspace after calling vcpu_put, so
	 * we reset cntvoff to 0 to ensure a consistent read between user
	 * accesses to the virtual counter and kernel access to the physical
	 * counter of non-VHE case. For VHE, the virtual counter uses a fixed
	 * virtual offset of zero, so no need to zero CNTVOFF_EL2 register.
	 */
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
			set_timer_irq_phys_active(vtimer, false);
		else
			enable_percpu_irq(host_vtimer_irq, host_vtimer_irq_flags);
	}
}

void kvm_timer_sync_hwstate(struct kvm_vcpu *vcpu)
{
	struct arch_timer_cpu *timer = vcpu_timer(vcpu);

	if (unlikely(!timer->enabled))
		return;

	if (unlikely(!irqchip_in_kernel(vcpu->kvm)))
		unmask_vtimer_irq_user(vcpu);
}

int kvm_timer_vcpu_reset(struct kvm_vcpu *vcpu)
{
	struct arch_timer_cpu *timer = vcpu_timer(vcpu);
	struct timer_map map;

	get_timer_map(vcpu, &map);

	/*
	 * The bits in CNTV_CTL are architecturally reset to UNKNOWN for ARMv8
	 * and to 0 for ARMv7.  We provide an implementation that always
	 * resets the timer to be disabled and unmasked and is compliant with
	 * the ARMv7 architecture.
	 */
	vcpu_vtimer(vcpu)->cnt_ctl = 0;
	vcpu_ptimer(vcpu)->cnt_ctl = 0;

	if (timer->enabled) {
		kvm_timer_update_irq(vcpu, false, vcpu_vtimer(vcpu));
		kvm_timer_update_irq(vcpu, false, vcpu_ptimer(vcpu));

		if (irqchip_in_kernel(vcpu->kvm)) {
			kvm_vgic_reset_mapped_irq(vcpu, map.direct_vtimer->irq.irq);
			if (map.direct_ptimer)
				kvm_vgic_reset_mapped_irq(vcpu, map.direct_ptimer->irq.irq);
		}
	}

	if (map.emul_ptimer)
		soft_timer_cancel(&map.emul_ptimer->hrtimer);

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
	struct arch_timer_cpu *timer = vcpu_timer(vcpu);
	struct arch_timer_context *vtimer = vcpu_vtimer(vcpu);
	struct arch_timer_context *ptimer = vcpu_ptimer(vcpu);

	/* Synchronize cntvoff across all vtimers of a VM. */
	update_vtimer_cntvoff(vcpu, kvm_phys_timer_read());
	ptimer->cntvoff = 0;

	hrtimer_init(&timer->bg_timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS_HARD);
	timer->bg_timer.function = kvm_bg_timer_expire;

	hrtimer_init(&vtimer->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS_HARD);
	hrtimer_init(&ptimer->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS_HARD);
	vtimer->hrtimer.function = kvm_hrtimer_expire;
	ptimer->hrtimer.function = kvm_hrtimer_expire;

	vtimer->irq.irq = default_vtimer_irq.irq;
	ptimer->irq.irq = default_ptimer_irq.irq;

	vtimer->host_timer_irq = host_vtimer_irq;
	ptimer->host_timer_irq = host_ptimer_irq;

	vtimer->host_timer_irq_flags = host_vtimer_irq_flags;
	ptimer->host_timer_irq_flags = host_ptimer_irq_flags;

	vtimer->vcpu = vcpu;
	ptimer->vcpu = vcpu;
}

static void kvm_timer_init_interrupt(void *info)
{
	enable_percpu_irq(host_vtimer_irq, host_vtimer_irq_flags);
	enable_percpu_irq(host_ptimer_irq, host_ptimer_irq_flags);
}

int kvm_arm_timer_set_reg(struct kvm_vcpu *vcpu, u64 regid, u64 value)
{
	struct arch_timer_context *timer;

	switch (regid) {
	case KVM_REG_ARM_TIMER_CTL:
		timer = vcpu_vtimer(vcpu);
		kvm_arm_timer_write(vcpu, timer, TIMER_REG_CTL, value);
		break;
	case KVM_REG_ARM_TIMER_CNT:
		timer = vcpu_vtimer(vcpu);
		update_vtimer_cntvoff(vcpu, kvm_phys_timer_read() - value);
		break;
	case KVM_REG_ARM_TIMER_CVAL:
		timer = vcpu_vtimer(vcpu);
		kvm_arm_timer_write(vcpu, timer, TIMER_REG_CVAL, value);
		break;
	case KVM_REG_ARM_PTIMER_CTL:
		timer = vcpu_ptimer(vcpu);
		kvm_arm_timer_write(vcpu, timer, TIMER_REG_CTL, value);
		break;
	case KVM_REG_ARM_PTIMER_CVAL:
		timer = vcpu_ptimer(vcpu);
		kvm_arm_timer_write(vcpu, timer, TIMER_REG_CVAL, value);
		break;

	default:
		return -1;
	}

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
	switch (regid) {
	case KVM_REG_ARM_TIMER_CTL:
		return kvm_arm_timer_read(vcpu,
					  vcpu_vtimer(vcpu), TIMER_REG_CTL);
	case KVM_REG_ARM_TIMER_CNT:
		return kvm_arm_timer_read(vcpu,
					  vcpu_vtimer(vcpu), TIMER_REG_CNT);
	case KVM_REG_ARM_TIMER_CVAL:
		return kvm_arm_timer_read(vcpu,
					  vcpu_vtimer(vcpu), TIMER_REG_CVAL);
	case KVM_REG_ARM_PTIMER_CTL:
		return kvm_arm_timer_read(vcpu,
					  vcpu_ptimer(vcpu), TIMER_REG_CTL);
	case KVM_REG_ARM_PTIMER_CNT:
		return kvm_arm_timer_read(vcpu,
					  vcpu_ptimer(vcpu), TIMER_REG_CNT);
	case KVM_REG_ARM_PTIMER_CVAL:
		return kvm_arm_timer_read(vcpu,
					  vcpu_ptimer(vcpu), TIMER_REG_CVAL);
	}
	return (u64)-1;
}

static u64 kvm_arm_timer_read(struct kvm_vcpu *vcpu,
			      struct arch_timer_context *timer,
			      enum kvm_arch_timer_regs treg)
{
	u64 val;

	switch (treg) {
	case TIMER_REG_TVAL:
		val = timer->cnt_cval - kvm_phys_timer_read() + timer->cntvoff;
		val &= lower_32_bits(val);
		break;

	case TIMER_REG_CTL:
		val = read_timer_ctl(timer);
		break;

	case TIMER_REG_CVAL:
		val = timer->cnt_cval;
		break;

	case TIMER_REG_CNT:
		val = kvm_phys_timer_read() - timer->cntvoff;
		break;

	default:
		BUG();
	}

	return val;
}

u64 kvm_arm_timer_read_sysreg(struct kvm_vcpu *vcpu,
			      enum kvm_arch_timers tmr,
			      enum kvm_arch_timer_regs treg)
{
	u64 val;

	preempt_disable();
	kvm_timer_vcpu_put(vcpu);

	val = kvm_arm_timer_read(vcpu, vcpu_get_timer(vcpu, tmr), treg);

	kvm_timer_vcpu_load(vcpu);
	preempt_enable();

	return val;
}

static void kvm_arm_timer_write(struct kvm_vcpu *vcpu,
				struct arch_timer_context *timer,
				enum kvm_arch_timer_regs treg,
				u64 val)
{
	switch (treg) {
	case TIMER_REG_TVAL:
		timer->cnt_cval = kvm_phys_timer_read() - timer->cntvoff + (s32)val;
		break;

	case TIMER_REG_CTL:
		timer->cnt_ctl = val & ~ARCH_TIMER_CTRL_IT_STAT;
		break;

	case TIMER_REG_CVAL:
		timer->cnt_cval = val;
		break;

	default:
		BUG();
	}
}

void kvm_arm_timer_write_sysreg(struct kvm_vcpu *vcpu,
				enum kvm_arch_timers tmr,
				enum kvm_arch_timer_regs treg,
				u64 val)
{
	preempt_disable();
	kvm_timer_vcpu_put(vcpu);

	kvm_arm_timer_write(vcpu, vcpu_get_timer(vcpu, tmr), treg, val);

	kvm_timer_vcpu_load(vcpu);
	preempt_enable();
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

	/* First, do the virtual EL1 timer irq */

	if (info->virtual_irq <= 0) {
		kvm_err("kvm_arch_timer: invalid virtual timer IRQ: %d\n",
			info->virtual_irq);
		return -ENODEV;
	}
	host_vtimer_irq = info->virtual_irq;

	host_vtimer_irq_flags = irq_get_trigger_type(host_vtimer_irq);
	if (host_vtimer_irq_flags != IRQF_TRIGGER_HIGH &&
	    host_vtimer_irq_flags != IRQF_TRIGGER_LOW) {
		kvm_err("Invalid trigger for vtimer IRQ%d, assuming level low\n",
			host_vtimer_irq);
		host_vtimer_irq_flags = IRQF_TRIGGER_LOW;
	}

	err = request_percpu_irq(host_vtimer_irq, kvm_arch_timer_handler,
				 "kvm guest vtimer", kvm_get_running_vcpus());
	if (err) {
		kvm_err("kvm_arch_timer: can't request vtimer interrupt %d (%d)\n",
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

	/* Now let's do the physical EL1 timer irq */

	if (info->physical_irq > 0) {
		host_ptimer_irq = info->physical_irq;
		host_ptimer_irq_flags = irq_get_trigger_type(host_ptimer_irq);
		if (host_ptimer_irq_flags != IRQF_TRIGGER_HIGH &&
		    host_ptimer_irq_flags != IRQF_TRIGGER_LOW) {
			kvm_err("Invalid trigger for ptimer IRQ%d, assuming level low\n",
				host_ptimer_irq);
			host_ptimer_irq_flags = IRQF_TRIGGER_LOW;
		}

		err = request_percpu_irq(host_ptimer_irq, kvm_arch_timer_handler,
					 "kvm guest ptimer", kvm_get_running_vcpus());
		if (err) {
			kvm_err("kvm_arch_timer: can't request ptimer interrupt %d (%d)\n",
				host_ptimer_irq, err);
			return err;
		}

		if (has_gic) {
			err = irq_set_vcpu_affinity(host_ptimer_irq,
						    kvm_get_running_vcpus());
			if (err) {
				kvm_err("kvm_arch_timer: error setting vcpu affinity\n");
				goto out_free_irq;
			}
		}

		kvm_debug("physical timer IRQ%d\n", host_ptimer_irq);
	} else if (has_vhe()) {
		kvm_err("kvm_arch_timer: invalid physical timer IRQ: %d\n",
			info->physical_irq);
		err = -ENODEV;
		goto out_free_irq;
	}

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
	struct arch_timer_cpu *timer = vcpu_timer(vcpu);

	soft_timer_cancel(&timer->bg_timer);
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
	struct kvm_vcpu *vcpu = kvm_get_running_vcpu();
	struct arch_timer_context *timer;

	if (vintid == vcpu_vtimer(vcpu)->irq.irq)
		timer = vcpu_vtimer(vcpu);
	else if (vintid == vcpu_ptimer(vcpu)->irq.irq)
		timer = vcpu_ptimer(vcpu);
	else
		BUG();

	return kvm_timer_should_fire(timer);
}

int kvm_timer_enable(struct kvm_vcpu *vcpu)
{
	struct arch_timer_cpu *timer = vcpu_timer(vcpu);
	struct timer_map map;
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

	get_timer_map(vcpu, &map);

	ret = kvm_vgic_map_phys_irq(vcpu,
				    map.direct_vtimer->host_timer_irq,
				    map.direct_vtimer->irq.irq,
				    kvm_arch_timer_get_input_level);
	if (ret)
		return ret;

	if (map.direct_ptimer) {
		ret = kvm_vgic_map_phys_irq(vcpu,
					    map.direct_ptimer->host_timer_irq,
					    map.direct_ptimer->irq.irq,
					    kvm_arch_timer_get_input_level);
	}

	if (ret)
		return ret;

no_vgic:
	timer->enabled = 1;
	return 0;
}

/*
 * On VHE system, we only need to configure the EL2 timer trap register once,
 * not for every world switch.
 * The host kernel runs at EL2 with HCR_EL2.TGE == 1,
 * and this makes those bits have no effect for the host kernel execution.
 */
void kvm_timer_init_vhe(void)
{
	/* When HCR_EL2.E2H ==1, EL1PCEN and EL1PCTEN are shifted by 10 */
	u32 cnthctl_shift = 10;
	u64 val;

	/*
	 * VHE systems allow the guest direct access to the EL1 physical
	 * timer/counter.
	 */
	val = read_sysreg(cnthctl_el2);
	val |= (CNTHCTL_EL1PCEN << cnthctl_shift);
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
