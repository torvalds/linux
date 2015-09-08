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

#include <linux/cpu.h>
#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <linux/perf_event.h>
#include <asm/kvm_emulate.h>
#include <kvm/arm_pmu.h>

/**
 * kvm_pmu_get_counter_value - get PMU counter value
 * @vcpu: The vcpu pointer
 * @select_idx: The counter index
 */
u64 kvm_pmu_get_counter_value(struct kvm_vcpu *vcpu, u64 select_idx)
{
	u64 counter, reg, enabled, running;
	struct kvm_pmu *pmu = &vcpu->arch.pmu;
	struct kvm_pmc *pmc = &pmu->pmc[select_idx];

	reg = (select_idx == ARMV8_PMU_CYCLE_IDX)
	      ? PMCCNTR_EL0 : PMEVCNTR0_EL0 + select_idx;
	counter = vcpu_sys_reg(vcpu, reg);

	/* The real counter value is equal to the value of counter register plus
	 * the value perf event counts.
	 */
	if (pmc->perf_event)
		counter += perf_event_read_value(pmc->perf_event, &enabled,
						 &running);

	return counter & pmc->bitmask;
}

/**
 * kvm_pmu_set_counter_value - set PMU counter value
 * @vcpu: The vcpu pointer
 * @select_idx: The counter index
 * @val: The counter value
 */
void kvm_pmu_set_counter_value(struct kvm_vcpu *vcpu, u64 select_idx, u64 val)
{
	u64 reg;

	reg = (select_idx == ARMV8_PMU_CYCLE_IDX)
	      ? PMCCNTR_EL0 : PMEVCNTR0_EL0 + select_idx;
	vcpu_sys_reg(vcpu, reg) += (s64)val - kvm_pmu_get_counter_value(vcpu, select_idx);
}

u64 kvm_pmu_valid_counter_mask(struct kvm_vcpu *vcpu)
{
	u64 val = vcpu_sys_reg(vcpu, PMCR_EL0) >> ARMV8_PMU_PMCR_N_SHIFT;

	val &= ARMV8_PMU_PMCR_N_MASK;
	if (val == 0)
		return BIT(ARMV8_PMU_CYCLE_IDX);
	else
		return GENMASK(val - 1, 0) | BIT(ARMV8_PMU_CYCLE_IDX);
}

/**
 * kvm_pmu_enable_counter - enable selected PMU counter
 * @vcpu: The vcpu pointer
 * @val: the value guest writes to PMCNTENSET register
 *
 * Call perf_event_enable to start counting the perf event
 */
void kvm_pmu_enable_counter(struct kvm_vcpu *vcpu, u64 val)
{
	int i;
	struct kvm_pmu *pmu = &vcpu->arch.pmu;
	struct kvm_pmc *pmc;

	if (!(vcpu_sys_reg(vcpu, PMCR_EL0) & ARMV8_PMU_PMCR_E) || !val)
		return;

	for (i = 0; i < ARMV8_PMU_MAX_COUNTERS; i++) {
		if (!(val & BIT(i)))
			continue;

		pmc = &pmu->pmc[i];
		if (pmc->perf_event) {
			perf_event_enable(pmc->perf_event);
			if (pmc->perf_event->state != PERF_EVENT_STATE_ACTIVE)
				kvm_debug("fail to enable perf event\n");
		}
	}
}

/**
 * kvm_pmu_disable_counter - disable selected PMU counter
 * @vcpu: The vcpu pointer
 * @val: the value guest writes to PMCNTENCLR register
 *
 * Call perf_event_disable to stop counting the perf event
 */
void kvm_pmu_disable_counter(struct kvm_vcpu *vcpu, u64 val)
{
	int i;
	struct kvm_pmu *pmu = &vcpu->arch.pmu;
	struct kvm_pmc *pmc;

	if (!val)
		return;

	for (i = 0; i < ARMV8_PMU_MAX_COUNTERS; i++) {
		if (!(val & BIT(i)))
			continue;

		pmc = &pmu->pmc[i];
		if (pmc->perf_event)
			perf_event_disable(pmc->perf_event);
	}
}
