/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997 Jonathan Lemon
 * All rights reserved.
 *
 * Derived from register.h, which is
 *     Copyright (c) 1996 Michael Smith.  All rights reserved.
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

#ifndef _MACHINE_VM86_H_
#define _MACHINE_VM86_H_ 1

/* standard register representation */
typedef union {
	u_int	r_ex;
	struct {
		u_short	r_x;
		u_int	:16;
	} r_w;
	struct {
		u_char	r_l;
		u_char	r_h;
		u_int	:16;
	} r_b;
} reg86_t;

/* layout must match definition of struct trapframe_vm86 in <machine/frame.h> */

struct vm86frame {
	int	kernel_fs;
	int	kernel_es;
	int	kernel_ds;
	reg86_t	edi;
	reg86_t	esi;
	reg86_t	ebp;
	reg86_t	isp;
	reg86_t	ebx;
	reg86_t	edx;
	reg86_t	ecx;
	reg86_t	eax;
	int	vmf_trapno;
	int	vmf_err;
	reg86_t	eip;
	reg86_t	cs;
	reg86_t	eflags;
	reg86_t	esp;
	reg86_t	ss;
	reg86_t	es;
	reg86_t	ds;
	reg86_t	fs;
	reg86_t	gs;
#define vmf_ah		eax.r_b.r_h
#define vmf_al		eax.r_b.r_l
#define vmf_ax		eax.r_w.r_x
#define vmf_eax		eax.r_ex
#define vmf_bh		ebx.r_b.r_h
#define vmf_bl		ebx.r_b.r_l
#define vmf_bx		ebx.r_w.r_x
#define vmf_ebx		ebx.r_ex
#define vmf_ch		ecx.r_b.r_h
#define vmf_cl		ecx.r_b.r_l
#define vmf_cx		ecx.r_w.r_x
#define vmf_ecx		ecx.r_ex
#define vmf_dh		edx.r_b.r_h
#define vmf_dl		edx.r_b.r_l
#define vmf_dx		edx.r_w.r_x
#define vmf_edx		edx.r_ex
#define vmf_si		esi.r_w.r_x
#define vmf_di		edi.r_w.r_x
#define vmf_cs		cs.r_w.r_x
#define vmf_ds		ds.r_w.r_x
#define vmf_es		es.r_w.r_x
#define vmf_ss		ss.r_w.r_x
#define vmf_bp		ebp.r_w.r_x
#define vmf_sp		esp.r_w.r_x
#define vmf_ip		eip.r_w.r_x
#define vmf_flags	eflags.r_w.r_x
#define vmf_eflags	eflags.r_ex
};

#define VM86_PMAPSIZE	24
#define VMAP_MALLOC	1	/* page was malloced by us */

struct vm86context {
	int	npages;
	struct	vm86pmap {
		int	flags;
		int	pte_num;
		vm_offset_t	kva;
		uint64_t	old_pte;
	} pmap[VM86_PMAPSIZE];
};

#define VM_USERCHANGE   (PSL_USERCHANGE)
#define VME_USERCHANGE  (VM_USERCHANGE | PSL_VIP | PSL_VIF)

struct vm86_kernel {
	caddr_t	vm86_intmap;			/* interrupt map */
	u_int	vm86_eflags;			/* emulated flags */
	int	vm86_has_vme;			/* VME support */
	int	vm86_inited;			/* we were initialized */
	int	vm86_debug;
	caddr_t	vm86_sproc;			/* address of sproc */
};

#define VM86_INIT	1
#define VM86_SET_VME	2
#define VM86_GET_VME	3
#define VM86_INTCALL	4

struct vm86_init_args {
        int     debug;                  /* debug flag */
        int     cpu_type;               /* cpu type to emulate */
        u_char  int_map[32];            /* interrupt map */ 
};

struct vm86_vme_args {
	int	state;			/* status */
};

struct vm86_intcall_args {
	int	intnum;
	struct 	vm86frame vmf;
};

#ifdef _KERNEL
extern 	int vm86paddr;

struct thread;
extern	int vm86_emulate(struct vm86frame *);
extern	int vm86_sysarch(struct thread *, char *);
extern void vm86_trap(struct vm86frame *);
extern 	int vm86_intcall(int, struct vm86frame *);
extern 	int vm86_datacall(int, struct vm86frame *, struct vm86context *);
extern void vm86_initialize(void);
extern vm_offset_t vm86_getpage(struct vm86context *, int);
extern vm_offset_t vm86_addpage(struct vm86context *, int, vm_offset_t);
extern int vm86_getptr(struct vm86context *, vm_offset_t, u_short *, u_short *);

extern vm_offset_t vm86_getaddr(struct vm86context *, u_short, u_short);
#endif /* _KERNEL */

#endif /* _MACHINE_VM86_H_ */
