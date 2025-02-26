/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Type definitions for the Microsoft hypervisor.
 */
#ifndef _HV_HVHDK_H
#define _HV_HVHDK_H

#include <linux/build_bug.h>

#include "hvhdk_mini.h"
#include "hvgdk.h"

/* Bits for dirty mask of hv_vp_register_page */
#define HV_X64_REGISTER_CLASS_GENERAL	0
#define HV_X64_REGISTER_CLASS_IP	1
#define HV_X64_REGISTER_CLASS_XMM	2
#define HV_X64_REGISTER_CLASS_SEGMENT	3
#define HV_X64_REGISTER_CLASS_FLAGS	4

#define HV_VP_REGISTER_PAGE_VERSION_1	1u

struct hv_vp_register_page {
	u16 version;
	u8 isvalid;
	u8 rsvdz;
	u32 dirty;
	union {
		struct {
			/* General purpose registers
			 * (HV_X64_REGISTER_CLASS_GENERAL)
			 */
			union {
				struct {
					u64 rax;
					u64 rcx;
					u64 rdx;
					u64 rbx;
					u64 rsp;
					u64 rbp;
					u64 rsi;
					u64 rdi;
					u64 r8;
					u64 r9;
					u64 r10;
					u64 r11;
					u64 r12;
					u64 r13;
					u64 r14;
					u64 r15;
				} __packed;

				u64 gp_registers[16];
			};
			/* Instruction pointer (HV_X64_REGISTER_CLASS_IP) */
			u64 rip;
			/* Flags (HV_X64_REGISTER_CLASS_FLAGS) */
			u64 rflags;
		} __packed;

		u64 registers[18];
	};
	/* Volatile XMM registers (HV_X64_REGISTER_CLASS_XMM) */
	union {
		struct {
			struct hv_u128 xmm0;
			struct hv_u128 xmm1;
			struct hv_u128 xmm2;
			struct hv_u128 xmm3;
			struct hv_u128 xmm4;
			struct hv_u128 xmm5;
		} __packed;

		struct hv_u128 xmm_registers[6];
	};
	/* Segment registers (HV_X64_REGISTER_CLASS_SEGMENT) */
	union {
		struct {
			struct hv_x64_segment_register es;
			struct hv_x64_segment_register cs;
			struct hv_x64_segment_register ss;
			struct hv_x64_segment_register ds;
			struct hv_x64_segment_register fs;
			struct hv_x64_segment_register gs;
		} __packed;

		struct hv_x64_segment_register segment_registers[6];
	};
	/* Misc. control registers (cannot be set via this interface) */
	u64 cr0;
	u64 cr3;
	u64 cr4;
	u64 cr8;
	u64 efer;
	u64 dr7;
	union hv_x64_pending_interruption_register pending_interruption;
	union hv_x64_interrupt_state_register interrupt_state;
	u64 instruction_emulation_hints;
} __packed;

#define HV_PARTITION_PROCESSOR_FEATURES_BANKS 2

union hv_partition_processor_features {
	u64 as_uint64[HV_PARTITION_PROCESSOR_FEATURES_BANKS];
	struct {
		u64 sse3_support : 1;
		u64 lahf_sahf_support : 1;
		u64 ssse3_support : 1;
		u64 sse4_1_support : 1;
		u64 sse4_2_support : 1;
		u64 sse4a_support : 1;
		u64 xop_support : 1;
		u64 pop_cnt_support : 1;
		u64 cmpxchg16b_support : 1;
		u64 altmovcr8_support : 1;
		u64 lzcnt_support : 1;
		u64 mis_align_sse_support : 1;
		u64 mmx_ext_support : 1;
		u64 amd3dnow_support : 1;
		u64 extended_amd3dnow_support : 1;
		u64 page_1gb_support : 1;
		u64 aes_support : 1;
		u64 pclmulqdq_support : 1;
		u64 pcid_support : 1;
		u64 fma4_support : 1;
		u64 f16c_support : 1;
		u64 rd_rand_support : 1;
		u64 rd_wr_fs_gs_support : 1;
		u64 smep_support : 1;
		u64 enhanced_fast_string_support : 1;
		u64 bmi1_support : 1;
		u64 bmi2_support : 1;
		u64 hle_support_deprecated : 1;
		u64 rtm_support_deprecated : 1;
		u64 movbe_support : 1;
		u64 npiep1_support : 1;
		u64 dep_x87_fpu_save_support : 1;
		u64 rd_seed_support : 1;
		u64 adx_support : 1;
		u64 intel_prefetch_support : 1;
		u64 smap_support : 1;
		u64 hle_support : 1;
		u64 rtm_support : 1;
		u64 rdtscp_support : 1;
		u64 clflushopt_support : 1;
		u64 clwb_support : 1;
		u64 sha_support : 1;
		u64 x87_pointers_saved_support : 1;
		u64 invpcid_support : 1;
		u64 ibrs_support : 1;
		u64 stibp_support : 1;
		u64 ibpb_support: 1;
		u64 unrestricted_guest_support : 1;
		u64 mdd_support : 1;
		u64 fast_short_rep_mov_support : 1;
		u64 l1dcache_flush_support : 1;
		u64 rdcl_no_support : 1;
		u64 ibrs_all_support : 1;
		u64 skip_l1df_support : 1;
		u64 ssb_no_support : 1;
		u64 rsb_a_no_support : 1;
		u64 virt_spec_ctrl_support : 1;
		u64 rd_pid_support : 1;
		u64 umip_support : 1;
		u64 mbs_no_support : 1;
		u64 mb_clear_support : 1;
		u64 taa_no_support : 1;
		u64 tsx_ctrl_support : 1;
		/*
		 * N.B. The final processor feature bit in bank 0 is reserved to
		 * simplify potential downlevel backports.
		 */
		u64 reserved_bank0 : 1;

		/* N.B. Begin bank 1 processor features. */
		u64 acount_mcount_support : 1;
		u64 tsc_invariant_support : 1;
		u64 cl_zero_support : 1;
		u64 rdpru_support : 1;
		u64 la57_support : 1;
		u64 mbec_support : 1;
		u64 nested_virt_support : 1;
		u64 psfd_support : 1;
		u64 cet_ss_support : 1;
		u64 cet_ibt_support : 1;
		u64 vmx_exception_inject_support : 1;
		u64 enqcmd_support : 1;
		u64 umwait_tpause_support : 1;
		u64 movdiri_support : 1;
		u64 movdir64b_support : 1;
		u64 cldemote_support : 1;
		u64 serialize_support : 1;
		u64 tsc_deadline_tmr_support : 1;
		u64 tsc_adjust_support : 1;
		u64 fzlrep_movsb : 1;
		u64 fsrep_stosb : 1;
		u64 fsrep_cmpsb : 1;
		u64 reserved_bank1 : 42;
	} __packed;
};

union hv_partition_processor_xsave_features {
	struct {
		u64 xsave_support : 1;
		u64 xsaveopt_support : 1;
		u64 avx_support : 1;
		u64 reserved1 : 61;
	} __packed;
	u64 as_uint64;
};

struct hv_partition_creation_properties {
	union hv_partition_processor_features disabled_processor_features;
	union hv_partition_processor_xsave_features
		disabled_processor_xsave_features;
} __packed;

#define HV_PARTITION_SYNTHETIC_PROCESSOR_FEATURES_BANKS 1

union hv_partition_synthetic_processor_features {
	u64 as_uint64[HV_PARTITION_SYNTHETIC_PROCESSOR_FEATURES_BANKS];

	struct {
		u64 hypervisor_present : 1;
		/* Support for HV#1: (CPUID leaves 0x40000000 - 0x40000006)*/
		u64 hv1 : 1;
		u64 access_vp_run_time_reg : 1; /* HV_X64_MSR_VP_RUNTIME */
		u64 access_partition_reference_counter : 1; /* HV_X64_MSR_TIME_REF_COUNT */
		u64 access_synic_regs : 1; /* SINT-related registers */
		/*
		 * Access to HV_X64_MSR_STIMER0_CONFIG through
		 * HV_X64_MSR_STIMER3_COUNT.
		 */
		u64 access_synthetic_timer_regs : 1;
		u64 access_intr_ctrl_regs : 1; /* APIC MSRs and VP assist page*/
		/* HV_X64_MSR_GUEST_OS_ID and HV_X64_MSR_HYPERCALL */
		u64 access_hypercall_regs : 1;
		u64 access_vp_index : 1;
		u64 access_partition_reference_tsc : 1;
		u64 access_guest_idle_reg : 1;
		u64 access_frequency_regs : 1;
		u64 reserved_z12 : 1;
		u64 reserved_z13 : 1;
		u64 reserved_z14 : 1;
		u64 enable_extended_gva_ranges_for_flush_virtual_address_list : 1;
		u64 reserved_z16 : 1;
		u64 reserved_z17 : 1;
		/* Use fast hypercall output. Corresponds to privilege. */
		u64 fast_hypercall_output : 1;
		u64 reserved_z19 : 1;
		u64 start_virtual_processor : 1; /* Can start VPs */
		u64 reserved_z21 : 1;
		/* Synthetic timers in direct mode. */
		u64 direct_synthetic_timers : 1;
		u64 reserved_z23 : 1;
		u64 extended_processor_masks : 1;

		/* Enable various hypercalls */
		u64 tb_flush_hypercalls : 1;
		u64 synthetic_cluster_ipi : 1;
		u64 notify_long_spin_wait : 1;
		u64 query_numa_distance : 1;
		u64 signal_events : 1;
		u64 retarget_device_interrupt : 1;
		u64 restore_time : 1;

		/* EnlightenedVmcs nested enlightenment is supported. */
		u64 enlightened_vmcs : 1;
		u64 reserved : 31;
	} __packed;
};

#define HV_MAKE_COMPATIBILITY_VERSION(major_, minor_)	\
	((u32)((major_) << 8 | (minor_)))

#define HV_COMPATIBILITY_21_H2		HV_MAKE_COMPATIBILITY_VERSION(0X6, 0X9)

union hv_partition_isolation_properties {
	u64 as_uint64;
	struct {
		u64 isolation_type: 5;
		u64 isolation_host_type : 2;
		u64 rsvd_z: 5;
		u64 shared_gpa_boundary_page_number: 52;
	} __packed;
};

/*
 * Various isolation types supported by MSHV.
 */
#define HV_PARTITION_ISOLATION_TYPE_NONE            0
#define HV_PARTITION_ISOLATION_TYPE_SNP             2
#define HV_PARTITION_ISOLATION_TYPE_TDX             3

/*
 * Various host isolation types supported by MSHV.
 */
#define HV_PARTITION_ISOLATION_HOST_TYPE_NONE       0x0
#define HV_PARTITION_ISOLATION_HOST_TYPE_HARDWARE   0x1
#define HV_PARTITION_ISOLATION_HOST_TYPE_RESERVED   0x2

/* Note: Exo partition is enabled by default */
#define HV_PARTITION_CREATION_FLAG_EXO_PARTITION                    BIT(8)
#define HV_PARTITION_CREATION_FLAG_LAPIC_ENABLED                    BIT(13)
#define HV_PARTITION_CREATION_FLAG_INTERCEPT_MESSAGE_PAGE_ENABLED   BIT(19)
#define HV_PARTITION_CREATION_FLAG_X2APIC_CAPABLE                   BIT(22)

struct hv_input_create_partition {
	u64 flags;
	struct hv_proximity_domain_info proximity_domain_info;
	u32 compatibility_version;
	u32 padding;
	struct hv_partition_creation_properties partition_creation_properties;
	union hv_partition_isolation_properties isolation_properties;
} __packed;

struct hv_output_create_partition {
	u64 partition_id;
} __packed;

struct hv_input_initialize_partition {
	u64 partition_id;
} __packed;

struct hv_input_finalize_partition {
	u64 partition_id;
} __packed;

struct hv_input_delete_partition {
	u64 partition_id;
} __packed;

struct hv_input_get_partition_property {
	u64 partition_id;
	u32 property_code; /* enum hv_partition_property_code */
	u32 padding;
} __packed;

struct hv_output_get_partition_property {
	u64 property_value;
} __packed;

struct hv_input_set_partition_property {
	u64 partition_id;
	u32 property_code; /* enum hv_partition_property_code */
	u32 padding;
	u64 property_value;
} __packed;

enum hv_vp_state_page_type {
	HV_VP_STATE_PAGE_REGISTERS = 0,
	HV_VP_STATE_PAGE_INTERCEPT_MESSAGE = 1,
	HV_VP_STATE_PAGE_COUNT
};

struct hv_input_map_vp_state_page {
	u64 partition_id;
	u32 vp_index;
	u32 type; /* enum hv_vp_state_page_type */
} __packed;

struct hv_output_map_vp_state_page {
	u64 map_location; /* GPA page number */
} __packed;

struct hv_input_unmap_vp_state_page {
	u64 partition_id;
	u32 vp_index;
	u32 type; /* enum hv_vp_state_page_type */
} __packed;

struct hv_opaque_intercept_message {
	u32 vp_index;
} __packed;

enum hv_port_type {
	HV_PORT_TYPE_MESSAGE = 1,
	HV_PORT_TYPE_EVENT   = 2,
	HV_PORT_TYPE_MONITOR = 3,
	HV_PORT_TYPE_DOORBELL = 4	/* Root Partition only */
};

struct hv_port_info {
	u32 port_type; /* enum hv_port_type */
	u32 padding;
	union {
		struct {
			u32 target_sint;
			u32 target_vp;
			u64 rsvdz;
		} message_port_info;
		struct {
			u32 target_sint;
			u32 target_vp;
			u16 base_flag_number;
			u16 flag_count;
			u32 rsvdz;
		} event_port_info;
		struct {
			u64 monitor_address;
			u64 rsvdz;
		} monitor_port_info;
		struct {
			u32 target_sint;
			u32 target_vp;
			u64 rsvdz;
		} doorbell_port_info;
	};
} __packed;

struct hv_connection_info {
	u32 port_type;
	u32 padding;
	union {
		struct {
			u64 rsvdz;
		} message_connection_info;
		struct {
			u64 rsvdz;
		} event_connection_info;
		struct {
			u64 monitor_address;
		} monitor_connection_info;
		struct {
			u64 gpa;
			u64 trigger_value;
			u64 flags;
		} doorbell_connection_info;
	};
} __packed;

/* Define synthetic interrupt controller flag constants. */
#define HV_EVENT_FLAGS_COUNT		(256 * 8)
#define HV_EVENT_FLAGS_BYTE_COUNT	(256)
#define HV_EVENT_FLAGS32_COUNT		(256 / sizeof(u32))

/* linux side we create long version of flags to use long bit ops on flags */
#define HV_EVENT_FLAGS_UL_COUNT		(256 / sizeof(ulong))

/* Define the synthetic interrupt controller event flags format. */
union hv_synic_event_flags {
	unsigned char flags8[HV_EVENT_FLAGS_BYTE_COUNT];
	u32 flags32[HV_EVENT_FLAGS32_COUNT];
	ulong flags[HV_EVENT_FLAGS_UL_COUNT];  /* linux only */
};

struct hv_synic_event_flags_page {
	volatile union hv_synic_event_flags event_flags[HV_SYNIC_SINT_COUNT];
};

#define HV_SYNIC_EVENT_RING_MESSAGE_COUNT 63

struct hv_synic_event_ring {
	u8  signal_masked;
	u8  ring_full;
	u16 reserved_z;
	u32 data[HV_SYNIC_EVENT_RING_MESSAGE_COUNT];
} __packed;

struct hv_synic_event_ring_page {
	struct hv_synic_event_ring sint_event_ring[HV_SYNIC_SINT_COUNT];
};

/* Define SynIC control register. */
union hv_synic_scontrol {
	u64 as_uint64;
	struct {
		u64 enable : 1;
		u64 reserved : 63;
	} __packed;
};

/* Define the format of the SIEFP register */
union hv_synic_siefp {
	u64 as_uint64;
	struct {
		u64 siefp_enabled : 1;
		u64 preserved : 11;
		u64 base_siefp_gpa : 52;
	} __packed;
};

union hv_synic_sirbp {
	u64 as_uint64;
	struct {
		u64 sirbp_enabled : 1;
		u64 preserved : 11;
		u64 base_sirbp_gpa : 52;
	} __packed;
};

union hv_interrupt_control {
	u64 as_uint64;
	struct {
		u32 interrupt_type; /* enum hv_interrupt_type */
		u32 level_triggered : 1;
		u32 logical_dest_mode : 1;
		u32 rsvd : 30;
	} __packed;
};

struct hv_stimer_state {
	struct {
		u32 undelivered_msg_pending : 1;
		u32 reserved : 31;
	} __packed flags;
	u32 resvd;
	u64 config;
	u64 count;
	u64 adjustment;
	u64 undelivered_exp_time;
} __packed;

struct hv_synthetic_timers_state {
	struct hv_stimer_state timers[HV_SYNIC_STIMER_COUNT];
	u64 reserved[5];
} __packed;

union hv_input_delete_vp {
	u64 as_uint64[2];
	struct {
		u64 partition_id;
		u32 vp_index;
		u8 reserved[4];
	} __packed;
} __packed;

struct hv_input_assert_virtual_interrupt {
	u64 partition_id;
	union hv_interrupt_control control;
	u64 dest_addr; /* cpu's apic id */
	u32 vector;
	u8 target_vtl;
	u8 rsvd_z0;
	u16 rsvd_z1;
} __packed;

struct hv_input_create_port {
	u64 port_partition_id;
	union hv_port_id port_id;
	u8 port_vtl;
	u8 min_connection_vtl;
	u16 padding;
	u64 connection_partition_id;
	struct hv_port_info port_info;
	struct hv_proximity_domain_info proximity_domain_info;
} __packed;

union hv_input_delete_port {
	u64 as_uint64[2];
	struct {
		u64 port_partition_id;
		union hv_port_id port_id;
		u32 reserved;
	};
} __packed;

struct hv_input_connect_port {
	u64 connection_partition_id;
	union hv_connection_id connection_id;
	u8 connection_vtl;
	u8 rsvdz0;
	u16 rsvdz1;
	u64 port_partition_id;
	union hv_port_id port_id;
	u32 reserved2;
	struct hv_connection_info connection_info;
	struct hv_proximity_domain_info proximity_domain_info;
} __packed;

union hv_input_disconnect_port {
	u64 as_uint64[2];
	struct {
		u64 connection_partition_id;
		union hv_connection_id connection_id;
		u32 is_doorbell: 1;
		u32 reserved: 31;
	} __packed;
} __packed;

union hv_input_notify_port_ring_empty {
	u64 as_uint64;
	struct {
		u32 sint_index;
		u32 reserved;
	};
} __packed;

struct hv_vp_state_data_xsave {
	u64 flags;
	union hv_x64_xsave_xfem_register states;
} __packed;

/*
 * For getting and setting VP state, there are two options based on the state type:
 *
 *     1.) Data that is accessed by PFNs in the input hypercall page. This is used
 *         for state which may not fit into the hypercall pages.
 *     2.) Data that is accessed directly in the input\output hypercall pages.
 *         This is used for state that will always fit into the hypercall pages.
 *
 * In the future this could be dynamic based on the size if needed.
 *
 * Note these hypercalls have an 8-byte aligned variable header size as per the tlfs
 */

#define HV_GET_SET_VP_STATE_TYPE_PFN	BIT(31)

enum hv_get_set_vp_state_type {
	/* HvGetSetVpStateLocalInterruptControllerState - APIC/GIC state */
	HV_GET_SET_VP_STATE_LAPIC_STATE	     = 0 | HV_GET_SET_VP_STATE_TYPE_PFN,
	HV_GET_SET_VP_STATE_XSAVE	     = 1 | HV_GET_SET_VP_STATE_TYPE_PFN,
	HV_GET_SET_VP_STATE_SIM_PAGE	     = 2 | HV_GET_SET_VP_STATE_TYPE_PFN,
	HV_GET_SET_VP_STATE_SIEF_PAGE	     = 3 | HV_GET_SET_VP_STATE_TYPE_PFN,
	HV_GET_SET_VP_STATE_SYNTHETIC_TIMERS = 4,
};

struct hv_vp_state_data {
	u32 type;
	u32 rsvd;
	struct hv_vp_state_data_xsave xsave;
} __packed;

struct hv_input_get_vp_state {
	u64 partition_id;
	u32 vp_index;
	u8 input_vtl;
	u8 rsvd0;
	u16 rsvd1;
	struct hv_vp_state_data state_data;
	u64 output_data_pfns[];
} __packed;

union hv_output_get_vp_state {
	struct hv_synthetic_timers_state synthetic_timers_state;
} __packed;

union hv_input_set_vp_state_data {
	u64 pfns;
	u8 bytes;
} __packed;

struct hv_input_set_vp_state {
	u64 partition_id;
	u32 vp_index;
	u8 input_vtl;
	u8 rsvd0;
	u16 rsvd1;
	struct hv_vp_state_data state_data;
	union hv_input_set_vp_state_data data[];
} __packed;

/*
 * Dispatch state for the VP communicated by the hypervisor to the
 * VP-dispatching thread in the root on return from HVCALL_DISPATCH_VP.
 */
enum hv_vp_dispatch_state {
	HV_VP_DISPATCH_STATE_INVALID	= 0,
	HV_VP_DISPATCH_STATE_BLOCKED	= 1,
	HV_VP_DISPATCH_STATE_READY	= 2,
};

/*
 * Dispatch event that caused the current dispatch state on return from
 * HVCALL_DISPATCH_VP.
 */
enum hv_vp_dispatch_event {
	HV_VP_DISPATCH_EVENT_INVALID	= 0x00000000,
	HV_VP_DISPATCH_EVENT_SUSPEND	= 0x00000001,
	HV_VP_DISPATCH_EVENT_INTERCEPT	= 0x00000002,
};

#define HV_ROOT_SCHEDULER_MAX_VPS_PER_CHILD_PARTITION   1024
/* The maximum array size of HV_GENERIC_SET (vp_set) buffer */
#define HV_GENERIC_SET_QWORD_COUNT(max) (((((max) - 1) >> 6) + 1) + 2)

struct hv_vp_signal_bitset_scheduler_message {
	u64 partition_id;
	u32 overflow_count;
	u16 vp_count;
	u16 reserved;

#define BITSET_BUFFER_SIZE \
	HV_GENERIC_SET_QWORD_COUNT(HV_ROOT_SCHEDULER_MAX_VPS_PER_CHILD_PARTITION)
	union {
		struct hv_vpset bitset;
		u64 bitset_buffer[BITSET_BUFFER_SIZE];
	} vp_bitset;
#undef BITSET_BUFFER_SIZE
} __packed;

static_assert(sizeof(struct hv_vp_signal_bitset_scheduler_message) <=
	(sizeof(struct hv_message) - sizeof(struct hv_message_header)));

#define HV_MESSAGE_MAX_PARTITION_VP_PAIR_COUNT \
	(((sizeof(struct hv_message) - sizeof(struct hv_message_header)) / \
	 (sizeof(u64 /* partition id */) + sizeof(u32 /* vp index */))) - 1)

struct hv_vp_signal_pair_scheduler_message {
	u32 overflow_count;
	u8 vp_count;
	u8 reserved1[3];

	u64 partition_ids[HV_MESSAGE_MAX_PARTITION_VP_PAIR_COUNT];
	u32 vp_indexes[HV_MESSAGE_MAX_PARTITION_VP_PAIR_COUNT];

	u8 reserved2[4];
} __packed;

static_assert(sizeof(struct hv_vp_signal_pair_scheduler_message) ==
	(sizeof(struct hv_message) - sizeof(struct hv_message_header)));

/* Input and output structures for HVCALL_DISPATCH_VP */
#define HV_DISPATCH_VP_FLAG_CLEAR_INTERCEPT_SUSPEND	0x1
#define HV_DISPATCH_VP_FLAG_ENABLE_CALLER_INTERRUPTS	0x2
#define HV_DISPATCH_VP_FLAG_SET_CALLER_SPEC_CTRL	0x4
#define HV_DISPATCH_VP_FLAG_SKIP_VP_SPEC_FLUSH		0x8
#define HV_DISPATCH_VP_FLAG_SKIP_CALLER_SPEC_FLUSH	0x10
#define HV_DISPATCH_VP_FLAG_SKIP_CALLER_USER_SPEC_FLUSH	0x20

struct hv_input_dispatch_vp {
	u64 partition_id;
	u32 vp_index;
	u32 flags;
	u64 time_slice; /* in 100ns */
	u64 spec_ctrl;
} __packed;

struct hv_output_dispatch_vp {
	u32 dispatch_state; /* enum hv_vp_dispatch_state */
	u32 dispatch_event; /* enum hv_vp_dispatch_event */
} __packed;

#endif /* _HV_HVHDK_H */
