/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * rseq-arm.h
 *
 * (C) Copyright 2016-2018 - Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 */

/*
 * - ARM little endian
 *
 * RSEQ_SIG uses the udf A32 instruction with an uncommon immediate operand
 * value 0x5de3. This traps if user-space reaches this instruction by mistake,
 * and the uncommon operand ensures the kernel does not move the instruction
 * pointer to attacker-controlled code on rseq abort.
 *
 * The instruction pattern in the A32 instruction set is:
 *
 * e7f5def3    udf    #24035    ; 0x5de3
 *
 * This translates to the following instruction pattern in the T16 instruction
 * set:
 *
 * little endian:
 * def3        udf    #243      ; 0xf3
 * e7f5        b.n    <7f5>
 *
 * - ARMv6+ big endian (BE8):
 *
 * ARMv6+ -mbig-endian generates mixed endianness code vs data: little-endian
 * code and big-endian data. The data value of the signature needs to have its
 * byte order reversed to generate the trap instruction:
 *
 * Data: 0xf3def5e7
 *
 * Translates to this A32 instruction pattern:
 *
 * e7f5def3    udf    #24035    ; 0x5de3
 *
 * Translates to this T16 instruction pattern:
 *
 * def3        udf    #243      ; 0xf3
 * e7f5        b.n    <7f5>
 *
 * - Prior to ARMv6 big endian (BE32):
 *
 * Prior to ARMv6, -mbig-endian generates big-endian code and data
 * (which match), so the endianness of the data representation of the
 * signature should not be reversed. However, the choice between BE32
 * and BE8 is done by the linker, so we cannot know whether code and
 * data endianness will be mixed before the linker is invoked. So rather
 * than try to play tricks with the linker, the rseq signature is simply
 * data (not a trap instruction) prior to ARMv6 on big endian. This is
 * why the signature is expressed as data (.word) rather than as
 * instruction (.inst) in assembler.
 */

#ifdef __ARMEB__
#define RSEQ_SIG    0xf3def5e7      /* udf    #24035    ; 0x5de3 (ARMv6+) */
#else
#define RSEQ_SIG    0xe7f5def3      /* udf    #24035    ; 0x5de3 */
#endif

#define rseq_smp_mb()	__asm__ __volatile__ ("dmb" ::: "memory", "cc")
#define rseq_smp_rmb()	__asm__ __volatile__ ("dmb" ::: "memory", "cc")
#define rseq_smp_wmb()	__asm__ __volatile__ ("dmb" ::: "memory", "cc")

#define rseq_smp_load_acquire(p)					\
__extension__ ({							\
	__typeof(*p) ____p1 = RSEQ_READ_ONCE(*p);			\
	rseq_smp_mb();							\
	____p1;								\
})

#define rseq_smp_acquire__after_ctrl_dep()	rseq_smp_rmb()

#define rseq_smp_store_release(p, v)					\
do {									\
	rseq_smp_mb();							\
	RSEQ_WRITE_ONCE(*p, v);						\
} while (0)

#ifdef RSEQ_SKIP_FASTPATH
#include "rseq-skip.h"
#else /* !RSEQ_SKIP_FASTPATH */

#define __RSEQ_ASM_DEFINE_TABLE(label, version, flags, start_ip,	\
				post_commit_offset, abort_ip)		\
		".pushsection __rseq_cs, \"aw\"\n\t"			\
		".balign 32\n\t"					\
		__rseq_str(label) ":\n\t"					\
		".word " __rseq_str(version) ", " __rseq_str(flags) "\n\t" \
		".word " __rseq_str(start_ip) ", 0x0, " __rseq_str(post_commit_offset) ", 0x0, " __rseq_str(abort_ip) ", 0x0\n\t" \
		".popsection\n\t"					\
		".pushsection __rseq_cs_ptr_array, \"aw\"\n\t"		\
		".word " __rseq_str(label) "b, 0x0\n\t"			\
		".popsection\n\t"

#define RSEQ_ASM_DEFINE_TABLE(label, start_ip, post_commit_ip, abort_ip) \
	__RSEQ_ASM_DEFINE_TABLE(label, 0x0, 0x0, start_ip,		\
				(post_commit_ip - start_ip), abort_ip)

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
		".word " __rseq_str(start_ip) ", 0x0, " __rseq_str(exit_ip) ", 0x0\n\t" \
		".popsection\n\t"

#define RSEQ_ASM_STORE_RSEQ_CS(label, cs_label, rseq_cs)		\
		RSEQ_INJECT_ASM(1)					\
		"adr r0, " __rseq_str(cs_label) "\n\t"			\
		"str r0, %[" __rseq_str(rseq_cs) "]\n\t"		\
		__rseq_str(label) ":\n\t"

#define RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, label)		\
		RSEQ_INJECT_ASM(2)					\
		"ldr r0, %[" __rseq_str(current_cpu_id) "]\n\t"	\
		"cmp %[" __rseq_str(cpu_id) "], r0\n\t"		\
		"bne " __rseq_str(label) "\n\t"

#define __RSEQ_ASM_DEFINE_ABORT(table_label, label, teardown,		\
				abort_label, version, flags,		\
				start_ip, post_commit_offset, abort_ip)	\
		".balign 32\n\t"					\
		__rseq_str(table_label) ":\n\t"				\
		".word " __rseq_str(version) ", " __rseq_str(flags) "\n\t" \
		".word " __rseq_str(start_ip) ", 0x0, " __rseq_str(post_commit_offset) ", 0x0, " __rseq_str(abort_ip) ", 0x0\n\t" \
		".word " __rseq_str(RSEQ_SIG) "\n\t"			\
		__rseq_str(label) ":\n\t"				\
		teardown						\
		"b %l[" __rseq_str(abort_label) "]\n\t"

#define RSEQ_ASM_DEFINE_ABORT(table_label, label, teardown, abort_label, \
			      start_ip, post_commit_ip, abort_ip)	\
	__RSEQ_ASM_DEFINE_ABORT(table_label, label, teardown,		\
				abort_label, 0x0, 0x0, start_ip,	\
				(post_commit_ip - start_ip), abort_ip)

#define RSEQ_ASM_DEFINE_CMPFAIL(label, teardown, cmpfail_label)		\
		__rseq_str(label) ":\n\t"				\
		teardown						\
		"b %l[" __rseq_str(cmpfail_label) "]\n\t"

#define rseq_workaround_gcc_asm_size_guess()	__asm__ __volatile__("")

static inline __attribute__((always_inline))
int rseq_cmpeqv_storev(intptr_t *v, intptr_t expect, intptr_t newv, int cpu)
{
	RSEQ_INJECT_C(9)

	rseq_workaround_gcc_asm_size_guess();
	__asm__ __volatile__ goto (
		RSEQ_ASM_DEFINE_TABLE(9, 1f, 2f, 4f) /* start, commit, abort */
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[cmpfail])
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[error1])
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[error2])
#endif
		/* Start rseq by storing table entry pointer into rseq_cs. */
		RSEQ_ASM_STORE_RSEQ_CS(1, 3f, rseq_cs)
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
		RSEQ_INJECT_ASM(3)
		"ldr r0, %[v]\n\t"
		"cmp %[expect], r0\n\t"
		"bne %l[cmpfail]\n\t"
		RSEQ_INJECT_ASM(4)
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, %l[error1])
		"ldr r0, %[v]\n\t"
		"cmp %[expect], r0\n\t"
		"bne %l[error2]\n\t"
#endif
		/* final store */
		"str %[newv], %[v]\n\t"
		"2:\n\t"
		RSEQ_INJECT_ASM(5)
		"b 5f\n\t"
		RSEQ_ASM_DEFINE_ABORT(3, 4, "", abort, 1b, 2b, 4f)
		"5:\n\t"
		: /* gcc asm goto does not allow outputs */
		: [cpu_id]		"r" (cpu),
		  [current_cpu_id]	"m" (rseq_get_abi()->cpu_id),
		  [rseq_cs]		"m" (rseq_get_abi()->rseq_cs),
		  [v]			"m" (*v),
		  [expect]		"r" (expect),
		  [newv]		"r" (newv)
		  RSEQ_INJECT_INPUT
		: "r0", "memory", "cc"
		  RSEQ_INJECT_CLOBBER
		: abort, cmpfail
#ifdef RSEQ_COMPARE_TWICE
		  , error1, error2
#endif
	);
	rseq_workaround_gcc_asm_size_guess();
	return 0;
abort:
	rseq_workaround_gcc_asm_size_guess();
	RSEQ_INJECT_FAILED
	return -1;
cmpfail:
	rseq_workaround_gcc_asm_size_guess();
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

	rseq_workaround_gcc_asm_size_guess();
	__asm__ __volatile__ goto (
		RSEQ_ASM_DEFINE_TABLE(9, 1f, 2f, 4f) /* start, commit, abort */
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[cmpfail])
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[error1])
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[error2])
#endif
		/* Start rseq by storing table entry pointer into rseq_cs. */
		RSEQ_ASM_STORE_RSEQ_CS(1, 3f, rseq_cs)
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
		RSEQ_INJECT_ASM(3)
		"ldr r0, %[v]\n\t"
		"cmp %[expectnot], r0\n\t"
		"beq %l[cmpfail]\n\t"
		RSEQ_INJECT_ASM(4)
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, %l[error1])
		"ldr r0, %[v]\n\t"
		"cmp %[expectnot], r0\n\t"
		"beq %l[error2]\n\t"
#endif
		"str r0, %[load]\n\t"
		"add r0, %[voffp]\n\t"
		"ldr r0, [r0]\n\t"
		/* final store */
		"str r0, %[v]\n\t"
		"2:\n\t"
		RSEQ_INJECT_ASM(5)
		"b 5f\n\t"
		RSEQ_ASM_DEFINE_ABORT(3, 4, "", abort, 1b, 2b, 4f)
		"5:\n\t"
		: /* gcc asm goto does not allow outputs */
		: [cpu_id]		"r" (cpu),
		  [current_cpu_id]	"m" (rseq_get_abi()->cpu_id),
		  [rseq_cs]		"m" (rseq_get_abi()->rseq_cs),
		  /* final store input */
		  [v]			"m" (*v),
		  [expectnot]		"r" (expectnot),
		  [voffp]		"Ir" (voffp),
		  [load]		"m" (*load)
		  RSEQ_INJECT_INPUT
		: "r0", "memory", "cc"
		  RSEQ_INJECT_CLOBBER
		: abort, cmpfail
#ifdef RSEQ_COMPARE_TWICE
		  , error1, error2
#endif
	);
	rseq_workaround_gcc_asm_size_guess();
	return 0;
abort:
	rseq_workaround_gcc_asm_size_guess();
	RSEQ_INJECT_FAILED
	return -1;
cmpfail:
	rseq_workaround_gcc_asm_size_guess();
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

	rseq_workaround_gcc_asm_size_guess();
	__asm__ __volatile__ goto (
		RSEQ_ASM_DEFINE_TABLE(9, 1f, 2f, 4f) /* start, commit, abort */
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[error1])
#endif
		/* Start rseq by storing table entry pointer into rseq_cs. */
		RSEQ_ASM_STORE_RSEQ_CS(1, 3f, rseq_cs)
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
		RSEQ_INJECT_ASM(3)
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, %l[error1])
#endif
		"ldr r0, %[v]\n\t"
		"add r0, %[count]\n\t"
		/* final store */
		"str r0, %[v]\n\t"
		"2:\n\t"
		RSEQ_INJECT_ASM(4)
		"b 5f\n\t"
		RSEQ_ASM_DEFINE_ABORT(3, 4, "", abort, 1b, 2b, 4f)
		"5:\n\t"
		: /* gcc asm goto does not allow outputs */
		: [cpu_id]		"r" (cpu),
		  [current_cpu_id]	"m" (rseq_get_abi()->cpu_id),
		  [rseq_cs]		"m" (rseq_get_abi()->rseq_cs),
		  [v]			"m" (*v),
		  [count]		"Ir" (count)
		  RSEQ_INJECT_INPUT
		: "r0", "memory", "cc"
		  RSEQ_INJECT_CLOBBER
		: abort
#ifdef RSEQ_COMPARE_TWICE
		  , error1
#endif
	);
	rseq_workaround_gcc_asm_size_guess();
	return 0;
abort:
	rseq_workaround_gcc_asm_size_guess();
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

	rseq_workaround_gcc_asm_size_guess();
	__asm__ __volatile__ goto (
		RSEQ_ASM_DEFINE_TABLE(9, 1f, 2f, 4f) /* start, commit, abort */
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[cmpfail])
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[error1])
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[error2])
#endif
		/* Start rseq by storing table entry pointer into rseq_cs. */
		RSEQ_ASM_STORE_RSEQ_CS(1, 3f, rseq_cs)
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
		RSEQ_INJECT_ASM(3)
		"ldr r0, %[v]\n\t"
		"cmp %[expect], r0\n\t"
		"bne %l[cmpfail]\n\t"
		RSEQ_INJECT_ASM(4)
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, %l[error1])
		"ldr r0, %[v]\n\t"
		"cmp %[expect], r0\n\t"
		"bne %l[error2]\n\t"
#endif
		/* try store */
		"str %[newv2], %[v2]\n\t"
		RSEQ_INJECT_ASM(5)
		/* final store */
		"str %[newv], %[v]\n\t"
		"2:\n\t"
		RSEQ_INJECT_ASM(6)
		"b 5f\n\t"
		RSEQ_ASM_DEFINE_ABORT(3, 4, "", abort, 1b, 2b, 4f)
		"5:\n\t"
		: /* gcc asm goto does not allow outputs */
		: [cpu_id]		"r" (cpu),
		  [current_cpu_id]	"m" (rseq_get_abi()->cpu_id),
		  [rseq_cs]		"m" (rseq_get_abi()->rseq_cs),
		  /* try store input */
		  [v2]			"m" (*v2),
		  [newv2]		"r" (newv2),
		  /* final store input */
		  [v]			"m" (*v),
		  [expect]		"r" (expect),
		  [newv]		"r" (newv)
		  RSEQ_INJECT_INPUT
		: "r0", "memory", "cc"
		  RSEQ_INJECT_CLOBBER
		: abort, cmpfail
#ifdef RSEQ_COMPARE_TWICE
		  , error1, error2
#endif
	);
	rseq_workaround_gcc_asm_size_guess();
	return 0;
abort:
	rseq_workaround_gcc_asm_size_guess();
	RSEQ_INJECT_FAILED
	return -1;
cmpfail:
	rseq_workaround_gcc_asm_size_guess();
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

	rseq_workaround_gcc_asm_size_guess();
	__asm__ __volatile__ goto (
		RSEQ_ASM_DEFINE_TABLE(9, 1f, 2f, 4f) /* start, commit, abort */
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[cmpfail])
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[error1])
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[error2])
#endif
		/* Start rseq by storing table entry pointer into rseq_cs. */
		RSEQ_ASM_STORE_RSEQ_CS(1, 3f, rseq_cs)
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
		RSEQ_INJECT_ASM(3)
		"ldr r0, %[v]\n\t"
		"cmp %[expect], r0\n\t"
		"bne %l[cmpfail]\n\t"
		RSEQ_INJECT_ASM(4)
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, %l[error1])
		"ldr r0, %[v]\n\t"
		"cmp %[expect], r0\n\t"
		"bne %l[error2]\n\t"
#endif
		/* try store */
		"str %[newv2], %[v2]\n\t"
		RSEQ_INJECT_ASM(5)
		"dmb\n\t"	/* full mb provides store-release */
		/* final store */
		"str %[newv], %[v]\n\t"
		"2:\n\t"
		RSEQ_INJECT_ASM(6)
		"b 5f\n\t"
		RSEQ_ASM_DEFINE_ABORT(3, 4, "", abort, 1b, 2b, 4f)
		"5:\n\t"
		: /* gcc asm goto does not allow outputs */
		: [cpu_id]		"r" (cpu),
		  [current_cpu_id]	"m" (rseq_get_abi()->cpu_id),
		  [rseq_cs]		"m" (rseq_get_abi()->rseq_cs),
		  /* try store input */
		  [v2]			"m" (*v2),
		  [newv2]		"r" (newv2),
		  /* final store input */
		  [v]			"m" (*v),
		  [expect]		"r" (expect),
		  [newv]		"r" (newv)
		  RSEQ_INJECT_INPUT
		: "r0", "memory", "cc"
		  RSEQ_INJECT_CLOBBER
		: abort, cmpfail
#ifdef RSEQ_COMPARE_TWICE
		  , error1, error2
#endif
	);
	rseq_workaround_gcc_asm_size_guess();
	return 0;
abort:
	rseq_workaround_gcc_asm_size_guess();
	RSEQ_INJECT_FAILED
	return -1;
cmpfail:
	rseq_workaround_gcc_asm_size_guess();
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

	rseq_workaround_gcc_asm_size_guess();
	__asm__ __volatile__ goto (
		RSEQ_ASM_DEFINE_TABLE(9, 1f, 2f, 4f) /* start, commit, abort */
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[cmpfail])
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[error1])
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[error2])
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[error3])
#endif
		/* Start rseq by storing table entry pointer into rseq_cs. */
		RSEQ_ASM_STORE_RSEQ_CS(1, 3f, rseq_cs)
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
		RSEQ_INJECT_ASM(3)
		"ldr r0, %[v]\n\t"
		"cmp %[expect], r0\n\t"
		"bne %l[cmpfail]\n\t"
		RSEQ_INJECT_ASM(4)
		"ldr r0, %[v2]\n\t"
		"cmp %[expect2], r0\n\t"
		"bne %l[cmpfail]\n\t"
		RSEQ_INJECT_ASM(5)
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, %l[error1])
		"ldr r0, %[v]\n\t"
		"cmp %[expect], r0\n\t"
		"bne %l[error2]\n\t"
		"ldr r0, %[v2]\n\t"
		"cmp %[expect2], r0\n\t"
		"bne %l[error3]\n\t"
#endif
		/* final store */
		"str %[newv], %[v]\n\t"
		"2:\n\t"
		RSEQ_INJECT_ASM(6)
		"b 5f\n\t"
		RSEQ_ASM_DEFINE_ABORT(3, 4, "", abort, 1b, 2b, 4f)
		"5:\n\t"
		: /* gcc asm goto does not allow outputs */
		: [cpu_id]		"r" (cpu),
		  [current_cpu_id]	"m" (rseq_get_abi()->cpu_id),
		  [rseq_cs]		"m" (rseq_get_abi()->rseq_cs),
		  /* cmp2 input */
		  [v2]			"m" (*v2),
		  [expect2]		"r" (expect2),
		  /* final store input */
		  [v]			"m" (*v),
		  [expect]		"r" (expect),
		  [newv]		"r" (newv)
		  RSEQ_INJECT_INPUT
		: "r0", "memory", "cc"
		  RSEQ_INJECT_CLOBBER
		: abort, cmpfail
#ifdef RSEQ_COMPARE_TWICE
		  , error1, error2, error3
#endif
	);
	rseq_workaround_gcc_asm_size_guess();
	return 0;
abort:
	rseq_workaround_gcc_asm_size_guess();
	RSEQ_INJECT_FAILED
	return -1;
cmpfail:
	rseq_workaround_gcc_asm_size_guess();
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
	uint32_t rseq_scratch[3];

	RSEQ_INJECT_C(9)

	rseq_workaround_gcc_asm_size_guess();
	__asm__ __volatile__ goto (
		RSEQ_ASM_DEFINE_TABLE(9, 1f, 2f, 4f) /* start, commit, abort */
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[cmpfail])
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[error1])
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[error2])
#endif
		"str %[src], %[rseq_scratch0]\n\t"
		"str %[dst], %[rseq_scratch1]\n\t"
		"str %[len], %[rseq_scratch2]\n\t"
		/* Start rseq by storing table entry pointer into rseq_cs. */
		RSEQ_ASM_STORE_RSEQ_CS(1, 3f, rseq_cs)
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
		RSEQ_INJECT_ASM(3)
		"ldr r0, %[v]\n\t"
		"cmp %[expect], r0\n\t"
		"bne 5f\n\t"
		RSEQ_INJECT_ASM(4)
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 6f)
		"ldr r0, %[v]\n\t"
		"cmp %[expect], r0\n\t"
		"bne 7f\n\t"
#endif
		/* try memcpy */
		"cmp %[len], #0\n\t" \
		"beq 333f\n\t" \
		"222:\n\t" \
		"ldrb %%r0, [%[src]]\n\t" \
		"strb %%r0, [%[dst]]\n\t" \
		"adds %[src], #1\n\t" \
		"adds %[dst], #1\n\t" \
		"subs %[len], #1\n\t" \
		"bne 222b\n\t" \
		"333:\n\t" \
		RSEQ_INJECT_ASM(5)
		/* final store */
		"str %[newv], %[v]\n\t"
		"2:\n\t"
		RSEQ_INJECT_ASM(6)
		/* teardown */
		"ldr %[len], %[rseq_scratch2]\n\t"
		"ldr %[dst], %[rseq_scratch1]\n\t"
		"ldr %[src], %[rseq_scratch0]\n\t"
		"b 8f\n\t"
		RSEQ_ASM_DEFINE_ABORT(3, 4,
				      /* teardown */
				      "ldr %[len], %[rseq_scratch2]\n\t"
				      "ldr %[dst], %[rseq_scratch1]\n\t"
				      "ldr %[src], %[rseq_scratch0]\n\t",
				      abort, 1b, 2b, 4f)
		RSEQ_ASM_DEFINE_CMPFAIL(5,
					/* teardown */
					"ldr %[len], %[rseq_scratch2]\n\t"
					"ldr %[dst], %[rseq_scratch1]\n\t"
					"ldr %[src], %[rseq_scratch0]\n\t",
					cmpfail)
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_DEFINE_CMPFAIL(6,
					/* teardown */
					"ldr %[len], %[rseq_scratch2]\n\t"
					"ldr %[dst], %[rseq_scratch1]\n\t"
					"ldr %[src], %[rseq_scratch0]\n\t",
					error1)
		RSEQ_ASM_DEFINE_CMPFAIL(7,
					/* teardown */
					"ldr %[len], %[rseq_scratch2]\n\t"
					"ldr %[dst], %[rseq_scratch1]\n\t"
					"ldr %[src], %[rseq_scratch0]\n\t",
					error2)
#endif
		"8:\n\t"
		: /* gcc asm goto does not allow outputs */
		: [cpu_id]		"r" (cpu),
		  [current_cpu_id]	"m" (rseq_get_abi()->cpu_id),
		  [rseq_cs]		"m" (rseq_get_abi()->rseq_cs),
		  /* final store input */
		  [v]			"m" (*v),
		  [expect]		"r" (expect),
		  [newv]		"r" (newv),
		  /* try memcpy input */
		  [dst]			"r" (dst),
		  [src]			"r" (src),
		  [len]			"r" (len),
		  [rseq_scratch0]	"m" (rseq_scratch[0]),
		  [rseq_scratch1]	"m" (rseq_scratch[1]),
		  [rseq_scratch2]	"m" (rseq_scratch[2])
		  RSEQ_INJECT_INPUT
		: "r0", "memory", "cc"
		  RSEQ_INJECT_CLOBBER
		: abort, cmpfail
#ifdef RSEQ_COMPARE_TWICE
		  , error1, error2
#endif
	);
	rseq_workaround_gcc_asm_size_guess();
	return 0;
abort:
	rseq_workaround_gcc_asm_size_guess();
	RSEQ_INJECT_FAILED
	return -1;
cmpfail:
	rseq_workaround_gcc_asm_size_guess();
	return 1;
#ifdef RSEQ_COMPARE_TWICE
error1:
	rseq_workaround_gcc_asm_size_guess();
	rseq_bug("cpu_id comparison failed");
error2:
	rseq_workaround_gcc_asm_size_guess();
	rseq_bug("expected value comparison failed");
#endif
}

static inline __attribute__((always_inline))
int rseq_cmpeqv_trymemcpy_storev_release(intptr_t *v, intptr_t expect,
					 void *dst, void *src, size_t len,
					 intptr_t newv, int cpu)
{
	uint32_t rseq_scratch[3];

	RSEQ_INJECT_C(9)

	rseq_workaround_gcc_asm_size_guess();
	__asm__ __volatile__ goto (
		RSEQ_ASM_DEFINE_TABLE(9, 1f, 2f, 4f) /* start, commit, abort */
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[cmpfail])
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[error1])
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[error2])
#endif
		"str %[src], %[rseq_scratch0]\n\t"
		"str %[dst], %[rseq_scratch1]\n\t"
		"str %[len], %[rseq_scratch2]\n\t"
		/* Start rseq by storing table entry pointer into rseq_cs. */
		RSEQ_ASM_STORE_RSEQ_CS(1, 3f, rseq_cs)
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
		RSEQ_INJECT_ASM(3)
		"ldr r0, %[v]\n\t"
		"cmp %[expect], r0\n\t"
		"bne 5f\n\t"
		RSEQ_INJECT_ASM(4)
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 6f)
		"ldr r0, %[v]\n\t"
		"cmp %[expect], r0\n\t"
		"bne 7f\n\t"
#endif
		/* try memcpy */
		"cmp %[len], #0\n\t" \
		"beq 333f\n\t" \
		"222:\n\t" \
		"ldrb %%r0, [%[src]]\n\t" \
		"strb %%r0, [%[dst]]\n\t" \
		"adds %[src], #1\n\t" \
		"adds %[dst], #1\n\t" \
		"subs %[len], #1\n\t" \
		"bne 222b\n\t" \
		"333:\n\t" \
		RSEQ_INJECT_ASM(5)
		"dmb\n\t"	/* full mb provides store-release */
		/* final store */
		"str %[newv], %[v]\n\t"
		"2:\n\t"
		RSEQ_INJECT_ASM(6)
		/* teardown */
		"ldr %[len], %[rseq_scratch2]\n\t"
		"ldr %[dst], %[rseq_scratch1]\n\t"
		"ldr %[src], %[rseq_scratch0]\n\t"
		"b 8f\n\t"
		RSEQ_ASM_DEFINE_ABORT(3, 4,
				      /* teardown */
				      "ldr %[len], %[rseq_scratch2]\n\t"
				      "ldr %[dst], %[rseq_scratch1]\n\t"
				      "ldr %[src], %[rseq_scratch0]\n\t",
				      abort, 1b, 2b, 4f)
		RSEQ_ASM_DEFINE_CMPFAIL(5,
					/* teardown */
					"ldr %[len], %[rseq_scratch2]\n\t"
					"ldr %[dst], %[rseq_scratch1]\n\t"
					"ldr %[src], %[rseq_scratch0]\n\t",
					cmpfail)
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_DEFINE_CMPFAIL(6,
					/* teardown */
					"ldr %[len], %[rseq_scratch2]\n\t"
					"ldr %[dst], %[rseq_scratch1]\n\t"
					"ldr %[src], %[rseq_scratch0]\n\t",
					error1)
		RSEQ_ASM_DEFINE_CMPFAIL(7,
					/* teardown */
					"ldr %[len], %[rseq_scratch2]\n\t"
					"ldr %[dst], %[rseq_scratch1]\n\t"
					"ldr %[src], %[rseq_scratch0]\n\t",
					error2)
#endif
		"8:\n\t"
		: /* gcc asm goto does not allow outputs */
		: [cpu_id]		"r" (cpu),
		  [current_cpu_id]	"m" (rseq_get_abi()->cpu_id),
		  [rseq_cs]		"m" (rseq_get_abi()->rseq_cs),
		  /* final store input */
		  [v]			"m" (*v),
		  [expect]		"r" (expect),
		  [newv]		"r" (newv),
		  /* try memcpy input */
		  [dst]			"r" (dst),
		  [src]			"r" (src),
		  [len]			"r" (len),
		  [rseq_scratch0]	"m" (rseq_scratch[0]),
		  [rseq_scratch1]	"m" (rseq_scratch[1]),
		  [rseq_scratch2]	"m" (rseq_scratch[2])
		  RSEQ_INJECT_INPUT
		: "r0", "memory", "cc"
		  RSEQ_INJECT_CLOBBER
		: abort, cmpfail
#ifdef RSEQ_COMPARE_TWICE
		  , error1, error2
#endif
	);
	rseq_workaround_gcc_asm_size_guess();
	return 0;
abort:
	rseq_workaround_gcc_asm_size_guess();
	RSEQ_INJECT_FAILED
	return -1;
cmpfail:
	rseq_workaround_gcc_asm_size_guess();
	return 1;
#ifdef RSEQ_COMPARE_TWICE
error1:
	rseq_workaround_gcc_asm_size_guess();
	rseq_bug("cpu_id comparison failed");
error2:
	rseq_workaround_gcc_asm_size_guess();
	rseq_bug("expected value comparison failed");
#endif
}

#endif /* !RSEQ_SKIP_FASTPATH */
