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
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/assym.h>
#include <sys/errno.h>
#include <sys/ktr.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/signal.h>
#include <sys/smp.h>
#include <sys/systm.h>
#include <sys/ucontext.h>
#include <sys/ucontext.h>
#include <sys/vmmeter.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>

#include <machine/pcb.h>
#include <machine/psl.h>
#include <machine/sigframe.h>

ASSYM(PC_CURTHREAD, offsetof(struct pcpu, pc_curthread));
ASSYM(PC_CURPCB, offsetof(struct pcpu, pc_curpcb));
ASSYM(PC_CURPMAP, offsetof(struct pcpu, pc_curpmap));
ASSYM(PC_TEMPSAVE, offsetof(struct pcpu, pc_tempsave));
ASSYM(PC_DISISAVE, offsetof(struct pcpu, pc_disisave));
ASSYM(PC_DBSAVE, offsetof(struct pcpu, pc_dbsave));
ASSYM(PC_RESTORE, offsetof(struct pcpu, pc_restore));

#if defined(BOOKE)
ASSYM(PC_BOOKE_CRITSAVE, offsetof(struct pcpu, pc_booke.critsave));
ASSYM(PC_BOOKE_MCHKSAVE, offsetof(struct pcpu, pc_booke.mchksave));
ASSYM(PC_BOOKE_TLBSAVE, offsetof(struct pcpu, pc_booke.tlbsave));
ASSYM(PC_BOOKE_TLB_LEVEL, offsetof(struct pcpu, pc_booke.tlb_level));
ASSYM(PC_BOOKE_TLB_LOCK, offsetof(struct pcpu, pc_booke.tlb_lock));
#endif

ASSYM(CPUSAVE_R27, CPUSAVE_R27*sizeof(register_t));
ASSYM(CPUSAVE_R28, CPUSAVE_R28*sizeof(register_t));
ASSYM(CPUSAVE_R29, CPUSAVE_R29*sizeof(register_t));
ASSYM(CPUSAVE_R30, CPUSAVE_R30*sizeof(register_t));
ASSYM(CPUSAVE_R31, CPUSAVE_R31*sizeof(register_t));
ASSYM(CPUSAVE_SRR0, CPUSAVE_SRR0*sizeof(register_t));
ASSYM(CPUSAVE_SRR1, CPUSAVE_SRR1*sizeof(register_t));
ASSYM(CPUSAVE_AIM_DAR, CPUSAVE_AIM_DAR*sizeof(register_t));
ASSYM(CPUSAVE_AIM_DSISR, CPUSAVE_AIM_DSISR*sizeof(register_t));
ASSYM(CPUSAVE_BOOKE_DEAR, CPUSAVE_BOOKE_DEAR*sizeof(register_t));
ASSYM(CPUSAVE_BOOKE_ESR, CPUSAVE_BOOKE_ESR*sizeof(register_t));
ASSYM(BOOKE_CRITSAVE_SRR0, BOOKE_CRITSAVE_SRR0*sizeof(register_t));
ASSYM(BOOKE_CRITSAVE_SRR1, BOOKE_CRITSAVE_SRR1*sizeof(register_t));

ASSYM(TLBSAVE_BOOKE_LR, TLBSAVE_BOOKE_LR*sizeof(register_t));
ASSYM(TLBSAVE_BOOKE_CR, TLBSAVE_BOOKE_CR*sizeof(register_t));
ASSYM(TLBSAVE_BOOKE_SRR0, TLBSAVE_BOOKE_SRR0*sizeof(register_t));
ASSYM(TLBSAVE_BOOKE_SRR1, TLBSAVE_BOOKE_SRR1*sizeof(register_t));
ASSYM(TLBSAVE_BOOKE_R20, TLBSAVE_BOOKE_R20*sizeof(register_t));
ASSYM(TLBSAVE_BOOKE_R21, TLBSAVE_BOOKE_R21*sizeof(register_t));
ASSYM(TLBSAVE_BOOKE_R22, TLBSAVE_BOOKE_R22*sizeof(register_t));
ASSYM(TLBSAVE_BOOKE_R23, TLBSAVE_BOOKE_R23*sizeof(register_t));
ASSYM(TLBSAVE_BOOKE_R24, TLBSAVE_BOOKE_R24*sizeof(register_t));
ASSYM(TLBSAVE_BOOKE_R25, TLBSAVE_BOOKE_R25*sizeof(register_t));
ASSYM(TLBSAVE_BOOKE_R26, TLBSAVE_BOOKE_R26*sizeof(register_t));
ASSYM(TLBSAVE_BOOKE_R27, TLBSAVE_BOOKE_R27*sizeof(register_t));
ASSYM(TLBSAVE_BOOKE_R28, TLBSAVE_BOOKE_R28*sizeof(register_t));
ASSYM(TLBSAVE_BOOKE_R29, TLBSAVE_BOOKE_R29*sizeof(register_t));
ASSYM(TLBSAVE_BOOKE_R30, TLBSAVE_BOOKE_R30*sizeof(register_t));
ASSYM(TLBSAVE_BOOKE_R31, TLBSAVE_BOOKE_R31*sizeof(register_t));

ASSYM(MTX_LOCK, offsetof(struct mtx, mtx_lock));

#if defined(AIM)
ASSYM(USER_ADDR, USER_ADDR);
#ifdef __powerpc64__
ASSYM(PC_KERNSLB, offsetof(struct pcpu, pc_aim.slb));
ASSYM(PC_USERSLB, offsetof(struct pcpu, pc_aim.userslb));
ASSYM(PC_SLBSAVE, offsetof(struct pcpu, pc_aim.slbsave));
ASSYM(PC_SLBSTACK, offsetof(struct pcpu, pc_aim.slbstack));
ASSYM(USER_SLB_SLOT, USER_SLB_SLOT);
ASSYM(USER_SLB_SLBE, USER_SLB_SLBE);
ASSYM(SEGMENT_MASK, SEGMENT_MASK);
#else
ASSYM(PM_SR, offsetof(struct pmap, pm_sr));
ASSYM(USER_SR, USER_SR);
#endif
#elif defined(BOOKE)
#ifdef __powerpc64__
ASSYM(PM_PP2D, offsetof(struct pmap, pm_pp2d));
#else
ASSYM(PM_PDIR, offsetof(struct pmap, pm_pdir));
#endif
/*
 * With pte_t being a bitfield struct, these fields cannot be addressed via
 * offsetof().
 */
ASSYM(PTE_RPN, 0);
ASSYM(PTE_FLAGS, sizeof(uint32_t));
#if defined(BOOKE_E500)
ASSYM(TLB_ENTRY_SIZE, sizeof(struct tlb_entry));
#endif
#endif

#ifdef __powerpc64__
ASSYM(FSP, 48);
#else
ASSYM(FSP, 8);
#endif
ASSYM(FRAMELEN, FRAMELEN);
ASSYM(FRAME_0, offsetof(struct trapframe, fixreg[0]));
ASSYM(FRAME_1, offsetof(struct trapframe, fixreg[1]));
ASSYM(FRAME_2, offsetof(struct trapframe, fixreg[2]));
ASSYM(FRAME_3, offsetof(struct trapframe, fixreg[3]));
ASSYM(FRAME_4, offsetof(struct trapframe, fixreg[4]));
ASSYM(FRAME_5, offsetof(struct trapframe, fixreg[5]));
ASSYM(FRAME_6, offsetof(struct trapframe, fixreg[6]));
ASSYM(FRAME_7, offsetof(struct trapframe, fixreg[7]));
ASSYM(FRAME_8, offsetof(struct trapframe, fixreg[8]));
ASSYM(FRAME_9, offsetof(struct trapframe, fixreg[9]));
ASSYM(FRAME_10, offsetof(struct trapframe, fixreg[10]));
ASSYM(FRAME_11, offsetof(struct trapframe, fixreg[11]));
ASSYM(FRAME_12, offsetof(struct trapframe, fixreg[12]));
ASSYM(FRAME_13, offsetof(struct trapframe, fixreg[13]));
ASSYM(FRAME_14, offsetof(struct trapframe, fixreg[14]));
ASSYM(FRAME_15, offsetof(struct trapframe, fixreg[15]));
ASSYM(FRAME_16, offsetof(struct trapframe, fixreg[16]));
ASSYM(FRAME_17, offsetof(struct trapframe, fixreg[17]));
ASSYM(FRAME_18, offsetof(struct trapframe, fixreg[18]));
ASSYM(FRAME_19, offsetof(struct trapframe, fixreg[19]));
ASSYM(FRAME_20, offsetof(struct trapframe, fixreg[20]));
ASSYM(FRAME_21, offsetof(struct trapframe, fixreg[21]));
ASSYM(FRAME_22, offsetof(struct trapframe, fixreg[22]));
ASSYM(FRAME_23, offsetof(struct trapframe, fixreg[23]));
ASSYM(FRAME_24, offsetof(struct trapframe, fixreg[24]));
ASSYM(FRAME_25, offsetof(struct trapframe, fixreg[25]));
ASSYM(FRAME_26, offsetof(struct trapframe, fixreg[26]));
ASSYM(FRAME_27, offsetof(struct trapframe, fixreg[27]));
ASSYM(FRAME_28, offsetof(struct trapframe, fixreg[28]));
ASSYM(FRAME_29, offsetof(struct trapframe, fixreg[29]));
ASSYM(FRAME_30, offsetof(struct trapframe, fixreg[30]));
ASSYM(FRAME_31, offsetof(struct trapframe, fixreg[31]));
ASSYM(FRAME_LR, offsetof(struct trapframe, lr));
ASSYM(FRAME_CR, offsetof(struct trapframe, cr));
ASSYM(FRAME_CTR, offsetof(struct trapframe, ctr));
ASSYM(FRAME_XER, offsetof(struct trapframe, xer));
ASSYM(FRAME_SRR0, offsetof(struct trapframe, srr0));
ASSYM(FRAME_SRR1, offsetof(struct trapframe, srr1));
ASSYM(FRAME_EXC, offsetof(struct trapframe, exc));
ASSYM(FRAME_AIM_DAR, offsetof(struct trapframe, dar));
ASSYM(FRAME_AIM_DSISR, offsetof(struct trapframe, cpu.aim.dsisr));
ASSYM(FRAME_BOOKE_DEAR, offsetof(struct trapframe, dar));
ASSYM(FRAME_BOOKE_ESR, offsetof(struct trapframe, cpu.booke.esr));
ASSYM(FRAME_BOOKE_DBCR0, offsetof(struct trapframe, cpu.booke.dbcr0));

ASSYM(CF_FUNC, offsetof(struct callframe, cf_func));
ASSYM(CF_ARG0, offsetof(struct callframe, cf_arg0));
ASSYM(CF_ARG1, offsetof(struct callframe, cf_arg1));
ASSYM(CF_SIZE, sizeof(struct callframe));

ASSYM(PCB_CONTEXT, offsetof(struct pcb, pcb_context));
ASSYM(PCB_CR, offsetof(struct pcb, pcb_cr));
ASSYM(PCB_DSCR, offsetof(struct pcb, pcb_dscr));
ASSYM(PCB_SP, offsetof(struct pcb, pcb_sp));
ASSYM(PCB_TOC, offsetof(struct pcb, pcb_toc));
ASSYM(PCB_LR, offsetof(struct pcb, pcb_lr));
ASSYM(PCB_ONFAULT, offsetof(struct pcb, pcb_onfault));
ASSYM(PCB_FLAGS, offsetof(struct pcb, pcb_flags));
ASSYM(PCB_FPU, PCB_FPU);
ASSYM(PCB_VEC, PCB_VEC);
ASSYM(PCB_CDSCR, PCB_CDSCR);

ASSYM(PCB_AIM_USR_VSID, offsetof(struct pcb, pcb_cpu.aim.usr_vsid));
ASSYM(PCB_BOOKE_DBCR0, offsetof(struct pcb, pcb_cpu.booke.dbcr0));

ASSYM(PCB_VSCR, offsetof(struct pcb, pcb_vec.vscr));

ASSYM(TD_LOCK, offsetof(struct thread, td_lock));
ASSYM(TD_PROC, offsetof(struct thread, td_proc));
ASSYM(TD_PCB, offsetof(struct thread, td_pcb));

ASSYM(P_VMSPACE, offsetof(struct proc, p_vmspace));

ASSYM(VM_PMAP, offsetof(struct vmspace, vm_pmap));

ASSYM(TD_FLAGS, offsetof(struct thread, td_flags));

ASSYM(TDF_ASTPENDING, TDF_ASTPENDING);
ASSYM(TDF_NEEDRESCHED, TDF_NEEDRESCHED);

ASSYM(SF_UC, offsetof(struct sigframe, sf_uc));

ASSYM(DMAP_BASE_ADDRESS, DMAP_BASE_ADDRESS);
ASSYM(MAXCOMLEN, MAXCOMLEN);

#ifdef __powerpc64__
ASSYM(PSL_CM, PSL_CM);
#endif
ASSYM(PSL_GS, PSL_GS);
ASSYM(PSL_DE, PSL_DE);
ASSYM(PSL_DS, PSL_DS);
ASSYM(PSL_IS, PSL_IS);
ASSYM(PSL_CE, PSL_CE);
ASSYM(PSL_UCLE, PSL_UCLE);
ASSYM(PSL_WE, PSL_WE);
ASSYM(PSL_UBLE, PSL_UBLE);

#if defined(AIM) && defined(__powerpc64__)
ASSYM(PSL_SF, PSL_SF);
ASSYM(PSL_HV, PSL_HV);
#endif

ASSYM(PSL_POW, PSL_POW);
ASSYM(PSL_ILE, PSL_ILE);
ASSYM(PSL_LE, PSL_LE);
ASSYM(PSL_SE, PSL_SE);
ASSYM(PSL_RI, PSL_RI);
ASSYM(PSL_DR, PSL_DR);
ASSYM(PSL_IP, PSL_IP);
ASSYM(PSL_IR, PSL_IR);

ASSYM(PSL_FE_DIS, PSL_FE_DIS);
ASSYM(PSL_FE_NONREC, PSL_FE_NONREC);
ASSYM(PSL_FE_PREC, PSL_FE_PREC);
ASSYM(PSL_FE_REC, PSL_FE_REC);

ASSYM(PSL_VEC, PSL_VEC);
ASSYM(PSL_BE, PSL_BE);
ASSYM(PSL_EE, PSL_EE);
ASSYM(PSL_FE0, PSL_FE0);
ASSYM(PSL_FE1, PSL_FE1);
ASSYM(PSL_FP, PSL_FP);
ASSYM(PSL_ME, PSL_ME);
ASSYM(PSL_PR, PSL_PR);
ASSYM(PSL_PMM, PSL_PMM);

