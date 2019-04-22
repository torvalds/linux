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

#ifndef __ASM_ARM_KVM_ARCH_TIMER_H
#define __ASM_ARM_KVM_ARCH_TIMER_H

#include <linux/clocksource.h>
#include <linux/hrtimer.h>

enum kvm_arch_timers {
	TIMER_PTIMER,
	TIMER_VTIMER,
	NR_KVM_TIMERS
};

enum kvm_arch_timer_regs {
	TIMER_REG_CNT,
	TIMER_REG_CVAL,
	TIMER_REG_TVAL,
	TIMER_REG_CTL,
};

struct arch_timer_context {
	struct kvm_vcpu			*vcpu;

	/* Registers: control register, timer value */
	u32				cnt_ctl;
	u64				cnt_cval;

	/* Timer IRQ */
	struct kvm_irq_level		irq;

	/* Virtual offset */
	u64				cntvoff;

	/* Emulated Timer (may be unused) */
	struct hrtimer			hrtimer;

	/*
	 * We have multiple paths which can save/restore the timer state onto
	 * the hardware, so we need some way of keeping track of where the
	 * latest state is.
	 */
	bool				loaded;

	/* Duplicated state from arch_timer.c for convenience */
	u32				host_timer_irq;
	u32				host_timer_irq_flags;
};

struct timer_map {
	struct arch_timer_context *direct_vtimer;
	struct arch_timer_context *direct_ptimer;
	struct arch_timer_context *emul_ptimer;
};

struct arch_timer_cpu {
	struct arch_timer_context timers[NR_KVM_TIMERS];

	/* Background timer used when the guest is not running */
	struct hrtimer			bg_timer;

	/* Is the timer enabled */
	bool			enabled;
};

int kvm_timer_hyp_init(bool);
int kvm_timer_enable(struct kvm_vcpu *vcpu);
int kvm_timer_vcpu_reset(struct kvm_vcpu *vcpu);
void kvm_timer_vcpu_init(struct kvm_vcpu *vcpu);
void kvm_timer_sync_hwstate(struct kvm_vcpu *vcpu);
bool kvm_timer_should_notify_user(struct kvm_vcpu *vcpu);
void kvm_timer_update_run(struct kvm_vcpu *vcpu);
void kvm_timer_vcpu_terminate(struct kvm_vcpu *vcpu);

u64 kvm_arm_timer_get_reg(struct kvm_vcpu *, u64 regid);
int kvm_arm_timer_set_reg(struct kvm_vcpu *, u64 regid, u64 value);

int kvm_arm_timer_set_attr(struct kvm_vcpu *vcpu, struct kvm_device_attr *attr);
int kvm_arm_timer_get_attr(struct kvm_vcpu *vcpu, struct kvm_device_attr *attr);
int kvm_arm_timer_has_attr(struct kvm_vcpu *vcpu, struct kvm_device_attr *attr);

bool kvm_timer_is_pending(struct kvm_vcpu *vcpu);

u64 kvm_phys_timer_read(void);

void kvm_timer_vcpu_load(struct kvm_vcpu *vcpu);
void kvm_timer_vcpu_put(struct kvm_vcpu *vcpu);

void kvm_timer_init_vhe(void);

bool kvm_arch_timer_get_input_level(int vintid);

#define vcpu_timer(v)	(&(v)->arch.timer_cpu)
#define vcpu_get_timer(v,t)	(&vcpu_timer(v)->timers[(t)])
#define vcpu_vtimer(v)	(&(v)->arch.timer_cpu.timers[TIMER_VTIMER])
#define vcpu_ptimer(v)	(&(v)->arch.timer_cpu.timers[TIMER_PTIMER])

#define arch_timer_ctx_index(ctx)	((ctx) - vcpu_timer((ctx)->vcpu)->timers)

u64 kvm_arm_timer_read_sysreg(struct kvm_vcpu *vcpu,
			      enum kvm_arch_timers tmr,
			      enum kvm_arch_timer_regs treg);
void kvm_arm_timer_write_sysreg(struct kvm_vcpu *vcpu,
				enum kvm_arch_timers tmr,
				enum kvm_arch_timer_regs treg,
				u64 val);

#endif
