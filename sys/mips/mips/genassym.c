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
 *	from: src/sys/i386/i386/genassym.c,v 1.86.2.1 2000/05/16 06:58:06 dillon
 *	JNPR: genassym.c,v 1.4 2007/08/09 11:23:32 katta
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/assym.h>
#include <machine/pte.h>
#include <sys/proc.h>
#include <sys/errno.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/socket.h>
#include <sys/resourcevar.h>
#include <sys/ucontext.h>
#include <sys/vmmeter.h>
#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <machine/cpuregs.h>
#include <machine/pcb.h>
#include <machine/sigframe.h>
#include <machine/proc.h>
#include <machine/tls.h>

#ifdef	CPU_CNMIPS
#include <machine/octeon_cop2.h>
#endif

#ifndef offsetof
#define	offsetof(t,m) (int)((&((t *)0L)->m))
#endif

ASSYM(TD_PCB, offsetof(struct thread, td_pcb));
ASSYM(TD_UPTE, offsetof(struct thread, td_md.md_upte));
ASSYM(TD_KSTACK, offsetof(struct thread, td_kstack));
ASSYM(TD_FLAGS, offsetof(struct thread, td_flags));
ASSYM(TD_LOCK, offsetof(struct thread, td_lock));
ASSYM(TD_MDFLAGS, offsetof(struct thread, td_md.md_flags));
ASSYM(TD_MDTLS, offsetof(struct thread, td_md.md_tls));
ASSYM(TD_MDTLS_TCB_OFFSET, offsetof(struct thread, td_md.md_tls_tcb_offset));

ASSYM(U_PCB_REGS, offsetof(struct pcb, pcb_regs.zero));
ASSYM(U_PCB_CONTEXT, offsetof(struct pcb, pcb_context));
ASSYM(U_PCB_ONFAULT, offsetof(struct pcb, pcb_onfault));
ASSYM(U_PCB_FPREGS, offsetof(struct pcb, pcb_regs.f0));

ASSYM(PC_CURPCB, offsetof(struct pcpu, pc_curpcb));
ASSYM(PC_SEGBASE, offsetof(struct pcpu, pc_segbase));
ASSYM(PC_CURTHREAD, offsetof(struct pcpu, pc_curthread));
ASSYM(PC_FPCURTHREAD, offsetof(struct pcpu, pc_fpcurthread));
ASSYM(PC_CPUID, offsetof(struct pcpu, pc_cpuid));

ASSYM(VM_MAX_KERNEL_ADDRESS, VM_MAX_KERNEL_ADDRESS);
ASSYM(VM_MAXUSER_ADDRESS, VM_MAXUSER_ADDRESS);
ASSYM(SIGF_UC, offsetof(struct sigframe, sf_uc));
#ifdef COMPAT_FREEBSD32
ASSYM(SIGF32_UC, offsetof(struct sigframe32, sf_uc));
#endif
ASSYM(SIGFPE, SIGFPE);
ASSYM(PAGE_SHIFT, PAGE_SHIFT);
ASSYM(PAGE_SIZE, PAGE_SIZE);
ASSYM(PDRSHIFT, PDRSHIFT);
ASSYM(SEGSHIFT, SEGSHIFT);
ASSYM(TDF_NEEDRESCHED, TDF_NEEDRESCHED);
ASSYM(TDF_ASTPENDING, TDF_ASTPENDING);
ASSYM(MAXCOMLEN, MAXCOMLEN);
ASSYM(MDTD_COP2USED, MDTD_COP2USED);

ASSYM(MIPS_KSEG0_START, MIPS_KSEG0_START);
ASSYM(MIPS_KSEG1_START, MIPS_KSEG1_START);
ASSYM(MIPS_KSEG2_START, MIPS_KSEG2_START);
ASSYM(MIPS_XKSEG_START, MIPS_XKSEG_START);

#ifdef	CPU_CNMIPS
ASSYM(TD_COP2OWNER, offsetof(struct thread, td_md.md_cop2owner));
ASSYM(TD_COP2, offsetof(struct thread, td_md.md_cop2));
ASSYM(TD_UCOP2, offsetof(struct thread, td_md.md_ucop2));
ASSYM(COP2_CRC_IV_OFFSET, offsetof(struct octeon_cop2_state, crc_iv));
ASSYM(COP2_CRC_LENGTH_OFFSET, offsetof(struct octeon_cop2_state, crc_length));
ASSYM(COP2_CRC_POLY_OFFSET, offsetof(struct octeon_cop2_state, crc_poly));
ASSYM(COP2_LLM_DAT0_OFFSET, offsetof(struct octeon_cop2_state, llm_dat));
ASSYM(COP2_LLM_DAT1_OFFSET, offsetof(struct octeon_cop2_state, llm_dat) + 8);
ASSYM(COP2_3DES_IV_OFFSET, offsetof(struct octeon_cop2_state, _3des_iv));
ASSYM(COP2_3DES_KEY0_OFFSET, offsetof(struct octeon_cop2_state, _3des_key));
ASSYM(COP2_3DES_KEY1_OFFSET, offsetof(struct octeon_cop2_state, _3des_key) + 8);
ASSYM(COP2_3DES_KEY2_OFFSET, offsetof(struct octeon_cop2_state, _3des_key) + 16);
ASSYM(COP2_3DES_RESULT_OFFSET, offsetof(struct octeon_cop2_state, _3des_result));
ASSYM(COP2_AES_INP0_OFFSET, offsetof(struct octeon_cop2_state, aes_inp0));
ASSYM(COP2_AES_IV0_OFFSET, offsetof(struct octeon_cop2_state, aes_iv));
ASSYM(COP2_AES_IV1_OFFSET, offsetof(struct octeon_cop2_state, aes_iv) + 8);
ASSYM(COP2_AES_KEY0_OFFSET, offsetof(struct octeon_cop2_state, aes_key));
ASSYM(COP2_AES_KEY1_OFFSET, offsetof(struct octeon_cop2_state, aes_key) + 8);
ASSYM(COP2_AES_KEY2_OFFSET, offsetof(struct octeon_cop2_state, aes_key) + 16);
ASSYM(COP2_AES_KEY3_OFFSET, offsetof(struct octeon_cop2_state, aes_key) + 24);
ASSYM(COP2_AES_KEYLEN_OFFSET, offsetof(struct octeon_cop2_state, aes_keylen));
ASSYM(COP2_AES_RESULT0_OFFSET, offsetof(struct octeon_cop2_state, aes_result));
ASSYM(COP2_AES_RESULT1_OFFSET, offsetof(struct octeon_cop2_state, aes_result) + 8);
ASSYM(COP2_HSH_DATW0_OFFSET, offsetof(struct octeon_cop2_state, hsh_datw));
ASSYM(COP2_HSH_DATW1_OFFSET, offsetof(struct octeon_cop2_state, hsh_datw) + 8);
ASSYM(COP2_HSH_DATW2_OFFSET, offsetof(struct octeon_cop2_state, hsh_datw) + 16);
ASSYM(COP2_HSH_DATW3_OFFSET, offsetof(struct octeon_cop2_state, hsh_datw) + 24);
ASSYM(COP2_HSH_DATW4_OFFSET, offsetof(struct octeon_cop2_state, hsh_datw) + 32);
ASSYM(COP2_HSH_DATW5_OFFSET, offsetof(struct octeon_cop2_state, hsh_datw) + 40);
ASSYM(COP2_HSH_DATW6_OFFSET, offsetof(struct octeon_cop2_state, hsh_datw) + 48);
ASSYM(COP2_HSH_DATW7_OFFSET, offsetof(struct octeon_cop2_state, hsh_datw) + 56);
ASSYM(COP2_HSH_DATW8_OFFSET, offsetof(struct octeon_cop2_state, hsh_datw) + 64);
ASSYM(COP2_HSH_DATW9_OFFSET, offsetof(struct octeon_cop2_state, hsh_datw) + 72);
ASSYM(COP2_HSH_DATW10_OFFSET, offsetof(struct octeon_cop2_state, hsh_datw) + 80);
ASSYM(COP2_HSH_DATW11_OFFSET, offsetof(struct octeon_cop2_state, hsh_datw) + 88);
ASSYM(COP2_HSH_DATW12_OFFSET, offsetof(struct octeon_cop2_state, hsh_datw) + 96);
ASSYM(COP2_HSH_DATW13_OFFSET, offsetof(struct octeon_cop2_state, hsh_datw) + 104);
ASSYM(COP2_HSH_DATW14_OFFSET, offsetof(struct octeon_cop2_state, hsh_datw) + 112);
ASSYM(COP2_HSH_IVW0_OFFSET, offsetof(struct octeon_cop2_state, hsh_ivw));
ASSYM(COP2_HSH_IVW1_OFFSET, offsetof(struct octeon_cop2_state, hsh_ivw) + 8);
ASSYM(COP2_HSH_IVW2_OFFSET, offsetof(struct octeon_cop2_state, hsh_ivw) + 16);
ASSYM(COP2_HSH_IVW3_OFFSET, offsetof(struct octeon_cop2_state, hsh_ivw) + 24);
ASSYM(COP2_HSH_IVW4_OFFSET, offsetof(struct octeon_cop2_state, hsh_ivw) + 32);
ASSYM(COP2_HSH_IVW5_OFFSET, offsetof(struct octeon_cop2_state, hsh_ivw) + 40);
ASSYM(COP2_HSH_IVW6_OFFSET, offsetof(struct octeon_cop2_state, hsh_ivw) + 48);
ASSYM(COP2_HSH_IVW7_OFFSET, offsetof(struct octeon_cop2_state, hsh_ivw) + 56);
ASSYM(COP2_GFM_MULT0_OFFSET, offsetof(struct octeon_cop2_state, gfm_mult));
ASSYM(COP2_GFM_MULT1_OFFSET, offsetof(struct octeon_cop2_state, gfm_mult) + 8);
ASSYM(COP2_GFM_POLY_OFFSET, offsetof(struct octeon_cop2_state, gfm_poly));
ASSYM(COP2_GFM_RESULT0_OFFSET, offsetof(struct octeon_cop2_state, gfm_result));
ASSYM(COP2_GFM_RESULT1_OFFSET, offsetof(struct octeon_cop2_state, gfm_result) + 8);
ASSYM(COP2_HSH_DATW0_PASS1_OFFSET, offsetof(struct octeon_cop2_state, hsh_datw));
ASSYM(COP2_HSH_DATW1_PASS1_OFFSET, offsetof(struct octeon_cop2_state, hsh_datw) + 8);
ASSYM(COP2_HSH_DATW2_PASS1_OFFSET, offsetof(struct octeon_cop2_state, hsh_datw) + 16);
ASSYM(COP2_HSH_DATW3_PASS1_OFFSET, offsetof(struct octeon_cop2_state, hsh_datw) + 24);
ASSYM(COP2_HSH_DATW4_PASS1_OFFSET, offsetof(struct octeon_cop2_state, hsh_datw) + 32);
ASSYM(COP2_HSH_DATW5_PASS1_OFFSET, offsetof(struct octeon_cop2_state, hsh_datw) + 40);
ASSYM(COP2_HSH_DATW6_PASS1_OFFSET, offsetof(struct octeon_cop2_state, hsh_datw) + 48);
ASSYM(COP2_HSH_IVW0_PASS1_OFFSET, offsetof(struct octeon_cop2_state, hsh_ivw));
ASSYM(COP2_HSH_IVW1_PASS1_OFFSET, offsetof(struct octeon_cop2_state, hsh_ivw) + 8);
ASSYM(COP2_HSH_IVW2_PASS1_OFFSET, offsetof(struct octeon_cop2_state, hsh_ivw) + 16);
#endif
