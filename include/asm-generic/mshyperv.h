/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Linux-specific definitions for managing interactions with Microsoft's
 * Hyper-V hypervisor. The definitions in this file are architecture
 * independent. See arch/<arch>/include/asm/mshyperv.h for definitions
 * that are specific to architecture <arch>.
 *
 * Definitions that are specified in the Hyper-V Top Level Functional
 * Spec (TLFS) should not go in this file, but should instead go in
 * hyperv-tlfs.h.
 *
 * Copyright (C) 2019, Microsoft, Inc.
 *
 * Author : Michael Kelley <mikelley@microsoft.com>
 */

#ifndef _ASM_GENERIC_MSHYPERV_H
#define _ASM_GENERIC_MSHYPERV_H

#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/cpumask.h>
#include <linux/nmi.h>
#include <asm/ptrace.h>
#include <asm/hyperv-tlfs.h>

#define VTPM_BASE_ADDRESS 0xfed40000

struct ms_hyperv_info {
	u32 features;
	u32 priv_high;
	u32 misc_features;
	u32 hints;
	u32 nested_features;
	u32 max_vp_index;
	u32 max_lp_index;
	u32 isolation_config_a;
	union {
		u32 isolation_config_b;
		struct {
			u32 cvm_type : 4;
			u32 reserved1 : 1;
			u32 shared_gpa_boundary_active : 1;
			u32 shared_gpa_boundary_bits : 6;
			u32 reserved2 : 20;
		};
	};
	u64 shared_gpa_boundary;
};
extern struct ms_hyperv_info ms_hyperv;
extern bool hv_nested;

extern void * __percpu *hyperv_pcpu_input_arg;
extern void * __percpu *hyperv_pcpu_output_arg;

extern u64 hv_do_hypercall(u64 control, void *inputaddr, void *outputaddr);
extern u64 hv_do_fast_hypercall8(u16 control, u64 input8);
extern bool hv_isolation_type_snp(void);

/* Helper functions that provide a consistent pattern for checking Hyper-V hypercall status. */
static inline int hv_result(u64 status)
{
	return status & HV_HYPERCALL_RESULT_MASK;
}

static inline bool hv_result_success(u64 status)
{
	return hv_result(status) == HV_STATUS_SUCCESS;
}

static inline unsigned int hv_repcomp(u64 status)
{
	/* Bits [43:32] of status have 'Reps completed' data. */
	return (status & HV_HYPERCALL_REP_COMP_MASK) >>
			 HV_HYPERCALL_REP_COMP_OFFSET;
}

/*
 * Rep hypercalls. Callers of this functions are supposed to ensure that
 * rep_count and varhead_size comply with Hyper-V hypercall definition.
 */
static inline u64 hv_do_rep_hypercall(u16 code, u16 rep_count, u16 varhead_size,
				      void *input, void *output)
{
	u64 control = code;
	u64 status;
	u16 rep_comp;

	control |= (u64)varhead_size << HV_HYPERCALL_VARHEAD_OFFSET;
	control |= (u64)rep_count << HV_HYPERCALL_REP_COMP_OFFSET;

	do {
		status = hv_do_hypercall(control, input, output);
		if (!hv_result_success(status))
			return status;

		rep_comp = hv_repcomp(status);

		control &= ~HV_HYPERCALL_REP_START_MASK;
		control |= (u64)rep_comp << HV_HYPERCALL_REP_START_OFFSET;

		touch_nmi_watchdog();
	} while (rep_comp < rep_count);

	return status;
}

/* Generate the guest OS identifier as described in the Hyper-V TLFS */
static inline u64 hv_generate_guest_id(u64 kernel_version)
{
	u64 guest_id;

	guest_id = (((u64)HV_LINUX_VENDOR_ID) << 48);
	guest_id |= (kernel_version << 16);

	return guest_id;
}

/* Free the message slot and signal end-of-message if required */
static inline void vmbus_signal_eom(struct hv_message *msg, u32 old_msg_type)
{
	/*
	 * On crash we're reading some other CPU's message page and we need
	 * to be careful: this other CPU may already had cleared the header
	 * and the host may already had delivered some other message there.
	 * In case we blindly write msg->header.message_type we're going
	 * to lose it. We can still lose a message of the same type but
	 * we count on the fact that there can only be one
	 * CHANNELMSG_UNLOAD_RESPONSE and we don't care about other messages
	 * on crash.
	 */
	if (cmpxchg(&msg->header.message_type, old_msg_type,
		    HVMSG_NONE) != old_msg_type)
		return;

	/*
	 * The cmxchg() above does an implicit memory barrier to
	 * ensure the write to MessageType (ie set to
	 * HVMSG_NONE) happens before we read the
	 * MessagePending and EOMing. Otherwise, the EOMing
	 * will not deliver any more messages since there is
	 * no empty slot
	 */
	if (msg->header.message_flags.msg_pending) {
		/*
		 * This will cause message queue rescan to
		 * possibly deliver another msg from the
		 * hypervisor
		 */
		hv_set_register(HV_REGISTER_EOM, 0);
	}
}

void hv_setup_vmbus_handler(void (*handler)(void));
void hv_remove_vmbus_handler(void);
void hv_setup_stimer0_handler(void (*handler)(void));
void hv_remove_stimer0_handler(void);

void hv_setup_kexec_handler(void (*handler)(void));
void hv_remove_kexec_handler(void);
void hv_setup_crash_handler(void (*handler)(struct pt_regs *regs));
void hv_remove_crash_handler(void);

extern int vmbus_interrupt;
extern int vmbus_irq;

extern bool hv_root_partition;

#if IS_ENABLED(CONFIG_HYPERV)
/*
 * Hypervisor's notion of virtual processor ID is different from
 * Linux' notion of CPU ID. This information can only be retrieved
 * in the context of the calling CPU. Setup a map for easy access
 * to this information.
 */
extern u32 *hv_vp_index;
extern u32 hv_max_vp_index;

extern u64 (*hv_read_reference_counter)(void);

/* Sentinel value for an uninitialized entry in hv_vp_index array */
#define VP_INVAL	U32_MAX

int __init hv_common_init(void);
void __init hv_common_free(void);
int hv_common_cpu_init(unsigned int cpu);
int hv_common_cpu_die(unsigned int cpu);

void *hv_alloc_hyperv_page(void);
void *hv_alloc_hyperv_zeroed_page(void);
void hv_free_hyperv_page(void *addr);

/**
 * hv_cpu_number_to_vp_number() - Map CPU to VP.
 * @cpu_number: CPU number in Linux terms
 *
 * This function returns the mapping between the Linux processor
 * number and the hypervisor's virtual processor number, useful
 * in making hypercalls and such that talk about specific
 * processors.
 *
 * Return: Virtual processor number in Hyper-V terms
 */
static inline int hv_cpu_number_to_vp_number(int cpu_number)
{
	return hv_vp_index[cpu_number];
}

static inline int __cpumask_to_vpset(struct hv_vpset *vpset,
				    const struct cpumask *cpus,
				    bool (*func)(int cpu))
{
	int cpu, vcpu, vcpu_bank, vcpu_offset, nr_bank = 1;
	int max_vcpu_bank = hv_max_vp_index / HV_VCPUS_PER_SPARSE_BANK;

	/* vpset.valid_bank_mask can represent up to HV_MAX_SPARSE_VCPU_BANKS banks */
	if (max_vcpu_bank >= HV_MAX_SPARSE_VCPU_BANKS)
		return 0;

	/*
	 * Clear all banks up to the maximum possible bank as hv_tlb_flush_ex
	 * structs are not cleared between calls, we risk flushing unneeded
	 * vCPUs otherwise.
	 */
	for (vcpu_bank = 0; vcpu_bank <= max_vcpu_bank; vcpu_bank++)
		vpset->bank_contents[vcpu_bank] = 0;

	/*
	 * Some banks may end up being empty but this is acceptable.
	 */
	for_each_cpu(cpu, cpus) {
		if (func && func(cpu))
			continue;
		vcpu = hv_cpu_number_to_vp_number(cpu);
		if (vcpu == VP_INVAL)
			return -1;
		vcpu_bank = vcpu / HV_VCPUS_PER_SPARSE_BANK;
		vcpu_offset = vcpu % HV_VCPUS_PER_SPARSE_BANK;
		__set_bit(vcpu_offset, (unsigned long *)
			  &vpset->bank_contents[vcpu_bank]);
		if (vcpu_bank >= nr_bank)
			nr_bank = vcpu_bank + 1;
	}
	vpset->valid_bank_mask = GENMASK_ULL(nr_bank - 1, 0);
	return nr_bank;
}

/*
 * Convert a Linux cpumask into a Hyper-V VPset. In the _skip variant,
 * 'func' is called for each CPU present in cpumask.  If 'func' returns
 * true, that CPU is skipped -- i.e., that CPU from cpumask is *not*
 * added to the Hyper-V VPset. If 'func' is NULL, no CPUs are
 * skipped.
 */
static inline int cpumask_to_vpset(struct hv_vpset *vpset,
				    const struct cpumask *cpus)
{
	return __cpumask_to_vpset(vpset, cpus, NULL);
}

static inline int cpumask_to_vpset_skip(struct hv_vpset *vpset,
				    const struct cpumask *cpus,
				    bool (*func)(int cpu))
{
	return __cpumask_to_vpset(vpset, cpus, func);
}

void hyperv_report_panic(struct pt_regs *regs, long err, bool in_die);
bool hv_is_hyperv_initialized(void);
bool hv_is_hibernation_supported(void);
enum hv_isolation_type hv_get_isolation_type(void);
bool hv_is_isolation_supported(void);
bool hv_isolation_type_snp(void);
u64 hv_ghcb_hypercall(u64 control, void *input, void *output, u32 input_size);
void hyperv_cleanup(void);
bool hv_query_ext_cap(u64 cap_query);
void hv_setup_dma_ops(struct device *dev, bool coherent);
#else /* CONFIG_HYPERV */
static inline bool hv_is_hyperv_initialized(void) { return false; }
static inline bool hv_is_hibernation_supported(void) { return false; }
static inline void hyperv_cleanup(void) {}
static inline bool hv_is_isolation_supported(void) { return false; }
static inline enum hv_isolation_type hv_get_isolation_type(void)
{
	return HV_ISOLATION_TYPE_NONE;
}
#endif /* CONFIG_HYPERV */

#endif
