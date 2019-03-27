/*
 * Copyright (c) 2004 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _KGDB_H_
#define	_KGDB_H_

struct thread_info;

extern kvm_t *kvm;

struct kthr {
	struct kthr	*next;
	uintptr_t	paddr;
	uintptr_t	kaddr;
	uintptr_t	kstack;
	uintptr_t	pcb;
	int		tid;
	int		pid;
	int		cpu;
};

extern struct kthr *curkthr;

void initialize_kld_target(void);
void initialize_kgdb_target(void);
void kgdb_dmesg(void);
CORE_ADDR kgdb_trgt_core_pcb(u_int);
CORE_ADDR kgdb_trgt_stop_pcb(u_int, u_int);
void kgdb_trgt_new_objfile(struct objfile *);
void kgdb_trgt_fetch_registers(int);
void kgdb_trgt_store_registers(int);
void kld_init(void);
void kld_new_objfile(struct objfile *);

frame_unwind_sniffer_ftype kgdb_trgt_trapframe_sniffer;

struct kthr *kgdb_thr_first(void);
struct kthr *kgdb_thr_init(void);
struct kthr *kgdb_thr_lookup_tid(int);
struct kthr *kgdb_thr_lookup_pid(int);
struct kthr *kgdb_thr_lookup_paddr(uintptr_t);
struct kthr *kgdb_thr_lookup_taddr(uintptr_t);
struct kthr *kgdb_thr_next(struct kthr *);
struct kthr *kgdb_thr_select(struct kthr *);
char        *kgdb_thr_extra_thread_info(int);

CORE_ADDR kgdb_lookup(const char *sym);
CORE_ADDR kgdb_parse_1(const char *, int);

#define	kgdb_parse(exp)		kgdb_parse_1((exp), 0)
#define	kgdb_parse_quiet(exp)	kgdb_parse_1((exp), 1)

extern int (*arm_tdep_pc_fixup)(CORE_ADDR *pc);
int kgdb_trgt_pc_fixup(CORE_ADDR *pc);

#endif /* _KGDB_H_ */
