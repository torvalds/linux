/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Ltd.
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#ifndef __ASM_ARM_KVM_ARCH_TIMER_H
#define __ASM_ARM_KVM_ARCH_TIMER_H

#include <linux/clocksource.h>
#include <linux/hrtimer.h>

enum kvm_arch_timers {
	TIMER_PTIMER,
	TIMER_VTIMER,
	NR_KVM_EL0_TIMERS,
	TIMER_HVTIMER = NR_KVM_EL0_TIMERS,
	TIMER_HPTIMER,
	NR_KVM_TIMERS
};

enum kvm_arch_timer_regs {
	TIMER_REG_CNT,
	TIMER_REG_CVAL,
	TIMER_REG_TVAL,
	TIMER_REG_CTL,
	TIMER_REG_VOFF,
};

struct arch_timer_offset {
	/*
	 * If set, pointer to one of the offsets in the kvm's offset
	 * structure. If NULL, assume a zero offset.
	 */
	u64	*vm_offset;
	/*
	 * If set, pointer to one of the offsets in the vcpu's sysreg
	 * array. If NULL, assume a zero offset.
	 */
	u64	*vcpu_offset;
};

struct arch_timer_vm_data {
	/* Offset applied to the virtual timer/counter */
	u64	voffset;
	/* Offset applied to the physical timer/counter */
	u64	poffset;

	/* The PPI for each timer, global to the VM */
	u8	ppi[NR_KVM_TIMERS];
};

struct arch_timer_context {
	struct kvm_vcpu			*vcpu;

	/* Emulated Timer (may be unused) */
	struct hrtimer			hrtimer;
	u64				ns_frac;

	/* Offset for this counter/timer */
	struct arch_timer_offset	offset;
	/*
	 * We have multiple paths which can save/restore the timer state onto
	 * the hardware, so we need some way of keeping track of where the
	 * latest state is.
	 */
	bool				loaded;

	/* Output level of the timer IRQ */
	struct {
		bool			level;
	} irq;

	/* Duplicated state from arch_timer.c for convenience */
	u32				host_timer_irq;
};

struct timer_map {
	struct arch_timer_context *direct_vtimer;
	struct arch_timer_context *direct_ptimer;
	struct arch_timer_context *emul_vtimer;
	struct arch_timer_context *emul_ptimer;
};

void get_timer_map(struct kvm_vcpu *vcpu, struct timer_map *map);

struct arch_timer_cpu {
	struct arch_timer_context timers[NR_KVM_TIMERS];

	/* Background timer used when the guest is not running */
	struct hrtimer			bg_timer;

	/* Is the timer enabled */
	bool			enabled;
};

int __init kvm_timer_hyp_init(bool has_gic);
int kvm_timer_enable(struct kvm_vcpu *vcpu);
void kvm_timer_vcpu_reset(struct kvm_vcpu *vcpu);
void kvm_timer_vcpu_init(struct kvm_vcpu *vcpu);
void kvm_timer_sync_user(struct kvm_vcpu *vcpu);
bool kvm_timer_should_notify_user(struct kvm_vcpu *vcpu);
void kvm_timer_update_run(struct kvm_vcpu *vcpu);
void kvm_timer_vcpu_terminate(struct kvm_vcpu *vcpu);

void kvm_timer_init_vm(struct kvm *kvm);

u64 kvm_arm_timer_get_reg(struct kvm_vcpu *, u64 regid);
int kvm_arm_timer_set_reg(struct kvm_vcpu *, u64 regid, u64 value);

int kvm_arm_timer_set_attr(struct kvm_vcpu *vcpu, struct kvm_device_attr *attr);
int kvm_arm_timer_get_attr(struct kvm_vcpu *vcpu, struct kvm_device_attr *attr);
int kvm_arm_timer_has_attr(struct kvm_vcpu *vcpu, struct kvm_device_attr *attr);

u64 kvm_phys_timer_read(void);

void kvm_timer_vcpu_load(struct kvm_vcpu *vcpu);
void kvm_timer_vcpu_put(struct kvm_vcpu *vcpu);

void kvm_timer_init_vhe(void);

#define vcpu_timer(v)	(&(v)->arch.timer_cpu)
#define vcpu_get_timer(v,t)	(&vcpu_timer(v)->timers[(t)])
#define vcpu_vtimer(v)	(&(v)->arch.timer_cpu.timers[TIMER_VTIMER])
#define vcpu_ptimer(v)	(&(v)->arch.timer_cpu.timers[TIMER_PTIMER])
#define vcpu_hvtimer(v)	(&(v)->arch.timer_cpu.timers[TIMER_HVTIMER])
#define vcpu_hptimer(v)	(&(v)->arch.timer_cpu.timers[TIMER_HPTIMER])

#define arch_timer_ctx_index(ctx)	((ctx) - vcpu_timer((ctx)->vcpu)->timers)

#define timer_vm_data(ctx)		(&(ctx)->vcpu->kvm->arch.timer_data)
#define timer_irq(ctx)			(timer_vm_data(ctx)->ppi[arch_timer_ctx_index(ctx)])

u64 kvm_arm_timer_read_sysreg(struct kvm_vcpu *vcpu,
			      enum kvm_arch_timers tmr,
			      enum kvm_arch_timer_regs treg);
void kvm_arm_timer_write_sysreg(struct kvm_vcpu *vcpu,
				enum kvm_arch_timers tmr,
				enum kvm_arch_timer_regs treg,
				u64 val);

/* Needed for tracing */
u32 timer_get_ctl(struct arch_timer_context *ctxt);
u64 timer_get_cval(struct arch_timer_context *ctxt);

/* CPU HP callbacks */
void kvm_timer_cpu_up(void);
void kvm_timer_cpu_down(void);

/* CNTKCTL_EL1 valid bits as of DDI0487J.a */
#define CNTKCTL_VALID_BITS	(BIT(17) | GENMASK_ULL(9, 0))

static inline bool has_cntpoff(void)
{
	return (has_vhe() && cpus_have_final_cap(ARM64_HAS_ECV_CNTPOFF));
}

#endif
