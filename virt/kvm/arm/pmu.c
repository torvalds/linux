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
#include <linux/uaccess.h>
#include <asm/kvm_emulate.h>
#include <kvm/arm_pmu.h>
#include <kvm/arm_vgic.h>

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
	counter = __vcpu_sys_reg(vcpu, reg);

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
	__vcpu_sys_reg(vcpu, reg) += (s64)val - kvm_pmu_get_counter_value(vcpu, select_idx);
}

/**
 * kvm_pmu_stop_counter - stop PMU counter
 * @pmc: The PMU counter pointer
 *
 * If this counter has been configured to monitor some event, release it here.
 */
static void kvm_pmu_stop_counter(struct kvm_vcpu *vcpu, struct kvm_pmc *pmc)
{
	u64 counter, reg;

	if (pmc->perf_event) {
		counter = kvm_pmu_get_counter_value(vcpu, pmc->idx);
		reg = (pmc->idx == ARMV8_PMU_CYCLE_IDX)
		       ? PMCCNTR_EL0 : PMEVCNTR0_EL0 + pmc->idx;
		__vcpu_sys_reg(vcpu, reg) = counter;
		perf_event_disable(pmc->perf_event);
		perf_event_release_kernel(pmc->perf_event);
		pmc->perf_event = NULL;
	}
}

/**
 * kvm_pmu_vcpu_reset - reset pmu state for cpu
 * @vcpu: The vcpu pointer
 *
 */
void kvm_pmu_vcpu_reset(struct kvm_vcpu *vcpu)
{
	int i;
	struct kvm_pmu *pmu = &vcpu->arch.pmu;

	for (i = 0; i < ARMV8_PMU_MAX_COUNTERS; i++) {
		kvm_pmu_stop_counter(vcpu, &pmu->pmc[i]);
		pmu->pmc[i].idx = i;
		pmu->pmc[i].bitmask = 0xffffffffUL;
	}
}

/**
 * kvm_pmu_vcpu_destroy - free perf event of PMU for cpu
 * @vcpu: The vcpu pointer
 *
 */
void kvm_pmu_vcpu_destroy(struct kvm_vcpu *vcpu)
{
	int i;
	struct kvm_pmu *pmu = &vcpu->arch.pmu;

	for (i = 0; i < ARMV8_PMU_MAX_COUNTERS; i++) {
		struct kvm_pmc *pmc = &pmu->pmc[i];

		if (pmc->perf_event) {
			perf_event_disable(pmc->perf_event);
			perf_event_release_kernel(pmc->perf_event);
			pmc->perf_event = NULL;
		}
	}
}

u64 kvm_pmu_valid_counter_mask(struct kvm_vcpu *vcpu)
{
	u64 val = __vcpu_sys_reg(vcpu, PMCR_EL0) >> ARMV8_PMU_PMCR_N_SHIFT;

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

	if (!(__vcpu_sys_reg(vcpu, PMCR_EL0) & ARMV8_PMU_PMCR_E) || !val)
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

static u64 kvm_pmu_overflow_status(struct kvm_vcpu *vcpu)
{
	u64 reg = 0;

	if ((__vcpu_sys_reg(vcpu, PMCR_EL0) & ARMV8_PMU_PMCR_E)) {
		reg = __vcpu_sys_reg(vcpu, PMOVSSET_EL0);
		reg &= __vcpu_sys_reg(vcpu, PMCNTENSET_EL0);
		reg &= __vcpu_sys_reg(vcpu, PMINTENSET_EL1);
		reg &= kvm_pmu_valid_counter_mask(vcpu);
	}

	return reg;
}

static void kvm_pmu_update_state(struct kvm_vcpu *vcpu)
{
	struct kvm_pmu *pmu = &vcpu->arch.pmu;
	bool overflow;

	if (!kvm_arm_pmu_v3_ready(vcpu))
		return;

	overflow = !!kvm_pmu_overflow_status(vcpu);
	if (pmu->irq_level == overflow)
		return;

	pmu->irq_level = overflow;

	if (likely(irqchip_in_kernel(vcpu->kvm))) {
		int ret = kvm_vgic_inject_irq(vcpu->kvm, vcpu->vcpu_id,
					      pmu->irq_num, overflow, pmu);
		WARN_ON(ret);
	}
}

bool kvm_pmu_should_notify_user(struct kvm_vcpu *vcpu)
{
	struct kvm_pmu *pmu = &vcpu->arch.pmu;
	struct kvm_sync_regs *sregs = &vcpu->run->s.regs;
	bool run_level = sregs->device_irq_level & KVM_ARM_DEV_PMU;

	if (likely(irqchip_in_kernel(vcpu->kvm)))
		return false;

	return pmu->irq_level != run_level;
}

/*
 * Reflect the PMU overflow interrupt output level into the kvm_run structure
 */
void kvm_pmu_update_run(struct kvm_vcpu *vcpu)
{
	struct kvm_sync_regs *regs = &vcpu->run->s.regs;

	/* Populate the timer bitmap for user space */
	regs->device_irq_level &= ~KVM_ARM_DEV_PMU;
	if (vcpu->arch.pmu.irq_level)
		regs->device_irq_level |= KVM_ARM_DEV_PMU;
}

/**
 * kvm_pmu_flush_hwstate - flush pmu state to cpu
 * @vcpu: The vcpu pointer
 *
 * Check if the PMU has overflowed while we were running in the host, and inject
 * an interrupt if that was the case.
 */
void kvm_pmu_flush_hwstate(struct kvm_vcpu *vcpu)
{
	kvm_pmu_update_state(vcpu);
}

/**
 * kvm_pmu_sync_hwstate - sync pmu state from cpu
 * @vcpu: The vcpu pointer
 *
 * Check if the PMU has overflowed while we were running in the guest, and
 * inject an interrupt if that was the case.
 */
void kvm_pmu_sync_hwstate(struct kvm_vcpu *vcpu)
{
	kvm_pmu_update_state(vcpu);
}

static inline struct kvm_vcpu *kvm_pmc_to_vcpu(struct kvm_pmc *pmc)
{
	struct kvm_pmu *pmu;
	struct kvm_vcpu_arch *vcpu_arch;

	pmc -= pmc->idx;
	pmu = container_of(pmc, struct kvm_pmu, pmc[0]);
	vcpu_arch = container_of(pmu, struct kvm_vcpu_arch, pmu);
	return container_of(vcpu_arch, struct kvm_vcpu, arch);
}

/**
 * When the perf event overflows, set the overflow status and inform the vcpu.
 */
static void kvm_pmu_perf_overflow(struct perf_event *perf_event,
				  struct perf_sample_data *data,
				  struct pt_regs *regs)
{
	struct kvm_pmc *pmc = perf_event->overflow_handler_context;
	struct kvm_vcpu *vcpu = kvm_pmc_to_vcpu(pmc);
	int idx = pmc->idx;

	__vcpu_sys_reg(vcpu, PMOVSSET_EL0) |= BIT(idx);

	if (kvm_pmu_overflow_status(vcpu)) {
		kvm_make_request(KVM_REQ_IRQ_PENDING, vcpu);
		kvm_vcpu_kick(vcpu);
	}
}

/**
 * kvm_pmu_software_increment - do software increment
 * @vcpu: The vcpu pointer
 * @val: the value guest writes to PMSWINC register
 */
void kvm_pmu_software_increment(struct kvm_vcpu *vcpu, u64 val)
{
	int i;
	u64 type, enable, reg;

	if (val == 0)
		return;

	enable = __vcpu_sys_reg(vcpu, PMCNTENSET_EL0);
	for (i = 0; i < ARMV8_PMU_CYCLE_IDX; i++) {
		if (!(val & BIT(i)))
			continue;
		type = __vcpu_sys_reg(vcpu, PMEVTYPER0_EL0 + i)
		       & ARMV8_PMU_EVTYPE_EVENT;
		if ((type == ARMV8_PMUV3_PERFCTR_SW_INCR)
		    && (enable & BIT(i))) {
			reg = __vcpu_sys_reg(vcpu, PMEVCNTR0_EL0 + i) + 1;
			reg = lower_32_bits(reg);
			__vcpu_sys_reg(vcpu, PMEVCNTR0_EL0 + i) = reg;
			if (!reg)
				__vcpu_sys_reg(vcpu, PMOVSSET_EL0) |= BIT(i);
		}
	}
}

/**
 * kvm_pmu_handle_pmcr - handle PMCR register
 * @vcpu: The vcpu pointer
 * @val: the value guest writes to PMCR register
 */
void kvm_pmu_handle_pmcr(struct kvm_vcpu *vcpu, u64 val)
{
	struct kvm_pmu *pmu = &vcpu->arch.pmu;
	struct kvm_pmc *pmc;
	u64 mask;
	int i;

	mask = kvm_pmu_valid_counter_mask(vcpu);
	if (val & ARMV8_PMU_PMCR_E) {
		kvm_pmu_enable_counter(vcpu,
		       __vcpu_sys_reg(vcpu, PMCNTENSET_EL0) & mask);
	} else {
		kvm_pmu_disable_counter(vcpu, mask);
	}

	if (val & ARMV8_PMU_PMCR_C)
		kvm_pmu_set_counter_value(vcpu, ARMV8_PMU_CYCLE_IDX, 0);

	if (val & ARMV8_PMU_PMCR_P) {
		for (i = 0; i < ARMV8_PMU_CYCLE_IDX; i++)
			kvm_pmu_set_counter_value(vcpu, i, 0);
	}

	if (val & ARMV8_PMU_PMCR_LC) {
		pmc = &pmu->pmc[ARMV8_PMU_CYCLE_IDX];
		pmc->bitmask = 0xffffffffffffffffUL;
	}
}

static bool kvm_pmu_counter_is_enabled(struct kvm_vcpu *vcpu, u64 select_idx)
{
	return (__vcpu_sys_reg(vcpu, PMCR_EL0) & ARMV8_PMU_PMCR_E) &&
	       (__vcpu_sys_reg(vcpu, PMCNTENSET_EL0) & BIT(select_idx));
}

/**
 * kvm_pmu_set_counter_event_type - set selected counter to monitor some event
 * @vcpu: The vcpu pointer
 * @data: The data guest writes to PMXEVTYPER_EL0
 * @select_idx: The number of selected counter
 *
 * When OS accesses PMXEVTYPER_EL0, that means it wants to set a PMC to count an
 * event with given hardware event number. Here we call perf_event API to
 * emulate this action and create a kernel perf event for it.
 */
void kvm_pmu_set_counter_event_type(struct kvm_vcpu *vcpu, u64 data,
				    u64 select_idx)
{
	struct kvm_pmu *pmu = &vcpu->arch.pmu;
	struct kvm_pmc *pmc = &pmu->pmc[select_idx];
	struct perf_event *event;
	struct perf_event_attr attr;
	u64 eventsel, counter;

	kvm_pmu_stop_counter(vcpu, pmc);
	eventsel = data & ARMV8_PMU_EVTYPE_EVENT;

	/* Software increment event does't need to be backed by a perf event */
	if (eventsel == ARMV8_PMUV3_PERFCTR_SW_INCR &&
	    select_idx != ARMV8_PMU_CYCLE_IDX)
		return;

	memset(&attr, 0, sizeof(struct perf_event_attr));
	attr.type = PERF_TYPE_RAW;
	attr.size = sizeof(attr);
	attr.pinned = 1;
	attr.disabled = !kvm_pmu_counter_is_enabled(vcpu, select_idx);
	attr.exclude_user = data & ARMV8_PMU_EXCLUDE_EL0 ? 1 : 0;
	attr.exclude_kernel = data & ARMV8_PMU_EXCLUDE_EL1 ? 1 : 0;
	attr.exclude_hv = 1; /* Don't count EL2 events */
	attr.exclude_host = 1; /* Don't count host events */
	attr.config = (select_idx == ARMV8_PMU_CYCLE_IDX) ?
		ARMV8_PMUV3_PERFCTR_CPU_CYCLES : eventsel;

	counter = kvm_pmu_get_counter_value(vcpu, select_idx);
	/* The initial sample period (overflow count) of an event. */
	attr.sample_period = (-counter) & pmc->bitmask;

	event = perf_event_create_kernel_counter(&attr, -1, current,
						 kvm_pmu_perf_overflow, pmc);
	if (IS_ERR(event)) {
		pr_err_once("kvm: pmu event creation failed %ld\n",
			    PTR_ERR(event));
		return;
	}

	pmc->perf_event = event;
}

bool kvm_arm_support_pmu_v3(void)
{
	/*
	 * Check if HW_PERF_EVENTS are supported by checking the number of
	 * hardware performance counters. This could ensure the presence of
	 * a physical PMU and CONFIG_PERF_EVENT is selected.
	 */
	return (perf_num_counters() > 0);
}

int kvm_arm_pmu_v3_enable(struct kvm_vcpu *vcpu)
{
	if (!vcpu->arch.pmu.created)
		return 0;

	/*
	 * A valid interrupt configuration for the PMU is either to have a
	 * properly configured interrupt number and using an in-kernel
	 * irqchip, or to not have an in-kernel GIC and not set an IRQ.
	 */
	if (irqchip_in_kernel(vcpu->kvm)) {
		int irq = vcpu->arch.pmu.irq_num;
		if (!kvm_arm_pmu_irq_initialized(vcpu))
			return -EINVAL;

		/*
		 * If we are using an in-kernel vgic, at this point we know
		 * the vgic will be initialized, so we can check the PMU irq
		 * number against the dimensions of the vgic and make sure
		 * it's valid.
		 */
		if (!irq_is_ppi(irq) && !vgic_valid_spi(vcpu->kvm, irq))
			return -EINVAL;
	} else if (kvm_arm_pmu_irq_initialized(vcpu)) {
		   return -EINVAL;
	}

	kvm_pmu_vcpu_reset(vcpu);
	vcpu->arch.pmu.ready = true;

	return 0;
}

static int kvm_arm_pmu_v3_init(struct kvm_vcpu *vcpu)
{
	if (!kvm_arm_support_pmu_v3())
		return -ENODEV;

	if (!test_bit(KVM_ARM_VCPU_PMU_V3, vcpu->arch.features))
		return -ENXIO;

	if (vcpu->arch.pmu.created)
		return -EBUSY;

	if (irqchip_in_kernel(vcpu->kvm)) {
		int ret;

		/*
		 * If using the PMU with an in-kernel virtual GIC
		 * implementation, we require the GIC to be already
		 * initialized when initializing the PMU.
		 */
		if (!vgic_initialized(vcpu->kvm))
			return -ENODEV;

		if (!kvm_arm_pmu_irq_initialized(vcpu))
			return -ENXIO;

		ret = kvm_vgic_set_owner(vcpu, vcpu->arch.pmu.irq_num,
					 &vcpu->arch.pmu);
		if (ret)
			return ret;
	}

	vcpu->arch.pmu.created = true;
	return 0;
}

/*
 * For one VM the interrupt type must be same for each vcpu.
 * As a PPI, the interrupt number is the same for all vcpus,
 * while as an SPI it must be a separate number per vcpu.
 */
static bool pmu_irq_is_valid(struct kvm *kvm, int irq)
{
	int i;
	struct kvm_vcpu *vcpu;

	kvm_for_each_vcpu(i, vcpu, kvm) {
		if (!kvm_arm_pmu_irq_initialized(vcpu))
			continue;

		if (irq_is_ppi(irq)) {
			if (vcpu->arch.pmu.irq_num != irq)
				return false;
		} else {
			if (vcpu->arch.pmu.irq_num == irq)
				return false;
		}
	}

	return true;
}

int kvm_arm_pmu_v3_set_attr(struct kvm_vcpu *vcpu, struct kvm_device_attr *attr)
{
	switch (attr->attr) {
	case KVM_ARM_VCPU_PMU_V3_IRQ: {
		int __user *uaddr = (int __user *)(long)attr->addr;
		int irq;

		if (!irqchip_in_kernel(vcpu->kvm))
			return -EINVAL;

		if (!test_bit(KVM_ARM_VCPU_PMU_V3, vcpu->arch.features))
			return -ENODEV;

		if (get_user(irq, uaddr))
			return -EFAULT;

		/* The PMU overflow interrupt can be a PPI or a valid SPI. */
		if (!(irq_is_ppi(irq) || irq_is_spi(irq)))
			return -EINVAL;

		if (!pmu_irq_is_valid(vcpu->kvm, irq))
			return -EINVAL;

		if (kvm_arm_pmu_irq_initialized(vcpu))
			return -EBUSY;

		kvm_debug("Set kvm ARM PMU irq: %d\n", irq);
		vcpu->arch.pmu.irq_num = irq;
		return 0;
	}
	case KVM_ARM_VCPU_PMU_V3_INIT:
		return kvm_arm_pmu_v3_init(vcpu);
	}

	return -ENXIO;
}

int kvm_arm_pmu_v3_get_attr(struct kvm_vcpu *vcpu, struct kvm_device_attr *attr)
{
	switch (attr->attr) {
	case KVM_ARM_VCPU_PMU_V3_IRQ: {
		int __user *uaddr = (int __user *)(long)attr->addr;
		int irq;

		if (!irqchip_in_kernel(vcpu->kvm))
			return -EINVAL;

		if (!test_bit(KVM_ARM_VCPU_PMU_V3, vcpu->arch.features))
			return -ENODEV;

		if (!kvm_arm_pmu_irq_initialized(vcpu))
			return -ENXIO;

		irq = vcpu->arch.pmu.irq_num;
		return put_user(irq, uaddr);
	}
	}

	return -ENXIO;
}

int kvm_arm_pmu_v3_has_attr(struct kvm_vcpu *vcpu, struct kvm_device_attr *attr)
{
	switch (attr->attr) {
	case KVM_ARM_VCPU_PMU_V3_IRQ:
	case KVM_ARM_VCPU_PMU_V3_INIT:
		if (kvm_arm_support_pmu_v3() &&
		    test_bit(KVM_ARM_VCPU_PMU_V3, vcpu->arch.features))
			return 0;
	}

	return -ENXIO;
}
