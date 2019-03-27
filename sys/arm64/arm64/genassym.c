/*-
 * Copyright (c) 2004 Olivier Houchard
 * Copyright (c) 2014 Andrew Turner
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <sys/param.h>
#include <sys/assym.h>
#include <sys/pcpu.h>
#include <sys/proc.h>

#include <machine/frame.h>
#include <machine/pcb.h>
#include <machine/vmparam.h>

ASSYM(TDF_ASTPENDING, TDF_ASTPENDING);
ASSYM(TDF_NEEDRESCHED, TDF_NEEDRESCHED);

ASSYM(PCPU_SIZE, sizeof(struct pcpu));
ASSYM(PC_CURPCB, offsetof(struct pcpu, pc_curpcb));
ASSYM(PC_CURTHREAD, offsetof(struct pcpu, pc_curthread));
ASSYM(PC_SSBD, offsetof(struct pcpu, pc_ssbd));

/* Size of pcb, rounded to keep stack alignment */
ASSYM(PCB_SIZE, roundup2(sizeof(struct pcb), STACKALIGNBYTES + 1));
ASSYM(PCB_SINGLE_STEP_SHIFT, PCB_SINGLE_STEP_SHIFT);
ASSYM(PCB_REGS, offsetof(struct pcb, pcb_x));
ASSYM(PCB_SP, offsetof(struct pcb, pcb_sp));
ASSYM(PCB_TPIDRRO, offsetof(struct pcb, pcb_tpidrro_el0));
ASSYM(PCB_ONFAULT, offsetof(struct pcb, pcb_onfault));
ASSYM(PCB_FLAGS, offsetof(struct pcb, pcb_flags));

ASSYM(P_MD, offsetof(struct proc, p_md));
ASSYM(MD_L0ADDR, offsetof(struct mdproc, md_l0addr));

ASSYM(SF_UC, offsetof(struct sigframe, sf_uc));

ASSYM(TD_PROC, offsetof(struct thread, td_proc));
ASSYM(TD_PCB, offsetof(struct thread, td_pcb));
ASSYM(TD_FLAGS, offsetof(struct thread, td_flags));
ASSYM(TD_FRAME, offsetof(struct thread, td_frame));
ASSYM(TD_LOCK, offsetof(struct thread, td_lock));

ASSYM(TF_SIZE, sizeof(struct trapframe));
ASSYM(TF_SP, offsetof(struct trapframe, tf_sp));
ASSYM(TF_ELR, offsetof(struct trapframe, tf_elr));
ASSYM(TF_SPSR, offsetof(struct trapframe, tf_spsr));
ASSYM(TF_X, offsetof(struct trapframe, tf_x));
