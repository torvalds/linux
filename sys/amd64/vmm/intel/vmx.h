/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 NetApp, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _VMX_H_
#define	_VMX_H_

#include "vmcs.h"

struct pmap;

struct vmxctx {
	register_t	guest_rdi;		/* Guest state */
	register_t	guest_rsi;
	register_t	guest_rdx;
	register_t	guest_rcx;
	register_t	guest_r8;
	register_t	guest_r9;
	register_t	guest_rax;
	register_t	guest_rbx;
	register_t	guest_rbp;
	register_t	guest_r10;
	register_t	guest_r11;
	register_t	guest_r12;
	register_t	guest_r13;
	register_t	guest_r14;
	register_t	guest_r15;
	register_t	guest_cr2;
	register_t	guest_dr0;
	register_t	guest_dr1;
	register_t	guest_dr2;
	register_t	guest_dr3;
	register_t	guest_dr6;

	register_t	host_r15;		/* Host state */
	register_t	host_r14;
	register_t	host_r13;
	register_t	host_r12;
	register_t	host_rbp;
	register_t	host_rsp;
	register_t	host_rbx;
	register_t	host_dr0;
	register_t	host_dr1;
	register_t	host_dr2;
	register_t	host_dr3;
	register_t	host_dr6;
	register_t	host_dr7;
	uint64_t	host_debugctl;
	int		host_tf;

	int		inst_fail_status;

	/*
	 * The pmap needs to be deactivated in vmx_enter_guest()
	 * so keep a copy of the 'pmap' in each vmxctx.
	 */
	struct pmap	*pmap;
};

struct vmxcap {
	int	set;
	uint32_t proc_ctls;
	uint32_t proc_ctls2;
};

struct vmxstate {
	uint64_t nextrip;	/* next instruction to be executed by guest */
	int	lastcpu;	/* host cpu that this 'vcpu' last ran on */
	uint16_t vpid;
};

struct apic_page {
	uint32_t reg[PAGE_SIZE / 4];
};
CTASSERT(sizeof(struct apic_page) == PAGE_SIZE);

/* Posted Interrupt Descriptor (described in section 29.6 of the Intel SDM) */
struct pir_desc {
	uint64_t	pir[4];
	uint64_t	pending;
	uint64_t	unused[3];
} __aligned(64);
CTASSERT(sizeof(struct pir_desc) == 64);

/* Index into the 'guest_msrs[]' array */
enum {
	IDX_MSR_LSTAR,
	IDX_MSR_CSTAR,
	IDX_MSR_STAR,
	IDX_MSR_SF_MASK,
	IDX_MSR_KGSBASE,
	IDX_MSR_PAT,
	GUEST_MSR_NUM		/* must be the last enumeration */
};

/* virtual machine softc */
struct vmx {
	struct vmcs	vmcs[VM_MAXCPU];	/* one vmcs per virtual cpu */
	struct apic_page apic_page[VM_MAXCPU];	/* one apic page per vcpu */
	char		msr_bitmap[PAGE_SIZE];
	struct pir_desc	pir_desc[VM_MAXCPU];
	uint64_t	guest_msrs[VM_MAXCPU][GUEST_MSR_NUM];
	struct vmxctx	ctx[VM_MAXCPU];
	struct vmxcap	cap[VM_MAXCPU];
	struct vmxstate	state[VM_MAXCPU];
	uint64_t	eptp;
	struct vm	*vm;
	long		eptgen[MAXCPU];		/* cached pmap->pm_eptgen */
};
CTASSERT((offsetof(struct vmx, vmcs) & PAGE_MASK) == 0);
CTASSERT((offsetof(struct vmx, msr_bitmap) & PAGE_MASK) == 0);
CTASSERT((offsetof(struct vmx, pir_desc[0]) & 63) == 0);

#define	VMX_GUEST_VMEXIT	0
#define	VMX_VMRESUME_ERROR	1
#define	VMX_VMLAUNCH_ERROR	2
#define	VMX_INVEPT_ERROR	3
int	vmx_enter_guest(struct vmxctx *ctx, struct vmx *vmx, int launched);
void	vmx_call_isr(uintptr_t entry);

u_long	vmx_fix_cr0(u_long cr0);
u_long	vmx_fix_cr4(u_long cr4);

int	vmx_set_tsc_offset(struct vmx *vmx, int vcpu, uint64_t offset);

extern char	vmx_exit_guest[];
extern char	vmx_exit_guest_flush_rsb[];

#endif
