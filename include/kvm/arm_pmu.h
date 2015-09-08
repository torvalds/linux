/*
 * Copyright (C) 2015 Linaro Ltd.
 * Author: Shannon Zhao <shannon.zhao@linaro.org>
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

#ifndef __ASM_ARM_KVM_PMU_H
#define __ASM_ARM_KVM_PMU_H

#ifdef CONFIG_KVM_ARM_PMU

#include <linux/perf_event.h>
#include <asm/perf_event.h>

#define ARMV8_PMU_CYCLE_IDX		(ARMV8_PMU_MAX_COUNTERS - 1)

struct kvm_pmc {
	u8 idx;	/* index into the pmu->pmc array */
	struct perf_event *perf_event;
	u64 bitmask;
};

struct kvm_pmu {
	int irq_num;
	struct kvm_pmc pmc[ARMV8_PMU_MAX_COUNTERS];
	bool ready;
};

#define kvm_arm_pmu_v3_ready(v)		((v)->arch.pmu.ready)
u64 kvm_pmu_get_counter_value(struct kvm_vcpu *vcpu, u64 select_idx);
void kvm_pmu_set_counter_value(struct kvm_vcpu *vcpu, u64 select_idx, u64 val);
u64 kvm_pmu_valid_counter_mask(struct kvm_vcpu *vcpu);
void kvm_pmu_disable_counter(struct kvm_vcpu *vcpu, u64 val);
void kvm_pmu_enable_counter(struct kvm_vcpu *vcpu, u64 val);
#else
struct kvm_pmu {
};

#define kvm_arm_pmu_v3_ready(v)		(false)
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
static inline void kvm_pmu_disable_counter(struct kvm_vcpu *vcpu, u64 val) {}
static inline void kvm_pmu_enable_counter(struct kvm_vcpu *vcpu, u64 val) {}
#endif

#endif
