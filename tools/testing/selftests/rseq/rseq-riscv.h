/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * Select the instruction "csrw mhartid, x0" as the RSEQ_SIG. Unlike
 * other architectures, the ebreak instruction has no immediate field for
 * distinguishing purposes. Hence, ebreak is not suitable as RSEQ_SIG.
 * "csrw mhartid, x0" can also satisfy the RSEQ requirement because it
 * is an uncommon instruction and will raise an illegal instruction
 * exception when executed in all modes.
 */
#include <endian.h>

#if defined(__BYTE_ORDER) ? (__BYTE_ORDER == __LITTLE_ENDIAN) : defined(__LITTLE_ENDIAN)
#define RSEQ_SIG   0xf1401073  /* csrr mhartid, x0 */
#else
#error "Currently, RSEQ only supports Little-Endian version"
#endif

#if __riscv_xlen == 64
#define __REG_SEL(a, b)	a
#elif __riscv_xlen == 32
#define __REG_SEL(a, b)	b
#endif

#define REG_L	__REG_SEL("ld ", "lw ")
#define REG_S	__REG_SEL("sd ", "sw ")

#define RISCV_FENCE(p, s) \
	__asm__ __volatile__ ("fence " #p "," #s : : : "memory")
#define rseq_smp_mb()	RISCV_FENCE(rw, rw)
#define rseq_smp_rmb()	RISCV_FENCE(r, r)
#define rseq_smp_wmb()	RISCV_FENCE(w, w)
#define RSEQ_ASM_TMP_REG_1	"t6"
#define RSEQ_ASM_TMP_REG_2	"t5"
#define RSEQ_ASM_TMP_REG_3	"t4"
#define RSEQ_ASM_TMP_REG_4	"t3"

#define rseq_smp_load_acquire(p)					\
__extension__ ({							\
	__typeof(*(p)) ____p1 = RSEQ_READ_ONCE(*(p));			\
	RISCV_FENCE(r, rw)						\
	____p1;								\
})

#define rseq_smp_acquire__after_ctrl_dep()	rseq_smp_rmb()

#define rseq_smp_store_release(p, v)					\
do {									\
	RISCV_FENCE(rw, w);						\
	RSEQ_WRITE_ONCE(*(p), v);						\
} while (0)

#ifdef RSEQ_SKIP_FASTPATH
#include "rseq-skip.h"
#else /* !RSEQ_SKIP_FASTPATH */

#define __RSEQ_ASM_DEFINE_TABLE(label, version, flags, start_ip,	\
				post_commit_offset, abort_ip)		\
	".pushsection	__rseq_cs, \"aw\"\n"				\
	".balign	32\n"						\
	__rseq_str(label) ":\n"						\
	".long	" __rseq_str(version) ", " __rseq_str(flags) "\n"	\
	".quad	" __rseq_str(start_ip) ", "				\
			  __rseq_str(post_commit_offset) ", "		\
			  __rseq_str(abort_ip) "\n"			\
	".popsection\n\t"						\
	".pushsection __rseq_cs_ptr_array, \"aw\"\n"			\
	".quad " __rseq_str(label) "b\n"				\
	".popsection\n"

#define RSEQ_ASM_DEFINE_TABLE(label, start_ip, post_commit_ip, abort_ip) \
	__RSEQ_ASM_DEFINE_TABLE(label, 0x0, 0x0, start_ip,		 \
				((post_commit_ip) - (start_ip)), abort_ip)

/*
 * Exit points of a rseq critical section consist of all instructions outside
 * of the critical section where a critical section can either branch to or
 * reach through the normal course of its execution. The abort IP and the
 * post-commit IP are already part of the __rseq_cs section and should not be
 * explicitly defined as additional exit points. Knowing all exit points is
 * useful to assist debuggers stepping over the critical section.
 */
#define RSEQ_ASM_DEFINE_EXIT_POINT(start_ip, exit_ip)			\
	".pushsection __rseq_exit_point_array, \"aw\"\n"		\
	".quad " __rseq_str(start_ip) ", " __rseq_str(exit_ip) "\n"	\
	".popsection\n"

#define RSEQ_ASM_STORE_RSEQ_CS(label, cs_label, rseq_cs)		\
	RSEQ_INJECT_ASM(1)						\
	"la	" RSEQ_ASM_TMP_REG_1 ", " __rseq_str(cs_label) "\n"	\
	REG_S	RSEQ_ASM_TMP_REG_1 ", %[" __rseq_str(rseq_cs) "]\n"	\
	__rseq_str(label) ":\n"

#define RSEQ_ASM_DEFINE_ABORT(label, abort_label)			\
	"j	222f\n"							\
	".balign	4\n"						\
	".long "	__rseq_str(RSEQ_SIG) "\n"			\
	__rseq_str(label) ":\n"						\
	"j	%l[" __rseq_str(abort_label) "]\n"			\
	"222:\n"

#define RSEQ_ASM_OP_STORE(value, var)					\
	REG_S	"%[" __rseq_str(value) "], %[" __rseq_str(var) "]\n"

#define RSEQ_ASM_OP_CMPEQ(var, expect, label)				\
	REG_L	RSEQ_ASM_TMP_REG_1 ", %[" __rseq_str(var) "]\n"		\
	"bne	" RSEQ_ASM_TMP_REG_1 ", %[" __rseq_str(expect) "] ,"	\
		  __rseq_str(label) "\n"

#define RSEQ_ASM_OP_CMPEQ32(var, expect, label)				\
	"lw	" RSEQ_ASM_TMP_REG_1 ", %[" __rseq_str(var) "]\n"	\
	"bne	" RSEQ_ASM_TMP_REG_1 ", %[" __rseq_str(expect) "] ,"	\
		  __rseq_str(label) "\n"

#define RSEQ_ASM_OP_CMPNE(var, expect, label)				\
	REG_L	RSEQ_ASM_TMP_REG_1 ", %[" __rseq_str(var) "]\n"		\
	"beq	" RSEQ_ASM_TMP_REG_1 ", %[" __rseq_str(expect) "] ,"	\
		  __rseq_str(label) "\n"

#define RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, label)		\
	RSEQ_INJECT_ASM(2)						\
	RSEQ_ASM_OP_CMPEQ32(current_cpu_id, cpu_id, label)

#define RSEQ_ASM_OP_R_LOAD(var)						\
	REG_L	RSEQ_ASM_TMP_REG_1 ", %[" __rseq_str(var) "]\n"

#define RSEQ_ASM_OP_R_STORE(var)					\
	REG_S	RSEQ_ASM_TMP_REG_1 ", %[" __rseq_str(var) "]\n"

#define RSEQ_ASM_OP_R_LOAD_OFF(offset)					\
	"add	" RSEQ_ASM_TMP_REG_1 ", %[" __rseq_str(offset) "], "	\
		 RSEQ_ASM_TMP_REG_1 "\n"				\
	REG_L	RSEQ_ASM_TMP_REG_1 ", (" RSEQ_ASM_TMP_REG_1 ")\n"

#define RSEQ_ASM_OP_R_ADD(count)					\
	"add	" RSEQ_ASM_TMP_REG_1 ", " RSEQ_ASM_TMP_REG_1		\
		", %[" __rseq_str(count) "]\n"

#define RSEQ_ASM_OP_FINAL_STORE(value, var, post_commit_label)		\
	RSEQ_ASM_OP_STORE(value, var)					\
	__rseq_str(post_commit_label) ":\n"

#define RSEQ_ASM_OP_FINAL_STORE_RELEASE(value, var, post_commit_label)	\
	"fence	rw, w\n"						\
	RSEQ_ASM_OP_STORE(value, var)					\
	__rseq_str(post_commit_label) ":\n"

#define RSEQ_ASM_OP_R_FINAL_STORE(var, post_commit_label)		\
	REG_S	RSEQ_ASM_TMP_REG_1 ", %[" __rseq_str(var) "]\n"		\
	__rseq_str(post_commit_label) ":\n"

#define RSEQ_ASM_OP_R_BAD_MEMCPY(dst, src, len)				\
	"beqz	%[" __rseq_str(len) "], 333f\n"				\
	"mv	" RSEQ_ASM_TMP_REG_1 ", %[" __rseq_str(len) "]\n"	\
	"mv	" RSEQ_ASM_TMP_REG_2 ", %[" __rseq_str(src) "]\n"	\
	"mv	" RSEQ_ASM_TMP_REG_3 ", %[" __rseq_str(dst) "]\n"	\
	"222:\n"							\
	"lb	" RSEQ_ASM_TMP_REG_4 ", 0(" RSEQ_ASM_TMP_REG_2 ")\n"	\
	"sb	" RSEQ_ASM_TMP_REG_4 ", 0(" RSEQ_ASM_TMP_REG_3 ")\n"	\
	"addi	" RSEQ_ASM_TMP_REG_1 ", " RSEQ_ASM_TMP_REG_1 ", -1\n"	\
	"addi	" RSEQ_ASM_TMP_REG_2 ", " RSEQ_ASM_TMP_REG_2 ", 1\n"	\
	"addi	" RSEQ_ASM_TMP_REG_3 ", " RSEQ_ASM_TMP_REG_3 ", 1\n"	\
	"bnez	" RSEQ_ASM_TMP_REG_1 ", 222b\n"				\
	"333:\n"

#define RSEQ_ASM_OP_R_DEREF_ADDV(ptr, off, post_commit_label)		\
	"mv	" RSEQ_ASM_TMP_REG_1 ", %[" __rseq_str(ptr) "]\n"	\
	RSEQ_ASM_OP_R_ADD(off)						\
	REG_L	  RSEQ_ASM_TMP_REG_1 ", 0(" RSEQ_ASM_TMP_REG_1 ")\n"	\
	RSEQ_ASM_OP_R_ADD(inc)						\
	__rseq_str(post_commit_label) ":\n"

static inline __always_inline
int rseq_cmpeqv_storev(intptr_t *v, intptr_t expect, intptr_t newv, int cpu)
{
	RSEQ_INJECT_C(9)

	__asm__ __volatile__ goto(RSEQ_ASM_DEFINE_TABLE(1, 2f, 3f, 4f)
				  RSEQ_ASM_DEFINE_EXIT_POINT(2f, "%l[cmpfail]")
#ifdef RSEQ_COMPARE_TWICE
				  RSEQ_ASM_DEFINE_EXIT_POINT(2f, "%l[error1]")
				  RSEQ_ASM_DEFINE_EXIT_POINT(2f, "%l[error2]")
#endif
				  RSEQ_ASM_STORE_RSEQ_CS(2, 1b, rseq_cs)
				  RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
				  RSEQ_INJECT_ASM(3)
				  RSEQ_ASM_OP_CMPEQ(v, expect, "%l[cmpfail]")
				  RSEQ_INJECT_ASM(4)
#ifdef RSEQ_COMPARE_TWICE
				  RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, "%l[error1]")
				  RSEQ_ASM_OP_CMPEQ(v, expect, "%l[error2]")
#endif
				  RSEQ_ASM_OP_FINAL_STORE(newv, v, 3)
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
				  : "memory", RSEQ_ASM_TMP_REG_1
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

static inline __always_inline
int rseq_cmpnev_storeoffp_load(intptr_t *v, intptr_t expectnot,
			       off_t voffp, intptr_t *load, int cpu)
{
	RSEQ_INJECT_C(9)

	__asm__ __volatile__ goto(RSEQ_ASM_DEFINE_TABLE(1, 2f, 3f, 4f)
				  RSEQ_ASM_DEFINE_EXIT_POINT(2f, "%l[cmpfail]")
#ifdef RSEQ_COMPARE_TWICE
				  RSEQ_ASM_DEFINE_EXIT_POINT(2f, "%l[error1]")
				  RSEQ_ASM_DEFINE_EXIT_POINT(2f, "%l[error2]")
#endif
				  RSEQ_ASM_STORE_RSEQ_CS(2, 1b, rseq_cs)
				  RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
				  RSEQ_INJECT_ASM(3)
				  RSEQ_ASM_OP_CMPNE(v, expectnot, "%l[cmpfail]")
				  RSEQ_INJECT_ASM(4)
#ifdef RSEQ_COMPARE_TWICE
				  RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, "%l[error1]")
				  RSEQ_ASM_OP_CMPNE(v, expectnot, "%l[error2]")
#endif
				  RSEQ_ASM_OP_R_LOAD(v)
				  RSEQ_ASM_OP_R_STORE(load)
				  RSEQ_ASM_OP_R_LOAD_OFF(voffp)
				  RSEQ_ASM_OP_R_FINAL_STORE(v, 3)
				  RSEQ_INJECT_ASM(5)
				  RSEQ_ASM_DEFINE_ABORT(4, abort)
				  : /* gcc asm goto does not allow outputs */
				  : [cpu_id]		"r" (cpu),
				    [current_cpu_id]	"m" (rseq_get_abi()->cpu_id),
				    [rseq_cs]		"m" (rseq_get_abi()->rseq_cs.arch.ptr),
				    [v]			"m" (*v),
				    [expectnot]		"r" (expectnot),
				    [load]		"m" (*load),
				    [voffp]		"r" (voffp)
				    RSEQ_INJECT_INPUT
				  : "memory", RSEQ_ASM_TMP_REG_1
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

static inline __always_inline
int rseq_addv(intptr_t *v, intptr_t count, int cpu)
{
	RSEQ_INJECT_C(9)

	__asm__ __volatile__ goto(RSEQ_ASM_DEFINE_TABLE(1, 2f, 3f, 4f)
#ifdef RSEQ_COMPARE_TWICE
				  RSEQ_ASM_DEFINE_EXIT_POINT(2f, "%l[error1]")
#endif
				  RSEQ_ASM_STORE_RSEQ_CS(2, 1b, rseq_cs)
				  RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
				  RSEQ_INJECT_ASM(3)
#ifdef RSEQ_COMPARE_TWICE
				  RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, "%l[error1]")
#endif
				  RSEQ_ASM_OP_R_LOAD(v)
				  RSEQ_ASM_OP_R_ADD(count)
				  RSEQ_ASM_OP_R_FINAL_STORE(v, 3)
				  RSEQ_INJECT_ASM(4)
				  RSEQ_ASM_DEFINE_ABORT(4, abort)
				  : /* gcc asm goto does not allow outputs */
				  : [cpu_id]		"r" (cpu),
				    [current_cpu_id]	"m" (rseq_get_abi()->cpu_id),
				    [rseq_cs]		"m" (rseq_get_abi()->rseq_cs.arch.ptr),
				    [v]			"m" (*v),
				    [count]		"r" (count)
				    RSEQ_INJECT_INPUT
				  : "memory", RSEQ_ASM_TMP_REG_1
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

static inline __always_inline
int rseq_cmpeqv_trystorev_storev(intptr_t *v, intptr_t expect,
				 intptr_t *v2, intptr_t newv2,
				 intptr_t newv, int cpu)
{
	RSEQ_INJECT_C(9)

	__asm__ __volatile__ goto(RSEQ_ASM_DEFINE_TABLE(1, 2f, 3f, 4f)
				  RSEQ_ASM_DEFINE_EXIT_POINT(2f, "%l[cmpfail]")
#ifdef RSEQ_COMPARE_TWICE
				  RSEQ_ASM_DEFINE_EXIT_POINT(2f, "%l[error1]")
				  RSEQ_ASM_DEFINE_EXIT_POINT(2f, "%l[error2]")
#endif
				  RSEQ_ASM_STORE_RSEQ_CS(2, 1b, rseq_cs)
				  RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
				  RSEQ_INJECT_ASM(3)
				  RSEQ_ASM_OP_CMPEQ(v, expect, "%l[cmpfail]")
				  RSEQ_INJECT_ASM(4)
#ifdef RSEQ_COMPARE_TWICE
				  RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, "%l[error1]")
				  RSEQ_ASM_OP_CMPEQ(v, expect, "%l[error2]")
#endif
				  RSEQ_ASM_OP_STORE(newv2, v2)
				  RSEQ_INJECT_ASM(5)
				  RSEQ_ASM_OP_FINAL_STORE(newv, v, 3)
				  RSEQ_INJECT_ASM(6)
				  RSEQ_ASM_DEFINE_ABORT(4, abort)
				  : /* gcc asm goto does not allow outputs */
				  : [cpu_id]		"r" (cpu),
				    [current_cpu_id]	"m" (rseq_get_abi()->cpu_id),
				    [rseq_cs]		"m" (rseq_get_abi()->rseq_cs.arch.ptr),
				    [expect]		"r" (expect),
				    [v]			"m" (*v),
				    [newv]		"r" (newv),
				    [v2]			"m" (*v2),
				    [newv2]		"r" (newv2)
				    RSEQ_INJECT_INPUT
				  : "memory", RSEQ_ASM_TMP_REG_1
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

static inline __always_inline
int rseq_cmpeqv_trystorev_storev_release(intptr_t *v, intptr_t expect,
					 intptr_t *v2, intptr_t newv2,
					 intptr_t newv, int cpu)
{
	RSEQ_INJECT_C(9)

	__asm__ __volatile__ goto(RSEQ_ASM_DEFINE_TABLE(1, 2f, 3f, 4f)
				  RSEQ_ASM_DEFINE_EXIT_POINT(2f, "%l[cmpfail]")
#ifdef RSEQ_COMPARE_TWICE
				  RSEQ_ASM_DEFINE_EXIT_POINT(2f, "%l[error1]")
				  RSEQ_ASM_DEFINE_EXIT_POINT(2f, "%l[error2]")
#endif
				  RSEQ_ASM_STORE_RSEQ_CS(2, 1b, rseq_cs)
				  RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
				  RSEQ_INJECT_ASM(3)
				  RSEQ_ASM_OP_CMPEQ(v, expect, "%l[cmpfail]")
				  RSEQ_INJECT_ASM(4)
#ifdef RSEQ_COMPARE_TWICE
				  RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, "%l[error1]")
				  RSEQ_ASM_OP_CMPEQ(v, expect, "%l[error2]")
#endif
				  RSEQ_ASM_OP_STORE(newv2, v2)
				  RSEQ_INJECT_ASM(5)
				  RSEQ_ASM_OP_FINAL_STORE_RELEASE(newv, v, 3)
				  RSEQ_INJECT_ASM(6)
				  RSEQ_ASM_DEFINE_ABORT(4, abort)
				  : /* gcc asm goto does not allow outputs */
				  : [cpu_id]		"r" (cpu),
				    [current_cpu_id]	"m" (rseq_get_abi()->cpu_id),
				    [rseq_cs]		"m" (rseq_get_abi()->rseq_cs.arch.ptr),
				    [expect]		"r" (expect),
				    [v]			"m" (*v),
				    [newv]		"r" (newv),
				    [v2]			"m" (*v2),
				    [newv2]		"r" (newv2)
				    RSEQ_INJECT_INPUT
				  : "memory", RSEQ_ASM_TMP_REG_1
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

static inline __always_inline
int rseq_cmpeqv_cmpeqv_storev(intptr_t *v, intptr_t expect,
			      intptr_t *v2, intptr_t expect2,
			      intptr_t newv, int cpu)
{
	RSEQ_INJECT_C(9)

	__asm__ __volatile__ goto(RSEQ_ASM_DEFINE_TABLE(1, 2f, 3f, 4f)
				  RSEQ_ASM_DEFINE_EXIT_POINT(2f, "%l[cmpfail]")
#ifdef RSEQ_COMPARE_TWICE
				  RSEQ_ASM_DEFINE_EXIT_POINT(2f, "%l[error1]")
				  RSEQ_ASM_DEFINE_EXIT_POINT(2f, "%l[error2]")
				  RSEQ_ASM_DEFINE_EXIT_POINT(2f, "%l[error3]")
#endif
				  RSEQ_ASM_STORE_RSEQ_CS(2, 1b, rseq_cs)
				  RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
				  RSEQ_INJECT_ASM(3)
				  RSEQ_ASM_OP_CMPEQ(v, expect, "%l[cmpfail]")
				  RSEQ_INJECT_ASM(4)
				  RSEQ_ASM_OP_CMPEQ(v2, expect2, "%l[cmpfail]")
				  RSEQ_INJECT_ASM(5)
#ifdef RSEQ_COMPARE_TWICE
				  RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, "%l[error1]")
				  RSEQ_ASM_OP_CMPEQ(v, expect, "%l[error2]")
				  RSEQ_ASM_OP_CMPEQ(v2, expect2, "%l[error3]")
#endif
				  RSEQ_ASM_OP_FINAL_STORE(newv, v, 3)
				  RSEQ_INJECT_ASM(6)
				  RSEQ_ASM_DEFINE_ABORT(4, abort)
				  : /* gcc asm goto does not allow outputs */
				  : [cpu_id]		"r" (cpu),
				    [current_cpu_id]	"m" (rseq_get_abi()->cpu_id),
				    [rseq_cs]		"m" (rseq_get_abi()->rseq_cs.arch.ptr),
				    [v]			"m" (*v),
				    [expect]		"r" (expect),
				    [v2]			"m" (*v2),
				    [expect2]		"r" (expect2),
				    [newv]		"r" (newv)
				    RSEQ_INJECT_INPUT
				  : "memory", RSEQ_ASM_TMP_REG_1
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
	rseq_bug("expected value comparison failed");
error3:
	rseq_bug("2nd expected value comparison failed");
#endif
}

static inline __always_inline
int rseq_cmpeqv_trymemcpy_storev(intptr_t *v, intptr_t expect,
				 void *dst, void *src, size_t len,
				 intptr_t newv, int cpu)
{
	RSEQ_INJECT_C(9)
	__asm__ __volatile__ goto(RSEQ_ASM_DEFINE_TABLE(1, 2f, 3f, 4f)
				  RSEQ_ASM_DEFINE_EXIT_POINT(2f, "%l[cmpfail]")
#ifdef RSEQ_COMPARE_TWICE
				  RSEQ_ASM_DEFINE_EXIT_POINT(2f, "%l[error1]")
				  RSEQ_ASM_DEFINE_EXIT_POINT(2f, "%l[error2]")
#endif
				  RSEQ_ASM_STORE_RSEQ_CS(2, 1b, rseq_cs)
				  RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
				  RSEQ_INJECT_ASM(3)
				  RSEQ_ASM_OP_CMPEQ(v, expect, "%l[cmpfail]")
				  RSEQ_INJECT_ASM(4)
#ifdef RSEQ_COMPARE_TWICE
				  RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, "%l[error1]")
				  RSEQ_ASM_OP_CMPEQ(v, expect, "%l[error2]")
#endif
				  RSEQ_ASM_OP_R_BAD_MEMCPY(dst, src, len)
				  RSEQ_INJECT_ASM(5)
				  RSEQ_ASM_OP_FINAL_STORE(newv, v, 3)
				  RSEQ_INJECT_ASM(6)
				  RSEQ_ASM_DEFINE_ABORT(4, abort)
				  : /* gcc asm goto does not allow outputs */
				  : [cpu_id]		"r" (cpu),
				    [current_cpu_id]	"m" (rseq_get_abi()->cpu_id),
				    [rseq_cs]		"m" (rseq_get_abi()->rseq_cs.arch.ptr),
				    [expect]		"r" (expect),
				    [v]			"m" (*v),
				    [newv]		"r" (newv),
				    [dst]			"r" (dst),
				    [src]			"r" (src),
				    [len]			"r" (len)
				    RSEQ_INJECT_INPUT
				  : "memory", RSEQ_ASM_TMP_REG_1, RSEQ_ASM_TMP_REG_2,
				    RSEQ_ASM_TMP_REG_3, RSEQ_ASM_TMP_REG_4
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

static inline __always_inline
int rseq_cmpeqv_trymemcpy_storev_release(intptr_t *v, intptr_t expect,
					 void *dst, void *src, size_t len,
					 intptr_t newv, int cpu)
{
	RSEQ_INJECT_C(9)

	__asm__ __volatile__ goto(RSEQ_ASM_DEFINE_TABLE(1, 2f, 3f, 4f)
				  RSEQ_ASM_DEFINE_EXIT_POINT(2f, "%l[cmpfail]")
#ifdef RSEQ_COMPARE_TWICE
				  RSEQ_ASM_DEFINE_EXIT_POINT(2f, "%l[error1]")
				  RSEQ_ASM_DEFINE_EXIT_POINT(2f, "%l[error2]")
#endif
				  RSEQ_ASM_STORE_RSEQ_CS(2, 1b, rseq_cs)
				  RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
				  RSEQ_INJECT_ASM(3)
				  RSEQ_ASM_OP_CMPEQ(v, expect, "%l[cmpfail]")
				  RSEQ_INJECT_ASM(4)
#ifdef RSEQ_COMPARE_TWICE
				  RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, "%l[error1]")
				  RSEQ_ASM_OP_CMPEQ(v, expect, "%l[error2]")
#endif
				  RSEQ_ASM_OP_R_BAD_MEMCPY(dst, src, len)
				  RSEQ_INJECT_ASM(5)
				  RSEQ_ASM_OP_FINAL_STORE_RELEASE(newv, v, 3)
				  RSEQ_INJECT_ASM(6)
				  RSEQ_ASM_DEFINE_ABORT(4, abort)
				  : /* gcc asm goto does not allow outputs */
				  : [cpu_id]		"r" (cpu),
				    [current_cpu_id]	"m" (rseq_get_abi()->cpu_id),
				    [rseq_cs]		"m" (rseq_get_abi()->rseq_cs.arch.ptr),
				    [expect]		"r" (expect),
				    [v]			"m" (*v),
				    [newv]		"r" (newv),
				    [dst]			"r" (dst),
				    [src]			"r" (src),
				    [len]			"r" (len)
				    RSEQ_INJECT_INPUT
				  : "memory", RSEQ_ASM_TMP_REG_1, RSEQ_ASM_TMP_REG_2,
				    RSEQ_ASM_TMP_REG_3, RSEQ_ASM_TMP_REG_4
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

#define RSEQ_ARCH_HAS_OFFSET_DEREF_ADDV

/*
 *   pval = *(ptr+off)
 *  *pval += inc;
 */
static inline __always_inline
int rseq_offset_deref_addv(intptr_t *ptr, off_t off, intptr_t inc, int cpu)
{
	RSEQ_INJECT_C(9)

	__asm__ __volatile__ goto(RSEQ_ASM_DEFINE_TABLE(1, 2f, 3f, 4f)
#ifdef RSEQ_COMPARE_TWICE
				  RSEQ_ASM_DEFINE_EXIT_POINT(2f, "%l[error1]")
#endif
				  RSEQ_ASM_STORE_RSEQ_CS(2, 1b, rseq_cs)
				  RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, 4f)
				  RSEQ_INJECT_ASM(3)
#ifdef RSEQ_COMPARE_TWICE
				  RSEQ_ASM_CMP_CPU_ID(cpu_id, current_cpu_id, "%l[error1]")
#endif
				  RSEQ_ASM_OP_R_DEREF_ADDV(ptr, off, 3)
				  RSEQ_INJECT_ASM(4)
				  RSEQ_ASM_DEFINE_ABORT(4, abort)
				  : /* gcc asm goto does not allow outputs */
				  : [cpu_id]		"r" (cpu),
				    [current_cpu_id]      "m" (rseq_get_abi()->cpu_id),
				    [rseq_cs]		"m" (rseq_get_abi()->rseq_cs.arch.ptr),
				    [ptr]			"r" (ptr),
				    [off]			"er" (off),
				    [inc]			"er" (inc)
				    RSEQ_INJECT_INPUT
				  : "memory", RSEQ_ASM_TMP_REG_1
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

#endif /* !RSEQ_SKIP_FASTPATH */
