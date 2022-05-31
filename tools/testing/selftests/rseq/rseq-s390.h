/* SPDX-License-Identifier: LGPL-2.1 OR MIT */

/*
 * RSEQ_SIG uses the trap4 instruction. As Linux does not make use of the
 * access-register mode nor the linkage stack this instruction will always
 * cause a special-operation exception (the trap-enabled bit in the DUCT
 * is and will stay 0). The instruction pattern is
 *	b2 ff 0f ff	trap4	4095(%r0)
 */
#define RSEQ_SIG	0xB2FF0FFF

#define rseq_smp_mb()	__asm__ __volatile__ ("bcr 15,0" ::: "memory")
#define rseq_smp_rmb()	rseq_smp_mb()
#define rseq_smp_wmb()	rseq_smp_mb()

#define rseq_smp_load_acquire(p)					\
__extension__ ({							\
	__typeof(*p) ____p1 = RSEQ_READ_ONCE(*p);			\
	rseq_barrier();							\
	____p1;								\
})

#define rseq_smp_acquire__after_ctrl_dep()	rseq_smp_rmb()

#define rseq_smp_store_release(p, v)					\
do {									\
	rseq_barrier();							\
	RSEQ_WRITE_ONCE(*p, v);						\
} while (0)

#ifdef RSEQ_SKIP_FASTPATH
#include "rseq-skip.h"
#else /* !RSEQ_SKIP_FASTPATH */

#ifdef __s390x__

#define LONG_L			"lg"
#define LONG_S			"stg"
#define LONG_LT_R		"ltgr"
#define LONG_CMP		"cg"
#define LONG_CMP_R		"cgr"
#define LONG_ADDI		"aghi"
#define LONG_ADD_R		"agr"

#define __RSEQ_ASM_DEFINE_TABLE(label, version, flags,			\
				start_ip, post_commit_offset, abort_ip)	\
		".pushsection __rseq_cs, \"aw\"\n\t"			\
		".balign 32\n\t"					\
		__rseq_str(label) ":\n\t"				\
		".long " __rseq_str(version) ", " __rseq_str(flags) "\n\t" \
		".quad " __rseq_str(start_ip) ", " __rseq_str(post_commit_offset) ", " __rseq_str(abort_ip) "\n\t" \
		".popsection\n\t"					\
		".pushsection __rseq_cs_ptr_array, \"aw\"\n\t"		\
		".quad " __rseq_str(label) "b\n\t"			\
		".popsection\n\t"

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

#elif __s390__

#define __RSEQ_ASM_DEFINE_TABLE(label, version, flags,			\
				start_ip, post_commit_offset, abort_ip)	\
		".pushsection __rseq_cs, \"aw\"\n\t"			\
		".balign 32\n\t"					\
		__rseq_str(label) ":\n\t"				\
		".long " __rseq_str(version) ", " __rseq_str(flags) "\n\t" \
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
#define RSEQ_ASM_DEFINE_EXIT_POINT(start_ip, exit_ip)			\
		".pushsection __rseq_exit_point_array, \"aw\"\n\t"	\
		".long 0x0, " __rseq_str(start_ip) ", 0x0, " __rseq_str(exit_ip) "\n\t" \
		".popsection\n\t"

#define LONG_L			"l"
#define LONG_S			"st"
#define LONG_LT_R		"ltr"
#define LONG_CMP		"c"
#define LONG_CMP_R		"cr"
#define LONG_ADDI		"ahi"
#define LONG_ADD_R		"ar"

#endif

#define RSEQ_ASM_DEFINE_TABLE(label, start_ip, post_commit_ip, abort_ip) \
	__RSEQ_ASM_DEFINE_TABLE(label, 0x0, 0x0, start_ip,		\
				(post_commit_ip - start_ip), abort_ip)

#define RSEQ_ASM_STORE_RSEQ_CS(label, cs_label, rseq_cs)		\
		RSEQ_INJECT_ASM(1)					\
		"larl %%r0, " __rseq_str(cs_label) "\n\t"		\
		LONG_S " %%r0, %[" __rseq_str(rseq_cs) "]\n\t"		\
		__rseq_str(label) ":\n\t"

#define RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, label)		\
		RSEQ_INJECT_ASM(2)					\
		"c %[" __rseq_str(cpu_id) "], %[" __rseq_str(current_cpu_id) "]\n\t" \
		"jnz " __rseq_str(label) "\n\t"

#define RSEQ_ASM_DEFINE_ABORT(label, teardown, abort_label)		\
		".pushsection __rseq_failure, \"ax\"\n\t"		\
		".long " __rseq_str(RSEQ_SIG) "\n\t"			\
		__rseq_str(label) ":\n\t"				\
		teardown						\
		"jg %l[" __rseq_str(abort_label) "]\n\t"		\
		".popsection\n\t"

#define RSEQ_ASM_DEFINE_CMPFAIL(label, teardown, cmpfail_label)		\
		".pushsection __rseq_failure, \"ax\"\n\t"		\
		__rseq_str(label) ":\n\t"				\
		teardown						\
		"jg %l[" __rseq_str(cmpfail_label) "]\n\t"		\
		".popsection\n\t"

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
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
		RSEQ_INJECT_ASM(3)
		LONG_CMP " %[expect], %[v]\n\t"
		"jnz %l[cmpfail]\n\t"
		RSEQ_INJECT_ASM(4)
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, %l[error1])
		LONG_CMP " %[expect], %[v]\n\t"
		"jnz %l[error2]\n\t"
#endif
		/* final store */
		LONG_S " %[newv], %[v]\n\t"
		"2:\n\t"
		RSEQ_INJECT_ASM(5)
		RSEQ_ASM_DEFINE_ABORT(4, "", abort)
		: /* gcc asm goto does not allow outputs */
		: [cpu_id]		"r" (cpu),
		  [current_cpu_id]	"m" (rseq_get_abi()->cpu_id),
		  [rseq_cs]		"m" (rseq_get_abi()->rseq_cs.arch.ptr),
		  [v]			"m" (*v),
		  [expect]		"r" (expect),
		  [newv]		"r" (newv)
		  RSEQ_INJECT_INPUT
		: "memory", "cc", "r0"
		  RSEQ_INJECT_CLOBBER
		: abort, cmpfail
#ifdef RSEQ_COMPARE_TWICE
		  , error1, error2
#endif
	);
	rseq_after_asm_goto();
	return 0;
abort:
	rseq_after_asm_goto();
	RSEQ_INJECT_FAILED
	return -1;
cmpfail:
	rseq_after_asm_goto();
	return 1;
#ifdef RSEQ_COMPARE_TWICE
error1:
	rseq_after_asm_goto();
	rseq_bug("cpu_id comparison failed");
error2:
	rseq_after_asm_goto();
	rseq_bug("expected value comparison failed");
#endif
}

/*
 * Compare @v against @expectnot. When it does _not_ match, load @v
 * into @load, and store the content of *@v + voffp into @v.
 */
static inline __attribute__((always_inline))
int rseq_cmpnev_storeoffp_load(intptr_t *v, intptr_t expectnot,
			       long voffp, intptr_t *load, int cpu)
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
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
		RSEQ_INJECT_ASM(3)
		LONG_L " %%r1, %[v]\n\t"
		LONG_CMP_R " %%r1, %[expectnot]\n\t"
		"je %l[cmpfail]\n\t"
		RSEQ_INJECT_ASM(4)
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, %l[error1])
		LONG_L " %%r1, %[v]\n\t"
		LONG_CMP_R " %%r1, %[expectnot]\n\t"
		"je %l[error2]\n\t"
#endif
		LONG_S " %%r1, %[load]\n\t"
		LONG_ADD_R " %%r1, %[voffp]\n\t"
		LONG_L " %%r1, 0(%%r1)\n\t"
		/* final store */
		LONG_S " %%r1, %[v]\n\t"
		"2:\n\t"
		RSEQ_INJECT_ASM(5)
		RSEQ_ASM_DEFINE_ABORT(4, "", abort)
		: /* gcc asm goto does not allow outputs */
		: [cpu_id]		"r" (cpu),
		  [current_cpu_id]	"m" (rseq_get_abi()->cpu_id),
		  [rseq_cs]		"m" (rseq_get_abi()->rseq_cs.arch.ptr),
		  /* final store input */
		  [v]			"m" (*v),
		  [expectnot]		"r" (expectnot),
		  [voffp]		"r" (voffp),
		  [load]		"m" (*load)
		  RSEQ_INJECT_INPUT
		: "memory", "cc", "r0", "r1"
		  RSEQ_INJECT_CLOBBER
		: abort, cmpfail
#ifdef RSEQ_COMPARE_TWICE
		  , error1, error2
#endif
	);
	rseq_after_asm_goto();
	return 0;
abort:
	rseq_after_asm_goto();
	RSEQ_INJECT_FAILED
	return -1;
cmpfail:
	rseq_after_asm_goto();
	return 1;
#ifdef RSEQ_COMPARE_TWICE
error1:
	rseq_after_asm_goto();
	rseq_bug("cpu_id comparison failed");
error2:
	rseq_after_asm_goto();
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
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
		RSEQ_INJECT_ASM(3)
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, %l[error1])
#endif
		LONG_L " %%r0, %[v]\n\t"
		LONG_ADD_R " %%r0, %[count]\n\t"
		/* final store */
		LONG_S " %%r0, %[v]\n\t"
		"2:\n\t"
		RSEQ_INJECT_ASM(4)
		RSEQ_ASM_DEFINE_ABORT(4, "", abort)
		: /* gcc asm goto does not allow outputs */
		: [cpu_id]		"r" (cpu),
		  [current_cpu_id]	"m" (rseq_get_abi()->cpu_id),
		  [rseq_cs]		"m" (rseq_get_abi()->rseq_cs.arch.ptr),
		  /* final store input */
		  [v]			"m" (*v),
		  [count]		"r" (count)
		  RSEQ_INJECT_INPUT
		: "memory", "cc", "r0"
		  RSEQ_INJECT_CLOBBER
		: abort
#ifdef RSEQ_COMPARE_TWICE
		  , error1
#endif
	);
	rseq_after_asm_goto();
	return 0;
abort:
	rseq_after_asm_goto();
	RSEQ_INJECT_FAILED
	return -1;
#ifdef RSEQ_COMPARE_TWICE
error1:
	rseq_after_asm_goto();
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
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
		RSEQ_INJECT_ASM(3)
		LONG_CMP " %[expect], %[v]\n\t"
		"jnz %l[cmpfail]\n\t"
		RSEQ_INJECT_ASM(4)
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, %l[error1])
		LONG_CMP " %[expect], %[v]\n\t"
		"jnz %l[error2]\n\t"
#endif
		/* try store */
		LONG_S " %[newv2], %[v2]\n\t"
		RSEQ_INJECT_ASM(5)
		/* final store */
		LONG_S " %[newv], %[v]\n\t"
		"2:\n\t"
		RSEQ_INJECT_ASM(6)
		RSEQ_ASM_DEFINE_ABORT(4, "", abort)
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
		: "memory", "cc", "r0"
		  RSEQ_INJECT_CLOBBER
		: abort, cmpfail
#ifdef RSEQ_COMPARE_TWICE
		  , error1, error2
#endif
	);
	rseq_after_asm_goto();
	return 0;
abort:
	rseq_after_asm_goto();
	RSEQ_INJECT_FAILED
	return -1;
cmpfail:
	rseq_after_asm_goto();
	return 1;
#ifdef RSEQ_COMPARE_TWICE
error1:
	rseq_after_asm_goto();
	rseq_bug("cpu_id comparison failed");
error2:
	rseq_after_asm_goto();
	rseq_bug("expected value comparison failed");
#endif
}

/* s390 is TSO. */
static inline __attribute__((always_inline))
int rseq_cmpeqv_trystorev_storev_release(intptr_t *v, intptr_t expect,
					 intptr_t *v2, intptr_t newv2,
					 intptr_t newv, int cpu)
{
	return rseq_cmpeqv_trystorev_storev(v, expect, v2, newv2, newv, cpu);
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
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
		RSEQ_INJECT_ASM(3)
		LONG_CMP " %[expect], %[v]\n\t"
		"jnz %l[cmpfail]\n\t"
		RSEQ_INJECT_ASM(4)
		LONG_CMP " %[expect2], %[v2]\n\t"
		"jnz %l[cmpfail]\n\t"
		RSEQ_INJECT_ASM(5)
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, %l[error1])
		LONG_CMP " %[expect], %[v]\n\t"
		"jnz %l[error2]\n\t"
		LONG_CMP " %[expect2], %[v2]\n\t"
		"jnz %l[error3]\n\t"
#endif
		/* final store */
		LONG_S " %[newv], %[v]\n\t"
		"2:\n\t"
		RSEQ_INJECT_ASM(6)
		RSEQ_ASM_DEFINE_ABORT(4, "", abort)
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
		: "memory", "cc", "r0"
		  RSEQ_INJECT_CLOBBER
		: abort, cmpfail
#ifdef RSEQ_COMPARE_TWICE
		  , error1, error2, error3
#endif
	);
	rseq_after_asm_goto();
	return 0;
abort:
	rseq_after_asm_goto();
	RSEQ_INJECT_FAILED
	return -1;
cmpfail:
	rseq_after_asm_goto();
	return 1;
#ifdef RSEQ_COMPARE_TWICE
error1:
	rseq_after_asm_goto();
	rseq_bug("cpu_id comparison failed");
error2:
	rseq_after_asm_goto();
	rseq_bug("1st expected value comparison failed");
error3:
	rseq_after_asm_goto();
	rseq_bug("2nd expected value comparison failed");
#endif
}

static inline __attribute__((always_inline))
int rseq_cmpeqv_trymemcpy_storev(intptr_t *v, intptr_t expect,
				 void *dst, void *src, size_t len,
				 intptr_t newv, int cpu)
{
	uint64_t rseq_scratch[3];

	RSEQ_INJECT_C(9)

	__asm__ __volatile__ goto (
		RSEQ_ASM_DEFINE_TABLE(3, 1f, 2f, 4f) /* start, commit, abort */
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[cmpfail])
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[error1])
		RSEQ_ASM_DEFINE_EXIT_POINT(1f, %l[error2])
#endif
		LONG_S " %[src], %[rseq_scratch0]\n\t"
		LONG_S " %[dst], %[rseq_scratch1]\n\t"
		LONG_S " %[len], %[rseq_scratch2]\n\t"
		/* Start rseq by storing table entry pointer into rseq_cs. */
		RSEQ_ASM_STORE_RSEQ_CS(1, 3b, rseq_cs)
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
		RSEQ_INJECT_ASM(3)
		LONG_CMP " %[expect], %[v]\n\t"
		"jnz 5f\n\t"
		RSEQ_INJECT_ASM(4)
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 6f)
		LONG_CMP " %[expect], %[v]\n\t"
		"jnz 7f\n\t"
#endif
		/* try memcpy */
		LONG_LT_R " %[len], %[len]\n\t"
		"jz 333f\n\t"
		"222:\n\t"
		"ic %%r0,0(%[src])\n\t"
		"stc %%r0,0(%[dst])\n\t"
		LONG_ADDI " %[src], 1\n\t"
		LONG_ADDI " %[dst], 1\n\t"
		LONG_ADDI " %[len], -1\n\t"
		"jnz 222b\n\t"
		"333:\n\t"
		RSEQ_INJECT_ASM(5)
		/* final store */
		LONG_S " %[newv], %[v]\n\t"
		"2:\n\t"
		RSEQ_INJECT_ASM(6)
		/* teardown */
		LONG_L " %[len], %[rseq_scratch2]\n\t"
		LONG_L " %[dst], %[rseq_scratch1]\n\t"
		LONG_L " %[src], %[rseq_scratch0]\n\t"
		RSEQ_ASM_DEFINE_ABORT(4,
			LONG_L " %[len], %[rseq_scratch2]\n\t"
			LONG_L " %[dst], %[rseq_scratch1]\n\t"
			LONG_L " %[src], %[rseq_scratch0]\n\t",
			abort)
		RSEQ_ASM_DEFINE_CMPFAIL(5,
			LONG_L " %[len], %[rseq_scratch2]\n\t"
			LONG_L " %[dst], %[rseq_scratch1]\n\t"
			LONG_L " %[src], %[rseq_scratch0]\n\t",
			cmpfail)
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_DEFINE_CMPFAIL(6,
			LONG_L " %[len], %[rseq_scratch2]\n\t"
			LONG_L " %[dst], %[rseq_scratch1]\n\t"
			LONG_L " %[src], %[rseq_scratch0]\n\t",
			error1)
		RSEQ_ASM_DEFINE_CMPFAIL(7,
			LONG_L " %[len], %[rseq_scratch2]\n\t"
			LONG_L " %[dst], %[rseq_scratch1]\n\t"
			LONG_L " %[src], %[rseq_scratch0]\n\t",
			error2)
#endif
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
		  [len]			"r" (len),
		  [rseq_scratch0]	"m" (rseq_scratch[0]),
		  [rseq_scratch1]	"m" (rseq_scratch[1]),
		  [rseq_scratch2]	"m" (rseq_scratch[2])
		  RSEQ_INJECT_INPUT
		: "memory", "cc", "r0"
		  RSEQ_INJECT_CLOBBER
		: abort, cmpfail
#ifdef RSEQ_COMPARE_TWICE
		  , error1, error2
#endif
	);
	rseq_after_asm_goto();
	return 0;
abort:
	rseq_after_asm_goto();
	RSEQ_INJECT_FAILED
	return -1;
cmpfail:
	rseq_after_asm_goto();
	return 1;
#ifdef RSEQ_COMPARE_TWICE
error1:
	rseq_after_asm_goto();
	rseq_bug("cpu_id comparison failed");
error2:
	rseq_after_asm_goto();
	rseq_bug("expected value comparison failed");
#endif
}

/* s390 is TSO. */
static inline __attribute__((always_inline))
int rseq_cmpeqv_trymemcpy_storev_release(intptr_t *v, intptr_t expect,
					 void *dst, void *src, size_t len,
					 intptr_t newv, int cpu)
{
	return rseq_cmpeqv_trymemcpy_storev(v, expect, dst, src, len,
					    newv, cpu);
}
#endif /* !RSEQ_SKIP_FASTPATH */
