#ifndef __CPUHOTPLUG_H
#define __CPUHOTPLUG_H

#include <linux/types.h>

enum cpuhp_state {
	CPUHP_OFFLINE,
	CPUHP_CREATE_THREADS,
	CPUHP_PERF_PREPARE,
	CPUHP_PERF_X86_PREPARE,
	CPUHP_PERF_X86_UNCORE_PREP,
	CPUHP_PERF_X86_AMD_UNCORE_PREP,
	CPUHP_PERF_X86_RAPL_PREP,
	CPUHP_PERF_BFIN,
	CPUHP_PERF_POWER,
	CPUHP_PERF_SUPERH,
	CPUHP_X86_HPET_DEAD,
	CPUHP_X86_APB_DEAD,
	CPUHP_WORKQUEUE_PREP,
	CPUHP_POWER_NUMA_PREPARE,
	CPUHP_HRTIMERS_PREPARE,
	CPUHP_PROFILE_PREPARE,
	CPUHP_X2APIC_PREPARE,
	CPUHP_SMPCFD_PREPARE,
	CPUHP_RCUTREE_PREP,
	CPUHP_NOTIFY_PREPARE,
	CPUHP_TIMERS_DEAD,
	CPUHP_BRINGUP_CPU,
	CPUHP_AP_IDLE_DEAD,
	CPUHP_AP_OFFLINE,
	CPUHP_AP_SCHED_STARTING,
	CPUHP_AP_RCUTREE_DYING,
	CPUHP_AP_IRQ_GIC_STARTING,
	CPUHP_AP_IRQ_GICV3_STARTING,
	CPUHP_AP_IRQ_HIP04_STARTING,
	CPUHP_AP_IRQ_ARMADA_XP_STARTING,
	CPUHP_AP_IRQ_ARMADA_CASC_STARTING,
	CPUHP_AP_IRQ_BCM2836_STARTING,
	CPUHP_AP_ARM_MVEBU_COHERENCY,
	CPUHP_AP_PERF_X86_UNCORE_STARTING,
	CPUHP_AP_PERF_X86_AMD_UNCORE_STARTING,
	CPUHP_AP_PERF_X86_STARTING,
	CPUHP_AP_PERF_X86_AMD_IBS_STARTING,
	CPUHP_AP_PERF_X86_CQM_STARTING,
	CPUHP_AP_PERF_X86_CSTATE_STARTING,
	CPUHP_AP_PERF_XTENSA_STARTING,
	CPUHP_AP_PERF_METAG_STARTING,
	CPUHP_AP_MIPS_OP_LOONGSON3_STARTING,
	CPUHP_AP_ARM_VFP_STARTING,
	CPUHP_AP_PERF_ARM_STARTING,
	CPUHP_AP_ARM_L2X0_STARTING,
	CPUHP_AP_ARM_ARCH_TIMER_STARTING,
	CPUHP_AP_ARM_GLOBAL_TIMER_STARTING,
	CPUHP_AP_DUMMY_TIMER_STARTING,
	CPUHP_AP_EXYNOS4_MCT_TIMER_STARTING,
	CPUHP_AP_ARM_TWD_STARTING,
	CPUHP_AP_METAG_TIMER_STARTING,
	CPUHP_AP_QCOM_TIMER_STARTING,
	CPUHP_AP_ARMADA_TIMER_STARTING,
	CPUHP_AP_MARCO_TIMER_STARTING,
	CPUHP_AP_MIPS_GIC_TIMER_STARTING,
	CPUHP_AP_ARC_TIMER_STARTING,
	CPUHP_AP_KVM_STARTING,
	CPUHP_AP_KVM_ARM_VGIC_INIT_STARTING,
	CPUHP_AP_KVM_ARM_VGIC_STARTING,
	CPUHP_AP_KVM_ARM_TIMER_STARTING,
	CPUHP_AP_ARM_XEN_STARTING,
	CPUHP_AP_ARM_CORESIGHT_STARTING,
	CPUHP_AP_ARM_CORESIGHT4_STARTING,
	CPUHP_AP_ARM64_ISNDEP_STARTING,
	CPUHP_AP_SMPCFD_DYING,
	CPUHP_AP_X86_TBOOT_DYING,
	CPUHP_AP_NOTIFY_STARTING,
	CPUHP_AP_ONLINE,
	CPUHP_TEARDOWN_CPU,
	CPUHP_AP_ONLINE_IDLE,
	CPUHP_AP_SMPBOOT_THREADS,
	CPUHP_AP_X86_VDSO_VMA_ONLINE,
	CPUHP_AP_PERF_ONLINE,
	CPUHP_AP_PERF_X86_ONLINE,
	CPUHP_AP_PERF_X86_UNCORE_ONLINE,
	CPUHP_AP_PERF_X86_AMD_UNCORE_ONLINE,
	CPUHP_AP_PERF_X86_AMD_POWER_ONLINE,
	CPUHP_AP_PERF_X86_RAPL_ONLINE,
	CPUHP_AP_PERF_X86_CQM_ONLINE,
	CPUHP_AP_PERF_X86_CSTATE_ONLINE,
	CPUHP_AP_PERF_S390_CF_ONLINE,
	CPUHP_AP_PERF_S390_SF_ONLINE,
	CPUHP_AP_PERF_ARM_CCI_ONLINE,
	CPUHP_AP_PERF_ARM_CCN_ONLINE,
	CPUHP_AP_WORKQUEUE_ONLINE,
	CPUHP_AP_RCUTREE_ONLINE,
	CPUHP_AP_NOTIFY_ONLINE,
	CPUHP_AP_ONLINE_DYN,
	CPUHP_AP_ONLINE_DYN_END		= CPUHP_AP_ONLINE_DYN + 30,
	CPUHP_AP_X86_HPET_ONLINE,
	CPUHP_AP_X86_KVM_CLK_ONLINE,
	CPUHP_AP_ACTIVE,
	CPUHP_ONLINE,
};

int __cpuhp_setup_state(enum cpuhp_state state,	const char *name, bool invoke,
			int (*startup)(unsigned int cpu),
			int (*teardown)(unsigned int cpu));

/**
 * cpuhp_setup_state - Setup hotplug state callbacks with calling the callbacks
 * @state:	The state for which the calls are installed
 * @name:	Name of the callback (will be used in debug output)
 * @startup:	startup callback function
 * @teardown:	teardown callback function
 *
 * Installs the callback functions and invokes the startup callback on
 * the present cpus which have already reached the @state.
 */
static inline int cpuhp_setup_state(enum cpuhp_state state,
				    const char *name,
				    int (*startup)(unsigned int cpu),
				    int (*teardown)(unsigned int cpu))
{
	return __cpuhp_setup_state(state, name, true, startup, teardown);
}

/**
 * cpuhp_setup_state_nocalls - Setup hotplug state callbacks without calling the
 *			       callbacks
 * @state:	The state for which the calls are installed
 * @name:	Name of the callback.
 * @startup:	startup callback function
 * @teardown:	teardown callback function
 *
 * Same as @cpuhp_setup_state except that no calls are executed are invoked
 * during installation of this callback. NOP if SMP=n or HOTPLUG_CPU=n.
 */
static inline int cpuhp_setup_state_nocalls(enum cpuhp_state state,
					    const char *name,
					    int (*startup)(unsigned int cpu),
					    int (*teardown)(unsigned int cpu))
{
	return __cpuhp_setup_state(state, name, false, startup, teardown);
}

void __cpuhp_remove_state(enum cpuhp_state state, bool invoke);

/**
 * cpuhp_remove_state - Remove hotplug state callbacks and invoke the teardown
 * @state:	The state for which the calls are removed
 *
 * Removes the callback functions and invokes the teardown callback on
 * the present cpus which have already reached the @state.
 */
static inline void cpuhp_remove_state(enum cpuhp_state state)
{
	__cpuhp_remove_state(state, true);
}

/**
 * cpuhp_remove_state_nocalls - Remove hotplug state callbacks without invoking
 *				teardown
 * @state:	The state for which the calls are removed
 */
static inline void cpuhp_remove_state_nocalls(enum cpuhp_state state)
{
	__cpuhp_remove_state(state, false);
}

#ifdef CONFIG_SMP
void cpuhp_online_idle(enum cpuhp_state state);
#else
static inline void cpuhp_online_idle(enum cpuhp_state state) { }
#endif

#endif
