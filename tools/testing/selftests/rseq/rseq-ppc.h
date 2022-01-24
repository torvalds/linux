/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * rseq-ppc.h
 *
 * (C) Copyright 2016-2018 - Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
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

#ifdef RSEQ_SKIP_FASTPATH
#include "rseq-skip.h"
#else /* !RSEQ_SKIP_FASTPATH */

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

static inline __attribute__((always_inline))
int rseq_cmpeqv_storev(intptr_t *v, intptr_t expect, intptr_t newv, int cpu)
{
	RSEQ_INJECT_C(9)

	__asm__ __volatile__ goto (
		RSEQ_ASM_DEFINE_TABLE(3, 1f, 2f, 4f) /* start, commit, abort */
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[cmpfail])
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[error1])
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[error2])
#endif
		/* Start rseq by storing table entry pointer into rseq_cs. */
		RSEQ_ASM_STORE_RSEQ_CS(1, 3b, rseq_cs)
		/* cmp cpuid */
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
		RSEQ_INJECT_ASM(3)
		/* cmp @v equal to @expect */
		RSEQ_ASM_OP_CMPEQ(v, expect, %l[cmpfail])
		RSEQ_INJECT_ASM(4)
#ifdef RSEQ_COMPARE_TWICE
		/* cmp cpuid */
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, %l[error1])
		/* cmp @v equal to @expect */
		RSEQ_ASM_OP_CMPEQ(v, expect, %l[error2])
#endif
		/* final store */
		RSEQ_ASM_OP_FINAL_STORE(newv, v, 2)
		RSEQ_INJECT_ASM(5)
		RSEQ_ASM_DEFINE_ABORT(4, abort)
		: /* gcc asm goto does not allow outputs */
		: [cpu_id]		"r" (cpu),
		  [current_cpu_id]	"m" (rseq_get_abi()->cpu_id),
		  [rseq_cs]		"m" (rseq_get_abi()->rseq_cs.arch.ptr),
		  [v]			"m" (*v),
		  [expect]		"r" (expect),
		  [newv]		"r" (newv)
		  RSEQ_INJECT_INPUT
		: "memory", "cc", "r17"
		  RSEQ_INJECT_CLOBBER
		: abort, cmpfail
#ifdef RSEQ_COMPARE_TWICE
		  , error1, error2
#endif
	);
	return 0;
abort:
	RSEQ_INJECT_FAILED
	return -1;
cmpfail:
	return 1;
#ifdef RSEQ_COMPARE_TWICE
error1:
	rseq_bug("cpu_id comparison failed");
error2:
	rseq_bug("expected value comparison failed");
#endif
}

static inline __attribute__((always_inline))
int rseq_cmpnev_storeoffp_load(intptr_t *v, intptr_t expectnot,
			       off_t voffp, intptr_t *load, int cpu)
{
	RSEQ_INJECT_C(9)

	__asm__ __volatile__ goto (
		RSEQ_ASM_DEFINE_TABLE(3, 1f, 2f, 4f) /* start, commit, abort */
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[cmpfail])
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[error1])
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[error2])
#endif
		/* Start rseq by storing table entry pointer into rseq_cs. */
		RSEQ_ASM_STORE_RSEQ_CS(1, 3b, rseq_cs)
		/* cmp cpuid */
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
		RSEQ_INJECT_ASM(3)
		/* cmp @v not equal to @expectnot */
		RSEQ_ASM_OP_CMPNE(v, expectnot, %l[cmpfail])
		RSEQ_INJECT_ASM(4)
#ifdef RSEQ_COMPARE_TWICE
		/* cmp cpuid */
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, %l[error1])
		/* cmp @v not equal to @expectnot */
		RSEQ_ASM_OP_CMPNE(v, expectnot, %l[error2])
#endif
		/* load the value of @v */
		RSEQ_ASM_OP_R_LOAD(v)
		/* store it in @load */
		RSEQ_ASM_OP_R_STORE(load)
		/* dereference voffp(v) */
		RSEQ_ASM_OP_R_LOADX(voffp)
		/* final store the value at voffp(v) */
		RSEQ_ASM_OP_R_FINAL_STORE(v, 2)
		RSEQ_INJECT_ASM(5)
		RSEQ_ASM_DEFINE_ABORT(4, abort)
		: /* gcc asm goto does not allow outputs */
		: [cpu_id]		"r" (cpu),
		  [current_cpu_id]	"m" (rseq_get_abi()->cpu_id),
		  [rseq_cs]		"m" (rseq_get_abi()->rseq_cs.arch.ptr),
		  /* final store input */
		  [v]			"m" (*v),
		  [expectnot]		"r" (expectnot),
		  [voffp]		"b" (voffp),
		  [load]		"m" (*load)
		  RSEQ_INJECT_INPUT
		: "memory", "cc", "r17"
		  RSEQ_INJECT_CLOBBER
		: abort, cmpfail
#ifdef RSEQ_COMPARE_TWICE
		  , error1, error2
#endif
	);
	return 0;
abort:
	RSEQ_INJECT_FAILED
	return -1;
cmpfail:
	return 1;
#ifdef RSEQ_COMPARE_TWICE
error1:
	rseq_bug("cpu_id comparison failed");
error2:
	rseq_bug("expected value comparison failed");
#endif
}

static inline __attribute__((always_inline))
int rseq_addv(intptr_t *v, intptr_t count, int cpu)
{
	RSEQ_INJECT_C(9)

	__asm__ __volatile__ goto (
		RSEQ_ASM_DEFINE_TABLE(3, 1f, 2f, 4f) /* start, commit, abort */
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[error1])
#endif
		/* Start rseq by storing table entry pointer into rseq_cs. */
		RSEQ_ASM_STORE_RSEQ_CS(1, 3b, rseq_cs)
		/* cmp cpuid */
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
		RSEQ_INJECT_ASM(3)
#ifdef RSEQ_COMPARE_TWICE
		/* cmp cpuid */
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, %l[error1])
#endif
		/* load the value of @v */
		RSEQ_ASM_OP_R_LOAD(v)
		/* add @count to it */
		RSEQ_ASM_OP_R_ADD(count)
		/* final store */
		RSEQ_ASM_OP_R_FINAL_STORE(v, 2)
		RSEQ_INJECT_ASM(4)
		RSEQ_ASM_DEFINE_ABORT(4, abort)
		: /* gcc asm goto does not allow outputs */
		: [cpu_id]		"r" (cpu),
		  [current_cpu_id]	"m" (rseq_get_abi()->cpu_id),
		  [rseq_cs]		"m" (rseq_get_abi()->rseq_cs.arch.ptr),
		  /* final store input */
		  [v]			"m" (*v),
		  [count]		"r" (count)
		  RSEQ_INJECT_INPUT
		: "memory", "cc", "r17"
		  RSEQ_INJECT_CLOBBER
		: abort
#ifdef RSEQ_COMPARE_TWICE
		  , error1
#endif
	);
	return 0;
abort:
	RSEQ_INJECT_FAILED
	return -1;
#ifdef RSEQ_COMPARE_TWICE
error1:
	rseq_bug("cpu_id comparison failed");
#endif
}

static inline __attribute__((always_inline))
int rseq_cmpeqv_trystorev_storev(intptr_t *v, intptr_t expect,
				 intptr_t *v2, intptr_t newv2,
				 intptr_t newv, int cpu)
{
	RSEQ_INJECT_C(9)

	__asm__ __volatile__ goto (
		RSEQ_ASM_DEFINE_TABLE(3, 1f, 2f, 4f) /* start, commit, abort */
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[cmpfail])
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[error1])
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[error2])
#endif
		/* Start rseq by storing table entry pointer into rseq_cs. */
		RSEQ_ASM_STORE_RSEQ_CS(1, 3b, rseq_cs)
		/* cmp cpuid */
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
		RSEQ_INJECT_ASM(3)
		/* cmp @v equal to @expect */
		RSEQ_ASM_OP_CMPEQ(v, expect, %l[cmpfail])
		RSEQ_INJECT_ASM(4)
#ifdef RSEQ_COMPARE_TWICE
		/* cmp cpuid */
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, %l[error1])
		/* cmp @v equal to @expect */
		RSEQ_ASM_OP_CMPEQ(v, expect, %l[error2])
#endif
		/* try store */
		RSEQ_ASM_OP_STORE(newv2, v2)
		RSEQ_INJECT_ASM(5)
		/* final store */
		RSEQ_ASM_OP_FINAL_STORE(newv, v, 2)
		RSEQ_INJECT_ASM(6)
		RSEQ_ASM_DEFINE_ABORT(4, abort)
		: /* gcc asm goto does not allow outputs */
		: [cpu_id]		"r" (cpu),
		  [current_cpu_id]	"m" (rseq_get_abi()->cpu_id),
		  [rseq_cs]		"m" (rseq_get_abi()->rseq_cs.arch.ptr),
		  /* try store input */
		  [v2]			"m" (*v2),
		  [newv2]		"r" (newv2),
		  /* final store input */
		  [v]			"m" (*v),
		  [expect]		"r" (expect),
		  [newv]		"r" (newv)
		  RSEQ_INJECT_INPUT
		: "memory", "cc", "r17"
		  RSEQ_INJECT_CLOBBER
		: abort, cmpfail
#ifdef RSEQ_COMPARE_TWICE
		  , error1, error2
#endif
	);
	return 0;
abort:
	RSEQ_INJECT_FAILED
	return -1;
cmpfail:
	return 1;
#ifdef RSEQ_COMPARE_TWICE
error1:
	rseq_bug("cpu_id comparison failed");
error2:
	rseq_bug("expected value comparison failed");
#endif
}

static inline __attribute__((always_inline))
int rseq_cmpeqv_trystorev_storev_release(intptr_t *v, intptr_t expect,
					 intptr_t *v2, intptr_t newv2,
					 intptr_t newv, int cpu)
{
	RSEQ_INJECT_C(9)

	__asm__ __volatile__ goto (
		RSEQ_ASM_DEFINE_TABLE(3, 1f, 2f, 4f) /* start, commit, abort */
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[cmpfail])
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[error1])
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[error2])
#endif
		/* Start rseq by storing table entry pointer into rseq_cs. */
		RSEQ_ASM_STORE_RSEQ_CS(1, 3b, rseq_cs)
		/* cmp cpuid */
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
		RSEQ_INJECT_ASM(3)
		/* cmp @v equal to @expect */
		RSEQ_ASM_OP_CMPEQ(v, expect, %l[cmpfail])
		RSEQ_INJECT_ASM(4)
#ifdef RSEQ_COMPARE_TWICE
		/* cmp cpuid */
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, %l[error1])
		/* cmp @v equal to @expect */
		RSEQ_ASM_OP_CMPEQ(v, expect, %l[error2])
#endif
		/* try store */
		RSEQ_ASM_OP_STORE(newv2, v2)
		RSEQ_INJECT_ASM(5)
		/* for 'release' */
		"lwsync\n\t"
		/* final store */
		RSEQ_ASM_OP_FINAL_STORE(newv, v, 2)
		RSEQ_INJECT_ASM(6)
		RSEQ_ASM_DEFINE_ABORT(4, abort)
		: /* gcc asm goto does not allow outputs */
		: [cpu_id]		"r" (cpu),
		  [current_cpu_id]	"m" (rseq_get_abi()->cpu_id),
		  [rseq_cs]		"m" (rseq_get_abi()->rseq_cs.arch.ptr),
		  /* try store input */
		  [v2]			"m" (*v2),
		  [newv2]		"r" (newv2),
		  /* final store input */
		  [v]			"m" (*v),
		  [expect]		"r" (expect),
		  [newv]		"r" (newv)
		  RSEQ_INJECT_INPUT
		: "memory", "cc", "r17"
		  RSEQ_INJECT_CLOBBER
		: abort, cmpfail
#ifdef RSEQ_COMPARE_TWICE
		  , error1, error2
#endif
	);
	return 0;
abort:
	RSEQ_INJECT_FAILED
	return -1;
cmpfail:
	return 1;
#ifdef RSEQ_COMPARE_TWICE
error1:
	rseq_bug("cpu_id comparison failed");
error2:
	rseq_bug("expected value comparison failed");
#endif
}

static inline __attribute__((always_inline))
int rseq_cmpeqv_cmpeqv_storev(intptr_t *v, intptr_t expect,
			      intptr_t *v2, intptr_t expect2,
			      intptr_t newv, int cpu)
{
	RSEQ_INJECT_C(9)

	__asm__ __volatile__ goto (
		RSEQ_ASM_DEFINE_TABLE(3, 1f, 2f, 4f) /* start, commit, abort */
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[cmpfail])
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[error1])
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[error2])
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[error3])
#endif
		/* Start rseq by storing table entry pointer into rseq_cs. */
		RSEQ_ASM_STORE_RSEQ_CS(1, 3b, rseq_cs)
		/* cmp cpuid */
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
		RSEQ_INJECT_ASM(3)
		/* cmp @v equal to @expect */
		RSEQ_ASM_OP_CMPEQ(v, expect, %l[cmpfail])
		RSEQ_INJECT_ASM(4)
		/* cmp @v2 equal to @expct2 */
		RSEQ_ASM_OP_CMPEQ(v2, expect2, %l[cmpfail])
		RSEQ_INJECT_ASM(5)
#ifdef RSEQ_COMPARE_TWICE
		/* cmp cpuid */
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, %l[error1])
		/* cmp @v equal to @expect */
		RSEQ_ASM_OP_CMPEQ(v, expect, %l[error2])
		/* cmp @v2 equal to @expct2 */
		RSEQ_ASM_OP_CMPEQ(v2, expect2, %l[error3])
#endif
		/* final store */
		RSEQ_ASM_OP_FINAL_STORE(newv, v, 2)
		RSEQ_INJECT_ASM(6)
		RSEQ_ASM_DEFINE_ABORT(4, abort)
		: /* gcc asm goto does not allow outputs */
		: [cpu_id]		"r" (cpu),
		  [current_cpu_id]	"m" (rseq_get_abi()->cpu_id),
		  [rseq_cs]		"m" (rseq_get_abi()->rseq_cs.arch.ptr),
		  /* cmp2 input */
		  [v2]			"m" (*v2),
		  [expect2]		"r" (expect2),
		  /* final store input */
		  [v]			"m" (*v),
		  [expect]		"r" (expect),
		  [newv]		"r" (newv)
		  RSEQ_INJECT_INPUT
		: "memory", "cc", "r17"
		  RSEQ_INJECT_CLOBBER
		: abort, cmpfail
#ifdef RSEQ_COMPARE_TWICE
		  , error1, error2, error3
#endif
	);
	return 0;
abort:
	RSEQ_INJECT_FAILED
	return -1;
cmpfail:
	return 1;
#ifdef RSEQ_COMPARE_TWICE
error1:
	rseq_bug("cpu_id comparison failed");
error2:
	rseq_bug("1st expected value comparison failed");
error3:
	rseq_bug("2nd expected value comparison failed");
#endif
}

static inline __attribute__((always_inline))
int rseq_cmpeqv_trymemcpy_storev(intptr_t *v, intptr_t expect,
				 void *dst, void *src, size_t len,
				 intptr_t newv, int cpu)
{
	RSEQ_INJECT_C(9)

	__asm__ __volatile__ goto (
		RSEQ_ASM_DEFINE_TABLE(3, 1f, 2f, 4f) /* start, commit, abort */
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[cmpfail])
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[error1])
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[error2])
#endif
		/* setup for mempcy */
		"mr %%r19, %[len]\n\t"
		"mr %%r20, %[src]\n\t"
		"mr %%r21, %[dst]\n\t"
		/* Start rseq by storing table entry pointer into rseq_cs. */
		RSEQ_ASM_STORE_RSEQ_CS(1, 3b, rseq_cs)
		/* cmp cpuid */
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
		RSEQ_INJECT_ASM(3)
		/* cmp @v equal to @expect */
		RSEQ_ASM_OP_CMPEQ(v, expect, %l[cmpfail])
		RSEQ_INJECT_ASM(4)
#ifdef RSEQ_COMPARE_TWICE
		/* cmp cpuid */
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, %l[error1])
		/* cmp @v equal to @expect */
		RSEQ_ASM_OP_CMPEQ(v, expect, %l[error2])
#endif
		/* try memcpy */
		RSEQ_ASM_OP_R_MEMCPY()
		RSEQ_INJECT_ASM(5)
		/* final store */
		RSEQ_ASM_OP_FINAL_STORE(newv, v, 2)
		RSEQ_INJECT_ASM(6)
		/* teardown */
		RSEQ_ASM_DEFINE_ABORT(4, abort)
		: /* gcc asm goto does not allow outputs */
		: [cpu_id]		"r" (cpu),
		  [current_cpu_id]	"m" (rseq_get_abi()->cpu_id),
		  [rseq_cs]		"m" (rseq_get_abi()->rseq_cs.arch.ptr),
		  /* final store input */
		  [v]			"m" (*v),
		  [expect]		"r" (expect),
		  [newv]		"r" (newv),
		  /* try memcpy input */
		  [dst]			"r" (dst),
		  [src]			"r" (src),
		  [len]			"r" (len)
		  RSEQ_INJECT_INPUT
		: "memory", "cc", "r17", "r18", "r19", "r20", "r21"
		  RSEQ_INJECT_CLOBBER
		: abort, cmpfail
#ifdef RSEQ_COMPARE_TWICE
		  , error1, error2
#endif
	);
	return 0;
abort:
	RSEQ_INJECT_FAILED
	return -1;
cmpfail:
	return 1;
#ifdef RSEQ_COMPARE_TWICE
error1:
	rseq_bug("cpu_id comparison failed");
error2:
	rseq_bug("expected value comparison failed");
#endif
}

static inline __attribute__((always_inline))
int rseq_cmpeqv_trymemcpy_storev_release(intptr_t *v, intptr_t expect,
					 void *dst, void *src, size_t len,
					 intptr_t newv, int cpu)
{
	RSEQ_INJECT_C(9)

	__asm__ __volatile__ goto (
		RSEQ_ASM_DEFINE_TABLE(3, 1f, 2f, 4f) /* start, commit, abort */
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[cmpfail])
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[error1])
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[error2])
#endif
		/* setup for mempcy */
		"mr %%r19, %[len]\n\t"
		"mr %%r20, %[src]\n\t"
		"mr %%r21, %[dst]\n\t"
		/* Start rseq by storing table entry pointer into rseq_cs. */
		RSEQ_ASM_STORE_RSEQ_CS(1, 3b, rseq_cs)
		/* cmp cpuid */
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
		RSEQ_INJECT_ASM(3)
		/* cmp @v equal to @expect */
		RSEQ_ASM_OP_CMPEQ(v, expect, %l[cmpfail])
		RSEQ_INJECT_ASM(4)
#ifdef RSEQ_COMPARE_TWICE
		/* cmp cpuid */
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, %l[error1])
		/* cmp @v equal to @expect */
		RSEQ_ASM_OP_CMPEQ(v, expect, %l[error2])
#endif
		/* try memcpy */
		RSEQ_ASM_OP_R_MEMCPY()
		RSEQ_INJECT_ASM(5)
		/* for 'release' */
		"lwsync\n\t"
		/* final store */
		RSEQ_ASM_OP_FINAL_STORE(newv, v, 2)
		RSEQ_INJECT_ASM(6)
		/* teardown */
		RSEQ_ASM_DEFINE_ABORT(4, abort)
		: /* gcc asm goto does not allow outputs */
		: [cpu_id]		"r" (cpu),
		  [current_cpu_id]	"m" (rseq_get_abi()->cpu_id),
		  [rseq_cs]		"m" (rseq_get_abi()->rseq_cs.arch.ptr),
		  /* final store input */
		  [v]			"m" (*v),
		  [expect]		"r" (expect),
		  [newv]		"r" (newv),
		  /* try memcpy input */
		  [dst]			"r" (dst),
		  [src]			"r" (src),
		  [len]			"r" (len)
		  RSEQ_INJECT_INPUT
		: "memory", "cc", "r17", "r18", "r19", "r20", "r21"
		  RSEQ_INJECT_CLOBBER
		: abort, cmpfail
#ifdef RSEQ_COMPARE_TWICE
		  , error1, error2
#endif
	);
	return 0;
abort:
	RSEQ_INJECT_FAILED
	return -1;
cmpfail:
	return 1;
#ifdef RSEQ_COMPARE_TWICE
error1:
	rseq_bug("cpu_id comparison failed");
error2:
	rseq_bug("expected value comparison failed");
#endif
}

#endif /* !RSEQ_SKIP_FASTPATH */
