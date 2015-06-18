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
#else
struct kvm_pmu {
};

#define kvm_arm_pmu_v3_ready(v)		(false)
#endif

#endif
