/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2015 Linaro Ltd.
 * Author: Shannon Zhao <shannon.zhao@linaro.org>
 */

#ifndef __ASM_ARM_KVM_PMU_H
#define __ASM_ARM_KVM_PMU_H

#include <linux/perf_event.h>
#include <asm/perf_event.h>

#define ARMV8_PMU_CYCLE_IDX		(ARMV8_PMU_MAX_COUNTERS - 1)
#define ARMV8_PMU_MAX_COUNTER_PAIRS	((ARMV8_PMU_MAX_COUNTERS + 1) >> 1)

DECLARE_STATIC_KEY_FALSE(kvm_arm_pmu_available);

static __always_inline bool kvm_arm_support_pmu_v3(void)
{
	return static_branch_likely(&kvm_arm_pmu_available);
}

#ifdef CONFIG_HW_PERF_EVENTS

struct kvm_pmc {
	u8 idx;	/* index into the pmu->pmc array */
	struct perf_event *perf_event;
};

struct kvm_pmu {
	int irq_num;
	struct kvm_pmc pmc[ARMV8_PMU_MAX_COUNTERS];
	DECLARE_BITMAP(chained, ARMV8_PMU_MAX_COUNTER_PAIRS);
	bool created;
	bool irq_level;
	struct irq_work overflow_work;
};

#define kvm_arm_pmu_irq_initialized(v)	((v)->arch.pmu.irq_num >= VGIC_NR_SGIS)
u64 kvm_pmu_get_counter_value(struct kvm_vcpu *vcpu, u64 select_idx);
void kvm_pmu_set_counter_value(struct kvm_vcpu *vcpu, u64 select_idx, u64 val);
u64 kvm_pmu_valid_counter_mask(struct kvm_vcpu *vcpu);
u64 kvm_pmu_get_pmceid(struct kvm_vcpu *vcpu, bool pmceid1);
void kvm_pmu_vcpu_init(struct kvm_vcpu *vcpu);
void kvm_pmu_vcpu_reset(struct kvm_vcpu *vcpu);
void kvm_pmu_vcpu_destroy(struct kvm_vcpu *vcpu);
void kvm_pmu_disable_counter_mask(struct kvm_vcpu *vcpu, u64 val);
void kvm_pmu_enable_counter_mask(struct kvm_vcpu *vcpu, u64 val);
void kvm_pmu_flush_hwstate(struct kvm_vcpu *vcpu);
void kvm_pmu_sync_hwstate(struct kvm_vcpu *vcpu);
bool kvm_pmu_should_notify_user(struct kvm_vcpu *vcpu);
void kvm_pmu_update_run(struct kvm_vcpu *vcpu);
void kvm_pmu_software_increment(struct kvm_vcpu *vcpu, u64 val);
void kvm_pmu_handle_pmcr(struct kvm_vcpu *vcpu, u64 val);
void kvm_pmu_set_counter_event_type(struct kvm_vcpu *vcpu, u64 data,
				    u64 select_idx);
int kvm_arm_pmu_v3_set_attr(struct kvm_vcpu *vcpu,
			    struct kvm_device_attr *attr);
int kvm_arm_pmu_v3_get_attr(struct kvm_vcpu *vcpu,
			    struct kvm_device_attr *attr);
int kvm_arm_pmu_v3_has_attr(struct kvm_vcpu *vcpu,
			    struct kvm_device_attr *attr);
int kvm_arm_pmu_v3_enable(struct kvm_vcpu *vcpu);
#else
struct kvm_pmu {
};

#define kvm_arm_pmu_irq_initialized(v)	(false)
static inline u64 kvm_pmu_get_counter_value(struct kvm_vcpu *vcpu,
					    u64 select_idx)
{
	return 0;
}
static inline void kvm_pmu_set_counter_value(struct kvm_vcpu *vcpu,
					     u64 select_idx, u64 val) {}
static inline u64 kvm_pmu_valid_counter_mask(struct kvm_vcpu *vcpu)
{
	return 0;
}
static inline void kvm_pmu_vcpu_init(struct kvm_vcpu *vcpu) {}
static inline void kvm_pmu_vcpu_reset(struct kvm_vcpu *vcpu) {}
static inline void kvm_pmu_vcpu_destroy(struct kvm_vcpu *vcpu) {}
static inline void kvm_pmu_disable_counter_mask(struct kvm_vcpu *vcpu, u64 val) {}
static inline void kvm_pmu_enable_counter_mask(struct kvm_vcpu *vcpu, u64 val) {}
static inline void kvm_pmu_flush_hwstate(struct kvm_vcpu *vcpu) {}
static inline void kvm_pmu_sync_hwstate(struct kvm_vcpu *vcpu) {}
static inline bool kvm_pmu_should_notify_user(struct kvm_vcpu *vcpu)
{
	return false;
}
static inline void kvm_pmu_update_run(struct kvm_vcpu *vcpu) {}
static inline void kvm_pmu_software_increment(struct kvm_vcpu *vcpu, u64 val) {}
static inline void kvm_pmu_handle_pmcr(struct kvm_vcpu *vcpu, u64 val) {}
static inline void kvm_pmu_set_counter_event_type(struct kvm_vcpu *vcpu,
						  u64 data, u64 select_idx) {}
static inline int kvm_arm_pmu_v3_set_attr(struct kvm_vcpu *vcpu,
					  struct kvm_device_attr *attr)
{
	return -ENXIO;
}
static inline int kvm_arm_pmu_v3_get_attr(struct kvm_vcpu *vcpu,
					  struct kvm_device_attr *attr)
{
	return -ENXIO;
}
static inline int kvm_arm_pmu_v3_has_attr(struct kvm_vcpu *vcpu,
					  struct kvm_device_attr *attr)
{
	return -ENXIO;
}
static inline int kvm_arm_pmu_v3_enable(struct kvm_vcpu *vcpu)
{
	return 0;
}
static inline u64 kvm_pmu_get_pmceid(struct kvm_vcpu *vcpu, bool pmceid1)
{
	return 0;
}

#endif

#endif
