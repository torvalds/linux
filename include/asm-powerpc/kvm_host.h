/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright IBM Corp. 2007
 *
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 */

#ifndef __POWERPC_KVM_HOST_H__
#define __POWERPC_KVM_HOST_H__

#include <linux/mutex.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/kvm_types.h>
#include <asm/kvm_asm.h>

#define KVM_MAX_VCPUS 1
#define KVM_MEMORY_SLOTS 32
/* memory slots that does not exposed to userspace */
#define KVM_PRIVATE_MEM_SLOTS 4

/* We don't currently support large pages. */
#define KVM_PAGES_PER_HPAGE (1<<31)

struct kvm;
struct kvm_run;
struct kvm_vcpu;

struct kvm_vm_stat {
	u32 remote_tlb_flush;
};

struct kvm_vcpu_stat {
	u32 sum_exits;
	u32 mmio_exits;
	u32 dcr_exits;
	u32 signal_exits;
	u32 light_exits;
	/* Account for special types of light exits: */
	u32 itlb_real_miss_exits;
	u32 itlb_virt_miss_exits;
	u32 dtlb_real_miss_exits;
	u32 dtlb_virt_miss_exits;
	u32 syscall_exits;
	u32 isi_exits;
	u32 dsi_exits;
	u32 emulated_inst_exits;
	u32 dec_exits;
	u32 ext_intr_exits;
	u32 halt_wakeup;
};

struct tlbe {
	u32 tid; /* Only the low 8 bits are used. */
	u32 word0;
	u32 word1;
	u32 word2;
};

struct kvm_arch {
};

struct kvm_vcpu_arch {
	/* Unmodified copy of the guest's TLB. */
	struct tlbe guest_tlb[PPC44x_TLB_SIZE];
	/* TLB that's actually used when the guest is running. */
	struct tlbe shadow_tlb[PPC44x_TLB_SIZE];
	/* Pages which are referenced in the shadow TLB. */
	struct page *shadow_pages[PPC44x_TLB_SIZE];
	/* Copy of the host's TLB. */
	struct tlbe host_tlb[PPC44x_TLB_SIZE];

	u32 host_stack;
	u32 host_pid;

	u64 fpr[32];
	u32 gpr[32];

	u32 pc;
	u32 cr;
	u32 ctr;
	u32 lr;
	u32 xer;

	u32 msr;
	u32 mmucr;
	u32 sprg0;
	u32 sprg1;
	u32 sprg2;
	u32 sprg3;
	u32 sprg4;
	u32 sprg5;
	u32 sprg6;
	u32 sprg7;
	u32 srr0;
	u32 srr1;
	u32 csrr0;
	u32 csrr1;
	u32 dsrr0;
	u32 dsrr1;
	u32 dear;
	u32 esr;
	u32 dec;
	u32 decar;
	u32 tbl;
	u32 tbu;
	u32 tcr;
	u32 tsr;
	u32 ivor[16];
	u32 ivpr;
	u32 pir;
	u32 pid;
	u32 pvr;
	u32 ccr0;
	u32 ccr1;
	u32 dbcr0;
	u32 dbcr1;

	u32 last_inst;
	u32 fault_dear;
	u32 fault_esr;
	gpa_t paddr_accessed;

	u8 io_gpr; /* GPR used as IO source/target */
	u8 mmio_is_bigendian;
	u8 dcr_needed;
	u8 dcr_is_write;

	u32 cpr0_cfgaddr; /* holds the last set cpr0_cfgaddr */

	struct timer_list dec_timer;
	unsigned long pending_exceptions;
};

struct kvm_guest_debug {
	int enabled;
	unsigned long bp[4];
	int singlestep;
};

#endif /* __POWERPC_KVM_HOST_H__ */
