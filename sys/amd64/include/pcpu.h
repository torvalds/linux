/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) Peter Wemm <peter@netplex.com.au>
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

#ifndef _MACHINE_PCPU_H_
#define	_MACHINE_PCPU_H_

#ifndef _SYS_CDEFS_H_
#error "sys/cdefs.h is a prerequisite for this file"
#endif

#define	PC_PTI_STACK_SZ	16
/*
 * The SMP parts are setup in pmap.c and locore.s for the BSP, and
 * mp_machdep.c sets up the data for the AP's to "see" when they awake.
 * The reason for doing it via a struct is so that an array of pointers
 * to each CPU's data can be set up for things like "check curproc on all
 * other processors"
 */
#define	PCPU_MD_FIELDS							\
	char	pc_monitorbuf[128] __aligned(128); /* cache line */	\
	struct	pcpu *pc_prvspace;	/* Self-reference */		\
	struct	pmap *pc_curpmap;					\
	struct	amd64tss *pc_tssp;	/* TSS segment active on CPU */	\
	struct	amd64tss *pc_commontssp;/* Common TSS for the CPU */	\
	uint64_t pc_kcr3;						\
	uint64_t pc_ucr3;						\
	uint64_t pc_saved_ucr3;						\
	register_t pc_rsp0;						\
	register_t pc_scratch_rsp;	/* User %rsp in syscall */	\
	register_t pc_scratch_rax;					\
	u_int	pc_apic_id;						\
	u_int   pc_acpi_id;		/* ACPI CPU id */		\
	/* Pointer to the CPU %fs descriptor */				\
	struct user_segment_descriptor	*pc_fs32p;			\
	/* Pointer to the CPU %gs descriptor */				\
	struct user_segment_descriptor	*pc_gs32p;			\
	/* Pointer to the CPU LDT descriptor */				\
	struct system_segment_descriptor *pc_ldt;			\
	/* Pointer to the CPU TSS descriptor */				\
	struct system_segment_descriptor *pc_tss;			\
	uint64_t	pc_pm_save_cnt;					\
	u_int	pc_cmci_mask;		/* MCx banks for CMCI */	\
	uint64_t pc_dbreg[16];		/* ddb debugging regs */	\
	uint64_t pc_pti_stack[PC_PTI_STACK_SZ];				\
	register_t pc_pti_rsp0;						\
	int pc_dbreg_cmd;		/* ddb debugging reg cmd */	\
	u_int	pc_vcpu_id;		/* Xen vCPU ID */		\
	uint32_t pc_pcid_next;						\
	uint32_t pc_pcid_gen;						\
	uint32_t pc_smp_tlb_done;	/* TLB op acknowledgement */	\
	uint32_t pc_ibpb_set;						\
	char	__pad[3288]		/* pad to UMA_PCPU_ALLOC_SIZE */

#define	PC_DBREG_CMD_NONE	0
#define	PC_DBREG_CMD_LOAD	1

#ifdef _KERNEL

#if defined(__GNUCLIKE_ASM) && defined(__GNUCLIKE___TYPEOF)

/*
 * Evaluates to the byte offset of the per-cpu variable name.
 */
#define	__pcpu_offset(name)						\
	__offsetof(struct pcpu, name)

/*
 * Evaluates to the type of the per-cpu variable name.
 */
#define	__pcpu_type(name)						\
	__typeof(((struct pcpu *)0)->name)

/*
 * Evaluates to the address of the per-cpu variable name.
 */
#define	__PCPU_PTR(name) __extension__ ({				\
	__pcpu_type(name) *__p;						\
									\
	__asm __volatile("movq %%gs:%1,%0; addq %2,%0"			\
	    : "=r" (__p)						\
	    : "m" (*(struct pcpu *)(__pcpu_offset(pc_prvspace))),	\
	      "i" (__pcpu_offset(name)));				\
									\
	__p;								\
})

/*
 * Evaluates to the value of the per-cpu variable name.
 */
#define	__PCPU_GET(name) __extension__ ({				\
	__pcpu_type(name) __res;					\
	struct __s {							\
		u_char	__b[MIN(sizeof(__pcpu_type(name)), 8)];		\
	} __s;								\
									\
	if (sizeof(__res) == 1 || sizeof(__res) == 2 ||			\
	    sizeof(__res) == 4 || sizeof(__res) == 8) {			\
		__asm __volatile("mov %%gs:%1,%0"			\
		    : "=r" (__s)					\
		    : "m" (*(struct __s *)(__pcpu_offset(name))));	\
		*(struct __s *)(void *)&__res = __s;			\
	} else {							\
		__res = *__PCPU_PTR(name);				\
	}								\
	__res;								\
})

/*
 * Adds the value to the per-cpu counter name.  The implementation
 * must be atomic with respect to interrupts.
 */
#define	__PCPU_ADD(name, val) do {					\
	__pcpu_type(name) __val;					\
	struct __s {							\
		u_char	__b[MIN(sizeof(__pcpu_type(name)), 8)];		\
	} __s;								\
									\
	__val = (val);							\
	if (sizeof(__val) == 1 || sizeof(__val) == 2 ||			\
	    sizeof(__val) == 4 || sizeof(__val) == 8) {			\
		__s = *(struct __s *)(void *)&__val;			\
		__asm __volatile("add %1,%%gs:%0"			\
		    : "=m" (*(struct __s *)(__pcpu_offset(name)))	\
		    : "r" (__s));					\
	} else								\
		*__PCPU_PTR(name) += __val;				\
} while (0)

/*
 * Increments the value of the per-cpu counter name.  The implementation
 * must be atomic with respect to interrupts.
 */
#define	__PCPU_INC(name) do {						\
	CTASSERT(sizeof(__pcpu_type(name)) == 1 ||			\
	    sizeof(__pcpu_type(name)) == 2 ||				\
	    sizeof(__pcpu_type(name)) == 4 ||				\
	    sizeof(__pcpu_type(name)) == 8);				\
	if (sizeof(__pcpu_type(name)) == 1) {				\
		__asm __volatile("incb %%gs:%0"				\
		    : "=m" (*(__pcpu_type(name) *)(__pcpu_offset(name)))\
		    : "m" (*(__pcpu_type(name) *)(__pcpu_offset(name))));\
	} else if (sizeof(__pcpu_type(name)) == 2) {			\
		__asm __volatile("incw %%gs:%0"				\
		    : "=m" (*(__pcpu_type(name) *)(__pcpu_offset(name)))\
		    : "m" (*(__pcpu_type(name) *)(__pcpu_offset(name))));\
	} else if (sizeof(__pcpu_type(name)) == 4) {			\
		__asm __volatile("incl %%gs:%0"				\
		    : "=m" (*(__pcpu_type(name) *)(__pcpu_offset(name)))\
		    : "m" (*(__pcpu_type(name) *)(__pcpu_offset(name))));\
	} else if (sizeof(__pcpu_type(name)) == 8) {			\
		__asm __volatile("incq %%gs:%0"				\
		    : "=m" (*(__pcpu_type(name) *)(__pcpu_offset(name)))\
		    : "m" (*(__pcpu_type(name) *)(__pcpu_offset(name))));\
	}								\
} while (0)

/*
 * Sets the value of the per-cpu variable name to value val.
 */
#define	__PCPU_SET(name, val) {						\
	__pcpu_type(name) __val;					\
	struct __s {							\
		u_char	__b[MIN(sizeof(__pcpu_type(name)), 8)];		\
	} __s;								\
									\
	__val = (val);							\
	if (sizeof(__val) == 1 || sizeof(__val) == 2 ||			\
	    sizeof(__val) == 4 || sizeof(__val) == 8) {			\
		__s = *(struct __s *)(void *)&__val;			\
		__asm __volatile("mov %1,%%gs:%0"			\
		    : "=m" (*(struct __s *)(__pcpu_offset(name)))	\
		    : "r" (__s));					\
	} else {							\
		*__PCPU_PTR(name) = __val;				\
	}								\
}

#define	get_pcpu() __extension__ ({					\
	struct pcpu *__pc;						\
									\
	__asm __volatile("movq %%gs:%1,%0"				\
	    : "=r" (__pc)						\
	    : "m" (*(struct pcpu *)(__pcpu_offset(pc_prvspace))));	\
	__pc;								\
})

#define	PCPU_GET(member)	__PCPU_GET(pc_ ## member)
#define	PCPU_ADD(member, val)	__PCPU_ADD(pc_ ## member, val)
#define	PCPU_INC(member)	__PCPU_INC(pc_ ## member)
#define	PCPU_PTR(member)	__PCPU_PTR(pc_ ## member)
#define	PCPU_SET(member, val)	__PCPU_SET(pc_ ## member, val)

#define	OFFSETOF_CURTHREAD	0
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnull-dereference"
#endif
static __inline __pure2 struct thread *
__curthread(void)
{
	struct thread *td;

	__asm("movq %%gs:%P1,%0" : "=r" (td) : "n" (OFFSETOF_CURTHREAD));
	return (td);
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#define	curthread		(__curthread())

#define	OFFSETOF_CURPCB		32
static __inline __pure2 struct pcb *
__curpcb(void)
{
	struct pcb *pcb;

	__asm("movq %%gs:%P1,%0" : "=r" (pcb) : "n" (OFFSETOF_CURPCB));
	return (pcb);
}
#define	curpcb		(__curpcb())

#define	IS_BSP()	(PCPU_GET(cpuid) == 0)

#else /* !__GNUCLIKE_ASM || !__GNUCLIKE___TYPEOF */

#error "this file needs to be ported to your compiler"

#endif /* __GNUCLIKE_ASM && __GNUCLIKE___TYPEOF */

#endif /* _KERNEL */

#endif /* !_MACHINE_PCPU_H_ */
