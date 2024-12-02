/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * rseq-arm64.h
 *
 * (C) Copyright 2016-2018 - Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 * (C) Copyright 2018 - Will Deacon <will.deacon@arm.com>
 */

/*
 * aarch64 -mbig-endian generates mixed endianness code vs data:
 * little-endian code and big-endian data. Ensure the RSEQ_SIG signature
 * matches code endianness.
 */
#define RSEQ_SIG_CODE	0xd428bc00	/* BRK #0x45E0.  */

#ifdef __AARCH64EB__
#define RSEQ_SIG_DATA	0x00bc28d4	/* BRK #0x45E0.  */
#else
#define RSEQ_SIG_DATA	RSEQ_SIG_CODE
#endif

#define RSEQ_SIG	RSEQ_SIG_DATA

#define rseq_smp_mb()	__asm__ __volatile__ ("dmb ish" ::: "memory")
#define rseq_smp_rmb()	__asm__ __volatile__ ("dmb ishld" ::: "memory")
#define rseq_smp_wmb()	__asm__ __volatile__ ("dmb ishst" ::: "memory")

#define rseq_smp_load_acquire(p)						\
__extension__ ({								\
	__typeof(*p) ____p1;							\
	switch (sizeof(*p)) {							\
	case 1:									\
		asm volatile ("ldarb %w0, %1"					\
			: "=r" (*(__u8 *)p)					\
			: "Q" (*p) : "memory");					\
		break;								\
	case 2:									\
		asm volatile ("ldarh %w0, %1"					\
			: "=r" (*(__u16 *)p)					\
			: "Q" (*p) : "memory");					\
		break;								\
	case 4:									\
		asm volatile ("ldar %w0, %1"					\
			: "=r" (*(__u32 *)p)					\
			: "Q" (*p) : "memory");					\
		break;								\
	case 8:									\
		asm volatile ("ldar %0, %1"					\
			: "=r" (*(__u64 *)p)					\
			: "Q" (*p) : "memory");					\
		break;								\
	}									\
	____p1;									\
})

#define rseq_smp_acquire__after_ctrl_dep()	rseq_smp_rmb()

#define rseq_smp_store_release(p, v)						\
do {										\
	switch (sizeof(*p)) {							\
	case 1:									\
		asm volatile ("stlrb %w1, %0"					\
				: "=Q" (*p)					\
				: "r" ((__u8)v)					\
				: "memory");					\
		break;								\
	case 2:									\
		asm volatile ("stlrh %w1, %0"					\
				: "=Q" (*p)					\
				: "r" ((__u16)v)				\
				: "memory");					\
		break;								\
	case 4:									\
		asm volatile ("stlr %w1, %0"					\
				: "=Q" (*p)					\
				: "r" ((__u32)v)				\
				: "memory");					\
		break;								\
	case 8:									\
		asm volatile ("stlr %1, %0"					\
				: "=Q" (*p)					\
				: "r" ((__u64)v)				\
				: "memory");					\
		break;								\
	}									\
} while (0)

#ifdef RSEQ_SKIP_FASTPATH
#include "rseq-skip.h"
#else /* !RSEQ_SKIP_FASTPATH */

#define RSEQ_ASM_TMP_REG32	"w15"
#define RSEQ_ASM_TMP_REG	"x15"
#define RSEQ_ASM_TMP_REG_2	"x14"

#define __RSEQ_ASM_DEFINE_TABLE(label, version, flags, start_ip,		\
				post_commit_offset, abort_ip)			\
	"	.pushsection	__rseq_cs, \"aw\"\n"				\
	"	.balign	32\n"							\
	__rseq_str(label) ":\n"							\
	"	.long	" __rseq_str(version) ", " __rseq_str(flags) "\n"	\
	"	.quad	" __rseq_str(start_ip) ", "				\
			  __rseq_str(post_commit_offset) ", "			\
			  __rseq_str(abort_ip) "\n"				\
	"	.popsection\n\t"						\
	"	.pushsection __rseq_cs_ptr_array, \"aw\"\n"				\
	"	.quad " __rseq_str(label) "b\n"					\
	"	.popsection\n"

#define RSEQ_ASM_DEFINE_TABLE(label, start_ip, post_commit_ip, abort_ip)	\
	__RSEQ_ASM_DEFINE_TABLE(label, 0x0, 0x0, start_ip,			\
				(post_commit_ip - start_ip), abort_ip)

/*
 * Exit points of a rseq critical section consist of all instructions outside
 * of the critical section where a critical section can either branch to or
 * reach through the normal course of its execution. The abort IP and the
 * post-commit IP are already part of the __rseq_cs section and should not be
 * explicitly defined as additional exit points. Knowing all exit points is
 * useful to assist debuggers stepping over the critical section.
 */
#define RSEQ_ASM_DEFINE_EXIT_POINT(start_ip, exit_ip)				\
	"	.pushsection __rseq_exit_point_array, \"aw\"\n"			\
	"	.quad " __rseq_str(start_ip) ", " __rseq_str(exit_ip) "\n"	\
	"	.popsection\n"

#define RSEQ_ASM_STORE_RSEQ_CS(label, cs_label, rseq_cs)			\
	RSEQ_INJECT_ASM(1)							\
	"	adrp	" RSEQ_ASM_TMP_REG ", " __rseq_str(cs_label) "\n"	\
	"	add	" RSEQ_ASM_TMP_REG ", " RSEQ_ASM_TMP_REG		\
			", :lo12:" __rseq_str(cs_label) "\n"			\
	"	str	" RSEQ_ASM_TMP_REG ", %[" __rseq_str(rseq_cs) "]\n"	\
	__rseq_str(label) ":\n"

#define RSEQ_ASM_DEFINE_ABORT(label, abort_label)				\
	"	b	222f\n"							\
	"	.inst 	"	__rseq_str(RSEQ_SIG_CODE) "\n"			\
	__rseq_str(label) ":\n"							\
	"	b	%l[" __rseq_str(abort_label) "]\n"			\
	"222:\n"

#define RSEQ_ASM_OP_STORE(value, var)						\
	"	str	%[" __rseq_str(value) "], %[" __rseq_str(var) "]\n"

#define RSEQ_ASM_OP_STORE_RELEASE(value, var)					\
	"	stlr	%[" __rseq_str(value) "], %[" __rseq_str(var) "]\n"

#define RSEQ_ASM_OP_FINAL_STORE(value, var, post_commit_label)			\
	RSEQ_ASM_OP_STORE(value, var)						\
	__rseq_str(post_commit_label) ":\n"

#define RSEQ_ASM_OP_FINAL_STORE_RELEASE(value, var, post_commit_label)		\
	RSEQ_ASM_OP_STORE_RELEASE(value, var)					\
	__rseq_str(post_commit_label) ":\n"

#define RSEQ_ASM_OP_CMPEQ(var, expect, label)					\
	"	ldr	" RSEQ_ASM_TMP_REG ", %[" __rseq_str(var) "]\n"		\
	"	sub	" RSEQ_ASM_TMP_REG ", " RSEQ_ASM_TMP_REG		\
			", %[" __rseq_str(expect) "]\n"				\
	"	cbnz	" RSEQ_ASM_TMP_REG ", " __rseq_str(label) "\n"

#define RSEQ_ASM_OP_CMPEQ32(var, expect, label)					\
	"	ldr	" RSEQ_ASM_TMP_REG32 ", %[" __rseq_str(var) "]\n"	\
	"	sub	" RSEQ_ASM_TMP_REG32 ", " RSEQ_ASM_TMP_REG32		\
			", %w[" __rseq_str(expect) "]\n"			\
	"	cbnz	" RSEQ_ASM_TMP_REG32 ", " __rseq_str(label) "\n"

#define RSEQ_ASM_OP_CMPNE(var, expect, label)					\
	"	ldr	" RSEQ_ASM_TMP_REG ", %[" __rseq_str(var) "]\n"		\
	"	sub	" RSEQ_ASM_TMP_REG ", " RSEQ_ASM_TMP_REG		\
			", %[" __rseq_str(expect) "]\n"				\
	"	cbz	" RSEQ_ASM_TMP_REG ", " __rseq_str(label) "\n"

#define RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, label)			\
	RSEQ_INJECT_ASM(2)							\
	RSEQ_ASM_OP_CMPEQ32(current_cpu_id, cpu_id, label)

#define RSEQ_ASM_OP_R_LOAD(var)							\
	"	ldr	" RSEQ_ASM_TMP_REG ", %[" __rseq_str(var) "]\n"

#define RSEQ_ASM_OP_R_STORE(var)						\
	"	str	" RSEQ_ASM_TMP_REG ", %[" __rseq_str(var) "]\n"

#define RSEQ_ASM_OP_R_LOAD_OFF(offset)						\
	"	ldr	" RSEQ_ASM_TMP_REG ", [" RSEQ_ASM_TMP_REG		\
			", %[" __rseq_str(offset) "]]\n"

#define RSEQ_ASM_OP_R_ADD(count)						\
	"	add	" RSEQ_ASM_TMP_REG ", " RSEQ_ASM_TMP_REG		\
			", %[" __rseq_str(count) "]\n"

#define RSEQ_ASM_OP_R_FINAL_STORE(var, post_commit_label)			\
	"	str	" RSEQ_ASM_TMP_REG ", %[" __rseq_str(var) "]\n"		\
	__rseq_str(post_commit_label) ":\n"

#define RSEQ_ASM_OP_R_BAD_MEMCPY(dst, src, len)					\
	"	cbz	%[" __rseq_str(len) "], 333f\n"				\
	"	mov	" RSEQ_ASM_TMP_REG_2 ", %[" __rseq_str(len) "]\n"	\
	"222:	sub	" RSEQ_ASM_TMP_REG_2 ", " RSEQ_ASM_TMP_REG_2 ", #1\n"	\
	"	ldrb	" RSEQ_ASM_TMP_REG32 ", [%[" __rseq_str(src) "]"	\
			", " RSEQ_ASM_TMP_REG_2 "]\n"				\
	"	strb	" RSEQ_ASM_TMP_REG32 ", [%[" __rseq_str(dst) "]"	\
			", " RSEQ_ASM_TMP_REG_2 "]\n"				\
	"	cbnz	" RSEQ_ASM_TMP_REG_2 ", 222b\n"				\
	"333:\n"

static inline __attribute__((always_inline))
int rseq_cmpeqv_storev(intptr_t *v, intptr_t expect, intptr_t newv, int cpu)
{
	RSEQ_INJECT_C(9)

	__asm__ __volatile__ goto (
		RSEQ_ASM_DEFINE_TABLE(1, 2f, 3f, 4f)
		RSEQ_ASM_DEFINE_EXIT_POINT(2f, %l[cmpfail])
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_DEFINE_EXIT_POINT(2f, %l[error1])
		RSEQ_ASM_DEFINE_EXIT_POINT(2f, %l[error2])
#endif
		RSEQ_ASM_STORE_RSEQ_CS(2, 1b, rseq_cs)
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
		RSEQ_INJECT_ASM(3)
		RSEQ_ASM_OP_CMPEQ(v, expect, %l[cmpfail])
		RSEQ_INJECT_ASM(4)
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, %l[error1])
		RSEQ_ASM_OP_CMPEQ(v, expect, %l[error2])
#endif
		RSEQ_ASM_OP_FINAL_STORE(newv, v, 3)
		RSEQ_INJECT_ASM(5)
		RSEQ_ASM_DEFINE_ABORT(4, abort)
		: /* gcc asm goto does not allow outputs */
		: [cpu_id]		"r" (cpu),
		  [current_cpu_id]	"Qo" (rseq_get_abi()->cpu_id),
		  [rseq_cs]		"m" (rseq_get_abi()->rseq_cs.arch.ptr),
		  [v]			"Qo" (*v),
		  [expect]		"r" (expect),
		  [newv]		"r" (newv)
		  RSEQ_INJECT_INPUT
		: "memory", RSEQ_ASM_TMP_REG
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
int rseq_cmpnev_storeoffp_load(intptr_t *v, intptr_t expectnot,
			       long voffp, intptr_t *load, int cpu)
{
	RSEQ_INJECT_C(9)

	__asm__ __volatile__ goto (
		RSEQ_ASM_DEFINE_TABLE(1, 2f, 3f, 4f)
		RSEQ_ASM_DEFINE_EXIT_POINT(2f, %l[cmpfail])
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_DEFINE_EXIT_POINT(2f, %l[error1])
		RSEQ_ASM_DEFINE_EXIT_POINT(2f, %l[error2])
#endif
		RSEQ_ASM_STORE_RSEQ_CS(2, 1b, rseq_cs)
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
		RSEQ_INJECT_ASM(3)
		RSEQ_ASM_OP_CMPNE(v, expectnot, %l[cmpfail])
		RSEQ_INJECT_ASM(4)
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, %l[error1])
		RSEQ_ASM_OP_CMPNE(v, expectnot, %l[error2])
#endif
		RSEQ_ASM_OP_R_LOAD(v)
		RSEQ_ASM_OP_R_STORE(load)
		RSEQ_ASM_OP_R_LOAD_OFF(voffp)
		RSEQ_ASM_OP_R_FINAL_STORE(v, 3)
		RSEQ_INJECT_ASM(5)
		RSEQ_ASM_DEFINE_ABORT(4, abort)
		: /* gcc asm goto does not allow outputs */
		: [cpu_id]		"r" (cpu),
		  [current_cpu_id]	"Qo" (rseq_get_abi()->cpu_id),
		  [rseq_cs]		"m" (rseq_get_abi()->rseq_cs.arch.ptr),
		  [v]			"Qo" (*v),
		  [expectnot]		"r" (expectnot),
		  [load]		"Qo" (*load),
		  [voffp]		"r" (voffp)
		  RSEQ_INJECT_INPUT
		: "memory", RSEQ_ASM_TMP_REG
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
		RSEQ_ASM_DEFINE_TABLE(1, 2f, 3f, 4f)
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_DEFINE_EXIT_POINT(2f, %l[error1])
#endif
		RSEQ_ASM_STORE_RSEQ_CS(2, 1b, rseq_cs)
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
		RSEQ_INJECT_ASM(3)
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, %l[error1])
#endif
		RSEQ_ASM_OP_R_LOAD(v)
		RSEQ_ASM_OP_R_ADD(count)
		RSEQ_ASM_OP_R_FINAL_STORE(v, 3)
		RSEQ_INJECT_ASM(4)
		RSEQ_ASM_DEFINE_ABORT(4, abort)
		: /* gcc asm goto does not allow outputs */
		: [cpu_id]		"r" (cpu),
		  [current_cpu_id]	"Qo" (rseq_get_abi()->cpu_id),
		  [rseq_cs]		"m" (rseq_get_abi()->rseq_cs.arch.ptr),
		  [v]			"Qo" (*v),
		  [count]		"r" (count)
		  RSEQ_INJECT_INPUT
		: "memory", RSEQ_ASM_TMP_REG
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
		RSEQ_ASM_DEFINE_TABLE(1, 2f, 3f, 4f)
		RSEQ_ASM_DEFINE_EXIT_POINT(2f, %l[cmpfail])
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_DEFINE_EXIT_POINT(2f, %l[error1])
		RSEQ_ASM_DEFINE_EXIT_POINT(2f, %l[error2])
#endif
		RSEQ_ASM_STORE_RSEQ_CS(2, 1b, rseq_cs)
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
		RSEQ_INJECT_ASM(3)
		RSEQ_ASM_OP_CMPEQ(v, expect, %l[cmpfail])
		RSEQ_INJECT_ASM(4)
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, %l[error1])
		RSEQ_ASM_OP_CMPEQ(v, expect, %l[error2])
#endif
		RSEQ_ASM_OP_STORE(newv2, v2)
		RSEQ_INJECT_ASM(5)
		RSEQ_ASM_OP_FINAL_STORE(newv, v, 3)
		RSEQ_INJECT_ASM(6)
		RSEQ_ASM_DEFINE_ABORT(4, abort)
		: /* gcc asm goto does not allow outputs */
		: [cpu_id]		"r" (cpu),
		  [current_cpu_id]	"Qo" (rseq_get_abi()->cpu_id),
		  [rseq_cs]		"m" (rseq_get_abi()->rseq_cs.arch.ptr),
		  [expect]		"r" (expect),
		  [v]			"Qo" (*v),
		  [newv]		"r" (newv),
		  [v2]			"Qo" (*v2),
		  [newv2]		"r" (newv2)
		  RSEQ_INJECT_INPUT
		: "memory", RSEQ_ASM_TMP_REG
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
int rseq_cmpeqv_trystorev_storev_release(intptr_t *v, intptr_t expect,
					 intptr_t *v2, intptr_t newv2,
					 intptr_t newv, int cpu)
{
	RSEQ_INJECT_C(9)

	__asm__ __volatile__ goto (
		RSEQ_ASM_DEFINE_TABLE(1, 2f, 3f, 4f)
		RSEQ_ASM_DEFINE_EXIT_POINT(2f, %l[cmpfail])
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_DEFINE_EXIT_POINT(2f, %l[error1])
		RSEQ_ASM_DEFINE_EXIT_POINT(2f, %l[error2])
#endif
		RSEQ_ASM_STORE_RSEQ_CS(2, 1b, rseq_cs)
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
		RSEQ_INJECT_ASM(3)
		RSEQ_ASM_OP_CMPEQ(v, expect, %l[cmpfail])
		RSEQ_INJECT_ASM(4)
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, %l[error1])
		RSEQ_ASM_OP_CMPEQ(v, expect, %l[error2])
#endif
		RSEQ_ASM_OP_STORE(newv2, v2)
		RSEQ_INJECT_ASM(5)
		RSEQ_ASM_OP_FINAL_STORE_RELEASE(newv, v, 3)
		RSEQ_INJECT_ASM(6)
		RSEQ_ASM_DEFINE_ABORT(4, abort)
		: /* gcc asm goto does not allow outputs */
		: [cpu_id]		"r" (cpu),
		  [current_cpu_id]	"Qo" (rseq_get_abi()->cpu_id),
		  [rseq_cs]		"m" (rseq_get_abi()->rseq_cs.arch.ptr),
		  [expect]		"r" (expect),
		  [v]			"Qo" (*v),
		  [newv]		"r" (newv),
		  [v2]			"Qo" (*v2),
		  [newv2]		"r" (newv2)
		  RSEQ_INJECT_INPUT
		: "memory", RSEQ_ASM_TMP_REG
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
int rseq_cmpeqv_cmpeqv_storev(intptr_t *v, intptr_t expect,
			      intptr_t *v2, intptr_t expect2,
			      intptr_t newv, int cpu)
{
	RSEQ_INJECT_C(9)

	__asm__ __volatile__ goto (
		RSEQ_ASM_DEFINE_TABLE(1, 2f, 3f, 4f)
		RSEQ_ASM_DEFINE_EXIT_POINT(2f, %l[cmpfail])
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_DEFINE_EXIT_POINT(2f, %l[error1])
		RSEQ_ASM_DEFINE_EXIT_POINT(2f, %l[error2])
		RSEQ_ASM_DEFINE_EXIT_POINT(2f, %l[error3])
#endif
		RSEQ_ASM_STORE_RSEQ_CS(2, 1b, rseq_cs)
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
		RSEQ_INJECT_ASM(3)
		RSEQ_ASM_OP_CMPEQ(v, expect, %l[cmpfail])
		RSEQ_INJECT_ASM(4)
		RSEQ_ASM_OP_CMPEQ(v2, expect2, %l[cmpfail])
		RSEQ_INJECT_ASM(5)
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, %l[error1])
		RSEQ_ASM_OP_CMPEQ(v, expect, %l[error2])
		RSEQ_ASM_OP_CMPEQ(v2, expect2, %l[error3])
#endif
		RSEQ_ASM_OP_FINAL_STORE(newv, v, 3)
		RSEQ_INJECT_ASM(6)
		RSEQ_ASM_DEFINE_ABORT(4, abort)
		: /* gcc asm goto does not allow outputs */
		: [cpu_id]		"r" (cpu),
		  [current_cpu_id]	"Qo" (rseq_get_abi()->cpu_id),
		  [rseq_cs]		"m" (rseq_get_abi()->rseq_cs.arch.ptr),
		  [v]			"Qo" (*v),
		  [expect]		"r" (expect),
		  [v2]			"Qo" (*v2),
		  [expect2]		"r" (expect2),
		  [newv]		"r" (newv)
		  RSEQ_INJECT_INPUT
		: "memory", RSEQ_ASM_TMP_REG
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
	rseq_bug("expected value comparison failed");
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
	RSEQ_INJECT_C(9)

	__asm__ __volatile__ goto (
		RSEQ_ASM_DEFINE_TABLE(1, 2f, 3f, 4f)
		RSEQ_ASM_DEFINE_EXIT_POINT(2f, %l[cmpfail])
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_DEFINE_EXIT_POINT(2f, %l[error1])
		RSEQ_ASM_DEFINE_EXIT_POINT(2f, %l[error2])
#endif
		RSEQ_ASM_STORE_RSEQ_CS(2, 1b, rseq_cs)
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
		RSEQ_INJECT_ASM(3)
		RSEQ_ASM_OP_CMPEQ(v, expect, %l[cmpfail])
		RSEQ_INJECT_ASM(4)
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, %l[error1])
		RSEQ_ASM_OP_CMPEQ(v, expect, %l[error2])
#endif
		RSEQ_ASM_OP_R_BAD_MEMCPY(dst, src, len)
		RSEQ_INJECT_ASM(5)
		RSEQ_ASM_OP_FINAL_STORE(newv, v, 3)
		RSEQ_INJECT_ASM(6)
		RSEQ_ASM_DEFINE_ABORT(4, abort)
		: /* gcc asm goto does not allow outputs */
		: [cpu_id]		"r" (cpu),
		  [current_cpu_id]	"Qo" (rseq_get_abi()->cpu_id),
		  [rseq_cs]		"m" (rseq_get_abi()->rseq_cs.arch.ptr),
		  [expect]		"r" (expect),
		  [v]			"Qo" (*v),
		  [newv]		"r" (newv),
		  [dst]			"r" (dst),
		  [src]			"r" (src),
		  [len]			"r" (len)
		  RSEQ_INJECT_INPUT
		: "memory", RSEQ_ASM_TMP_REG, RSEQ_ASM_TMP_REG_2
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
int rseq_cmpeqv_trymemcpy_storev_release(intptr_t *v, intptr_t expect,
					 void *dst, void *src, size_t len,
					 intptr_t newv, int cpu)
{
	RSEQ_INJECT_C(9)

	__asm__ __volatile__ goto (
		RSEQ_ASM_DEFINE_TABLE(1, 2f, 3f, 4f)
		RSEQ_ASM_DEFINE_EXIT_POINT(2f, %l[cmpfail])
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_DEFINE_EXIT_POINT(2f, %l[error1])
		RSEQ_ASM_DEFINE_EXIT_POINT(2f, %l[error2])
#endif
		RSEQ_ASM_STORE_RSEQ_CS(2, 1b, rseq_cs)
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
		RSEQ_INJECT_ASM(3)
		RSEQ_ASM_OP_CMPEQ(v, expect, %l[cmpfail])
		RSEQ_INJECT_ASM(4)
#ifdef RSEQ_COMPARE_TWICE
		RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, %l[error1])
		RSEQ_ASM_OP_CMPEQ(v, expect, %l[error2])
#endif
		RSEQ_ASM_OP_R_BAD_MEMCPY(dst, src, len)
		RSEQ_INJECT_ASM(5)
		RSEQ_ASM_OP_FINAL_STORE_RELEASE(newv, v, 3)
		RSEQ_INJECT_ASM(6)
		RSEQ_ASM_DEFINE_ABORT(4, abort)
		: /* gcc asm goto does not allow outputs */
		: [cpu_id]		"r" (cpu),
		  [current_cpu_id]	"Qo" (rseq_get_abi()->cpu_id),
		  [rseq_cs]		"m" (rseq_get_abi()->rseq_cs.arch.ptr),
		  [expect]		"r" (expect),
		  [v]			"Qo" (*v),
		  [newv]		"r" (newv),
		  [dst]			"r" (dst),
		  [src]			"r" (src),
		  [len]			"r" (len)
		  RSEQ_INJECT_INPUT
		: "memory", RSEQ_ASM_TMP_REG, RSEQ_ASM_TMP_REG_2
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

#endif /* !RSEQ_SKIP_FASTPATH */
