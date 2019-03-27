/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Jake Burkholder.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include "opt_kstack_pages.h"

#include <sys/param.h>
#include <sys/assym.h>
#include <sys/ktr.h>
#include <sys/proc.h>
#include <sys/smp.h>
#include <sys/vmmeter.h>
#include <sys/_cpuset.h>

#include <machine/atomic.h>
#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>

#include <machine/cache.h>
#include <machine/pcb.h>
#include <machine/setjmp.h>
#include <machine/smp.h>
#include <machine/tlb.h>
#include <machine/tte.h>
#include <machine/vmparam.h>

ASSYM(KERNBASE, KERNBASE);

ASSYM(KSTACK_PAGES, KSTACK_PAGES);
ASSYM(PCPU_PAGES, PCPU_PAGES);

ASSYM(TAR_VPN_SHIFT, TAR_VPN_SHIFT);

ASSYM(_NCPUBITS, _NCPUBITS);

ASSYM(TLB_DEMAP_ALL, TLB_DEMAP_ALL);
ASSYM(TLB_DEMAP_CONTEXT, TLB_DEMAP_CONTEXT);
ASSYM(TLB_DEMAP_NUCLEUS, TLB_DEMAP_NUCLEUS);
ASSYM(TLB_DEMAP_PAGE, TLB_DEMAP_PAGE);
ASSYM(TLB_DEMAP_PRIMARY, TLB_DEMAP_PRIMARY);

ASSYM(INT_SHIFT, INT_SHIFT);
ASSYM(PTR_SHIFT, PTR_SHIFT);

ASSYM(PAGE_SHIFT, PAGE_SHIFT);
ASSYM(PAGE_SHIFT_8K, PAGE_SHIFT_8K);
ASSYM(PAGE_SHIFT_4M, PAGE_SHIFT_4M);
ASSYM(PAGE_SIZE, PAGE_SIZE);
ASSYM(PAGE_SIZE_4M, PAGE_SIZE_4M);

#ifdef SMP
ASSYM(CSA_PCPU, offsetof(struct cpu_start_args, csa_pcpu));
ASSYM(CSA_STATE, offsetof(struct cpu_start_args, csa_state));
ASSYM(CSA_MID, offsetof(struct cpu_start_args, csa_mid));
ASSYM(CSA_STICK, offsetof(struct cpu_start_args, csa_stick));
ASSYM(CSA_TICK, offsetof(struct cpu_start_args, csa_tick));
ASSYM(CSA_TTES, offsetof(struct cpu_start_args, csa_ttes));
ASSYM(CSA_VER, offsetof(struct cpu_start_args, csa_ver));
#endif

ASSYM(DC_SIZE, offsetof(struct cacheinfo, dc_size));
ASSYM(DC_LINESIZE, offsetof(struct cacheinfo, dc_linesize));
ASSYM(IC_SIZE, offsetof(struct cacheinfo, ic_size));
ASSYM(IC_LINESIZE, offsetof(struct cacheinfo, ic_linesize));

ASSYM(KTR_SIZEOF, sizeof(struct ktr_entry));
ASSYM(KTR_LINE, offsetof(struct ktr_entry, ktr_line));
ASSYM(KTR_FILE, offsetof(struct ktr_entry, ktr_file));
ASSYM(KTR_DESC, offsetof(struct ktr_entry, ktr_desc));
ASSYM(KTR_CPU, offsetof(struct ktr_entry, ktr_cpu));
ASSYM(KTR_TIMESTAMP, offsetof(struct ktr_entry, ktr_timestamp));
ASSYM(KTR_PARM1, offsetof(struct ktr_entry, ktr_parms[0]));
ASSYM(KTR_PARM2, offsetof(struct ktr_entry, ktr_parms[1]));
ASSYM(KTR_PARM3, offsetof(struct ktr_entry, ktr_parms[2]));
ASSYM(KTR_PARM4, offsetof(struct ktr_entry, ktr_parms[3]));
ASSYM(KTR_PARM5, offsetof(struct ktr_entry, ktr_parms[4]));
ASSYM(KTR_PARM6, offsetof(struct ktr_entry, ktr_parms[5]));

ASSYM(TTE_SHIFT, TTE_SHIFT);
ASSYM(TTE_VPN, offsetof(struct tte, tte_vpn));
ASSYM(TTE_DATA, offsetof(struct tte, tte_data));

ASSYM(TD_V, TD_V);
ASSYM(TD_EXEC, TD_EXEC);
ASSYM(TD_REF, TD_REF);
ASSYM(TD_SW, TD_SW);
ASSYM(TD_L, TD_L);
ASSYM(TD_CP, TD_CP);
ASSYM(TD_CV, TD_CV);
ASSYM(TD_W, TD_W);

ASSYM(TS_MIN, TS_MIN);
ASSYM(TS_MAX, TS_MAX);
ASSYM(TLB_DAR_SLOT_SHIFT, TLB_DAR_SLOT_SHIFT);
ASSYM(TLB_CXR_PGSZ_MASK, TLB_CXR_PGSZ_MASK);
ASSYM(TLB_DIRECT_ADDRESS_MASK, TLB_DIRECT_ADDRESS_MASK);
ASSYM(TLB_DIRECT_TO_TTE_MASK, TLB_DIRECT_TO_TTE_MASK);
ASSYM(TV_SIZE_BITS, TV_SIZE_BITS);

ASSYM(V_INTR, offsetof(struct vmmeter, v_intr));

ASSYM(MAXCOMLEN, MAXCOMLEN);
ASSYM(PC_CURTHREAD, offsetof(struct pcpu, pc_curthread));
ASSYM(PC_CURPCB, offsetof(struct pcpu, pc_curpcb));
ASSYM(PC_CPUID, offsetof(struct pcpu, pc_cpuid));
ASSYM(PC_IRHEAD, offsetof(struct pcpu, pc_irhead));
ASSYM(PC_IRTAIL, offsetof(struct pcpu, pc_irtail));
ASSYM(PC_IRFREE, offsetof(struct pcpu, pc_irfree));
ASSYM(PC_SIZEOF, sizeof(struct pcpu));

ASSYM(PC_CACHE, offsetof(struct pcpu, pc_cache));
ASSYM(PC_MID, offsetof(struct pcpu, pc_mid));
ASSYM(PC_PMAP, offsetof(struct pcpu, pc_pmap));
ASSYM(PC_TLB_CTX, offsetof(struct pcpu, pc_tlb_ctx));
ASSYM(PC_TLB_CTX_MAX, offsetof(struct pcpu, pc_tlb_ctx_max));
ASSYM(PC_TLB_CTX_MIN, offsetof(struct pcpu, pc_tlb_ctx_min));

ASSYM(IR_NEXT, offsetof(struct intr_request, ir_next));
ASSYM(IR_FUNC, offsetof(struct intr_request, ir_func));
ASSYM(IR_ARG, offsetof(struct intr_request, ir_arg));
ASSYM(IR_PRI, offsetof(struct intr_request, ir_pri));
ASSYM(IR_VEC, offsetof(struct intr_request, ir_vec));

#ifdef SMP
ASSYM(ICA_PA, offsetof(struct ipi_cache_args, ica_pa));

ASSYM(IRA_MASK, offsetof(struct ipi_rd_args, ira_mask));
ASSYM(IRA_VAL, offsetof(struct ipi_rd_args, ira_val));

ASSYM(ITA_MASK, offsetof(struct ipi_tlb_args, ita_mask));
ASSYM(ITA_PMAP, offsetof(struct ipi_tlb_args, ita_pmap));
ASSYM(ITA_START, offsetof(struct ipi_tlb_args, ita_start));
ASSYM(ITA_END, offsetof(struct ipi_tlb_args, ita_end));
ASSYM(ITA_VA, offsetof(struct ipi_tlb_args, ita_va));
#endif

ASSYM(IV_FUNC, offsetof(struct intr_vector, iv_func));
ASSYM(IV_ARG, offsetof(struct intr_vector, iv_arg));
ASSYM(IV_PRI, offsetof(struct intr_vector, iv_pri));

ASSYM(TDF_ASTPENDING, TDF_ASTPENDING);
ASSYM(TDF_NEEDRESCHED, TDF_NEEDRESCHED);

ASSYM(MD_UTRAP, offsetof(struct mdproc, md_utrap));

ASSYM(P_COMM, offsetof(struct proc, p_comm));
ASSYM(P_MD, offsetof(struct proc, p_md));
ASSYM(P_PID, offsetof(struct proc, p_pid));
ASSYM(P_VMSPACE, offsetof(struct proc, p_vmspace));

ASSYM(TD_FLAGS, offsetof(struct thread, td_flags));
ASSYM(TD_FRAME, offsetof(struct thread, td_frame));
ASSYM(TD_KSTACK, offsetof(struct thread, td_kstack));
ASSYM(TD_LOCK, offsetof(struct thread, td_lock));
ASSYM(TD_PCB, offsetof(struct thread, td_pcb));
ASSYM(TD_PROC, offsetof(struct thread, td_proc));
ASSYM(TD_MD, offsetof(struct thread, td_md));
ASSYM(MD_SAVED_PIL, offsetof(struct mdthread, md_saved_pil));

ASSYM(PCB_SIZEOF, sizeof(struct pcb));
ASSYM(PCB_RW, offsetof(struct pcb, pcb_rw));
ASSYM(PCB_KFP, offsetof(struct pcb, pcb_kfp));
ASSYM(PCB_UFP, offsetof(struct pcb, pcb_ufp));
ASSYM(PCB_RWSP, offsetof(struct pcb, pcb_rwsp));
ASSYM(PCB_FLAGS, offsetof(struct pcb, pcb_flags));
ASSYM(PCB_NSAVED, offsetof(struct pcb, pcb_nsaved));
ASSYM(PCB_PC, offsetof(struct pcb, pcb_pc));
ASSYM(PCB_SP, offsetof(struct pcb, pcb_sp));
ASSYM(PCB_PAD, offsetof(struct pcb, pcb_pad));

ASSYM(VM_PMAP, offsetof(struct vmspace, vm_pmap));
ASSYM(PM_ACTIVE, offsetof(struct pmap, pm_active));
ASSYM(PM_CONTEXT, offsetof(struct pmap, pm_context));
ASSYM(PM_TSB, offsetof(struct pmap, pm_tsb));

ASSYM(_JB_FP, offsetof(struct _jmp_buf, _jb[_JB_FP]));
ASSYM(_JB_PC, offsetof(struct _jmp_buf, _jb[_JB_PC]));
ASSYM(_JB_SP, offsetof(struct _jmp_buf, _jb[_JB_SP]));
ASSYM(_JB_SIGFLAG, offsetof(struct _jmp_buf, _jb[_JB_SIGFLAG]));
ASSYM(_JB_SIGMASK, offsetof(struct _jmp_buf, _jb[_JB_SIGMASK]));

ASSYM(TF_G0, offsetof(struct trapframe, tf_global[0]));
ASSYM(TF_G1, offsetof(struct trapframe, tf_global[1]));
ASSYM(TF_G2, offsetof(struct trapframe, tf_global[2]));
ASSYM(TF_G3, offsetof(struct trapframe, tf_global[3]));
ASSYM(TF_G4, offsetof(struct trapframe, tf_global[4]));
ASSYM(TF_G5, offsetof(struct trapframe, tf_global[5]));
ASSYM(TF_G6, offsetof(struct trapframe, tf_global[6]));
ASSYM(TF_G7, offsetof(struct trapframe, tf_global[7]));
ASSYM(TF_O0, offsetof(struct trapframe, tf_out[0]));
ASSYM(TF_O1, offsetof(struct trapframe, tf_out[1]));
ASSYM(TF_O2, offsetof(struct trapframe, tf_out[2]));
ASSYM(TF_O3, offsetof(struct trapframe, tf_out[3]));
ASSYM(TF_O4, offsetof(struct trapframe, tf_out[4]));
ASSYM(TF_O5, offsetof(struct trapframe, tf_out[5]));
ASSYM(TF_O6, offsetof(struct trapframe, tf_out[6]));
ASSYM(TF_O7, offsetof(struct trapframe, tf_out[7]));
ASSYM(TF_FPRS, offsetof(struct trapframe, tf_fprs));
ASSYM(TF_FSR, offsetof(struct trapframe, tf_fsr));
ASSYM(TF_GSR, offsetof(struct trapframe, tf_gsr));
ASSYM(TF_PIL, offsetof(struct trapframe, tf_pil));
ASSYM(TF_LEVEL, offsetof(struct trapframe, tf_level));
ASSYM(TF_SFAR, offsetof(struct trapframe, tf_sfar));
ASSYM(TF_SFSR, offsetof(struct trapframe, tf_sfsr));
ASSYM(TF_TAR, offsetof(struct trapframe, tf_tar));
ASSYM(TF_TYPE, offsetof(struct trapframe, tf_type));
ASSYM(TF_Y, offsetof(struct trapframe, tf_y));
ASSYM(TF_TNPC, offsetof(struct trapframe, tf_tnpc));
ASSYM(TF_TPC, offsetof(struct trapframe, tf_tpc));
ASSYM(TF_TSTATE, offsetof(struct trapframe, tf_tstate));
ASSYM(TF_WSTATE, offsetof(struct trapframe, tf_wstate));
ASSYM(TF_SIZEOF, sizeof(struct trapframe));

ASSYM(VM_MIN_DIRECT_ADDRESS, VM_MIN_DIRECT_ADDRESS);
ASSYM(VM_MIN_PROM_ADDRESS, VM_MIN_PROM_ADDRESS);
ASSYM(VM_MAX_PROM_ADDRESS, VM_MAX_PROM_ADDRESS);
