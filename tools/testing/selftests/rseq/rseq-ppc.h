/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * rseq-ppc.h
 *
 * (C) Copyright 2016-2022 - Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 * (C) Copyright 2016-2018 - Boqun Feng <boqun.feng@gmail.com>
 */

/*
 * RSEQ_SIG is used with the following trap instruction:
 *
 * powerpc-be:    0f e5 00 0b           twui   r5,11
 * powerpc64-le:  0b 00 e5 0f           twui   r5,11
 * powerpc64-be:  0f e5 00 0b           twui   r5,11
 */

#define RSEQ_SIG	0x0fe5000b

#define rseq_smp_mb()		__asm__ __volatile__ ("sync"	::: "memory", "cc")
#define rseq_smp_lwsync()	__asm__ __volatile__ ("lwsync"	::: "memory", "cc")
#define rseq_smp_rmb()		rseq_smp_lwsync()
#define rseq_smp_wmb()		rseq_smp_lwsync()

#define rseq_smp_load_acquire(p)					\
__extension__ ({							\
	__typeof(*p) ____p1 = RSEQ_READ_ONCE(*p);			\
	rseq_smp_lwsync();						\
	____p1;								\
})

#define rseq_smp_acquire__after_ctrl_dep()	rseq_smp_lwsync()

#define rseq_smp_store_release(p, v)					\
do {									\
	rseq_smp_lwsync();						\
	RSEQ_WRITE_ONCE(*p, v);						\
} while (0)

/*
 * The __rseq_cs_ptr_array and __rseq_cs sections can be used by debuggers to
 * better handle single-stepping through the restartable critical sections.
 */

#ifdef __PPC64__

#define RSEQ_STORE_LONG(arg)	"std%U[" __rseq_str(arg) "]%X[" __rseq_str(arg) "] "	/* To memory ("m" constraint) */
#define RSEQ_STORE_INT(arg)	"stw%U[" __rseq_str(arg) "]%X[" __rseq_str(arg) "] "	/* To memory ("m" constraint) */
#define RSEQ_LOAD_LONG(arg)	"ld%U[" __rseq_str(arg) "]%X[" __rseq_str(arg) "] "	/* From memory ("m" constraint) */
#define RSEQ_LOAD_INT(arg)	"lwz%U[" __rseq_str(arg) "]%X[" __rseq_str(arg) "] "	/* From memory ("m" constraint) */
#define RSEQ_LOADX_LONG		"ldx "							/* From base register ("b" constraint) */
#define RSEQ_CMP_LONG		"cmpd "
#define RSEQ_CMP_LONG_INT	"cmpdi "

#define __RSEQ_ASM_DEFINE_TABLE(label, version, flags,				\
			start_ip, post_commit_offset, abort_ip)			\
		".pushsection __rseq_cs, \"aw\"\n\t"				\
		".balign 32\n\t"						\
		__rseq_str(label) ":\n\t"					\
		".long " __rseq_str(version) ", " __rseq_str(flags) "\n\t"	\
		".quad " __rseq_str(start_ip) ", " __rseq_str(post_commit_offset) ", " __rseq_str(abort_ip) "\n\t" \
		".popsection\n\t"						\
		".pushsection __rseq_cs_ptr_array, \"aw\"\n\t"			\
		".quad " __rseq_str(label) "b\n\t"				\
		".popsection\n\t"

#define RSEQ_ASM_STORE_RSEQ_CS(label, cs_label, rseq_cs)			\
		RSEQ_INJECT_ASM(1)						\
		"lis %%r17, (" __rseq_str(cs_label) ")@highest\n\t"		\
		"ori %%r17, %%r17, (" __rseq_str(cs_label) ")@higher\n\t"	\
		"rldicr %%r17, %%r17, 32, 31\n\t"				\
		"oris %%r17, %%r17, (" __rseq_str(cs_label) ")@high\n\t"	\
		"ori %%r17, %%r17, (" __rseq_str(cs_label) ")@l\n\t"		\
		"std %%r17, %[" __rseq_str(rseq_cs) "]\n\t"			\
		__rseq_str(label) ":\n\t"

/*
 * Exit points of a rseq critical section consist of all instructions outside
 * of the critical section where a critical section can either branch to or
 * reach through the normal course of its execution. The abort IP and the
 * post-commit IP are already part of the __rseq_cs section and should not be
 * explicitly defined as additional exit points. Knowing all exit points is
 * useful to assist debuggers stepping over the critical section.
 */
#define RSEQ_ASM_DEFINE_EXIT_POINT(start_ip, exit_ip)			\
		".pushsection __rseq_exit_point_array, \"aw\"\n\t"	\
		".quad " __rseq_str(start_ip) ", " __rseq_str(exit_ip) "\n\t" \
		".popsection\n\t"

#else /* #ifdef __PPC64__ */

#define RSEQ_STORE_LONG(arg)	"stw%U[" __rseq_str(arg) "]%X[" __rseq_str(arg) "] "	/* To memory ("m" constraint) */
#define RSEQ_STORE_INT(arg)	RSEQ_STORE_LONG(arg)					/* To memory ("m" constraint) */
#define RSEQ_LOAD_LONG(arg)	"lwz%U[" __rseq_str(arg) "]%X[" __rseq_str(arg) "] "	/* From memory ("m" constraint) */
#define RSEQ_LOAD_INT(arg)	RSEQ_LOAD_LONG(arg)					/* From memory ("m" constraint) */
#define RSEQ_LOADX_LONG		"lwzx "							/* From base register ("b" constraint) */
#define RSEQ_CMP_LONG		"cmpw "
#define RSEQ_CMP_LONG_INT	"cmpwi "

#define __RSEQ_ASM_DEFINE_TABLE(label, version, flags,				\
			start_ip, post_commit_offset, abort_ip)			\
		".pushsection __rseq_cs, \"aw\"\n\t"				\
		".balign 32\n\t"						\
		__rseq_str(label) ":\n\t"					\
		".long " __rseq_str(version) ", " __rseq_str(flags) "\n\t"	\
		/* 32-bit only supported on BE */				\
		".long 0x0, " __rseq_str(start_ip) ", 0x0, " __rseq_str(post_commit_offset) ", 0x0, " __rseq_str(abort_ip) "\n\t" \
		".popsection\n\t"					\
		".pushsection __rseq_cs_ptr_array, \"aw\"\n\t"		\
		".long 0x0, " __rseq_str(label) "b\n\t"			\
		".popsection\n\t"

/*
 * Exit points of a rseq critical section consist of all instructions outside
 * of the critical section where a critical section can either branch to or
 * reach through the normal course of its execution. The abort IP and the
 * post-commit IP are already part of the __rseq_cs section and should not be
 * explicitly defined as additional exit points. Knowing all exit points is
 * useful to assist debuggers stepping over the critical section.
 */
#define RSEQ_ASM_DEFINE_EXIT_POINT(start_ip, exit_ip)				\
		".pushsection __rseq_exit_point_array, \"aw\"\n\t"		\
		/* 32-bit only supported on BE */				\
		".long 0x0, " __rseq_str(start_ip) ", 0x0, " __rseq_str(exit_ip) "\n\t"	\
		".popsection\n\t"

#define RSEQ_ASM_STORE_RSEQ_CS(label, cs_label, rseq_cs)			\
		RSEQ_INJECT_ASM(1)						\
		"lis %%r17, (" __rseq_str(cs_label) ")@ha\n\t"			\
		"addi %%r17, %%r17, (" __rseq_str(cs_label) ")@l\n\t"		\
		RSEQ_STORE_INT(rseq_cs) "%%r17, %[" __rseq_str(rseq_cs) "]\n\t"	\
		__rseq_str(label) ":\n\t"

#endif /* #ifdef __PPC64__ */

#define RSEQ_ASM_DEFINE_TABLE(label, start_ip, post_commit_ip, abort_ip)	\
		__RSEQ_ASM_DEFINE_TABLE(label, 0x0, 0x0, start_ip,		\
					(post_commit_ip - start_ip), abort_ip)

#define RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, label)			\
		RSEQ_INJECT_ASM(2)						\
		RSEQ_LOAD_INT(current_cpu_id) "%%r17, %[" __rseq_str(current_cpu_id) "]\n\t" \
		"cmpw cr7, %[" __rseq_str(cpu_id) "], %%r17\n\t"		\
		"bne- cr7, " __rseq_str(label) "\n\t"

#define RSEQ_ASM_DEFINE_ABORT(label, abort_label)				\
		".pushsection __rseq_failure, \"ax\"\n\t"			\
		".long " __rseq_str(RSEQ_SIG) "\n\t"				\
		__rseq_str(label) ":\n\t"					\
		"b %l[" __rseq_str(abort_label) "]\n\t"				\
		".popsection\n\t"

/*
 * RSEQ_ASM_OPs: asm operations for rseq
 * 	RSEQ_ASM_OP_R_*: has hard-code registers in it
 * 	RSEQ_ASM_OP_* (else): doesn't have hard-code registers(unless cr7)
 */
#define RSEQ_ASM_OP_CMPEQ(var, expect, label)					\
		RSEQ_LOAD_LONG(var) "%%r17, %[" __rseq_str(var) "]\n\t"		\
		RSEQ_CMP_LONG "cr7, %%r17, %[" __rseq_str(expect) "]\n\t"		\
		"bne- cr7, " __rseq_str(label) "\n\t"

#define RSEQ_ASM_OP_CMPNE(var, expectnot, label)				\
		RSEQ_LOAD_LONG(var) "%%r17, %[" __rseq_str(var) "]\n\t"		\
		RSEQ_CMP_LONG "cr7, %%r17, %[" __rseq_str(expectnot) "]\n\t"		\
		"beq- cr7, " __rseq_str(label) "\n\t"

#define RSEQ_ASM_OP_STORE(value, var)						\
		RSEQ_STORE_LONG(var) "%[" __rseq_str(value) "], %[" __rseq_str(var) "]\n\t"

/* Load @var to r17 */
#define RSEQ_ASM_OP_R_LOAD(var)							\
		RSEQ_LOAD_LONG(var) "%%r17, %[" __rseq_str(var) "]\n\t"

/* Store r17 to @var */
#define RSEQ_ASM_OP_R_STORE(var)						\
		RSEQ_STORE_LONG(var) "%%r17, %[" __rseq_str(var) "]\n\t"

/* Add @count to r17 */
#define RSEQ_ASM_OP_R_ADD(count)						\
		"add %%r17, %[" __rseq_str(count) "], %%r17\n\t"

/* Load (r17 + voffp) to r17 */
#define RSEQ_ASM_OP_R_LOADX(voffp)						\
		RSEQ_LOADX_LONG "%%r17, %[" __rseq_str(voffp) "], %%r17\n\t"

/* TODO: implement a faster memcpy. */
#define RSEQ_ASM_OP_R_MEMCPY() \
		RSEQ_CMP_LONG_INT "%%r19, 0\n\t" \
		"beq 333f\n\t" \
		"addi %%r20, %%r20, -1\n\t" \
		"addi %%r21, %%r21, -1\n\t" \
		"222:\n\t" \
		"lbzu %%r18, 1(%%r20)\n\t" \
		"stbu %%r18, 1(%%r21)\n\t" \
		"addi %%r19, %%r19, -1\n\t" \
		RSEQ_CMP_LONG_INT "%%r19, 0\n\t" \
		"bne 222b\n\t" \
		"333:\n\t" \

#define RSEQ_ASM_OP_R_FINAL_STORE(var, post_commit_label)			\
		RSEQ_STORE_LONG(var) "%%r17, %[" __rseq_str(var) "]\n\t"			\
		__rseq_str(post_commit_label) ":\n\t"

#define RSEQ_ASM_OP_FINAL_STORE(value, var, post_commit_label)			\
		RSEQ_STORE_LONG(var) "%[" __rseq_str(value) "], %[" __rseq_str(var) "]\n\t" \
		__rseq_str(post_commit_label) ":\n\t"

/* Per-cpu-id indexing. */

#define RSEQ_TEMPLATE_CPU_ID
#define RSEQ_TEMPLATE_MO_RELAXED
#include "rseq-ppc-bits.h"
#undef RSEQ_TEMPLATE_MO_RELAXED

#define RSEQ_TEMPLATE_MO_RELEASE
#include "rseq-ppc-bits.h"
#undef RSEQ_TEMPLATE_MO_RELEASE
#undef RSEQ_TEMPLATE_CPU_ID

/* Per-mm-cid indexing. */

#define RSEQ_TEMPLATE_MM_CID
#define RSEQ_TEMPLATE_MO_RELAXED
#include "rseq-ppc-bits.h"
#undef RSEQ_TEMPLATE_MO_RELAXED

#define RSEQ_TEMPLATE_MO_RELEASE
#include "rseq-ppc-bits.h"
#undef RSEQ_TEMPLATE_MO_RELEASE
#undef RSEQ_TEMPLATE_MM_CID

/* APIs which are not based on cpu ids. */

#define RSEQ_TEMPLATE_CPU_ID_NONE
#define RSEQ_TEMPLATE_MO_RELAXED
#include "rseq-ppc-bits.h"
#undef RSEQ_TEMPLATE_MO_RELAXED
#undef RSEQ_TEMPLATE_CPU_ID_NONE
