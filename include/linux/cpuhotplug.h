/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __CPUHOTPLUG_H
#define __CPUHOTPLUG_H

#include <linux/types.h>

/*
 * CPU-up			CPU-down
 *
 * BP		AP		BP		AP
 *
 * OFFLINE			OFFLINE
 *   |				  ^
 *   v				  |
 * BRINGUP_CPU->AP_OFFLINE	BRINGUP_CPU  <- AP_IDLE_DEAD (idle thread/play_dead)
 *		  |				AP_OFFLINE
 *		  v (IRQ-off)	  ,---------------^
 *		AP_ONLNE	  | (stop_machine)
 *		  |		TEARDOWN_CPU <-	AP_ONLINE_IDLE
 *		  |				  ^
 *		  v				  |
 *              AP_ACTIVE			AP_ACTIVE
 */

enum cpuhp_state {
	CPUHP_INVALID = -1,
	CPUHP_OFFLINE = 0,
	CPUHP_CREATE_THREADS,
	CPUHP_PERF_PREPARE,
	CPUHP_PERF_X86_PREPARE,
	CPUHP_PERF_X86_AMD_UNCORE_PREP,
	CPUHP_PERF_POWER,
	CPUHP_PERF_SUPERH,
	CPUHP_X86_HPET_DEAD,
	CPUHP_X86_APB_DEAD,
	CPUHP_X86_MCE_DEAD,
	CPUHP_VIRT_NET_DEAD,
	CPUHP_SLUB_DEAD,
	CPUHP_DEBUG_OBJ_DEAD,
	CPUHP_MM_WRITEBACK_DEAD,
	CPUHP_MM_VMSTAT_DEAD,
	CPUHP_SOFTIRQ_DEAD,
	CPUHP_NET_MVNETA_DEAD,
	CPUHP_CPUIDLE_DEAD,
	CPUHP_ARM64_FPSIMD_DEAD,
	CPUHP_ARM_OMAP_WAKE_DEAD,
	CPUHP_IRQ_POLL_DEAD,
	CPUHP_BLOCK_SOFTIRQ_DEAD,
	CPUHP_ACPI_CPUDRV_DEAD,
	CPUHP_S390_PFAULT_DEAD,
	CPUHP_BLK_MQ_DEAD,
	CPUHP_FS_BUFF_DEAD,
	CPUHP_PRINTK_DEAD,
	CPUHP_MM_MEMCQ_DEAD,
	CPUHP_PERCPU_CNT_DEAD,
	CPUHP_RADIX_DEAD,
	CPUHP_PAGE_ALLOC_DEAD,
	CPUHP_NET_DEV_DEAD,
	CPUHP_PCI_XGENE_DEAD,
	CPUHP_IOMMU_INTEL_DEAD,
	CPUHP_LUSTRE_CFS_DEAD,
	CPUHP_AP_ARM_CACHE_B15_RAC_DEAD,
	CPUHP_PADATA_DEAD,
	CPUHP_WORKQUEUE_PREP,
	CPUHP_POWER_NUMA_PREPARE,
	CPUHP_HRTIMERS_PREPARE,
	CPUHP_PROFILE_PREPARE,
	CPUHP_X2APIC_PREPARE,
	CPUHP_SMPCFD_PREPARE,
	CPUHP_RELAY_PREPARE,
	CPUHP_SLAB_PREPARE,
	CPUHP_MD_RAID5_PREPARE,
	CPUHP_RCUTREE_PREP,
	CPUHP_CPUIDLE_COUPLED_PREPARE,
	CPUHP_POWERPC_PMAC_PREPARE,
	CPUHP_POWERPC_MMU_CTX_PREPARE,
	CPUHP_XEN_PREPARE,
	CPUHP_XEN_EVTCHN_PREPARE,
	CPUHP_ARM_SHMOBILE_SCU_PREPARE,
	CPUHP_SH_SH3X_PREPARE,
	CPUHP_NET_FLOW_PREPARE,
	CPUHP_TOPOLOGY_PREPARE,
	CPUHP_NET_IUCV_PREPARE,
	CPUHP_ARM_BL_PREPARE,
	CPUHP_TRACE_RB_PREPARE,
	CPUHP_MM_ZS_PREPARE,
	CPUHP_MM_ZSWP_MEM_PREPARE,
	CPUHP_MM_ZSWP_POOL_PREPARE,
	CPUHP_KVM_PPC_BOOK3S_PREPARE,
	CPUHP_ZCOMP_PREPARE,
	CPUHP_TIMERS_PREPARE,
	CPUHP_MIPS_SOC_PREPARE,
	CPUHP_BP_PREPARE_DYN,
	CPUHP_BP_PREPARE_DYN_END		= CPUHP_BP_PREPARE_DYN + 20,
	CPUHP_BRINGUP_CPU,
	CPUHP_AP_IDLE_DEAD,
	CPUHP_AP_OFFLINE,
	CPUHP_AP_SCHED_STARTING,
	CPUHP_AP_RCUTREE_DYING,
	CPUHP_AP_CPU_PM_STARTING,
	CPUHP_AP_IRQ_GIC_STARTING,
	CPUHP_AP_IRQ_HIP04_STARTING,
	CPUHP_AP_IRQ_APPLE_AIC_STARTING,
	CPUHP_AP_IRQ_ARMADA_XP_STARTING,
	CPUHP_AP_IRQ_BCM2836_STARTING,
	CPUHP_AP_IRQ_MIPS_GIC_STARTING,
	CPUHP_AP_IRQ_RISCV_STARTING,
	CPUHP_AP_IRQ_SIFIVE_PLIC_STARTING,
	CPUHP_AP_ARM_MVEBU_COHERENCY,
	CPUHP_AP_MICROCODE_LOADER,
	CPUHP_AP_PERF_X86_AMD_UNCORE_STARTING,
	CPUHP_AP_PERF_X86_STARTING,
	CPUHP_AP_PERF_X86_AMD_IBS_STARTING,
	CPUHP_AP_PERF_X86_CQM_STARTING,
	CPUHP_AP_PERF_X86_CSTATE_STARTING,
	CPUHP_AP_PERF_XTENSA_STARTING,
	CPUHP_AP_MIPS_OP_LOONGSON3_STARTING,
	CPUHP_AP_ARM_SDEI_STARTING,
	CPUHP_AP_ARM_VFP_STARTING,
	CPUHP_AP_ARM64_DEBUG_MONITORS_STARTING,
	CPUHP_AP_PERF_ARM_HW_BREAKPOINT_STARTING,
	CPUHP_AP_PERF_ARM_ACPI_STARTING,
	CPUHP_AP_PERF_ARM_STARTING,
	CPUHP_AP_ARM_L2X0_STARTING,
	CPUHP_AP_EXYNOS4_MCT_TIMER_STARTING,
	CPUHP_AP_ARM_ARCH_TIMER_STARTING,
	CPUHP_AP_ARM_GLOBAL_TIMER_STARTING,
	CPUHP_AP_JCORE_TIMER_STARTING,
	CPUHP_AP_ARM_TWD_STARTING,
	CPUHP_AP_QCOM_TIMER_STARTING,
	CPUHP_AP_TEGRA_TIMER_STARTING,
	CPUHP_AP_ARMADA_TIMER_STARTING,
	CPUHP_AP_MARCO_TIMER_STARTING,
	CPUHP_AP_MIPS_GIC_TIMER_STARTING,
	CPUHP_AP_ARC_TIMER_STARTING,
	CPUHP_AP_RISCV_TIMER_STARTING,
	CPUHP_AP_CLINT_TIMER_STARTING,
	CPUHP_AP_CSKY_TIMER_STARTING,
	CPUHP_AP_HYPERV_TIMER_STARTING,
	CPUHP_AP_KVM_STARTING,
	CPUHP_AP_KVM_ARM_VGIC_INIT_STARTING,
	CPUHP_AP_KVM_ARM_VGIC_STARTING,
	CPUHP_AP_KVM_ARM_TIMER_STARTING,
	/* Must be the last timer callback */
	CPUHP_AP_DUMMY_TIMER_STARTING,
	CPUHP_AP_ARM_XEN_STARTING,
	CPUHP_AP_ARM_CORESIGHT_STARTING,
	CPUHP_AP_ARM_CORESIGHT_CTI_STARTING,
	CPUHP_AP_ARM64_ISNDEP_STARTING,
	CPUHP_AP_SMPCFD_DYING,
	CPUHP_AP_X86_TBOOT_DYING,
	CPUHP_AP_ARM_CACHE_B15_RAC_DYING,
	CPUHP_AP_ONLINE,
	CPUHP_TEARDOWN_CPU,
	CPUHP_AP_ONLINE_IDLE,
	CPUHP_AP_SCHED_WAIT_EMPTY,
	CPUHP_AP_SMPBOOT_THREADS,
	CPUHP_AP_X86_VDSO_VMA_ONLINE,
	CPUHP_AP_IRQ_AFFINITY_ONLINE,
	CPUHP_AP_BLK_MQ_ONLINE,
	CPUHP_AP_ARM_MVEBU_SYNC_CLOCKS,
	CPUHP_AP_X86_INTEL_EPB_ONLINE,
	CPUHP_AP_PERF_ONLINE,
	CPUHP_AP_PERF_X86_ONLINE,
	CPUHP_AP_PERF_X86_UNCORE_ONLINE,
	CPUHP_AP_PERF_X86_AMD_UNCORE_ONLINE,
	CPUHP_AP_PERF_X86_AMD_POWER_ONLINE,
	CPUHP_AP_PERF_X86_RAPL_ONLINE,
	CPUHP_AP_PERF_X86_CQM_ONLINE,
	CPUHP_AP_PERF_X86_CSTATE_ONLINE,
	CPUHP_AP_PERF_S390_CF_ONLINE,
	CPUHP_AP_PERF_S390_CFD_ONLINE,
	CPUHP_AP_PERF_S390_SF_ONLINE,
	CPUHP_AP_PERF_ARM_CCI_ONLINE,
	CPUHP_AP_PERF_ARM_CCN_ONLINE,
	CPUHP_AP_PERF_ARM_HISI_DDRC_ONLINE,
	CPUHP_AP_PERF_ARM_HISI_HHA_ONLINE,
	CPUHP_AP_PERF_ARM_HISI_L3_ONLINE,
	CPUHP_AP_PERF_ARM_L2X0_ONLINE,
	CPUHP_AP_PERF_ARM_QCOM_L2_ONLINE,
	CPUHP_AP_PERF_ARM_QCOM_L3_ONLINE,
	CPUHP_AP_PERF_ARM_APM_XGENE_ONLINE,
	CPUHP_AP_PERF_ARM_CAVIUM_TX2_UNCORE_ONLINE,
	CPUHP_AP_PERF_POWERPC_NEST_IMC_ONLINE,
	CPUHP_AP_PERF_POWERPC_CORE_IMC_ONLINE,
	CPUHP_AP_PERF_POWERPC_THREAD_IMC_ONLINE,
	CPUHP_AP_PERF_POWERPC_TRACE_IMC_ONLINE,
	CPUHP_AP_PERF_POWERPC_HV_24x7_ONLINE,
	CPUHP_AP_PERF_POWERPC_HV_GPCI_ONLINE,
	CPUHP_AP_PERF_CSKY_ONLINE,
	CPUHP_AP_WATCHDOG_ONLINE,
	CPUHP_AP_WORKQUEUE_ONLINE,
	CPUHP_AP_RCUTREE_ONLINE,
	CPUHP_AP_BASE_CACHEINFO_ONLINE,
	CPUHP_AP_ONLINE_DYN,
	CPUHP_AP_ONLINE_DYN_END		= CPUHP_AP_ONLINE_DYN + 30,
	CPUHP_AP_X86_HPET_ONLINE,
	CPUHP_AP_X86_KVM_CLK_ONLINE,
	CPUHP_AP_DTPM_CPU_ONLINE,
	CPUHP_AP_ACTIVE,
	CPUHP_ONLINE,
};

int __cpuhp_setup_state(enum cpuhp_state state,	const char *name, bool invoke,
			int (*startup)(unsigned int cpu),
			int (*teardown)(unsigned int cpu), bool multi_instance);

int __cpuhp_setup_state_cpuslocked(enum cpuhp_state state, const char *name,
				   bool invoke,
				   int (*startup)(unsigned int cpu),
				   int (*teardown)(unsigned int cpu),
				   bool multi_instance);
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
	return __cpuhp_setup_state(state, name, true, startup, teardown, false);
}

static inline int cpuhp_setup_state_cpuslocked(enum cpuhp_state state,
					       const char *name,
					       int (*startup)(unsigned int cpu),
					       int (*teardown)(unsigned int cpu))
{
	return __cpuhp_setup_state_cpuslocked(state, name, true, startup,
					      teardown, false);
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
	return __cpuhp_setup_state(state, name, false, startup, teardown,
				   false);
}

static inline int cpuhp_setup_state_nocalls_cpuslocked(enum cpuhp_state state,
						     const char *name,
						     int (*startup)(unsigned int cpu),
						     int (*teardown)(unsigned int cpu))
{
	return __cpuhp_setup_state_cpuslocked(state, name, false, startup,
					    teardown, false);
}

/**
 * cpuhp_setup_state_multi - Add callbacks for multi state
 * @state:	The state for which the calls are installed
 * @name:	Name of the callback.
 * @startup:	startup callback function
 * @teardown:	teardown callback function
 *
 * Sets the internal multi_instance flag and prepares a state to work as a multi
 * instance callback. No callbacks are invoked at this point. The callbacks are
 * invoked once an instance for this state are registered via
 * @cpuhp_state_add_instance or @cpuhp_state_add_instance_nocalls.
 */
static inline int cpuhp_setup_state_multi(enum cpuhp_state state,
					  const char *name,
					  int (*startup)(unsigned int cpu,
							 struct hlist_node *node),
					  int (*teardown)(unsigned int cpu,
							  struct hlist_node *node))
{
	return __cpuhp_setup_state(state, name, false,
				   (void *) startup,
				   (void *) teardown, true);
}

int __cpuhp_state_add_instance(enum cpuhp_state state, struct hlist_node *node,
			       bool invoke);
int __cpuhp_state_add_instance_cpuslocked(enum cpuhp_state state,
					  struct hlist_node *node, bool invoke);

/**
 * cpuhp_state_add_instance - Add an instance for a state and invoke startup
 *                            callback.
 * @state:	The state for which the instance is installed
 * @node:	The node for this individual state.
 *
 * Installs the instance for the @state and invokes the startup callback on
 * the present cpus which have already reached the @state. The @state must have
 * been earlier marked as multi-instance by @cpuhp_setup_state_multi.
 */
static inline int cpuhp_state_add_instance(enum cpuhp_state state,
					   struct hlist_node *node)
{
	return __cpuhp_state_add_instance(state, node, true);
}

/**
 * cpuhp_state_add_instance_nocalls - Add an instance for a state without
 *                                    invoking the startup callback.
 * @state:	The state for which the instance is installed
 * @node:	The node for this individual state.
 *
 * Installs the instance for the @state The @state must have been earlier
 * marked as multi-instance by @cpuhp_setup_state_multi.
 */
static inline int cpuhp_state_add_instance_nocalls(enum cpuhp_state state,
						   struct hlist_node *node)
{
	return __cpuhp_state_add_instance(state, node, false);
}

static inline int
cpuhp_state_add_instance_nocalls_cpuslocked(enum cpuhp_state state,
					    struct hlist_node *node)
{
	return __cpuhp_state_add_instance_cpuslocked(state, node, false);
}

void __cpuhp_remove_state(enum cpuhp_state state, bool invoke);
void __cpuhp_remove_state_cpuslocked(enum cpuhp_state state, bool invoke);

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

static inline void cpuhp_remove_state_nocalls_cpuslocked(enum cpuhp_state state)
{
	__cpuhp_remove_state_cpuslocked(state, false);
}

/**
 * cpuhp_remove_multi_state - Remove hotplug multi state callback
 * @state:	The state for which the calls are removed
 *
 * Removes the callback functions from a multi state. This is the reverse of
 * cpuhp_setup_state_multi(). All instances should have been removed before
 * invoking this function.
 */
static inline void cpuhp_remove_multi_state(enum cpuhp_state state)
{
	__cpuhp_remove_state(state, false);
}

int __cpuhp_state_remove_instance(enum cpuhp_state state,
				  struct hlist_node *node, bool invoke);

/**
 * cpuhp_state_remove_instance - Remove hotplug instance from state and invoke
 *                               the teardown callback
 * @state:	The state from which the instance is removed
 * @node:	The node for this individual state.
 *
 * Removes the instance and invokes the teardown callback on the present cpus
 * which have already reached the @state.
 */
static inline int cpuhp_state_remove_instance(enum cpuhp_state state,
					      struct hlist_node *node)
{
	return __cpuhp_state_remove_instance(state, node, true);
}

/**
 * cpuhp_state_remove_instance_nocalls - Remove hotplug instance from state
 *					 without invoking the reatdown callback
 * @state:	The state from which the instance is removed
 * @node:	The node for this individual state.
 *
 * Removes the instance without invoking the teardown callback.
 */
static inline int cpuhp_state_remove_instance_nocalls(enum cpuhp_state state,
						      struct hlist_node *node)
{
	return __cpuhp_state_remove_instance(state, node, false);
}

#ifdef CONFIG_SMP
void cpuhp_online_idle(enum cpuhp_state state);
#else
static inline void cpuhp_online_idle(enum cpuhp_state state) { }
#endif

#endif
