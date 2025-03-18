/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Type definitions for the Microsoft hypervisor.
 */
#ifndef _HV_HVGDK_MINI_H
#define _HV_HVGDK_MINI_H

#include <linux/types.h>
#include <linux/bits.h>

struct hv_u128 {
	u64 low_part;
	u64 high_part;
} __packed;

/* NOTE: when adding below, update hv_result_to_string() */
#define HV_STATUS_SUCCESS			    0x0
#define HV_STATUS_INVALID_HYPERCALL_CODE	    0x2
#define HV_STATUS_INVALID_HYPERCALL_INPUT	    0x3
#define HV_STATUS_INVALID_ALIGNMENT		    0x4
#define HV_STATUS_INVALID_PARAMETER		    0x5
#define HV_STATUS_ACCESS_DENIED			    0x6
#define HV_STATUS_INVALID_PARTITION_STATE	    0x7
#define HV_STATUS_OPERATION_DENIED		    0x8
#define HV_STATUS_UNKNOWN_PROPERTY		    0x9
#define HV_STATUS_PROPERTY_VALUE_OUT_OF_RANGE	    0xA
#define HV_STATUS_INSUFFICIENT_MEMORY		    0xB
#define HV_STATUS_INVALID_PARTITION_ID		    0xD
#define HV_STATUS_INVALID_VP_INDEX		    0xE
#define HV_STATUS_NOT_FOUND			    0x10
#define HV_STATUS_INVALID_PORT_ID		    0x11
#define HV_STATUS_INVALID_CONNECTION_ID		    0x12
#define HV_STATUS_INSUFFICIENT_BUFFERS		    0x13
#define HV_STATUS_NOT_ACKNOWLEDGED		    0x14
#define HV_STATUS_INVALID_VP_STATE		    0x15
#define HV_STATUS_NO_RESOURCES			    0x1D
#define HV_STATUS_PROCESSOR_FEATURE_NOT_SUPPORTED   0x20
#define HV_STATUS_INVALID_LP_INDEX		    0x41
#define HV_STATUS_INVALID_REGISTER_VALUE	    0x50
#define HV_STATUS_OPERATION_FAILED		    0x71
#define HV_STATUS_TIME_OUT			    0x78
#define HV_STATUS_CALL_PENDING			    0x79
#define HV_STATUS_VTL_ALREADY_ENABLED		    0x86

/*
 * The Hyper-V TimeRefCount register and the TSC
 * page provide a guest VM clock with 100ns tick rate
 */
#define HV_CLOCK_HZ (NSEC_PER_SEC / 100)

#define HV_HYP_PAGE_SHIFT		12
#define HV_HYP_PAGE_SIZE		BIT(HV_HYP_PAGE_SHIFT)
#define HV_HYP_PAGE_MASK		(~(HV_HYP_PAGE_SIZE - 1))
#define HV_HYP_LARGE_PAGE_SHIFT		21

#define HV_PARTITION_ID_INVALID		((u64)0)
#define HV_PARTITION_ID_SELF		((u64)-1)

/* Hyper-V specific model specific registers (MSRs) */

#if defined(CONFIG_X86)
/* HV_X64_SYNTHETIC_MSR */
#define HV_X64_MSR_GUEST_OS_ID			0x40000000
#define HV_X64_MSR_HYPERCALL			0x40000001
#define HV_X64_MSR_VP_INDEX			0x40000002
#define HV_X64_MSR_RESET			0x40000003
#define HV_X64_MSR_VP_RUNTIME			0x40000010
#define HV_X64_MSR_TIME_REF_COUNT		0x40000020
#define HV_X64_MSR_REFERENCE_TSC		0x40000021
#define HV_X64_MSR_TSC_FREQUENCY		0x40000022
#define HV_X64_MSR_APIC_FREQUENCY		0x40000023

/* Define the virtual APIC registers */
#define HV_X64_MSR_EOI				0x40000070
#define HV_X64_MSR_ICR				0x40000071
#define HV_X64_MSR_TPR				0x40000072
#define HV_X64_MSR_VP_ASSIST_PAGE		0x40000073

/* Define synthetic interrupt controller model specific registers. */
#define HV_X64_MSR_SCONTROL			0x40000080
#define HV_X64_MSR_SVERSION			0x40000081
#define HV_X64_MSR_SIEFP			0x40000082
#define HV_X64_MSR_SIMP				0x40000083
#define HV_X64_MSR_EOM				0x40000084
#define HV_X64_MSR_SIRBP			0x40000085
#define HV_X64_MSR_SINT0			0x40000090
#define HV_X64_MSR_SINT1			0x40000091
#define HV_X64_MSR_SINT2			0x40000092
#define HV_X64_MSR_SINT3			0x40000093
#define HV_X64_MSR_SINT4			0x40000094
#define HV_X64_MSR_SINT5			0x40000095
#define HV_X64_MSR_SINT6			0x40000096
#define HV_X64_MSR_SINT7			0x40000097
#define HV_X64_MSR_SINT8			0x40000098
#define HV_X64_MSR_SINT9			0x40000099
#define HV_X64_MSR_SINT10			0x4000009A
#define HV_X64_MSR_SINT11			0x4000009B
#define HV_X64_MSR_SINT12			0x4000009C
#define HV_X64_MSR_SINT13			0x4000009D
#define HV_X64_MSR_SINT14			0x4000009E
#define HV_X64_MSR_SINT15			0x4000009F

/* Define synthetic interrupt controller model specific registers for nested hypervisor */
#define HV_X64_MSR_NESTED_SCONTROL		0x40001080
#define HV_X64_MSR_NESTED_SVERSION		0x40001081
#define HV_X64_MSR_NESTED_SIEFP			0x40001082
#define HV_X64_MSR_NESTED_SIMP			0x40001083
#define HV_X64_MSR_NESTED_EOM			0x40001084
#define HV_X64_MSR_NESTED_SINT0			0x40001090

/*
 * Synthetic Timer MSRs. Four timers per vcpu.
 */
#define HV_X64_MSR_STIMER0_CONFIG		0x400000B0
#define HV_X64_MSR_STIMER0_COUNT		0x400000B1
#define HV_X64_MSR_STIMER1_CONFIG		0x400000B2
#define HV_X64_MSR_STIMER1_COUNT		0x400000B3
#define HV_X64_MSR_STIMER2_CONFIG		0x400000B4
#define HV_X64_MSR_STIMER2_COUNT		0x400000B5
#define HV_X64_MSR_STIMER3_CONFIG		0x400000B6
#define HV_X64_MSR_STIMER3_COUNT		0x400000B7

/* Hyper-V guest idle MSR */
#define HV_X64_MSR_GUEST_IDLE			0x400000F0

/* Hyper-V guest crash notification MSR's */
#define HV_X64_MSR_CRASH_P0			0x40000100
#define HV_X64_MSR_CRASH_P1			0x40000101
#define HV_X64_MSR_CRASH_P2			0x40000102
#define HV_X64_MSR_CRASH_P3			0x40000103
#define HV_X64_MSR_CRASH_P4			0x40000104
#define HV_X64_MSR_CRASH_CTL			0x40000105

#define HV_X64_MSR_HYPERCALL_ENABLE		0x00000001
#define HV_X64_MSR_HYPERCALL_PAGE_ADDRESS_SHIFT	12
#define HV_X64_MSR_HYPERCALL_PAGE_ADDRESS_MASK	\
		(~((1ull << HV_X64_MSR_HYPERCALL_PAGE_ADDRESS_SHIFT) - 1))

#define HV_X64_MSR_CRASH_PARAMS		\
		(1 + (HV_X64_MSR_CRASH_P4 - HV_X64_MSR_CRASH_P0))

#define HV_IPI_LOW_VECTOR	 0x10
#define HV_IPI_HIGH_VECTOR	 0xff

#define HV_X64_MSR_VP_ASSIST_PAGE_ENABLE	0x00000001
#define HV_X64_MSR_VP_ASSIST_PAGE_ADDRESS_SHIFT	12
#define HV_X64_MSR_VP_ASSIST_PAGE_ADDRESS_MASK	\
		(~((1ull << HV_X64_MSR_VP_ASSIST_PAGE_ADDRESS_SHIFT) - 1))

/* Hyper-V Enlightened VMCS version mask in nested features CPUID */
#define HV_X64_ENLIGHTENED_VMCS_VERSION		0xff

#define HV_X64_MSR_TSC_REFERENCE_ENABLE		0x00000001
#define HV_X64_MSR_TSC_REFERENCE_ADDRESS_SHIFT	12

/* Number of XMM registers used in hypercall input/output */
#define HV_HYPERCALL_MAX_XMM_REGISTERS		6

struct hv_reenlightenment_control {
	u64 vector : 8;
	u64 reserved1 : 8;
	u64 enabled : 1;
	u64 reserved2 : 15;
	u64 target_vp : 32;
}  __packed;

struct hv_tsc_emulation_status {	 /* HV_TSC_EMULATION_STATUS */
	u64 inprogress : 1;
	u64 reserved : 63;
} __packed;

struct hv_tsc_emulation_control {	 /* HV_TSC_INVARIANT_CONTROL */
	u64 enabled : 1;
	u64 reserved : 63;
} __packed;

/* TSC emulation after migration */
#define HV_X64_MSR_REENLIGHTENMENT_CONTROL	0x40000106
#define HV_X64_MSR_TSC_EMULATION_CONTROL	0x40000107
#define HV_X64_MSR_TSC_EMULATION_STATUS		0x40000108
#define HV_X64_MSR_TSC_INVARIANT_CONTROL	0x40000118
#define HV_EXPOSE_INVARIANT_TSC		BIT_ULL(0)

#endif /* CONFIG_X86 */

struct hv_output_get_partition_id {
	u64 partition_id;
} __packed;

/* HV_CRASH_CTL_REG_CONTENTS */
#define HV_CRASH_CTL_CRASH_NOTIFY_MSG		 BIT_ULL(62)
#define HV_CRASH_CTL_CRASH_NOTIFY		 BIT_ULL(63)

union hv_reference_tsc_msr {
	u64 as_uint64;
	struct {
		u64 enable : 1;
		u64 reserved : 11;
		u64 pfn : 52;
	} __packed;
};

/* The maximum number of sparse vCPU banks which can be encoded by 'struct hv_vpset' */
#define HV_MAX_SPARSE_VCPU_BANKS (64)
/* The number of vCPUs in one sparse bank */
#define HV_VCPUS_PER_SPARSE_BANK (64)

/*
 * Some of Hyper-V structs do not use hv_vpset where linux uses them.
 *
 * struct hv_vpset is usually used as part of hypercall input. The portion
 * that counts as "fixed size input header" vs. "variable size input header"
 * varies per hypercall. See comments at relevant hypercall call sites as to
 * how the "valid_bank_mask" field should be accounted.
 */
struct hv_vpset {	 /* HV_VP_SET */
	u64 format;
	u64 valid_bank_mask;
	u64 bank_contents[];
} __packed;

/*
 * Version info reported by hypervisor
 * Changed to a union for convenience
 */
union hv_hypervisor_version_info {
	struct {
		u32 build_number;

		u32 minor_version : 16;
		u32 major_version : 16;

		u32 service_pack;

		u32 service_number : 24;
		u32 service_branch : 8;
	};
	struct {
		u32 eax;
		u32 ebx;
		u32 ecx;
		u32 edx;
	};
};

/* HV_CPUID_FUNCTION */
#define HYPERV_CPUID_VENDOR_AND_MAX_FUNCTIONS	0x40000000
#define HYPERV_CPUID_INTERFACE			0x40000001
#define HYPERV_CPUID_VERSION			0x40000002
#define HYPERV_CPUID_FEATURES			0x40000003
#define HYPERV_CPUID_ENLIGHTMENT_INFO		0x40000004
#define HYPERV_CPUID_IMPLEMENT_LIMITS		0x40000005
#define HYPERV_CPUID_CPU_MANAGEMENT_FEATURES	0x40000007
#define HYPERV_CPUID_NESTED_FEATURES		0x4000000A
#define HYPERV_CPUID_ISOLATION_CONFIG		0x4000000C

#define HYPERV_CPUID_VIRT_STACK_INTERFACE	 0x40000081
#define HYPERV_VS_INTERFACE_EAX_SIGNATURE	 0x31235356  /* "VS#1" */

#define HYPERV_CPUID_VIRT_STACK_PROPERTIES	 0x40000082
/* Support for the extended IOAPIC RTE format */
#define HYPERV_VS_PROPERTIES_EAX_EXTENDED_IOAPIC_RTE	 BIT(2)

#define HYPERV_HYPERVISOR_PRESENT_BIT		 0x80000000
#define HYPERV_CPUID_MIN			 0x40000005
#define HYPERV_CPUID_MAX			 0x4000ffff

/*
 * HV_X64_HYPERVISOR_FEATURES (EAX), or
 * HV_PARTITION_PRIVILEGE_MASK [31-0]
 */
#define HV_MSR_VP_RUNTIME_AVAILABLE			BIT(0)
#define HV_MSR_TIME_REF_COUNT_AVAILABLE			BIT(1)
#define HV_MSR_SYNIC_AVAILABLE				BIT(2)
#define HV_MSR_SYNTIMER_AVAILABLE			BIT(3)
#define HV_MSR_APIC_ACCESS_AVAILABLE			BIT(4)
#define HV_MSR_HYPERCALL_AVAILABLE			BIT(5)
#define HV_MSR_VP_INDEX_AVAILABLE			BIT(6)
#define HV_MSR_RESET_AVAILABLE				BIT(7)
#define HV_MSR_STAT_PAGES_AVAILABLE			BIT(8)
#define HV_MSR_REFERENCE_TSC_AVAILABLE			BIT(9)
#define HV_MSR_GUEST_IDLE_AVAILABLE			BIT(10)
#define HV_ACCESS_FREQUENCY_MSRS			BIT(11)
#define HV_ACCESS_REENLIGHTENMENT			BIT(13)
#define HV_ACCESS_TSC_INVARIANT				BIT(15)

/*
 * HV_X64_HYPERVISOR_FEATURES (EBX), or
 * HV_PARTITION_PRIVILEGE_MASK [63-32]
 */
#define HV_CREATE_PARTITIONS				BIT(0)
#define HV_ACCESS_PARTITION_ID				BIT(1)
#define HV_ACCESS_MEMORY_POOL				BIT(2)
#define HV_ADJUST_MESSAGE_BUFFERS			BIT(3)
#define HV_POST_MESSAGES				BIT(4)
#define HV_SIGNAL_EVENTS				BIT(5)
#define HV_CREATE_PORT					BIT(6)
#define HV_CONNECT_PORT					BIT(7)
#define HV_ACCESS_STATS					BIT(8)
#define HV_DEBUGGING					BIT(11)
#define HV_CPU_MANAGEMENT				BIT(12)
#define HV_ENABLE_EXTENDED_HYPERCALLS			BIT(20)
#define HV_ISOLATION					BIT(22)

#if defined(CONFIG_X86)
/* HV_X64_HYPERVISOR_FEATURES (EDX) */
#define HV_X64_MWAIT_AVAILABLE				BIT(0)
#define HV_X64_GUEST_DEBUGGING_AVAILABLE		BIT(1)
#define HV_X64_PERF_MONITOR_AVAILABLE			BIT(2)
#define HV_X64_CPU_DYNAMIC_PARTITIONING_AVAILABLE	BIT(3)
#define HV_X64_HYPERCALL_XMM_INPUT_AVAILABLE		BIT(4)
#define HV_X64_GUEST_IDLE_STATE_AVAILABLE		BIT(5)
#define HV_FEATURE_FREQUENCY_MSRS_AVAILABLE		BIT(8)
#define HV_FEATURE_GUEST_CRASH_MSR_AVAILABLE		BIT(10)
#define HV_FEATURE_DEBUG_MSRS_AVAILABLE			BIT(11)
#define HV_FEATURE_EXT_GVA_RANGES_FLUSH			BIT(14)
/*
 * Support for returning hypercall output block via XMM
 * registers is available
 */
#define HV_X64_HYPERCALL_XMM_OUTPUT_AVAILABLE		BIT(15)
/* stimer Direct Mode is available */
#define HV_STIMER_DIRECT_MODE_AVAILABLE			BIT(19)

/*
 * Implementation recommendations. Indicates which behaviors the hypervisor
 * recommends the OS implement for optimal performance.
 * These are HYPERV_CPUID_ENLIGHTMENT_INFO.EAX bits.
 */
/* HV_X64_ENLIGHTENMENT_INFORMATION */
#define HV_X64_AS_SWITCH_RECOMMENDED			BIT(0)
#define HV_X64_LOCAL_TLB_FLUSH_RECOMMENDED		BIT(1)
#define HV_X64_REMOTE_TLB_FLUSH_RECOMMENDED		BIT(2)
#define HV_X64_APIC_ACCESS_RECOMMENDED			BIT(3)
#define HV_X64_SYSTEM_RESET_RECOMMENDED			BIT(4)
#define HV_X64_RELAXED_TIMING_RECOMMENDED		BIT(5)
#define HV_DEPRECATING_AEOI_RECOMMENDED			BIT(9)
#define HV_X64_CLUSTER_IPI_RECOMMENDED			BIT(10)
#define HV_X64_EX_PROCESSOR_MASKS_RECOMMENDED		BIT(11)
#define HV_X64_HYPERV_NESTED				BIT(12)
#define HV_X64_ENLIGHTENED_VMCS_RECOMMENDED		BIT(14)
#define HV_X64_USE_MMIO_HYPERCALLS			BIT(21)

/*
 * CPU management features identification.
 * These are HYPERV_CPUID_CPU_MANAGEMENT_FEATURES.EAX bits.
 */
#define HV_X64_START_LOGICAL_PROCESSOR			BIT(0)
#define HV_X64_CREATE_ROOT_VIRTUAL_PROCESSOR		BIT(1)
#define HV_X64_PERFORMANCE_COUNTER_SYNC			BIT(2)
#define HV_X64_RESERVED_IDENTITY_BIT			BIT(31)

/*
 * Virtual processor will never share a physical core with another virtual
 * processor, except for virtual processors that are reported as sibling SMT
 * threads.
 */
#define HV_X64_NO_NONARCH_CORESHARING			BIT(18)

/* Nested features. These are HYPERV_CPUID_NESTED_FEATURES.EAX bits. */
#define HV_X64_NESTED_DIRECT_FLUSH			BIT(17)
#define HV_X64_NESTED_GUEST_MAPPING_FLUSH		BIT(18)
#define HV_X64_NESTED_MSR_BITMAP			BIT(19)

/* Nested features #2. These are HYPERV_CPUID_NESTED_FEATURES.EBX bits. */
#define HV_X64_NESTED_EVMCS1_PERF_GLOBAL_CTRL		BIT(0)

/*
 * This is specific to AMD and specifies that enlightened TLB flush is
 * supported. If guest opts in to this feature, ASID invalidations only
 * flushes gva -> hpa mapping entries. To flush the TLB entries derived
 * from NPT, hypercalls should be used (HvFlushGuestPhysicalAddressSpace
 * or HvFlushGuestPhysicalAddressList).
 */
#define HV_X64_NESTED_ENLIGHTENED_TLB			BIT(22)

/* HYPERV_CPUID_ISOLATION_CONFIG.EAX bits. */
#define HV_PARAVISOR_PRESENT				BIT(0)

/* HYPERV_CPUID_ISOLATION_CONFIG.EBX bits. */
#define HV_ISOLATION_TYPE				GENMASK(3, 0)
#define HV_SHARED_GPA_BOUNDARY_ACTIVE			BIT(5)
#define HV_SHARED_GPA_BOUNDARY_BITS			GENMASK(11, 6)

/* HYPERV_CPUID_FEATURES.ECX bits. */
#define HV_VP_DISPATCH_INTERRUPT_INJECTION_AVAILABLE	BIT(9)
#define HV_VP_GHCB_ROOT_MAPPING_AVAILABLE		BIT(10)

enum hv_isolation_type {
	HV_ISOLATION_TYPE_NONE	= 0,	/* HV_PARTITION_ISOLATION_TYPE_NONE */
	HV_ISOLATION_TYPE_VBS	= 1,
	HV_ISOLATION_TYPE_SNP	= 2,
	HV_ISOLATION_TYPE_TDX	= 3
};

union hv_x64_msr_hypercall_contents {
	u64 as_uint64;
	struct {
		u64 enable : 1;
		u64 reserved : 11;
		u64 guest_physical_address : 52;
	} __packed;
};
#endif /* CONFIG_X86 */

#if defined(CONFIG_ARM64)
#define HV_FEATURE_GUEST_CRASH_MSR_AVAILABLE	BIT(8)
#define HV_STIMER_DIRECT_MODE_AVAILABLE		BIT(13)
#endif /* CONFIG_ARM64 */

#if defined(CONFIG_X86)
#define HV_MAXIMUM_PROCESSORS	    2048
#elif defined(CONFIG_ARM64) /* CONFIG_X86 */
#define HV_MAXIMUM_PROCESSORS	    320
#endif /* CONFIG_ARM64 */

#define HV_MAX_VP_INDEX			(HV_MAXIMUM_PROCESSORS - 1)
#define HV_VP_INDEX_SELF		((u32)-2)
#define HV_ANY_VP			((u32)-1)

union hv_vp_assist_msr_contents {	 /* HV_REGISTER_VP_ASSIST_PAGE */
	u64 as_uint64;
	struct {
		u64 enable : 1;
		u64 reserved : 11;
		u64 pfn : 52;
	} __packed;
};

/* Declare the various hypercall operations. */
/* HV_CALL_CODE */
#define HVCALL_FLUSH_VIRTUAL_ADDRESS_SPACE		0x0002
#define HVCALL_FLUSH_VIRTUAL_ADDRESS_LIST		0x0003
#define HVCALL_NOTIFY_LONG_SPIN_WAIT			0x0008
#define HVCALL_SEND_IPI					0x000b
#define HVCALL_ENABLE_VP_VTL				0x000f
#define HVCALL_FLUSH_VIRTUAL_ADDRESS_SPACE_EX		0x0013
#define HVCALL_FLUSH_VIRTUAL_ADDRESS_LIST_EX		0x0014
#define HVCALL_SEND_IPI_EX				0x0015
#define HVCALL_CREATE_PARTITION				0x0040
#define HVCALL_INITIALIZE_PARTITION			0x0041
#define HVCALL_FINALIZE_PARTITION			0x0042
#define HVCALL_DELETE_PARTITION				0x0043
#define HVCALL_GET_PARTITION_PROPERTY			0x0044
#define HVCALL_SET_PARTITION_PROPERTY			0x0045
#define HVCALL_GET_PARTITION_ID				0x0046
#define HVCALL_DEPOSIT_MEMORY				0x0048
#define HVCALL_WITHDRAW_MEMORY				0x0049
#define HVCALL_MAP_GPA_PAGES				0x004b
#define HVCALL_UNMAP_GPA_PAGES				0x004c
#define HVCALL_INSTALL_INTERCEPT			0x004d
#define HVCALL_CREATE_VP				0x004e
#define HVCALL_DELETE_VP				0x004f
#define HVCALL_GET_VP_REGISTERS				0x0050
#define HVCALL_SET_VP_REGISTERS				0x0051
#define HVCALL_TRANSLATE_VIRTUAL_ADDRESS		0x0052
#define HVCALL_CLEAR_VIRTUAL_INTERRUPT			0x0056
#define HVCALL_DELETE_PORT				0x0058
#define HVCALL_DISCONNECT_PORT				0x005b
#define HVCALL_POST_MESSAGE				0x005c
#define HVCALL_SIGNAL_EVENT				0x005d
#define HVCALL_POST_DEBUG_DATA				0x0069
#define HVCALL_RETRIEVE_DEBUG_DATA			0x006a
#define HVCALL_RESET_DEBUG_SESSION			0x006b
#define HVCALL_MAP_STATS_PAGE				0x006c
#define HVCALL_UNMAP_STATS_PAGE				0x006d
#define HVCALL_ADD_LOGICAL_PROCESSOR			0x0076
#define HVCALL_GET_SYSTEM_PROPERTY			0x007b
#define HVCALL_MAP_DEVICE_INTERRUPT			0x007c
#define HVCALL_UNMAP_DEVICE_INTERRUPT			0x007d
#define HVCALL_RETARGET_INTERRUPT			0x007e
#define HVCALL_NOTIFY_PORT_RING_EMPTY			0x008b
#define HVCALL_REGISTER_INTERCEPT_RESULT		0x0091
#define HVCALL_ASSERT_VIRTUAL_INTERRUPT			0x0094
#define HVCALL_CREATE_PORT				0x0095
#define HVCALL_CONNECT_PORT				0x0096
#define HVCALL_START_VP					0x0099
#define HVCALL_GET_VP_ID_FROM_APIC_ID			0x009a
#define HVCALL_FLUSH_GUEST_PHYSICAL_ADDRESS_SPACE	0x00af
#define HVCALL_FLUSH_GUEST_PHYSICAL_ADDRESS_LIST	0x00b0
#define HVCALL_SIGNAL_EVENT_DIRECT			0x00c0
#define HVCALL_POST_MESSAGE_DIRECT			0x00c1
#define HVCALL_DISPATCH_VP				0x00c2
#define HVCALL_GET_GPA_PAGES_ACCESS_STATES		0x00c9
#define HVCALL_ACQUIRE_SPARSE_SPA_PAGE_HOST_ACCESS	0x00d7
#define HVCALL_RELEASE_SPARSE_SPA_PAGE_HOST_ACCESS	0x00d8
#define HVCALL_MODIFY_SPARSE_GPA_PAGE_HOST_VISIBILITY	0x00db
#define HVCALL_MAP_VP_STATE_PAGE			0x00e1
#define HVCALL_UNMAP_VP_STATE_PAGE			0x00e2
#define HVCALL_GET_VP_STATE				0x00e3
#define HVCALL_SET_VP_STATE				0x00e4
#define HVCALL_GET_VP_CPUID_VALUES			0x00f4
#define HVCALL_MMIO_READ				0x0106
#define HVCALL_MMIO_WRITE				0x0107

/* HV_HYPERCALL_INPUT */
#define HV_HYPERCALL_RESULT_MASK	GENMASK_ULL(15, 0)
#define HV_HYPERCALL_FAST_BIT		BIT(16)
#define HV_HYPERCALL_VARHEAD_OFFSET	17
#define HV_HYPERCALL_VARHEAD_MASK	GENMASK_ULL(26, 17)
#define HV_HYPERCALL_RSVD0_MASK		GENMASK_ULL(31, 27)
#define HV_HYPERCALL_NESTED		BIT_ULL(31)
#define HV_HYPERCALL_REP_COMP_OFFSET	32
#define HV_HYPERCALL_REP_COMP_1		BIT_ULL(32)
#define HV_HYPERCALL_REP_COMP_MASK	GENMASK_ULL(43, 32)
#define HV_HYPERCALL_RSVD1_MASK		GENMASK_ULL(47, 44)
#define HV_HYPERCALL_REP_START_OFFSET	48
#define HV_HYPERCALL_REP_START_MASK	GENMASK_ULL(59, 48)
#define HV_HYPERCALL_RSVD2_MASK		GENMASK_ULL(63, 60)
#define HV_HYPERCALL_RSVD_MASK		(HV_HYPERCALL_RSVD0_MASK | \
					 HV_HYPERCALL_RSVD1_MASK | \
					 HV_HYPERCALL_RSVD2_MASK)

/* HvFlushGuestPhysicalAddressSpace hypercalls */
struct hv_guest_mapping_flush {
	u64 address_space;
	u64 flags;
} __packed;

/*
 *  HV_MAX_FLUSH_PAGES = "additional_pages" + 1. It's limited
 *  by the bitwidth of "additional_pages" in union hv_gpa_page_range.
 */
#define HV_MAX_FLUSH_PAGES (2048)
#define HV_GPA_PAGE_RANGE_PAGE_SIZE_2MB		0
#define HV_GPA_PAGE_RANGE_PAGE_SIZE_1GB		1

#define HV_FLUSH_ALL_PROCESSORS			BIT(0)
#define HV_FLUSH_ALL_VIRTUAL_ADDRESS_SPACES	BIT(1)
#define HV_FLUSH_NON_GLOBAL_MAPPINGS_ONLY	BIT(2)
#define HV_FLUSH_USE_EXTENDED_RANGE_FORMAT	BIT(3)

/* HvFlushGuestPhysicalAddressList, HvExtCallMemoryHeatHint hypercall */
union hv_gpa_page_range {
	u64 address_space;
	struct {
		u64 additional_pages : 11;
		u64 largepage : 1;
		u64 basepfn : 52;
	} page;
	struct {
		u64 reserved : 12;
		u64 page_size : 1;
		u64 reserved1 : 8;
		u64 base_large_pfn : 43;
	};
};

/*
 * All input flush parameters should be in single page. The max flush
 * count is equal with how many entries of union hv_gpa_page_range can
 * be populated into the input parameter page.
 */
#define HV_MAX_FLUSH_REP_COUNT ((HV_HYP_PAGE_SIZE - 2 * sizeof(u64)) / \
				sizeof(union hv_gpa_page_range))

struct hv_guest_mapping_flush_list {
	u64 address_space;
	u64 flags;
	union hv_gpa_page_range gpa_list[HV_MAX_FLUSH_REP_COUNT];
};

struct hv_tlb_flush {	 /* HV_INPUT_FLUSH_VIRTUAL_ADDRESS_LIST */
	u64 address_space;
	u64 flags;
	u64 processor_mask;
	u64 gva_list[];
} __packed;

/* HvFlushVirtualAddressSpaceEx, HvFlushVirtualAddressListEx hypercalls */
struct hv_tlb_flush_ex {
	u64 address_space;
	u64 flags;
	struct hv_vpset hv_vp_set;
	u64 gva_list[];
} __packed;

struct ms_hyperv_tsc_page {	 /* HV_REFERENCE_TSC_PAGE */
	volatile u32 tsc_sequence;
	u32 reserved1;
	volatile u64 tsc_scale;
	volatile s64 tsc_offset;
} __packed;

/* Define the number of synthetic interrupt sources. */
#define HV_SYNIC_SINT_COUNT (16)

/* Define the expected SynIC version. */
#define HV_SYNIC_VERSION_1		(0x1)
/* Valid SynIC vectors are 16-255. */
#define HV_SYNIC_FIRST_VALID_VECTOR	(16)

#define HV_SYNIC_CONTROL_ENABLE		(1ULL << 0)
#define HV_SYNIC_SIMP_ENABLE		(1ULL << 0)
#define HV_SYNIC_SIEFP_ENABLE		(1ULL << 0)
#define HV_SYNIC_SINT_MASKED		(1ULL << 16)
#define HV_SYNIC_SINT_AUTO_EOI		(1ULL << 17)
#define HV_SYNIC_SINT_VECTOR_MASK	(0xFF)

#

/* Hyper-V defined statically assigned SINTs */
#define HV_SYNIC_INTERCEPTION_SINT_INDEX 0x00000000
#define HV_SYNIC_IOMMU_FAULT_SINT_INDEX  0x00000001
#define HV_SYNIC_VMBUS_SINT_INDEX	 0x00000002
#define HV_SYNIC_FIRST_UNUSED_SINT_INDEX 0x00000005

/* mshv assigned SINT for doorbell */
#define HV_SYNIC_DOORBELL_SINT_INDEX     HV_SYNIC_FIRST_UNUSED_SINT_INDEX

enum hv_interrupt_type {
	HV_X64_INTERRUPT_TYPE_FIXED		= 0x0000,
	HV_X64_INTERRUPT_TYPE_LOWESTPRIORITY	= 0x0001,
	HV_X64_INTERRUPT_TYPE_SMI		= 0x0002,
	HV_X64_INTERRUPT_TYPE_REMOTEREAD	= 0x0003,
	HV_X64_INTERRUPT_TYPE_NMI		= 0x0004,
	HV_X64_INTERRUPT_TYPE_INIT		= 0x0005,
	HV_X64_INTERRUPT_TYPE_SIPI		= 0x0006,
	HV_X64_INTERRUPT_TYPE_EXTINT		= 0x0007,
	HV_X64_INTERRUPT_TYPE_LOCALINT0		= 0x0008,
	HV_X64_INTERRUPT_TYPE_LOCALINT1		= 0x0009,
	HV_X64_INTERRUPT_TYPE_MAXIMUM		= 0x000A,
};

/* Define synthetic interrupt source. */
union hv_synic_sint {
	u64 as_uint64;
	struct {
		u64 vector : 8;
		u64 reserved1 : 8;
		u64 masked : 1;
		u64 auto_eoi : 1;
		u64 polling : 1;
		u64 as_intercept : 1;
		u64 proxy : 1;
		u64 reserved2 : 43;
	} __packed;
};

union hv_x64_xsave_xfem_register {
	u64 as_uint64;
	struct {
		u32 low_uint32;
		u32 high_uint32;
	} __packed;
	struct {
		u64 legacy_x87 : 1;
		u64 legacy_sse : 1;
		u64 avx : 1;
		u64 mpx_bndreg : 1;
		u64 mpx_bndcsr : 1;
		u64 avx_512_op_mask : 1;
		u64 avx_512_zmmhi : 1;
		u64 avx_512_zmm16_31 : 1;
		u64 rsvd8_9 : 2;
		u64 pasid : 1;
		u64 cet_u : 1;
		u64 cet_s : 1;
		u64 rsvd13_16 : 4;
		u64 xtile_cfg : 1;
		u64 xtile_data : 1;
		u64 rsvd19_63 : 45;
	} __packed;
};

/* Synthetic timer configuration */
union hv_stimer_config {	 /* HV_X64_MSR_STIMER_CONFIG_CONTENTS */
	u64 as_uint64;
	struct {
		u64 enable : 1;
		u64 periodic : 1;
		u64 lazy : 1;
		u64 auto_enable : 1;
		u64 apic_vector : 8;
		u64 direct_mode : 1;
		u64 reserved_z0 : 3;
		u64 sintx : 4;
		u64 reserved_z1 : 44;
	} __packed;
};

/* Define the number of synthetic timers */
#define HV_SYNIC_STIMER_COUNT	(4)

/* Define port identifier type. */
union hv_port_id {
	u32 asu32;
	struct {
		u32 id : 24;
		u32 reserved : 8;
	} __packed u;
};

#define HV_MESSAGE_SIZE			(256)
#define HV_MESSAGE_PAYLOAD_BYTE_COUNT	(240)
#define HV_MESSAGE_PAYLOAD_QWORD_COUNT	(30)

/* Define hypervisor message types. */
enum hv_message_type {
	HVMSG_NONE				= 0x00000000,

	/* Memory access messages. */
	HVMSG_UNMAPPED_GPA			= 0x80000000,
	HVMSG_GPA_INTERCEPT			= 0x80000001,

	/* Timer notification messages. */
	HVMSG_TIMER_EXPIRED			= 0x80000010,

	/* Error messages. */
	HVMSG_INVALID_VP_REGISTER_VALUE		= 0x80000020,
	HVMSG_UNRECOVERABLE_EXCEPTION		= 0x80000021,
	HVMSG_UNSUPPORTED_FEATURE		= 0x80000022,

	/*
	 * Opaque intercept message. The original intercept message is only
	 * accessible from the mapped intercept message page.
	 */
	HVMSG_OPAQUE_INTERCEPT			= 0x8000003F,

	/* Trace buffer complete messages. */
	HVMSG_EVENTLOG_BUFFERCOMPLETE		= 0x80000040,

	/* Hypercall intercept */
	HVMSG_HYPERCALL_INTERCEPT		= 0x80000050,

	/* SynIC intercepts */
	HVMSG_SYNIC_EVENT_INTERCEPT		= 0x80000060,
	HVMSG_SYNIC_SINT_INTERCEPT		= 0x80000061,
	HVMSG_SYNIC_SINT_DELIVERABLE	= 0x80000062,

	/* Async call completion intercept */
	HVMSG_ASYNC_CALL_COMPLETION		= 0x80000070,

	/* Root scheduler messages */
	HVMSG_SCHEDULER_VP_SIGNAL_BITSET	= 0x80000100,
	HVMSG_SCHEDULER_VP_SIGNAL_PAIR		= 0x80000101,

	/* Platform-specific processor intercept messages. */
	HVMSG_X64_IO_PORT_INTERCEPT		= 0x80010000,
	HVMSG_X64_MSR_INTERCEPT			= 0x80010001,
	HVMSG_X64_CPUID_INTERCEPT		= 0x80010002,
	HVMSG_X64_EXCEPTION_INTERCEPT		= 0x80010003,
	HVMSG_X64_APIC_EOI			= 0x80010004,
	HVMSG_X64_LEGACY_FP_ERROR		= 0x80010005,
	HVMSG_X64_IOMMU_PRQ			= 0x80010006,
	HVMSG_X64_HALT				= 0x80010007,
	HVMSG_X64_INTERRUPTION_DELIVERABLE	= 0x80010008,
	HVMSG_X64_SIPI_INTERCEPT		= 0x80010009,
};

/* Define the format of the SIMP register */
union hv_synic_simp {
	u64 as_uint64;
	struct {
		u64 simp_enabled : 1;
		u64 preserved : 11;
		u64 base_simp_gpa : 52;
	} __packed;
};

union hv_message_flags {
	u8 asu8;
	struct {
		u8 msg_pending : 1;
		u8 reserved : 7;
	} __packed;
};

struct hv_message_header {
	u32 message_type;
	u8 payload_size;
	union hv_message_flags message_flags;
	u8 reserved[2];
	union {
		u64 sender;
		union hv_port_id port;
	};
} __packed;

/*
 * Message format for notifications delivered via
 * intercept message(as_intercept=1)
 */
struct hv_notification_message_payload {
	u32 sint_index;
} __packed;

struct hv_message {
	struct hv_message_header header;
	union {
		u64 payload[HV_MESSAGE_PAYLOAD_QWORD_COUNT];
	} u;
} __packed;

/* Define the synthetic interrupt message page layout. */
struct hv_message_page {
	struct hv_message sint_message[HV_SYNIC_SINT_COUNT];
} __packed;

/* Define timer message payload structure. */
struct hv_timer_message_payload {
	u32 timer_index;
	u32 reserved;
	u64 expiration_time;	/* When the timer expired */
	u64 delivery_time;	/* When the message was delivered */
} __packed;

struct hv_x64_segment_register {
	u64 base;
	u32 limit;
	u16 selector;
	union {
		struct {
			u16 segment_type : 4;
			u16 non_system_segment : 1;
			u16 descriptor_privilege_level : 2;
			u16 present : 1;
			u16 reserved : 4;
			u16 available : 1;
			u16 _long : 1;
			u16 _default : 1;
			u16 granularity : 1;
		} __packed;
		u16 attributes;
	};
} __packed;

struct hv_x64_table_register {
	u16 pad[3];
	u16 limit;
	u64 base;
} __packed;

#define HV_NORMAL_VTL	0

union hv_input_vtl {
	u8 as_uint8;
	struct {
		u8 target_vtl : 4;
		u8 use_target_vtl : 1;
		u8 reserved_z : 3;
	};
} __packed;

struct hv_init_vp_context {
	u64 rip;
	u64 rsp;
	u64 rflags;

	struct hv_x64_segment_register cs;
	struct hv_x64_segment_register ds;
	struct hv_x64_segment_register es;
	struct hv_x64_segment_register fs;
	struct hv_x64_segment_register gs;
	struct hv_x64_segment_register ss;
	struct hv_x64_segment_register tr;
	struct hv_x64_segment_register ldtr;

	struct hv_x64_table_register idtr;
	struct hv_x64_table_register gdtr;

	u64 efer;
	u64 cr0;
	u64 cr3;
	u64 cr4;
	u64 msr_cr_pat;
} __packed;

struct hv_enable_vp_vtl {
	u64				partition_id;
	u32				vp_index;
	union hv_input_vtl		target_vtl;
	u8				mbz0;
	u16				mbz1;
	struct hv_init_vp_context	vp_context;
} __packed;

struct hv_get_vp_from_apic_id_in {
	u64 partition_id;
	union hv_input_vtl target_vtl;
	u8 res[7];
	u32 apic_ids[];
} __packed;

struct hv_nested_enlightenments_control {
	struct {
		u32 directhypercall : 1;
		u32 reserved : 31;
	} __packed features;
	struct {
		u32 inter_partition_comm : 1;
		u32 reserved : 31;
	} __packed hypercall_controls;
} __packed;

/* Define virtual processor assist page structure. */
struct hv_vp_assist_page {
	u32 apic_assist;
	u32 reserved1;
	u32 vtl_entry_reason;
	u32 vtl_reserved;
	u64 vtl_ret_x64rax;
	u64 vtl_ret_x64rcx;
	struct hv_nested_enlightenments_control nested_control;
	u8 enlighten_vmentry;
	u8 reserved2[7];
	u64 current_nested_vmcs;
	u8 synthetic_time_unhalted_timer_expired;
	u8 reserved3[7];
	u8 virtualization_fault_information[40];
	u8 reserved4[8];
	u8 intercept_message[256];
	u8 vtl_ret_actions[256];
} __packed;

enum hv_register_name {
	/* Suspend Registers */
	HV_REGISTER_EXPLICIT_SUSPEND				= 0x00000000,
	HV_REGISTER_INTERCEPT_SUSPEND				= 0x00000001,
	HV_REGISTER_DISPATCH_SUSPEND				= 0x00000003,

	/* Version - 128-bit result same as CPUID 0x40000002 */
	HV_REGISTER_HYPERVISOR_VERSION				= 0x00000100,

	/* Feature Access (registers are 128 bits) - same as CPUID 0x40000003 - 0x4000000B */
	HV_REGISTER_PRIVILEGES_AND_FEATURES_INFO		= 0x00000200,
	HV_REGISTER_FEATURES_INFO				= 0x00000201,
	HV_REGISTER_IMPLEMENTATION_LIMITS_INFO			= 0x00000202,
	HV_REGISTER_HARDWARE_FEATURES_INFO			= 0x00000203,
	HV_REGISTER_CPU_MANAGEMENT_FEATURES_INFO		= 0x00000204,
	HV_REGISTER_SVM_FEATURES_INFO				= 0x00000205,
	HV_REGISTER_SKIP_LEVEL_FEATURES_INFO			= 0x00000206,
	HV_REGISTER_NESTED_VIRT_FEATURES_INFO			= 0x00000207,
	HV_REGISTER_IPT_FEATURES_INFO				= 0x00000208,

	/* Guest Crash Registers */
	HV_REGISTER_GUEST_CRASH_P0				= 0x00000210,
	HV_REGISTER_GUEST_CRASH_P1				= 0x00000211,
	HV_REGISTER_GUEST_CRASH_P2				= 0x00000212,
	HV_REGISTER_GUEST_CRASH_P3				= 0x00000213,
	HV_REGISTER_GUEST_CRASH_P4				= 0x00000214,
	HV_REGISTER_GUEST_CRASH_CTL				= 0x00000215,

	/* Misc */
	HV_REGISTER_VP_RUNTIME					= 0x00090000,
	HV_REGISTER_GUEST_OS_ID					= 0x00090002,
	HV_REGISTER_VP_INDEX					= 0x00090003,
	HV_REGISTER_TIME_REF_COUNT				= 0x00090004,
	HV_REGISTER_CPU_MANAGEMENT_VERSION			= 0x00090007,
	HV_REGISTER_VP_ASSIST_PAGE				= 0x00090013,
	HV_REGISTER_VP_ROOT_SIGNAL_COUNT			= 0x00090014,
	HV_REGISTER_REFERENCE_TSC				= 0x00090017,

	/* Hypervisor-defined Registers (Synic) */
	HV_REGISTER_SINT0					= 0x000A0000,
	HV_REGISTER_SINT1					= 0x000A0001,
	HV_REGISTER_SINT2					= 0x000A0002,
	HV_REGISTER_SINT3					= 0x000A0003,
	HV_REGISTER_SINT4					= 0x000A0004,
	HV_REGISTER_SINT5					= 0x000A0005,
	HV_REGISTER_SINT6					= 0x000A0006,
	HV_REGISTER_SINT7					= 0x000A0007,
	HV_REGISTER_SINT8					= 0x000A0008,
	HV_REGISTER_SINT9					= 0x000A0009,
	HV_REGISTER_SINT10					= 0x000A000A,
	HV_REGISTER_SINT11					= 0x000A000B,
	HV_REGISTER_SINT12					= 0x000A000C,
	HV_REGISTER_SINT13					= 0x000A000D,
	HV_REGISTER_SINT14					= 0x000A000E,
	HV_REGISTER_SINT15					= 0x000A000F,
	HV_REGISTER_SCONTROL					= 0x000A0010,
	HV_REGISTER_SVERSION					= 0x000A0011,
	HV_REGISTER_SIEFP					= 0x000A0012,
	HV_REGISTER_SIMP					= 0x000A0013,
	HV_REGISTER_EOM						= 0x000A0014,
	HV_REGISTER_SIRBP					= 0x000A0015,

	HV_REGISTER_NESTED_SINT0				= 0x000A1000,
	HV_REGISTER_NESTED_SINT1				= 0x000A1001,
	HV_REGISTER_NESTED_SINT2				= 0x000A1002,
	HV_REGISTER_NESTED_SINT3				= 0x000A1003,
	HV_REGISTER_NESTED_SINT4				= 0x000A1004,
	HV_REGISTER_NESTED_SINT5				= 0x000A1005,
	HV_REGISTER_NESTED_SINT6				= 0x000A1006,
	HV_REGISTER_NESTED_SINT7				= 0x000A1007,
	HV_REGISTER_NESTED_SINT8				= 0x000A1008,
	HV_REGISTER_NESTED_SINT9				= 0x000A1009,
	HV_REGISTER_NESTED_SINT10				= 0x000A100A,
	HV_REGISTER_NESTED_SINT11				= 0x000A100B,
	HV_REGISTER_NESTED_SINT12				= 0x000A100C,
	HV_REGISTER_NESTED_SINT13				= 0x000A100D,
	HV_REGISTER_NESTED_SINT14				= 0x000A100E,
	HV_REGISTER_NESTED_SINT15				= 0x000A100F,
	HV_REGISTER_NESTED_SCONTROL				= 0x000A1010,
	HV_REGISTER_NESTED_SVERSION				= 0x000A1011,
	HV_REGISTER_NESTED_SIFP					= 0x000A1012,
	HV_REGISTER_NESTED_SIPP					= 0x000A1013,
	HV_REGISTER_NESTED_EOM					= 0x000A1014,
	HV_REGISTER_NESTED_SIRBP				= 0x000a1015,

	/* Hypervisor-defined Registers (Synthetic Timers) */
	HV_REGISTER_STIMER0_CONFIG				= 0x000B0000,
	HV_REGISTER_STIMER0_COUNT				= 0x000B0001,

	/* VSM */
	HV_REGISTER_VSM_VP_STATUS				= 0x000D0003,
};

/*
 * Arch compatibility regs for use with hv_set/get_register
 */
#if defined(CONFIG_X86)

/*
 * To support arch-generic code calling hv_set/get_register:
 * - On x86, HV_MSR_ indicates an MSR accessed via rdmsrl/wrmsrl
 * - On ARM, HV_MSR_ indicates a VP register accessed via hypercall
 */
#define HV_MSR_CRASH_P0		(HV_X64_MSR_CRASH_P0)
#define HV_MSR_CRASH_P1		(HV_X64_MSR_CRASH_P1)
#define HV_MSR_CRASH_P2		(HV_X64_MSR_CRASH_P2)
#define HV_MSR_CRASH_P3		(HV_X64_MSR_CRASH_P3)
#define HV_MSR_CRASH_P4		(HV_X64_MSR_CRASH_P4)
#define HV_MSR_CRASH_CTL	(HV_X64_MSR_CRASH_CTL)

#define HV_MSR_VP_INDEX		(HV_X64_MSR_VP_INDEX)
#define HV_MSR_TIME_REF_COUNT	(HV_X64_MSR_TIME_REF_COUNT)
#define HV_MSR_REFERENCE_TSC	(HV_X64_MSR_REFERENCE_TSC)

#define HV_MSR_SINT0		(HV_X64_MSR_SINT0)
#define HV_MSR_SVERSION		(HV_X64_MSR_SVERSION)
#define HV_MSR_SCONTROL		(HV_X64_MSR_SCONTROL)
#define HV_MSR_SIEFP		(HV_X64_MSR_SIEFP)
#define HV_MSR_SIMP		(HV_X64_MSR_SIMP)
#define HV_MSR_EOM		(HV_X64_MSR_EOM)
#define HV_MSR_SIRBP		(HV_X64_MSR_SIRBP)

#define HV_MSR_NESTED_SCONTROL	(HV_X64_MSR_NESTED_SCONTROL)
#define HV_MSR_NESTED_SVERSION	(HV_X64_MSR_NESTED_SVERSION)
#define HV_MSR_NESTED_SIEFP	(HV_X64_MSR_NESTED_SIEFP)
#define HV_MSR_NESTED_SIMP	(HV_X64_MSR_NESTED_SIMP)
#define HV_MSR_NESTED_EOM	(HV_X64_MSR_NESTED_EOM)
#define HV_MSR_NESTED_SINT0	(HV_X64_MSR_NESTED_SINT0)

#define HV_MSR_STIMER0_CONFIG	(HV_X64_MSR_STIMER0_CONFIG)
#define HV_MSR_STIMER0_COUNT	(HV_X64_MSR_STIMER0_COUNT)

#elif defined(CONFIG_ARM64) /* CONFIG_X86 */

#define HV_MSR_CRASH_P0		(HV_REGISTER_GUEST_CRASH_P0)
#define HV_MSR_CRASH_P1		(HV_REGISTER_GUEST_CRASH_P1)
#define HV_MSR_CRASH_P2		(HV_REGISTER_GUEST_CRASH_P2)
#define HV_MSR_CRASH_P3		(HV_REGISTER_GUEST_CRASH_P3)
#define HV_MSR_CRASH_P4		(HV_REGISTER_GUEST_CRASH_P4)
#define HV_MSR_CRASH_CTL	(HV_REGISTER_GUEST_CRASH_CTL)

#define HV_MSR_VP_INDEX		(HV_REGISTER_VP_INDEX)
#define HV_MSR_TIME_REF_COUNT	(HV_REGISTER_TIME_REF_COUNT)
#define HV_MSR_REFERENCE_TSC	(HV_REGISTER_REFERENCE_TSC)

#define HV_MSR_SINT0		(HV_REGISTER_SINT0)
#define HV_MSR_SCONTROL		(HV_REGISTER_SCONTROL)
#define HV_MSR_SIEFP		(HV_REGISTER_SIEFP)
#define HV_MSR_SIMP		(HV_REGISTER_SIMP)
#define HV_MSR_EOM		(HV_REGISTER_EOM)
#define HV_MSR_SIRBP		(HV_REGISTER_SIRBP)

#define HV_MSR_STIMER0_CONFIG	(HV_REGISTER_STIMER0_CONFIG)
#define HV_MSR_STIMER0_COUNT	(HV_REGISTER_STIMER0_COUNT)

#endif /* CONFIG_ARM64 */

union hv_explicit_suspend_register {
	u64 as_uint64;
	struct {
		u64 suspended : 1;
		u64 reserved : 63;
	} __packed;
};

union hv_intercept_suspend_register {
	u64 as_uint64;
	struct {
		u64 suspended : 1;
		u64 reserved : 63;
	} __packed;
};

union hv_dispatch_suspend_register {
	u64 as_uint64;
	struct {
		u64 suspended : 1;
		u64 reserved : 63;
	} __packed;
};

union hv_arm64_pending_interruption_register {
	u64 as_uint64;
	struct {
		u64 interruption_pending : 1;
		u64 interruption_type: 1;
		u64 reserved : 30;
		u64 error_code : 32;
	} __packed;
};

union hv_arm64_interrupt_state_register {
	u64 as_uint64;
	struct {
		u64 interrupt_shadow : 1;
		u64 reserved : 63;
	} __packed;
};

union hv_arm64_pending_synthetic_exception_event {
	u64 as_uint64[2];
	struct {
		u8 event_pending : 1;
		u8 event_type : 3;
		u8 reserved : 4;
		u8 rsvd[3];
		u32 exception_type;
		u64 context;
	} __packed;
};

union hv_x64_interrupt_state_register {
	u64 as_uint64;
	struct {
		u64 interrupt_shadow : 1;
		u64 nmi_masked : 1;
		u64 reserved : 62;
	} __packed;
};

union hv_x64_pending_interruption_register {
	u64 as_uint64;
	struct {
		u32 interruption_pending : 1;
		u32 interruption_type : 3;
		u32 deliver_error_code : 1;
		u32 instruction_length : 4;
		u32 nested_event : 1;
		u32 reserved : 6;
		u32 interruption_vector : 16;
		u32 error_code;
	} __packed;
};

union hv_register_value {
	struct hv_u128 reg128;
	u64 reg64;
	u32 reg32;
	u16 reg16;
	u8 reg8;

	struct hv_x64_segment_register segment;
	struct hv_x64_table_register table;
	union hv_explicit_suspend_register explicit_suspend;
	union hv_intercept_suspend_register intercept_suspend;
	union hv_dispatch_suspend_register dispatch_suspend;
#ifdef CONFIG_ARM64
	union hv_arm64_interrupt_state_register interrupt_state;
	union hv_arm64_pending_interruption_register pending_interruption;
#endif
#ifdef CONFIG_X86
	union hv_x64_interrupt_state_register interrupt_state;
	union hv_x64_pending_interruption_register pending_interruption;
#endif
	union hv_arm64_pending_synthetic_exception_event pending_synthetic_exception_event;
};

/* NOTE: Linux helper struct - NOT from Hyper-V code. */
struct hv_output_get_vp_registers {
	DECLARE_FLEX_ARRAY(union hv_register_value, values);
};

#if defined(CONFIG_ARM64)
/* HvGetVpRegisters returns an array of these output elements */
struct hv_get_vp_registers_output {
	union {
		struct {
			u32 a;
			u32 b;
			u32 c;
			u32 d;
		} as32 __packed;
		struct {
			u64 low;
			u64 high;
		} as64 __packed;
	};
};

#endif /* CONFIG_ARM64 */

struct hv_register_assoc {
	u32 name;			/* enum hv_register_name */
	u32 reserved1;
	u64 reserved2;
	union hv_register_value value;
} __packed;

struct hv_input_get_vp_registers {
	u64 partition_id;
	u32 vp_index;
	union hv_input_vtl input_vtl;
	u8  rsvd_z8;
	u16 rsvd_z16;
	u32 names[];
} __packed;

struct hv_input_set_vp_registers {
	u64 partition_id;
	u32 vp_index;
	union hv_input_vtl input_vtl;
	u8  rsvd_z8;
	u16 rsvd_z16;
	struct hv_register_assoc elements[];
} __packed;

#define HV_UNMAP_GPA_LARGE_PAGE		0x2

/* HvCallSendSyntheticClusterIpi hypercall */
struct hv_send_ipi {	 /* HV_INPUT_SEND_SYNTHETIC_CLUSTER_IPI */
	u32 vector;
	u32 reserved;
	u64 cpu_mask;
} __packed;

#define	HV_X64_VTL_MASK			GENMASK(3, 0)

/* Hyper-V memory host visibility */
enum hv_mem_host_visibility {
	VMBUS_PAGE_NOT_VISIBLE		= 0,
	VMBUS_PAGE_VISIBLE_READ_ONLY	= 1,
	VMBUS_PAGE_VISIBLE_READ_WRITE	= 3
};

/* HvCallModifySparseGpaPageHostVisibility hypercall */
#define HV_MAX_MODIFY_GPA_REP_COUNT	((HV_HYP_PAGE_SIZE / sizeof(u64)) - 2)
struct hv_gpa_range_for_visibility {
	u64 partition_id;
	u32 host_visibility : 2;
	u32 reserved0 : 30;
	u32 reserved1;
	u64 gpa_page_list[HV_MAX_MODIFY_GPA_REP_COUNT];
} __packed;

#if defined(CONFIG_X86)
union hv_msi_address_register { /* HV_MSI_ADDRESS */
	u32 as_uint32;
	struct {
		u32 reserved1 : 2;
		u32 destination_mode : 1;
		u32 redirection_hint : 1;
		u32 reserved2 : 8;
		u32 destination_id : 8;
		u32 msi_base : 12;
	};
} __packed;

union hv_msi_data_register {	 /* HV_MSI_ENTRY.Data */
	u32 as_uint32;
	struct {
		u32 vector : 8;
		u32 delivery_mode : 3;
		u32 reserved1 : 3;
		u32 level_assert : 1;
		u32 trigger_mode : 1;
		u32 reserved2 : 16;
	};
} __packed;

union hv_msi_entry {	 /* HV_MSI_ENTRY */

	u64 as_uint64;
	struct {
		union hv_msi_address_register address;
		union hv_msi_data_register data;
	} __packed;
};

#elif defined(CONFIG_ARM64) /* CONFIG_X86 */

union hv_msi_entry {
	u64 as_uint64[2];
	struct {
		u64 address;
		u32 data;
		u32 reserved;
	} __packed;
};
#endif /* CONFIG_ARM64 */

union hv_ioapic_rte {
	u64 as_uint64;

	struct {
		u32 vector : 8;
		u32 delivery_mode : 3;
		u32 destination_mode : 1;
		u32 delivery_status : 1;
		u32 interrupt_polarity : 1;
		u32 remote_irr : 1;
		u32 trigger_mode : 1;
		u32 interrupt_mask : 1;
		u32 reserved1 : 15;

		u32 reserved2 : 24;
		u32 destination_id : 8;
	};

	struct {
		u32 low_uint32;
		u32 high_uint32;
	};
} __packed;

enum hv_interrupt_source {	 /* HV_INTERRUPT_SOURCE */
	HV_INTERRUPT_SOURCE_MSI = 1, /* MSI and MSI-X */
	HV_INTERRUPT_SOURCE_IOAPIC,
};

struct hv_interrupt_entry {	 /* HV_INTERRUPT_ENTRY */
	u32 source;
	u32 reserved1;
	union {
		union hv_msi_entry msi_entry;
		union hv_ioapic_rte ioapic_rte;
	};
} __packed;

#define HV_DEVICE_INTERRUPT_TARGET_MULTICAST		1
#define HV_DEVICE_INTERRUPT_TARGET_PROCESSOR_SET	2

struct hv_device_interrupt_target {	 /* HV_DEVICE_INTERRUPT_TARGET */
	u32 vector;
	u32 flags;		/* HV_DEVICE_INTERRUPT_TARGET_* above */
	union {
		u64 vp_mask;
		struct hv_vpset vp_set;
	};
} __packed;

struct hv_retarget_device_interrupt {	 /* HV_INPUT_RETARGET_DEVICE_INTERRUPT */
	u64 partition_id;		/* use "self" */
	u64 device_id;
	struct hv_interrupt_entry int_entry;
	u64 reserved2;
	struct hv_device_interrupt_target int_target;
} __packed __aligned(8);

enum hv_intercept_type {
#if defined(CONFIG_X86)
	HV_INTERCEPT_TYPE_X64_IO_PORT			= 0x00000000,
	HV_INTERCEPT_TYPE_X64_MSR			= 0x00000001,
	HV_INTERCEPT_TYPE_X64_CPUID			= 0x00000002,
#endif
	HV_INTERCEPT_TYPE_EXCEPTION			= 0x00000003,
	/* Used to be HV_INTERCEPT_TYPE_REGISTER */
	HV_INTERCEPT_TYPE_RESERVED0			= 0x00000004,
	HV_INTERCEPT_TYPE_MMIO				= 0x00000005,
#if defined(CONFIG_X86)
	HV_INTERCEPT_TYPE_X64_GLOBAL_CPUID		= 0x00000006,
	HV_INTERCEPT_TYPE_X64_APIC_SMI			= 0x00000007,
#endif
	HV_INTERCEPT_TYPE_HYPERCALL			= 0x00000008,
#if defined(CONFIG_X86)
	HV_INTERCEPT_TYPE_X64_APIC_INIT_SIPI		= 0x00000009,
	HV_INTERCEPT_MC_UPDATE_PATCH_LEVEL_MSR_READ	= 0x0000000A,
	HV_INTERCEPT_TYPE_X64_APIC_WRITE		= 0x0000000B,
	HV_INTERCEPT_TYPE_X64_MSR_INDEX			= 0x0000000C,
#endif
	HV_INTERCEPT_TYPE_MAX,
	HV_INTERCEPT_TYPE_INVALID			= 0xFFFFFFFF,
};

union hv_intercept_parameters {
	/*  HV_INTERCEPT_PARAMETERS is defined to be an 8-byte field. */
	u64 as_uint64;
#if defined(CONFIG_X86)
	/* HV_INTERCEPT_TYPE_X64_IO_PORT */
	u16 io_port;
	/* HV_INTERCEPT_TYPE_X64_CPUID */
	u32 cpuid_index;
	/* HV_INTERCEPT_TYPE_X64_APIC_WRITE */
	u32 apic_write_mask;
	/* HV_INTERCEPT_TYPE_EXCEPTION */
	u16 exception_vector;
	/* HV_INTERCEPT_TYPE_X64_MSR_INDEX */
	u32 msr_index;
#endif
	/* N.B. Other intercept types do not have any parameters. */
};

/* Data structures for HVCALL_MMIO_READ and HVCALL_MMIO_WRITE */
#define HV_HYPERCALL_MMIO_MAX_DATA_LENGTH 64

struct hv_mmio_read_input { /* HV_INPUT_MEMORY_MAPPED_IO_READ */
	u64 gpa;
	u32 size;
	u32 reserved;
} __packed;

struct hv_mmio_read_output {
	u8 data[HV_HYPERCALL_MMIO_MAX_DATA_LENGTH];
} __packed;

struct hv_mmio_write_input {
	u64 gpa;
	u32 size;
	u32 reserved;
	u8 data[HV_HYPERCALL_MMIO_MAX_DATA_LENGTH];
} __packed;

#endif /* _HV_HVGDK_MINI_H */
