/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2010 Juli Mallett <jmallett@FreeBSD.org>
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
 * $FreeBSD$
 */

#ifndef	_MACHINE_TLB_H_
#define	_MACHINE_TLB_H_

/*
 * The first TLB entry that write random hits.
 * TLB entry 0 maps the kernel stack of the currently running thread
 * TLB entry 1 maps the pcpu area of processor (only for SMP builds)
 */
#define	KSTACK_TLB_ENTRY	0
#ifdef SMP
#define	PCPU_TLB_ENTRY		1
#define	VMWIRED_ENTRIES		2
#else
#define	VMWIRED_ENTRIES		1
#endif	/* SMP */

/*
 * The number of process id entries.
 */
#define	VMNUM_PIDS		256

extern int num_tlbentries;

void tlb_insert_wired(unsigned, vm_offset_t, pt_entry_t, pt_entry_t);
void tlb_invalidate_address(struct pmap *, vm_offset_t);
void tlb_invalidate_all(void);
void tlb_invalidate_all_user(struct pmap *);
void tlb_invalidate_range(struct pmap *, vm_offset_t, vm_offset_t);
void tlb_save(void);
void tlb_update(struct pmap *, vm_offset_t, pt_entry_t);

#endif /* !_MACHINE_TLB_H_ */
