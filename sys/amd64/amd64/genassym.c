/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)genassym.c	5.11 (Berkeley) 5/10/91
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_hwpmc_hooks.h"
#include "opt_kstack_pages.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/assym.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/proc.h>
#ifdef	HWPMC_HOOKS
#include <sys/pmckern.h>
#endif
#include <sys/errno.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/socket.h>
#include <sys/resourcevar.h>
#include <sys/ucontext.h>
#include <machine/tss.h>
#include <sys/vmmeter.h>
#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <sys/proc.h>
#include <x86/apicreg.h>
#include <machine/cpu.h>
#include <machine/pcb.h>
#include <machine/sigframe.h>
#include <machine/proc.h>
#include <machine/segments.h>
#include <machine/efi.h>

ASSYM(P_VMSPACE, offsetof(struct proc, p_vmspace));
ASSYM(VM_PMAP, offsetof(struct vmspace, vm_pmap));
ASSYM(PM_ACTIVE, offsetof(struct pmap, pm_active));

ASSYM(P_MD, offsetof(struct proc, p_md));
ASSYM(MD_LDT, offsetof(struct mdproc, md_ldt));
ASSYM(MD_LDT_SD, offsetof(struct mdproc, md_ldt_sd));

ASSYM(MD_EFIRT_TMP, offsetof(struct mdthread, md_efirt_tmp));

ASSYM(TD_LOCK, offsetof(struct thread, td_lock));
ASSYM(TD_FLAGS, offsetof(struct thread, td_flags));
ASSYM(TD_PCB, offsetof(struct thread, td_pcb));
ASSYM(TD_PFLAGS, offsetof(struct thread, td_pflags));
ASSYM(TD_PROC, offsetof(struct thread, td_proc));
ASSYM(TD_FRAME, offsetof(struct thread, td_frame));
ASSYM(TD_MD, offsetof(struct thread, td_md));

ASSYM(TDF_ASTPENDING, TDF_ASTPENDING);
ASSYM(TDF_NEEDRESCHED, TDF_NEEDRESCHED);

ASSYM(TDP_CALLCHAIN, TDP_CALLCHAIN);
ASSYM(TDP_KTHREAD, TDP_KTHREAD);

ASSYM(PAGE_SIZE, PAGE_SIZE);
ASSYM(NPTEPG, NPTEPG);
ASSYM(NPDEPG, NPDEPG);
ASSYM(addr_PTmap, addr_PTmap);
ASSYM(addr_PDmap, addr_PDmap);
ASSYM(addr_PDPmap, addr_PDPmap);
ASSYM(addr_PML4map, addr_PML4map);
ASSYM(addr_PML4pml4e, addr_PML4pml4e);
ASSYM(PDESIZE, sizeof(pd_entry_t));
ASSYM(PTESIZE, sizeof(pt_entry_t));
ASSYM(PAGE_SHIFT, PAGE_SHIFT);
ASSYM(PAGE_MASK, PAGE_MASK);
ASSYM(PDRSHIFT, PDRSHIFT);
ASSYM(PDPSHIFT, PDPSHIFT);
ASSYM(PML4SHIFT, PML4SHIFT);
ASSYM(val_KPDPI, KPDPI);
ASSYM(val_KPML4I, KPML4I);
ASSYM(val_PML4PML4I, PML4PML4I);
ASSYM(VM_MAXUSER_ADDRESS, VM_MAXUSER_ADDRESS);
ASSYM(KERNBASE, KERNBASE);
ASSYM(DMAP_MIN_ADDRESS, DMAP_MIN_ADDRESS);
ASSYM(DMAP_MAX_ADDRESS, DMAP_MAX_ADDRESS);

ASSYM(PCB_R15, offsetof(struct pcb, pcb_r15));
ASSYM(PCB_R14, offsetof(struct pcb, pcb_r14));
ASSYM(PCB_R13, offsetof(struct pcb, pcb_r13));
ASSYM(PCB_R12, offsetof(struct pcb, pcb_r12));
ASSYM(PCB_RBP, offsetof(struct pcb, pcb_rbp));
ASSYM(PCB_RSP, offsetof(struct pcb, pcb_rsp));
ASSYM(PCB_RBX, offsetof(struct pcb, pcb_rbx));
ASSYM(PCB_RIP, offsetof(struct pcb, pcb_rip));
ASSYM(PCB_FSBASE, offsetof(struct pcb, pcb_fsbase));
ASSYM(PCB_GSBASE, offsetof(struct pcb, pcb_gsbase));
ASSYM(PCB_KGSBASE, offsetof(struct pcb, pcb_kgsbase));
ASSYM(PCB_CR0, offsetof(struct pcb, pcb_cr0));
ASSYM(PCB_CR2, offsetof(struct pcb, pcb_cr2));
ASSYM(PCB_CR3, offsetof(struct pcb, pcb_cr3));
ASSYM(PCB_CR4, offsetof(struct pcb, pcb_cr4));
ASSYM(PCB_DR0, offsetof(struct pcb, pcb_dr0));
ASSYM(PCB_DR1, offsetof(struct pcb, pcb_dr1));
ASSYM(PCB_DR2, offsetof(struct pcb, pcb_dr2));
ASSYM(PCB_DR3, offsetof(struct pcb, pcb_dr3));
ASSYM(PCB_DR6, offsetof(struct pcb, pcb_dr6));
ASSYM(PCB_DR7, offsetof(struct pcb, pcb_dr7));
ASSYM(PCB_GDT, offsetof(struct pcb, pcb_gdt));
ASSYM(PCB_IDT, offsetof(struct pcb, pcb_idt));
ASSYM(PCB_LDT, offsetof(struct pcb, pcb_ldt));
ASSYM(PCB_TR, offsetof(struct pcb, pcb_tr));
ASSYM(PCB_FLAGS, offsetof(struct pcb, pcb_flags));
ASSYM(PCB_ONFAULT, offsetof(struct pcb, pcb_onfault));
ASSYM(PCB_SAVED_UCR3, offsetof(struct pcb, pcb_saved_ucr3));
ASSYM(PCB_TSSP, offsetof(struct pcb, pcb_tssp));
ASSYM(PCB_SAVEFPU, offsetof(struct pcb, pcb_save));
ASSYM(PCB_EFER, offsetof(struct pcb, pcb_efer));
ASSYM(PCB_STAR, offsetof(struct pcb, pcb_star));
ASSYM(PCB_LSTAR, offsetof(struct pcb, pcb_lstar));
ASSYM(PCB_CSTAR, offsetof(struct pcb, pcb_cstar));
ASSYM(PCB_SFMASK, offsetof(struct pcb, pcb_sfmask));
ASSYM(PCB_SIZE, sizeof(struct pcb));
ASSYM(PCB_FULL_IRET, PCB_FULL_IRET);
ASSYM(PCB_DBREGS, PCB_DBREGS);
ASSYM(PCB_32BIT, PCB_32BIT);

ASSYM(TSS_RSP0, offsetof(struct amd64tss, tss_rsp0));

ASSYM(TF_R15, offsetof(struct trapframe, tf_r15));
ASSYM(TF_R14, offsetof(struct trapframe, tf_r14));
ASSYM(TF_R13, offsetof(struct trapframe, tf_r13));
ASSYM(TF_R12, offsetof(struct trapframe, tf_r12));
ASSYM(TF_R11, offsetof(struct trapframe, tf_r11));
ASSYM(TF_R10, offsetof(struct trapframe, tf_r10));
ASSYM(TF_R9, offsetof(struct trapframe, tf_r9));
ASSYM(TF_R8, offsetof(struct trapframe, tf_r8));
ASSYM(TF_RDI, offsetof(struct trapframe, tf_rdi));
ASSYM(TF_RSI, offsetof(struct trapframe, tf_rsi));
ASSYM(TF_RBP, offsetof(struct trapframe, tf_rbp));
ASSYM(TF_RBX, offsetof(struct trapframe, tf_rbx));
ASSYM(TF_RDX, offsetof(struct trapframe, tf_rdx));
ASSYM(TF_RCX, offsetof(struct trapframe, tf_rcx));
ASSYM(TF_RAX, offsetof(struct trapframe, tf_rax));
ASSYM(TF_TRAPNO, offsetof(struct trapframe, tf_trapno));
ASSYM(TF_ADDR, offsetof(struct trapframe, tf_addr));
ASSYM(TF_ERR, offsetof(struct trapframe, tf_err));
ASSYM(TF_RIP, offsetof(struct trapframe, tf_rip));
ASSYM(TF_CS, offsetof(struct trapframe, tf_cs));
ASSYM(TF_RFLAGS, offsetof(struct trapframe, tf_rflags));
ASSYM(TF_RSP, offsetof(struct trapframe, tf_rsp));
ASSYM(TF_SS, offsetof(struct trapframe, tf_ss));
ASSYM(TF_DS, offsetof(struct trapframe, tf_ds));
ASSYM(TF_ES, offsetof(struct trapframe, tf_es));
ASSYM(TF_FS, offsetof(struct trapframe, tf_fs));
ASSYM(TF_GS, offsetof(struct trapframe, tf_gs));
ASSYM(TF_FLAGS, offsetof(struct trapframe, tf_flags));
ASSYM(TF_SIZE, sizeof(struct trapframe));
ASSYM(TF_HASSEGS, TF_HASSEGS);

ASSYM(PTI_RDX, offsetof(struct pti_frame, pti_rdx));
ASSYM(PTI_RAX, offsetof(struct pti_frame, pti_rax));
ASSYM(PTI_ERR, offsetof(struct pti_frame, pti_err));
ASSYM(PTI_RIP, offsetof(struct pti_frame, pti_rip));
ASSYM(PTI_CS, offsetof(struct pti_frame, pti_cs));
ASSYM(PTI_RFLAGS, offsetof(struct pti_frame, pti_rflags));
ASSYM(PTI_RSP, offsetof(struct pti_frame, pti_rsp));
ASSYM(PTI_SS, offsetof(struct pti_frame, pti_ss));
ASSYM(PTI_SIZE, sizeof(struct pti_frame));

ASSYM(SIGF_HANDLER, offsetof(struct sigframe, sf_ahu.sf_handler));
ASSYM(SIGF_UC, offsetof(struct sigframe, sf_uc));
ASSYM(UC_EFLAGS, offsetof(ucontext_t, uc_mcontext.mc_rflags));
ASSYM(ENOENT, ENOENT);
ASSYM(EFAULT, EFAULT);
ASSYM(ENAMETOOLONG, ENAMETOOLONG);
ASSYM(MAXCOMLEN, MAXCOMLEN);
ASSYM(MAXPATHLEN, MAXPATHLEN);
ASSYM(PC_SIZEOF, sizeof(struct pcpu));
ASSYM(PC_PRVSPACE, offsetof(struct pcpu, pc_prvspace));
ASSYM(PC_CURTHREAD, offsetof(struct pcpu, pc_curthread));
ASSYM(PC_FPCURTHREAD, offsetof(struct pcpu, pc_fpcurthread));
ASSYM(PC_IDLETHREAD, offsetof(struct pcpu, pc_idlethread));
ASSYM(PC_CURPCB, offsetof(struct pcpu, pc_curpcb));
ASSYM(PC_CPUID, offsetof(struct pcpu, pc_cpuid));
ASSYM(PC_SCRATCH_RSP, offsetof(struct pcpu, pc_scratch_rsp));
ASSYM(PC_SCRATCH_RAX, offsetof(struct pcpu, pc_scratch_rax));
ASSYM(PC_CURPMAP, offsetof(struct pcpu, pc_curpmap));
ASSYM(PC_TSSP, offsetof(struct pcpu, pc_tssp));
ASSYM(PC_RSP0, offsetof(struct pcpu, pc_rsp0));
ASSYM(PC_FS32P, offsetof(struct pcpu, pc_fs32p));
ASSYM(PC_GS32P, offsetof(struct pcpu, pc_gs32p));
ASSYM(PC_LDT, offsetof(struct pcpu, pc_ldt));
ASSYM(PC_COMMONTSSP, offsetof(struct pcpu, pc_commontssp));
ASSYM(PC_TSS, offsetof(struct pcpu, pc_tss));
ASSYM(PC_PM_SAVE_CNT, offsetof(struct pcpu, pc_pm_save_cnt));
ASSYM(PC_KCR3, offsetof(struct pcpu, pc_kcr3));
ASSYM(PC_UCR3, offsetof(struct pcpu, pc_ucr3));
ASSYM(PC_SAVED_UCR3, offsetof(struct pcpu, pc_saved_ucr3));
ASSYM(PC_PTI_STACK, offsetof(struct pcpu, pc_pti_stack));
ASSYM(PC_PTI_STACK_SZ, PC_PTI_STACK_SZ);
ASSYM(PC_PTI_RSP0, offsetof(struct pcpu, pc_pti_rsp0));
ASSYM(PC_IBPB_SET, offsetof(struct pcpu, pc_ibpb_set));
 
ASSYM(LA_EOI, LAPIC_EOI * LAPIC_MEM_MUL);
ASSYM(LA_ISR, LAPIC_ISR0 * LAPIC_MEM_MUL);

ASSYM(KCSEL, GSEL(GCODE_SEL, SEL_KPL));
ASSYM(KDSEL, GSEL(GDATA_SEL, SEL_KPL));
ASSYM(KUCSEL, GSEL(GUCODE_SEL, SEL_UPL));
ASSYM(KUDSEL, GSEL(GUDATA_SEL, SEL_UPL));
ASSYM(KUC32SEL, GSEL(GUCODE32_SEL, SEL_UPL));
ASSYM(KUF32SEL, GSEL(GUFS32_SEL, SEL_UPL));
ASSYM(KUG32SEL, GSEL(GUGS32_SEL, SEL_UPL));
ASSYM(TSSSEL, GSEL(GPROC0_SEL, SEL_KPL));
ASSYM(LDTSEL, GSEL(GUSERLDT_SEL, SEL_KPL));
ASSYM(SEL_RPL_MASK, SEL_RPL_MASK);

ASSYM(__FreeBSD_version, __FreeBSD_version);

#ifdef	HWPMC_HOOKS
ASSYM(PMC_FN_USER_CALLCHAIN, PMC_FN_USER_CALLCHAIN);
#endif

ASSYM(EC_EFI_STATUS, offsetof(struct efirt_callinfo, ec_efi_status));
ASSYM(EC_FPTR, offsetof(struct efirt_callinfo, ec_fptr));
ASSYM(EC_ARGCNT, offsetof(struct efirt_callinfo, ec_argcnt));
ASSYM(EC_ARG1, offsetof(struct efirt_callinfo, ec_arg1));
ASSYM(EC_ARG2, offsetof(struct efirt_callinfo, ec_arg2));
ASSYM(EC_ARG3, offsetof(struct efirt_callinfo, ec_arg3));
ASSYM(EC_ARG4, offsetof(struct efirt_callinfo, ec_arg4));
ASSYM(EC_ARG5, offsetof(struct efirt_callinfo, ec_arg5));
ASSYM(EC_RBX, offsetof(struct efirt_callinfo, ec_rbx));
ASSYM(EC_RSP, offsetof(struct efirt_callinfo, ec_rsp));
ASSYM(EC_RBP, offsetof(struct efirt_callinfo, ec_rbp));
ASSYM(EC_R12, offsetof(struct efirt_callinfo, ec_r12));
ASSYM(EC_R13, offsetof(struct efirt_callinfo, ec_r13));
ASSYM(EC_R14, offsetof(struct efirt_callinfo, ec_r14));
ASSYM(EC_R15, offsetof(struct efirt_callinfo, ec_r15));
