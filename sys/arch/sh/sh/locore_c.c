/*	$OpenBSD: locore_c.c,v 1.15 2025/05/21 09:06:58 mpi Exp $	*/
/*	$NetBSD: locore_c.c,v 1.13 2006/03/04 01:13:35 uwe Exp $	*/

/*-
 * Copyright (c) 1996, 1997, 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*-
 * Copyright (c) 1982, 1987, 1990 The Regents of the University of California.
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
 *	@(#)Locore.c
 */

/*-
 * Copyright (c) 1993, 1994, 1995, 1996, 1997
 *	 Charles M. Hannum.  All rights reserved.
 * Copyright (c) 1992 Terrence R. Lambert.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *	@(#)Locore.c
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/sched.h>

#include <uvm/uvm_extern.h>

#include <sh/locore.h>
#include <sh/cpu.h>
#include <sh/pcb.h>
#include <sh/pmap.h>
#include <sh/mmu_sh3.h>
#include <sh/mmu_sh4.h>
#include <sh/ubcreg.h>

void (*__sh_switch_resume)(struct proc *);
void cpu_switch_prepare(struct proc *, struct proc *);

/*
 * Prepare context switch from oproc to nproc.
 * This code is used by cpu_switchto.
 */
void
cpu_switch_prepare(struct proc *oproc, struct proc *nproc)
{
	nproc->p_stat = SONPROC;

	if (oproc && (oproc->p_md.md_flags & MDP_STEP))
		_reg_write_2(SH_(BBRB), 0);

	curpcb = nproc->p_md.md_pcb;
	pmap_activate(nproc);

	if (nproc->p_md.md_flags & MDP_STEP) {
		int pm_asid = nproc->p_vmspace->vm_map.pmap->pm_asid;

		_reg_write_2(SH_(BBRB), 0);
		_reg_write_4(SH_(BARB), nproc->p_md.md_regs->tf_spc);
		_reg_write_1(SH_(BASRB), pm_asid);
		_reg_write_1(SH_(BAMRB), 0);
		_reg_write_2(SH_(BRCR), 0x0040);
		_reg_write_2(SH_(BBRB), 0x0014);
	}

	curproc = nproc;
}

void
cpu_exit(struct proc *p)
{
	if (p->p_md.md_flags & MDP_STEP)
		_reg_write_2(SH_(BBRB), 0);
}

#ifndef P1_STACK
#ifdef SH3
/*
 * void sh3_switch_setup(struct proc *p):
 *	prepare kernel stack PTE table. TLB miss handler check these.
 */
void
sh3_switch_setup(struct proc *p)
{
	pt_entry_t *pte;
	struct md_upte *md_upte = p->p_md.md_upte;
	uint32_t vpn;
	int i;

	vpn = (uint32_t)p->p_addr;
	vpn &= ~PGOFSET;
	for (i = 0; i < UPAGES; i++, vpn += PAGE_SIZE, md_upte++) {
		pte = __pmap_kpte_lookup(vpn);
		KDASSERT(pte && *pte != 0);

		md_upte->addr = vpn;
		md_upte->data = (*pte & PG_HW_BITS) | PG_D | PG_V;
	}
}
#endif /* SH3 */

#ifdef SH4
/*
 * void sh4_switch_setup(struct proc *p):
 *	prepare kernel stack PTE table. sh4_switch_resume wired this PTE.
 */
void
sh4_switch_setup(struct proc *p)
{
	pt_entry_t *pte;
	struct md_upte *md_upte = p->p_md.md_upte;
	uint32_t vpn;
	int i, e;

	vpn = (uint32_t)p->p_addr;
	vpn &= ~PGOFSET;
	e = SH4_UTLB_ENTRY - UPAGES;
	for (i = 0; i < UPAGES; i++, e++, vpn += PAGE_SIZE) {
		pte = __pmap_kpte_lookup(vpn);
		KDASSERT(pte && *pte != 0);
		/* Address array */
		md_upte->addr = SH4_UTLB_AA | (e << SH4_UTLB_E_SHIFT);
		md_upte->data = vpn | SH4_UTLB_AA_D | SH4_UTLB_AA_V;
		md_upte++;
		/* Data array */
		md_upte->addr = SH4_UTLB_DA1 | (e << SH4_UTLB_E_SHIFT);
		md_upte->data = (*pte & PG_HW_BITS) |
		    SH4_UTLB_DA1_D | SH4_UTLB_DA1_V;
		md_upte++;
	}
}
#endif /* SH4 */
#endif /* !P1_STACK */
