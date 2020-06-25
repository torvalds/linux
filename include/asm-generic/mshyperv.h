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
#include <asm/ptrace.h>
#include <asm/hyperv-tlfs.h>

struct ms_hyperv_info {
	u32 features;
	u32 misc_features;
	u32 hints;
	u32 nested_features;
	u32 max_vp_index;
	u32 max_lp_index;
};
extern struct ms_hyperv_info ms_hyperv;

extern u64 hv_do_hypercall(u64 control, void *inputaddr, void *outputaddr);
extern u64 hv_do_fast_hypercall8(u16 control, u64 input8);


/* Generate the guest OS identifier as described in the Hyper-V TLFS */
static inline  __u64 generate_guest_id(__u64 d_info1, __u64 kernel_version,
				       __u64 d_info2)
{
	__u64 guest_id = 0;

	guest_id = (((__u64)HV_LINUX_VENDOR_ID) << 48);
	guest_id |= (d_info1 << 48);
	guest_id |= (kernel_version << 16);
	guest_id |= d_info2;

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
		hv_signal_eom();
	}
}

void hv_setup_vmbus_irq(void (*handler)(void));
void hv_remove_vmbus_irq(void);
void hv_enable_vmbus_irq(void);
void hv_disable_vmbus_irq(void);

void hv_setup_kexec_handler(void (*handler)(void));
void hv_remove_kexec_handler(void);
void hv_setup_crash_handler(void (*handler)(struct pt_regs *regs));
void hv_remove_crash_handler(void);

#if IS_ENABLED(CONFIG_HYPERV)
/*
 * Hypervisor's notion of virtual processor ID is different from
 * Linux' notion of CPU ID. This information can only be retrieved
 * in the context of the calling CPU. Setup a map for easy access
 * to this information.
 */
extern u32 *hv_vp_index;
extern u32 hv_max_vp_index;

/* Sentinel value for an uninitialized entry in hv_vp_index array */
#define VP_INVAL	U32_MAX

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

static inline int cpumask_to_vpset(struct hv_vpset *vpset,
				    const struct cpumask *cpus)
{
	int cpu, vcpu, vcpu_bank, vcpu_offset, nr_bank = 1;

	/* valid_bank_mask can represent up to 64 banks */
	if (hv_max_vp_index / 64 >= 64)
		return 0;

	/*
	 * Clear all banks up to the maximum possible bank as hv_tlb_flush_ex
	 * structs are not cleared between calls, we risk flushing unneeded
	 * vCPUs otherwise.
	 */
	for (vcpu_bank = 0; vcpu_bank <= hv_max_vp_index / 64; vcpu_bank++)
		vpset->bank_contents[vcpu_bank] = 0;

	/*
	 * Some banks may end up being empty but this is acceptable.
	 */
	for_each_cpu(cpu, cpus) {
		vcpu = hv_cpu_number_to_vp_number(cpu);
		if (vcpu == VP_INVAL)
			return -1;
		vcpu_bank = vcpu / 64;
		vcpu_offset = vcpu % 64;
		__set_bit(vcpu_offset, (unsigned long *)
			  &vpset->bank_contents[vcpu_bank]);
		if (vcpu_bank >= nr_bank)
			nr_bank = vcpu_bank + 1;
	}
	vpset->valid_bank_mask = GENMASK_ULL(nr_bank - 1, 0);
	return nr_bank;
}

void hyperv_report_panic(struct pt_regs *regs, long err, bool in_die);
void hyperv_report_panic_msg(phys_addr_t pa, size_t size);
bool hv_is_hyperv_initialized(void);
bool hv_is_hibernation_supported(void);
void hyperv_cleanup(void);
void hv_setup_sched_clock(void *sched_clock);
#else /* CONFIG_HYPERV */
static inline bool hv_is_hyperv_initialized(void) { return false; }
static inline bool hv_is_hibernation_supported(void) { return false; }
static inline void hyperv_cleanup(void) {}
#endif /* CONFIG_HYPERV */

#if IS_ENABLED(CONFIG_HYPERV)
extern int hv_setup_stimer0_irq(int *irq, int *vector, void (*handler)(void));
extern void hv_remove_stimer0_irq(int irq);
#endif

#endif
