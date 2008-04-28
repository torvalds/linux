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
 * Copyright IBM Corp. 2008
 *
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 */

#ifndef __POWERPC_KVM_PPC_H__
#define __POWERPC_KVM_PPC_H__

/* This file exists just so we can dereference kvm_vcpu, avoiding nested header
 * dependencies. */

#include <linux/mutex.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/kvm_types.h>
#include <linux/kvm_host.h>

struct kvm_tlb {
	struct tlbe guest_tlb[PPC44x_TLB_SIZE];
	struct tlbe shadow_tlb[PPC44x_TLB_SIZE];
};

enum emulation_result {
	EMULATE_DONE,         /* no further processing */
	EMULATE_DO_MMIO,      /* kvm_run filled with MMIO request */
	EMULATE_DO_DCR,       /* kvm_run filled with DCR request */
	EMULATE_FAIL,         /* can't emulate this instruction */
};

extern const unsigned char exception_priority[];
extern const unsigned char priority_exception[];

extern int __kvmppc_vcpu_run(struct kvm_run *kvm_run, struct kvm_vcpu *vcpu);
extern char kvmppc_handlers_start[];
extern unsigned long kvmppc_handler_len;

extern void kvmppc_dump_vcpu(struct kvm_vcpu *vcpu);
extern int kvmppc_handle_load(struct kvm_run *run, struct kvm_vcpu *vcpu,
                              unsigned int rt, unsigned int bytes,
                              int is_bigendian);
extern int kvmppc_handle_store(struct kvm_run *run, struct kvm_vcpu *vcpu,
                               u32 val, unsigned int bytes, int is_bigendian);

extern int kvmppc_emulate_instruction(struct kvm_run *run,
                                      struct kvm_vcpu *vcpu);

extern void kvmppc_mmu_map(struct kvm_vcpu *vcpu, u64 gvaddr, gfn_t gfn,
                           u64 asid, u32 flags);
extern void kvmppc_mmu_invalidate(struct kvm_vcpu *vcpu, u64 eaddr, u64 asid);
extern void kvmppc_mmu_priv_switch(struct kvm_vcpu *vcpu, int usermode);

extern void kvmppc_check_and_deliver_interrupts(struct kvm_vcpu *vcpu);

static inline void kvmppc_queue_exception(struct kvm_vcpu *vcpu, int exception)
{
	unsigned int priority = exception_priority[exception];
	set_bit(priority, &vcpu->arch.pending_exceptions);
}

static inline void kvmppc_clear_exception(struct kvm_vcpu *vcpu, int exception)
{
	unsigned int priority = exception_priority[exception];
	clear_bit(priority, &vcpu->arch.pending_exceptions);
}

static inline void kvmppc_set_msr(struct kvm_vcpu *vcpu, u32 new_msr)
{
	if ((new_msr & MSR_PR) != (vcpu->arch.msr & MSR_PR))
		kvmppc_mmu_priv_switch(vcpu, new_msr & MSR_PR);

	vcpu->arch.msr = new_msr;
}

#endif /* __POWERPC_KVM_PPC_H__ */
